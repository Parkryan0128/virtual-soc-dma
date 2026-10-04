#!/usr/bin/env python3
"""Validate device contracts and boot real firmware; retain evidence on failure."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from firmware_scenarios import STAGES, load_scenarios, select_scenarios

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = ROOT / 'tests/scenarios/firmware.json'

def tap_failure(output):
    """Require a complete nonempty TAP plan; reject skipped/failed cases."""
    plans = re.findall(r'^1\.\.(\d+)$', output, re.MULTILINE)
    cases = re.findall(r'^(not ok|ok) (\d+) (/\S+)(.*)$', output, re.MULTILINE)
    if len(plans) != 1 or int(plans[0]) == 0:
        return 'missing or empty TAP plan'
    count = int(plans[0])
    if len(cases) != count or [int(c[1]) for c in cases] != list(range(1, count + 1)):
        return 'incomplete or duplicate TAP results'
    if len({c[2] for c in cases}) != count:
        return 'duplicate TAP test name'
    if any(c[0] != 'ok' or re.search(r'#\s*(SKIP|TODO)', c[3], re.I) for c in cases):
        return 'failed or skipped TAP test'
    if re.search(r'^Bail out!', output, re.MULTILINE):
        return 'TAP bailout'
    return None


def stop_process_group(process):
    # QTest and platform checks spawn QEMU. Killing only their parent leaks it.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def run(name, command, expected=(), timeout=30, quiet=False, evidence=None, artifact_root=None):
    artifact = (artifact_root or ROOT / 'artifacts') / name
    if artifact.exists():
        shutil.rmtree(artifact)
    artifact.mkdir(parents=True, exist_ok=True)
    record = {'name': name, 'command': command, 'artifact_dir': str(artifact), 'passed': False}
    stdout, stderr = b'', b''
    try:
        env = dict(os.environ, QTEST_QEMU_BINARY=str(ROOT / 'build/qemu/qemu-system-riscv32'))
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              start_new_session=True, env=env) as proc:
            try:
                stdout, stderr = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                stop_process_group(proc)
                stdout, stderr = proc.communicate()
                record['reason'] = 'host watchdog expired'
            except BaseException:
                stop_process_group(proc)
                proc.communicate()
                raise
            record['exit_code'] = proc.returncode
    except OSError as exc:
        record['reason'] = str(exc)
        stderr = (str(exc) + '\n').encode()
    (artifact / 'stdout.log').write_bytes(stdout)
    (artifact / 'stderr.log').write_bytes(stderr)
    try:
        output = stdout.decode('utf-8')
    except UnicodeDecodeError:
        output = stdout.decode('utf-8', errors='replace')
        record.setdefault('reason', 'invalid UTF-8 output')
    if 'reason' not in record:
        missing = [m for m in expected if '\n' + m.strip('\n') + '\n' not in '\n' + output.rstrip('\n') + '\n']
        if record['exit_code'] != 0:
            record['reason'] = f'exit {record["exit_code"]}'
        elif re.search(r'^FAIL(?:\s|$)', output, re.MULTILINE):
            record['reason'] = 'explicit failure marker'
        elif missing:
            record['reason'] = 'missing required evidence: ' + repr(missing)
        elif evidence == 'tap' and (failure := tap_failure(output)):
            record['reason'] = failure
        elif evidence == 'unittest' and not re.search(rb'Ran [1-9]\d* tests?\b[\s\S]*\nOK\s*$', stderr):
            record['reason'] = 'missing successful host test results'
    record['passed'] = 'reason' not in record
    if not quiet or not record['passed']:
        print(output, end='')
    if not record['passed']:
        print(record['reason'], file=sys.stderr)
        print(stderr.decode('utf-8', errors='replace'), file=sys.stderr)
    if not quiet:
        print(('PASS ' if record['passed'] else 'FAIL ') + name)
    return record


def guest(scenario, demo=False, artifact_root=None):
    name = scenario['name']
    artifact_root = artifact_root or ROOT / 'artifacts'
    binary = str(ROOT / 'build/qemu/qemu-system-riscv32')
    command = [binary, '-M', scenario.get('machine', 'virt,dma=on,aia=none'),
               '-cpu', 'rv32', '-smp', '1', '-m', '128M', '-bios', 'none',
               '-nographic', '-monitor', 'none', '-icount', 'shift=0,align=off,sleep=off',
               '-kernel', str(ROOT / f'build/firmware/{name}.elf')]
    command.extend(scenario.get('options', []))
    if scenario.get('trace'):
        (artifact_root / name).mkdir(parents=True, exist_ok=True)
        command += ['-trace', f'enable=virtual_dma_*,file={artifact_root / name}/model.trace']
    return run(name, command, scenario['expected'], quiet=demo, artifact_root=artifact_root)


def write_summary(path, records):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(records, indent=2) + '\n')
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser()
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--stage', choices=STAGES)
    selection.add_argument('--scenario', help='run one firmware scenario from the manifest')
    selection.add_argument('--demo', action='store_true', help='short firmware demonstration')
    args = parser.parse_args()
    stage = args.stage or 'all'
    artifact_root = ROOT / 'artifacts'
    filename = 'summary.json'
    if args.scenario:
        if not re.fullmatch(r'[a-z][a-z0-9_]*', args.scenario):
            parser.error('invalid scenario name')
        artifact_root /= 'scenarios'
        filename = args.scenario + '-summary.json'
    elif args.demo:
        artifact_root /= 'demo'
    elif stage != 'all':
        artifact_root = artifact_root / 'stages' / stage
    summary = artifact_root / filename
    # A crash, invalid manifest or interrupted run must not expose old success.
    write_summary(summary, [dict(name='run', passed=False, reason='run incomplete')])
    records = []
    try:
        scenarios = select_scenarios(load_scenarios(SCENARIOS), stage, args.scenario, args.demo)
    except (OSError, ValueError) as exc:
        write_summary(summary, [dict(name='setup', passed=False, reason=str(exc))])
        parser.error(str(exc))
    if not args.scenario and not args.demo:
        if stage == 'all':
            records.append(run('runner', [sys.executable, '-m', 'unittest', 'discover', '-s', str(ROOT / 'tests'), '-p', 'test_*.py', '-v'], evidence='unittest', artifact_root=artifact_root))
        if stage != 'boot':
            records.append(run('platform', [sys.executable, str(ROOT / 'scripts/check-platform.py'),
                                           '--artifacts-dir', str(artifact_root / 'platform')],
                               expected=['PASS platform DTB and configuration constraints'], artifact_root=artifact_root))
            command = [str(ROOT / 'build/qemu/tests/qtest/virtual-dma-test')]
            expected = []
            if stage == 'detect':
                command += ['-p', '/riscv32/virtual-dma/registers']
                expected = ['ok 1 /riscv32/virtual-dma/registers']
            records.append(run('qtest', command, expected, timeout=120, evidence='tap', artifact_root=artifact_root))
    if args.demo:
        print('Virtual SoC firmware demonstration')
    for scenario in scenarios:
        result = guest(scenario, demo=args.demo, artifact_root=artifact_root)
        records.append(result)
        if args.demo and result['passed']:
            print((artifact_root / scenario['name'] / 'stdout.log').read_text(), end='')
    passed = all(record['passed'] for record in records)
    kind = 'demo' if args.demo else args.scenario or stage
    write_summary(summary, records)
    print(f'{"PASS" if passed else "FAIL"} {kind}: '
          f'{sum(record["passed"] for record in records)}/{len(records)} groups')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
