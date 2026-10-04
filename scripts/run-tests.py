#!/usr/bin/env python3
"""Run native device tests and independently booted guest scenarios."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def run(name, command, expected=(), timeout=30):
    artifact = ROOT / 'artifacts' / name
    artifact.mkdir(parents=True, exist_ok=True)
    record = {'name': name, 'command': command}
    try:
        env = dict(os.environ, QTEST_QEMU_BINARY=str(ROOT/'build/qemu/qemu-system-riscv32'))
        proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout, env=env)
        (artifact/'stdout.log').write_text(proc.stdout)
        (artifact/'stderr.log').write_text(proc.stderr)
        record['exit_code'] = proc.returncode
        record['passed'] = proc.returncode == 0 and all(marker in proc.stdout for marker in expected)
        print(proc.stdout, end='')
        if not record['passed']:
            print(proc.stderr, file=sys.stderr)
    except subprocess.TimeoutExpired as exc:
        record.update(passed=False, reason='host watchdog expired')
        (artifact/'stdout.log').write_bytes(exc.stdout or b'')
        (artifact/'stderr.log').write_bytes(exc.stderr or b'')
    print(('PASS ' if record['passed'] else 'FAIL ') + name)
    return record


def guest(name, machine, extra=(), expected=(), trace=False):
    binary = str(ROOT/'build/qemu/qemu-system-riscv32')
    cmd = [binary, '-M', machine, '-cpu', 'rv32', '-smp', '1', '-m', '128M',
           '-bios', 'none', '-nographic', '-monitor', 'none',
           '-icount', 'shift=0,align=off,sleep=off',
           '-kernel', str(ROOT/f'build/firmware/{name}.elf')]
    cmd.extend(extra)
    if trace:
        (ROOT/'artifacts'/name).mkdir(parents=True, exist_ok=True)
        cmd += ['-trace', f'enable=virtual_dma_*,file={ROOT}/artifacts/{name}/model.trace']
    return run(name, cmd, expected)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage', choices=['boot', 'detect', 'polling', 'irq', 'all'], default='all')
    args = parser.parse_args()
    records = [guest('boot', 'virt,aia=none', expected=['PASS timer\nPASS boot\n'])]
    if args.stage != 'boot':
        records.append(run('platform', [sys.executable, str(ROOT/'scripts/check-platform.py')], expected=['PASS platform DTB']))
        qcmd = [str(ROOT/'build/qemu/tests/qtest/virtual-dma-test')]
        if args.stage == 'detect':
            qcmd += ['-p', '/riscv32/virtual-dma/registers']
        records.append(run('qtest', qcmd, expected=['ok 1 /riscv32/virtual-dma/'], timeout=120))
        records.append(guest('detect', 'virt,dma=on,aia=none', expected=['PASS DMA detection\n']))
    if args.stage in ('polling', 'irq', 'all'):
        records.append(guest('polling', 'virt,dma=on,aia=none', expected=['PASS polling suite\n'], trace=True))
    if args.stage in ('irq', 'all'):
        records.append(guest('interrupt', 'virt,dma=on,aia=none', expected=['PASS interrupt suite\n'], trace=True))
    if args.stage == 'all':
        for fault, flag in [('stall', 'stall-next'), ('dropirq', 'drop-irq-next')]:
            records.append(guest(fault, 'virt,dma=on,aia=none',
                ['-global', f'virtual-dma.{flag}=on'],
                [f'PASS {fault} timeout\n', f'PASS {fault} recovery\n'], trace=True))
    (ROOT/'artifacts/summary.json').write_text(json.dumps(records, indent=2)+'\n')
    return 0 if all(record['passed'] for record in records) else 1

if __name__ == '__main__':
    sys.exit(main())
