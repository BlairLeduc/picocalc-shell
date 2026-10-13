/* bininfo.h — a program's binary info, as the Pico SDK lays it out
 * (pico/binary_info/defs.h, structure.h), read from an image of it.
 *
 * Portable: the image is bytes, and the address it is seen at by the
 * program is given, so the same code reads stage3 in flash on the board
 * (design.md §8: the loader's version) and a UF2 decoded on the host. */
#ifndef SHELL_BININFO_H
#define SHELL_BININFO_H

#include <stdint.h>

/* The SDK's ids for strings with the Raspberry Pi tag. */
#define BININFO_ID_PROGRAM_NAME    0x02031c86u
#define BININFO_ID_PROGRAM_VERSION 0x11a9bc3au

/* The string with tag RP and `id` in the image of `len` bytes that the
 * program sees at `addr`, or NULL if it has none or the binary info is
 * malformed. Points into img. Strings in RAM are found through the
 * binary info's address mapping table. */
const char *bininfo_string(const uint8_t *img, uint32_t len, uint32_t addr, uint32_t id);

#endif /* SHELL_BININFO_H */
