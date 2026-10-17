# shell

A command line for the [ClockworkPi PicoCalc](https://www.clockworkpi.com/picocalc)
with an RP2350 board (Pico 2, Pico 2 W, Pimoroni Pico Plus 2 W and the
like). From the shell you can:

- manage files on the SD card, FAT16 or FAT32;
- edit text files, such as the settings and keymap files that programs
  keep on the card (`/atom/pico-atom.cfg`, `/oric/keymaps/*.map`);
- run a UF2 program by name: `pico-oric` runs `/pico2-apps/pico-oric.uf2`.
  Leaving the program comes back to the shell.

It comes in two flavours, choose one that you are most famiar with:

| | UNIX flavour | DOS flavour |
|---|---|---|
| Commands | `ls`, `cd`, `cp`, `mv`, `rm`, `grep`, `more`, … | `DIR`, `CD`, `COPY`, `MOVE`, `DEL`, `FIND`, `MORE`, … |
| Editor | a simplified **vi** | **EDIT**, with menus |
| Scripts | `sh file` and `/etc/profile` | batch files and `C:\AUTOEXEC.BAT` |
| Screen | 40 or 64 columns, in VT-100 font | 40 columns, in IBM's PC font |
| Guide | [docs/user-guide-unix.md](docs/user-guide-unix.md) | [docs/user-guide-dos.md](docs/user-guide-dos.md) |

```
/>cd /oric                       C:\>CD \ORIC
/oric>ls keymaps | more          C:\ORIC>DIR KEYMAPS /P
/oric>vi pico-oric.cfg           C:\ORIC>EDIT PICO-ORIC.CFG
/oric>pico-oric                  C:\ORIC>PICO-ORIC
```

You copy the one you want to use to the card as `BOOT2350.UF2`.

## How it works

The shell runs alongside pelrun's
[UF2 Loader](https://github.com/pelrun/uf2loader) (2.3 to 2.5), in place of
the loader's menu.

To run a program, the shell checks the UF2 is for an RP2350 and fits in the
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

1. Get the flavour you want to use from the
   [releases](https://github.com/BlairLeduc/picocalc-shell/releases):
   `shell-unix.uf2` or `shell-dos.uf2`. Or build it (below).
2. On the SD card, rename the loader's `/BOOT2350.UF2` to `/BOOT2350.ORG`.
   You can return to the shell's `menu` command (`EXIT` in the DOS flavour)
at any time.
3. Copy the shell to the root of the card, renamed `/BOOT2350.UF2`.
4. Put your programs in `/pico2-apps/`, exactly where the loader expects them.
5. Hold Up or F1 at power-on to start the shell.

To change flavour, copy the other one over `/BOOT2350.UF2`, renamed in
the same way. To return to using the loader's men ,
rename `/BOOT2350.ORG` to `/BOOT2350.UF2`.

### The card

|File/Directory|Description|
|---|---|
|/BOOT2350.UF2 |        The shell|
|/BOOT2350.ORG |        The loader's original menu, run with `menu` (DOS: EXIT)|
|/pico2-apps/|*.uf2     Programs, run by name|
|/etc/profile |         Runs at startup for the UNIX flavour|
|/AUTOEXEC.BAT |        Runs at startup for the DOS flavour|
|/home/ |               Your home directory|
|/.shell/ |             The shell's history and large pipes|


### Programs that keep data in flash

Running a program rewrites the app partition. A program may have its files corrupted
if a program keeps files in flash when program runs. A program meant to be run from
the shell must keep its data on the SD Card.

## The UNIX flavour

**Commands:** `backlight battery bootsel cat cd clear cp df echo export
font grep head history info ls man menu mkdir more mv phosphor poweroff
pwd reboot rm rmdir set sh tail touch unset usbdisk vi view wc`.
All commands are built in. Use `man` to lists the available command and
`man name` gives a short explaination.

**The command line:** 
- quoting with `'…'`, `"…"` and `\`
- globs `*`, `?`and `[…]`
- variables (`$NAME`, `$?`, `NAME=value`)
- redirection with `>`, `>>` and `<`
- pipes, `|`
- (conditional) concatenation `;`, `&&` and `||`
- Up and Down recall the history
- Tab completes commands and paths
- `sh file` runs a script
- `/etc/profile` runs at startup.

**vi** edits files of up to 64 KB, keeping their line endings (LF or
CR LF). `view` or `vi -R` opens a file read-only. Brk leaves at once,
discarding changes.

**`font`** switches between a 40-column and a 64-column font.

[docs/user-guide-unix.md](docs/user-guide-unix.md) explains it all.

## The DOS flavour

**Commands:** `ATTRIB BACKLIGHT BATTERY BOOTSEL CALL CD CLS COPY DEL
DELTREE DIR ECHO EDIT EXIT FIND FOR GOTO HELP IF INFO MD MEM MORE MOVE
PATH PAUSE PHOSPHOR POWEROFF PROMPT RD REBOOT REM REN SET SHIFT TYPE
USBDISK VER VOL XCOPY`, with `CHDIR`, `ERASE`, `MKDIR`, `RENAME` and
`RMDIR` as DOS had them. Use `HELP` to list availble commands and `HELP name`
for more detail.

**The command line:**

- the card is `C:`
- paths use `\`
- the prompt is `PROMPT`'s, `C:\HOME>` by default
- wildcards follow DOS's rules (`*.*`, `*.TXT`)
- variables are `%NAME%`
- redirection with `>`, `>>`, `<`, `> NUL` and `|`
- F3 repeats the last line
- Tab completes commands and paths.

**Batch files** run by name, with `%1`, `SHIFT`, `GOTO`, `IF`, `FOR`,
`CALL` and `ECHO OFF`. `C:\AUTOEXEC.BAT` runs at startup.

**EDIT** is modelled from the DOS: a menu bar, dialog boxes, Alt
and the arrows to select, and Ctrl+X, Ctrl+C and Ctrl+V. It edits files
of up to 64 KB, keeping their line endings.

[docs/user-guide-dos.md](docs/user-guide-dos.md) explains it all.

## In both flavours

**Files:** names are case-insensitive but keep their case, as FAT has them.
Names starting with `.`, and names FAT marks hidden or system, are left
out of listings unless asked for (`ls -a`, `DIR /A`). The PicoCalc has no
clock, so files get a fixed timestamp. Every write goes to `name.new`
first and is renamed into place, so a failed write leaves the old file as
it was.

**`usbdisk`** exposes the SD card to a computer over USB. You can
eject it on the computer, or press Esc, to come back to the shell.

**The PicoCalc:** `battery`, `backlight`, `phosphor` (the text's colour:
white, green or amber), `info`, `poweroff`, `reboot` and `bootsel`.

Program arguments (`pico-oric game.tap`) are not supported yet.

## Adding an exit command to a program

[`include/picocalc_shell.h`](include/picocalc_shell.h) is one
independent header:

```c
#include "picocalc_shell.h"

save_settings();                 // finish every card write first
if (!picocalc_shell_exit())      // reboots into the shell
    show_message("Cannot return to the shell");
```

The function returns false, and does nothing, if the SD Card cannot be
accessed, or if UF2 Loader did not start the program (it was copied on
in BOOTSEL). The program can then do something else, such as power off.
`picocalc_shell_loader_present()` and `picocalc_shell_card_present()`
tell it beforehand, to label its menu item. The header is under the MIT licence as well
as the GPL-3.0, so a program under any licence can include it.

## Building

The firmware needs the [Pico SDK](https://github.com/raspberrypi/pico-sdk)
and the Arm toolchain; `tools/build.sh` uses the newest it finds under
`~/.pico-sdk/` (where the Pico VS Code extension puts them).

```sh
tools/build.sh -DPICO_SHELL_UART=OFF build/pico-release                               # UNIX flavour, release
tools/build.sh -DPICO_SHELL_UART=OFF -DPICO_SHELL_FLAVOUR=dos build/pico-dos-release  # DOS flavour, release
tools/build.sh                                                                        # development: UART1 log, mirror and typed keys
```

The output is `build/<variant>/BOOT2350.uf2`. A release offers the two
release builds as `shell-unix.uf2` and `shell-dos.uf2`.

The shell's core is portable C, and builds and runs on a computer over a
FAT image file. This is how it is tested:

```sh
cmake -S . -B build/host -DPICO_SHELL_HOST=ON
cmake --build build/host -j
ctest --test-dir build/host --output-on-failure
build/host/shell card.img        # the UNIX flavour in your terminal; a new 64 MB FAT32 image if none
build/host/shell-dos card.img    # the DOS flavour
```

## Documents

- [docs/user-guide-unix.md](docs/user-guide-unix.md): the UNIX flavour, for users:
  every command, the command line, vi and the card.
- [docs/user-guide-dos.md](docs/user-guide-dos.md): the DOS flavour, for
  users: every command, batch files, EDIT and the card.
- [docs/design.md](docs/design.md): the design, its settled decisions and
  the milestones.
- [docs/hardware-notes.md](docs/hardware-notes.md): the PicoCalc's
  hardware, as measured.
- [docs/milestones.md](docs/milestones.md): what each milestone checked,
  on which board.

## Licence

GPL-3.0; see [LICENSE](LICENSE). `include/picocalc_shell.h` is also under
the MIT licence. Code from other projects (UF2 Loader, the owner's
emulators and picocalc-text-starter, Pico Logo's vi) and the DOS
flavour's font (IBM's PC font from VileR's Ultimate Oldschool PC Font
Pack, CC BY-SA 4.0) are listed in [THIRD-PARTY.md](THIRD-PARTY.md).
