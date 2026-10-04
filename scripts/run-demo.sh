#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 scripts/run-tests.py
