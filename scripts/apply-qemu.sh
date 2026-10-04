#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./scripts/fetch-qemu.sh
src=build/qemu-src
for patch in qemu/patches/*.patch; do
    if git -C "$src" apply --check "$PWD/$patch" 2>/dev/null; then
        git -C "$src" apply "$PWD/$patch"
    elif ! git -C "$src" apply --reverse --check "$PWD/$patch" 2>/dev/null; then
        echo "Patch cannot be applied or verified: $patch" >&2
        exit 1
    fi
done
cp model/virtual_dma.c "$src/hw/dma/"
cp model/virtual_dma.h platform/dma_regs.h platform/platform.h "$src/include/hw/dma/"
cp tests/qtest/virtual-dma-test.c "$src/tests/qtest/"
python3 - <<'TRACE'
from pathlib import Path
p = Path('build/qemu-src/hw/dma/trace-events')
t = p.read_text().split('# virtual_dma.c')[0].rstrip() + '\n\n'
p.write_text(t + Path('model/trace-events').read_text())
TRACE
