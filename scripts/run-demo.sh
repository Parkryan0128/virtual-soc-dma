#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "${1:-}" = --docker ]; then
    exec ./scripts/test-in-container.sh demo
fi
exec python3 scripts/run-tests.py --demo
