/* Copyright 2026 — Apache-2.0
 * TPMS CAP: stream the sliced FSK audio out of the K-plug for a PC.
 *
 * Parks and slices exactly like TPMS EU / NA (same band, tuning offset and
 * IF presets, same dectpms slicer), then sends every sliced sample over the
 * K-plug serial lines (the programming cable UV Studio uses) at 115200 baud.
 * tools/kplug.html reads the stream in a browser (Web Serial) and decodes
 * Toyota (PMV-C210 and PMV-107J) and Ford the way rtl_433 does, so each
 * burst can be checked bit by bit on the PC.
 *
 * Wire format, one byte at a time:
 *   0xxxxxxx  seven sliced samples, oldest in bit 6, 38400 samples a second
 *   0x80      gap: sampling paused (screen redraw, retune or a full queue)
 *   0x81 s v  settings: s bit 0 band (1 = 433.92), bits 2:1 IF preset,
 *             bits 6:3 tuning offset nibble (two's complement, 5 kHz steps);
 *             v bits 1:0 the SENS level (0 LOW, 1 MID, 2 HIGH), bit 2 the
 *             forced backlight, bit 3 the demod (1 = OOK)
 *   0x82 m    the radio's own decoder accepted a packet of model m
 *
 * The PC can set them too. A command is 0xA5, c, c ^ 0xFF (the check byte
 * keeps line noise from changing a setting); c is a field and a value:
 *   0x1v band (v 1 = 433.92)   0x2v IF preset    0x3v offset nibble
 *   0x4v SENS level            0x5v backlight    0x60 clear counters
 *   0x70 send the settings record now            0x8v demod (v 1 = OOK)
 *
 * OOK routes AM audio as TPMS EU2 / NA2 do, for Schrader, GM, BMW and Nissan
 * sensors. Only the FSK parsers fit here, so in OOK the PC does all the
 * decoding and no 0x82 records are sent.
 *
 * The app API reserves the K-plug port but does not implement it yet, so
 * this app drives USART1 of the PY32F071 directly. The resident firmware has
 * already set it up at 38400 baud for K5Viewer; the app only changes the
 * baud rate while it runs and restores it on exit. Close UV Studio and
 * K5Viewer first: their frames would mix into the stream.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app_api.h"
/* Build the FSK parsers (Ford, Toyota) of dectpms, as in TPMS EU. */
#define TPMS_FLAVOUR_EU 1
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
#define REG_47         0x47
#define REG_47_BYPASS  0x6942u
#define REG_33         0x33
#define REG_38         0x38
#define REG_39         0x39
#define LNA_UHF        0x0008u
#define LNA_VHF        0x0004u
#define FREQ_315       31500000u
#define FREQ_433       43392000u
#define FOFF_STEP      500
#define KEY_EVERY      1536u
#define LED_STEP       (FS / 10u)
#define SET_EVERY      (FS * 2u)
#define DRAW_HOLD_MS   200u
#define DRAW_EVERY_MS  10000u
/* 0xC2: cfg[2] gained the SENS level, so 0xC1 saves load the defaults. */
#define CFG_MAGIC      0xC2u
/* SENS levels as in TPMS EU: 0 LOW, 1 MID (the old fixed gate), 2 HIGH. */
#define SENS_LEVELS    3u
#define SENS_DEFAULT   1u
/* cfg[1]: bit 0 band, bit 1 the force backlight option (key 0). */
#define CFG_LIGHT      0x02u
/* cfg[1] bit 2: OOK demod (AM audio) instead of FSK. */
#define CFG_OOK        0x04u
/* As TPMS EU: the lamp stays on this long after the last key, unless forced. */
#define DISP_IDLE_MS   10000u
/* Radio backlight time in storage (gEeprom.BACKLIGHT_TIME, firmware v6.3.0
 * settings block 0xA008 byte 5). 61 is always on. */
#define BL_TIME_ADDR   0x00A00Du
#define BL_ALWAYS      61u
/* "CAP" capsule end (2 + 3 cells of 4 px) and the band text start. */
#define TITLE_END      14u
#define BAND_X         28u

/* USART1 of the PY32F071 (STM32F1-style registers), wired to the K-plug. */
#define USART1_SR      (*(volatile uint32_t *)0x40013800u)
#define USART1_DR      (*(volatile uint32_t *)0x40013804u)
#define USART1_BRR     (*(volatile uint32_t *)0x40013808u)
#define USART1_CR1     (*(volatile uint32_t *)0x4001380Cu)
#define USART1_CR3     (*(volatile uint32_t *)0x40013814u)
#define SR_RXNE        0x20u
#define SR_TC          0x40u
#define SR_TXE         0x80u
#define CR1_TE         0x0008u
#define CR1_UE         0x2000u
#define CR3_OVER8      0x0800u
#define CR3_DMAR       0x0040u
#define CMD_SYNC       0xA5u
/* 48 MHz cycles to wait for the line to go idle: ~10 ms. */
#define TC_WAIT        480000u

#define MARK_GAP       0x80u
#define MARK_SET       0x81u
#define MARK_HIT       0x82u
#define RING           64u

/* Decoder signal gate per SENS level, as in TPMS EU: a lower gate lets
 * weaker bursts through, a higher one ignores more noise. */
static const uint8_t SENS_GATE[SENS_LEVELS] = { 60u, 40u, 28u };
static const char *const SENS_NAME[SENS_LEVELS] = { "LOW", "MID", "HIGH" };

/* Same presets as TPMS EU / NA (STAR cycles them). */
static const uint16_t IF_PRESET[4] = { 0x7E2Cu, 0x7E28u, 0x7E0Cu, 0x7E08u };

/* F4HWN gFontLight, as in TPMS EU: 9 status columns, LSB is the top pixel. */
static const uint8_t LIGHT_ICON[9] = {
    0x0Cu, 0x12u, 0x21u, 0x6Du, 0x79u, 0x6Du, 0x21u, 0x12u, 0x0Cu
};

static struct {
    const app_api_t *A;
    uint8_t prevKey, spk, band433, ifp, foff, led, uart, lost, drawDue;
    uint8_t acc, accN, head, tail;
    uint8_t light, blOff, blTime, sens, ook;  /* forced lamp, lamp released, radio BL time */
    uint8_t fn;       /* 1 after F: the next * or 3 steps back */
    uint8_t rxState, rxCmd, cmd;  /* PC command parser; cmd waits for the next poll */
    uint16_t ledCnt, keyCnt, hits, gaps;
    uint32_t brr, cr3, sent, setCnt, lastNote, lastDraw, lastKey;
    uint8_t ring[RING];
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
 * Terminate a tiny-font string and draw it at the left edge.
 *
 * @param buf Start of the string.
 * @param y Body-pixel row.
 * @param end Cursor just past the last character.
 */
__attribute__((noinline))
static void row(char *buf, uint8_t y, char *end)
{
    *end = '\0';
    g.A->print_tiny(buf, 2, y, false, true);
}

/**
 * Tuning offset in 5 kHz steps, from the saved nibble.
 *
 * @return -8 to 7.
 */
static int32_t foffSteps(void)
{
    return (int8_t)(uint8_t)(g.foff << 4) >> 4;
}

/**
 * Queue one byte for the K-plug. A full queue drops it and marks a gap.
 *
 * @param b Byte to send.
 */
static void queue(uint8_t b)
{
    uint8_t n = (uint8_t)((g.head + 1u) & (RING - 1u));
    if (n == g.tail) {
        g.lost = 1u;
        return;
    }
    g.ring[g.head] = b;
    g.head = n;
}

/**
 * Hand the next queued byte to the UART when it can take one.
 */
static void txPoll(void)
{
    if (g.head == g.tail || !(USART1_SR & SR_TXE)) return;
    USART1_DR = g.ring[g.tail];
    g.tail = (uint8_t)((g.tail + 1u) & (RING - 1u));
    g.sent++;
}

/**
 * Read a byte from the PC when one has arrived and parse commands.
 *
 * A command is CMD_SYNC, c, c ^ 0xFF. A good one waits in g.cmd for the next
 * key poll, where it is applied like a key.
 */
static void rxPoll(void)
{
    uint8_t b;
    if (!(USART1_SR & SR_RXNE)) return;
    b = (uint8_t)USART1_DR;
    if (g.rxState == 1u) {
        g.rxCmd = b;
        g.rxState = 2u;
        return;
    }
    if (g.rxState == 2u && b == (uint8_t)~g.rxCmd) {
        g.cmd = g.rxCmd;
        g.rxState = 0;
        return;
    }
    g.rxState = (b == CMD_SYNC) ? 1u : 0u;
}

/**
 * Wait until the queue is sent and the line is idle, or time runs out.
 */
static void txDrain(void)
{
    uint32_t t0 = g.A->cycles_48mhz();
    while ((uint32_t)(g.A->cycles_48mhz() - t0) < TC_WAIT) {
        txPoll();
        if (g.head == g.tail && (USART1_SR & SR_TC)) return;
    }
}

/**
 * Mark a pause in the sample stream and drop the partial sample byte.
 */
static void markGap(void)
{
    g.accN = 0;
    g.acc = 0;
    g.lost = 0;
    queue(MARK_GAP);
    if (g.gaps < 0xffffu) g.gaps++;
}

/**
 * Queue the settings record so the PC knows the band, IF, offset and SENS.
 */
static void markSettings(void)
{
    queue(MARK_SET);
    queue((uint8_t)(g.band433 | (g.ifp << 1) | (g.foff << 3)));
    queue((uint8_t)(g.sens | (g.light << 2) | (g.ook << 3)));
    g.setCnt = 0;
}

/**
 * Reset the slicer at the saved SENS level.
 *
 * @param dec Slicer to reset.
 */
static void decInit(dectpms_t *dec)
{
    dectpms_init(dec, g.ook ? TPMS_MODE_OOK : TPMS_MODE_FSK);
    dec->gate = SENS_GATE[g.sens];
}

/**
 * Switch the K-plug UART to 115200 baud for the stream.
 *
 * The resident firmware leaves USART1 at 38400 baud with 16x oversampling,
 * where BRR is the bus clock over the baud rate, so a third of it is
 * 115200. Anything else is left alone and nothing is sent.
 *
 * @return True when the UART is ready to stream.
 */
static bool uartOpen(void)
{
    uint32_t cr1 = USART1_CR1;
    if (!(cr1 & CR1_UE) || !(cr1 & CR1_TE) || (USART1_CR3 & CR3_OVER8)) return false;
    g.brr = USART1_BRR;
    if (g.brr < 300u || g.brr > 0xffffu) return false;
    txDrain();
    USART1_CR1 = cr1 & ~CR1_UE;
    USART1_BRR = (g.brr + 1u) / 3u;
    /* Received bytes come to the app (rxPoll), not the resident DMA ring. */
    g.cr3 = USART1_CR3;
    USART1_CR3 = g.cr3 & ~CR3_DMAR;
    USART1_CR1 = cr1;
    (void)USART1_SR;
    (void)USART1_DR;
    return true;
}

/**
 * Send what is queued, then give the UART back: resident baud rate and DMA.
 */
static void uartClose(void)
{
    uint32_t cr1 = USART1_CR1;
    txDrain();
    USART1_CR1 = cr1 & ~CR1_UE;
    USART1_BRR = g.brr;
    USART1_CR3 = g.cr3;
    USART1_CR1 = cr1;
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
 * Park on the band plus the offset and route FSK audio as TPMS EU does.
 */
static void applyRf(void)
{
    uint32_t f = g.band433 ? FREQ_433 : FREQ_315;
    f += (uint32_t)(foffSteps() * FOFF_STEP);
    g.A->bk_write(REG_38, (uint16_t)(f & 0xFFFFu));
    g.A->bk_write(REG_39, (uint16_t)(f >> 16));
    g.A->tones_off_rx();
    g.A->bk_write(REG_2B, (uint16_t)((g.A->bk_read(REG_2B) | 0x0700u) & ~0x0007u));
    g.A->bk_write(REG_73, (uint16_t)(g.A->bk_read(REG_73) | 0x0010u));
    g.A->bk_write(REG_43, IF_PRESET[g.ifp]);
    g.A->set_agc(false);
    /* AF last: FSK = raw bypass, OOK = AM, as TPMS EU / EU2. */
    if (g.ook) g.A->set_af(APP_AF_AM);
    else g.A->bk_write(REG_47, REG_47_BYPASS);
    selectUhfLna();
    g.A->audio_path(g.spk != 0u);
}

/**
 * Draw the status line and the capture counters, then blit them.
 *
 * blit_page is used rather than blit_full, which would also send a
 * K5Viewer frame into the stream.
 *
 * @param dec Slicer whose Toyota counter is shown.
 */
__attribute__((noinline))
static void draw(const dectpms_t *dec)
{
    char str[36];
    char *o;

    g.A->display_clear();
    g.A->status_clear();
    g.A->draw_battery();
    g.A->print_inverse("CAP", 2, 0, true, true, 14);
    o = put(str, g.band433 ? "433.92 " : "315.00 ");
    o = put(o, g.ook ? "OOK" : "FSK");
    *o = '\0';
    g.A->print_tiny(str, BAND_X, 1, true, true);
    /* The bulb sits between the capsule and the band while the lamp is forced on. */
    if (g.light && g.A->status_line) {
        memcpy(g.A->status_line + TITLE_END + 1u
               + ((BAND_X - TITLE_END - 1u - sizeof LIGHT_ICON) >> 1),
               LIGHT_ICON, sizeof LIGHT_ICON);
    }

    o = put(str, g.uart ? "K-PLUG 115200: KPLUG.HTML" : "NO K-PLUG UART");
    row(str, 8, o);
    o = puti(str, foffSteps() * 5);
    o = put(o, "K IF");
    o = putHex(o, IF_PRESET[g.ifp], 4);
    o = put(o, " SENS ");
    o = put(o, SENS_NAME[g.sens]);
    row(str, 16, o);
    o = put(str, "TOYT ");
    o = puti(o, dec->ty_best);
    o = put(o, "/72 HITS ");
    o = puti(o, g.hits);
    row(str, 24, o);
    o = put(str, "SENT ");
    o = puti(o, (int32_t)(g.sent >> 10));
    o = put(o, "KB GAPS ");
    o = puti(o, g.gaps);
    row(str, 32, o);
    o = put(str, "2 BAND * IF 3 SENS 0 LIGHT");
    row(str, 40, o);
    o = put(str, "ARROWS OFS 4 DEMOD 1 SPK 5 CLR");
    row(str, 49, o);
    g.A->blit_page(0);
    g.A->blit_page(8);
    g.lastDraw = g.A->ticks_ms();
}

/**
 * Park again after a band, IF or offset change and tell the PC.
 *
 * @param dec Slicer to reset.
 */
static void retune(dectpms_t *dec)
{
    applyRf();
    decInit(dec);
    markSettings();
}

/**
 * Clear the counters on the screen and the Toyota counter.
 *
 * @param dec Slicer to reset.
 */
static void clearCounts(dectpms_t *dec)
{
    g.hits = 0;
    g.gaps = 0;
    g.sent = 0;
    decInit(dec);
}

/**
 * Apply a command from the PC, as the matching key would.
 *
 * @param c Command: field in the high nibble, value in the low one.
 * @param dec Slicer, reset when the radio retunes.
 */
static void applyCmd(uint8_t c, dectpms_t *dec)
{
    uint8_t v = (uint8_t)(c & 15u);
    switch (c >> 4) {
        case 1u:
            g.band433 = (uint8_t)(v & 1u);
            break;
        case 2u:
            g.ifp = (uint8_t)(v & 3u);
            break;
        case 3u:
            g.foff = v;
            break;
        case 4u:
            if (v < SENS_LEVELS) g.sens = v;
            decInit(dec);
            markSettings();
            return;
        case 5u:
            g.light = (uint8_t)(v & 1u);
            markSettings();
            return;
        case 6u:
            clearCounts(dec);
            return;
        case 8u:
            g.ook = (uint8_t)(v & 1u);
            break;
        case 7u:
            markSettings();
            return;
        default:
            return;
    }
    retune(dec);
}

/**
 * Handle one new keypress.
 *
 * @param key Key code from the firmware.
 * @param dec Slicer, reset when the radio retunes.
 * @return True when the app should exit.
 */
static bool handleKey(uint8_t key, dectpms_t *dec)
{
    int8_t d;

    g.lastKey = g.A->ticks_ms();
    if (g.blOff) {
        /* As TPMS EU: EXIT on a dark screen only wakes it, unless the radio
           backlight is always on and the screen is never dark. */
        if (key == APP_KEY_EXIT && g.blTime == BL_ALWAYS) return true;
        g.blOff = 0;
        if (key == APP_KEY_EXIT) return false;
    }
    switch (key) {
        case APP_KEY_EXIT:
            return true;
        case APP_KEY_1:
            g.spk ^= 1u;
            g.A->audio_path(g.spk != 0u);
            return false;
        case APP_KEY_2:
            g.band433 ^= 1u;
            break;
        case APP_KEY_STAR:
            /* Up one preset, or down one (up three) after F. */
            g.ifp = (uint8_t)((g.ifp + 1u + 2u * g.fn) & 3u);
            break;
        case APP_KEY_4:
            /* FSK (Ford, Toyota) or OOK (Schrader, GM, BMW, Nissan). */
            g.ook ^= 1u;
            break;
        case APP_KEY_3:
            /* SENS LOW / MID / HIGH, as in TPMS EU. No retune needed. */
            /* Up one level, or down one (up two) after F. */
            g.sens = (uint8_t)((g.sens + 1u + g.fn) % SENS_LEVELS);
            decInit(dec);
            markSettings();
            return false;
        case APP_KEY_0:
            /* Force backlight on or off. Off times out from this press. */
            g.light ^= 1u;
            markSettings();
            return false;
        case APP_KEY_5:
            clearCounts(dec);
            return false;
        default:
            d = g.A->nav_dir(key);
            if (!d) return false;
            g.foff = (uint8_t)((g.foff + d) & 15u);
            break;
    }
    retune(dec);
    return false;
}

/**
 * Sample, slice and stream until EXIT.
 */
__attribute__((noinline))
static void capture(void)
{
    dectpms_t dec;
    tpms_pkt_t pkt;
    uint32_t (*const clk)(void) = g.A->cycles_48mhz;
    uint16_t (*const sample)(void) = g.A->af_capture_sample;
    uint32_t next;
    bool wantUi = true;

    decInit(&dec);
    markSettings();
    next = clk();
    for (;;) {
        /* Spare time before each sample feeds the UART. */
        while ((int32_t)(clk() - next) < 0) {
            if (g.uart) {
                txPoll();
                rxPoll();
            }
        }
        next += CYC_PER_SAMPLE;
        if (dectpms_push(&dec, sample(), &pkt)) {
            if (g.hits < 0xffffu) g.hits++;
            queue(MARK_HIT);
            queue(pkt.model);
            g.led = 2u;
            g.ledCnt = 0;
            g.A->led(true);
            dec.note = 1u;
            dectpms_rearm(&dec);
        }
        if (g.uart) {
            g.acc = (uint8_t)((g.acc << 1) | dec.level);
            if (++g.accN == 7u) {
                g.accN = 0;
                if (g.lost) markGap();
                else queue(g.acc & 0x7fu);
            }
            if (++g.setCnt >= SET_EVERY) markSettings();
        }
        if (dec.note) {
            dec.note = 0;
            g.drawDue = 1u;
            g.lastNote = g.A->ticks_ms();
        }
        if (g.led && ++g.ledCnt >= LED_STEP) {
            g.ledCnt = 0;
            g.led--;
            g.A->led(g.led & 1u);
        }
        if (++g.keyCnt < KEY_EVERY) continue;
        g.keyCnt = 0;

        uint32_t now = g.A->ticks_ms();
        if (g.drawDue && (uint32_t)(now - g.lastNote) >= DRAW_HOLD_MS) wantUi = true;
        if ((uint32_t)(now - g.lastDraw) >= DRAW_EVERY_MS) wantUi = true;
        uint8_t key = g.A->get_key();
        bool fresh = key != g.prevKey && key != APP_KEY_INVALID;
        g.prevKey = key;
        if (fresh) {
            if (handleKey(key, &dec)) break;
            /* F is a one-shot shift, like the radio's own: the next * or 3
               key steps back. A second F cancels it, any other key uses
               it up. handleKey ignores F itself. */
            g.fn = (uint8_t)((key == APP_KEY_F) & (g.fn ^ 1u));
            wantUi = true;
        }
        if (g.cmd) {
            applyCmd(g.cmd, &dec);
            g.cmd = 0;
            wantUi = true;
        }
        /* Lamp: held while forced or within DISP_IDLE_MS of a key, then
           released to the radio's own countdown. */
        /* Signed: a key handled just now sets lastKey after now was read. */
        if (!g.light && !g.blOff && (int32_t)(now - g.lastKey) >= (int32_t)DISP_IDLE_MS) {
            g.blOff = 1u;
        }
        g.A->backlight_update();
        if (g.light || !g.blOff) g.A->backlight_on();
        if (wantUi) {
            g.drawDue = 0;
            wantUi = false;
            g.A->battery_sample();
            draw(&dec);
            /* Sampling stopped while the screen was drawn. */
            if (g.uart) markGap();
            next = clk() + CYC_PER_SAMPLE;
        }
    }
}

/**
 * Overlay entry. Stream, then restore the radio and the UART.
 *
 * @param api Firmware service table.
 */
__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    uint8_t cfg[4] = {0};
    uint16_t r43, r2b, r73;

    memset(&g, 0, sizeof g);
    g.A = api;
    g.prevKey = APP_KEY_INVALID;
    g.band433 = 1u;
    g.sens = SENS_DEFAULT;
    /* Unknown (no storage read on this firmware) keeps the timed lamp. */
    g.blTime = 1u;
    if (api->sys_storage_read) api->sys_storage_read(BL_TIME_ADDR, &g.blTime, 1);
    api->cfg_load(cfg, 4);
    if (cfg[0] == CFG_MAGIC) {
        g.band433 = cfg[1] & 1u;
        g.light = (cfg[1] & CFG_LIGHT) ? 1u : 0u;
        g.ook = (cfg[1] & CFG_OOK) ? 1u : 0u;
        g.ifp = cfg[2] & 3u;
        g.sens = (uint8_t)((cfg[2] >> 2) & 3u);
        g.foff = cfg[3] & 15u;
        if (g.sens >= SENS_LEVELS) g.sens = SENS_DEFAULT;
    }
    /* A radio backlight set to always on starts with the force option on. */
    if (g.blTime == BL_ALWAYS) g.light = 1u;
    g.lastKey = api->ticks_ms();
    api->led(false);
    api->backlight_on();
    api->af_capture_open();
    r43 = api->bk_read(REG_43);
    r2b = api->bk_read(REG_2B);
    r73 = api->bk_read(REG_73);
    applyRf();
    api->delay_ms(50);
    g.uart = uartOpen() ? 1u : 0u;

    capture();

    if (g.uart) uartClose();
    api->bk_write(REG_43, r43);
    api->bk_write(REG_2B, r2b);
    api->bk_write(REG_73, r73);
    api->set_agc(true);
    api->af_capture_close();
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
    api->led(false);

    cfg[0] = CFG_MAGIC;
    cfg[1] = (uint8_t)(g.band433 | (g.light ? CFG_LIGHT : 0u) | (g.ook ? CFG_OOK : 0u));
    cfg[2] = (uint8_t)(g.ifp | (g.sens << 2));
    cfg[3] = g.foff;
    api->cfg_save(cfg, 4);
}
