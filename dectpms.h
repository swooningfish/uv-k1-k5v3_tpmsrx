/* TPMS bit parsers and AF slicer (Ford / Toyota FSK, Schrader OOK). */
#ifndef DECTPMS_H
#define DECTPMS_H
#include <stdint.h>
#include <stdbool.h>

enum {
    TPMS_FORD = 0, TPMS_TOYT = 1, TPMS_SCHR = 2,
    TPMS_EG53 = 3, TPMS_BMW = 4, TPMS_NISS = 5
};
enum { TPMS_MODE_FSK = 0, TPMS_MODE_OOK = 1 };

/* One firmware image is one flavour. The host test builds every parser. */
#if !defined(TPMS_FLAVOUR_EU) && !defined(TPMS_FLAVOUR_NA) && \
    !defined(TPMS_FLAVOUR_EU2) && !defined(TPMS_FLAVOUR_NA2)
#define TPMS_HOST_ALL 1
#endif
#if defined(TPMS_HOST_ALL) || defined(TPMS_FLAVOUR_EU) || defined(TPMS_FLAVOUR_NA)
#define TPMS_HAVE_FSK 1
#define TPMS_HAVE_SCHR 1
#endif
#if defined(TPMS_HOST_ALL) || defined(TPMS_FLAVOUR_EU2) || defined(TPMS_FLAVOUR_NA2)
#define TPMS_HAVE_EG53 1
#endif
#if defined(TPMS_HOST_ALL) || defined(TPMS_FLAVOUR_EU2)
#define TPMS_HAVE_BMW 1
#endif
#if defined(TPMS_HOST_ALL) || defined(TPMS_FLAVOUR_NA2)
#define TPMS_HAVE_NIS 1
#endif

#define TPMS_NO_TEMP (-32768)

typedef struct {
    uint32_t id;
    int16_t  psi;     /* whole PSI */
    int16_t  temp_c;  /* whole C, or TPMS_NO_TEMP */
    uint8_t  model;
} tpms_pkt_t;

/* Host-testable field parsers (already Manchester / sync-stripped). */
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
bool tpms_parse_ford(const uint8_t b[8], tpms_pkt_t *out);
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
bool tpms_parse_toyota(const uint8_t b[9], tpms_pkt_t *out);
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
bool tpms_parse_schrader(const uint8_t b[8], tpms_pkt_t *out);
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
bool tpms_parse_eg53(const uint8_t b[10], tpms_pkt_t *out);
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
bool tpms_parse_bmw(const uint8_t b[6], tpms_pkt_t *out);
/**
 * Decode one Nissan or Subaru frame on the Nissan pressure scale.
 *
 * PSI is the raw field divided by four. There is no temperature.
 *
 * @param b Five payload bytes.
 * @param out Filled with id, PSI, TPMS_NO_TEMP, and TPMS_NISS.
 * @return True when the nibble checksum matches and the body is not empty.
 */
bool tpms_parse_nis(const uint8_t b[5], tpms_pkt_t *out);

/* Sample-rate demodulator. FS = 38400. */
typedef struct {
    int32_t  mean, amp, phase;
    uint32_t win;          /* last 32 PCM / OOK bits */
    uint16_t gap, bitn;
    uint8_t  mode, state, inv, level, half;
    uint8_t  raw[40];      /* collected payload bits, MSB first packing */
} dectpms_t;

/**
 * Clear the slicer and select FSK or OOK.
 *
 * @param d Slicer state.
 * @param mode TPMS_MODE_FSK or TPMS_MODE_OOK.
 */
void dectpms_init(dectpms_t *d, uint8_t mode);
/**
 * Drop a partial packet and hunt for the next preamble.
 *
 * @param d Slicer state.
 */
void dectpms_rearm(dectpms_t *d);
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
bool dectpms_push(dectpms_t *d, uint16_t sample, tpms_pkt_t *out);

#endif
