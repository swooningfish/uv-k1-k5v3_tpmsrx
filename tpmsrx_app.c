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
#define LED_STEP       (FS / 10u)
#define CFG_MAGIC      0xB2u
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
#define MENU_CLR       0u
#elif defined(TPMS_FLAVOUR_NA2)
#define TPMS_LOCK_BAND 1
#define TPMS_BAND433   0u
#define MENU_CLR       0u
#else
#define TPMS_LOCK_BAND 0
#define MENU_BAND      0u
#define MENU_CLR       1u
#endif
#define DISP_OFF       1u
#define DISP_TMO       2u
#define DISP_IDLE_MS   10000u
/* Radio backlight time in storage (gEeprom.BACKLIGHT_TIME, firmware v6.3.0
 * settings block 0xA008 byte 5). 0 keeps the lamp off, 61 is always on. */
#define BL_TIME_ADDR   0x00A00Du
#define BL_ALWAYS      61u
#define BATT_RESERVE   48u
/* print_inverse paints the title capsule through this x (end pixel inclusive). */
#define TITLE_CAP_END  (2u + T_TITLE_CHARS * 4u)

/* F4HWN gFontLight: 9 status columns, LSB is the top pixel. */
static const uint8_t LIGHT_ICON[9] = {
    0x0Cu, 0x12u, 0x21u, 0x6Du, 0x79u, 0x6Du, 0x21u, 0x12u, 0x0Cu
};

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
    uint8_t disp;     /* DISP_OFF / DISP_TMO */
} app_cfg_t;

static struct {
    const app_api_t *A;
    uint8_t prevKey, led, nRows, drawDue, menu, menuSel, blOff, spk;
    uint8_t blTime;   /* radio backlight time: 0 off, BL_ALWAYS on, else timed */
    uint16_t ledCnt, keyCnt;
    int16_t rssi;
    uint32_t lastHit, lastKey;
    app_cfg_t cfg;
    row_t *rows;
} g;

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
 * Append eight uppercase hex digits.
 *
 * @param o Cursor to write at.
 * @param v Value, printed in full 32 bits.
 * @return Cursor after the digits.
 */
static char *putHex8(char *o, uint32_t v)
{
    for (int i = 7; i >= 0; i--) {
        uint8_t n = (uint8_t)((v >> (i * 4)) & 0xfu);
        *o++ = (char)(n < 10u ? (char)('0' + n) : (char)('A' + (n - 10u)));
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
 * @return Cursor after the digits.
 */
static char *putBand(char *o)
{
    o = puti(o, g.cfg.band433 ? 433 : 315);
    *o++ = '.';
    if (g.cfg.band433) {
        *o++ = '9';
        *o++ = '2';
    } else {
        *o++ = '0';
        *o++ = '0';
    }
    return o;
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
    g.A->bk_write(REG_38, (uint16_t)(f & 0xFFFFu));
    g.A->bk_write(REG_39, (uint16_t)(f >> 16));
    g.A->tones_off_rx();
    /* Shared front-end: wide IF, filters bypassed, AFC off, fixed gain. */
    g.A->bk_write(REG_2B, (uint16_t)((g.A->bk_read(REG_2B) | 0x0700u) & ~0x0007u));
    g.A->bk_write(REG_73, (uint16_t)(g.A->bk_read(REG_73) | 0x0010u));
    g.A->bk_write(REG_43, REG_43_WIDE);
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
 * Draw the status bulb while the backlight is lit and the gap fits.
 *
 * The bulb sits between the title capsule and the band.
 * It hides when the idle timeout turns the lamp off, and after the 0 key.
 * It follows the radio backlight setting when the firmware can read it:
 * never shown when that is OFF, always shown when it is always on.
 *
 * @param bandX Left edge of the band text, in pixels.
 */
static void drawLight(uint8_t bandX)
{
    uint8_t *line;
    unsigned gap;
    /* Radio backlight OFF: BACKLIGHT_TurnOn never lights the lamp. */
    if (!g.blTime) return;
    /* Always on: the lamp stays lit after the idle timeout and the 0 key. */
    if (g.blTime != BL_ALWAYS && (g.cfg.disp != DISP_TMO || g.blOff)) return;
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
    unsigned width;
    uint8_t bandX;
    g.A->status_clear();
    g.A->draw_battery();
    g.A->print_inverse(ui + (g.menu ? T_MENU : T_TITLE), 2, 0, true, true,
                       (uint8_t)(2u + T_TITLE_CHARS * 4u));
    o = putBand(str);
    *o++ = ' ';
    o = put(o, ui + T_DEMOD);
    /* Always reserve 8 px for the speaker icon so the band and bulb never shift. */
    *o++ = ' ';
    *o++ = ' ';
    *o = '\0';
    /* Band (6), demod (3) and both slots are always 12 cells, 48 px, so the
       text fits between the title capsule and the 48 px battery reserve. */
    width = (unsigned)(o - str) * 4u;
    bandX = (uint8_t)(128u - BATT_RESERVE - width);
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
    const uint16_t labels[1] = { T_CLR };
#else
    const uint16_t labels[2] = { T_BAND, T_CLR };
#endif
    for (uint8_t i = 0; i < MENU_ITEMS; i++) {
        o = str;
        *o++ = (i == g.menuSel) ? '>' : ' ';
        o = put(o, ui + labels[i]);
#if !TPMS_LOCK_BAND
        if (i == MENU_BAND) {
            *o++ = ' ';
            o = putBand(o);
        }
#endif
        *o = '\0';
        /* Capsules like CW Keyer help: selected row is the full-width mark. */
        g.A->print_inverse(str, 2, (uint8_t)(i + 1u), false, true,
                           (uint8_t)(i == g.menuSel ? 100u : 78u));
    }
    /* Body is 56 px. A 6 px glyph at y=47 ends at pixel 52. */
    o = put(str, ui + T_HELP1);
    tiny(str, 2, 41, o);
    o = put(str, ui + T_HELP2);
    tiny(str, 2, 47, o);
    o = put(str, ui + T_HELP3);
    {
        unsigned hw = (unsigned)(o - str) * 4u;
        uint8_t hx = (uint8_t)((hw >= 126u) ? 2u : (126u - hw));
        tiny(str, hx, 47, o);
    }
}

/**
 * Draw the listening key strip on body line 6, right aligned.
 *
 * @param ui Packed UI strings for this flavour.
 */
static void drawListHints(const char *ui)
{
    char str[28];
    char *o = put(str, ui + T_HINT);
    unsigned w = (unsigned)(o - str) * 4u;
    uint8_t x = (uint8_t)((w >= 126u) ? 2u : (126u - w));
    /* Body line 6. print_inverse draws glyphs at (line * 8) + 1.
     * y is body pixels (0..55); 57 is the next page and lands on the status line. */
    tiny(str, x, 49, o);
}

/**
 * Draw the menu, the listening screen, or the live sensor list.
 *
 * @param ui Packed UI strings for this flavour.
 */
__attribute__((noinline))
static void draw(const char *ui)
{
    char str[34];
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
        o = putBand(o);
        *o++ = ' ';
        o = put(o, ui + T_REGION);
        tinyCenter(str, 8, o);
        o = put(str, ui + T_CARS1);
        tinyCenter(str, 16, o);
        y = 24;
        if (ui[T_CARS2]) {
            o = put(str, ui + T_CARS2);
            tinyCenter(str, y, o);
            y = (uint8_t)(y + 8u);
        }
        o = put(str, ui + T_MENU);
        *o = '\0';
        w = (unsigned)(o - str) * 4u;
        g.A->print_inverse(str, 2, 6, false, true, (uint8_t)(2u + w));
        drawListHints(ui);
        return;
    }
    for (uint8_t i = 0; i < g.nRows && i < ROWS; i++) {
        const row_t *r = &g.rows[i];
        o = put(str, modelName(r->model, ui));
        *o++ = ' ';
        o = putHex8(o, r->id);
        *o++ = ' ';
        o = puti(o, r->psi);
        o = put(o, ui + T_PSI);
        if (r->temp_c != (int16_t)TPMS_NO_TEMP) {
            *o++ = ' ';
            o = puti(o, r->temp_c);
            o = put(o, ui + T_C);
        }
        *o++ = ' ';
        o = puti(o, r->rssi);
        *o++ = ' ';
        o = putAge(o, now - r->t);
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
            dectpms_init(dec, TPMS_PATH);
            break;
#endif
        case MENU_CLR:
            g.nRows = 0;
            break;
        default:
            break;
    }
#if TPMS_LOCK_BAND
    (void)dec;
#endif
}

/**
 * Handle one new keypress.
 *
 * @param key Key code from the firmware.
 * @param dec Slicer, reset when the band or demod changes.
 * @return True when the app should exit.
 */
static bool handleKey(uint8_t key, dectpms_t *dec)
{
    g.lastKey = g.A->ticks_ms();
    if (g.blOff && !g.menu) {
        if (key == APP_KEY_EXIT) return true;
        g.blOff = 0;
        if (g.cfg.disp == DISP_OFF) g.cfg.disp = DISP_TMO;
        if (key == APP_KEY_0) return false; /* toggle consumed as wake */
        /* fall through so other keys still act */
    }

    if (g.menu) {
        int8_t d = g.A->nav_dir(key);
        if (d) {
            int8_t n = (int8_t)g.menuSel + d;
            if (n < 0) n = (int8_t)(MENU_ITEMS - 1u);
            if (n >= (int8_t)MENU_ITEMS) n = 0;
            g.menuSel = (uint8_t)n;
            return false;
        }
        if (key == APP_KEY_EXIT) {
            g.menu = 0;
            return false;
        }
        if (key == APP_KEY_MENU || key == APP_KEY_2 || key == APP_KEY_5)
            menuChange(dec);
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
            applyRf();
            dectpms_init(dec, TPMS_PATH);
            break;
#endif
        case APP_KEY_0:
            g.cfg.disp = DISP_OFF;
            g.blOff = 1;
            break;
        default:
            break;
    }
    return false;
}

/**
 * Report whether the resident backlight should stay lit.
 *
 * The menu keeps the lamp on. After the last key it stays on for the
 * idle window. Display off leaves it off.
 *
 * @return True when the lamp should be held on.
 */
static bool backlightHeld(void)
{
    /* Menu stays lit. 10s holds the light after the last key. OFF releases it. */
    if (g.menu) return true;
    return g.cfg.disp == DISP_TMO && !g.blOff;
}

/**
 * Turn the backlight off after the idle timeout.
 *
 * @return True when the lamp was just released.
 */
static bool serviceDisp(void)
{
    if (g.cfg.disp != DISP_TMO || g.menu || g.blOff) return false;
    if ((uint32_t)(g.A->ticks_ms() - g.lastKey) < DISP_IDLE_MS) return false;
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
    dectpms_init(&dec, TPMS_PATH);
    uint32_t (*const clk)(void) = g.A->cycles_48mhz;
    uint16_t (*const sample)(void) = g.A->af_capture_sample;
    uint32_t next = clk();
    bool wantUi = true;
    g.lastHit = 0;
    g.lastKey = g.A->ticks_ms();

    for (;;) {
        while ((int32_t)(clk() - next) < 0) {}
        next += CYC_PER_SAMPLE;
        uint16_t x = sample();
        tpms_pkt_t pkt;
        if (!g.menu && dectpms_push(&dec, x, &pkt)) {
            g.rssi = g.A->rssi_dbm();
            upsert(&pkt, g.rssi);
            g.led = 2u;
            g.ledCnt = 0;
            g.A->led(true);
            g.drawDue = 1u;
            dectpms_rearm(&dec);
        }

        if (g.led && ++g.ledCnt >= LED_STEP) {
            g.ledCnt = 0;
            g.led--;
            g.A->led(g.led & 1u);
        }

        if (!wantUi) {
            if (++g.keyCnt < KEY_EVERY) continue;
        }
        g.keyCnt = 0;
        if (serviceDisp()) wantUi = true;
        /* Drawing blocks sampling, so wait out the rest of the burst train. */
        if (g.drawDue && (uint32_t)(g.A->ticks_ms() - g.lastHit) >= DRAW_HOLD_MS) {
            g.drawDue = 0;
            wantUi = true;
        }

        uint8_t key = g.A->get_key();
        bool keyUi = key != g.prevKey && key != APP_KEY_INVALID;
        g.prevKey = key;
        if (keyUi) {
            if (handleKey(key, &dec)) break;
            wantUi = true;
        }

        /* Tick the resident backlight every poll. Holding re-arms it; releasing
           lets the radio countdown turn the light off. The LCD stays drawn. */
        g.A->backlight_update();
        if (backlightHeld()) g.A->backlight_on();

        if (wantUi) {
            g.drawDue = 0;
            g.A->battery_sample();
            draw(ui);
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
    uint8_t cfg[4];
    char ui[UI_SIZE] __attribute__((aligned(4)));
    uint16_t r43, r2b;

    memset(rows, 0, sizeof rows);
    g.A = api;
    g.rows = rows;
    g.prevKey = APP_KEY_INVALID;
    g.nRows = 0;
    g.menu = 0;
    g.blOff = 0;
    g.spk = 0;
    g.cfg.disp = DISP_TMO;
    /* Unknown (no storage read on this firmware) keeps the timed behaviour. */
    g.blTime = 1u;
    if (api->sys_storage_read) api->sys_storage_read(BL_TIME_ADDR, &g.blTime, 1);
    api->led(false);
    api->asset_read(0, ui, UI_SIZE);

    api->af_capture_open();
    r43 = api->bk_read(REG_43);
    r2b = api->bk_read(REG_2B);

    /* Defaults per flavour (first launch). The saved band is restored when
     * present, then EU2 forces 433.92 and NA2 forces 315.00. Byte 2 held the
     * old demod choice and is ignored: each app has one demod now. */
    api->cfg_load(cfg, 4);
    if (cfg[0] == CFG_MAGIC || cfg[0] == 0xB1u) {
        g.cfg.band433 = cfg[1] ? 1u : 0u;
        if (cfg[0] == CFG_MAGIC && cfg[3] == DISP_OFF) g.cfg.disp = DISP_OFF;
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
    if (g.cfg.disp == DISP_OFF) g.blOff = 1;
    else api->backlight_on();

    applyRf();
    api->delay_ms(50);
    listen(ui);

    api->bk_write(REG_43, r43);
    api->bk_write(REG_2B, r2b);
    api->set_agc(true);
    api->af_capture_close();
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
    api->led(false);

    cfg[0] = CFG_MAGIC;
    cfg[1] = g.cfg.band433;
    cfg[2] = TPMS_PATH;
    cfg[3] = g.cfg.disp;
    api->cfg_save(cfg, 4);
}
