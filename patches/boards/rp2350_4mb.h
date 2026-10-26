/* rp2350_4mb.h — the Pico 2's board, with 4 MB of flash, for M7's
 * stage3 (tools/build-loader.sh). stage3 sizes its app partition from
 * the flash size when it is built, so each size is its own build; the
 * rest is the Pico 2's, which the shell's boards all run (design.md M7).
 *
 * CMake reads only this file's pico_board_cmake lines, so they are
 * repeated from boards/pico2.h. Only preprocessor directives: the
 * assembler includes it too. */

#ifndef _BOARDS_RP2350_4MB_H
#define _BOARDS_RP2350_4MB_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (4 * 1024 * 1024))
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)

#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#include "boards/pico2.h"

#endif
