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

/**
 * Read one MSB-first bit from a packed buffer.
 *
 * @param bits Packed bytes, high bit on the left.
 * @param i Bit index from the start of bits.
 * @return Zero or one.
 */
static uint8_t bit_at(const uint8_t *bits, unsigned i)
{
    return (uint8_t)((bits[i >> 3] >> (7u - (i & 7u))) & 1u);
}

/**
 * Write one MSB-first bit in a packed buffer.
 *
 * @param bits Packed bytes, high bit on the left.
 * @param i Bit index from the start of bits.
 * @param v Nonzero stores a one.
 */
static void bit_set(uint8_t *bits, unsigned i, uint8_t v)
{
    if (v) bits[i >> 3] = (uint8_t)(bits[i >> 3] | (uint8_t)(0x80u >> (i & 7u)));
    else bits[i >> 3] = (uint8_t)(bits[i >> 3] & (uint8_t)~(0x80u >> (i & 7u)));
}

#if TPMS_HAVE_FSK || TPMS_HAVE_SCHR
/**
 * CRC-8 over a buffer, MSB first and not reflected.
 *
 * @param d Input bytes.
 * @param n Number of bytes.
 * @param poly Generator polynomial.
 * @param init Initial register value.
 * @return CRC of the buffer.
 */
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
/**
 * Read a big-endian 32-bit value.
 *
 * @param b At least four bytes.
 * @return The first four bytes as an integer.
 */
static uint32_t be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}
#endif

/**
 * Convert pressure in tenths of a kilopascal to whole PSI.
 *
 * @param kpa10 Pressure times ten, in kPa.
 * @return Nearest whole PSI.
 */
static int16_t psi_from_kpa10(unsigned kpa10)
{
    return (int16_t)((kpa10 * 145u + 5000u) / 10000u);
}

#if TPMS_HAVE_FSK
/**
 * Decode one Ford frame after Manchester decoding.
 *
 * Bytes 0..6 are summed and must equal byte 7. Pressure is whole PSI.
 * Temperature is omitted when its high bit is set.
 *
 * @param b Eight payload bytes, checksum last.
 * @param out Filled with id, PSI, Celsius, and TPMS_FORD.
 * @return True when the checksum and status flags are valid.
 */
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

/**
 * Decode one Toyota frame after differential Manchester decoding.
 *
 * CRC-8 covers the first eight bytes, poly 0x07 and init 0x80.
 * The repeated pressure field must match.
 *
 * @param b Nine payload bytes, CRC last.
 * @param out Filled with id, PSI, Celsius, and TPMS_TOYT.
 * @return True when the CRC and the pressure check match.
 */
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
/**
 * Decode one Schrader MRXGG4 frame after the sync nibble is removed.
 *
 * CRC-8 covers the first seven bytes, poly 0x07 and init 0xF0.
 * Pressure is stored as 2.5 kPa steps and returned as whole PSI.
 *
 * @param b Eight payload bytes, CRC last.
 * @param out Filled with id, PSI, Celsius, and TPMS_SCHR.
 * @return True when the CRC matches.
 */
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
/**
 * Decode one Schrader EG53 frame.
 *
 * The sum of the first nine bytes must equal the last byte.
 * An all-zero body is rejected. Fahrenheit is converted to Celsius.
 *
 * @param b Ten payload bytes, checksum last.
 * @param out Filled with id, PSI, Celsius, and TPMS_EG53.
 * @return True when the checksum matches and the body is not empty.
 */
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
/**
 * Decode one BMW or Mini Schrader frame.
 *
 * The 24-bit id must not be 0 or all ones. Pressure above 450 kPa,
 * or a temperature outside -40 C to 85 C, is rejected.
 *
 * @param b Six bytes holding the 46-bit body.
 * @param out Filled with id, PSI, Celsius, and TPMS_BMW.
 * @return True when the check bits and the range limits pass.
 */
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
/**
 * Decode one Nissan or Subaru frame on the Nissan pressure scale.
 *
 * PSI is the raw field divided by four. There is no temperature.
 *
 * @param b Five payload bytes.
 * @param out Filled with id, PSI, TPMS_NO_TEMP, and TPMS_NISS.
 * @return True when the nibble checksum matches and the body is not empty.
 */
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

#if TPMS_HAVE_FSK || TPMS_HAVE_NIS
/**
 * Manchester-decode pairs, emitting the second bit when they differ.
 *
 * This matches rtl_433. A pair of equal bits ends the row.
 *
 * @param raw Packed source bits, MSB first.
 * @param start Index of the first source bit.
 * @param nraw Number of source bits.
 * @param out Packed decoded bits.
 * @param max_bits Capacity of out, in bits.
 * @return Number of decoded bits written.
 */
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
/**
 * Differential-Manchester decode a row that already passed its sync.
 *
 * This is the rtl_433 decoder, simplified for one aligned row.
 *
 * @param raw Packed source bits, MSB first.
 * @param nraw Number of source bits.
 * @param out Packed decoded bits.
 * @param max_bits Capacity of out, in bits.
 * @return Number of decoded bits written.
 */
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

/**
 * Drop every collected payload bit.
 *
 * @param d Slicer state.
 */
static void raw_clear(dectpms_t *d)
{
    d->bitn = 0;
    for (unsigned i = 0; i < sizeof d->raw; i++) d->raw[i] = 0;
}

/**
 * Append one payload bit when the raw buffer still has room.
 *
 * @param d Slicer state.
 * @param bit Zero or one.
 */
static void put_bit(dectpms_t *d, uint8_t bit)
{
    if (d->bitn >= sizeof d->raw * 8u) return;
    bit_set(d->raw, d->bitn++, bit);
}

/**
 * Clear the slicer and select FSK or OOK.
 *
 * @param d Slicer state.
 * @param mode TPMS_MODE_FSK or TPMS_MODE_OOK.
 */
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

/**
 * Drop a partial packet and hunt for the next preamble.
 *
 * @param d Slicer state.
 */
void dectpms_rearm(dectpms_t *d)
{
    d->state = ST_HUNT;
    d->gap = 0;
    d->win = 0;
    d->phase = 0;
    raw_clear(d);
}

#if TPMS_HAVE_EG53 || TPMS_HAVE_BMW
/**
 * Copy a bit range into a fresh MSB-first buffer.
 *
 * @param raw Packed source bits.
 * @param nraw Number of source bits.
 * @param base First source bit to copy.
 * @param out Destination. Cleared before the copy.
 * @param nbits Number of bits to copy.
 */
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
/**
 * Manchester-decode the Ford bit buffer and parse the 8-byte frame.
 *
 * @param d Slicer holding the raw Ford bits.
 * @param out Filled on success.
 * @return True when a full frame decodes and checks out.
 */
static bool finish_ford(dectpms_t *d, tpms_pkt_t *out)
{
    uint8_t pkt[8];
    for (unsigned i = 0; i < 8u; i++) pkt[i] = 0;
    if (man_decode(d->raw, 0, d->bitn, pkt, 64) < 64u) return false;
    return tpms_parse_ford(pkt, out);
}

/**
 * Differential-Manchester decode the Toyota buffer and parse it.
 *
 * @param d Slicer holding the raw Toyota bits.
 * @param out Filled on success.
 * @return True when a full frame decodes and checks out.
 */
static bool finish_toyota(dectpms_t *d, tpms_pkt_t *out)
{
    uint8_t pkt[9];
    for (unsigned i = 0; i < 9u; i++) pkt[i] = 0;
    if (dman_decode(d->raw, d->bitn, pkt, 72) < 72u) return false;
    return tpms_parse_toyota(pkt, out);
}
#endif

#if TPMS_HAVE_SCHR
/**
 * Take the last 68 OOK bits, drop the sync nibble, and parse Schrader.
 *
 * @param d Slicer holding the OOK burst.
 * @param out Filled on success.
 * @return True when 68 bits are present and the frame checks out.
 */
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
/**
 * Drop the 40-bit EG53 preamble and parse the 80-bit body.
 *
 * @param d Slicer holding the OOK burst.
 * @param out Filled on success.
 * @return True when 120 bits are present and the frame checks out.
 */
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
/**
 * Check the BMW 16-bit sync and parse the following 46 bits.
 *
 * @param d Slicer holding the OOK burst.
 * @param out Filled on success.
 * @return True when the sync and the frame both check out.
 */
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
/**
 * Find the first 16-bit pattern in a packed bit row.
 *
 * @param raw Packed bits, MSB first.
 * @param nbits Number of bits to search.
 * @param pat Pattern to match once 16 bits are in the window.
 * @return Bit index of the match, or -1.
 */
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

/**
 * Find the Nissan sync, Manchester-decode, invert, and parse.
 *
 * @param d Slicer holding the OOK burst.
 * @param out Filled on success.
 * @return True when the sync, the length, and the frame all check out.
 */
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

/**
 * Try each OOK parser this flavour includes, longest frame first.
 *
 * @param d Slicer holding the OOK burst.
 * @param out Filled on success.
 * @return True when one parser accepts the burst.
 */
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

#if TPMS_HAVE_FSK
/**
 * Drop a payload that failed its check and hunt again on the same bit clock.
 *
 * FSK discriminator noise never goes quiet, so the gap rearm cannot be relied on.
 *
 * @param d Slicer state.
 */
static void hunt_again(dectpms_t *d)
{
    d->state = ST_HUNT;
    d->win = 0;
    raw_clear(d);
}
#endif

/**
 * Advance the preamble hunt or the payload from one demodulated bit.
 *
 * @param d Slicer state.
 * @param bit Zero or one, already sliced.
 * @param out Filled when a frame completes.
 * @param hit Set true when out holds a packet.
 */
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
            else hunt_again(d);
        }
        return;
    }

    if (d->state == ST_TOYT) {
        put_bit(d, bit);
        if (d->bitn >= 160u) {
            if (finish_toyota(d, out)) *hit = true;
            else hunt_again(d);
        }
    }
#else
    (void)out;
    (void)hit;
#endif
}

/**
 * Feed one AF sample and report a finished frame.
 *
 * Samples are 12-bit PCM centered on 2048, taken at 38400 Hz.
 * FSK frames complete on length. OOK frames complete when the carrier drops.
 *
 * @param d Slicer state.
 * @param sample One AF sample.
 * @param out Filled when a packet checks out.
 * @return True when out holds a new packet.
 */
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
