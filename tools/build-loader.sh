#!/usr/bin/env bash
# build-loader.sh — build UF2 Loader's stage3 with M7's patch (design.md
# §10, M7), and say where the .uf2 is.
#
#   tools/build-loader.sh            # build/loader/output/bootloader_pico2.uf2
#   tools/build-loader.sh --stock    # stock 2.5, in build/loader-stock, to compare
#   PICO_BOARD=pimoroni_pico_plus2_rp2350 tools/build-loader.sh   # another board
#
# The loader is the submodule third_party/uf2loader, pinned at v2.5. It is
# copied, patches/uf2loader-boot-shell.patch is applied to the copy, and
# only stage3 is built, for pico2 unless PICO_BOARD names another (the
# board sets the flash size, and so the app partition), against the
# newest Pico SDK and toolchain under ~/.pico-sdk/ (as tools/build.sh
# finds them). The owner installs the result in BOOTSEL; nothing here
# writes to a board.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/third_party/uf2loader"
patch_file="$root/patches/uf2loader-boot-shell.patch"
stock=
case "${1:-}" in
    -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --stock) stock=1 ;;
    "") ;;
    *) echo "build-loader.sh: unknown argument $1" >&2; exit 2 ;;
esac
board="${PICO_BOARD:-pico2}"
dir="$root/build/loader${stock:+-stock}"
[ "$board" = pico2 ] || dir="$dir-$board"

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

# The output goes to $dir/output, the loader's CMAKE_ARCHIVE_OUTPUT_DIRECTORY.
cmake -S "$dir/src" -B "$dir/build" -DPICO_BOARD="$board" \
    -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_TOOLCHAIN_PATH="$toolchain" >/dev/null
cmake --build "$dir/build" -j --target stage3 | grep -A5 'Memory region'

uf2="$dir/output/bootloader_$board.uf2"
if [ ! -f "$uf2" ]; then
    echo "build-loader.sh: no $uf2 was made" >&2
    exit 1
fi
echo "build-loader.sh: $uf2, to be installed in BOOTSEL"
