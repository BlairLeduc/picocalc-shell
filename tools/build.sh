#!/usr/bin/env bash
# build.sh — build the firmware from the current source (CLAUDE.md), and
# say where the .uf2 is. Copied from the emulators.
#
#   tools/build.sh                         # build/pico/BOOT2350.uf2
#   tools/build.sh -DPICO_SHELL_UART=OFF build/pico-release   # the one that ships
#   tools/build.sh -DPICO_BOARD=pico2_w build/pico-w   # the W build, and ntp.uf2
#
# Arguments starting -D are passed to CMake's configure step; another
# argument names the build directory, which is configured first if it has
# never been. Build each such variant in its own directory
# (EL §13.1). The Pico SDK and the Arm toolchain are the
# newest under the Pico VS Code extension's ~/.pico-sdk/, unless
# PICO_SDK_PATH names one that exists: that variable is often left
# pointing at a version since removed.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
dir=
defs=()
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -D*) defs+=("$1"); shift ;;
        -*) echo "build.sh: unknown option $1" >&2; exit 2 ;;
        *) dir="$1"; shift ;;
    esac
done
dir="${dir:-$root/build/pico}"

newest() { ls -d "$1"/*/ 2>/dev/null | sort -V | tail -n1 | sed 's:/$::'; }

if [ -z "${PICO_SDK_PATH:-}" ] || [ ! -d "$PICO_SDK_PATH" ]; then
    PICO_SDK_PATH="$(newest "$HOME/.pico-sdk/sdk")"
fi
if [ -z "$PICO_SDK_PATH" ]; then
    echo "build.sh: no Pico SDK; install one or set PICO_SDK_PATH" >&2
    exit 1
fi
export PICO_SDK_PATH

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    tc="$(newest "$HOME/.pico-sdk/toolchain")"
    if [ -z "$tc" ] || [ ! -x "$tc/bin/arm-none-eabi-gcc" ]; then
        echo "build.sh: arm-none-eabi-gcc not found; put it on PATH" >&2
        exit 1
    fi
    export PATH="$tc/bin:$PATH"
fi
toolchain="$(dirname "$(dirname "$(command -v arm-none-eabi-gcc)")")"

echo "build.sh: $PICO_SDK_PATH, $(basename "$toolchain") -> $dir"

# A cache made with another SDK or compiler keeps them: CMake prefers the
# cached PICO_SDK_PATH to the environment's and will not switch compilers
# in place. Start that cache afresh, keeping the variant's PICO_SHELL_*
# options and its board, so a directory configured before an SDK update
# follows it.
cache="$dir/CMakeCache.txt"
fresh=()
if [ -f "$cache" ]; then
    cached() { sed -n "s/^$1:[A-Z]*=//p" "$cache"; }
    if [ "$(cached PICO_SDK_PATH)" != "$PICO_SDK_PATH" ] ||
       [ "$(cached CMAKE_C_COMPILER)" != "$toolchain/bin/arm-none-eabi-gcc" ]; then
        echo "build.sh: $dir was configured with another SDK or toolchain; reconfiguring"
        fresh=(--fresh)
        while IFS= read -r opt; do
            defs=("-D$opt" ${defs[@]+"${defs[@]}"})
        done < <(sed -nE 's/^(PICO_SHELL_[A-Z0-9_]*|PICO_BOARD):[A-Z]*=/\1=/p' "$cache")
    fi
fi
if [ ! -f "$cache" ] || [ ${#fresh[@]} -gt 0 ] || [ ${#defs[@]} -gt 0 ]; then
    cmake ${fresh[@]+"${fresh[@]}"} -S "$root" -B "$dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_TOOLCHAIN_PATH="$toolchain" \
        ${defs[@]+"${defs[@]}"}
fi
cmake --build "$dir" -j

# The SDK writes the UF2 only when it links the ELF, so a UF2 moved or
# deleted since (copied to the card with mv, say) is not made again by a
# build with nothing to do: relink.
if [ ! -f "$dir/BOOT2350.uf2" ]; then
    echo "build.sh: $dir/BOOT2350.uf2 is missing; relinking"
    rm -f "$dir/BOOT2350.elf"
    cmake --build "$dir" -j --target shell
fi
if [ ! -f "$dir/BOOT2350.uf2" ]; then
    echo "build.sh: no $dir/BOOT2350.uf2 was made" >&2
    exit 1
fi

arm-none-eabi-size "$dir/BOOT2350.elf"
echo "build.sh: $dir/BOOT2350.uf2, to be copied to the card as /BOOT2350.UF2"
if [ -f "$dir/ntp.uf2" ]; then
    arm-none-eabi-size "$dir/ntp.elf"
    echo "build.sh: $dir/ntp.uf2, to be copied to the card as /pico2-apps/ntp.uf2"
fi
