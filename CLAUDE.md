# Project

- **shell** (repo `picocalc-shell`) is a UNIX-like command line for the ClockworkPi PicoCalc, written in **C11** against the **Pico SDK**, for RP2350 boards. It manages files on the SD card (FAT16/32 via FatFs), edits text files in a simplified **vi**, and runs UF2 programs by name: `pico-oric` runs `/pico2-apps/pico-oric.uf2` (the loader menu's own directory).
- It is a **`no_flash` RAM program installed as `/BOOT2350.UF2`**, in place of pelrun's UF2 Loader menu. It runs a program by flashing the app partition and rebooting. A program returns to the shell through `picocalc_shell_exit()`, or by the user holding Up/F1 at power-on. Its prompt is the current directory followed by `>` (`/home>`).
- **Settled decisions** are in the table at the top of docs/design.md. Do not reopen them: the shell replaces `BOOT2350.UF2`; programs may add an exit command; power-on with no key held starts the last program; program arguments are deferred; the terminal uses the owner's fonts; the shell is called shell and its prompt is the current directory followed by `>`; the licence is GPL-3.0; RP2350 only, no RP2040; programs live in `/pico2-apps/`.
- **Status:** design complete (Draft 3); no milestone started. Each milestone's record (what was verified, on which board, on what date, and what was not checked) goes in `docs/milestones.md`, not here. Keep this line to one sentence per milestone.

## The documents

| File | Authority on |
|---|---|
| [docs/design.md](docs/design.md) | the shell: boot chain, commands, architecture, budgets, settled decisions, milestones' scope and done-when criteria |
| [docs/hardware-notes.md](docs/hardware-notes.md) (**HW §N**) | the PicoCalc: wiring, protocols, timing, measured costs, quirks. A new hardware fact goes here, written to stand alone |
| [docs/milestones.md](docs/milestones.md) | what each milestone verified, on which board and date, and what it did not check, newest first |

Read docs/design.md before starting work. **EL §N** refers to `references/pico-oric/docs/emulator-lessons.md`. Its architecture (§2.1), testing (§11), hardware workflow (§13) and process (§14) chapters apply to this project.

# References (read-only, never edit)

- `references/uf2loader`: UF2 Loader v2.5. It is the authority on the boot chain (`stage3/stage3.c`), the scratch-register commands (`common/bootloader/proginfo.c`) and flashing (`ui/uf2.c`).
- `references/pico-atom`, `pico-jupiter-ace`, `pico-oric`: the owner's emulators, which are the **model programs** and the **source of the port drivers** (`src/port/sd.c`, `diskio.c`, `lcd.c`, `southbridge.c`, `kbd.c`, `fatfs/`). pico-oric's copy is the newest. Their card layout and settings-file conventions (EL §8.6, §8.7) are what the shell and vi serve.
- `references/picocalc-text-starter`: the owner's MIT starter. Use its **terminal** (`drivers/display.c`, the VT100/ANSI state machine), its text drawing and scroll (`drivers/lcd.c`), its southbridge power, battery and backlight calls, and its key codes (design §4.1, §5). **Never use its `fat32.c` or `clib.c`**: they are buggy. It is **older than docs/hardware-notes.md**, so where they disagree (keyboard polling, LCD SPI rate, SD driver) the hardware notes win.
- `references/fonts`: the owner's `font_8x10` (40 × 32, the default) and `font_5x10` (64 × 32), used by the terminal (design §4.1).
- `references/pico-logo`: use **only its vi key layer** (`devices/picocalc/editor_vi.c`, `editor_pattern.c`, `editor_undo.c`, `editor_lines.c`, `editor_search.c`). It is otherwise a special case: it keeps a filesystem in flash (design §3.6). Do not model the shell on it.
- When a file is taken from a reference, its header says where it came from, and `THIRD-PARTY.md` records it.

# Build & Test

Not set up yet (design §6). The intended shape is the emulators': one CMake tree. The default build is the `pico2` firmware (`BOOT2350.UF2`). `-DPICO_SHELL_HOST=ON` builds the core and its CTest tests with the system compiler. Both build under `-Wall -Wextra -Werror`. Logic is developed and tested on the host, with FatFs over FAT16/FAT32 image files; write a host test rather than relying on a hardware run.

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
