#!/usr/bin/env bash
# swd-ram.sh — run the shell from SRAM over SWD, without the card or the
# loader (design.md §6; hardware-notes.md §2.7).
#
#   tools/uart-log.sh 30 out/run.log &
#   tools/swd-ram.sh                      # build/pico/BOOT2350.elf
#   tools/swd-ram.sh build/pico-release/BOOT2350.elf
#
# The shell is a no_flash image: everything it runs is in SRAM. This
# halts both cores, writes the image into SRAM and verifies it, then
# reboots by the watchdog with the bootrom's "boot to PC and SP" words set
# (scratch 4-7, as the SDK's watchdog_reboot(pc, sp, 0) sets them). The
# bootrom does its whole start-up, which the image needs (starting it from
# a debugger's reset halt instead skips the bootrom's enabling of the
# coprocessors, and the SDK's runtime init then faults), and jumps to the
# image's entry point. Nothing is written to flash, so a power cycle gives
# back whatever the flash held. What it does not test is the boot chain:
# stage3 loading /BOOT2350.UF2 from the card (tools/swd-shell.sh does, on
# a board with the loader). The start time it reports counts from this
# reset, so it does not include stage3's.
#
# OpenOCD is found as tools/swd-shell.sh finds it.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
image="${1:-$root/build/pico/BOOT2350.elf}"
if [ ! -f "$image" ]; then
    echo "swd-ram.sh: no image at $image — build the firmware first (CLAUDE.md)" >&2
    exit 1
fi

openocd="${OPENOCD:-}"
if [ -z "$openocd" ]; then
    if command -v openocd >/dev/null 2>&1; then
        openocd="$(command -v openocd)"
    else
        openocd="$(ls -d "$HOME"/.pico-sdk/openocd/*/openocd 2>/dev/null | sort -V | tail -n1 || true)"
    fi
fi
if [ -z "$openocd" ] || [ ! -x "$openocd" ]; then
    echo "swd-ram.sh: OpenOCD not found; install the Raspberry Pi fork or set OPENOCD" >&2
    exit 1
fi
scripts=()
if [ -d "$(dirname "$openocd")/scripts" ]; then
    scripts=(-s "$(dirname "$openocd")/scripts")
fi

# Refuse anything that is not wholly in SRAM, or load_image would try to
# write flash: every loadable segment's physical address is 0x2xxxxxxx.
if command -v arm-none-eabi-readelf >/dev/null 2>&1; then
    if arm-none-eabi-readelf -lW "$image" | awk '$1 == "LOAD" && $4 !~ /^0x2/ {bad=1} END {exit !bad}'; then
        echo "swd-ram.sh: $image loads outside SRAM; it is not a no_flash image" >&2
        exit 1
    fi
else
    echo "swd-ram.sh: arm-none-eabi-readelf not on PATH; cannot check the image is SRAM only" >&2
    exit 1
fi

WATCHDOG=0x400d8000        # CTRL at +0, SCRATCH0 at +0xc
PSM_WDSEL=0x40018008
entry=$(arm-none-eabi-readelf -h "$image" | awk '/Entry point/ {print $4}')
sp=0x$(arm-none-eabi-nm "$image" | awk '$3 == "__StackTop" {print $1}')
pc=$(( entry | 1 ))
key=$(( pc ^ ((-0xb007c0d3) & 0xffffffff) ))

echo "swd-ram.sh: $(basename "$image") -> SRAM, entry $entry, via $openocd"
# The trigger is last: the reset cuts its own write short, which OpenOCD
# reports as an error.
"$openocd" "${scripts[@]}" \
    -f interface/cmsis-dap.cfg \
    -f target/rp2350.cfg \
    -c "adapter speed ${SWD_KHZ:-5000}" \
    -c "init" \
    -c "halt" \
    -c "targets rp2350.cm0" \
    -c "load_image \"$image\"" \
    -c "verify_image \"$image\"" \
    -c "write_memory $WATCHDOG 32 {0}" \
    -c "write_memory $((WATCHDOG + 0x1c)) 32 {0xb007c0d3 $key $sp $pc}" \
    -c "write_memory $PSM_WDSEL 32 {0x01fffff3}" \
    -c "echo started" \
    -c "write_memory $WATCHDOG 32 {0x80000000}" \
    -c "shutdown" 2>&1 | grep -iE 'error|invalid|fail|verified|downloaded|started' |
    grep -v 'Failed to write memory at 0x00000000\|failed to write memory' || true
