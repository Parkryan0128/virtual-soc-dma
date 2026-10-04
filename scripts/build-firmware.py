#!/usr/bin/env python3
"""Cross-compile every scenario declared in the shared manifest."""
import os
from pathlib import Path
import subprocess
from firmware_scenarios import load_scenarios

ROOT = Path(__file__).resolve().parents[1]


def main():
    scenarios = load_scenarios()
    (ROOT / 'build/firmware').mkdir(parents=True, exist_ok=True)
    common = [os.environ.get('RISCV_CC', 'riscv64-unknown-elf-gcc'),
              '-march=rv32im_zicsr', '-mabi=ilp32', '-mcmodel=medany', '-msmall-data-limit=0',
              '-O2', '-g', '-ffreestanding', '-fno-builtin', '-nostdlib', '-Wall', '-Wextra', '-Werror',
              '-Iplatform', '-Ifirmware/drivers', '-T', 'firmware/linker.ld',
              'firmware/startup.S', 'firmware/traps.S', 'firmware/drivers/dma.c',
              'firmware/drivers/platform_io.c', '-Wl,--build-id=none']
    for scenario in scenarios:
        command = common + ['-D' + d for d in scenario.get('defines', [])]
        command += ['firmware/tests/' + scenario['source'], '-o', f'build/firmware/{scenario["name"]}.elf']
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
