#!/usr/bin/env python3
"""Validate device contracts and boot real firmware; retain evidence on failure."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = ROOT / 'tests/scenarios/firmware.json'
STAGES = ['boot', 'detect', 'polling', 'irq', 'all']


def run(name, command, expected=(), timeout=30, quiet=False):
    artifact = ROOT / 'artifacts' / name
    artifact.mkdir(parents=True, exist_ok=True)
    record = {'name': name, 'command': command}
    try:
        env = dict(os.environ, QTEST_QEMU_BINARY=str(ROOT / 'build/qemu/qemu-system-riscv32'))
        proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout, env=env)
        (artifact / 'stdout.log').write_text(proc.stdout)
        (artifact / 'stderr.log').write_text(proc.stderr)
        record['exit_code'] = proc.returncode
        record['passed'] = proc.returncode == 0 and all(marker in proc.stdout for marker in expected)
        if not quiet or not record['passed']:
            print(proc.stdout, end='')
        if not record['passed']:
            record['reason'] = f'exit {proc.returncode} or missing required evidence'
            print(proc.stderr, file=sys.stderr)
    except subprocess.TimeoutExpired as exc:
        record.update(passed=False, reason='host watchdog expired')
        (artifact / 'stdout.log').write_bytes(exc.stdout or b'')
        (artifact / 'stderr.log').write_bytes(exc.stderr or b'')
    except OSError as exc:
        record.update(passed=False, reason=str(exc))
        (artifact / 'stderr.log').write_text(str(exc) + '\n')
    if not quiet:
        print(('PASS ' if record['passed'] else 'FAIL ') + name)
    return record


def guest(scenario, demo=False):
    name = scenario['name']
    binary = str(ROOT / 'build/qemu/qemu-system-riscv32')
    command = [binary, '-M', scenario.get('machine', 'virt,dma=on,aia=none'),
               '-cpu', 'rv32', '-smp', '1', '-m', '128M', '-bios', 'none',
               '-nographic', '-monitor', 'none', '-icount', 'shift=0,align=off,sleep=off',
               '-kernel', str(ROOT / f'build/firmware/{name}.elf')]
    command.extend(scenario.get('options', []))
    if scenario.get('trace'):
        (ROOT / 'artifacts' / name).mkdir(parents=True, exist_ok=True)
        command += ['-trace', f'enable=virtual_dma_*,file={ROOT}/artifacts/{name}/model.trace']
    return run(name, command, scenario['expected'], quiet=demo)


def main():
    parser = argparse.ArgumentParser()
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--stage', choices=STAGES)
    selection.add_argument('--scenario', help='run one firmware scenario from the manifest')
    selection.add_argument('--demo', action='store_true', help='short firmware demonstration')
    args = parser.parse_args()
    scenarios = json.loads(SCENARIOS.read_text())
    stage = args.stage or 'all'
    records = []
    if args.scenario:
        scenarios = [s for s in scenarios if s['name'] == args.scenario]
        if not scenarios:
            parser.error('unknown scenario')
    elif args.demo:
        scenarios = [s for s in scenarios if s.get('demo')]
    else:
        scenarios = [s for s in scenarios if STAGES.index(s['stage']) <= STAGES.index(stage)]
        if stage == 'all':
            records.append(run('runner', [sys.executable, '-m', 'unittest', 'discover', '-s', str(ROOT / 'tests'), '-p', 'test_runner.py', '-v']))
        if stage != 'boot':
            records.append(run('platform', [sys.executable, str(ROOT / 'scripts/check-platform.py')],
                               expected=['PASS platform DTB']))
            command = [str(ROOT / 'build/qemu/tests/qtest/virtual-dma-test')]
            expected = ['1..10', 'ok 10 /riscv32/virtual-dma/dropirq_error']
            if stage == 'detect':
                command += ['-p', '/riscv32/virtual-dma/registers']
                expected = ['ok 1 /riscv32/virtual-dma/registers']
            records.append(run('qtest', command, expected, timeout=120))
    if args.demo:
        print('Virtual SoC firmware demonstration')
    for scenario in scenarios:
        result = guest(scenario, demo=args.demo)
        records.append(result)
        if args.demo and result['passed']:
            print((ROOT / 'artifacts' / scenario['name'] / 'stdout.log').read_text(), end='')
    passed = all(record['passed'] for record in records)
    kind = 'demo' if args.demo else args.scenario or stage
    filename = f'{kind}-summary.json' if args.demo or args.scenario else 'summary.json'
    (ROOT / 'artifacts').mkdir(exist_ok=True)
    (ROOT / 'artifacts' / filename).write_text(json.dumps(records, indent=2) + '\n')
    print(f'{"PASS" if passed else "FAIL"} {kind}: '
          f'{sum(record["passed"] for record in records)}/{len(records)} groups')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
