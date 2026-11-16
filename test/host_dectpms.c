/* Host checks for Ford / Toyota / Renault / Schrader TPMS field parsers. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../dectpms.h"
#include "../dectpms.c"

static int fails;

/**
 * Record a failed check.
 *
 * @param cond Nonzero when the check passed.
 * @param msg Text printed when the check failed.
 */
static void expect(int cond, const char *msg)
{
    if (!cond) {
        printf("FAIL: %s\n", msg);
        fails++;
    }
}

/**
 * CRC-8 used to build fixture bytes. MSB first, not reflected.
 *
 * @param d Input bytes.
 * @param n Number of bytes.
 * @param poly Generator polynomial.
 * @param init Initial register value.
 * @return CRC of the buffer.
 */
static uint8_t crc8_ref(const uint8_t *d, unsigned n, uint8_t poly, uint8_t init)
{
    uint8_t c = init;
    while (n--) {
        c ^= *d++;
        for (uint8_t i = 0; i < 8u; i++)
            c = (uint8_t)((c & 0x80u) ? ((c << 1) ^ poly) : (c << 1));
    }
    return c;
}

/**
 * Check a valid Ford frame and a broken checksum.
 */
static void test_ford(void)
{
    uint8_t b[8] = {
        0xA1, 0xB2, 0xC3, 0xD4, /* id */
        144,                     /* pressure * 4 = 36 PSI */
        56 + 18,                 /* 18 C, temp valid */
        0x04,                    /* at rest */
        0
    };
    b[7] = (uint8_t)(b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6]);
    tpms_pkt_t o;
    expect(tpms_parse_ford(b, &o), "ford valid");
    expect(o.model == TPMS_FORD, "ford model");
    expect(o.id == 0xA1B2C3D4u, "ford id");
    expect(o.psi == 36, "ford psi");
    expect(o.temp_c == 18, "ford temp");

    b[7] ^= 0xffu;
    expect(!tpms_parse_ford(b, &o), "ford bad checksum");
}

/**
 * Check a valid Toyota frame and a broken CRC.
 */
static void test_toyota(void)
{
    /* Build a 9-byte Toyota frame.
     * pressure raw = 140 -> PSI = 140*0.25-7 = 28
     * temp raw = 41 -> 1 C
     * Layout from rtl_433:
     *   b4: status bit + pressure high 7
     *   b5: pressure low 1 + temp high 7
     *   b6: temp low 1 + status low 7
     *   b7: ~pressure
     */
    uint8_t b[9] = {0x00, 0xF2, 0x11, 0xA9, 0, 0, 0, 0, 0};
    unsigned p = 140;
    unsigned t = 41;
    b[4] = (uint8_t)((p >> 1) & 0x7fu);          /* status bit 0 */
    b[5] = (uint8_t)(((p & 1u) << 7) | ((t >> 1) & 0x7fu));
    b[6] = (uint8_t)(((t & 1u) << 7));           /* status low 0 */
    b[7] = (uint8_t)(p ^ 0xffu);
    b[8] = crc8_ref(b, 8, 0x07u, 0x80u);

    tpms_pkt_t o;
    expect(tpms_parse_toyota(b, &o), "toyota valid");
    expect(o.model == TPMS_TOYT, "toyota model");
    expect(o.id == 0x00F211A9u, "toyota id");
    expect(o.psi == 28, "toyota psi");
    expect(o.temp_c == 1, "toyota temp");

    b[8] ^= 1u;
    expect(!tpms_parse_toyota(b, &o), "toyota bad crc");
}

/**
 * Check a valid Schrader frame and a broken CRC.
 */
static void test_schrader(void)
{
    /* Example from rtl_433 comment: 7 f6 70 3a 38 b2 00 49 49
     * After sync nibble strip: f6 70 3a 38 b2 00 49 49
     * Wait: "7 f6 70 3a 38 b2 00 49 49" is sync nibble 7 then 8 bytes.
     * So b = f6 70 3a 38 b2 00 49 49
     * id = (f6&0x0F)<<24 | 70<<16 | 3a<<8 | 38 = 0x0703A38
     * Actually: serial_id = (b[1]&0x0F)<<24 | b[2]<<16 | b[3]<<8 | b[4]
     * with b[0]=flags/preamble byte = 0xf6, so id from b[1]..
     * From comment: S PF FI II II II PP TT CC
     * b[0]=0xf6 (P=f, F=6?), actually after extract:
     * pressure = b[5]*25 mbar, temp = b[6]-50
     *
     * Build our own with a matching CRC. */
    uint8_t b[8] = {
        0xF6,       /* preamble/flags */
        0x70, 0x3A, 0x38, 0xB2, /* id nibbles: (0x70&0xF)<<24 | 0x3A<<16 | 0x38<<8 | 0xB2 */
        132,        /* *2.5 = 330 kPa ≈ 48 PSI */
        62,         /* 12 C */
        0
    };
    b[7] = crc8_ref(b, 7, 0x07u, 0xf0u);

    tpms_pkt_t o;
    expect(tpms_parse_schrader(b, &o), "schrader valid");
    expect(o.model == TPMS_SCHR, "schrader model");
    expect(o.id == 0x003A38B2u, "schrader id");
    /* 132 * 2.5 = 330 kPa; psi ≈ 330 * 145 / 1000 = 47.85 -> 48 */
    expect(o.psi == 48, "schrader psi");
    expect(o.temp_c == 12, "schrader temp");

    b[7] ^= 1u;
    expect(!tpms_parse_schrader(b, &o), "schrader bad crc");
}

/**
 * Unpack the first nbits of a byte buffer, MSB first, one bit per byte.
 *
 * @param out Destination, one bit per byte, starting at out[at].
 * @param at First index to write.
 * @param b Source bytes.
 * @param nbits Number of bits.
 * @return Index after the last bit written.
 */
static unsigned unpack(uint8_t *out, unsigned at, const uint8_t *b, unsigned nbits)
{
    for (unsigned i = 0; i < nbits; i++)
        out[at++] = (uint8_t)((b[i >> 3] >> (7u - (i & 7u))) & 1u);
    return at;
}

/**
 * Turn OOK data bits into the Manchester chips the slicer collects.
 *
 * Bit 0 of a burst is 0, and its first chip is the idle before the
 * carrier, so only its "on" chip is kept. A 1 is on then off, a 0 is off
 * then on, as rtl_433's zero-bit slicer reads them.
 *
 * @param chips Destination, one chip per byte.
 * @param bits Data bits, one per byte.
 * @param n Number of bits.
 * @return Number of chips.
 */
static unsigned to_chips(uint8_t *chips, const uint8_t *bits, unsigned n)
{
    unsigned c = 0;
    chips[c++] = 1;
    for (unsigned i = 1; i < n; i++) {
        chips[c++] = bits[i];
        chips[c++] = (uint8_t)!bits[i];
    }
    return c;
}

/**
 * Build the data bits of a Schrader or BMW burst.
 *
 * @param bits Destination, one bit per byte.
 * @param bmw Nonzero for BMW (61 bits), zero for Schrader (68 bits).
 * @param crc_ok Zero breaks the Schrader CRC.
 * @return Number of bits.
 */
static unsigned ook_bits(uint8_t *bits, int bmw, int crc_ok)
{
    static const uint8_t bmw_sync[2] = {0x7F, 0xFF};
    static const uint8_t bmw_body[6] = {0x02, 0x01, 0x8A, 0xCC, 0x86, 0x30};
    static const uint8_t schr_sync = 0x70; /* sync nibble 7 */
    uint8_t schr[8] = {0xF6, 0x70, 0x3A, 0x38, 0xB2, 132, 62, 0};
    unsigned n;
    if (bmw) {
        n = unpack(bits, 0, bmw_sync, 16);
        return unpack(bits, n, bmw_body, 45);
    }
    schr[7] = crc8_ref(schr, 7, 0x07u, 0xf0u);
    if (!crc_ok) schr[7] ^= 1u;
    n = unpack(bits, 0, &schr_sync, 4);
    return unpack(bits, n, schr, 64);
}

/**
 * Load chips into a fresh OOK slicer, then the two off chips that end it.
 *
 * @param d Slicer to fill.
 * @param chips One chip per byte.
 * @param n Number of chips.
 */
static void load_chips(dectpms_t *d, const uint8_t *chips, unsigned n)
{
    dectpms_init(d, TPMS_MODE_OOK);
    for (unsigned i = 0; i < n; i++) put_bit(d, chips[i]);
    put_bit(d, 0);
    put_bit(d, 0);
}

/**
 * Run whole OOK bursts through the parser chain the EU2 / NA2 apps use.
 *
 * The chips are Manchester-decoded first, as rtl_433 does. Schrader shares
 * the chain with EG53, BMW and Nissan, so a BMW burst must come out as BMW
 * and a Schrader burst as Schrader.
 */
static void test_ook_chain(void)
{
    uint8_t bits[128], chips[260];
    unsigned n;
    dectpms_t d;
    tpms_pkt_t o;

    n = ook_bits(bits, 1, 1);
    load_chips(&d, chips, to_chips(chips, bits, n));
    memset(&o, 0, sizeof o);
    expect(finish_ook(&d, &o), "ook chain takes a bmw burst");
    expect(o.model == TPMS_BMW, "ook chain bmw burst is bmw, not schrader");

    n = ook_bits(bits, 0, 1);
    load_chips(&d, chips, to_chips(chips, bits, n));
    memset(&o, 0, sizeof o);
    expect(finish_ook(&d, &o), "ook chain takes a schrader burst");
    expect(o.model == TPMS_SCHR && o.id == 0x003A38B2u, "ook chain schrader burst is schrader");

    /* Data bits handed over as if they were chips must not decode. */
    load_chips(&d, bits, n);
    expect(!finish_ook(&d, &o), "ook chain needs manchester chips, not bits");

    n = ook_bits(bits, 0, 0);
    load_chips(&d, chips, to_chips(chips, bits, n));
    expect(!finish_ook(&d, &o), "ook chain drops a bad schrader burst");
}

/**
 * Build the PCM chips of a Nissan / Subaru burst.
 *
 * The 36-chip preamble F5555555E is trimmed or lengthened, then each data
 * bit v after the first goes out as v then !v (the parser inverts).
 *
 * @param chips Destination, one chip per byte.
 * @param b Five payload bytes; the top bit of b[0] must be 1.
 * @param skip Preamble chips dropped from the front.
 * @param extra Alternating chips added before the preamble.
 * @return Number of chips.
 */
static unsigned nis_chips(uint8_t *chips, const uint8_t *b, unsigned skip, unsigned extra)
{
    static const uint8_t pre[5] = {0xF5, 0x55, 0x55, 0x55, 0xE0};
    uint8_t tmp[36];
    unsigned c = 0;
    for (unsigned i = 0; i < extra; i++) chips[c++] = (uint8_t)((extra - i) & 1u);
    unpack(tmp, 0, pre, 36);
    for (unsigned i = skip; i < 36u; i++) chips[c++] = tmp[i];
    for (unsigned i = 1; i < 38u; i++) {
        uint8_t v = (uint8_t)((b[i >> 3] >> (7u - (i & 7u))) & 1u);
        chips[c++] = v;
        chips[c++] = (uint8_t)!v;
    }
    return c;
}

/**
 * Run Nissan bursts through the OOK chain, with the preamble bounds rtl_433 uses.
 */
static void test_nis_chain(void)
{
    uint8_t b[5] = {0x81, 0x23, 0x45, 0x68, 0};
    uint8_t chips[200];
    dectpms_t d;
    tpms_pkt_t o;
    for (int xx = 0; xx < 4; xx++) {
        int sum = 0;
        b[4] = (uint8_t)(xx << 2);
        for (int i = 0; i < 5; i++)
            sum += (b[i] & 3) + ((b[i] >> 2) & 3) + ((b[i] >> 4) & 3) + ((b[i] >> 6) & 3);
        if ((sum & 3) == 1) break;
    }

    load_chips(&d, chips, nis_chips(chips, b, 0, 0));
    memset(&o, 0, sizeof o);
    expect(finish_ook(&d, &o), "ook chain takes a nissan burst");
    expect(o.model == TPMS_NISS && o.id == 0x00123456u && o.psi == 32, "nissan burst fields");

    load_chips(&d, chips, nis_chips(chips, b, 16, 0));
    expect(finish_ook(&d, &o), "nissan with an 18-chip preamble");
    load_chips(&d, chips, nis_chips(chips, b, 20, 0));
    expect(!finish_ook(&d, &o), "nissan with a 14-chip preamble is too short");
    load_chips(&d, chips, nis_chips(chips, b, 0, 8));
    expect(finish_ook(&d, &o), "nissan row of 118 chips");
    load_chips(&d, chips, nis_chips(chips, b, 0, 10));
    expect(!finish_ook(&d, &o), "nissan row of 120 chips is too long");
}

static unsigned rng_state;
/* Toyota test counter of the slicer after the last feed(). */
static uint8_t last_ty_best;
/* Samples stalled by the RSSI read in feed(); 0 for none. */
static unsigned stall_samples;

/**
 * Small deterministic noise source.
 *
 * @return Uniform noise, -1 to 1.
 */
static double noise1(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return ((double)((rng_state >> 8) & 0xffffu) / 32768.0) - 1.0;
}

/**
 * Render chips as AF samples at 38.4 kHz and feed them to the slicer.
 *
 * Edges go through a one-pole low-pass and a little noise is added.
 * FSK chips swing either side of mid-scale with discriminator noise around
 * the burst. OOK chips are carrier on or off, with silence around it.
 *
 * @param mode TPMS_MODE_FSK or TPMS_MODE_OOK.
 * @param chips One chip per byte.
 * @param n Number of chips.
 * @param us Chip length in microseconds.
 * @param phase Start offset in samples, 0 to 1.
 * @param invert Nonzero flips the demodulator polarity.
 * @param out Filled with the last packet.
 * @return Number of packets decoded.
 */
static int feed(uint8_t mode, const uint8_t *chips, unsigned n, double us,
                double phase, int invert, tpms_pkt_t *out)
{
    dectpms_t d;
    double spc = 38400.0 * us / 1e6, y = 0;
    unsigned lead = 300u, total = lead + (unsigned)(n * spc) + 600u;
    unsigned stall_end = 0;
    int hits = 0;
    static uint16_t xs[4096];
    if (total > 4096u) total = 4096u;
    dectpms_init(&d, mode);
    for (unsigned s = 0; s < total; s++) {
        double lvl, idle = (mode == TPMS_MODE_FSK) ? (noise1() > 0 ? 1.0 : -1.0) : 0.0;
        unsigned ci = (s < lead) ? n : (unsigned)((s - lead + phase) / spc);
        if (ci < n) lvl = chips[ci] ? 1.0 : (mode == TPMS_MODE_FSK ? -1.0 : 0.0);
        else lvl = idle;
        y += 0.7 * (lvl - y);
        double v = invert ? -y : y;
        xs[s] = (uint16_t)(2048.0 + 600.0 * v + 40.0 * noise1());
    }
    for (unsigned s = 0; s < total; s++) {
        uint16_t x = xs[s];
        /* The OOK app reads RSSI at chip RSSI_CHIP, which stalls sampling:
           the samples due during the stall are all read when it ends. */
        if (stall_samples && !stall_end && mode == TPMS_MODE_OOK && d.bitn >= 16u) {
            stall_end = s + stall_samples + 1u;
            if (stall_end >= total) stall_end = total - 1u;
        } else if (stall_end && s < stall_end) {
            x = xs[stall_end];
        }
        if (dectpms_push(&d, x, out)) {
            hits++;
            dectpms_rearm(&d);
            stall_end = 0;
        }
    }
    last_ty_best = d.ty_best;
    return hits;
}

/**
 * Count the start phases at which a burst decodes to the expected sensor.
 *
 * @param mode TPMS_MODE_FSK or TPMS_MODE_OOK.
 * @param chips One chip per byte.
 * @param n Number of chips.
 * @param us Chip length in microseconds.
 * @param invert Nonzero flips the demodulator polarity.
 * @param model Expected model.
 * @param id Expected id.
 * @return Number of the five phases that decoded correctly.
 */
static int phases_ok(uint8_t mode, const uint8_t *chips, unsigned n, double us,
                     int invert, uint8_t model, uint32_t id)
{
    int ok = 0;
    for (int ph = 0; ph < 5; ph++) {
        tpms_pkt_t o;
        rng_state = 11u + (unsigned)ph;
        memset(&o, 0, sizeof o);
        if (feed(mode, chips, n, us, ph * 0.2, invert, &o) && o.model == model && o.id == id)
            ok++;
    }
    return ok;
}

/**
 * Feed Ford and Toyota FSK bursts as samples, in both polarities.
 *
 * rtl_433 sees Ford as 55 55 55 56 and decodes the inverted payload.
 * Toyota is not inverted; its lead-in 0101 0101 0011 11 holds the A9E sync.
 * Either discriminator polarity must decode.
 */
static void test_fsk_samples(void)
{
    static const uint8_t ford_pre[4] = {0x55, 0x55, 0x55, 0x56};
    static const uint8_t toyt_pre[2] = {0x55, 0x3C};
    uint8_t ford[8] = {0xA1, 0xB2, 0xC3, 0xD4, 144, 56 + 18, 0x04, 0};
    uint8_t toyt[9] = {0x00, 0xF2, 0x11, 0xA9, 0, 0, 0, 0, 0};
    uint8_t fc[200], tc[200];
    unsigned nf, nt, p = 140, t = 41;
    uint8_t last;
    char msg[64];

    ford[7] = (uint8_t)(ford[0] + ford[1] + ford[2] + ford[3] + ford[4] + ford[5] + ford[6]);
    nf = unpack(fc, 0, ford_pre, 32);
    for (unsigned i = 0; i < 64u; i++) {
        uint8_t b = (uint8_t)((ford[i >> 3] >> (7u - (i & 7u))) & 1u);
        fc[nf++] = b;              /* air is the inverse of (!b, b) */
        fc[nf++] = (uint8_t)!b;
    }

    toyt[4] = (uint8_t)((p >> 1) & 0x7fu);
    toyt[5] = (uint8_t)(((p & 1u) << 7) | ((t >> 1) & 0x7fu));
    toyt[6] = (uint8_t)((t & 1u) << 7);
    toyt[7] = (uint8_t)(p ^ 0xffu);
    toyt[8] = crc8_ref(toyt, 8, 0x07u, 0x80u);
    nt = unpack(tc, 0, toyt_pre, 14);
    last = tc[nt - 1];
    for (unsigned i = 0; i < 72u; i++) {
        uint8_t b = (uint8_t)((toyt[i >> 3] >> (7u - (i & 7u))) & 1u);
        uint8_t c1 = (uint8_t)!last, c2 = b ? c1 : (uint8_t)!c1;
        tc[nt++] = c1;
        tc[nt++] = c2;
        last = c2;
    }
    for (unsigned i = 0; i < 3u; i++) {
        last = (uint8_t)!last;
        tc[nt++] = last;
    }

    for (int inv = 0; inv < 2; inv++) {
        int ok = phases_ok(TPMS_MODE_FSK, fc, nf, 52, inv, TPMS_FORD, 0xA1B2C3D4u);
        snprintf(msg, sizeof msg, "ford samples, polarity %d: %d/5", inv, ok);
        expect(ok >= 4, msg);
        ok = phases_ok(TPMS_MODE_FSK, tc, nt, 52, inv, TPMS_TOYT, 0x00F211A9u);
        snprintf(msg, sizeof msg, "toyota samples, polarity %d: %d/5", inv, ok);
        expect(ok >= 4, msg);
    }

    /* A Toyota frame with a bad CRC gives no packet, but the test counter
       still shows that all 72 bits decoded after the sync. Flipping the
       second chip of the last CRC bit, and every trailer chip after it,
       flips that bit and keeps the differential clock intact. */
    {
        tpms_pkt_t o;
        unsigned i = nt - 3u - 2u;
        tc[i + 1u] = (uint8_t)!tc[i + 1u];
        tc[i + 2u] = (uint8_t)!tc[i + 2u];
        tc[i + 3u] = (uint8_t)!tc[i + 3u];
        tc[i + 4u] = (uint8_t)!tc[i + 4u];
        rng_state = 11u;
        memset(&o, 0, sizeof o);
        expect(!feed(TPMS_MODE_FSK, tc, nt, 52, 0.0, 0, &o), "toyota bad crc: no packet");
        expect(last_ty_best == 72u, "toyota bad crc: test counter reaches 72");
    }
}

/**
 * Feed Schrader and BMW OOK bursts as samples, in both AM polarities.
 */
static void test_ook_samples(void)
{
    uint8_t bits[128], chips[260];
    char msg[64];

    for (int bmw = 0; bmw < 2; bmw++) {
        unsigned n = to_chips(chips, bits, ook_bits(bits, bmw, 1));
        for (int inv = 0; inv < 2; inv++) {
            int ok = phases_ok(TPMS_MODE_OOK, chips, n, 120, inv,
                               bmw ? TPMS_BMW : TPMS_SCHR,
                               bmw ? 0x00100C56u : 0x003A38B2u);
            snprintf(msg, sizeof msg, "%s samples, polarity %d: %d/5",
                     bmw ? "bmw" : "schrader", inv, ok);
            expect(ok >= 4, msg);
        }
    }

    /* The OOK app reads RSSI 16 chips in, inside a Schrader frame. The
       slicer must ride out a read that stalls sampling for up to 9 samples
       (~230 us); 8 is checked to keep a margin. */
    {
        unsigned n = to_chips(chips, bits, ook_bits(bits, 0, 1));
        for (int inv = 0; inv < 2; inv++) {
            int ok;
            stall_samples = 8u;
            ok = phases_ok(TPMS_MODE_OOK, chips, n, 120, inv, TPMS_SCHR, 0x003A38B2u);
            stall_samples = 0;
            snprintf(msg, sizeof msg, "schrader, 8-sample rssi stall, polarity %d: %d/5", inv, ok);
            expect(ok >= 4, msg);
        }
    }
}

/**
 * Check a valid EG53 frame and a broken checksum.
 */
static void test_eg53(void)
{
    uint8_t b[10] = {0x01, 0x02, 0x03, 0x04, 0xAB, 0xCD, 0xEF, 132, 68, 0};
    unsigned sum = 0;
    tpms_pkt_t o;
    for (int i = 0; i < 9; i++) sum += b[i];
    b[9] = (uint8_t)sum;
    expect(tpms_parse_eg53(b, &o), "eg53 valid");
    expect(o.model == TPMS_EG53, "eg53 model");
    expect(o.id == 0x00ABCDEFu, "eg53 id");
    expect(o.psi == 48, "eg53 psi");
    expect(o.temp_c == 20, "eg53 temp");
    b[9] ^= 1u;
    expect(!tpms_parse_eg53(b, &o), "eg53 bad checksum");
}

/**
 * Check a valid BMW frame and a broken check.
 */
static void test_bmw(void)
{
    /* 200 kPa, temp byte 70 -> 20 C. The two check bits have to be a fixed point. */
    uint8_t b[6] = {0x02, 0x01, 0x8A, 0xCC, 0x86, 0x30};
    tpms_pkt_t o;
    expect(tpms_parse_bmw(b, &o), "bmw valid");
    expect(o.model == TPMS_BMW, "bmw model");
    expect(o.id == 0x00100C56u, "bmw id");
    expect(o.psi == 29, "bmw psi");
    expect(o.temp_c == 20, "bmw temp");
    b[1] ^= 0xffu;
    expect(!tpms_parse_bmw(b, &o), "bmw bad check");
}

/**
 * Check a valid Nissan frame and a broken check.
 */
static void test_nis(void)
{
    uint8_t b[5] = {0x71, 0x23, 0x45, 0x68, 0};
    tpms_pkt_t o;
    int xx;
    for (xx = 0; xx < 4; xx++) {
        int sum = 0;
        b[4] = (uint8_t)(xx << 2);
        for (int i = 0; i < 5; i++)
            sum += (b[i] & 3) + ((b[i] >> 2) & 3) + ((b[i] >> 4) & 3) + ((b[i] >> 6) & 3);
        if ((sum & 3) == 1) break;
    }
    expect(xx < 4, "nis checksum found");
    expect(tpms_parse_nis(b, &o), "nis valid");
    expect(o.model == TPMS_NISS, "nis model");
    expect(o.id == 0x00123456u, "nis id");
    expect(o.psi == 32, "nis psi");
    expect(o.temp_c == (int16_t)TPMS_NO_TEMP, "nis no temp");
    b[0] ^= 0xffu;
    expect(!tpms_parse_nis(b, &o), "nis bad check");
}

/**
 * Check Ford pressure and temperature conversions.
 */
static void test_conversions(void)
{
    /* Ford high-pressure bit 0x20 adds 0x100 to psibits. */
    uint8_t b[8] = {1, 2, 3, 4, 0x10, 56, 0x24, 0}; /* moving + high bit */
    /* flags 0x24 = 0x20 | 0x04 — 0x04 alone is at rest, but 0x20|0x04 is ok?
     * Plan: switch on (f & 0x4c): 0x24 & 0x4c = 0x04 -> at rest. OK. */
    b[6] = 0x04;
    b[4] = 0; /* with 0x20 would be set on flags */
    /* Use moving 0x44 and pressure 100 -> 25 PSI */
    b[4] = 100;
    b[5] = 56 + 5;
    b[6] = 0x44;
    b[7] = (uint8_t)(b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6]);
    tpms_pkt_t o;
    expect(tpms_parse_ford(b, &o), "ford moving");
    expect(o.psi == 25, "ford 25 psi");
    expect(o.temp_c == 5, "ford 5 C");
}

/**
 * Feed a real Ford burst as samples, in both polarities.
 *
 * rtl_433 printed id c4ff1d57, 37.5 PSI, 71 C. The status byte is 0x48,
 * and the sensor sends a ninth byte, f0, that is also a valid Renault CRC
 * of the first eight. It must decode as Ford, never as Renault.
 */
static void test_ford_capture(void)
{
    static const uint8_t pre[4] = {0x55, 0x55, 0x55, 0x56};
    static const uint8_t f[9] = {0xC4, 0xFF, 0x1D, 0x57, 0x96, 0x7F, 0x48, 0x94, 0xF0};
    uint8_t c[200];
    unsigned n;
    char msg[64];
    tpms_pkt_t o;

    memset(&o, 0, sizeof o);
    expect(tpms_parse_ford(f, &o), "ford capture: status 0x48 accepted");
    expect(o.id == 0xC4FF1D57u && o.psi == 38 && o.temp_c == 71, "ford capture: fields");
    expect(crc8_ref(f, 8, 0x07u, 0x00u) == f[8], "ford capture: trailer is a Renault CRC");

    n = unpack(c, 0, pre, 32);
    for (unsigned i = 0; i < 72u; i++) {
        uint8_t bit = (uint8_t)((f[i >> 3] >> (7u - (i & 7u))) & 1u);
        c[n++] = bit;
        c[n++] = (uint8_t)!bit;
    }
    for (int inv = 0; inv < 2; inv++) {
        int ok = phases_ok(TPMS_MODE_FSK, c, n, 52, inv, TPMS_FORD, 0xC4FF1D57u);
        snprintf(msg, sizeof msg, "ford capture samples, polarity %d: %d/5", inv, ok);
        expect(ok >= 4, msg);
    }
}

/**
 * Build the Renault frame that rtl_433 printed for a real sensor.
 *
 * id 6fb001, flags 31, 174 kPa (raw 232), 1 C.
 *
 * @param b Filled with nine bytes, CRC last.
 */
static void renault_frame(uint8_t b[9])
{
    static const uint8_t body[8] = {0xC4, 0xE8, 31, 0x01, 0xB0, 0x6F, 0xFF, 0xFF};
    memcpy(b, body, 8);
    b[8] = crc8_ref(b, 8, 0x07u, 0x00u);
}

/**
 * Check a valid Renault frame, a broken CRC, and the pressure scale.
 */
static void test_renault(void)
{
    uint8_t b[9];
    tpms_pkt_t o;
    renault_frame(b);
    memset(&o, 0, sizeof o);
    expect(tpms_parse_renault(b, &o), "renault valid");
    expect(o.model == TPMS_RENA, "renault model");
    expect(o.id == 0x6FB001u, "renault id (little-endian)");
    expect(o.psi == 25, "renault 174 kPa is 25 psi");
    expect(o.temp_c == 1, "renault temp");
    b[8] ^= 1u;
    expect(!tpms_parse_renault(b, &o), "renault bad crc");
    /* 1023 raw is 767.25 kPa, 111.3 PSI. */
    b[0] = 0x03u;
    b[1] = 0xFFu;
    b[8] = crc8_ref(b, 8, 0x07u, 0x00u);
    expect(tpms_parse_renault(b, &o) && o.psi == 111, "renault full-scale psi");
}

/**
 * Feed a Renault FSK burst as samples, in both polarities.
 *
 * Renault has the Ford sync (55 55 55 56 on air) and nine Manchester bytes,
 * so the slicer must not give up when the first eight fail the Ford check.
 */
static void test_renault_samples(void)
{
    static const uint8_t pre[4] = {0x55, 0x55, 0x55, 0x56};
    uint8_t b[9], c[200];
    unsigned n;
    char msg[64];

    renault_frame(b);
    n = unpack(c, 0, pre, 32);
    for (unsigned i = 0; i < 72u; i++) {
        uint8_t bit = (uint8_t)((b[i >> 3] >> (7u - (i & 7u))) & 1u);
        c[n++] = bit;
        c[n++] = (uint8_t)!bit;
    }
    for (int inv = 0; inv < 2; inv++) {
        int ok = phases_ok(TPMS_MODE_FSK, c, n, 52, inv, TPMS_RENA, 0x6FB001u);
        snprintf(msg, sizeof msg, "renault samples, polarity %d: %d/5", inv, ok);
        expect(ok >= 4, msg);
    }
}

/**
 * Run every parser check.
 *
 * @return Zero when every check passed.
 */
int main(void)
{
    fails = 0;
    test_ook_chain();
    test_nis_chain();
    test_fsk_samples();
    test_ook_samples();
    test_ford();
    test_ford_capture();
    test_toyota();
    test_renault();
    test_renault_samples();
    test_schrader();
    test_eg53();
    test_bmw();
    test_nis();
    test_conversions();
    if (fails) {
        printf("%d test(s) failed\n", fails);
        return 1;
    }
    printf("tpmsrx host tests passed\n");
    return 0;
}
