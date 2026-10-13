# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §10 holds each milestone's scope
and done-when criteria; this file keeps the full record (EL §14.3).

Each entry gives:

- the milestone and the branch, with the date;
- what was **checked on the host**: the tests added, planted bugs caught,
  and the memory report;
- what was **checked on the board**: which board (physical identity, not
  just the build target, HW §2.1), which build, who checked it (the owner's
  checks on the panel count, and say so), and the log under `out/`;
- **measurements** beside the estimates they replace (EL §14.4);
- a **not checked** list, carried forward until something checks each item
  or a decision drops it.

## M0 — Skeleton (branch `m0-skeleton`, 2026-10-12)

**Done**, but one measurement is carried forward: the start time from power-on
through stage3 (below).

**Checked on the board:**

- **The owner, on a Pico 2 W, release build, from the card** (`/BOOT2350.UF2`,
  Up held at power-on): the shell started from the loader, the banner and
  `/>` showed in both fonts, and the display test ran correctly on the
  panel, its 2,000 scrolled lines included, so the 0/480/0 scroll band
  works. Its report: 186.98 rows/s, 138,949.17 chars/s, 7,890.73
  displayed cps.
- **Claude, on a Plus 2 W** (RP2350B rev 2, id `7458DC82A89AAC12`), over
  the Debug Probe. This board has no UF2 Loader: pico-oric is at flash
  offset 0 (QMI address translation is the identity), so the shell was
  started from SRAM with `tools/swd-ram.sh` and nothing was written to
  flash. That path skips stage3, so it does not check the boot chain.
  - Development build (`out/m0-ram-dev-3.log`, `-4.log`): the UART banner
    (`chip=RP2350B rev=2`, clk_sys 150 MHz, southbridge version `0x00`,
    LCD SPI 25 MHz configured); the screen mirrored over the UART; typed
    keys, Backspace, Ctrl-C, `nope: not found`, `font 5x10` and back; the
    FS screen dump matching the mirror in both fonts; GS stats with 0 key
    overflows and 0 I²C errors.
  - Release build, driven over SWD (`tools/swd-drive.py`): `displaytest`
    ran to the end, with `R` in the box as on the host.

**Measurements** (Plus 2 W, release, 2026-10-12):

| What | Estimate | Measured |
|---|---|---|
| Shell start, power-on to prompt | ~0.5 s (design.md §2.2) | not measured: needs the loader |
| Shell start, bootrom's reset to prompt, no stage3 | — | 560.3–560.5 ms over 3 runs: `main` at 8.5–8.7 ms, LCD up at 485.1–485.3 ms |
| The same at 75 MHz, the default from M0's end | — | 463.2–463.4 ms over 3 runs: LCD up at 437.2–437.4 ms |
| Display test, 25 MHz | — | Plus 2 W 187.43 lines/s, 7,890.73 displayed cps; Pico 2 W (owner) 186.98 lines/s, 7,890.73 displayed cps |
| Display test, 75 MHz (`build/pico-release-75`, timing only) | — | 511 lines/s, 12,666 displayed cps |
| RAM, development / release (link map) | ~504 KB available | 72,892 B / 69,380 B |

The character phase took the same time on both boards to the reported
hundredth (138,949.17 chars/s, 7,603,852 µs on the Plus 2 W): it runs
from SRAM at the same clock and SPI rate, with nothing else running, so it
is deterministic. The rows phase differs by 0.24 %.

At 25 MHz, of the 560 ms, about 400 ms is `lcd_init`'s datasheet waits, about 66 ms
is clearing the panel at 25 MHz, and about 75 ms is the first prompt
redrawing 32 rows that are already black: the renderer's shadow starts
unknown. Seeding the shadow with blanks after the clear would save the
last of these.

**Checked on the host** (macOS, Apple clang 21, `-Wall -Wextra -Werror`,
C11; firmware with Arm GCC 15.2 and Pico SDK 2.3.1):

- Both builds compile clean: `-DPICO_SHELL_HOST=ON` (the core and six
  test binaries) and the firmware, development and release
  (`-DPICO_SHELL_UART=OFF`). The firmware is `BOOT2350.uf2`: family
  `rp2350-arm-s`, 147 blocks at `0x20000000`–`0x20009300` (stage3 accepts
  up to `0x20080000`), its image definition at `0x2000015c`, inside the
  first 4 KB that stage3's `rom_chain_image(SRAM_BASE, 0x1000)` searches.
- `test_skeleton`: every core header builds with no SDK; config.h agrees
  with the fonts and the panel. Putting `#include "pico/stdlib.h"` in
  `config.h` breaks the host build, as it should (EL §2.1).
- `test_term`: text, wrap, cursor motion, erase, SGR and the reverse
  semantics, character sets, scrolls and regions, modes, reports. The
  renderer contract is checked by replaying 20,000 random runs of
  sequences through a model of the panel (48 rows of frame memory,
  hardware scroll, dirty rows only) and requiring the panel to equal the
  grid after every flush; a control renderer that ignores the scroll must
  differ, and does.
- `test_displaytest`: the starter's display test, byte for byte, in both
  fonts: after the 2,000 rows, rows 0–30 hold rows 1970–2000 in their
  xterm colours; after the 60,000 characters, the DEC box and the last
  letter (`R`, colour 16 + 59999 % 215) are in place; the byte count
  matches; the panel model matches the grid after every write.
- `test_shell`: the banner and `/>` in both fonts, typing, Backspace,
  Ctrl-C, the line limit, `font`, and `displaytest` stopping when asked.
- `test_keys`: presses type, holds and releases do not (a release with no
  press included), Caps Lock is accepted, Ctrl chords, UART bytes.
- `test_screen` (added after the PR's review): the port's renderer,
  `src/port/screen.c`, built on the host against a model of the panel
  (`fake_lcd.c`: 480 rows of frame memory, the scroll offset, windows
  that must not wrap). After every flush of 1,500 random runs of
  sequences, in both fonts, every panel pixel must be the one the grid
  calls for, with the cursor at the terminal's cursor and nowhere else; a
  panel that ignores the scroll must differ, and does. Also: scrolling
  under a visible cursor leaves no stray underline, the blink's two
  phases, output showing the cursor at once, and a one-cell change
  sending one cell's 80 pixels.
- CSI parameters saturate at 65535, and an out-of-range 256-colour index
  or RGB component is ignored (also from the review); `test_term` checks
  both.
- **Planted bugs**, one at a time in a scratch copy, each rebuilt clean;
  every one failed at least one test: the dirty mask not moved on a
  scroll up (term, displaytest), moved the wrong way on a scroll down
  (term), the scroll not counted (term, displaytest), CUU's starter
  default of no move (term), deferred wrap (term, displaytest), DEC
  graphics off by one (term, displaytest), regions ignored (term), no
  LF to CR LF (shell, displaytest), releases typing (keys), no
  checkerboard for 0x80 and above (term), reverse not swapping (term).
  In `screen.c`, each caught by `test_screen`: the cursor erased after the
  scroll instead of before, the cursor kept through a scroll, a span one
  cell short in the shadow, the shadow indexed by panel row, no bold, a
  font change keeping the shadow, no blink, output not restarting the
  blink, the scroll sent the wrong way. In `term.c`: parameters wrapping,
  and a colour index taken modulo 256.

**Not checked:**

- Shell start time from power-on, through stage3's 0.5 s key wait and the
  card read, against the ~0.5 s estimate. Needs a board with the loader
  and the development build on its card, or the GS line; or the
  `s_prompt_us` read over SWD.
- `tools/swd-shell.sh` (the loader's `BOOT_SD` command over SWD): on the
  Plus 2 W the watchdog reset happened, but there is no stage3 there to
  take the command. Check it on a board with the loader.
- 75 MHz on the panel by eye. The owner made it the default after M0's
  measurements (design.md §4.1); M1's board run is the first at that rate.
- The cursor's blink and the scroll's underline by eye on the panel; both
  are now checked on the host against the panel model (`test_screen`).
- Every key code and event on the southbridge (HW §6.2); key events are
  not logged in this build, since the UART carries the screen.
