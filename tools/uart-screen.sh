#!/usr/bin/env bash
# uart-screen.sh — ask the shell for its screen as text (design.md §4.1;
# EL §11.2). Copied from pico-oric.
#
#   tools/uart-log.sh 10 out/screen.log &
#   tools/uart-screen.sh
#
# Sends FS (0x1C), which no key sends; the development build prints the
# terminal's 32 rows from its cell grid (dump_screen in src/port/main.c),
# so the capture shows what the panel should. Send GS (0x1D) instead for
# the start time and the counters. Start uart-log.sh first: this only
# writes.
set -euo pipefail
UART_TYPE_DELAY=0 exec "$(dirname "$0")/uart-type.sh" '\034'
