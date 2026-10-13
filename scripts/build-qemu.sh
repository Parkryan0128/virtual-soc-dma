#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root=$PWD
complete=false
cleanup() {
    if [ "$complete" = false ]; then
        rm -f "$root/build/qemu/qemu-system-riscv32" \
              "$root/build/qemu/tests/qtest/virtual-dma-test"
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# Invalidate the old pair before patching; failed/partial builds cannot be tested.
cleanup
./scripts/apply-qemu.sh
mkdir -p build/qemu
cd build/qemu
if [ ! -f build.ninja ]; then
    ../qemu-src/configure --target-list=riscv32-softmmu --disable-docs --disable-werror \
        --disable-gtk --disable-sdl --disable-vnc --disable-curses
fi
ninja -j "${JOBS:-4}" qemu-system-riscv32 tests/qtest/virtual-dma-test
complete=true
