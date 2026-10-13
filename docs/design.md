# PicoCalc Shell — design

Status: **Draft 6, 2026-10-13.** Feasibility study and first design. M0
(the skeleton), M1 (files), M2 (run), M3 (vi) and M4 (comfort) are done
([milestones.md](milestones.md)).

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
| 2026-10-12 | `include/picocalc_shell.h` is offered under the MIT licence as well as the GPL-3.0. | 9 |

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

All four figures were **estimates**. *Measured at M2* (Plus 2 W, UF2 Loader
2.5, development build, 2026-10-13; [milestones.md](milestones.md)):

| Operation | Measured | Where the time goes |
|---|---|---|
| Run a program for the first time | pico-oric 3.0 s, pico-atom 2.95 s, pico-ace 2.7 s from Enter to the reboot, then 0.53 s to the program's `main` | check 0.26–0.33 s; write 2.4–3.0 s, about 61 ms a sector for erase, program and read-back, plus a second read of the file |
| Run the same program again | 0.33–0.40 s from Enter to the reboot, then 0.53 s to `main` | one read of the file and a compare; nothing written |
| Exit back to the shell | 1.35 s from `picocalc_shell_exit()` to the prompt | stage3's 0.5 s wait for a key, loading the 158 KB UF2, and the panel's start-up (0.43 s) |
| Reboot to a program's `main` | 0.53 s | stage3's 0.5 s wait for a key |

The estimates included the boot, so they compare with the totals: a first
run about 3.5 s to the program's `main` against ~3 s, about 15 % over; a
repeat about 0.9 s against ~0.5 s; the exit 1.35 s against ~0.5 s. The
repeat and the exit both pay stage3's half-second wait for a key, which
the estimates left out and the shell cannot shorten (§3.4: no stage3
change); the exit's other 0.35 s over is loading the shell and the panel's
start-up. The first run was not far over its estimate, so 64 KB block
erase was not added (§10).

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

*As built at M2* (`src/core/uf2.c`, `run.c`; `src/port/loader.c`):

- **Two passes.** The first reads the whole file and checks every block
  before flash is touched, building each 4 KB sector as the loader would
  leave it (pages the program does not fill are erased, and the loader's
  proginfo words cleared) and comparing it with flash. The second runs
  only if a sector differs: it reads the file again and erases and
  programs just those sectors, reading each back. So a first run reads
  the file twice; a repeat run reads it once and writes nothing.
- **Stricter than the loader** in three ways: blocks must ascend (the
  SDK writes them so); a block naming no family is not taken for RP2350;
  and any block out of sequence refuses the file rather than being
  skipped. Blocks for other chips are skipped, so a UF2 for several chips
  works.
- **No bootable image while writing.** The program's first sector is
  erased before any other and written last; in it the pages holding a
  picobin block's first word go last, highest first, so the IMAGE_DEF,
  the block loop's head, is the very last page written. stage3 cannot start a
  half-written program: `rom_chain_image` finds no image and stage3 loads
  `/BOOT2350.UF2` instead, so a write cut short comes back to the shell.
  This costs one erase and program of the first sector on any run that
  writes.
- **Lookup.** A builtin wins; a word with a `/` is a path; otherwise
  `name.uf2` in `/pico2-apps`, a fixed directory until `$PATH` comes with
  variables (M4). A name not found is "not found" whatever stopped the
  search, as in sh; a path says why. A program ends the line: the reboot
  drops whatever follows it. Arguments and redirections are refused
  (§3.5).
- **The loader.** `loader.c` reads stage3's version from its binary info
  and runs programs only under 2.3, 2.4, 2.4.1 and 2.5, the versions with this
  scratch-command protocol (2.2 had none). Under anything else, or with no
  partition table, or when stage3 did not start the shell (as over SWD),
  a program is refused before flash is touched, with the reason.
- **Ctrl-C** stops the check; nothing stops a write once begun.

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

*As built at M2:* the header is header-only, static inline functions over
the SDK, so a program needs nothing but the file; `src/port/loader.c`
checks with static asserts that it agrees with the loader's values.
`test/pico/exit_test.c` builds `exit-test.uf2`, a program that shows what
it does on the panel and calls `picocalc_shell_exit()` when a key is
pressed, so the card can be taken out first to see the call refuse. It
checked the round trip on the board.

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

  *As built at M4* (`src/port/usbdisk.c`): TinyUSB's MSC device, from the
  SDK, with the loader's descriptors, so the computer sees the same disk
  from either. Each SCSI read or write is one `sd.c` block; the card's
  size comes from its CSD (`sd_sectors`). The run ends when the computer
  ejects the card (START STOP UNIT with LoEj), when Break, Esc or Ctrl-C
  is pressed, or when the card is taken out; the keyboard is polled
  between USB tasks, which keeps the southbridge's I²C alive. macOS
  sends that command on eject, so ejecting there brings the shell back
  (the owner, 2026-10-13). After an eject the unit reports no medium
  until the shell disconnects. The rate is about 170 KB/s (a 30 MB file
  in about 3 minutes, by eye): each SCSI transfer is one 512-byte block,
  as in the loader. Esc
  before an eject may lose what the computer was writing, and the shell
  says so when it starts. The history is saved first, and FatFs mounts
  afresh after; a current directory no longer on the card becomes `/`.
  `usbdisk` is refused with a redirection or in a pipe, which would hold
  a file open across it.

### 4.3 The card

```
/BOOT2350.UF2         the shell
/BOOT2350.ORG         the loader's own menu, run with `menu`
/pico2-apps/*.uf2     programs, run by name; the loader's menu lists the same files
/etc/profile          run at startup
/home/                $HOME, the starting directory
/.shell/              history, and pipes too large for RAM while they run
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
in sh. Errors go to the terminal, never to a redirection. Break, Esc or
Ctrl-C stops a long command and drops the rest of the line.

*As built at M4:*

- **Expansion as each command runs** (`src/core/expand.c`). The parser
  keeps each word as typed, quotes and all; variables, quotes and globs
  are dealt with as the command runs, so `false; echo $?` and
  `cd x; ls *` see what the command before did. Up to `SHELL_ARGS_MAX`
  (128) words in `SHELL_ARGS_TEXT` (4 KB); more is refused, not cut.
- **Variables** (`src/core/vars.c`): `$NAME`, `${NAME}` and `$?`, outside
  single quotes; a `$` before anything else is itself, and an unset
  variable is empty. `NAME=value` as a whole command sets one (several
  may be given); `NAME=value cmd` is refused, since there is no
  environment to pass. `set` lists them, `unset` removes, and `export`
  sets or lists, and is otherwise the same as setting one. A value is
  never split into words or globbed. A word left empty with no quotes is
  dropped, as in sh. All of them share 1 KB (`SHELL_VARS_BYTES`). `HOME`
  (`/home`) is `cd`'s default and the starting directory, and `PATH`
  (`/pico2-apps`) is where programs are found: each directory in turn,
  `:` between them, a relative one from the current directory.
- **Globs** in a word's last name only: `*/x` stays as typed. The matches
  are sorted as `ls` sorts them, letters match without regard to case as
  FAT's do, and a pattern that matches nothing stays as typed. Hidden
  names (§4.2) match only a pattern that starts with `.`; `.` and `..`
  never do. Redirections expand variables but not globs.
- **Pipes** (`src/core/pipe.c`). Each command of `a | b | c` runs to
  completion into a pipe, and the next then reads it. A pipe takes one
  of two 16 KB RAM buffers (the 32 KB of §7) if one is free; beyond 16 KB,
  or with none free, it goes through `/.shell/pipeNN`, written in place
  (it is the shell's own file, not a user's) and removed when the pipe
  closes. A pipeline's status is its last command's, and `&&` and `||`
  take the whole pipeline. `a > f | b` and `a | b < f` are refused by the
  parser. A program, `menu` or `usbdisk` is refused in a pipe, and with
  any redirection here or at a level above, since leaving the shell would
  leave its file behind. `cat`, `more` and `grep` read a file, `<` or a
  pipe the same way (`src/core/input.c`); the keyboard is never a
  command's input.
- **Scripts.** `sh file` runs a file a line at a time; `/etc/profile`
  runs when the shell starts, before the history is shown and the first
  prompt. A script's output and redirections are those of the `sh`
  command. A mistake is reported with the file and line, and the script
  goes on; a stop ends the script and the line. Scripts nest
  `SHELL_DEPTH - 1` (3) deep, each level with its own parsed line,
  expanded words and redirections. Lines of up to `SHELL_LINE_MAX` (255);
  CR LF is accepted. There are no script arguments (`$1`), as programs
  have none (§3.5).
- **The line editor** (`src/core/line.c`): Left, Right, Home, End (and
  Ctrl-B, F, A, E), Backspace, Del, Ctrl-U and Ctrl-K, Ctrl-C, Up and
  Down (and Ctrl-P, N) for the history, and Tab. A line over several
  rows is redrawn with relative cursor moves, worked out from the
  prompt's width; the terminal wraps as soon as a row fills (§4.1). The
  prompt starts its own row when the output before it did not end one.
- **Tab** completes a command name (a builtin, or a program in `$PATH`
  without `.uf2`) as a command's first word, and a path anywhere else.
  One match goes in whole, with `/` after a directory and a space after
  anything else; several put in what they share, and a second Tab with
  nothing to add lists them in columns. Names are put in as the card
  spells them, with `\` before characters the shell would take as
  special. Up to `SHELL_COMPLETE_MAX` (128) names are offered.
- **History** (`src/core/history.c`): 2 KB of lines, the oldest dropped;
  a blank line, and one the same as the one before, are not kept. It is
  read from `/.shell/history` at startup (falling back to a whole
  `.new`, §4.2) and written through `name.new` when the shell is left for
  a program, the menu or `usbdisk`, and only if it changed: one card
  write a session rather than one a command. Lines typed since are lost
  if the power goes. `history` lists it.

### 4.5 Builtins

Everything that touches files is a builtin, because running anything else
costs a flash and a reboot.

*As built at M4:* `cat cd cp df displaytest echo export font grep history
ls menu mkdir more mv pwd rm rmdir set sh touch unset usbdisk vi view`.
Of the v1 column, `head tail wc help clear phosphor battery backlight
poweroff info reboot bootsel` are not built yet. `more` and `grep` are in
`src/core/text.c`:

- `more` shows `TERM_ROWS - 1` rows, a long line counting as the rows it
  wraps to, then `--More--` in reverse: Space for a page, Enter or Down
  for a row, q, Esc, Brk or Ctrl-C to stop. Writing to a file or a pipe
  it copies, as `cat` does.
- `grep [-v] [-n] [-c] [-i] pattern [file...]` uses vi's pattern matcher
  (`editor_pattern.c`, §4.6), so its patterns are vi's and it matches
  without regard to case, as `/` in vi does; `-i` is accepted and changes
  nothing. With several files each line starts with its file's name.
  Lines are read in pieces of `SHELL_TEXT_LINE - 1` (511) bytes, and a
  longer line is matched a piece at a time. The status is 0 if a line
  was found, 1 if none, 2 on an error.

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

*As built at M3* (`src/core/vi.c`; the key layer in `src/core/editor_*.c`):

- **What was lifted.** `editor_vi.c`, `editor_pattern.c`, `editor_undo.c`
  and `editor_lines.c`, changed only in their includes and in the page,
  which is `VI_TEXT_ROWS` (31) lines. Their capacities keep Pico Logo's
  names in `config.h`. `editor_search.c` is not used: the key layer
  searches with `editor_pattern.c`. Pico Logo's own tests for the four
  files run here unchanged, through `test/host/unity.h`, a shim with only
  the parts of Unity they use. Carrying out the key layer's actions
  (yank, paste, join, indent, open a line, and the rest) is adapted from
  `editor.c`'s `editor_vi_apply`, since the key layer only names byte
  ranges.
- **The key layer's own choices are kept.** `$` puts the cursor just past
  the line's last character (Pico Logo's "exclusive" end), so `$r` has
  nothing to replace; `$x` does nothing and `$a` appends. There is one
  mark, one register, and `:w`, `:wq`, `:x`, `:q`, `:q!`, `ZZ` and `ZQ`,
  but no `:w name` and no `:e`. `:wq` always writes, as in vi; `ZZ` and
  `:x` write only when there is a change, so `ZZ` on a new file makes
  nothing. Brk leaves at once from every mode, discarding changes.
- **Keys** come through a new `getkey` hook in `shell_hooks_t`, since vi
  blocks while it runs: on the board it polls the keyboard and the UART
  and blinks the cursor (the prompt's loop is now the same call), on the
  host it reads stdin, and in tests it types a script. The end of input
  leaves vi as Brk does. Esc (0x1B) is given to the key layer as its
  `KEY_ESC`.
- **Insert mode.** Enter carries the line's indentation down, as `o`
  does; Tab inserts a tab character; Backspace crosses line breaks; the
  arrows, Home, End and the page keys move.
- **Shifting.** `>` adds 4 spaces per step (`VI_SHIFT_WIDTH`) to every
  line that is not empty; `<` takes up to 4 columns of spaces or tabs, a
  tab counting as 4.
- **Line endings.** A file is CR LF when every line break in it is; the
  CRs come off on reading and go back on writing. A file that mixes the
  two is LF, and its CRs are characters, shown as `^M`. Whether the last
  line ends in a line break is kept too (`[noeol]`). An empty file stays
  empty; text typed into it gets a final line break.
- **Size.** The buffer is 64 KB with its NUL, so a file of up to 65,535
  bytes opens. A larger one is refused with its size. An edit that would
  take the buffer past that is refused and said.
- **Copy buffer and undo.** The register is 8 KB (`VI_COPY_SIZE`); a yank
  larger than that is refused and said, and the register keeps what it
  had (Pico Logo's 1 KB copy buffer cut a yank short). A delete still
  happens, and `u` brings it back. The undo journal is 16 KB, as §7
  budgeted, and drops its oldest steps to make room.
- **The screen.** 31 rows of text and a status row. Lines do not wrap:
  the whole view scrolls sideways to keep the cursor in it. Tabs stop
  every 8 columns; a control character shows as `^X`, a byte of 0x80 or
  above as the terminal's checkerboard. Rows past the end show `~`. The
  visual selection is in reverse video. The status row holds the command
  line, a message (until the next key) or the mode, and the cursor's line
  and column; it never writes its last column, where the terminal would
  wrap and scroll. vi keeps a copy of what each row shows and sends only
  the rows that change, as escape sequences, so the UART mirror shows the
  same screen. Leaving clears the screen and prints what was written.
  The cursor is the terminal's underline in every mode; Pico Logo's block
  cursor for normal mode is not there.

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
for it; while it does, core 0's stack has scratch X as well as scratch Y
(§7).

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
| Pico Logo | `editor_vi.c`, `editor_pattern.c`, `editor_undo.c`, `editor_lines.c` | as is (§4.6) |
| | `editor.c` | its `editor_vi_apply`, adapted into `vi.c` (§4.6) |
| | `tests/test_editor_*.c` | as is, through `test/host/unity.h` |
| UF2 Loader | `ui/uf2.c`, `proginfo.c` | §3.3 |
| | `ui/lib/usb_msc/` (`usb_msc.c`, `usb_descriptors.c`, `tusb_config.h`) | `usbdisk` (§4.2), *as built at M4* |

## 6. Build and test

- **Layout:** `src/core/`, `src/port/`, `src/host/` (the host's disk
  and shell), `src/fatfs/ffconf.h`, `include/picocalc_shell.h`,
  `test/host/`, `test/pico/` (board-only programs, such as `exit-test`),
  `tools/`, `docs/`, plus `references/` (read-only).
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
  The real UF2s (the emulators' release builds and the loader's own) are
  copied into `test/suites/uf2/` by `tools/fetch-suites.sh`; `test_uf2`
  checks them where present and skips them where not.
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
| vi buffer and undo journal (*and, as built at M3, an 8 KB register*) | 64 + 16 KB |
| Pipe buffer (*as built at M4: two of 16 KB*) | 32 KB |
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
buffer most of the `.bss`. **Measured at M2** (the same tools,
2026-10-13, after the review): development 158,224 B, release 154,284 B; the UF2 code and
its buffers (a 4 KB sector, a block, a page and the 512-byte bitmap of
sectors that differ) and the bootrom's 4 KB work area for the partition
table are most of the growth. The deepest call chain, `cp` down into FatFs's
`f_rename`, needs well under 2 KB of the 4 KB stack (`-fstack-usage`).
**Measured at M3** (the same tools, 2026-10-13): development 290,628 B,
release 286,496 B. vi is about 35 KB of code (the key layer 21 KB, the
pattern matcher 3.6 KB, undo 1.8 KB, the front end 8.2 KB) and 94 KB of
`.bss`: the 64 KB buffer, the 16 KB journal, the 8 KB register, and 4 KB
for the copy of the screen.

**Measured at M4** (the same tools, 2026-10-13): development 410,004 B,
release 406,260 B. Of the 119 KB of growth (link map): the two pipe
buffers, 32 KB; four levels of parsed line and expanded words
(`s_frames`), 21 KB; the files each level and each pipe may hold open,
13 KB, as FatFs keeps a sector in each `FIL`; TinyUSB with its MSC class,
15 KB; `more` and `grep`'s buffers, 6 KB; history, completion and
variables, about 8 KB; and the rest of the new code. About 100 KB of the
504 KB is left for the stack and slack.

**The stack** (*corrected at M3*). Core 0's stack starts at the top of
scratch Y and may run down to `__StackLimit` at the bottom of scratch X:
8 KB, because core 1 never starts and nothing is linked into scratch X.
The 4 KB above was scratch Y alone. The deepest chain is now vi's `:s`
with a 32-byte pattern: the pattern matcher recurses once per atom, at
120 B a level, so about 4.0 KB for it and 1.1 KB for the shell and vi
above it, 5.0 KB in all (`-fstack-usage`, worked by hand). Starting core 1
would take scratch X for its own stack and leave 4 KB, which this chain
would overrun: the pattern limit (`LOGO_VI_TEXT_MAX`) would have to come
down first. *At M4* each level of `sh` adds about 232 B (`run_line`,
with the pipeline and command inlined, 144 B; `cmd_sh` 8 B; `script`
80 B), so vi's `:s` in a script three levels down is about 5.7 KB of the
8 KB. Tab completion is about 0.5 KB below the prompt.

## 8. Risks

| Risk | Mitigation |
|---|---|
| A loader release changes the scratch commands, the partition layout or the flash end | Everything depends on one file, `loader.c`. The shell reads stage3's binary info at startup and refuses to flash on a version it does not know: 2.3, 2.4, 2.4.1 and 2.5 (*as built at M2*). Tell pelrun about the shell early. |
| Exit with no card: stage3 drops to BOOTSEL | `picocalc_shell_exit()` checks card detect first (§3.4). If the card is removed between the check and the reboot, power-cycling recovers. |
| A half-written program after a power loss mid-flash | stage3 cannot be touched: it is in its own partition. *As built at M2:* the program's first sector is written last (§3.3), so stage3 finds no image and loads the shell; run the program again. A reset mid-write was checked on the Plus 2 W, and the owner cut the power mid-write on a Pico 2 W: both came back to the shell. |
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

*Settled 2026-10-12:* `include/picocalc_shell.h` is offered under the MIT
licence as well (`GPL-3.0-or-later OR MIT`), so that a program under any
licence can include it.

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
