/* Copyright 2026 — Apache-2.0
 * TPMS DBG: hear the receiver and record raw bursts. No TPMS decode.
 * Same park and AF route as the list apps (315.00 / 433.92, FSK bypass or OOK AM).
 * A burst is the sliced PCM the list app would have parsed. The last one stays
 * on screen and, if a USB host has the port open, goes out as one line.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app_api.h"

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
#define LNA_UHF        0x0008u
#define LNA_VHF        0x0004u
#define FREQ_315       31500000u
#define FREQ_433       43392000u
#define KEY_EVERY      1536u
#define COL_SAMPLES    768u
#define FEED_SLACK     800
#define CAP_BITS       192u
#define CAP_BYTES      (CAP_BITS / 8u)
#define STEP_FSK       32768u
#define STEP_OOK       14245u
#define GAP_FSK        8u
#define GAP_OOK        24u
#define LED_STEP       (FS / 10u)

static struct {
    const app_api_t *A;
    uint8_t prevKey, spk, fsk, band433, wfX, led, hot, level, half;
    uint8_t bitn, showN, bursts, logged;
    uint16_t ledCnt, keyCnt, colN, afCol, afNow, afPk, gap, phase;
    int16_t rssi, rssiPk, bRssi, logRssi;
    int32_t mean, amp, bAf, logAf;
    bool lcd, want;
    uint8_t wf[128];
    uint8_t cap[CAP_BYTES];
    uint8_t show[CAP_BYTES];
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
 * Map a nibble to an uppercase hex digit.
 *
 * @param n Value from 0 to 15.
 * @return The digit character.
 */
static char hexd(uint8_t n)
{
    return (char)(n < 10u ? (char)('0' + n) : (char)('A' + (n - 10u)));
}

/**
 * Append bytes as uppercase hex.
 *
 * @param o Cursor to write at.
 * @param b Bytes to print.
 * @param n Number of bytes.
 * @return Cursor after the digits.
 */
static char *putHex(char *o, const uint8_t *b, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        *o++ = hexd((uint8_t)(b[i] >> 4));
        *o++ = hexd((uint8_t)(b[i] & 0x0fu));
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
 * Park on 315.000 or 433.920 MHz and point AF at FSK or OOK.
 *
 * tones_off_rx pulses the PLL and mutes audio, so the IF and AF writes follow.
 */
static void applyRf(void)
{
    uint32_t f = g.band433 ? FREQ_433 : FREQ_315;
    g.A->bk_write(REG_38, (uint16_t)(f & 0xFFFFu));
    g.A->bk_write(REG_39, (uint16_t)(f >> 16));
    g.A->tones_off_rx();
    g.A->bk_write(REG_2B, (uint16_t)((g.A->bk_read(REG_2B) | 0x0700u) & ~0x0007u));
    g.A->bk_write(REG_73, (uint16_t)(g.A->bk_read(REG_73) | 0x0010u));
    g.A->bk_write(REG_43, REG_43_WIDE);
    g.A->set_agc(false);
    if (g.fsk) g.A->bk_write(REG_47, REG_47_BYPASS);
    else g.A->set_af(APP_AF_AM);
    selectUhfLna();
    g.A->audio_path(g.spk != 0u);
}

/**
 * Clear the slicer so the next edge starts a new burst.
 */
static void rearm(void)
{
    g.mean = 0;
    g.amp = 0;
    g.phase = 0;
    g.gap = g.fsk ? GAP_FSK : GAP_OOK;
    g.level = 0;
    g.half = 0;
    g.hot = 0;
    g.bitn = 0;
}

/**
 * Keep the burst that just ended and ask for a redraw.
 */
static void commit(void)
{
    uint8_t n = g.bitn > CAP_BITS ? (uint8_t)CAP_BITS : g.bitn;
    uint8_t nb = (uint8_t)((n + 7u) >> 3);
    uint8_t i;
    for (i = 0; i < nb; i++) g.show[i] = g.cap[i];
    for (; i < CAP_BYTES; i++) g.show[i] = 0;
    g.showN = g.bitn;
    g.logRssi = g.bRssi;
    g.logAf = g.bAf;
    if (g.bursts < 255u) g.bursts++;
    g.led = 2u;
    g.ledCnt = 0;
    g.A->led(true);
    g.want = true;
}

/**
 * Slice one AF sample into the open burst.
 *
 * The bit clock matches dectpms. There is no Manchester decode.
 *
 * @param sample One AF sample, mid-scale 2048.
 */
static void push(uint16_t sample)
{
    int32_t x = (int32_t)sample - 2048;
    int32_t ax = x < 0 ? -x : x;
    int32_t y, ay, thr;
    uint8_t lvl;
    uint16_t lim, step, prev;

    if (ax > 2047) ax = 2047;
    if ((uint16_t)ax > g.afCol) g.afCol = (uint16_t)ax;

    g.mean += x - (g.mean >> 4);
    y = x - (g.mean >> 4);
    ay = y < 0 ? -y : y;
    if (ay > g.amp) g.amp = ay;
    else g.amp -= g.amp >> 6;

    thr = g.amp > 160 ? (g.amp >> 3) : 40;
    lvl = g.level;
    if (y > thr) lvl = 1u;
    else if (y < -thr) lvl = 0u;
    g.level = lvl;

    lim = g.fsk ? GAP_FSK : GAP_OOK;
    if (g.amp <= 120) {
        if (g.gap < lim) g.gap++;
        if (g.gap >= lim) {
            if (g.hot && g.bitn >= 16u) commit();
            g.hot = 0;
            g.bitn = 0;
            return;
        }
    } else {
        if (!g.hot) {
            g.bitn = 0;
            g.phase = 0;
            g.bAf = 0;
            g.bRssi = g.rssi;
            memset(g.cap, 0, sizeof g.cap);
        }
        g.hot = 1u;
        g.gap = 0;
        if (ay > g.bAf) g.bAf = ay;
        if (g.rssi > g.bRssi) g.bRssi = g.rssi;
    }

    step = g.fsk ? STEP_FSK : STEP_OOK;
    prev = g.phase;
    g.phase = (uint16_t)(g.phase + step);
    if (g.phase >= prev) return;

    if (lvl != g.half) {
        if (g.phase > 32768u) g.phase = (uint16_t)(g.phase - 8192u);
        else g.phase = (uint16_t)(g.phase + 8192u);
    }
    g.half = lvl;
    if (g.bitn < CAP_BITS) {
        uint8_t i = g.bitn;
        uint8_t mask = (uint8_t)(0x80u >> (i & 7u));
        if (lvl) g.cap[i >> 3] = (uint8_t)(g.cap[i >> 3] | mask);
        else g.cap[i >> 3] = (uint8_t)(g.cap[i >> 3] & (uint8_t)~mask);
    }
    if (g.bitn < 255u) g.bitn++;
}

/**
 * Pack the last 20 ms of RSSI and peak audio into one column.
 */
static void column(void)
{
    uint8_t afh = (uint8_t)(g.afCol >> 6);
    int16_t d = (int16_t)(g.rssi + 120);
    uint8_t rh;
    if (afh > 15u) afh = 15u;
    g.afNow = g.afCol;
    if (g.afCol > g.afPk) g.afPk = g.afCol;
    g.afCol = 0;
    if (d < 0) d = 0;
    if (d > 60) d = 60;
    rh = (uint8_t)(d >> 2);
    g.wf[g.wfX] = (uint8_t)((rh << 4) | afh);
    g.wfX = (uint8_t)((g.wfX + 1u) & 127u);
}

/**
 * Draw a vertical bar upward from a pixel row.
 *
 * @param x Column.
 * @param bottom Bottom pixel, inclusive.
 * @param h Height in pixels, clamped to 15.
 */
static void col(uint8_t x, uint8_t bottom, uint8_t h)
{
    uint8_t (*fb)[128] = g.A->fb;
    if (h > 15u) h = 15u;
    for (uint8_t i = 0; i < h; i++) {
        uint8_t y = (uint8_t)(bottom - i);
        fb[y >> 3][x] |= (uint8_t)(1u << (y & 7u));
    }
}

/**
 * Draw the status line: DBG, band, demod, speaker, and battery.
 */
static void drawStatus(void)
{
    char str[16];
    char *o = puti(str, g.band433 ? 433 : 315);
    unsigned w;
    g.A->status_clear();
    g.A->draw_battery();
    g.A->print_inverse("DBG", 2, 0, true, true, 14);
    *o++ = '.';
    if (g.band433) {
        *o++ = '9';
        *o++ = '2';
    } else {
        *o++ = '0';
        *o++ = '0';
    }
    *o++ = ' ';
    *o++ = (char)(g.fsk ? 'F' : 'O');
    if (g.spk) {
        *o++ = ' ';
        *o++ = 'S';
    }
    *o = '\0';
    w = (unsigned)(o - str) * 4u;
    g.A->print_tiny(str, (uint8_t)(128u - 48u - w), 1, true, true);
}

/**
 * Draw the recorder screen and both waterfall strips.
 */
static void draw(void)
{
    char str[28];
    char *o;
    uint8_t (*fb)[128] = g.A->fb;
    uint8_t nb, x, p;
    uint16_t r47 = g.A->bk_read(REG_47);
    uint16_t r33 = g.A->bk_read(REG_33);

    g.A->display_clear();
    drawStatus();

    o = str;
    *o++ = 'R';
    o = puti(o, g.rssi);
    *o++ = ' ';
    o = puti(o, g.rssiPk);
    tiny(str, 0, 0, o);

    o = str;
    *o++ = 'A';
    o = puti(o, (int32_t)g.afNow);
    *o++ = '/';
    o = puti(o, (int32_t)g.afPk);
    *o++ = ' ';
    *o++ = 'F';
    o = puti(o, (int32_t)((r47 >> 8) & 0x0fu));
    *o++ = ' ';
    *o++ = 'U';
    o = puti(o, (r33 & LNA_UHF) ? 1 : 0);
    *o++ = ' ';
    *o++ = 'N';
    o = puti(o, g.bursts);
    tiny(str, 0, 8, o);

    o = str;
    *o++ = 'B';
    o = puti(o, g.showN);
    *o++ = ' ';
    nb = (uint8_t)(((g.showN > CAP_BITS ? CAP_BITS : g.showN) + 7u) >> 3);
    if (nb > 8u) nb = 8u;
    o = putHex(o, g.show, nb);
    tiny(str, 0, 16, o);

    for (p = 3; p < 7u; p++) memset(fb[p], 0, 128);
    for (x = 0; x < 128u; x++) {
        uint8_t s = g.wf[(uint8_t)((g.wfX + x) & 127u)];
        col(x, 39, (uint8_t)(s >> 4));
        col(x, 55, (uint8_t)(s & 15u));
    }
}

/**
 * Write the last burst when a USB host is holding the port open.
 */
static void logBurst(void)
{
    char b[96];
    char *o;
    uint8_t nb;
    if (g.A->api_size < 380u) return;
    if ((g.A->serial_ctl(APP_SERIAL_USB, 0) & APP_SERIAL_HOST) == 0) return;
    o = put(b, "dbg ");
    o = puti(o, g.band433 ? 433 : 315);
    *o++ = ' ';
    *o++ = (char)(g.fsk ? 'F' : 'O');
    o = put(o, " r=");
    o = puti(o, g.logRssi);
    o = put(o, " a=");
    o = puti(o, g.logAf);
    o = put(o, " n=");
    o = puti(o, g.bursts);
    o = put(o, " b=");
    o = puti(o, g.showN);
    *o++ = ' ';
    nb = (uint8_t)(((g.showN > CAP_BITS ? CAP_BITS : g.showN) + 7u) >> 3);
    if (nb > 16u) nb = 16u;
    o = putHex(o, g.show, nb);
    *o++ = '\n';
    g.A->serial_write(APP_SERIAL_USB, b, (uint16_t)(o - b));
}

/**
 * Handle one new keypress.
 *
 * @return True when the app should exit.
 */
static bool keys(void)
{
    uint8_t key = g.A->get_key();
    bool edge = key != g.prevKey && key != APP_KEY_INVALID;
    g.prevKey = key;
    if (!edge) return false;
    g.A->backlight_on();
    g.want = true;
    switch (key) {
        case APP_KEY_EXIT:
            return true;
        case APP_KEY_1:
            g.spk ^= 1u;
            g.A->audio_path(g.spk != 0u);
            break;
        case APP_KEY_2:
            g.band433 ^= 1u;
            applyRf();
            rearm();
            break;
        case APP_KEY_STAR:
            g.fsk ^= 1u;
            applyRf();
            rearm();
            break;
        case APP_KEY_5:
            g.afPk = 0;
            g.afNow = 0;
            g.afCol = 0;
            g.rssiPk = g.rssi;
            g.bursts = 0;
            g.logged = 0;
            g.showN = 0;
            memset(g.wf, 0, sizeof g.wf);
            memset(g.show, 0, sizeof g.show);
            rearm();
            break;
        default:
            break;
    }
    return false;
}

/**
 * Sample audio until EXIT, recording bursts and refreshing the screen.
 */
static void listen(void)
{
    uint32_t (*const clk)(void) = g.A->cycles_48mhz;
    uint16_t (*const sample)(void) = g.A->af_capture_sample;
    uint32_t next = clk();
    g.want = true;

    for (;;) {
        while ((int32_t)(clk() - next) < 0) {
            if (g.lcd && (int32_t)(next - clk()) > FEED_SLACK)
                g.lcd = g.A->lcd_feed(1);
        }
        next += CYC_PER_SAMPLE;
        push(sample());
        if (++g.colN >= COL_SAMPLES) {
            g.colN = 0;
            g.rssi = g.A->rssi_dbm();
            if (g.rssi > g.rssiPk) g.rssiPk = g.rssi;
            column();
            next = clk() + CYC_PER_SAMPLE;
        }

        if (g.led && ++g.ledCnt >= LED_STEP) {
            g.ledCnt = 0;
            g.led--;
            g.A->led(g.led & 1u);
        }

        if (!g.want && ++g.keyCnt < KEY_EVERY) continue;
        g.keyCnt = 0;
        if (keys()) break;
        g.A->backlight_update();
        g.A->backlight_on();

        if (g.want || !g.lcd) {
            g.want = false;
            g.A->battery_sample();
            draw();
            if (g.logged != g.bursts) {
                logBurst();
                g.logged = g.bursts;
            }
            if (g.A->api_size >= 392u) g.lcd = g.A->lcd_feed(0);
            if (!g.lcd) {
                g.A->blit_status();
                g.A->blit_full();
            }
            next = clk() + CYC_PER_SAMPLE;
        }
    }
}

/**
 * Overlay entry for the burst recorder.
 *
 * Listen, then restore the receiver registers.
 *
 * @param api Firmware service table.
 */
__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    uint16_t r43, r2b;
    g.A = api;
    g.prevKey = APP_KEY_INVALID;
    g.spk = 0;
    g.fsk = 1u;
    g.band433 = 1u;
    g.rssi = -130;
    g.rssiPk = -200;
    g.logged = 0;
    g.bursts = 0;
    memset(g.wf, 0, sizeof g.wf);
    memset(g.show, 0, sizeof g.show);
    api->led(false);
    api->backlight_on();
    api->af_capture_open();
    r43 = api->bk_read(REG_43);
    r2b = api->bk_read(REG_2B);
    rearm();
    applyRf();
    api->delay_ms(50);
    listen();
    api->bk_write(REG_43, r43);
    api->bk_write(REG_2B, r2b);
    api->set_agc(true);
    api->af_capture_close();
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
    api->led(false);
}
