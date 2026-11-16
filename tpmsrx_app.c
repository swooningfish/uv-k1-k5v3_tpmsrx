/* Copyright 2026 — Apache-2.0
 * TPMS RX: Ford / Toyota FSK and Schrader OOK on PA4 at 38.4 kHz.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app_api.h"
#include "tpmsrx_assets.h"
#include "dectpms.c"

/**
 * Copy bytes. The overlay is freestanding and has no libc.
 *
 * @param d Destination.
 * @param s Source.
 * @param n Number of bytes.
 * @return The destination.
 */
void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *D = d;
    const uint8_t *S = s;

    while (n--) {
        *D++ = *S++;
    }
    return d;
}
/**
 * Fill bytes. The overlay is freestanding and has no libc.
 *
 * @param d Destination.
 * @param c Byte value, converted to unsigned char.
 * @param n Number of bytes.
 * @return The destination.
 */
void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;

    while (n) {
        p[--n] = (uint8_t)c;
    }
    return d;
}

#define FS             38400u
#define CYC_PER_SAMPLE (48000000u / FS)
#define REG_2B         0x2B
#define REG_73         0x73
#define REG_43         0x43
#define REG_43_WIDE    0x7E2Cu
#define REG_47         0x47
#define REG_47_BYPASS  0x6942u
#define REG_33         0x33
#define REG_38         0x38
#define REG_39         0x39
/* BK4819_ToggleGpioOut: bit is 0x40 >> pin. GPIO3 UHF = 3, GPIO4 VHF = 4. */
#define LNA_UHF        0x0008u
#define LNA_VHF        0x0004u
#define FREQ_315       31500000u
#define FREQ_433       43392000u
#define KEY_EVERY      1536u
#define DRAW_HOLD_MS   200u
#define VIEW_EVERY_MS  500u
#define LED_STEP       (FS / 10u)
#define CFG_MAGIC      0xB3u
/* Sensitivity: 0 low, 1 mid (the old fixed gate), 2 high. */
#define SENS_LEVELS    3u
#define SENS_DEFAULT   1u
#define ROWS           7u
/* One demod per app: EU / NA are FSK (Ford, Toyota), EU2 / NA2 are OOK. */
#if TPMS_HAVE_FSK
#define TPMS_PATH      TPMS_MODE_FSK
#else
#define TPMS_PATH      TPMS_MODE_OOK
#endif
/* EU2 is fixed at 433.92 and NA2 at 315.00, so neither shows BAND.
 * EU / NA can change band. */
#if defined(TPMS_FLAVOUR_EU2)
#define TPMS_LOCK_BAND 1
#define TPMS_BAND433   1u
#define MENU_SENS      0u
#define MENU_CLR       1u
#elif defined(TPMS_FLAVOUR_NA2)
#define TPMS_LOCK_BAND 1
#define TPMS_BAND433   0u
#define MENU_SENS      0u
#define MENU_CLR       1u
#else
#define TPMS_LOCK_BAND 0
#define MENU_BAND      0u
#define MENU_SENS      1u
#define MENU_CLR       2u
#endif
/* Saved display mode. 1 was the old "backlight off" mode and now loads as
 * DISP_TMO. DISP_ON forces the lamp on and shows the bulb. */
#define DISP_TMO       2u
#define DISP_ON        3u
#define DISP_IDLE_MS   10000u
/* Radio backlight time in storage (gEeprom.BACKLIGHT_TIME, firmware v6.3.0
 * settings block 0xA008 byte 5). 0 keeps the lamp off, 61 is always on. */
#define BL_TIME_ADDR   0x00A00Du
#define BL_ALWAYS      61u
#define BATT_RESERVE   48u
/* OOK chip (120 us) at which RSSI is read: about 2 ms into the burst.
 * That is inside a Schrader frame (it has no preamble), but the host test
 * shows the slicer rides out a read that stalls sampling for up to 9
 * samples (~230 us). TPMS DBG shows the worst read as S. */
#define RSSI_CHIP      16u
/* Visible characters on one 128 px row of 4 px tiny glyphs. */
#define ROW_CHARS      32
/* print_inverse paints the title capsule through this x (end pixel inclusive). */
#define TITLE_CAP_END  (2u + T_TITLE_CHARS * 4u)

/* F4HWN gFontLight: 9 status columns, LSB is the top pixel. */
static const uint8_t LIGHT_ICON[9] = {
    0x0Cu, 0x12u, 0x21u, 0x6Du, 0x79u, 0x6Du, 0x21u, 0x12u, 0x0Cu
};

/* Decoder signal gate per sensitivity level. RF gain is already fixed at
 * maximum (AGC off), so this is the knob: a lower gate lets weaker bursts
 * through, a higher one ignores more noise and distant cars. */
static const uint8_t SENS_GATE[SENS_LEVELS] = { 60u, 40u, 28u };
static const uint16_t SENS_LABEL[SENS_LEVELS] = { T_SLO, T_SMID, T_SHI };

#if TPMS_HAVE_FSK
/* FSK test settings: STAR cycles the REG_43 IF / demod preset, the arrow
 * keys move the tuning by FOFF_STEP. Preset 0 is the shipped value.
 * Bit 2 changes the gain after the FM demod by 6 dB. Bits 5:4 pick the
 * 25 kHz (10) or 12.5 kHz (00) bandwidth mode; 12.5 kHz gives more audio
 * per kHz of deviation but clips wide FSK sooner. */
static const uint16_t IF_PRESET[4] = { 0x7E2Cu, 0x7E28u, 0x7E0Cu, 0x7E08u };
/* 5 kHz in 10 Hz units. The offset is a 4-bit two's complement nibble,
 * -40 to +35 kHz, and the arrows wrap from one end to the other. */
#define FOFF_STEP      500
#endif

/* Speaker: box, cone, one sound wave. 7 status columns, LSB is the top pixel. */
static const uint8_t SPK_ICON[7] = {
    0x1Cu, 0x1Cu, 0x3Eu, 0x7Fu, 0x00u, 0x22u, 0x1Cu
};

typedef struct {
    uint32_t id;
    uint32_t t;
    int16_t  psi;
    int16_t  temp_c;
    int16_t  rssi;
    uint8_t  model;
} row_t;

typedef struct {
    uint8_t band433;
    uint8_t sens;     /* 0 low .. 2 high */
    uint8_t disp;     /* DISP_TMO / DISP_ON */
    uint8_t ifp;      /* FSK: IF_PRESET index */
    uint8_t foff;     /* FSK: tuning offset nibble, see foffSteps */
} app_cfg_t;

static struct {
    const app_api_t *A;
    uint8_t prevKey, led, nRows, drawDue, menu, menuSel, blOff, spk;
    uint8_t fn;       /* 1 after F: the next SENS or IF change steps back */
    uint8_t blTime;   /* radio backlight time: 0 off, BL_ALWAYS on, else timed */
    uint16_t ledCnt, keyCnt;
    int16_t rssi;
    uint32_t lastHit, lastKey, lastView;
    app_cfg_t cfg;
    row_t *rows;
} g;

#if TPMS_HAVE_FSK
/**
 * Tuning offset in FOFF_STEP units, from the saved nibble.
 *
 * @return -8 to 7.
 */
static int32_t foffSteps(void)
{
    return (int8_t)(uint8_t)(g.cfg.foff << 4) >> 4;
}
#endif

/**
 * Unsigned divide and remainder for the ARM EABI.
 *
 * @param n Dividend.
 * @param d Divisor.
 * @return Quotient in the low 32 bits and remainder in the high 32 bits.
 */
uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d)
{
    return g.A->uidivmod(n, d);
}
/**
 * Unsigned divide for the ARM EABI. Alias of __aeabi_uidivmod.
 *
 * @param n Dividend.
 * @param d Divisor.
 * @return Quotient in the low 32 bits and remainder in the high 32 bits.
 */
uint64_t __aeabi_uidiv(uint32_t n, uint32_t d) __attribute__((alias("__aeabi_uidivmod")));
/**
 * Signed divide and remainder for the ARM EABI.
 *
 * @param n Dividend.
 * @param d Divisor.
 * @return Quotient in the low 32 bits and remainder in the high 32 bits.
 */
uint64_t __aeabi_idivmod(int32_t n, int32_t d)
{
    return g.A->idivmod(n, d);
}
/**
 * Signed divide for the ARM EABI. Alias of __aeabi_idivmod.
 *
 * @param n Dividend.
 * @param d Divisor.
 * @return Quotient in the low 32 bits and remainder in the high 32 bits.
 */
uint64_t __aeabi_idiv(int32_t n, int32_t d) __attribute__((alias("__aeabi_idivmod")));

/**
 * Append a C string.
 *
 * @param o Cursor to write at.
 * @param s Text to copy, not including its terminator.
 * @return Cursor after the copied text.
 */
static char *put(char *o, const char *s)
{
    while (*s) {
        *o++ = *s++;
    }
    return o;
}
/**
 * Append a signed decimal via the firmware formatter.
 *
 * @param o Cursor to write at.
 * @param v Value to append.
 * @return Cursor after the digits.
 */
__attribute__((noinline))
static char *puti(char *o, int32_t v)
{
    return g.A->append_i32(o, v);
}

/**
 * Append the low digits of a value as uppercase hex.
 *
 * @param o Cursor to write at.
 * @param v Value to print.
 * @param digits Number of hex digits, 1 to 8.
 * @return Cursor after the digits.
 */
static char *putHex(char *o, uint32_t v, uint8_t digits)
{
    while (digits--) {
        uint8_t c = (uint8_t)('0' + ((v >> (digits * 4u)) & 0xfu));
        /* '9' + 1 + 7 is 'A'. */
        if (c > '9') c = (uint8_t)(c + 7u);
        *o++ = (char)c;
    }
    return o;
}

/**
 * Append an age as whole seconds, minutes, or hours.
 *
 * @param o Cursor to write at.
 * @param age_ms Age in milliseconds.
 * @return Cursor after the unit suffix.
 */
static char *putAge(char *o, uint32_t age_ms)
{
    if (age_ms < 60000u) {
        o = puti(o, (int32_t)(age_ms / 1000u));
        *o++ = 's';
        return o;
    }
    if (age_ms < 3600000u) {
        o = puti(o, (int32_t)(age_ms / 60000u));
        *o++ = 'm';
        return o;
    }
    o = puti(o, (int32_t)(age_ms / 3600000u));
    *o++ = 'h';
    return o;
}

/**
 * Append 315.00 or 433.92 from the saved band.
 *
 * @param o Cursor to write at.
 * @param ui Packed UI strings for this flavour.
 * @return Cursor after the digits.
 */
__attribute__((always_inline))
static inline char *putBand(char *o, const char *ui)
{
    return put(o, ui + (g.cfg.band433 ? T_B433 : T_B315));
}

/**
 * Reset the slicer for this app's demod at the saved sensitivity.
 *
 * @param dec Slicer to reset.
 */
static void decInit(dectpms_t *dec)
{
    dectpms_init(dec, TPMS_PATH);
    dec->gate = SENS_GATE[g.cfg.sens];
}

/**
 * Terminate a tiny-font string and draw it.
 *
 * @param buf Start of the string.
 * @param x Left pixel.
 * @param y Body-pixel row.
 * @param end Cursor just past the last character.
 */
static void tiny(char *buf, uint8_t x, uint8_t y, char *end)
{
    *end = '\0';
    g.A->print_tiny(buf, x, y, false, true);
}

/**
 * Draw a tiny-font string centered in the 128 px width.
 *
 * @param buf Start of the string.
 * @param y Body-pixel row.
 * @param end Cursor just past the last character.
 */
static void tinyCenter(char *buf, uint8_t y, char *end)
{
    unsigned w = (unsigned)(end - buf) * 4u;
    tiny(buf, (uint8_t)((w >= 128u) ? 0u : ((128u - w) >> 1)), y, end);
}

/**
 * Select the UHF LNA when REG_33 can be read back.
 *
 * REG_33 is the GPIO word. A write-only register stays on the VFO path.
 */
static void selectUhfLna(void)
{
    uint16_t cur = g.A->bk_read(REG_33);
    uint16_t n;
    g.A->bk_write(REG_33, cur);
    if (g.A->bk_read(REG_33) != cur) return;
    n = (uint16_t)((cur | LNA_UHF) & (uint16_t)~LNA_VHF);
    g.A->bk_write(REG_33, n);
    if (g.A->bk_read(REG_33) != n) g.A->bk_write(REG_33, cur);
}

/**
 * Park on 315.000 or 433.920 MHz and point AF at the active demod.
 *
 * tones_off_rx pulses the PLL and mutes audio, so IF and AF writes follow.
 * FSK owns REG_47 as raw bypass. OOK owns it as AM.
 */
static void applyRf(void)
{
    uint32_t f = g.cfg.band433 ? FREQ_433 : FREQ_315;
#if TPMS_HAVE_FSK
    f += (uint32_t)(foffSteps() * FOFF_STEP);
#endif
    g.A->bk_write(REG_38, (uint16_t)(f & 0xFFFFu));
    g.A->bk_write(REG_39, (uint16_t)(f >> 16));
    g.A->tones_off_rx();
    /* Shared front-end: wide IF, filters bypassed, AFC off, fixed gain. */
    g.A->bk_write(REG_2B, (uint16_t)((g.A->bk_read(REG_2B) | 0x0700u) & ~0x0007u));
    g.A->bk_write(REG_73, (uint16_t)(g.A->bk_read(REG_73) | 0x0010u));
#if TPMS_HAVE_FSK
    g.A->bk_write(REG_43, IF_PRESET[g.cfg.ifp]);
#else
    g.A->bk_write(REG_43, REG_43_WIDE);
#endif
    g.A->set_agc(false);
    /* AF last: clears mute from tones_off_rx. FSK = raw bypass; OOK = AM. */
#if TPMS_HAVE_FSK
    g.A->bk_write(REG_47, REG_47_BYPASS);
#else
    g.A->set_af(APP_AF_AM);
#endif
    selectUhfLna();
    g.A->audio_path(g.spk != 0u);
}

/**
 * UI label for a decoded sensor model.
 *
 * @param m TPMS_FORD or one of the other model ids.
 * @param ui Packed UI strings for this flavour.
 * @return Pointer at the label inside ui, or ui if the model is absent.
 */
static const char *modelName(uint8_t m, const char *ui)
{
#if TPMS_HAVE_FSK
    if (m == TPMS_FORD) return ui + T_FORD;
    if (m == TPMS_TOYT) return ui + T_TOYT;
#endif
#if TPMS_HAVE_SCHR
    if (m == TPMS_SCHR) return ui + T_SCHR;
#endif
#if TPMS_HAVE_EG53
    if (m == TPMS_EG53) return ui + T_EG53;
#endif
#if TPMS_HAVE_BMW
    if (m == TPMS_BMW) return ui + T_BMW;
#endif
#if TPMS_HAVE_NIS
    if (m == TPMS_NISS) return ui + T_NISS;
#endif
    (void)m;
    return ui;
}

/**
 * Hex digits a sensor model's id needs, as rtl_433 prints them.
 *
 * Ford and Toyota ids are 32 bits, Schrader MRXGG4 28, the rest 24.
 *
 * @param m TPMS_FORD or one of the other model ids.
 * @return 8, 7, or 6.
 */
static uint8_t idDigits(uint8_t m)
{
#if TPMS_HAVE_FSK
    (void)m;
    return 8u;
#else
    return m == TPMS_SCHR ? 7u : 6u;
#endif
}

/**
 * Update the live row for this sensor, or insert one.
 *
 * A full list replaces the oldest row.
 *
 * @param p Packet just decoded.
 * @param rssi Signal strength in dBm.
 */
static void upsert(const tpms_pkt_t *p, int16_t rssi)
{
    uint32_t now = g.A->ticks_ms();
    g.lastHit = now;
    for (uint8_t i = 0; i < g.nRows; i++) {
        if (g.rows[i].id == p->id && g.rows[i].model == p->model) {
            g.rows[i].psi = p->psi;
            g.rows[i].temp_c = p->temp_c;
            g.rows[i].rssi = rssi;
            g.rows[i].t = now;
            return;
        }
    }
    row_t *r;
    if (g.nRows < ROWS) r = &g.rows[g.nRows++];
    else {
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < ROWS; i++)
            if ((int32_t)(g.rows[i].t - g.rows[oldest].t) < 0) oldest = i;
        r = &g.rows[oldest];
    }
    r->id = p->id;
    r->model = p->model;
    r->psi = p->psi;
    r->temp_c = p->temp_c;
    r->rssi = rssi;
    r->t = now;
}

/**
 * Draw the status bulb while the backlight is forced on.
 *
 * The bulb sits between the title capsule and the band.
 * It shows only while the force backlight option is on (key 0, or the radio
 * backlight setting is always on), so the lamp is kept lit regardless of
 * keys. A key press that lights the lamp for the idle window (the speaker
 * key, for example) does not show it.
 *
 * @param bandX Left edge of the band text, in pixels.
 */
static void drawLight(uint8_t bandX)
{
    uint8_t *line;
    unsigned gap;
    /* Only a forced, always-on backlight earns the bulb. */
    if (g.cfg.disp != DISP_ON) return;
    if (bandX < TITLE_CAP_END + 2u + sizeof LIGHT_ICON) return;
    line = g.A->status_line;
    if (!line) return;
    gap = (unsigned)(bandX - (TITLE_CAP_END + 1u) - sizeof LIGHT_ICON);
    memcpy(line + (TITLE_CAP_END + 1u) + (gap >> 1), LIGHT_ICON, sizeof LIGHT_ICON);
}

/**
 * Draw the status line: title, bulb, band, demod, and speaker icon.
 *
 * @param ui Packed UI strings for this flavour.
 */
static void drawStatus(const char *ui)
{
    char str[20];
    char *o;
    /* Band (6), demod (3) and both slots are always 12 cells, 48 px, so the
       text fits between the title capsule and the 48 px battery reserve. */
    const uint8_t bandX = (uint8_t)(128u - BATT_RESERVE - 12u * 4u);
    g.A->status_clear();
    g.A->draw_battery();
    g.A->print_inverse(ui + (g.menu ? T_MENU : T_TITLE), 2, 0, true, true,
                       (uint8_t)(2u + T_TITLE_CHARS * 4u));
    o = putBand(str, ui);
    *o++ = ' ';
    o = put(o, ui + T_DEMOD);
    /* Always reserve 8 px for the speaker icon so the band and bulb never shift. */
    *o++ = ' ';
    *o++ = ' ';
    *o = '\0';
    g.A->print_tiny(str, bandX, 1, true, true);
    drawLight(bandX);
    /* The icon fills the slot that ends where the band text ends. */
    if (g.spk && g.A->status_line)
        memcpy(g.A->status_line + (128u - BATT_RESERVE) - sizeof SPK_ICON,
               SPK_ICON, sizeof SPK_ICON);
}

/**
 * Draw the options menu and its help line.
 *
 * @param ui Packed UI strings for this flavour.
 */
static void drawMenu(const char *ui)
{
    char str[32]; /* 31-cell help row with the version + NUL */
    char *o;
#if TPMS_LOCK_BAND
    const uint16_t labels[2] = { T_SENS, T_CLR };
#else
    const uint16_t labels[3] = { T_BAND, T_SENS, T_CLR };
#endif
    for (uint8_t i = 0; i < MENU_ITEMS; i++) {
        o = str;
        /* The cursor turns round while F is armed: MENU will step back.
           '<' is two below '>'. */
        *o++ = (i == g.menuSel) ? (char)('>' - 2u * g.fn) : ' ';
        o = put(o, ui + labels[i]);
#if !TPMS_LOCK_BAND
        if (i == MENU_BAND) {
            *o++ = ' ';
            o = putBand(o, ui);
        }
#endif
        if (i == MENU_SENS) {
            *o++ = ' ';
            o = put(o, ui + SENS_LABEL[g.cfg.sens]);
        }
        *o = '\0';
        /* Capsules like CW Keyer help: selected row is the full-width mark. */
        g.A->print_inverse(str, 2, (uint8_t)(i + 1u), false, true,
                           (uint8_t)(i == g.menuSel ? 100u : 78u));
    }
    /* Body is 56 px. A 6 px glyph at y=47 ends at pixel 52. Both help
       rows are 31 cells, padded in gen_assets.py so the right-hand text
       ends at x=126. */
    g.A->print_tiny(ui + T_HELP1, 2, 41, false, true);
    g.A->print_tiny(ui + T_HELP2, 2, 47, false, true);
}

/**
 * Draw the listening key strip on body line 6, right aligned.
 *
 * @param ui Packed UI strings for this flavour.
 */
static void drawListHints(const char *ui)
{
    /* Body line 6. print_inverse draws glyphs at (line * 8) + 1.
     * y is body pixels (0..55); 57 is the next page and lands on the status line.
     * gen_assets.py sets HINT_X so the strip ends at x=126. */
    g.A->print_tiny(ui + T_HINT, HINT_X, 49, false, true);
}

/**
 * Draw the menu, the listening screen, or the live sensor list.
 *
 * @param ui Packed UI strings for this flavour.
 * @param dec Slicer whose FSK test counters the listening screen shows.
 */
__attribute__((noinline))
static void draw(const char *ui, const dectpms_t *dec)
{
    /* Widest row: model, 8-digit id, six int16 or age fields, and units. */
    char str[48];
    char *o;
    uint32_t now = g.A->ticks_ms();

    g.A->display_clear();
    drawStatus(ui);

    if (g.menu) {
        drawMenu(ui);
        return;
    }

    if (!g.nRows) {
        unsigned w;
        uint8_t y;
        /* "LISTENING 433.92 UK/EU" — band + region on one line. */
        o = put(str, ui + T_WAIT);
        *o++ = ' ';
        o = putBand(o, ui);
        *o++ = ' ';
        o = put(o, ui + T_REGION);
        tinyCenter(str, 8, o);
        /* Sensor rows: one row per newline. */
        {
            const char *s = ui + T_CARS;
            o = str;
            y = 16;
            for (; *s; s++) {
                if (*s != '\n') {
                    *o++ = *s;
                    continue;
                }
                tinyCenter(str, y, o);
                y = (uint8_t)(y + 8u);
                o = str;
            }
        }
#if TPMS_HAVE_FSK
        /* Test line on the blank second car row, e.g.
           "-5K IF7E2C TOYT 40/72": tuning offset, REG_43 preset, and the
           most Toyota bits one sync decoded since the last retune. */
        o = puti(str, foffSteps() * 5);
        o = put(o, ui + T_IF);
        o = putHex(o, IF_PRESET[g.cfg.ifp], 4);
        o = put(o, ui + T_TY);
        o = puti(o, dec->ty_best);
        o = put(o, ui + T_TYEND);
        tinyCenter(str, 24, o);
#else
        (void)dec;
#endif
        o = put(str, ui + T_MENU);
        *o = '\0';
        w = (unsigned)(o - str) * 4u;
        g.A->print_inverse(str, 2, 6, false, true, (uint8_t)(2u + w));
        drawListHints(ui);
        return;
    }
    for (uint8_t i = 0; i < g.nRows && i < ROWS; i++) {
        const row_t *r = &g.rows[i];
        char *sp;
        o = put(str, modelName(r->model, ui));
        *o++ = ' ';
        o = putHex(o, r->id, idDigits(r->model));
        *o++ = ' ';
        o = puti(o, r->psi);
        o = put(o, ui + T_PSI);
        if (r->temp_c != (int16_t)TPMS_NO_TEMP) {
            *o++ = ' ';
            o = puti(o, r->temp_c);
            o = put(o, ui + T_C);
        }
        sp = o;
        *o++ = ' ';
        o = puti(o, r->rssi);
        *o++ = ' ';
        o = putAge(o, now - r->t);
        /* A cold, weak row ("-12C -105 45s") is one cell too wide for the
           screen. The C unit already ends the temperature, so drop the space
           after it. Shift left one char at a time: the ranges overlap. */
        if (o - str > ROW_CHARS && r->temp_c != (int16_t)TPMS_NO_TEMP) {
            for (char *p = sp; p + 1 < o; p++) p[0] = p[1];
            o--;
        }
        tiny(str, 0, (uint8_t)(8u + i * 6u), o);
    }
    /* Key strip on the MENU baseline when the list is short. */
    if (g.nRows <= 3u)
        drawListHints(ui);
}

/**
 * Apply the selected menu item.
 *
 * @param dec Slicer to reset when the band or demod changes.
 */
static void menuChange(dectpms_t *dec)
{
    switch (g.menuSel) {
#if !TPMS_LOCK_BAND
        case MENU_BAND:
            g.cfg.band433 ^= 1u;
            applyRf();
            decInit(dec);
            break;
#endif
        case MENU_SENS:
            /* Up one level, or down one (up two) after F. */
            g.cfg.sens = (uint8_t)((g.cfg.sens + 1u + g.fn) % SENS_LEVELS);
            decInit(dec);
            break;
        case MENU_CLR:
            g.nRows = 0;
            g.menu = 0; /* back to the listening screen */
            break;
        default:
            break;
    }
}

/**
 * Handle one new keypress.
 *
 * @param key Key code from the firmware.
 * @param dec Slicer, reset when the band or demod changes.
 * @param now ticks_ms at this key poll.
 * @return True when the app should exit.
 */
static bool handleKey(uint8_t key, dectpms_t *dec, uint32_t now)
{
    g.lastKey = now;
    if (g.blOff && !g.menu) {
        /* EXIT on a dark screen only wakes it, so a press to see the list
           does not quit. With the radio backlight always on the screen is
           never dark, so EXIT quits straight away. */
        if (key == APP_KEY_EXIT && g.blTime == BL_ALWAYS) return true;
        g.blOff = 0;
        /* EXIT is consumed as the wake */
        if (key == APP_KEY_EXIT) return false;
        /* fall through so other keys still act; 0 forces the lamp on */
    }

    if (g.menu) {
        int8_t d = g.A->nav_dir(key);
        if (d) {
            /* d is -1 or +1, so adding MENU_ITEMS keeps the sum positive
               and the remainder wraps either way. */
            g.menuSel = (uint8_t)((g.menuSel + MENU_ITEMS + d) % MENU_ITEMS);
            return false;
        }
        if (key == APP_KEY_EXIT)
            g.menu = 0;
        else if (key == APP_KEY_MENU || key == APP_KEY_2 || key == APP_KEY_5)
            menuChange(dec);
        /* Decoding pauses in the menu, so drop the partial burst it cut off. */
        if (!g.menu) dectpms_rearm(dec);
        return false;
    }

    switch (key) {
        case APP_KEY_EXIT:
            return true;
        case APP_KEY_1:
            g.spk ^= 1u;
            g.A->audio_path(g.spk != 0u);
            break;
        case APP_KEY_MENU:
            g.menu = 1;
            g.menuSel = 0;
            break;
#if !TPMS_LOCK_BAND
        case APP_KEY_2:
            g.cfg.band433 ^= 1u;
            goto retune;
#endif
#if TPMS_HAVE_FSK
        case APP_KEY_STAR:
            /* Up one preset, or down one (up three) after F. */
            g.cfg.ifp = (uint8_t)((g.cfg.ifp + 1u + 2u * g.fn) & 3u);
            goto retune;
#endif
        case APP_KEY_0:
            /* Toggle the force backlight option. Off returns to the idle
               timeout, counted from this press. */
            g.cfg.disp = (g.cfg.disp == DISP_ON) ? DISP_TMO : DISP_ON;
            g.blOff = 0;
            break;
        default:
#if TPMS_HAVE_FSK
        {
            /* Arrows move the tuning offset one step. */
            int8_t d = g.A->nav_dir(key);
            if (!d) break;
            g.cfg.foff = (uint8_t)((g.cfg.foff + d) & 15u);
            goto retune;
        }
#else
            break;
#endif
    }
    return false;
#if TPMS_HAVE_FSK || !TPMS_LOCK_BAND
retune:
    /* Band, IF preset or offset changed: park again and reset the counters. */
    applyRf();
    decInit(dec);
    return false;
#endif
}

/**
 * Report whether the resident backlight should stay lit.
 *
 * The menu and the force backlight option keep the lamp on. Otherwise it
 * stays on for the idle window after the last key.
 *
 * @return True when the lamp should be held on.
 */
static bool backlightHeld(void)
{
    /* Menu and force stay lit. 10s holds the light after the last key. */
    if (g.menu || g.cfg.disp == DISP_ON) return true;
    return !g.blOff;
}

/**
 * Turn the backlight off after the idle timeout.
 *
 * @param now ticks_ms at this key poll.
 * @return True when the lamp was just released.
 */
static bool serviceDisp(uint32_t now)
{
    if (g.cfg.disp != DISP_TMO || g.menu || g.blOff) return false;
    if ((uint32_t)(now - g.lastKey) < DISP_IDLE_MS) return false;
    g.blOff = 1;
    return true;
}

/**
 * Sample audio until EXIT, decoding packets and refreshing the screen.
 *
 * @param ui Packed UI strings for this flavour.
 */
__attribute__((noinline))
static void listen(const char *ui)
{
    dectpms_t dec;
    decInit(&dec);
    uint32_t (*const clk)(void) = g.A->cycles_48mhz;
    uint16_t (*const sample)(void) = g.A->af_capture_sample;
    uint32_t next = clk();
    bool wantUi = true;
#if !TPMS_HAVE_FSK
    bool rssiDue = true;
#endif
    g.lastKey = g.A->ticks_ms();

    for (;;) {
        while ((int32_t)(clk() - next) < 0) {}
        next += CYC_PER_SAMPLE;
        uint16_t x = sample();
        tpms_pkt_t pkt;
#if !TPMS_HAVE_FSK
        /* OOK frames finish after the carrier drops, when RSSI is back at
           the floor, so read it once while the burst is still on air. */
        if (dec.bitn < RSSI_CHIP) rssiDue = true;
        else if (rssiDue) {
            rssiDue = false;
            g.rssi = g.A->rssi_dbm();
        }
#endif
        if (!g.menu && dectpms_push(&dec, x, &pkt)) {
#if TPMS_HAVE_FSK
            g.rssi = g.A->rssi_dbm();
#endif
            upsert(&pkt, g.rssi);
            g.led = 2u;
            g.ledCnt = 0;
            g.A->led(true);
            g.drawDue = 1u;
            dectpms_rearm(&dec);
        }
#if TPMS_HAVE_FSK
        /* The Toyota test counter rose: redraw at the next poll that is
           DRAW_HOLD_MS past the last decode. */
        if (dec.note) {
            dec.note = 0;
            g.drawDue = 1u;
        }
#endif

        if (g.led && ++g.ledCnt >= LED_STEP) {
            g.ledCnt = 0;
            g.led--;
            g.A->led(g.led & 1u);
        }

        if (!wantUi) {
            if (++g.keyCnt < KEY_EVERY) continue;
        }
        g.keyCnt = 0;
        /* One clock read serves the whole poll. */
        uint32_t now = g.A->ticks_ms();
        if (serviceDisp(now)) wantUi = true;
        /* Drawing blocks sampling, so wait out the rest of the burst train. */
        if (g.drawDue && (uint32_t)(now - g.lastHit) >= DRAW_HOLD_MS) {
            g.drawDue = 0;
            wantUi = true;
        }

        uint8_t key = g.A->get_key();
        bool keyUi = key != g.prevKey && key != APP_KEY_INVALID;
        g.prevKey = key;
        if (keyUi) {
            if (handleKey(key, &dec, now)) break;
            /* F is a one-shot shift, like the radio's own: the next SENS or
               IF key steps back. A second F cancels it, any other key uses
               it up. handleKey ignores F itself. */
            g.fn = (uint8_t)((key == APP_KEY_F) & (g.fn ^ 1u));
            wantUi = true;
        }

        /* Tick the resident backlight every poll. Holding re-arms it; releasing
           lets the radio countdown turn the light off. The LCD stays drawn. */
        g.A->backlight_update();
        if (backlightHeld()) g.A->backlight_on();

        /* A screen capture (UV Studio, K5Viewer) pings, then waits for a frame,
           but blit_full only sends one when the screen is redrawn. Offer the
           viewer a frame between redraws; it sends nothing with no viewer.
           viewer_update (offset 388) is past the end of an older level 3 table. */
        if (!wantUi && g.A->api_size >= 392u
            && (uint32_t)(now - g.lastView) >= VIEW_EVERY_MS) {
            g.lastView = now;
            g.A->viewer_update();
            next = clk() + CYC_PER_SAMPLE;
        }

        if (wantUi) {
            g.drawDue = 0;
            g.A->battery_sample();
            draw(ui, &dec);
            g.A->blit_status();
            g.A->blit_full();
            wantUi = false;
            next = clk() + CYC_PER_SAMPLE;
        }
    }
}

/**
 * Overlay entry. Listen, then restore the radio and save the settings.
 *
 * @param api Firmware service table.
 */
__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    row_t rows[ROWS];
    /* Zeroed so a first launch with nothing saved never reads stack junk. */
    uint8_t cfg[4] = {0};
    char ui[UI_SIZE] __attribute__((aligned(4)));
    uint16_t r43, r2b, r73;

    memset(rows, 0, sizeof rows);
    /* The overlay loader does not clear BSS, so start every field at 0. */
    memset(&g, 0, sizeof g);
    g.A = api;
    g.rows = rows;
    g.prevKey = APP_KEY_INVALID;
    g.cfg.disp = DISP_TMO;
    /* Unknown (no storage read on this firmware) keeps the timed behaviour. */
    g.blTime = 1u;
    if (api->sys_storage_read) api->sys_storage_read(BL_TIME_ADDR, &g.blTime, 1);
    api->led(false);
    api->asset_read(0, ui, UI_SIZE);

    api->af_capture_open();
    r43 = api->bk_read(REG_43);
    r2b = api->bk_read(REG_2B);
    r73 = api->bk_read(REG_73);

    /* Defaults per flavour (first launch). The saved band is restored when
     * present, then EU2 forces 433.92 and NA2 forces 315.00. Byte 2 held the
     * old demod choice (0xB1 / 0xB2) and now holds the sensitivity (0xB3).
     * FSK apps pack the IF preset in bits 3:2 and the signed tuning offset
     * in bits 7:4, so an older save (0..2) loads them as 0. */
    api->cfg_load(cfg, 4);
    g.cfg.sens = SENS_DEFAULT;
    if (cfg[0] == CFG_MAGIC || cfg[0] == 0xB2u || cfg[0] == 0xB1u) {
        g.cfg.band433 = cfg[1] ? 1u : 0u;
        if (cfg[0] != 0xB1u && cfg[3] == DISP_ON) g.cfg.disp = DISP_ON;
        if (cfg[0] == CFG_MAGIC && (cfg[2] & 3u) < SENS_LEVELS) {
            g.cfg.sens = cfg[2] & 3u;
#if TPMS_HAVE_FSK
            g.cfg.ifp = (uint8_t)((cfg[2] >> 2) & 3u);
            g.cfg.foff = (uint8_t)(cfg[2] >> 4);
#endif
        }
    } else {
#if defined(TPMS_FLAVOUR_NA) || defined(TPMS_FLAVOUR_NA2)
        g.cfg.band433 = 0u; /* 315.00 MHz */
#else
        g.cfg.band433 = 1u; /* 433.92 MHz */
#endif
    }
#if TPMS_LOCK_BAND
    g.cfg.band433 = TPMS_BAND433;
#endif
    /* A radio backlight set to always on starts with the force option on. */
    if (g.blTime == BL_ALWAYS) g.cfg.disp = DISP_ON;
    api->backlight_on();

    applyRf();
    api->delay_ms(50);
    listen(ui);

    api->bk_write(REG_43, r43);
    api->bk_write(REG_2B, r2b);
    api->bk_write(REG_73, r73);
    api->set_agc(true);
    api->af_capture_close();
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
    api->led(false);

    cfg[0] = CFG_MAGIC;
    cfg[1] = g.cfg.band433;
#if TPMS_HAVE_FSK
    cfg[2] = (uint8_t)(g.cfg.sens | (g.cfg.ifp << 2) | (g.cfg.foff << 4));
#else
    cfg[2] = g.cfg.sens;
#endif
    cfg[3] = g.cfg.disp;
    api->cfg_save(cfg, 4);
}
