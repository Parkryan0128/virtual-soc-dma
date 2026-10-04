#!/usr/bin/env python3
import pathlib, subprocess
root = pathlib.Path(__file__).resolve().parents[1]
cmd = [str(root/'build/qemu/qemu-system-riscv32'), '-M', 'virt,aia=none', '-cpu', 'rv32', '-smp', '1', '-m', '128M', '-bios', 'none', '-nographic', '-monitor', 'none', '-icount', 'shift=0,align=off,sleep=off', '-kernel', str(root/'build/firmware/boot.elf')]
result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
(root/'artifacts').mkdir(exist_ok=True)
(root/'artifacts/boot.log').write_text(result.stdout+result.stderr)
print(result.stdout, end='')
assert result.returncode == 0, result.stderr
assert 'PASS timer\nPASS boot\n' in result.stdout
