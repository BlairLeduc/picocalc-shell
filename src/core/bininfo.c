/* bininfo.c — a program's binary info (bininfo.h).
 *
 * Near the start of the image, after the vector table, the SDK puts a
 * header of five words: BINARY_INFO_MARKER_START, the start and end of
 * an array of pointers to entries, the address mapping table, and
 * BINARY_INFO_MARKER_END. A string entry is { u16 type, u16 tag, u32 id,
 * u32 pointer }. The mapping table is { source, dest start, dest end }
 * rows ending with a zero source: data the program copies to RAM, which
 * in the image is still at its source. Every pointer is checked to lie
 * in the image, so a damaged one gives NULL, never a stray read. */

#include "bininfo.h"

#include <stdbool.h>
#include <stddef.h>

#define MARKER_START 0x7188ebf2u
#define MARKER_END   0xe71aa390u
#define TYPE_ID_AND_STRING 6u
#define TAG_RP ((uint16_t)('R' | ('P' << 8)))

/* How far into the image the header is looked for: past the RP2350's
 * vector table, as picotool looks. */
#define HEADER_SEARCH 1024u
#define MAP_ROWS_MAX  16u
#define ENTRIES_MAX   256u
#define STRING_MAX    256u

typedef struct {
    const uint8_t *img;
    uint32_t len, addr, map;
} image_t;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The image offset of `n` bytes the program sees at `a`, or -1:
 * directly, or through the mapping table. */
static int64_t direct(const image_t *im, uint32_t a, uint32_t n) {
    if (a < im->addr || a - im->addr > im->len || im->len - (a - im->addr) < n) return -1;
    return a - im->addr;
}

static int64_t locate(const image_t *im, uint32_t a, uint32_t n) {
    int64_t at = direct(im, a, n);
    if (at >= 0 || !im->map) return at;
    for (uint32_t row = 0; row < MAP_ROWS_MAX; row++) {
        int64_t r = direct(im, im->map + row * 12u, 12u);
        if (r < 0) return -1;
        uint32_t src = rd32(im->img + r), lo = rd32(im->img + r + 4), hi = rd32(im->img + r + 8);
        if (src == 0) return -1;
        if (a >= lo && a < hi && hi - a >= n) return direct(im, src + (a - lo), n);
    }
    return -1;
}

const char *bininfo_string(const uint8_t *img, uint32_t len, uint32_t addr, uint32_t id) {
    image_t im = { img, len, addr, 0 };
    uint32_t search = len < HEADER_SEARCH ? len : HEADER_SEARCH;
    for (uint32_t h = 0; h + 20u <= search; h += 4) {
        if (rd32(img + h) != MARKER_START || rd32(img + h + 16) != MARKER_END) continue;
        uint32_t first = rd32(img + h + 4), last = rd32(img + h + 8);
        im.map = rd32(img + h + 12);
        if (last < first || (last - first) / 4u > ENTRIES_MAX) return NULL;
        for (uint32_t p = first; p < last; p += 4) {
            int64_t at = locate(&im, p, 4);
            if (at < 0) return NULL;
            int64_t e = locate(&im, rd32(img + at), 12);
            if (e < 0) continue;
            const uint8_t *ent = img + e;
            if ((uint32_t)(ent[0] | ent[1] << 8) != TYPE_ID_AND_STRING ||
                (uint16_t)(ent[2] | ent[3] << 8) != TAG_RP || rd32(ent + 4) != id)
                continue;
            int64_t s = locate(&im, rd32(ent + 8), 1);
            if (s < 0) return NULL;
            /* The string must end inside the image. */
            for (uint32_t i = 0; i < STRING_MAX && (uint64_t)s + i < len; i++)
                if (img[s + i] == '\0') return (const char *)img + s;
            return NULL;
        }
        return NULL;
    }
    return NULL;
}
