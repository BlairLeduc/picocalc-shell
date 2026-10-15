# Project

- **shell** (repo `picocalc-shell`) is a UNIX-like command line for the ClockworkPi PicoCalc, written in **C11** against the **Pico SDK**, for RP2350 boards. It manages files on the SD card (FAT16/32 via FatFs), edits text files in a simplified **vi**, and runs UF2 programs by name: `pico-oric` runs `/pico2-apps/pico-oric.uf2` (the loader menu's own directory).
- It is a **`no_flash` RAM program installed as `/BOOT2350.UF2`**, in place of pelrun's UF2 Loader menu. It runs a program by flashing the app partition and rebooting. A program returns to the shell through `picocalc_shell_exit()`, or by the user holding Up, F1 or F5 at power-on. Its prompt is the current directory followed by `>` (`/home>`).
- **Settled decisions** are in the table at the top of docs/design.md. Do not reopen them: the shell replaces `BOOT2350.UF2`; programs may add an exit command; power-on with no key held starts the last program; program arguments are deferred; the terminal uses the owner's fonts; the shell is called shell and its prompt is the current directory followed by `>`; the licence is GPL-3.0; RP2350 only, no RP2040; programs live in `/pico2-apps/`; the LCD's SPI runs at 75 MHz; a second, DOS-like flavour (card `C:`, EDIT in place of vi, never called MS-DOS) is its own `BOOT2350.UF2`, chosen by copying it to the card.
- **Status:** M0 (skeleton) done on branch `m0-skeleton`, 2026-10-12: on a Pico 2 W from the card (owner) and a Plus 2 W over SWD; power-on start time carried forward. M1 (files) done on branch `m1-files`, 2026-10-12: host tests over FAT16 and FAT32 with write faults, and the owner's release run on a Pico 2 W; card timings carried forward. M2 (run) done on branch `m2-run`, 2026-10-13: host tests with power loss at every flash operation, Claude's runs on the Plus 2 W under UF2 Loader 2.5, and the owner's on a Pico 2 W, power cut mid-write included. M3 (vi) done on branch `m3-vi`, 2026-10-13: host tests (Pico Logo's and `test_vi`), an edit of `/atom/pico-atom.cfg` on the Plus 2 W that pico-atom then read (the owner saw it on the panel), and a 41 KB file opened in under a second. M4 (comfort) done on branch `m4-comfort`, 2026-10-13: host tests for expansion, pipes, scripts, `more`, `grep` and the line editor, Claude's runs on the Plus 2 W, and the owner's on a Pico 2 W, release build, `usbdisk` copying files each way. M6 (USB speed) done on branch `m6-usb-speed`, 2026-10-13: `test_sd` against a card model, and Claude's runs on the Plus 2 W to a Mac, `usbdisk` from 219 to about 610 KB/s writing and 432 to about 700 KB/s reading, compared byte for byte, and the owner's on a Pico 2 W with an iPad mini. M8 (the rest of v1) done on branch `m8-rest`, 2026-10-14: host tests (`test_text`, the new `test_system`), Claude's runs on the Plus 2 W (`poweroff` cut the power, `reboot` kept the history, `bootsel` reached BOOTSEL), and the owner's on a Pico 2 W, with new phosphor colours; M5 is still to do. M9 (the DOS flavour) under way on branch `m9-dos`: step 1 (parser, `C:` paths, prompt, commands) built 2026-10-14, host tests over FAT16 and FAT32 and Claude's runs on the Plus 2 W from SRAM. Each milestone's record (what was verified, on which board, on what date, and what was not checked) goes in `docs/milestones.md`, not here. Keep this line to one sentence per milestone.

## The documents

| File | Authority on |
|---|---|
| [docs/design.md](docs/design.md) | the shell: boot chain, commands, architecture, budgets, settled decisions, milestones' scope and done-when criteria |
| [docs/hardware-notes.md](docs/hardware-notes.md) (**HW §N**) | the PicoCalc: wiring, protocols, timing, measured costs, quirks. A new hardware fact goes here, written to stand alone |
| [docs/milestones.md](docs/milestones.md) | what each milestone verified, on which board and date, and what it did not check, newest first |
| [docs/user-guide.md](docs/user-guide.md) | every command and the command line, for users. Keep it in step with the builtins and their `help` text |

Read docs/design.md before starting work. **EL §N** refers to `references/pico-oric/docs/emulator-lessons.md`. Its architecture (§2.1), testing (§11), hardware workflow (§13) and process (§14) chapters apply to this project.

# References (read-only, never edit)

- `references/uf2loader`: UF2 Loader v2.5. It is the authority on the boot chain (`stage3/stage3.c`), the scratch-register commands (`common/bootloader/proginfo.c`) and flashing (`ui/uf2.c`).
- `references/pico-atom`, `pico-jupiter-ace`, `pico-oric`: the owner's emulators, which are the **model programs** and the **source of the port drivers** (`src/port/sd.c`, `diskio.c`, `lcd.c`, `southbridge.c`, `kbd.c`, `fatfs/`). pico-oric's copy is the newest. Their card layout and settings-file conventions (EL §8.6, §8.7) are what the shell and vi serve.
- `references/picocalc-text-starter`: the owner's MIT starter. Use its **terminal** (`drivers/display.c`, the VT100/ANSI state machine), its text drawing and scroll (`drivers/lcd.c`), its southbridge power, battery and backlight calls, and its key codes (design §4.1, §5). **Never use its `fat32.c` or `clib.c`**: they are buggy. It is **older than docs/hardware-notes.md**, so where they disagree (keyboard polling, LCD SPI rate, SD driver) the hardware notes win.
- `references/fonts`: the owner's `font_8x10` (40 × 32, the default) and `font_5x10` (64 × 32), used by the terminal (design §4.1).
- `references/pico-logo`: use **only its vi key layer** (`devices/picocalc/editor_vi.c`, `editor_pattern.c`, `editor_undo.c`, `editor_lines.c`, `editor_search.c`). It is otherwise a special case: it keeps a filesystem in flash (design §3.6). Do not model the shell on it.
- When a file is taken from a reference, its header says where it came from, and `THIRD-PARTY.md` records it.

# Build & Test

One CMake tree (design §6), both builds under `-Wall -Wextra -Werror`. Logic is developed and tested on the host (from M1, with FatFs over FAT16/FAT32 image files); write a host test rather than relying on a hardware run.

- Host tests: `cmake -S . -B build/host -DPICO_SHELL_HOST=ON && cmake --build build/host -j && ctest --test-dir build/host --output-on-failure`
- `tools/fetch-suites.sh` copies the real UF2s (the emulators' release builds, the loader's output) into `test/suites/uf2/` for `test_uf2`, which skips them if absent.
- Firmware: `tools/build.sh` gives `build/pico/BOOT2350.uf2` (development: UART1 log, mirror and typed keys); `tools/build.sh -DPICO_SHELL_UART=OFF build/pico-release` is the release build. Add `-DPICO_SHELL_FLAVOUR=dos` for the DOS flavour, in its own directory (`build/pico-dos`, `build/pico-dos-release`). The host build has both flavours: `build/host/shell` and `build/host/shell-dos`, and `test_dos`. It finds the newest SDK and toolchain under `~/.pico-sdk/`.
- On the board: `tools/uart-log.sh` captures UART1, `tools/uart-type.sh` types, `tools/uart-screen.sh` dumps the grid as text. `tools/usb-rate.py /Volumes/NAME [MB]` times `usbdisk` each way from the Mac and compares the file byte for byte.
- Over SWD, with no card: `tools/swd-ram.sh [elf]` runs a build from SRAM (no flash written); `tools/swd-drive.py elf type|screen|stats` drives the release build; `tools/swd-shell.sh` sends the loader's `BOOT_SD`. The Plus 2 W on the probe has UF2 Loader 2.5 in flash since M2, and a test card; pico-oric, there before, is now a program on that card like any other.

# How to work

- **Think before coding.** State assumptions. If there are several interpretations, present them rather than picking one silently.
- **Simplicity first.** Write the minimum code that solves the problem. Add no speculative features.
- **Surgical changes.** Touch only what the request needs, and match the existing style.
- **Goal-driven.** Turn each task into a verifiable check, then loop until it passes. Run all tests after any change.
- **Keep the documents current.** When a decision is made or the build departs from the design, update docs/design.md in the same change. Record each milestone in docs/milestones.md (EL §14.3); CLAUDE.md gets only the one-line status.
- **Estimates stay labelled** as estimates until a measurement replaces them, and every measurement names its board, build and date (EL §14.4).

# Constraints

- **core / port split** (EL §2.1). `src/core/` is portable C with no SDK headers. `src/port/` is the only place the SDK appears. Every fixed capacity lives in `src/core/config.h`.
- Everything runs from **SRAM**, with ~504 KB for code and data together.
- All knowledge of the loader's private protocol (scratch commands, partition remap, flash end) lives in **`src/port/loader.c`**.
- Never write flash outside the app partition. Check a UF2's family and extent before erasing anything.
- Card writes go through `name.new`, then unlink, then rename (EL §8.6). Keep every SD wait below the southbridge's 2.5 s watchdog.
- Test on a card set aside for testing, never on the owner's own files (EL §13.4).
