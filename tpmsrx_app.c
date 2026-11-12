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
#define REFRESH_POLLS  25u
#define LED_STEP       (FS / 10u)
#define CFG_MAGIC      0xB2u
#define ROWS           7u
#define PREF_FSK       0u
#define PREF_OOK       1u
#define PREF_AUTO      2u
#define AUTO_DWELL_MS  2500u
#define AUTO_HOLD_MS   10000u
/* EU2: fixed 433.92 OOK. NA2: OOK only, band still changeable. EU/NA: both free. */
#if defined(TPMS_FLAVOUR_EU2) || defined(TPMS_FLAVOUR_NA2)
#define TPMS_LOCK_MODE 1
#else
#define TPMS_LOCK_MODE 0
#endif
#if defined(TPMS_FLAVOUR_EU2)
#define TPMS_LOCK_BAND 1
#define MENU_CLR       0u
#elif defined(TPMS_FLAVOUR_NA2)
#define TPMS_LOCK_BAND 0
#define MENU_BAND      0u
#define MENU_CLR       1u
#else
#define TPMS_LOCK_BAND 0
#define MENU_BAND      0u
#define MENU_MODE      1u
#define MENU_CLR       2u
#endif
#define DISP_OFF       1u
#define DISP_TMO       2u
#define DISP_IDLE_MS   10000u
#define BATT_RESERVE   48u
/* print_inverse paints the title capsule through this x (end pixel inclusive). */
#define TITLE_CAP_END  (2u + T_TITLE_CHARS * 4u)

/* F4HWN gFontLight: 9 status columns, LSB is the top pixel. */
static const uint8_t LIGHT_ICON[9] = {
    0x0Cu, 0x12u, 0x21u, 0x6Du, 0x79u, 0x6Du, 0x21u, 0x12u, 0x0Cu
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
    uint8_t pref;     /* PREF_FSK / PREF_OOK / PREF_AUTO */
    uint8_t disp;     /* DISP_OFF / DISP_TMO */
} app_cfg_t;

static struct {
    const app_api_t *A;
    uint8_t prevKey, led, nRows, refresh, menu, menuSel, blOff, path, spk;
    uint16_t ledCnt, keyCnt;
    int16_t rssi;
    uint32_t autoAt, lastHit, lastKey;
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
 * Status-line demod text for the current preference.
 *
 * AUTO shows A:F or A:O for the path in use.
 * A fixed preference shows FSK or OOK.
 *
 * @param ui Packed UI strings for this flavour.
 * @return Pointer at the label inside ui.
 */
static const char *modeLabel(const char *ui)
{
    if (g.cfg.pref == PREF_AUTO)
        return ui + (g.path == TPMS_MODE_FSK ? T_AF : T_AO);
    if (g.cfg.pref == PREF_OOK) return ui + T_OOK;
    return ui + T_FSK;
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
 * Choose the demod the slicer should run for the saved preference.
 *
 * @return TPMS_MODE_FSK or TPMS_MODE_OOK.
 */
static uint8_t activePath(void)
{
    if (g.cfg.pref == PREF_OOK) return TPMS_MODE_OOK;
    if (g.cfg.pref == PREF_AUTO) return g.path;
    return TPMS_MODE_FSK;
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
    uint8_t path = activePath();
    g.A->bk_write(REG_38, (uint16_t)(f & 0xFFFFu));
    g.A->bk_write(REG_39, (uint16_t)(f >> 16));
    g.A->tones_off_rx();
    /* Shared front-end: wide IF, filters bypassed, AFC off, fixed gain. */
    g.A->bk_write(REG_2B, (uint16_t)((g.A->bk_read(REG_2B) | 0x0700u) & ~0x0007u));
    g.A->bk_write(REG_73, (uint16_t)(g.A->bk_read(REG_73) | 0x0010u));
    g.A->bk_write(REG_43, REG_43_WIDE);
    g.A->set_agc(false);
    /* AF last: clears mute from tones_off_rx. FSK = raw bypass; OOK = AM. */
    if (path == TPMS_MODE_FSK)
        g.A->bk_write(REG_47, REG_47_BYPASS);
    else
        g.A->set_af(APP_AF_AM);
    selectUhfLna();
    g.A->audio_path(g.spk != 0u);
}

/**
 * Switch demod, retune, and clear the slicer.
 *
 * Does nothing when the radio is already on that path.
 *
 * @param path TPMS_MODE_FSK or TPMS_MODE_OOK.
 * @param dec Slicer to reset after the retune.
 */
static void setPath(uint8_t path, dectpms_t *dec)
{
    if (g.path == path) return;
    g.path = path;
    applyRf();
    dectpms_init(dec, path);
    g.autoAt = g.A->ticks_ms();
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
 * Draw the status bulb when the light setting is on and the gap fits.
 *
 * The bulb sits between the title capsule and the band.
 * Display off hides it, including after the 0 key.
 *
 * @param bandX Left edge of the band text, in pixels.
 */
static void drawLight(uint8_t bandX)
{
    uint8_t *line;
    unsigned gap;
    if (g.cfg.disp != DISP_TMO) return;
    if (bandX < TITLE_CAP_END + 2u + sizeof LIGHT_ICON) return;
    line = g.A->status_line;
    if (!line) return;
    gap = (unsigned)(bandX - (TITLE_CAP_END + 1u) - sizeof LIGHT_ICON);
    memcpy(line + (TITLE_CAP_END + 1u) + (gap >> 1), LIGHT_ICON, sizeof LIGHT_ICON);
}

/**
 * Draw the status line: title, bulb, band, demod, and speaker mark.
 *
 * @param ui Packed UI strings for this flavour.
 */
static void drawStatus(const char *ui)
{
    char str[20];
    char *o;
    unsigned width;
    g.A->status_clear();
    g.A->draw_battery();
    g.A->print_inverse(ui + (g.menu ? T_MENU : T_TITLE), 2, 0, true, true,
                       (uint8_t)(2u + T_TITLE_CHARS * 4u));
    o = putBand(str);
    *o++ = ' ';
    o = put(o, modeLabel(ui));
    if (g.spk) {
        *o++ = ' ';
        *o++ = 'S';
    }
    *o = '\0';
    width = (unsigned)(o - str) * 4u;
    /* 22 px clears the title capsule. 48 px covers the battery icon plus text. */
    if (width + 22u + BATT_RESERVE <= 128u) {
        uint8_t bandX = (uint8_t)(128u - BATT_RESERVE - width);
        g.A->print_tiny(str, bandX, 1, true, true);
        drawLight(bandX);
    } else
        g.A->print_tiny(str, 2, 1, false, true);
}

/**
 * Draw the options menu and its help line.
 *
 * @param ui Packed UI strings for this flavour.
 */
static void drawMenu(const char *ui)
{
    char str[28];
    char *o;
#if defined(TPMS_FLAVOUR_EU2)
    const uint16_t labels[1] = { T_CLR };
#elif defined(TPMS_FLAVOUR_NA2)
    const uint16_t labels[2] = { T_BAND, T_CLR };
#else
    const uint16_t labels[3] = { T_BAND, T_MODE, T_CLR };
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
#if !TPMS_LOCK_MODE
        else if (i == MENU_MODE) {
            *o++ = ' ';
            if (g.cfg.pref == PREF_AUTO) o = put(o, ui + T_AUTO);
            else if (g.cfg.pref == PREF_OOK) o = put(o, ui + T_OOK);
            else o = put(o, ui + T_FSK);
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

#if !TPMS_LOCK_MODE
/**
 * Cycle FSK, OOK, and AUTO, then retune.
 *
 * @param dec Slicer to reset on the new path.
 */
static void cyclePref(dectpms_t *dec)
{
    if (g.cfg.pref == PREF_FSK) g.cfg.pref = PREF_OOK;
    else if (g.cfg.pref == PREF_OOK) g.cfg.pref = PREF_AUTO;
    else g.cfg.pref = PREF_FSK;
    if (g.cfg.pref != PREF_AUTO) {
        g.path = (g.cfg.pref == PREF_OOK) ? TPMS_MODE_OOK : TPMS_MODE_FSK;
    }
    applyRf();
    dectpms_init(dec, activePath());
    g.autoAt = g.A->ticks_ms();
}
#endif

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
            dectpms_init(dec, activePath());
            break;
#endif
#if !TPMS_LOCK_MODE
        case MENU_MODE:
            cyclePref(dec);
            break;
#endif
        case MENU_CLR:
            g.nRows = 0;
            break;
        default:
            break;
    }
#if TPMS_LOCK_BAND && TPMS_LOCK_MODE
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
        if (key == APP_KEY_MENU || key == APP_KEY_STAR || key == APP_KEY_2 || key == APP_KEY_5)
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
            dectpms_init(dec, activePath());
            break;
#endif
#if !TPMS_LOCK_MODE
        case APP_KEY_STAR:
            cyclePref(dec);
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
 * In AUTO, swap demod after the dwell unless a fresh decode is held.
 *
 * @param dec Slicer to reset when the path changes.
 */
static void serviceAuto(dectpms_t *dec)
{
    uint32_t now;
    if (g.cfg.pref != PREF_AUTO) return;
    now = g.A->ticks_ms();
    if ((uint32_t)(now - g.lastHit) < AUTO_HOLD_MS) return;
    if ((uint32_t)(now - g.autoAt) < AUTO_DWELL_MS) return;
    setPath((uint8_t)(g.path == TPMS_MODE_FSK ? TPMS_MODE_OOK : TPMS_MODE_FSK), dec);
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
    dectpms_init(&dec, activePath());
    uint32_t (*const clk)(void) = g.A->cycles_48mhz;
    uint16_t (*const sample)(void) = g.A->af_capture_sample;
    uint32_t next = clk();
    bool wantUi = true;
    g.autoAt = g.A->ticks_ms();
    g.lastHit = 0;
    g.lastKey = g.autoAt;

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
            wantUi = true;
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
        serviceAuto(&dec);
        if (serviceDisp()) wantUi = true;

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

        if (wantUi || ++g.refresh >= REFRESH_POLLS) {
            g.refresh = 0;
            g.rssi = g.A->rssi_dbm();
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
    api->led(false);
    api->asset_read(0, ui, UI_SIZE);

    api->af_capture_open();
    r43 = api->bk_read(REG_43);
    r2b = api->bk_read(REG_2B);

    /* Defaults per flavour (first launch). Saved band/mode are restored when
     * present, then lock flags below force OOK / 433.92 where the sensors need it. */
    api->cfg_load(cfg, 4);
    if (cfg[0] == CFG_MAGIC || cfg[0] == 0xB1u) {
        g.cfg.band433 = cfg[1] ? 1u : 0u;
        if (cfg[0] == CFG_MAGIC && cfg[2] <= PREF_AUTO) g.cfg.pref = cfg[2];
        else g.cfg.pref = cfg[2] ? PREF_OOK : PREF_FSK;
        if (cfg[0] == CFG_MAGIC && cfg[3] == DISP_OFF) g.cfg.disp = DISP_OFF;
    } else {
#if defined(TPMS_FLAVOUR_NA) || defined(TPMS_FLAVOUR_NA2)
        g.cfg.band433 = 0u; /* 315.00 MHz */
#else
        g.cfg.band433 = 1u; /* 433.92 MHz */
#endif
#if TPMS_LOCK_MODE
        g.cfg.pref = PREF_OOK;
#else
        g.cfg.pref = PREF_FSK;
#endif
    }
#if TPMS_LOCK_MODE
    g.cfg.pref = PREF_OOK;
#endif
#if TPMS_LOCK_BAND
    g.cfg.band433 = 1u;
#endif
    if (g.cfg.disp == DISP_OFF) g.blOff = 1;
    else api->backlight_on();
    g.path = (g.cfg.pref == PREF_OOK) ? TPMS_MODE_OOK : TPMS_MODE_FSK;

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
    cfg[2] = g.cfg.pref;
    cfg[3] = g.cfg.disp;
    api->cfg_save(cfg, 4);
}
