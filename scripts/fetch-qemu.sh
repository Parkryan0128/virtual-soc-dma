#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
revision=$(sed -n '2p' qemu/base-version.txt)
mkdir -p build
if [ ! -d build/qemu-src/.git ]; then
    git init build/qemu-src
    git -C build/qemu-src remote add origin https://github.com/qemu/qemu.git
    git -C build/qemu-src fetch --depth 1 origin "$revision"
    git -C build/qemu-src checkout --detach FETCH_HEAD
fi
[ "$(git -C build/qemu-src rev-parse HEAD)" = "$revision" ] || { echo 'Wrong QEMU revision' >&2; exit 1; }
