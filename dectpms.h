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
bool tpms_parse_ford(const uint8_t b[8], tpms_pkt_t *out);
bool tpms_parse_toyota(const uint8_t b[9], tpms_pkt_t *out);
bool tpms_parse_schrader(const uint8_t b[8], tpms_pkt_t *out);
bool tpms_parse_eg53(const uint8_t b[10], tpms_pkt_t *out);
bool tpms_parse_bmw(const uint8_t b[6], tpms_pkt_t *out);
bool tpms_parse_nis(const uint8_t b[5], tpms_pkt_t *out);

/* Sample-rate demodulator. FS = 38400. */
typedef struct {
    int32_t  mean, amp, phase;
    uint32_t win;          /* last 32 PCM / OOK bits */
    uint16_t gap;
    uint8_t  mode, state, bitn, inv, level, half;
    uint8_t  raw[40];      /* collected payload bits, MSB first packing */
} dectpms_t;

void dectpms_init(dectpms_t *d, uint8_t mode);
void dectpms_rearm(dectpms_t *d);
bool dectpms_push(dectpms_t *d, uint16_t sample, tpms_pkt_t *out);

#endif
