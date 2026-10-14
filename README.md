# shell

A UNIX-like command line for the [ClockworkPi PicoCalc](https://www.clockworkpi.com/picocalc)
with an RP2350 board (Pico 2, Pico 2 W, Pimoroni Pico Plus 2 W and the
like). From the shell you can:

- manage files on the SD card, FAT16 or FAT32: `ls`, `cd`, `cp`, `mv`,
  `rm`, `mkdir`, `cat`, `more`, `grep`, …;
- edit a text file in a simplified **vi**, such as the settings and keymap
  files that programs keep on the card (`/atom/pico-atom.cfg`,
  `/oric/keymaps/*.map`);
- run a UF2 program by name: `pico-oric` runs `/pico2-apps/pico-oric.uf2`.
  Leaving the program comes back to the shell.

```
/>cd /oric
/oric>ls keymaps | more
/oric>vi pico-oric.cfg
/oric>pico-oric
```

The prompt is the current directory followed by `>`.

## How it works

The shell runs alongside pelrun's
[UF2 Loader](https://github.com/pelrun/uf2loader) (2.3 to 2.5), in place of
the loader's own menu. It is a RAM program installed on the card as
`/BOOT2350.UF2`; the loader in flash is not changed.

To run a program, the shell checks the UF2 is for an RP2350 and fits the
app partition, writes it to flash, and reboots into it. Only the sectors
that differ from what is already in flash are written, so running the same
program again takes about a second. A program is never left half-written:
if the power goes mid-write, the PicoCalc comes back to the shell.

There are two ways back to the shell from a program:

- **Hold Up, F1 or F5 at power-on.** This works for every program.
- **The program's own exit command**, if it has one. Power-on with no key
  held starts the last program, as the loader does.

## Installing

You need a PicoCalc with an RP2350 board and UF2 Loader 2.3 or later in
flash.

1. Build `BOOT2350.UF2` (below), using the release build.
2. On the SD card, rename the loader's `/BOOT2350.UF2` to `/BOOT2350.ORG`.
   The shell's `menu` command runs it, so the loader's menu stays one
   command away.
3. Copy the shell's `BOOT2350.UF2` to the root of the card.
4. Put your programs in `/pico2-apps/`, the loader menu's own directory.
5. Hold Up at power-on to start the shell.

To go back to the loader's menu for good, rename `/BOOT2350.ORG` to
`/BOOT2350.UF2`.

### The card

```
/BOOT2350.UF2         the shell
/BOOT2350.ORG         the loader's own menu, run with `menu`
/pico2-apps/*.uf2     programs, run by name
/etc/profile          run when the shell starts
/home/                the starting directory ($HOME)
/.shell/              the shell's history and large pipes
```

### Programs that keep data in flash

Running a program rewrites the app partition, so a program that keeps its
own files in flash, after its code, loses them when another program runs.
A program meant to be run from the shell keeps its data on the card.
**Pico Logo** is the known exception: it keeps a filesystem in flash.

## Using it

**Commands:** `backlight battery bootsel cat cd clear cp df echo export
font grep head help history info ls menu mkdir more mv phosphor poweroff
pwd reboot rm rmdir set sh tail touch unset usbdisk vi view wc`.
Everything is built in, so it runs at once. `help` lists them and
`help name` explains one; [docs/user-guide.md](docs/user-guide.md)
explains them all.

**The command line:** quoting with `'…'`, `"…"` and `\`; globs `*`, `?`
and `[…]`; variables (`$NAME`, `$?`, `NAME=value`); redirection with `>`,
`>>` and `<`; pipes; `;`, `&&` and `||`. Up and Down recall the history,
and Tab completes commands and paths. `sh file` runs a script, and
`/etc/profile` runs at startup. There is no `if`, `for` or functions.

**Files:** names are case-insensitive but keep their case, as FAT has them.
`ls` hides names starting with `.` and names FAT marks hidden or system,
unless given `-a`. The PicoCalc has no clock, so files get a fixed
timestamp. Every write goes to `name.new` first and is renamed into place,
so a failed write leaves the old file as it was.

**vi** edits files of up to 64 KB, keeping their line endings (LF or
CR LF). `view` or `vi -R` opens a file read-only. Brk leaves at once,
discarding changes.

**`usbdisk`** offers the SD card to a computer over USB. Eject it on the
computer, or press Esc, to come back to the shell.

**`font`** switches between a 40-column and a 64-column font.

Program arguments (`pico-oric game.tap`) are not supported yet.

## Adding an exit command to a program

[`include/picocalc_shell.h`](include/picocalc_shell.h) is one header, with
nothing to link:

```c
#include "picocalc_shell.h"

save_settings();                 // finish every card write first
if (!picocalc_shell_exit())      // reboots into the shell
    show_message("No SD card");
```

It returns false, and does nothing, if there is no card to load the shell
from. The header is under the MIT licence as well as the GPL-3.0, so a
program under any licence can include it.

## Building

The firmware needs the [Pico SDK](https://github.com/raspberrypi/pico-sdk)
and the Arm toolchain; `tools/build.sh` uses the newest it finds under
`~/.pico-sdk/` (where the Pico VS Code extension puts them).

```sh
tools/build.sh -DPICO_SHELL_UART=OFF build/pico-release   # the release build
tools/build.sh                                            # development: UART1 log, mirror and typed keys
```

The output is `build/<variant>/BOOT2350.uf2`.

The shell's core is portable C, and builds and runs on a computer over a
FAT image file. This is how it is tested:

```sh
cmake -S . -B build/host -DPICO_SHELL_HOST=ON
cmake --build build/host -j
ctest --test-dir build/host --output-on-failure
build/host/shell card.img        # the shell in your terminal; a new 64 MB FAT32 image if none
```

## Documents

- [docs/user-guide.md](docs/user-guide.md): every command, the command
  line, vi and the card, for users.
- [docs/design.md](docs/design.md): the design, its settled decisions and
  the milestones.
- [docs/hardware-notes.md](docs/hardware-notes.md): the PicoCalc's
  hardware, as measured.
- [docs/milestones.md](docs/milestones.md): what each milestone checked,
  on which board.

## Licence

GPL-3.0; see [LICENSE](LICENSE). `include/picocalc_shell.h` is also under
the MIT licence. Code from other projects (UF2 Loader, the owner's
emulators and picocalc-text-starter, Pico Logo's vi) is listed in
[THIRD-PARTY.md](THIRD-PARTY.md).
