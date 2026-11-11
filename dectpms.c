/* Ford / Toyota FSK and Schrader MRXGG4 OOK parsers + 38.4 kHz AF slicer.
 * Field layouts match rtl_433 (tpms_ford, tpms_toyota, schraeder). */
#include "dectpms.h"

#define BIAS     2048
#define HYST     40
#define STEP_FSK 32768u   /* 65536 / 2 samples per 52 us chip */
#define STEP_OOK 14245u   /* ~4.608 samples per 120 us half-bit */
#define GAP_FSK  8u
#define GAP_OOK  24u

enum { ST_HUNT = 0, ST_FORD = 1, ST_TOYT = 2, ST_DONE = 4 };

static uint8_t bit_at(const uint8_t *bits, unsigned i)
{
    return (uint8_t)((bits[i >> 3] >> (7u - (i & 7u))) & 1u);
}

static void bit_set(uint8_t *bits, unsigned i, uint8_t v)
{
    if (v) bits[i >> 3] = (uint8_t)(bits[i >> 3] | (uint8_t)(0x80u >> (i & 7u)));
    else bits[i >> 3] = (uint8_t)(bits[i >> 3] & (uint8_t)~(0x80u >> (i & 7u)));
}

#if TPMS_HAVE_FSK || TPMS_HAVE_SCHR
static uint8_t crc8(const uint8_t *d, unsigned n, uint8_t poly, uint8_t init)
{
    uint8_t c = init;
    while (n--) {
        c ^= *d++;
        for (uint8_t i = 0; i < 8u; i++)
            c = (uint8_t)((c & 0x80u) ? ((c << 1) ^ poly) : (c << 1));
    }
    return c;
}
#endif

#if TPMS_HAVE_FSK
static uint32_t be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}
#endif

static int16_t psi_from_kpa10(unsigned kpa10)
{
    return (int16_t)((kpa10 * 145u + 5000u) / 10000u);
}

#if TPMS_HAVE_FSK
bool tpms_parse_ford(const uint8_t b[8], tpms_pkt_t *out)
{
    uint8_t sum = (uint8_t)(b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6]);
    if (sum != b[7]) return false;
    uint8_t f = b[6];
    if ((f & 0x90u) != 0) return false;
    switch (f & 0x4cu) {
        case 0x08u: case 0x04u: case 0x44u: break;
        default: return false;
    }
    int psibits = ((f & 0x20u) << 3) | b[4];
    out->model = TPMS_FORD;
    out->id = be32(b);
    out->psi = (int16_t)((psibits + 2) / 4);
    if (b[5] & 0x80u) out->temp_c = (int16_t)TPMS_NO_TEMP;
    else out->temp_c = (int16_t)((b[5] & 0x7fu) - 56);
    return true;
}

bool tpms_parse_toyota(const uint8_t b[9], tpms_pkt_t *out)
{
    if (crc8(b, 8, 0x07u, 0x80u) != b[8]) return false;
    unsigned p1 = (unsigned)((b[4] & 0x7fu) << 1) | (b[5] >> 7);
    unsigned p2 = b[7] ^ 0xffu;
    if (p1 != p2) return false;
    unsigned temp = (unsigned)((b[5] & 0x7fu) << 1) | (b[6] >> 7);
    out->model = TPMS_TOYT;
    out->id = be32(b);
    out->psi = (int16_t)(((int)p1 - 28) / 4);
    out->temp_c = (int16_t)((int)temp - 40);
    return true;
}
#endif /* TPMS_HAVE_FSK */

#if TPMS_HAVE_SCHR
bool tpms_parse_schrader(const uint8_t b[8], tpms_pkt_t *out)
{
    if (crc8(b, 7, 0x07u, 0xf0u) != b[7]) return false;
    out->model = TPMS_SCHR;
    out->id = ((uint32_t)(b[1] & 0x0fu) << 24) | ((uint32_t)b[2] << 16) |
              ((uint32_t)b[3] << 8) | b[4];
    out->psi = psi_from_kpa10((unsigned)b[5] * 25u);
    out->temp_c = (int16_t)((int)b[6] - 50);
    return true;
}
#endif /* TPMS_HAVE_SCHR */

#if TPMS_HAVE_EG53
bool tpms_parse_eg53(const uint8_t b[10], tpms_pkt_t *out)
{
    unsigned sum = 0;
    int f;
    if (!b[1] && !b[2] && !b[4] && !b[5] && !b[7] && !b[8]) return false;
    for (unsigned i = 0; i < 9u; i++) sum += b[i];
    if ((sum & 0xffu) != b[9]) return false;
    out->model = TPMS_EG53;
    out->id = ((uint32_t)b[4] << 16) | ((uint32_t)b[5] << 8) | b[6];
    out->psi = psi_from_kpa10((unsigned)b[7] * 25u);
    f = (int)b[8];
    out->temp_c = (int16_t)(((f - 32) * 5) / 9);
    return true;
}
#endif

#if TPMS_HAVE_BMW
bool tpms_parse_bmw(const uint8_t b[6], tpms_pkt_t *out)
{
    uint32_t serial = ((uint32_t)(b[0] & 0x1fu) << 19) | ((uint32_t)b[1] << 11) |
                      ((uint32_t)b[2] << 3) | ((uint32_t)b[3] >> 5);
    int even_ones = 0, n = 0, c1c2, temp;
    unsigned pressure;
    if (serial == 0u || serial == 0x00FFFFFFu) return false;
    for (int i = 3; i < 38; i++) {
        int bit = (b[i / 8] >> (7 - (i % 8))) & 1;
        if (bit) {
            n++;
            if (((i - 3) & 1) == 0) even_ones++;
        }
    }
    c1c2 = (even_ones + 2 * n - 1) & 3;
    if (c1c2 != (((b[4] >> 3) & 1) << 1 | ((b[4] >> 2) & 1))) return false;
    pressure = ((unsigned)(b[3] & 0x1fu) << 4) | ((unsigned)b[4] >> 4);
    temp = ((b[4] & 0x03) << 5) | (b[5] >> 3);
    if (pressure > 450u || temp - 50 < -40 || temp - 50 > 85) return false;
    out->model = TPMS_BMW;
    out->id = serial;
    out->psi = (int16_t)((pressure * 145u + 500u) / 1000u);
    out->temp_c = (int16_t)(temp - 50);
    return true;
}
#endif

#if TPMS_HAVE_NIS
bool tpms_parse_nis(const uint8_t b[5], tpms_pkt_t *out)
{
    int sum = 0;
    unsigned pressure;
    if (!b[0] && !b[1] && !b[2] && !b[3]) return false;
    for (int i = 0; i < 5; i++)
        sum += (b[i] & 3) + ((b[i] >> 2) & 3) + ((b[i] >> 4) & 3) + ((b[i] >> 6) & 3);
    if ((sum & 3) != 1) return false;
    out->model = TPMS_NISS;
    out->id = ((uint32_t)(b[0] & 0x0fu) << 20) | ((uint32_t)b[1] << 12) |
              ((uint32_t)b[2] << 4) | ((uint32_t)b[3] >> 4);
    pressure = ((unsigned)(b[3] & 0x0fu) << 4) | ((unsigned)b[4] >> 4);
    out->psi = (int16_t)(pressure / 4u);
    out->temp_c = (int16_t)TPMS_NO_TEMP;
    return true;
}
#endif

/* rtl_433 bitbuffer_manchester_decode: pairs, emit bit2 when a != b. */
#if TPMS_HAVE_FSK || TPMS_HAVE_NIS
static unsigned man_decode(const uint8_t *raw, unsigned start, unsigned nraw,
                           uint8_t *out, unsigned max_bits)
{
    unsigned n = 0, i = start;
    while (i + 1u < nraw && n < max_bits) {
        uint8_t a = bit_at(raw, i++);
        uint8_t b = bit_at(raw, i++);
        if (a == b) break;
        bit_set(out, n++, b);
    }
    return n;
}
#endif

#if TPMS_HAVE_FSK
/* rtl_433 bitbuffer_differential_manchester_decode (simplified after sync). */
static unsigned dman_decode(const uint8_t *raw, unsigned nraw, uint8_t *out, unsigned max_bits)
{
    unsigned n = 0, ipos = 0;
    uint8_t bit1, bit2 = 0;

    while (ipos + 1u < nraw) {
        bit1 = bit_at(raw, ipos++);
        bit2 = bit_at(raw, ipos++);
        uint8_t bit3 = (ipos < nraw) ? bit_at(raw, ipos) : bit2;
        if (bit1 != bit2) {
            if (bit2 != bit3) {
                bit_set(out, n++, 0);
            } else {
                bit2 = bit1;
                ipos -= 1u;
                break;
            }
        } else {
            bit2 = (uint8_t)(1u - bit1);
            ipos -= 2u;
            break;
        }
        if (n >= max_bits) return n;
    }

    while (ipos + 1u < nraw && n < max_bits) {
        bit1 = bit_at(raw, ipos++);
        if (bit1 == bit2) break;
        bit2 = bit_at(raw, ipos++);
        bit_set(out, n++, (uint8_t)(bit1 == bit2));
    }
    return n;
}
#endif

static void raw_clear(dectpms_t *d)
{
    d->bitn = 0;
    for (unsigned i = 0; i < sizeof d->raw; i++) d->raw[i] = 0;
}

static void put_bit(dectpms_t *d, uint8_t bit)
{
    if (d->bitn >= (uint8_t)(sizeof d->raw * 8u)) return;
    bit_set(d->raw, d->bitn++, bit);
}

void dectpms_init(dectpms_t *d, uint8_t mode)
{
    d->mean = (int32_t)BIAS << 4;
    d->amp = 0;
    d->phase = 0;
    d->win = 0;
    d->gap = 0;
    d->mode = mode;
    d->state = ST_HUNT;
    d->inv = 0;
    d->level = 0;
    d->half = 0;
    raw_clear(d);
}

void dectpms_rearm(dectpms_t *d)
{
    d->state = ST_HUNT;
    d->gap = 0;
    d->win = 0;
    d->phase = 0;
    raw_clear(d);
}

#if TPMS_HAVE_EG53 || TPMS_HAVE_BMW
static void pack_bits(const uint8_t *raw, unsigned nraw, unsigned base,
                      uint8_t *out, unsigned nbits)
{
    unsigned nbytes = (nbits + 7u) / 8u;
    for (unsigned i = 0; i < nbytes; i++) out[i] = 0;
    for (unsigned i = 0; i < nbits && base + i < nraw; i++)
        bit_set(out, i, bit_at(raw, base + i));
}
#endif

#if TPMS_HAVE_FSK
static bool finish_ford(dectpms_t *d, tpms_pkt_t *out)
{
    uint8_t pkt[8];
    for (unsigned i = 0; i < 8u; i++) pkt[i] = 0;
    if (man_decode(d->raw, 0, d->bitn, pkt, 64) < 64u) return false;
    return tpms_parse_ford(pkt, out);
}

static bool finish_toyota(dectpms_t *d, tpms_pkt_t *out)
{
    uint8_t pkt[9];
    for (unsigned i = 0; i < 9u; i++) pkt[i] = 0;
    if (dman_decode(d->raw, d->bitn, pkt, 72) < 72u) return false;
    return tpms_parse_toyota(pkt, out);
}
#endif

#if TPMS_HAVE_SCHR
static bool finish_schrader(dectpms_t *d, tpms_pkt_t *out)
{
    uint8_t b[8];
    unsigned n = d->bitn;
    if (n < 68u) return false;
    /* Prefer an exact 68-bit row; if longer, use the last 68. */
    unsigned base = (n > 68u) ? (n - 68u) : 0u;
    for (unsigned i = 0; i < 8u; i++) b[i] = 0;
    for (unsigned i = 0; i < 64u; i++)
        bit_set(b, i, bit_at(d->raw, base + 4u + i));
    return tpms_parse_schrader(b, out);
}
#endif

#if TPMS_HAVE_EG53
static bool finish_eg53(dectpms_t *d, tpms_pkt_t *out)
{
    uint8_t b[10];
    unsigned base = (d->bitn > 120u) ? (d->bitn - 120u) : 0u;
    if (d->bitn < 120u) return false;
    pack_bits(d->raw, d->bitn, base + 40u, b, 80);
    return tpms_parse_eg53(b, out);
}
#endif

#if TPMS_HAVE_BMW
static bool finish_bmw(dectpms_t *d, tpms_pkt_t *out)
{
    uint8_t b[6];
    uint16_t w = 0;
    if (d->bitn < 61u) return false;
    for (unsigned i = 0; i < 16u; i++)
        w = (uint16_t)((w << 1) | bit_at(d->raw, i));
    if (w != 0x7FFFu) return false;
    pack_bits(d->raw, d->bitn, 16, b, 46);
    return tpms_parse_bmw(b, out);
}
#endif

#if TPMS_HAVE_NIS
static int find16(const uint8_t *raw, unsigned nbits, uint16_t pat)
{
    uint16_t w = 0;
    if (nbits < 16u) return -1;
    for (unsigned i = 0; i < nbits; i++) {
        w = (uint16_t)((w << 1) | bit_at(raw, i));
        if (i >= 15u && w == pat) return (int)(i - 15u);
    }
    return -1;
}

static bool finish_nis(dectpms_t *d, tpms_pkt_t *out)
{
    int at = find16(d->raw, d->bitn, 0x555Eu);
    unsigned pos;
    uint8_t pkt[5];
    if (at < 0) return false;
    pos = (unsigned)at + 14u;
    if (pos + 76u > d->bitn) return false;
    for (unsigned i = 0; i < 5u; i++) pkt[i] = 0;
    if (man_decode(d->raw, pos, d->bitn, pkt, 38) < 38u) return false;
    for (unsigned i = 0; i < 38u; i++)
        bit_set(pkt, i, (uint8_t)(1u - bit_at(pkt, i)));
    return tpms_parse_nis(pkt, out);
}
#endif

static bool finish_ook(dectpms_t *d, tpms_pkt_t *out)
{
    unsigned n = d->bitn;
#if TPMS_HAVE_EG53
    if (n >= 120u && finish_eg53(d, out)) return true;
#endif
#if TPMS_HAVE_SCHR
    if (n >= 68u && finish_schrader(d, out)) return true;
#endif
#if TPMS_HAVE_BMW
    if (n >= 61u && n < 120u && finish_bmw(d, out)) return true;
#endif
#if TPMS_HAVE_NIS
    if (finish_nis(d, out)) return true;
#endif
    (void)n;
    return false;
}

static void on_bit(dectpms_t *d, uint8_t bit, tpms_pkt_t *out, bool *hit)
{
    d->win = (d->win << 1) | (bit & 1u);

    if (d->state == ST_HUNT) {
#if TPMS_HAVE_FSK
        if (d->mode == TPMS_MODE_FSK) {
            uint16_t w16 = (uint16_t)(d->win & 0xffffu);
            /* Ford searches AA A9 after invert; raw air is 55 56. */
            if (w16 == 0x5556u || w16 == 0xaaa9u) {
                d->state = ST_FORD;
                d->inv = (w16 == 0xaaa9u) ? 1u : 0u;
                raw_clear(d);
                return;
            }
            /* Toyota 12-bit preamble A9E (from A9 E0); keep its last bit. */
            if ((w16 & 0x0fffu) == 0x0a9eu) {
                d->state = ST_TOYT;
                raw_clear(d);
                put_bit(d, (uint8_t)(w16 & 1u));
                return;
            }
        } else
#endif
            put_bit(d, bit);
        return;
    }

#if TPMS_HAVE_FSK
    if (d->state == ST_FORD) {
        put_bit(d, (uint8_t)(bit ^ d->inv));
        if (d->bitn >= 128u) {
            if (finish_ford(d, out)) *hit = true;
            d->state = ST_DONE;
        }
        return;
    }

    if (d->state == ST_TOYT) {
        put_bit(d, bit);
        if (d->bitn >= 160u) {
            if (finish_toyota(d, out)) *hit = true;
            d->state = ST_DONE;
        }
    }
#else
    (void)out;
    (void)hit;
#endif
}

bool dectpms_push(dectpms_t *d, uint16_t sample, tpms_pkt_t *out)
{
    if (d->state == ST_DONE) return false;

    int32_t x = (int32_t)sample - BIAS;
    d->mean += (x - (d->mean >> 4));
    int32_t y = x - (d->mean >> 4);
    int32_t ay = y >= 0 ? y : -y;
    if (ay > d->amp) d->amp = ay;
    else d->amp -= (d->amp >> 6);

    int32_t thr = (d->amp > (HYST * 4)) ? (d->amp >> 3) : HYST;
    uint8_t lvl = d->level;
    if (y > thr) lvl = 1u;
    else if (y < -thr) lvl = 0u;
    d->level = lvl;

    bool active = d->amp > (HYST * 3);
    if (!active) {
        if (d->gap < 0xffffu) d->gap++;
        uint16_t lim = (d->mode == TPMS_MODE_FSK) ? GAP_FSK : GAP_OOK;
        if (d->gap >= lim) {
            bool hit = false;
            if (d->mode == TPMS_MODE_OOK)
                hit = finish_ook(d, out);
            dectpms_rearm(d);
            return hit;
        }
    } else {
        d->gap = 0;
    }

    uint16_t step = (d->mode == TPMS_MODE_FSK) ? STEP_FSK : STEP_OOK;
    d->phase = (int32_t)((uint32_t)d->phase + step);
    if (d->phase < 65536) return false;
    d->phase -= 65536;

    if (lvl != d->half) {
        if (d->phase > 32768) d->phase -= 8192;
        else d->phase += 8192;
    }
    d->half = lvl;

    bool hit = false;
    on_bit(d, lvl, out, &hit);
    if (hit) d->state = ST_DONE;
    return hit;
}
