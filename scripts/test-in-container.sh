#!/bin/sh
# Build in an isolated Linux container without requiring a host bind mount.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/buildx artifacts
image=virtual-soc-dma-dev:local
BUILDX_CONFIG="$PWD/build/buildx" docker build -t "$image" .
container="virtual-soc-dma-test-$$"
trap 'docker rm -f "$container" >/dev/null 2>&1 || true' EXIT INT TERM
# Copy only project source; no previous QEMU checkout, objects, or test results.
tar --exclude='./build' --exclude='./artifacts' --exclude='./.git' -cf build/source.tar .
docker create --name "$container" "$image" tail -f /dev/null >/dev/null
docker start "$container" >/dev/null
docker cp - "$container:/src" < build/source.tar
docker exec "$container" chown -R root:root /src
status=0
docker exec "$container" sh -c 'mkdir -p artifacts; ./scripts/build-firmware.sh > artifacts/build.log 2>&1 && ./scripts/build-qemu.sh >> artifacts/build.log 2>&1 && python3 scripts/run-tests.py' || status=$?
docker cp "$container:/src/artifacts/." artifacts/
if [ "$status" -ne 0 ]; then tail -n 80 artifacts/build.log; fi
exit "$status"
