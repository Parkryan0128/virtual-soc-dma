#!/usr/bin/env python3
"""Cross-compile every scenario declared in the shared manifest."""
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
from firmware_scenarios import load_scenarios

ROOT = Path(__file__).resolve().parents[1]


def main():
    output = ROOT / 'build/firmware'
    # Invalidate old and removed scenarios before validating or compiling.
    # A failed build must leave no runnable image from a previous source tree.
    if output.exists():
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    scenarios = load_scenarios()
    common = [os.environ.get('RISCV_CC', 'riscv64-unknown-elf-gcc'),
              '-march=rv32im_zicsr', '-mabi=ilp32', '-mcmodel=medany', '-msmall-data-limit=0',
              '-O2', '-g', '-ffreestanding', '-fno-builtin', '-nostdlib', '-Wall', '-Wextra', '-Werror',
              '-Iplatform', '-Ifirmware/drivers', '-T', 'firmware/linker.ld',
              'firmware/startup.S', 'firmware/traps.S', 'firmware/drivers/dma.c',
              'firmware/drivers/platform_io.c', '-Wl,--build-id=none']
    # Publish a complete set in one rename, never a mixture of old/new images.
    with tempfile.TemporaryDirectory(prefix='.firmware-', dir=output.parent) as temporary:
        staging = Path(temporary)
        for scenario in scenarios:
            command = common + ['-D' + d for d in scenario.get('defines', [])]
            command += ['firmware/tests/' + scenario['source'], '-o', str(staging / (scenario['name'] + '.elf'))]
            subprocess.run(command, cwd=ROOT, check=True)
        staging.rename(output)


if __name__ == '__main__':
    main()
