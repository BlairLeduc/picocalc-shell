# PicoCalc Shell — design

Status: **Draft 3, 2026-10-12.** Feasibility study and first design. M0
(the skeleton) and M1 (files) are done ([milestones.md](milestones.md)).

The model programs are the owner's emulators (pico-atom, pico-jupiter-ace
and pico-oric). Pico Logo is a special case, because it keeps a filesystem
in the PicoCalc's own flash (§3.6).

The owner's decisions so far are listed below. Do not reopen them. No
questions are open; the design is ready for M0.

| Date | Decision | § |
|---|---|---|
| 2026-10-12 | The shell replaces the UF2 Loader's `BOOT2350.UF2`. | 3.2 |
| 2026-10-12 | Programs may add an exit command that returns to the shell. | 3.4 |
| 2026-10-12 | Power-on with no key held starts the last program, as the loader does now. | 3.4 |
| 2026-10-12 | Program arguments are deferred. | 3.5 |
| 2026-10-12 | The terminal uses the owner's own 10-pixel fonts. | 4.1 |
| 2026-10-12 | The shell is called **shell**. Its prompt is the current directory followed by `>`. | 4.4 |
| 2026-10-12 | The licence is GPL-3.0. | 9 |
| 2026-10-12 | RP2350 boards only; RP2040 boards are left out. | 1 |
| 2026-10-12 | Programs live in `/pico2-apps/`, the loader menu's own directory, not `/bin`. | 4.3 |
| 2026-10-12 | The LCD's SPI runs at 75 MHz by default, after M0's measurements; 25 MHz stays as a build option. | 4.1 |
| 2026-10-12 | `ls` hides names FAT marks hidden or system, as it hides names starting with `.`. | 4.2 |
| 2026-10-12 | Variables come with M4. | 4.4, 10 |

References: **HW §N** is [hardware-notes.md](hardware-notes.md). **EL §N** is
the emulators' `emulator-lessons.md`
(`references/pico-oric/docs/emulator-lessons.md`). Its architecture,
testing and process chapters apply here unchanged.

## 1. Goal

A UNIX-like command line for the PicoCalc. From the shell you can:

- manage files on the SD card (`ls`, `cp`, `mv`, `rm`, `mkdir`, `cat`, …)
  on FAT16 or FAT32;
- edit a text file in a simplified vi. The main use is the settings and
  keymap files that programs keep on the card, such as
  `/atom/pico-atom.cfg` and `/oric/keymaps/*.map`;
- run a UF2 program by name, like a compiled binary: `pico-oric` runs
  `/pico2-apps/pico-oric.uf2`. Exiting the program comes back to the shell.

Non-goals: multitasking, users and permissions, a POSIX `sh` language,
networking, and RP2040 boards (*settled 2026-10-12*). The PicoCalc runs one firmware
image at a time, and the shell does not change that.

## 2. Feasibility

**Verdict: feasible.** Most of the work is already done:

- **UF2 Loader** (pelrun, v2.5) provides the boot chain, flashing from the
  card, and the menu-as-a-RAM-program slot the shell takes over.
- **The emulators** provide tested PicoCalc drivers, a FatFs configuration
  and a build/test/process model, all GPL-3.0 like the loader.
- **picocalc-text-starter** (the owner's, MIT) provides the terminal: a
  VT100/ANSI emulator with the DEC special graphics set, written for these
  fonts, plus text rendering with hardware scroll and the southbridge's
  power, battery and backlight calls. It is older than the hardware notes,
  so where the two differ the notes win (§4.1). Its `fat32.c` and `clib.c`
  are not used: they are known to be buggy.
- **Pico Logo** provides only its vi key layer, which is pure C with no
  screen access.

The shell itself is new: the command interpreter, the builtins, a vi front
end, and the launch path.

### 2.1 What a typical program looks like

The emulators are the programs the shell exists to run and configure:

| | pico-atom | pico-jupiter-ace | pico-oric |
|---|---|---|---|
| Release UF2 | 315 KB | 295 KB | 360 KB |
| Code and data flashed (UF2 is 2× payload) | ~155 KB | ~145 KB | ~176 KB |
| Target | `pico2`, every RP2350 board | same | same |
| Writes internal flash | no (EL §8.7) | no | no |
| Its directory on the card | `/atom/` | `/ace/` | `/oric/` |
| Settings | `pico-atom.cfg` | `pico-ace.cfg` | `pico-oric.cfg` |

Every program follows the same conventions (EL §8.6, §8.7):

- Files live under one directory per program, so programs can share a card.
- Settings are `key = value` text with `#` comments. Each is a few hundred
  bytes, LF or CRLF, and the program edits it in place.
- Writes go to `name.new`, then the old file is unlinked and the new one
  renamed into place. A missing file falls back to an existing `.new`.
- Nothing is written to flash, so running a program never costs another
  program its data.
- A plain `watchdog_reboot()` restarts the program itself. pico-atom does
  this to change the host clock. Under the loader that is still correct,
  because a reboot with no command goes back to the app (§3.1).

The shell follows the same rules for its own files, and vi is built for
files of this shape.

### 2.2 Costs

| Operation | Estimate | Basis |
|---|---|---|
| Shell start (stage3 loads `BOOT2350.UF2` into SRAM) | ~0.5 s | ~300 KB UF2 at ~0.9 MB/s, plus a cold mount of ~0.2 s (HW §7.1) |
| Run a program for the first time (pico-oric) | ~3 s | read 360 KB ~0.4 s, erase 44 sectors at ~45 ms each, program 700 pages at ~0.7 ms each; flash-chip datasheet figures, not measured |
| Run the same program again | ~0.5 s | differential flash: one read of the UF2 and no writes (§3.3) |
| Exit back to the shell | ~0.5 s | the same as shell start |

All four figures are **estimates**. M2 measures them (EL §14.4).

## 3. Where the shell sits in the boot chain

### 3.1 The loader today (v2.5)

```
power on
  └─ stage3 (8 KB, start of flash; its own partition on RP2350)
       ├─ no key held, no command ──────────────► app in flash (XIP)
       ├─ Down/F3 held ─────────────────────────► BOOTSEL
       ├─ Up/F1/F5 held, or command BOOT_SD ────► load /BOOT2350.UF2 into SRAM, run it
       ├─ command BOOT_RAM, arg = 8.3 path ─────► load that UF2 into SRAM, run it
       └─ the UF2 to load is missing or bad ────► BOOTSEL
```

Commands go through watchdog scratch registers 0–2. `scratch[0]` holds the
magic `0xe98cc638`, `[1]` the mode and `[2]` the argument
(`common/bootloader/proginfo.c`). They survive a watchdog reboot but not a
power cycle. stage3 clears the magic once it has read the command.

The loader's menu (`ui/`) is a `no_flash` binary. To run a flash UF2, it
calls `load_application_from_uf2()` (`ui/uf2.c`). That function erases and
programs the app partition one 256-byte page at a time; then the menu calls
`watchdog_reboot()` and stage3 jumps to the new app. A RAM UF2 is handed to
stage3 with `BOOT_RAM` and its 8.3 path, because stage3's Petit FatFs cannot
read long names.

### 3.2 The shell is `BOOT2350.UF2`

*Settled 2026-10-12.* The shell is built `no_flash` and installed on the
card as `/BOOT2350.UF2`. The loader's own menu is kept as
`/BOOT2350.ORG`, and `menu` runs it as a RAM program.

Consequences:

- The shell never lives in flash, so it can rewrite the whole app partition
  while it runs. The loader's menu relies on the same property.
- Installing or upgrading the shell means copying one file to the card. The
  loader in flash is not touched.
- Shell code runs from SRAM, with no XIP cache misses (HW §9.2).
- Before reading flash, the shell maps the app partition to `0x10000000`
  (`bl_remap_flash()`), as the menu does.

### 3.3 Running a program

`pico-oric args…` →

1. **Resolve.** A builtin wins. Otherwise search `$PATH` (default `/pico2-apps`)
   for `pico-oric.uf2`. A word with a `/` in it is a path.
2. **Check before touching flash.** Read the first block. Refuse a family
   ID that is not RP2350, and refuse a program that ends past
   `bl_info_get_flash_end()`. The error is "not for this device", with
   flash untouched.
3. **RAM program?** If the target addresses are in SRAM, hand off to stage3
   with `BOOT_RAM` and the 8.3 path, as the menu does.
4. **Differential flash.** Read the UF2 one 4 KB sector (16 blocks) at a
   time and compare it with flash. Erase and program only the sectors that
   differ. Running the same program again then costs one read of the file;
   switching programs costs what the loader costs today. Flash wear goes down
   as well.
5. **Reboot.** stage3 sees no key and no command, and jumps to the
   program.

The flashing code is the loader's `ui/uf2.c` with the comparison added
(§9). The loader's own error handling, such as the RP2350-E10 dummy block
and non-contiguous UF2s, comes with it.

### 3.4 Coming back to the shell

*Settled 2026-10-12: programs may add an exit command.* There are two ways
back:

- **Hold Up/F1/F5 at power-on.** This is the loader's existing gesture,
  and it works for every program. Power-on with no key held starts the last
  program. *Settled 2026-10-12:* that stays as it is, with no stage3 change.
- **An exit command in the program.** The shell publishes one small
  header, `include/picocalc_shell.h`:

  ```c
  // Return to the shell: stage3 loads /BOOT2350.UF2 instead of the app.
  // Returns false, and does nothing, if there is no card to load it from.
  bool picocalc_shell_exit(void);
  ```

  It does three things:
  1. Checks the card is present (GP22). Without a card, stage3 would drop to
     BOOTSEL (§3.1), and the PicoCalc would look dead.
  2. Sets `BOOT_SD` in the scratch registers.
  3. Calls `watchdog_reboot(0, 0, 0)`.

  The program must finish its own card writes first. In the emulators, that
  is the point where the guest is parked (EL §2.5).

For the emulators, *Exit to shell* belongs in the shared menu (EL §10), as
one item placed the same way in all three.

### 3.5 Arguments

stage3 passes nothing to the app. `pico-oric game.tap` needs a channel.
The candidate is a file, `/.shell/args`, which the shell writes before the
reboot and the program reads and deletes. Its content would be `cwd` and
`argv`, and an emulator would treat it like a build-time `BOOT_TAPE` (EL
§13.1). A block in SRAM does not survive the program's crt0. Exit status
could come back the same way. *Deferred, 2026-10-12.* Until then, a word
after a program's name is an error, so nothing comes to depend on a
behaviour we have not designed. A program runs exactly as it does from the
loader's menu.

### 3.6 Programs that keep data in flash

Pico Logo keeps a LittleFS filesystem in the app partition, after its code.
Running any other program reuses that flash, so the filesystem may be
partly overwritten. The shell cannot tell that such data exists. This is
documented in the README: a program meant to be run from the shell keeps
its data on the card. Pico Logo is the known exception.

## 4. The shell

### 4.1 Terminal

The terminal is a character grid on the 320×320 panel. *Settled
2026-10-12:* it uses the owner's own fonts, from `references/fonts/`. Both
are 10 pixels high, cover the 7-bit codes `0x00`–`0x7F` (128 glyphs), and store one byte per glyph
row:

| Font | Grid | Character |
|---|---|---|
| `font_8x10` | **40 × 32** | modelled on the VT100/VT220 font, with a round `0` and a square `O`; the default |
| `font_5x10` | **64 × 32** | the 8×10 face narrowed by the owner to 5 pixels; for wide listings |

`font` switches between them for the session, and `/etc/profile` can set
one. The files build cleanly with the host compiler under `-std=c11 -Wall
-Wextra -Werror`. They use two GCC/Clang extensions, binary literals and an
initialised flexible array member, so `-Wpedantic` stays off (checked
2026-10-12).

**The emulator.** The terminal is picocalc-text-starter's `display.c`, a
VT100/ANSI state machine that already handles:

- cursor motion, absolute positioning, save and restore;
- erase in line and in display;
- scroll up and down, scroll regions (`DECSTBM`), index and reverse index;
- tab stops;
- SGR bold, dim, underline and reverse;
- ANSI 8/16 colours, the xterm 256-colour palette, and 24-bit colour
  reduced to RGB565;
- the UK, ASCII and DEC special graphics character sets (`ESC ( 0`). The
  fonts' glyphs `0x00`–`0x1F` are the special graphics set, so line
  drawing works with no further check;
- cursor on and off (`DECTCEM`), and 64-column mode (private mode 4264),
  which is how `font` switches faces;
- device status reports, and BEL through a callback.

A phosphor preset (white, green or amber, from `display.h`) sets the default
foreground; `/etc/profile` chooses one. An unknown escape sequence and a
cancelled one draw glyph `0x02`, the checkerboard, as the starter does. A
byte of `0x80` or above has no glyph. Files from a computer may hold UTF-8
or Latin-1, so such a byte draws the same checkerboard, never a glyph from
past the table's end. vi shows the byte's value in its status line.

**One change to the starter's structure.** It draws straight into the
panel's frame memory and keeps no copy of the screen. The shell splits it
in two:

- `src/core/term.c` holds the parser and a **cell grid**. Each cell holds a
  character, its attributes and its colours, and rows that change are
  marked dirty (EL §5.3).
- `src/port/` draws the dirty rows.

*As built in M0 (2026-10-12).* The contract between them is in
`term.h`: the grid counts whole-screen scrolls, and its dirty mask moves
with each one. The renderer (`src/port/screen.c`) applies the count as a
hardware scroll, then draws each dirty row from its first changed cell
to its last, by comparing it with a shadow of what frame memory holds.
The shadow is indexed by frame-memory row, so a scroll moves nothing in
it. A scroll inside a `DECSTBM` region is done by redrawing the region's
rows. The scroll band is all 480 rows of frame memory (`VSCRDEF` 0/480/0,
as Pico Logo sets it; the starter set a 320-row band and wrapped at 480).
`term.c`'s header lists its small departures from the starter's
`display.c`: the region scrolls, the VT100 defaults for a missing count,
tab stops, and the checkerboard for unknown sequences.

The cell grid is what makes the terminal testable on the host, and lets the
development build dump the screen as text over the UART (EL §11.2). It also
lets vi and `more` redraw without re-sending their output. On the host, the
`shell` binary can skip the grid and pass the bytes straight to the macOS
terminal, which understands the same sequences.

**Where the hardware notes override the starter.** The starter is older
than the hardware notes, and the notes win:

| Starter | Shell | Why |
|---|---|---|
| keyboard polled from a repeating timer IRQ every 100 ms | polled from the main loop at about 30 Hz, draining the whole FIFO, the I²C bus guarded by a flag | HW §6.1 |
| key events taken as they come | ignore a release with no press; accept Caps Lock (`0xc1`); auto-repeat arrives as more presses | HW §6.2 |
| LCD SPI at 75 MHz | started at 25 MHz and reports the configured rate; 75 MHz by default since M0 measured both (*settled 2026-10-12*), with `-DPICO_SHELL_LCD_SPI_HZ=25000000` as the fallback | HW §4.2, §4.7 |
| its own `lcd.c` blit | the emulators' newer `lcd.c` underneath; the starter's glyph drawing, cursor and scroll layered on it | HW §4.3, §4.6 |
| hardware scroll offset applied in several places | every draw remaps y through the scroll offset in one `blit_begin`; the terminal is the scroll register's only user | HW §4.8 |
| its own `sdcard.c` and `fat32.c` | the emulators' `sd.c` and FatFs | HW §7.1; the starter's FAT32 is buggy |

Key codes come from the starter's `keyboard.h`, checked against HW §6.2.

Keys come from the southbridge FIFO. In the development build, UART1
mirrors the screen and its received bytes are typed as keys (EL §13.1,
§13.2), so a scripted run needs nobody at the keyboard. Two bytes are not
keys: FS (`0x1C`) dumps the grid as text, and GS (`0x1D`) logs the start
time and the counters (`tools/uart-screen.sh`). The mirror writes to the
UART as the output happens, so a long output in the development build
runs at 115,200 baud; measure display speed in the release build.

### 4.2 Filesystem

- **FatFs R0.15**, with the emulators' `ffconf.h` (`src/fatfs/ffconf.h`)
  plus `FF_USE_CHMOD` and `FF_FS_LOCK`, so a file open twice, or removed
  while open, is refused rather than corrupted. FAT12, FAT16 and FAT32 are
  all supported; exFAT stays off. The host build adds `FF_USE_MKFS` to
  format its test images.
- **The shell resolves paths itself** (`src/core/path.c`): it keeps the
  current directory as a string, resolves `.` and `..` against it, and
  gives FatFs absolute paths only (`FF_FS_RPATH = 0`). *Changed at M1*
  from `FF_FS_RPATH = 2`: FatFs forgets its current directory when it
  mounts a changed card, and the prompt would need a walk up the tree
  after every command. `cd` stores each name as the card spells it, so
  `cd DOCS` into `Docs` shows `/home/Docs>`.
- **One mount, while the card stays in.** The main loop polls card
  detect; any reading of "out" marks the disk uninitialised, and FatFs
  mounts afresh at the next command that touches it (HW §7.1's bounce).
  No file or directory stays open between commands.
- **The SD card driver** is the emulators' `sd.c` and `diskio.c` (HW §7.1).
  Every wait is bounded below the southbridge's 2.5 s watchdog.
- **UNIX paths on FAT.** `/` is the card root and the separator. Names are
  case-insensitive but keep their case, as FAT has them, and the
  documentation says so. `ls` and globs hide names starting with `.` or
  `._` (macOS AppleDouble, HW §7.1), and names with FAT's hidden or
  system attribute, such as Windows's `System Volume Information`, unless
  `-a` is given (*settled 2026-10-12*). A hidden name given as an operand
  is shown.
- **No clock** (HW §1.2). `FF_FS_NORTC`, as in the emulators, gives files a
  fixed timestamp. `date` may set a time for the session later.
- **Safe writes** (EL §8.6). `cp`, `>`, `>>`, and vi's `:w` write
  `name.new`, unlink `name`, then rename; `>>` copies the old file into
  `name.new` first. A write that fails or is stopped removes `name.new`
  and leaves `name` as it was. Shell files that are read at startup fall
  back to a whole `.new`. `mv` is a rename: one volume, so it never
  copies.
- **USB disk.** `usbdisk` unmounts FatFs and offers the card over USB MSC,
  as the loader's menu does. Ejecting on the host returns to the shell.

### 4.3 The card

```
/BOOT2350.UF2         the shell
/BOOT2350.ORG         the loader's own menu, run with `menu`
/pico2-apps/*.uf2     programs, run by name; the loader's menu lists the same files
/etc/profile          run at startup
/home/                $HOME, the starting directory
/.shell/              history
/atom/ /ace/ /oric/   the emulators' own directories, untouched
```

*Settled 2026-10-12:* programs live in `/pico2-apps/`, the directory the
loader's menu already uses, so a card works the same with either. A program
copied there shows up in both, and `menu` and the shell never disagree
about what is installed. `$PATH` can add further directories, but the
default is this one. Any file with the wrong family is caught by the check
in §3.3 before flash is touched.

### 4.4 Command language

This is deliberately small: UNIX spelling with a 1980s micro's ambitions.

- **The prompt** is the current directory followed by `>`, with no space and
  no abbreviation: `/>`, `/home>`, `/oric/keymaps>` (*settled 2026-10-12*).
  A path too long for the row wraps like any other text.

- **Words**, with `'…'` and `"…"` quoting and `\` escapes.
- **Globs** `*`, `?` and `[…]`, expanded by the shell.
- **Variables:** `$NAME`, `$?`, `set`, `unset` and `export`, for the
  session only.
- **Redirection:** `>`, `>>` and `<`.
- **Pipes `a | b`.** All builtins run in one process, so `a` runs to
  completion into a buffer, spilling to a temporary file when it is large,
  and `b` then reads it. This is enough for `ls | more` and `cat f | grep x`.
- `;`, `&&` and `||`. There is no `if`, `for` or functions in v1.
- **Line editing** with history and Tab completion of commands and paths.
- **Scripts:** `/etc/profile` runs at startup, and `sh file` runs a script.

*As built at M1* (`src/core/parse.c`): words, quoting and escapes, `#`
comments, `;`, `&&`, `||` and the three redirections. The whole line is
parsed before any of it runs, so a mistake at its end stops all of it, as
in sh. `|` is refused with a message until M4. Errors go to the terminal,
never to a redirection. Break, Esc or Ctrl-C stops a long command and drops
the rest of the line. Variables come with M4 (*settled 2026-10-12*);
until then `$` is an ordinary character.

### 4.5 Builtins

Everything that touches files is a builtin, because running anything else
costs a flash and a reboot.

| v1 | Later, if wanted |
|---|---|
| `ls [-l -a]` `cd` `pwd` `cat` `more` | `less` `find` `du` |
| `cp [-r]` `mv` `rm [-r]` `mkdir [-p]` `rmdir` `touch` | `chmod` (FAT read-only bit) |
| `echo` `head` `tail` `wc` `grep` | `sort` `uniq` `diff` `hexdump` |
| `df` | `fsck`-lite (check only) |
| `vi` `view` | |
| `help` `history` `clear` `set` `export` `font` `phosphor` | `alias` `date` |
| `battery` `backlight` `poweroff` (southbridge; HW §6) | |
| `info` (shell version, board, chip, flash, loader version, card, battery) `reboot` `bootsel` `menu` `usbdisk` | `which` `uf2info` (name, version, size from binary info) |

`displaytest` is a development command, not a builtin: the starter's
display stress test (M0), which reports rows and characters per second.
Break, Esc or Ctrl-C stops it.

### 4.6 vi

Pico Logo's vi key layer is pure functions of (buffer, cursor, key), with
host tests: `editor_vi.c`, `editor_pattern.c`, `editor_undo.c`,
`editor_lines.c` and `editor_search.c`. It is lifted largely as is. The
rest of Pico Logo's editor (`editor.c`, with its screen and Logo syntax
highlighting) is not used; a new front end draws on the §4.1 terminal.

Its scope is set by the files it is for, which are small text files:

- **Commands:** normal, insert and visual modes; motions; `f` `t` `%`; text
  objects; `/` `?` `n`; `:s`; `u` / `Ctrl-R`; `:w` `:q` `:wq` `:q!` `ZZ`;
  `Esc` and `Brk` as in Pico Logo.
- **Line endings.** The file's line ending, LF or CRLF, is detected and
  written back (EL §8.7).
- **Size.** The whole file is held in one buffer of 64 KB. A larger file is
  refused, never truncated.
- **Read-only.** `view` (or `vi -R`), and files with the FAT read-only
  attribute, open read-only.
- **Saving** goes through `name.new` and a rename, as in §4.2.

## 5. Architecture

The emulators' split applies (EL §2.1):

| Layer | Contains | Rules |
|---|---|---|
| **core** | parser, expansion, builtins, line editor, terminal cell grid and VT100 decoding, vi key layer and buffer, UF2 block parsing and sector comparison | portable C, no SDK header, all capacities in `src/core/config.h`; files through FatFs's API only |
| **port** | LCD, southbridge and keys, SD and `diskio.c`, flash erase/program, loader protocol, USB MSC, UART, `main.c` | the only place SDK headers appear |

FatFs is portable C, so the builtins run unchanged on the host against a
FAT image file. All knowledge of the loader's private protocol (scratch
commands, the partition remap and the flash end) lives in
`src/port/loader.c`.

The shell runs on one core. It polls the keyboard from its own loop, and
nothing else in it has a deadline. Core 1 stays idle until there is a reason
for it.

**What comes from where.** Where the sources differ, the hardware notes
decide (§4.1).

| From | File | Use |
|---|---|---|
| emulators (pico-oric's port is the newest copy) | `sd.c`, `diskio.c`, `storage.c` | as is |
| | `lcd.c` | as is, under the terminal's renderer |
| | `southbridge.c`, `kbd.c` | as is, without the cross-core ring |
| | `board.c`, `log.c` | trimmed |
| | `fatfs/` | R0.15, `ffconf.h` as in §4.2 |
| | `CMakeLists.txt` shape, test header | host option, `-Wall -Wextra -Werror` |
| picocalc-text-starter | `display.c` | split into `src/core/term.c` (parser and cell grid) and a port renderer (§4.1) |
| | `lcd.c` | glyph drawing, cursor and hardware scroll only, rebuilt on the emulators' `lcd.c` |
| | `southbridge.c` | battery, backlights, power-off and reset, added to the emulators' copy |
| | `keyboard.h` | the key code table |
| | `font-8x10.c`, `font-5x10.c` | from `references/fonts/`, the newer copies |
| Pico Logo | `editor_vi.c` and its four companions | §4.6 |
| UF2 Loader | `ui/uf2.c`, `proginfo.c` | §3.3 |

## 6. Build and test

- **Layout:** `src/core/`, `src/port/`, `src/host/` (the host's disk
  and shell), `src/fatfs/ffconf.h`, `include/picocalc_shell.h`,
  `test/host/`, `tools/`, `docs/`, plus `references/` (read-only).
- **Targets:** one CMake tree with two targets, as in the emulators. The
  default is the firmware: `PICO_BOARD=pico2`, `no_flash`, with output named
  `BOOT2350.UF2`. `-DPICO_SHELL_HOST=ON` builds the core with the system
  compiler, with no Pico SDK. Both build under `-Wall -Wextra -Werror`.
- **Host shell:** a `shell` binary runs the real core in a macOS
  terminal, over a FAT image file. This is the main development loop for
  everything except drivers.
- **Tests:** no framework, as in EL §11.1: one binary per area under CTest,
  with exit code 77 for a skip. The builtins are tested against FAT16 and
  FAT32 images, the UF2 parser against the emulators' real UF2s, and vi
  against key sequences. *Changed at M1:* the images are formatted by
  FatFs's own `f_mkfs` in the host build, not by `mkfs.fat`, which is not
  installed on the development Mac; so the tests need no tool beyond the
  compiler. A card formatted by another tool is checked on the board.
  `src/host/host_disk.c` can fail every write after the next *n*, which is
  how the safe-write rule is tested at each sector of a copy.
- **Two builds** (EL §13.2). The development build has UART1 log and typed
  keys. The release build has no UART; `tools/swd-drive.py` types into it
  and reads its screen and counters over SWD.
- **Running without the card.** `tools/swd-ram.sh` loads a build into
  SRAM over SWD and has the bootrom start it (HW §2.7), so a change can be
  tried without copying it to the card and without writing flash. It
  skips stage3, so the boot chain is checked from the card.
- **Generated output:** `build/<variant>/` holds build trees, `out/` holds
  UART captures, and `test/suites/` holds fetched test data. None of these
  are committed (`.gitignore`).
- **Documents** (EL §14.1): this design, `hardware-notes.md`, a
  `milestones.md` log from M0 on, and `CLAUDE.md`.

## 7. Memory budget (estimates)

| Item | Bytes |
|---|---:|
| RAM image limit: 520 KB SRAM, minus stage3's 8 KB and the scratch banks | ~504 KB |
| Code: core, FatFs, drivers, TinyUSB MSC | ~120 KB |
| Terminal cells, 64 × 32 × 6 B (character, attributes, two RGB565 colours) | 12 KB |
| The renderer's shadow of frame memory, 64 × 48 × 6 B (§4.1) | 18 KB |
| Fonts, 2 × 128 glyphs × 10 rows | 2.5 KB |
| vi buffer and undo journal | 64 + 16 KB |
| Pipe buffer | 32 KB |
| FatFs (`FATFS`, 2 × `FIL`, LFN buffer) | ~3 KB |
| Flash sector buffer (16 UF2 blocks) | 8 KB |
| Stacks and slack | the rest, ~230 KB |

Comfortable. Print the memory use on every build (EL §2.1). **Measured at
M0** (link map, 2026-10-12, Pico SDK 2.3.1, Arm GCC 15.2, `pico2`
Release): the development build uses 72,884 B of RAM (41,648 B of code
and read-only data, 33,284 B of `.bss`), the release build 69,308 B.
**Measured at M1** (the same tools, 2026-10-12): development 140,908 B
(76,940 B of code and read-only data, 66,016 B of `.bss`), release
137,388 B. Of the growth, FatFs and the builtins are about 35 KB of code,
and `ls`'s sorting buffers (16 KB of names, 4 KB for 512 entries) and the 4 KB copy
buffer most of the `.bss`. The deepest call chain, `cp` down into FatFs's
`f_rename`, needs well under 2 KB of the 4 KB stack (`-fstack-usage`).

## 8. Risks

| Risk | Mitigation |
|---|---|
| A loader release changes the scratch commands, the partition layout or the flash end | Everything depends on one file, `loader.c`. The shell reads stage3's binary info at startup and refuses to flash on a version it does not know. Tell pelrun about the shell early. |
| Exit with no card: stage3 drops to BOOTSEL | `picocalc_shell_exit()` checks card detect first (§3.4). If the card is removed between the check and the reboot, power-cycling recovers. |
| A half-written program after a power loss mid-flash | stage3 cannot be touched: it is in its own partition. Hold Up at power-on, then run the program again. |
| Programs that keep data in flash | §3.6 |
| Card removed while the shell runs | Debounce card detect, and fail cleanly; a failed mount is retried on the next change (HW §7.1). |
| Running a script on the owner's own card | Test cards and directories are kept apart from real ones (EL §13.4). |

## 9. Licence

*Settled 2026-10-12:* **GPL-3.0**, the licence of the loader and of all
three emulators. Code taken from them is used as it is: the loader's
`ui/uf2.c` and `proginfo.c`, and the emulators' port drivers. Each copied
file keeps a header that names its origin, and `THIRD-PARTY.md` lists
them. Pico Logo's vi files and picocalc-text-starter's terminal are MIT, which is
compatible. Both fonts are
the owner's own.

Still to decide when M2 publishes `picocalc_shell.h`: whether to offer that
one header under MIT as well, so that a program under any licence can
include it.

## 10. Milestones

Each milestone ends with something that runs on the board, something that is
measured, and a *not checked* list (EL §14.3). The record of each, as built, goes
in [milestones.md](milestones.md).

- **M0 — Skeleton.** Both builds and the host test runner. The firmware
  starts as `BOOT2350.UF2` from the loader, and the terminal (§4.1) shows a
  banner and the `/>` prompt in both fonts. The starter's display test
  passes on the host against the cell grid and on the panel, and the UART
  mirror works. Measure shell start time and memory use.
- **M1 — Files.** FatFs over SD, the parser, and `ls cd pwd cat cp mv rm
  mkdir rmdir touch echo df` with redirection. Host tests over FAT16 and
  FAT32 images, and the host shell.
- **M2 — Run (gate).** PATH, the family check, differential flashing, the
  reboot into the program, `menu`, and `picocalc_shell.h`. Measure first and
  repeat run times against §2.2 with the three emulators. If the first-run
  time is far over the estimate, add 64 KB block erase before going on.
- **M3 — vi.** The lifted key layer, the front end, CRLF handling and safe
  save. Done when it has edited `/atom/pico-atom.cfg` on the board and
  pico-atom has read the result.
- **M4 — Comfort.** History, Tab completion, globs, pipes, variables
  (§4.4), `more`, `grep`, `/etc/profile` and `usbdisk`.
- **M5 — Exit everywhere.** *Exit to shell* in the emulators' shared menu.
