#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./scripts/apply-qemu.sh
mkdir -p build/qemu
cd build/qemu
if [ ! -f build.ninja ]; then
    ../qemu-src/configure --target-list=riscv32-softmmu --disable-docs --disable-werror \
        --disable-gtk --disable-sdl --disable-vnc --disable-curses
fi
ninja -j "${JOBS:-4}" qemu-system-riscv32 tests/qtest/virtual-dma-test
