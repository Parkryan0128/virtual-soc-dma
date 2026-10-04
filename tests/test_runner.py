"""Check that failures cannot be reported as passing firmware validation."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('runner', Path(__file__).parents[1] / 'scripts/run-tests.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.previous = runner.ROOT
        runner.ROOT = Path(self.temporary.name)

    def tearDown(self):
        runner.ROOT = self.previous
        self.temporary.cleanup()

    def execute(self, command, expected=('PASS content',), timeout=5):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return runner.run('fixture', command, expected, timeout=timeout)

    def test_marker_does_not_override_failed_exit(self):
        result = self.execute([sys.executable, '-c', 'print("PASS content"); raise SystemExit(1)'])
        self.assertFalse(result['passed'])
        self.assertEqual(result['exit_code'], 1)

    def test_successful_exit_without_memory_evidence_fails(self):
        result = self.execute([sys.executable, '-c', 'print("BOOT only")'])
        self.assertFalse(result['passed'])
        self.assertTrue((runner.ROOT / 'artifacts/fixture/stdout.log').exists())

    def test_watchdog_catches_hung_guest(self):
        result = self.execute([sys.executable, '-c', 'import time; time.sleep(10)'], timeout=0.1)
        self.assertFalse(result['passed'])
        self.assertEqual(result['reason'], 'host watchdog expired')

    def test_missing_binary_is_a_recorded_failure(self):
        result = self.execute([str(runner.ROOT / 'missing-qemu')])
        self.assertFalse(result['passed'])
        self.assertTrue((runner.ROOT / 'artifacts/fixture/stderr.log').read_text())

    def test_success_requires_a_complete_marker_line(self):
        result = self.execute([sys.executable, '-c', 'print("NOT PASS content")'])
        self.assertFalse(result['passed'])

    def test_failure_marker_overrides_success_marker(self):
        result = self.execute([sys.executable, '-c', 'print("FAIL data\\nPASS content")'])
        self.assertFalse(result['passed'])

    def test_invalid_utf8_is_retained_and_reported(self):
        result = self.execute([sys.executable, '-c', 'import os; os.write(1, b"\\xffbad\\n")'])
        self.assertFalse(result['passed'])
        self.assertEqual((runner.ROOT / 'artifacts/fixture/stdout.log').read_bytes(), b'\xffbad\n')

    def test_failed_launch_replaces_previous_evidence(self):
        self.assertTrue(self.execute([sys.executable, '-c', 'print("PASS content")'])['passed'])
        (runner.ROOT / 'artifacts/fixture/model.trace').write_text('old completion')
        self.execute([str(runner.ROOT / 'missing-qemu')])
        self.assertEqual((runner.ROOT / 'artifacts/fixture/stdout.log').read_bytes(), b'')
        self.assertFalse((runner.ROOT / 'artifacts/fixture/model.trace').exists())

    def test_timeout_retains_partial_output(self):
        result = self.execute([sys.executable, '-c', 'import time; print("BOOT partial", flush=True); time.sleep(5)'], timeout=0.2)
        self.assertEqual(result['reason'], 'host watchdog expired')
        self.assertIn(b'BOOT partial', (runner.ROOT / 'artifacts/fixture/stdout.log').read_bytes())

    def test_complete_tap_plan_passes(self):
        self.assertIsNone(runner.tap_failure('1..2\nok 1 /a\nok 2 /b\n'))

    def invoke_main(self, *args):
        with mock.patch.object(sys, 'argv', ['run-tests.py', *args]), \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return runner.main()

    def test_bad_manifest_cannot_leave_passing_summary(self):
        summary = runner.ROOT / 'artifacts/summary.json'
        summary.parent.mkdir()
        summary.write_text('[{"name":"old", "passed":true}]')
        with mock.patch.object(runner, 'load_scenarios', side_effect=ValueError('bad manifest')):
            with self.assertRaises(SystemExit):
                self.invoke_main()
        self.assertTrue(summary.exists())
        self.assertFalse(all(r['passed'] for r in json.loads(summary.read_text())))

    def test_interrupted_run_cannot_leave_passing_summary(self):
        summary = runner.ROOT / 'artifacts/summary.json'
        summary.parent.mkdir()
        summary.write_text('[{"name":"old", "passed":true}]')
        scenarios = [dict(name='boot', stage='boot', expected=['PASS boot'])]
        with mock.patch.object(runner, 'load_scenarios', return_value=scenarios), \
             mock.patch.object(runner, 'run', side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                self.invoke_main()
        self.assertEqual(json.loads(summary.read_text())[0]['reason'], 'run incomplete')

    def test_demo_cannot_replace_full_suite_logs(self):
        artifact = runner.ROOT / 'artifacts/boot'
        artifact.mkdir(parents=True)
        (artifact / 'stdout.log').write_text('original full run')
        binary = runner.ROOT / 'build/qemu/qemu-system-riscv32'
        binary.parent.mkdir(parents=True)
        binary.write_text('#!' + sys.executable + '\nprint("PASS demo fixture")\n')
        binary.chmod(0o755)
        scenarios = [dict(name='boot', stage='boot', demo=True, expected=['PASS demo fixture'])]
        with mock.patch.object(runner, 'load_scenarios', return_value=scenarios):
            self.assertEqual(self.invoke_main('--demo'), 0)
        self.assertEqual((artifact / 'stdout.log').read_text(), 'original full run')
        self.assertIn('PASS demo fixture', (runner.ROOT / 'artifacts/demo/boot/stdout.log').read_text())

    def test_partial_runs_preserve_full_suite_evidence(self):
        artifact = runner.ROOT / 'artifacts/boot'
        artifact.mkdir(parents=True)
        (artifact / 'stdout.log').write_text('original full run')
        summary = runner.ROOT / 'artifacts/summary.json'
        summary.write_text('original full summary')
        binary = runner.ROOT / 'build/qemu/qemu-system-riscv32'
        binary.parent.mkdir(parents=True)
        binary.write_text('#!' + sys.executable + '\nprint("PASS fixture")\n')
        binary.chmod(0o755)
        scenarios = [dict(name='boot', stage='boot', expected=['PASS fixture'])]
        with mock.patch.object(runner, 'load_scenarios', return_value=scenarios):
            self.assertEqual(self.invoke_main('--stage', 'boot'), 0)
            self.assertEqual(self.invoke_main('--scenario', 'boot'), 0)
        self.assertEqual((artifact / 'stdout.log').read_text(), 'original full run')
        self.assertEqual(summary.read_text(), 'original full summary')
        for path in ('stages/boot/summary.json', 'scenarios/boot-summary.json'):
            self.assertTrue(all(r['passed'] for r in json.loads((runner.ROOT / 'artifacts' / path).read_text())))

    def test_incomplete_or_failed_tap_is_rejected(self):
        for output in ('1..0\n', '1..2\nok 1 /a\n', '1..2\nok 1 /a\nok 1 /b\n',
                       '1..1\nnot ok 1 /a\n', '1..1\nok 1 /a # SKIP disabled\n',
                       '1..2\nok 1 /a\nok 2 /a\n', '1..1\nok 1 /a\nBail out! error\n'):
            with self.subTest(output=output):
                self.assertIsNotNone(runner.tap_failure(output))

    @unittest.skipUnless(sys.platform in ('linux', 'darwin'), 'POSIX process groups')
    def test_watchdog_terminates_descendants(self):
        leaked = runner.ROOT / 'leaked'
        child = f'import time; from pathlib import Path; time.sleep(0.5); Path({str(leaked)!r}).touch()'
        parent = f'import subprocess,sys,time; subprocess.Popen([sys.executable,"-c",{child!r}]); time.sleep(5)'
        result = self.execute([sys.executable, '-c', parent], timeout=0.2)
        self.assertFalse(result['passed'])
        time.sleep(0.6)
        self.assertFalse(leaked.exists(), 'descendant survived the host watchdog')


if __name__ == '__main__':
    unittest.main()
