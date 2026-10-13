#!/usr/bin/env python3
"""Compatibility entry point; share the same failure handling as every scenario."""

import os
import sys
from pathlib import Path

runner = Path(__file__).with_name("run-tests.py")
os.execv(sys.executable, [sys.executable, str(runner), "--stage", "boot"])
