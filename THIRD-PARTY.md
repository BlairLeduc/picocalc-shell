# Third-party material

Everything in this repository is this project's own work under the GPL-3.0 in
[`LICENSE`](LICENSE), with the exceptions below. Each file taken from
elsewhere says where it came from in its header.

## The owner's emulators (pico-oric) — GPL-3.0

The same licence as this project, and the same owner.

| Here | From pico-oric | Changes |
|---|---|---|
| `src/port/lcd.c`, `lcd.h` | `src/port/lcd.c`, `lcd.h` | hardware vertical scroll added |
| `src/port/southbridge.c`, `.h` | the same | comments only |
| `src/port/kbd.c`, `.h` | the same | one core's ring in place of the cross-core rings |
| `src/port/board.c`, `.h` | the same | trimmed: no die temperature |
| `cmake/version.cmake` | the same | the header's name |
| `src/port/sd.c`, `sd.h` | the same | comments only |
| `src/port/diskio.c` | the same | `card_poll()`, for one mount held while the card stays in |
| `src/fatfs/ffconf.h` | `src/port/fatfs/ffconf.h` | the values its header lists |
| `test/host/test_util.h` | the same | the include guard |
| `tools/build.sh`, `uart-log.sh`, `uart-type.sh`, `uart-screen.sh` | `tools/` | names, keys and pacing |

## picocalc-text-starter — MIT, the owner's

<https://github.com/BlairLeduc/picocalc-text-starter>, Copyright (c) 2025
Blair Leduc, under the MIT licence, which is compatible with the GPL-3.0.

| Here | From the starter | Changes |
|---|---|---|
| `src/core/term.c`, `term.h` | `drivers/display.c`, `display.h` | draws into a cell grid; the list of departures is at the top of `term.c` |
| `src/port/screen.c` | `drivers/lcd.c` | its glyph drawing, bold, underline and cursor, over the emulators' `lcd.c` |
| `src/core/keycodes.h` | `drivers/keyboard.h` | the key code table only |
| `src/core/displaytest.c` | `tests.c`, `displaytest()` | split into its two phases; timing moved to `shell.c` |

## The fonts — the owner's

`src/core/font.h`, `font-8x10.c` and `font-5x10.c` are copied unchanged from
the owner's `references/fonts/` (design.md §4.1).

## FatFs — ChaN

The shell links FatFs R0.15 (with patch 1), in the firmware for the SD card
and on the host for image files. It is **not in the tree**: CMake
(`cmake/fatfs.cmake`) copies `ff.c`, `ff.h`, `ffunicode.c` and `diskio.h`
out of the Pico SDK's `lib/tinyusb/lib/fatfs/source/` into the build
directory at configure time, as the emulators do. The one FatFs file in the
repository is `src/fatfs/ffconf.h`, which started as the SDK's copy and
records in its header which values were changed.

> FatFs — Generic FAT Filesystem Module, Copyright (C) 2022, ChaN, all
> right reserved.
>
> FatFs module is an open source software. Redistribution and use of FatFs
> in source and binary forms, with or without modification, are permitted
> provided that the following condition is met:
>
> 1. Redistributions of source code must retain the above copyright notice,
>    this condition and the following disclaimer.
>
> This software is provided by the copyright holder and contributors "AS
> IS" and any warranties related to this software are DISCLAIMED. The
> copyright owner or contributors be NOT LIABLE for any damages caused by
> use of this software.

## Pico SDK import script — Raspberry Pi

`cmake/pico_sdk_import.cmake` is the Raspberry Pi Pico SDK's own
`external/pico_sdk_import.cmake`, copied as the SDK's instructions direct,
under the SDK's BSD-3-Clause licence.
