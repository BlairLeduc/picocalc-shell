#!/usr/bin/env bash
# uart-type.sh — type at the shell over UART1 (design.md §4.1; EL §13.1).
# Copied from pico-oric, with the shell's keys and pacing.
#
#   tools/uart-type.sh 'ls -l\r'
#   tools/uart-type.sh $'ls\r'
#
# The development build takes each received byte as a key (keys_uart in
# src/core/keys.c): characters type as themselves, CR and LF are Enter,
# BS and DEL Backspace, ESC is Esc, ^C is Ctrl-C, and \x81-\x85 are F1-F5.
# FS (\x1c), GS (\x1d) and RS (\x1e) are not keys: uart-screen.sh sends the
# first, and RS turns the logging of key events on and off.
#
# One character every 0.05 s: the UART's receive FIFO holds 32 bytes, and
# the shell reads it between commands, so a faster sender can lose
# characters while a command runs. UART_TYPE_DELAY overrides it.
#
# Writing while uart-log.sh reads is fine: that script refuses a second
# *reader*; this only writes. The port's settings are the ones uart-log.sh
# applied, so start the capture first.
set -euo pipefail

text="${1:?usage: uart-type.sh TEXT}"
delay="${UART_TYPE_DELAY:-0.05}"

dev="${UART_DEV:-}"
if [ -z "$dev" ]; then
    shopt -s nullglob
    cands=(/dev/cu.usbmodem* /dev/ttyACM*)
    if [ ${#cands[@]} -ne 1 ]; then
        echo "uart-type.sh: found ${#cands[@]} candidate ports (${cands[*]:-none}); set UART_DEV" >&2
        exit 1
    fi
    dev="${cands[0]}"
fi

# printf interprets \r and \n in TEXT, so a caller can write either.
text="$(printf '%b' "$text"; printf x)"
text="${text%x}"

exec 4>"$dev"
for ((i = 0; i < ${#text}; i++)); do
    printf '%s' "${text:$i:1}" >&4
    sleep "$delay"
done
exec 4>&-
