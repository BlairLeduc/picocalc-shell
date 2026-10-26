#!/usr/bin/env bash
# build-loader.sh — build UF2 Loader's stage3 with M7's patch (design.md
# §10, M7), one build per flash size, and say where the .uf2s are.
#
#   tools/build-loader.sh            # build/loader/output/bootloader_rp2350_{4,8,16}mb.uf2
#   tools/build-loader.sh 16         # only the 16 MB build
#   tools/build-loader.sh --stock    # stock 2.5, in build/loader-stock, to compare
#
# stage3 sizes the app partition from the flash size when it is built, so
# a build is for one size: one for a larger flash must never go on a
# smaller one. Each is the Pico 2's board with another flash size
# (patches/boards/rp2350_<n>mb.h). The loader is the submodule
# third_party/uf2loader, pinned at v2.5. It is copied,
# patches/uf2loader-boot-shell.patch is applied to the copy, and only
# stage3 is built, against the newest Pico SDK and toolchain under
# ~/.pico-sdk/ (as tools/build.sh finds them). The owner installs the
# result in BOOTSEL; nothing here writes to a board.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/third_party/uf2loader"
patch_file="$root/patches/uf2loader-boot-shell.patch"
boards="$root/patches/boards"
stock=
sizes=()
for arg in "$@"; do
    case "$arg" in
        -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        --stock) stock=1 ;;
        4|8|16) sizes+=("$arg") ;;
        *) echo "build-loader.sh: unknown argument $arg (sizes: 4, 8, 16)" >&2; exit 2 ;;
    esac
done
[ ${#sizes[@]} -gt 0 ] || sizes=(4 8 16)
dir="$root/build/loader${stock:+-stock}"

if [ ! -f "$src/stage3/stage3.c" ]; then
    echo "build-loader.sh: no loader; run git submodule update --init third_party/uf2loader" >&2
    exit 1
fi

newest() { ls -d "$1"/*/ 2>/dev/null | sort -V | tail -n1 | sed 's:/$::'; }

if [ -z "${PICO_SDK_PATH:-}" ] || [ ! -d "$PICO_SDK_PATH" ]; then
    PICO_SDK_PATH="$(newest "$HOME/.pico-sdk/sdk")"
fi
if [ -z "$PICO_SDK_PATH" ]; then
    echo "build-loader.sh: no Pico SDK; install one or set PICO_SDK_PATH" >&2
    exit 1
fi
export PICO_SDK_PATH

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    tc="$(newest "$HOME/.pico-sdk/toolchain")"
    if [ -z "$tc" ] || [ ! -x "$tc/bin/arm-none-eabi-gcc" ]; then
        echo "build-loader.sh: arm-none-eabi-gcc not found; put it on PATH" >&2
        exit 1
    fi
    export PATH="$tc/bin:$PATH"
fi
toolchain="$(dirname "$(dirname "$(command -v arm-none-eabi-gcc)")")"

echo "build-loader.sh: $PICO_SDK_PATH, $(basename "$toolchain") -> $dir"

# A fresh copy each time, so the patch always applies to stock 2.5.
rm -rf "$dir"
mkdir -p "$dir"
rsync -a --exclude .git "$src/" "$dir/src/"
if [ -z "$stock" ]; then
    patch -s -p1 -d "$dir/src" < "$patch_file"
fi

# Each build's output goes to $dir/output, the loader's
# CMAKE_ARCHIVE_OUTPUT_DIRECTORY (its build directory's ../output).
for mb in "${sizes[@]}"; do
    board="rp2350_${mb}mb"
    cmake -S "$dir/src" -B "$dir/build-$board" -DPICO_BOARD="$board" \
        -DPICO_BOARD_HEADER_DIRS="$boards" \
        -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_TOOLCHAIN_PATH="$toolchain" 2>&1 |
        grep 'part_size_kb'
    cmake --build "$dir/build-$board" -j --target stage3 | grep -A5 'Memory region'

    uf2="$dir/output/bootloader_$board.uf2"
    if [ ! -f "$uf2" ]; then
        echo "build-loader.sh: no $uf2 was made" >&2
        exit 1
    fi
    echo "build-loader.sh: $uf2, for $mb MB of flash, to be installed in BOOTSEL"
done
