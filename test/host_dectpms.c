/* Host checks for Ford / Toyota / Schrader TPMS field parsers. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../dectpms.h"
#include "../dectpms.c"

static int fails;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        printf("FAIL: %s\n", msg);
        fails++;
    }
}

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

int main(void)
{
    fails = 0;
    test_ford();
    test_toyota();
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
