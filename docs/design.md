# PicoCalc Shell — design

Status: **Draft 11, 2026-10-18.** Feasibility study and first design. M0
(the skeleton), M1 (files), M2 (run), M3 (vi), M4 (comfort), M6 (USB
speed), M8 (the rest of v1) and M9 (the DOS flavour) are done
([milestones.md](milestones.md)). There is no M5: *Exit to shell* in the
emulators is done in their own projects (§3.4). The commands are explained
for users in [user-guide-unix.md](user-guide-unix.md), and the DOS flavour's in
[user-guide-dos.md](user-guide-dos.md).

The model programs are the owner's emulators (pico-atom, pico-jupiter-ace
and pico-oric). Pico Logo is a special case, because it keeps a filesystem
in the PicoCalc's own flash (§3.6).

The owner's decisions so far are listed below. Do not reopen them. The
only open questions are M10's, a flash filesystem for programs to share
(§3.7).

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
| 2026-10-13 | A patched stage3 that starts the shell at power-on, and the last program with Up/F1/F5 held, is an optional milestone (M7). Until it is installed, the stock loader's behaviour above stands. | 3.4, 10 |
| 2026-10-14 | A second flavour, built from the same tree, feels like DOS and has an EDIT modelled on MS-DOS 5's, in place of vi. It is a separate `BOOT2350.UF2`; the user copies the one they want to the card, and neither flavour installs the other. The card is `C:`. EDIT covers the core look and keys, not all of MS-DOS 5's. It is never called MS-DOS. | 4.7, 10 |
| 2026-10-14 | The DOS flavour's 40-column font is IBM's PC BIOS 8x8, from VileR's Ultimate Oldschool PC Font Pack (CC BY-SA 4.0), drawn in the 8x10 cell so the grid stays 40 x 32. The UNIX flavour keeps the owner's VT100-like font, and both keep the owner's 5x10 for 64 columns. This narrows the 2026-10-12 decision on fonts for the DOS flavour only. | 4.1, 4.7 |
| 2026-10-14 | EDIT selects with Alt and the arrows, and cuts, copies and pastes with Ctrl+X, Ctrl+C and Ctrl+V: the keyboard cannot send Shift with the arrows, and Shift+Del is End (HW §6.3). | 4.7 |
| 2026-10-15 | The DOS flavour has only 40 columns, always in IBM's font: no `FONT` command and no 64-column mode, and EDIT runs in 40 columns, its boxes in IBM's line glyphs. This narrows the 2026-10-14 font decision: the 5x10 font is the UNIX flavour's alone. | 4.1, 4.7 |
| 2026-10-15 | The UNIX flavour's `ls` puts `/` after a directory's name, as `ls -F` does, in columns, one per line and `-l`. | 4.2 |
| 2026-10-16 | A release offers the two flavours' release builds as `shell-unix.uf2` and `shell-dos.uf2`; the user renames the one they want to `BOOT2350.UF2` on the card. | 4.7 |
| 2026-10-18 | The UNIX flavour has only 40 columns too, in the Master 512's font (`font-master-512-8x10.c`), arranged as the terminal's 128 glyphs with the DEC graphics at `0x00`–`0x1F`, as IBM's is. There is no `font` command and no 64-column mode in either flavour, and the owner's 8x10 and 5x10 are no longer built in. This replaces the 2026-10-12 and 2026-10-14 decisions on the UNIX flavour's fonts. | 4.1 |
| 2026-10-17 | `picocalc_shell_exit()` returns false when UF2 Loader did not start the program, as well as with no card, and the program decides what to do then (such as power off by its own means). The header stays independent and does not touch the southbridge; `picocalc_shell_loader_present()` tells a program beforehand. | 3.4 |

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
  *Decided 2026-10-13:* the optional M7 swaps this with a patched stage3
  (§10), for an owner who installs it.
- **An exit command in the program.** The shell publishes one small
  header, `include/picocalc_shell.h`:

  ```c
  // Return to the shell: stage3 loads /BOOT2350.UF2 instead of the app.
  // Returns false, and does nothing, if UF2 Loader did not start the
  // program or there is no card to load it from.
  bool picocalc_shell_exit(void);
  ```

  It does three things:
  1. Checks UF2 Loader started the program (below, *Decided 2026-10-17*),
     and the card is present (GP22). Without a card, stage3 would drop to
     BOOTSEL (§3.1), and the PicoCalc would look dead.
  2. Sets `BOOT_SD` in the scratch registers.
  3. Calls `watchdog_reboot(0, 0, 0)`.

  The program must finish its own card writes first. In the emulators, that
  is the point where the guest is parked (EL §2.5).

For the emulators, *Exit to shell* belongs in the shared menu (EL §10), as
one item placed the same way in all three. That work is done in the
emulators' own projects, not here (it was M5, removed from this plan).

*As built at M2:* the header is header-only, static inline functions over
the SDK, so a program needs nothing but the file; `src/port/loader.c`
checks with static asserts that it agrees with the loader's values.
`test/pico/exit_test.c` builds `exit-test.uf2`, a program that shows what
it does on the panel and calls `picocalc_shell_exit()` when a key is
pressed, so the card can be taken out first to see the call refuse. It
checked the round trip on the board.

*Decided 2026-10-17:* `picocalc_shell_exit()` also returns false, and does
nothing, if UF2 Loader did not start the program, as when it was copied on
in BOOTSEL; the program can then do something else, such as power off
through its own southbridge driver. The header stays independent: it does
not touch i2c1, which the program's keyboard driver owns.
`picocalc_shell_loader_present()` tells the program beforehand. On RP2350
the loader leaves no mark in the program (the proginfo words are
RP2040-only, and both it and the shell erase them), but stage3 starts it
with `rom_chain_image` from the app partition, which lies after stage3 in
flash, so the boot ROM maps a nonzero offset at `0x10000000` (QMI
`ATRANS0`, HW §7.2); a program copied on in BOOTSEL runs from offset 0.
The loader's own notes for programs (`proginfo.c`) read the same register.
It shows a partition boot, not stage3 by name; a false yes is harmless,
since without stage3 the `BOOT_SD` command is ignored and the reboot
starts the program again. `exit-test` says on the panel whether the
loader started it.

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
its data on the card. Pico Logo is the known exception. M10 would give
such programs a partition of their own (§3.7).

### 3.7 A flash filesystem for programs to share (M10)

*Proposed 2026-10-17; the questions at the end are open.* An optional
littlefs filesystem in its own flash partition. The shell creates, sizes
and formats it; any program may mount it if it is there. Programs get
storage that works without a card, and Pico Logo a place that running
other programs does not overwrite.

**Where it lives.** A second partition in UF2 Loader's partition table,
between stage3 and App:

```
0         8 KB                       8 KB + fs size                 flash end
| stage3  | filesystem (blocks 0 →)  | App (the program in flash)    |
            fixed start               this boundary moves
```

- The table is stage3's own: a PARTITION_TABLE block after its
  IMAGE_DEF, in its second 4 KB sector. In v2.2's `bootloader_pico2.uf2`
  picotool shows it at `0x1a68`–`0x1ac8`, neither hashed nor signed. App
  stays entry 0, because stage3, the menu and `loader.c` all look up
  partition 0; the filesystem is entry 1.
- Nothing in the loader assumes App starts at 8 KB. stage3 chains
  partition 0 from its start (`stage3/stage3.c`) and works out the flash
  end from it (`stage3/uf2.c`); the menu and the shell map App to
  `0x10000000` from any sector (`bl_remap_flash`). Programs run through
  that mapping as now, and `picocalc_shell_loader_present()` still sees
  a nonzero offset (§3.4).
- The partition is not bootable (no `arm_boot` or `riscv_boot`), has a
  fixed id and name, and accepts only the UF2 family `data`. A littlefs
  image wrapped as a UF2 can then be copied on in BOOTSEL; a program
  cannot.
- *Why a partition, not a region taken from App as Pico Logo's is:* the
  boot ROM enforces it. The shell writes flash through `rom_flash_op`
  with the table's permissions (`src/port/loader.c`), and the menu maps
  only App's size, so neither can write a program over the filesystem.
  Programs find it with `rom_load_partition_table` and
  `rom_get_partition_table_info`, by id, and picotool can show and load
  it. Pico Logo takes the top of flash only because nothing else existed.

**Writing the table.** This is the one exception to "never write flash
outside the app partition". The shell rewrites stage3's table block, in
`loader.c`, only for loader versions it knows: it reads the sector,
replaces the block, keeps the block loop's back-pointer correct, writes
the sector back and reads it again. One sector is erased and programmed.
A power cut in that window leaves no image at flash 0. The boot ROM then
starts BOOTSEL, and reinstalling UF2 Loader from a computer recovers; the
card and the filesystem's data are untouched. Reinstalling or upgrading
UF2 Loader writes its own one-partition table back. The shell then finds
the littlefs superblock at 8 KB and offers to restore the entry, before a
program run overwrites it.

**Resizing.** The filesystem's start never moves, so resizing moves only
the boundary. Growing is `lfs_fs_grow`, and keeps every file. Shrinking
is `lfs_fs_grow` built with `LFS_SHRINKNONRELOCATING`, and works only
when no block past the new end is in use. Every resize moves App's
start, so the program in flash is lost. The shell says so and asks
first, then erases App's new first sector, so that power-on finds no
image and starts the shell (§3.3); the next run writes the whole
program. The steps run in an order that keeps the superblock's block
count no larger than the partition:

- growing: erase App's new first sector, rewrite the table, then grow;
- shrinking: shrink, rewrite the table, then erase App's new first sector.

A power cut between steps leaves a filesystem that mounts, and the shell
finishes the resize on its next start. Removing the filesystem gives App
its 8 KB start back.

**What programs agree to.** These rules belong to a new header,
`include/picocalc_flashfs.h`, header-only like `picocalc_shell.h`. It
finds the partition and provides littlefs's read, program and erase
callbacks; the program brings its own littlefs.

- No partition with the id means no filesystem.
- littlefs's on-disk version 2.1, 4 KB blocks, mounted with
  `block_count` 0 so that the superblock gives the size, and the default
  `name_max`, `file_max` and `attr_max`. A client set smaller fails to
  mount. `read_size` and `prog_size` are each client's own.
- A program never formats or resizes. A failed mount means there is no
  filesystem.
- One directory per program, `/<program>/`, as on the card (EL §8.6,
  §8.7).
- Each erase and program runs with interrupts off. On a Plus 2 W using
  PSRAM, the XIP cache is cleaned first and invalidated after, as in
  Pico Logo's `devices/picocalc/picocalc_flash.c`.

**In each flavour.**

| | UNIX | DOS |
|---|---|---|
| Name | mounted at `/flash` | drive `D:`, as RAMDRIVE's was |
| Create or resize | `mkfs /flash SIZE`, which asks before erasing; `resize /flash SIZE`, which keeps the files | `FDISK`: a small 40-column menu to create, resize or delete |
| Format | `mkfs /flash` | `FORMAT D:` |
| Remove | `mkfs /flash 0` | `FDISK`, then delete |
| Space | `df`, card and flash | `DIR`'s bytes free, `CHKDSK D:` |
| Use | `cp`, `ls`, `vi` and the rest, across `/` and `/flash` | `D:`, `COPY C:\X D:\`, EDIT |

Some limits follow from littlefs:

- It has no dates, so `ls -l` and `DIR` would show none, unless a date
  attribute is added to the rules above.
- Its names are case-sensitive. The DOS flavour would store names in
  capitals and look them up ignoring case.
- `usbdisk` cannot show it, because a computer sees only FAT. Files go
  in and out through the card.

*Estimates:* littlefs's code is about 20 KB, with 1–2 KB of buffers, in
each flavour. A layer that sends each path to FatFs or littlefs comes on
top. The UNIX flavour has about 69 KB free (504 KB less M9's 435 KB).

**Open questions** (the owner's):

1. The exception to "never write flash outside the app partition", for
   stage3's table block.
2. Scope: every command on `/flash` and `D:`, or first only create,
   resize, `df` and copying in and out, with the mount later.
3. Shrinking when a block past the new end is in use: refuse, or copy
   the files to the card and back.
4. Pico Logo: made a client, or the shell warns before running it.
   Today it would format over the filesystem, since it formats when its
   own mount fails.
5. The DOS flavour's letter: `D:`.
6. A date attribute in the rules for programs.
7. `picocalc_flashfs.h` under the MIT licence as well as the GPL-3.0, as
   `picocalc_shell.h` is (§9).

**To check before building** (RP2350 datasheet, picotool):

- that the boot ROM accepts a table whose entries are not in address
  order;
- whether the table can live outside stage3's sector, which would remove
  the power-cut window;
- that loader versions 2.3 to 2.5 lay out the table block as 2.2 does;
- that BOOTSEL writes a `data` UF2 only to the partition that accepts it.

## 4. The shell

### 4.1 Terminal

The terminal is a character grid of **40 × 32** on the 320×320 panel, in
an 8×10 font that is 10 pixels high, covers the 7-bit codes `0x00`–`0x7F`
(128 glyphs), and stores one byte per glyph row. *Changed 2026-10-18:* the
UNIX flavour's font is the Master 512's (`font_bbc_master_512`,
`font-master-512-8x10.c`), and the DOS flavour's is IBM's (§4.7). Each
flavour links only its own; there is no `font` command and no 64-column
mode. The owner's `font_8x10` and `font_5x10` (40 and 64 columns, from
`references/fonts/`), settled 2026-10-12, are no longer built in, but
glyphs from `font_8x10` fill the DEC graphics both fonts lack.

The Master 512's font came in code page 437 order, 256 glyphs. It was
arranged once, as `tools/mkfont-ibm.py` arranges IBM's: `0x20`–`0x7E` are
its ASCII; `0x00`–`0x1F` are the DEC special graphics, each code page 437
has taken from the font's own glyph, the box-drawing ones carried into
rows 0 and 9 so they join; the rest, and `0x7F`, are `font_8x10`'s. Code
page 437's upper half is dropped, since the terminal draws nothing above
`0x7F`. The files build cleanly with the host compiler under `-std=c11 -Wall
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
- cursor on and off (`DECTCEM`). The starter's 64-column mode (private
  mode 4264) is ignored *(changed 2026-10-18)*;
- device status reports, and BEL through a callback.

A phosphor preset (white, green or amber, from `display.h`) sets the default
foreground; `/etc/profile` chooses one. *Changed 2026-10-14:* the owner's
phosphors, white `RGB(216, 240, 255)`, green `RGB(51, 255, 51)` and amber
`RGB(255, 183, 0)`. The starter's `display.h` defines green and amber but
never draws with them, and its amber, `RGB(255, 255, 51)`, showed as the
yellow it is. An unknown escape sequence and a
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
§13.2), so a scripted run needs nobody at the keyboard. Three bytes are
not keys: FS (`0x1C`) dumps the grid as text, GS (`0x1D`) logs the start
time and the counters (`tools/uart-screen.sh`), and RS (`0x1E`) starts or
stops logging each key event as `key <state> <code>`, as the southbridge
sent it (*from M9*, to capture chords, HW §6.3). The mirror writes to the
UART as the output happens, so a long output in the development build
runs at 115,200 baud; measure display speed in the release build.

### 4.2 Filesystem

- **FatFs R0.15**, with the emulators' `ffconf.h` (`src/fatfs/ffconf.h`)
  plus `FF_USE_CHMOD` and `FF_FS_LOCK` (and, *from M9*, `FF_USE_LABEL`, for
  DOS's `VOL` and `DIR`), so a file open twice, or removed
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
  from either. The card's size comes from its CSD (`sd_sectors`). The
  run ends when the computer
  ejects the card (START STOP UNIT with LoEj), when Break, Esc or Ctrl-C
  is pressed, or when the card is taken out; the keyboard is polled
  between USB tasks, which keeps the southbridge's I²C alive. macOS
  sends that command on eject, so ejecting there brings the shell back
  (the owner, 2026-10-13). After an eject the unit reports no medium
  until the shell disconnects. At M4 each SCSI transfer moved one
  512-byte block at a time, as in the loader, and writing to the card ran
  at about 170 KB/s (a 30 MB file from the Mac in about 3 minutes, by
  eye).

  *As built at M6:* TinyUSB's MSC buffer is 16 KB (`SHELL_USB_BUF`), and
  each piece of a SCSI read or write, up to that size, is one
  multi-block transfer on the card: CMD18, stopped by CMD12, and CMD25,
  stopped by its stop token (`sd_read_blocks`, `sd_write_blocks`).
  FatFs's runs of sectors use the same calls (`diskio.c`). Each wait
  inside a transfer keeps its own bound, and the transfer as a whole,
  its command's ready wait included, is given 1 s from the call; past
  that it is stopped, and with the card's last busy and the stop the
  call returns within 2 s, below the southbridge's 2.5 s.
  The stop is sent after an error too, so a card that answers is never
  left mid-transfer. `test_sd` checks all of this against a model of a
  card in SPI mode. The USB pieces and the card's transfers still take
  turns: nothing overlaps them. On the Plus 2 W to a Mac, release build,
  writing runs at about 610 KB/s and reading at about 700 KB/s (M4: 219
  and 432 KB/s; milestones.md). Esc
  before an eject may lose what the computer was writing, and the shell
  says so when it starts. An iPad has no eject (the owner, 2026-10-14),
  so there Esc is the only way back: once the iPad has finished its
  copies. The history is saved first, and FatFs mounts
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
  environment to pass, and so is `NAME=value > f`, rather than ignoring
  the redirection. `set` lists them, `unset` removes, and `export`
  sets or lists, and is otherwise the same as setting one. A value is
  never split into words or globbed. A word left empty with no quotes is
  dropped, as in sh. All of them share 1 KB (`SHELL_VARS_BYTES`). `HOME`
  (`/home`) is `cd`'s default and the starting directory, and `PATH`
  (`/pico2-apps`) is where programs are found: each directory in turn,
  `:` between them, a relative one from the current directory, and an
  empty one (`a::b`, a `:` at either end) the current directory, as in sh.
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
  closes. If output is lost on the way (the card full or failing), the
  shell says so and stops the pipeline with status 1, rather than run
  the next command on part of it. Otherwise a pipeline's status is its
  last command's, and `&&` and `||`
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
*(`displaytest` went on 2026-10-15, `font` on 2026-10-18.)*
*As built at M8:* the rest of the v1 column, `head tail wc help clear
phosphor battery backlight poweroff info reboot bootsel`. `more`, `grep`,
`head`, `tail` and `wc` are in `src/core/text.c`:

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
- `head` and `tail` take `-n N`, `-nN` or `-N` (10 by default), and head
  each of several files with `==> name <==`. `tail` keeps the last
  `SHELL_COPY_BUF` (4 KB) of its input in `more`'s buffer, as a ring, so
  it costs no RAM; lines asked for that began before that are left out,
  from the first whole line kept, and it says so.
- `wc [-l -w -c]` counts line breaks, words and bytes, padded to seven
  columns, with a total for several files; a single count of the input is
  printed alone, as `ls | wc -l` wants.

The PicoCalc's own commands are in `src/core/system.c`, and reach the
port through `shell_system_t` (`shell_set_system`), a second set of
hooks beside `shell_hooks_t`, so the host tests fake them and the host
shell, with none, says `not on this machine`:

- `help` (`man` in the UNIX flavour) lists the builtins in columns as
  wide as the longest name, to the screen's width; `man name` prints its usage and one line on what
  it does, both from the builtin table in `shell.c`.
- `clear` writes `ESC [ H ESC [ 2 J` to the output. `phosphor` calls the
  port's `term_set_phosphor`, then sends SGR 39 so the new ink applies
  at once; the shell remembers which was chosen, for `phosphor` alone.
- `battery` reads `BAT` (HW §6). `backlight` reads `BKL` or `BK2`, or
  writes then reads it back, since the MCU's source steps and clamps the
  level, though firmware `0x00` was seen to keep it as written (HW §4.11).
- `info` prints the shell's version, then the port's lines (chip and
  clock, unique id, static SRAM, UF2 Loader and its partition, whether
  programs can run, the keyboard's firmware version as read at bring-up),
  then the card (`df`'s figures) and the battery.
- `poweroff`, `reboot` and `bootsel` save the history, say what is
  happening, and call the port, which returns only on failure.
  `poweroff` writes 6 to `OFF` (HW §6), then waits 10 s for the power to
  go, and says so if it has not. The PicoCalc switches off on USB power
  too (the owner, 2026-10-14). `reboot`
  sends `BOOT_SD` from `loader.c` (`loader_reboot_shell`), as
  `picocalc_shell_exit()` does, so it comes back to the shell, not the
  last program (§3.1); it is refused with no card or no loader in flash,
  where stage3 would drop to BOOTSEL. `bootsel` calls the boot ROM's
  `reset_usb_boot`. All three are refused with a redirection or in a
  pipe, as `menu` is.

| v1 | Later, if wanted |
|---|---|
| `ls [-l -a]` `cd` `pwd` `cat` `more` | `less` `find` `du` |
| `cp [-r]` `mv` `rm [-r]` `mkdir [-p]` `rmdir` `touch` | `chmod` (FAT read-only bit) |
| `echo` `head` `tail` `wc` `grep` | `sort` `uniq` `diff` `hexdump` |
| `df` | `fsck`-lite (check only) |
| `vi` `view` | |
| `man` `history` `clear` `set` `export` `phosphor` | `alias` `date` |
| `battery` `backlight` `poweroff` (southbridge; HW §6) | |
| `info` (shell version, board, chip, flash, loader version, card, battery) `reboot` `bootsel` `menu` `usbdisk` | `which` `uf2info` (name, version, size from binary info) |

*(Changed 2026-10-15:)* the UNIX flavour's `help` is called `man`, as
on UNIX; the DOS flavour keeps `HELP`. `displaytest`, the starter's
display stress test kept from M0 as a development command, is gone.
*(Changed 2026-10-18:)* `font` is gone too: the flavour has one font, in
40 columns (§4.1).

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

### 4.7 The DOS flavour

*Settled 2026-10-14:* many PicoCalc users know DOS, not UNIX. A second
flavour, chosen at build time with `-DPICO_SHELL_FLAVOUR=dos` (`unix` is
the default), gives them a command line that feels like DOS and an EDIT
modelled on MS-DOS 5's. It is never called MS-DOS, which is Microsoft's
mark; it is "the DOS flavour" of shell. Each flavour is its own
`BOOT2350.UF2`, and the release ships both, as `shell-unix.uf2` and
`shell-dos.uf2` *(settled 2026-10-16)*. The user copies the one they
want to the card's root; neither flavour installs the other.

**What is shared.** Everything below the command language: FatFs and the
card, `files.c` (with safe saves, EL §8.6), running programs (`run.c`,
`uf2.c`, `loader.c`), the terminal, the line editor and history, pipes,
`text.c`, `system.c` and `usbdisk`. Programs run by name from
`/pico2-apps/`, as in §4.3.

**What differs** (the flavour's own files in `src/core/`):

- **The card is `C:`.** The prompt is `PROMPT`'s, `$P$G` by default, so
  `C:\HOME>`. Paths use `\`, and `/` is read as `\` too; a leading
  `C:` is accepted and dropped. `$P` writes names as FAT stores them.
- **The parser.** Command names and switches are not case-sensitive.
  Switches are `/X`, and may follow a name with no space (`DIR/W`).
  Variables are `%NAME%`. `\` is a path separator, not an escape. `|`,
  `>`, `>>` and `<` work as in the UNIX flavour. Wildcards are expanded
  by each command, with DOS's rules (`*.*` matches every name, `*.` names
  with no extension), as DOS did, not by the parser.
- **The commands.** `DIR [/W /P /S /A /B]`, `CD`/`CHDIR`, `MD`/`MKDIR`,
  `RD`/`RMDIR`, `COPY`, `XCOPY [/S]`, `DEL`/`ERASE`, `DELTREE`,
  `REN`/`RENAME`, `MOVE`, `TYPE`, `MORE`, `FIND [/V /N /C /I]`, `ATTRIB`,
  `CLS`, `VER`, `VOL`, `MEM`, `SET`, `PATH`, `PROMPT`, `ECHO`, `PAUSE`,
  `HELP`, `EDIT`, `EXIT` (back to the loader's menu, as `menu`). The
  PicoCalc's own commands keep their names: `battery`, `backlight`,
  `phosphor`, `info`, `usbdisk`, `poweroff`, `reboot`, `bootsel`.
  *(Changed 2026-10-15: `font` is gone, as the flavour has only 40
  columns.)* There is no `DATE` or `TIME`: the PicoCalc has no clock.
- **History.** Up and Down as now; F3 repeats the last line, as DOSKEY
  and DOS's own F3 did.
- **Batch files.** `NAME.BAT` runs by name, ahead of a program of the
  same name. `%0`–`%9`, `SHIFT`, `GOTO :label`, `IF [NOT] EXIST`,
  `IF [NOT] ERRORLEVEL n`, `IF [NOT] a==b`, `CALL`, `REM`, `ECHO OFF` and
  `@`, `PAUSE`, and `FOR %%x IN (set) DO command`. `C:\AUTOEXEC.BAT` runs
  at start, in place of `/etc/profile`.
- **No vi.** EDIT takes its place.

**EDIT.** The core of MS-DOS 5's EDIT, not all of it. It uses the buffer,
undo and pattern code under vi (`editor_lines.c`, `editor_undo.c`,
`editor_pattern.c`), with its own key layer and screen, and keeps vi's
rules for size, CR LF and safe save (§4.6).

- **The screen.** White on blue; a menu bar on the top row, the file's
  name in the title row, the status row at the foot (`F1=Help`, line and
  column). Dialogs are boxes drawn with the font's line-drawing glyphs
  (§4.1). EDIT runs in 40 columns, the flavour's only width
  *(changed 2026-10-15; it switched to 64 before)*.
- **Menus.** Tap Alt for the menu bar, or Alt and the letter: File (New,
  Open, Save, Save As, Exit), Edit (Cut, Copy, Paste, Clear), Search
  (Find, Repeat Last Find, Change), Options (Display, the phosphor), Help
  (the keys, one screen). No Print and no mouse.
- **Keys.** The arrows, Home, End and the page keys; Ctrl with the arrows
  for words; Insert toggles overtype; *(changed 2026-10-14, after the
  chords were measured, HW §6.3)* Alt with the arrows selects, since
  the keyboard swallows Shift with Left and Right and turns Shift with
  Up and Down into the page keys; Ctrl+X cuts, Ctrl+C copies, Ctrl+V
  pastes, since Shift+Del is End and Insert is only Shift+Enter or
  Alt+I; Del clears; Ctrl+Y deletes the line; F3 finds again; Esc closes
  a menu or a dialog. The clipboard is vi's 8 KB register.
- **The chords were checked on the board** before the key layer was
  fixed (HW §6.3): Alt with a letter arrives as the capital beside Alt's
  events, Alt with an arrow as the arrow, and a tap of Alt as a press
  and a release with no hold between.

*As built at M9, step 1* (2026-10-14; `src/core/dos*.c`, `flavour.h`):

- **How the flavours divide.** `flavour.h` is what a flavour gives the
  rest: its builtins' table, the variables' defaults, the banner and
  startup, the prompt's text, running a typed line, resolving a path as
  typed, and its words for a missing command or a file it cannot use.
  `unix.c` holds the UNIX flavour's (the parser, expansion, `sh` and
  `/etc/profile`, moved out of `shell.c` unchanged); `dos.c` the DOS
  flavour's. `shell.c` keeps what both share: running one command with
  its redirections (`shell_command`), a pipeline (`shell_pipeline`),
  the prompt and `usbdisk`. `files.c` resolves every operand
  through the flavour, so a shared command reads `C:\X` in the DOS
  build, and says its failures in the flavour's words. CMake builds the
  core once per flavour, with `SHELL_DOS` 0 or 1 for the few places that
  differ by a character (Tab's separator, F3 and Esc, `help`'s last
  lines, the builtins' case). The UNIX flavour's tests pass unchanged.
- **Start.** The banner is `PicoCalc shell <version> (DOS)`. The shell
  starts in `\HOME` if the card has it, spelled as the card has it, so
  the prompt is `C:\HOME>`; otherwise in `C:\`. `AUTOEXEC.BAT` runs
  after the banner (*from step 2*).
- **The line.** `%NAME%` is replaced over the whole line before it is
  parsed, at the prompt as well as in batch files (DOS 5 did it only in
  batch files; the design asks for variables, so the prompt has them
  too). Names are looked up in capitals, `SET` stores them so, and an
  unset one is empty; `%%` is `%`, and any other `%` is itself. Then the
  line is split at `|`, outside `"…"`; there is no `;`, `&&` or `||`, as
  in DOS. `<`, `>` and `>>` take the next word, anywhere in a command,
  and `> NUL` throws the output away (for a program or a command that
  leaves the shell, which refuse a redirection, it is dropped: NUL
  leaves no file behind). Words are split at blanks, and
  `"…"` keeps blanks in one and is removed. A command's name ends at a
  `/`, `\` or `.` when what comes before is a builtin, so `DIR/W`, `CD..`,
  `CD\` and `ECHO.` work. ECHO, SET, PATH and PROMPT read what followed
  the name as typed, so `ECHO a   b` keeps its spaces.
- **`/` as a switch or a separator.** The commands that take switches
  (DIR, COPY, XCOPY, DEL, DELTREE, FIND) read a word that starts with `/`
  as switches, one or several (`/W/P`), in either case; an unknown one
  is `Invalid switch - /X`. Every other `/` separates names, so
  `CD /HOME` works, and so does `TYPE HOME/A.TXT`.
- **Wildcards** (`dos_match`). A pattern with no `.` matches the whole
  name; one with a `.` matches the parts before and after the name's last
  `.` separately. `*` matches the rest of a part, `?` one character or
  none at a part's end, without regard to case, against the long name
  or the 8.3 one. So `*.*` is every name, `*.` names with no extension,
  `*` every name. DIR adds `.*` to a pattern with no `.`, as DOS did:
  `DIR A` lists `A.TXT` and `A.BAK`. A wildcard skips hidden and system
  entries (§4.2's rule), and a name given whole finds itself. A
  directory given whole is every file in it, for DIR, COPY, XCOPY and
  DEL; REN, MOVE, ATTRIB and DELTREE take it as itself.
- **The commands**, in DOS 5's words (`File not found`, `Access denied`,
  `Invalid directory`, `Required parameter missing`, `Too many
  parameters - X`, `Bad command or file name`):
  - **DIR** prints the volume line and serial number (`f_getlabel`, so
    `FF_USE_LABEL` is on), then the entries in the card's order, as DOS
    did. With no clock (§4.2) there are no date and time columns: a name
    that fits 8.3 is `NAME     EXT`, another is as it is. *(Changed
    2026-10-14, at the owner's asking: it was 23 columns, and a long name
    pushed the size off the line.)* `<DIR>` has its own column, 15, after
    the name, as in DOS; a size, with a comma every three digits, ends at
    column 39, short of the 40th, where the terminal wraps in the
    40-column font. A name too long to leave room puts either on the
    next line.
    Then the count, the bytes, and the bytes free, their numbers ending
    in the same column so `bytes` lines up (the count has 5 digits, as a
    FAT directory holds at most 65,535 entries).
    `/W` gives names 13 columns apart, `[DIR]` for directories; `/P`
    waits at `Press any key to continue . . .` each screenful, only on
    the screen, and Esc, Break or Ctrl-C there stops; `/S` lists each
    directory below that has a match, with `Total files listed:`; `/A`
    includes hidden and system entries; `/B` gives names alone, whole
    paths with `/S`.
  - **COPY** copies one file, every file a wildcard or a directory
    names (each name said), into a directory, or under a wildcard name
    (`COPY *.TXT *.BAK`); then `n file(s) copied`. A file over itself is
    `File cannot be copied onto itself`. DOS's joining of several files
    into one, and `+`, are not there: `Cannot copy several files into
    one file`. Every copy goes through `files_copy`, `cp`'s, so through
    `name.new`. `/V` is accepted and changes nothing.
  - **XCOPY** says `Reading source file(s)...`, names each file, and
    ends `n File(s) copied`. A directory, a wildcard or `/S` copies into
    a directory, made if need be; one file without them is copied to
    the name given. `/S` walks the tree (`files_walk`, cp -r's stack),
    makes each directory as a file is copied into it, so empty ones are
    left out, as without `/E` in DOS, and skips hidden entries. Copying
    a tree into itself is `Cannot perform a cyclic copy`. Its status is
    DOS's 4 for an error.
  - **DEL** (and ERASE) deletes the files a name, a wildcard or a
    directory names. `*.*`, or a directory, asks `All files in directory
    will be deleted! Are you sure (Y/N)?`; `/P` asks for each. The answer
    is one key. A read-only file is `Access denied`.
  - **DELTREE** asks `Delete directory "X" and all its subdirectories?
    [yn]` (or of a file), unless `/Y`, says `Deleting X...`, and removes
    everything below, whatever its attributes. It refuses the root and
    the current directory or one above it.
  - **REN** (and RENAME) renames by a mask, `REN *.TXT *.BAK`: each part
    of the new name, before and after its last `.`, takes `?` and `*`
    from the old one. The new name has no path (`Invalid parameter`).
    A name in the way is `Duplicate file name or file not found`; a
    change of case is allowed.
  - **MOVE**, as DOS 6's, moves files (several, a wildcard, or a list
    with commas) into a directory, or renames one file or directory.
    Each line says `C:\FROM => C:\TO [ok]` or why not. A file replaces a
    file; nothing replaces a directory.
  - **CD** alone, or `CD C:`, prints the current directory; **MD** and
    **RD** take one name; RD refuses the current directory or one above
    it (`Attempt to remove current directory`).
  - **TYPE** shows one file; **MORE** pages its input, or one file,
    through the UNIX flavour's `more`; **FIND** finds text as it is, not
    a pattern, with case unless `/I`; `/V`, `/N` (`[n]` before each line)
    and `/C`; a `---------- NAME` line before each file's lines.
  - **ATTRIB** shows `A  SHR     C:\PATH` for each file, hidden ones
    included, or sets and clears `+R -R +A -A +S -S +H -H`.
  - **SET**, **PATH** (in capitals, `;` between directories, `PATH ;`
    clears) and **PROMPT** (`$P $G $L $B $Q $$ $N $_ $E $H $V`; `$T` and
    `$D` give nothing, with no clock). With PROMPT cleared the prompt is
    `C>`, DOS's own. **ECHO** prints the rest of the line as typed,
    `ECHO.` a blank line, and `ECHO OFF` hides the prompt.
  - **VER**, **VOL**, **MEM** (the port's static SRAM through
    `shell_set_memory`, and the app partition's size as the largest
    program), **CLS**, **PAUSE**, **HELP** (the shared `help`), **EXIT**
    (`menu`), and the PicoCalc's own commands under their names, which
    take their words in either case.
- **Programs.** A name that is not a builtin runs `NAME.UF2` from the
  current directory first, as DOS looked, then from each directory of
  `PATH`; a name with a `\`, `/` or `:` is a path. Otherwise `Bad command
  or file name`.
- **At the prompt.** F3 puts in the rest of the last line from the
  cursor's column, as DOS's F3 did; Esc drops the line. Tab completes
  with `\` after a directory and puts nothing before a space, since DOS
  has no escapes. Up and Down work as in the UNIX flavour; `history` is
  not a command here. The history file is shared.

*As built at M9, step 2* (2026-10-14; `src/core/dos_batch.c`):

- **Levels.** A batch file runs a line at a time at the level below the
  command that ran it, as `sh` does (§4.4), up to `SHELL_DEPTH - 1` (3)
  deep. Each level has its file and reader, its parameters, its
  expanded line and its FOR (a directory, its word and pattern, and a
  line), 3,104 B; the four are 12,416 B. Buffers a function is done with
  before anything it runs could need them again (IF's two words, the
  echoed prompt, a path being searched) are file-scope statics, not on
  the stack, and IF after IF is taken in a loop, not by recursion, so a
  CALL level costs about 720 B of stack (`-fstack-usage`). `dos.c` keeps the parser's output per level, and
  `dos_batch.c` everything above it: a line, typed or read, goes through
  `dos_line`, which expands it, takes REM, IF and FOR from its start, and
  hands the rest to the parser (`dos_commands`).
- **Finding one.** A name that is not a builtin is looked for as
  `NAME.BAT` in the current directory, then along `PATH`, before
  `NAME.UF2` is looked for at all: a batch file anywhere on the search
  beats a program of the same name, which is the design's rule rather
  than DOS's per-directory order. `NAME.BAT` typed in full, and a path,
  work too; another extension is never a batch file. A batch file in a
  pipe or with a redirection is refused (`Cannot redirect a batch file;
  CALL it`), since its lines have their own; `CALL NAME > FILE` runs it
  under CALL's redirection.
- **Parameters.** `%0` is the name as typed; `%1`… are the words after
  it, split at blanks, `,`, `;` and `=` outside `"…"`, with the quotes
  kept, as DOS kept them, so `IF "%1"==""` works. Up to
  `SHELL_WORDS_MAX` are kept, and SHIFT brings those past `%9` in. They
  are replaced in the same pass as `%NAME%` (`dos_expand`), so `%%`
  becomes `%` first; at the prompt `%1` is left as it is, as in DOS.
- **The line.** With ECHO ON, each line but a label is shown after a
  blank line and the prompt, expanded, as DOS did; `@` at its start
  hides it. Leaving a batch file for the prompt puts ECHO back as it was
  there, so `@ECHO OFF` in `AUTOEXEC.BAT` does not hide the prompt.
  A line over 255 characters is `Line too long`, and the next runs.
  Break, Esc or Ctrl-C is looked for before each line and each turn of a
  FOR (`shell_interrupted`), so a loop of commands that never look for
  it still stops; the line ends with `^C` and status 130, as any stopped
  line does. DOS's `Terminate batch job (Y/N)?` is not asked.
- **IF** takes `NOT`, then `EXIST name` (a file or a directory, or with
  a wildcard any file that matches), `ERRORLEVEL n` (status `n` or
  more), or `a==b` (exactly, case included, quotes and all; blanks may
  stand round `==`), then a command, which may be another IF or a FOR.
  Every builtin sets the status (0, or 1 and more on failure), not only
  programs as in DOS, since a program never comes back to report one; a
  false IF, REM, an empty FOR and a label leave it as it was.
- **FOR %x IN (set) DO command**: the set is split as parameters are;
  a word with a wildcard is each file it matches, in the card's order,
  hidden ones left out, each with the path typed before it
  (`SUB\*.TXT` gives `SUB\A.TXT`). `%x` is one character, matched in
  its case; in a batch file it is written `%%x`. FOR cannot be nested
  in a line, as in DOS, though a CALLed file has its own. With ECHO ON
  each command is shown, at the prompt too, as DOS did.
- **GOTO** reads the file again from its start for `:label` (the label's
  word, in either case); not found, it says `Label not found` and the
  batch file ends. At the prompt it does nothing. **CALL** runs a batch
  file at the level below and comes back, or anything else as if typed.
  Without CALL, a batch file run from another takes its level, file and
  parameters, so a file may run itself without limit, as in DOS; GOTO
  and this both end a FOR running at that level.
- **`AUTOEXEC.BAT`** at the card's root runs after the banner, at level
  1, opened as `/etc/profile` is (`files_open_startup`, so a `.new` left
  by a cut write is used). Missing, it is nothing.

*As built at M9, step 3* (2026-10-14; `src/core/edit.c`,
`editor_file.c`, `keys.c`):

- **The font** *(decided 2026-10-14)*. In 40 columns the DOS flavour
  draws IBM's PC BIOS 8x8 glyphs (`font-ibm-8x10.c`, made by
  `tools/mkfont-ibm.py` from VileR's font pack, CC BY-SA 4.0,
  THIRD-PARTY.md) in rows 1-8 of the 8x10 cell. The DEC line-drawing
  glyphs EDIT's boxes use come from the same font where code page 437
  has them, their lines carried to the cell's edges so they join;
  `screen.c` picks the font by `SHELL_DOS`, so each flavour links only
  its own. *(Changed 2026-10-15:)* the flavour has no 64 columns, since
  IBM made no font that narrow: no `FONT`, and the terminal ignores the
  64-column mode (4264). The terminal's grid, the panel's shadow and
  EDIT's screens are 40 columns wide. *(Changed 2026-10-18:)* so is the
  UNIX flavour's, and `TERM_COLS` (40) is the only width (§4.1).

- **Chords.** `keys_event` reports chords only while EDIT asks
  (`keys_report_chords`), so the prompt and vi are as they were: Alt with
  a letter or an arrow is `KEYS_ALT` with the lower-case letter or the
  arrow; a tap of Alt, pressed and let go with no key and no hold event
  between, is `KEYS_ALT_TAP`; Ctrl with an arrow, Home or End is
  `KEYS_CTRL` with it. Alt+I stays Insert. Alt+`,` and Alt+`.` (the
  MCU's backlights) were expected to look like a tap and open the menu,
  but on the owner's Pico 2 W they did not (HW §6.3). On the UART, US (`0x1F`) puts Alt on the
  next byte, and US US is a tap; RS (`0x1E`) logs the key events (§4.1).
- **The file.** vi's reading and writing moved to `editor_file.c`
  unchanged, and both editors use it: the whole file in one buffer of
  `VI_BUF_SIZE`, CR LF kept if every break is one, the last line break
  kept, read-only from FAT's bit, saved through `name.new`. A new file
  gets LF, as in vi. A failed Open leaves the text as it was. EDIT has
  its own 64 KB buffer and 8 KB clipboard; the DOS build has no vi, so
  nothing is held twice.
- **Not used from vi:** the undo journal (no key in the design asks for
  undo, as MS-DOS 5's EDIT had none) and the pattern matcher: Find and
  Change match the text as typed, in either case, as EDIT did. Change
  changes every match at once and says how many; EDIT's Find and Verify
  is not there. `editor_lines.c` (the line memo and word moves) is
  shared.
- **The screen.** 40 columns, in IBM's font *(changed 2026-10-15: EDIT
  switched to the 64-column font before)*. Row 0 is the menu bar, black on grey with each menu's
  letter in white; row 1 the name, in capitals, centred in a line;
  rows 2-30 the text, white on blue (`EDIT_TEXT_ROWS`); row 31 the
  status, black on cyan: `F1=Help  Alt=Menu` or a message, `OVR` when
  typing over, and `Line:n Col:n`. Boxes and menus use the DEC line
  glyphs (`ESC ( 0`). The screen is built as cells and only the rows
  that changed are sent, as in vi; the status row's last cell is erased,
  not written, so the terminal never scrolls. Options, Display swaps the
  colours for the phosphor on black, in reverse video. The bar's grey is
  xterm's 248; the starter's 256-colour table had it nearly black, so
  `term.c` now works colours 16-255 out by xterm's formula (§4.1).
- **Menus and boxes.** A menu or a box draws over the text and takes
  keys until it closes: menus with Left, Right, Up, Down, Enter, the
  item's letter, Alt and a menu's letter, or Esc; *(changed
  2026-10-15)* Alt and an item's letter carries the item out, before
  any menu's, so Alt held from opening the menu works (Alt+F, Alt+X
  exits; Alt+S in File is Save); a box asking for a
  name or text offers the last one, replaced by the first letter typed;
  a question takes its buttons' letters, drawn in red as in MS-DOS's
  EDIT (underlined in the phosphor colours), Enter for the first, Esc to
  cancel. Exit, New and Open ask to save a change first; Save As asks
  before writing over another file. Brk is Exit; the end of input
  leaves without saving, as vi does.
- **In 40 columns** *(2026-10-15)*. The menu bar is 37 columns, so it
  fits. The keys help is written to 36 columns, to fit its box. A
  question or a reason wider than the screen less 4 is broken at a
  space. The ruler gives way to nothing but a message: it goes over
  `F1=Help  Alt=Menu` if it must, and a message too long to leave it
  room hides it until the next key. Two messages were shortened to fit
  the status row's 39 cells.

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
| | `font.h` | from `references/fonts/`, declaring each flavour's font; `font-8x10.c`'s DEC graphics are in both fonts (§4.1) |
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
- **Flavours** (*from M9*, §4.7). `-DPICO_SHELL_FLAVOUR=dos` builds the
  DOS flavour's `BOOT2350.UF2`, in a directory of its own
  (`tools/build.sh -DPICO_SHELL_FLAVOUR=dos build/pico-dos`); `unix` is
  the default. The host build makes both cores, `shell_core` and
  `shell_core_dos`, both host shells, `shell` and `shell-dos`, and
  `test_dos` beside the UNIX flavour's tests.
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
| Terminal cells, 40 × 32 × 6 B (character, attributes, two RGB565 colours) | 7.5 KB |
| The renderer's shadow of frame memory, 40 × 48 × 6 B (§4.1) | 11.25 KB |
| Font, 128 glyphs × 10 rows | 1.25 KB |
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

**Measured at M6** (the same tools, 2026-10-13): development 427,004 B,
release 423,260 B, 17,000 B more than M4: TinyUSB's MSC buffer grew from
512 B to 16 KB, and the multi-block transfers are about 1 KB of code.
About 83 KB is left.

**Measured at M9, step 1** (the same tools, 2026-10-14, release builds):
the UNIX flavour 435,480 B, 2,964 B more than M8's 432,516 B (mostly the
walk's own FatFs entry, two 321-byte prompt buffers, and `f_getlabel`); the DOS flavour 301,628 B, which has no vi (its 64 KB
buffer, 16 KB journal, 8 KB register and four levels of UNIX parse) but
its own four levels of expanded and parsed line, about 6 KB. EDIT is to
come. The DOS commands' deepest call, COPY down into FatFs, is about
3 KB of the 8 KB stack (`-fstack-usage`): each command keeps two paths
on the stack.

**Measured at M9, step 2** (the same tools, 2026-10-14, release builds,
the linker's RAM figure): the DOS flavour 321,228 B, 19,600 B more than
step 1's 301,628 B: 12,416 B for the batch files' four levels, about
1.7 KB of buffers kept off the stack, the rest their code. The UNIX
flavour is unchanged at 435,480 B. *Estimated from `-fstack-usage`:*
the deepest stack, a typed line CALLing three levels down through IF
and FOR to a COPY, is about 5.8 KB of the 8 KB: about 720 B a level
and step 1's 3 KB for COPY.

**Measured at M9, step 3** (the same tools, 2026-10-14, release builds,
`__end__` from the link map): the DOS flavour 422,100 B, 100,872 B more
than step 2's 321,228 B: EDIT's 64 KB buffer, 8 KB clipboard and two
4 KB screens of cells (shown and built), its code and `editor_lines.c`.
12,608 B less than the UNIX flavour, so it leaves a little more room.
The UNIX flavour 434,708 B, 772 B less than step 2's, in code, after
vi's file code moved to `editor_file.c`. EDIT keeps its large state static; its
own frames are small, and it runs at the prompt's depth or a batch
file's.

**Measured after M9** (the same tools, 2026-10-15, release builds,
`__end__` from the link map): with only 40 columns the DOS flavour is
407,416 B, 15,712 B less than the 423,128 B before: about 14.6 KB of
`.bss` (the terminal's grid, the panel's shadow of 48 rows and EDIT's
two screens, each 40 columns wide in place of 64) and 1.1 KB of code
and fonts (`FONT` and the owner's 5x10). The UNIX flavour is
unchanged.

**Measured after the font change** (the same tools, 2026-10-18, release
builds, `__end__`): with only 40 columns and one font, the UNIX flavour
is 418,984 B, 13,868 B less than the 432,852 B before (its grid, the
panel's shadow and vi's rows 40 columns wide in place of 64, one font in
place of two, and `font`'s code). The DOS flavour is 407,700 B, 268 B
less (`shell_cols` and the column switching in the terminal).

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
- **M6 — USB speed.** Make `usbdisk` faster than M4's 512 bytes per
  transfer (about 170 KB/s writing, by eye): a larger MSC buffer, and
  multi-block reads and writes in `sd.c` (CMD18, CMD25), each wait still
  bounded below the southbridge's 2.5 s (§4.2). Done when the rate each
  way is measured on the board, before and after, with a large file
  copied each way and compared byte for byte.
- **M7 — Shell at power-on (optional).** A patched UF2 Loader, so that
  power-on with no key held loads the shell, and Up/F1/F5 start the last
  program; Down/F3 still give BOOTSEL. pelrun's loader is a submodule
  pinned at v2.5 (`5c44a4b`) in `third_party/uf2loader`, with
  `references/` left alone; `patches/uf2loader-boot-shell.patch` changes
  only `stage3/stage3.c`, and `tools/build-loader.sh` applies it to a copy
  and builds `bootloader_pico2.uf2`. The patch:
  - swaps the no-key and Up/F1/F5 modes in `read_bootmode()`;
  - starts the program in flash, not BOOTSEL, when there is no card or
    `/BOOT2350.UF2` cannot be loaded, so a power-on without a card still
    does something;
  - names its version `2.5-shell`.

  The shell sends `BOOT_DEFAULT` as an explicit command when it starts a
  program, instead of no command, which stock stage3 also accepts; and
  `loader.c` accepts `2.5-shell`. The owner installs the loader in
  BOOTSEL; the shell never writes stage3's partition, and reinstalling
  the stock loader undoes M7. Check first whether the emulators reboot
  themselves with no command: with the patch, that would land in the
  shell. Done when, on the board with the patched loader, power-on
  gives the shell, Up held gives the last program, power-on with no card
  starts the program, and a program still runs, and comes back by its
  exit command, under both the patched and the stock loader. Offer the
  change to pelrun as an option.
- **M8 — The rest of v1.** The twelve v1 builtins not built at M4 (§4.5):
  - `head [-n N]`, `tail [-n N]` and `wc [-l -w -c]`, in
    `src/core/text.c` beside `more` and `grep`, reading files, `<` and
    pipes; `tail` keeps its last N lines in a fixed buffer from
    `config.h`, so a long input costs no more RAM.
  - `help`: the builtins, one line each; `help name` for one.
  - `clear`; `phosphor [white|green|amber]`, which sets the default
    foreground (§4.1), the current one with no operand.
  - `battery` (percent, and charging when bit 7 says so; HW §6, whose
    reading is refreshed only every 20 s) and `backlight [lcd|kbd] [N]`,
    reading the level first and showing it with no operand (HW §4.11).
  - `poweroff`: the history saved first, as before a program, then the
    southbridge's `OFF` register, which waits at least 6 s (HW §6).
  - `info`: shell version, board, chip, flash, loader version (from
    `loader.c`), card and battery.
  - `reboot`, back to the shell, with the loader's command sent from
    `loader.c` as `picocalc_shell_exit()` does, since a reboot with no
    command would start the last program (§3.1); `bootsel`, through the
    boot ROM. Both save the history first.

  The southbridge calls go through `src/port/` behind a hook, so that
  the host tests can fake them, as `usbdisk` is (M4). Done when the host
  tests cover each command, its errors and its usage; on the board,
  `battery` and both backlights agree with what the hardware shows,
  `poweroff` switches the PicoCalc off, `reboot` comes back to the
  shell with the history kept, and `bootsel` shows the RP2350's drive on
  a computer. Measure the RAM `help`'s text costs.
- **M9 — The DOS flavour** (§4.7), in steps, each with host tests over
  FAT16 and FAT32 images:
  1. The build option, the parser, `C:` paths and the prompt, and the
     commands, sharing `files.c` and `text.c` with the UNIX flavour.
     *Built 2026-10-14* (§4.7, milestones.md).
  2. Batch files and `AUTOEXEC.BAT`. *Built 2026-10-14* (§4.7,
     milestones.md).
  3. The Alt chords measured on a Plus 2 W and written in HW §6.3, then
     EDIT. *Built 2026-10-14* (§4.7, milestones.md).

  *Done 2026-10-14* (milestones.md). Both flavours build with
  `tools/build.sh` and on the host, and the UNIX flavour's tests still
  pass unchanged. Done when, on the board, the DOS
  flavour copies, renames and deletes files, runs a program by name and
  comes back, runs `AUTOEXEC.BAT`, and EDIT has changed
  `/atom/pico-atom.cfg` and pico-atom has read it, as M3 did for vi.
  Measure both flavours' RAM from the link map, and EDIT's opening time
  for a 41 KB file.
- **M10 — A flash filesystem for programs to share (optional; open
  questions in §3.7).** A littlefs partition between stage3 and App,
  which the shell creates, resizes, formats and removes, and which
  programs may mount (§3.7). In steps:
  1. The datasheet and picotool checks in §3.7. The design is changed
     where they disagree.
  2. `loader.c` reads and rewrites stage3's table block, and resizes in
     the order §3.7 gives. Host tests run over a flash image holding a
     real stage3 sector, with power lost at every flash operation of
     create, grow, shrink and remove; each must leave a table the boot
     ROM would accept and a filesystem that mounts.
  3. littlefs in both flavours, with each flavour's commands (§3.7). Host
     tests run over a flash image, beside the FAT16 and FAT32 ones.
  4. `include/picocalc_flashfs.h`, and `test/pico/flashfs_test.c`, which
     builds `flashfs-test.uf2`: a program that mounts the filesystem,
     shows a file the shell wrote, writes one back and exits to the shell.

  Done when, on the board:
  - each flavour creates the filesystem and copies a file in;
  - `flashfs-test` reads it and writes one that the shell then shows;
  - a resize up keeps the files, as does a resize down that leaves them
    room;
  - the program in flash is lost on a resize and power-on gives the
    shell;
  - `menu` cannot write a program over the filesystem;
  - a reset in the middle of a resize comes back to a filesystem that
    mounts;
  - after UF2 Loader is reinstalled, the shell offers to restore the
    entry.

  Measure each flavour's RAM, the mount time, and the rate of reading and
  writing a large file.
