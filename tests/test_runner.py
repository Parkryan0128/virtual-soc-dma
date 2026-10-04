"""Check that failures cannot be reported as passing firmware validation."""
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest

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


if __name__ == '__main__':
    unittest.main()
