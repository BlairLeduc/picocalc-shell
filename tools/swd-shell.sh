#!/usr/bin/env bash
# swd-shell.sh — reboot the PicoCalc into the shell over SWD, with nobody
# holding Up at power-on (design.md §3.4; hardware-notes.md §2.7).
#
#   tools/uart-log.sh 30 out/run.log &
#   tools/swd-shell.sh
#
# It does what picocalc_shell_exit() will: it sets the UF2 Loader's BOOT_SD
# command in the watchdog scratch registers, then a watchdog reboot, and
# stage3 loads /BOOT2350.UF2 from the card into SRAM. The registers are
# written while the target runs; nothing is halted and flash is not
# touched. The protocol is the loader's (common/bootloader/proginfo.c):
# scratch[1] the mode, scratch[2] its argument, scratch[0] the magic
# written last. src/port/loader.c holds the firmware's copy of it, and
# include/picocalc_shell.h the programs'.
# The reboot is the SDK's watchdog_reboot(0, 0, 0): reset everything but
# the oscillators, then trigger.
#
# OpenOCD is found as tools/build.sh finds the SDK: on PATH, or the newest
# under ~/.pico-sdk/openocd/, or OPENOCD.
set -euo pipefail

openocd="${OPENOCD:-}"
if [ -z "$openocd" ]; then
    if command -v openocd >/dev/null 2>&1; then
        openocd="$(command -v openocd)"
    else
        openocd="$(ls -d "$HOME"/.pico-sdk/openocd/*/openocd 2>/dev/null | sort -V | tail -n1 || true)"
    fi
fi
if [ -z "$openocd" ] || [ ! -x "$openocd" ]; then
    echo "swd-shell.sh: OpenOCD not found; install the Raspberry Pi fork or set OPENOCD" >&2
    exit 1
fi
scripts=()
if [ -d "$(dirname "$openocd")/scripts" ]; then
    scripts=(-s "$(dirname "$openocd")/scripts")
fi

WATCHDOG=0x400d8000        # CTRL at +0, SCRATCH0 at +0xc
PSM_WDSEL=0x40018008
MAGIC=0xe98cc638           # PICOCALC_BL_MAGIC
BOOT_SD=1

"$openocd" "${scripts[@]}" \
    -f interface/cmsis-dap.cfg \
    -f target/rp2350.cfg \
    -c "adapter speed ${SWD_KHZ:-5000}" \
    -c "init" \
    -c "write_memory $WATCHDOG 32 {0}" \
    -c "write_memory $((WATCHDOG + 0x1c)) 32 {0}" \
    -c "write_memory $((WATCHDOG + 0x10)) 32 {$BOOT_SD}" \
    -c "write_memory $((WATCHDOG + 0x14)) 32 {0}" \
    -c "write_memory $((WATCHDOG + 0x0c)) 32 {$MAGIC}" \
    -c "write_memory $PSM_WDSEL 32 {0x01fffff3}" \
    -c "write_memory $WATCHDOG 32 {0x80000000}" \
    -c "shutdown" 2>&1 | grep -E '^Error' >&2 || true
echo "swd-shell.sh: BOOT_SD set and the watchdog triggered; stage3 loads /BOOT2350.UF2"
