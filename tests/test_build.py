"""Failed builds must never leave runnable stale or partially updated firmware."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class FirmwareBuildTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'scripts').mkdir()
        for filename in ('build-firmware.py', 'firmware_scenarios.py'):
            shutil.copyfile(Path(__file__).parents[1] / 'scripts' / filename,
                            self.root / 'scripts' / filename)
        (self.root / 'tests/scenarios').mkdir(parents=True)
        self.manifest = self.root / 'tests/scenarios/firmware.json'
        self.manifest.write_text(json.dumps([
            dict(name=name, stage='boot', expected=['PASS']) for name in ('boot', 'detect')]))
        self.output = self.root / 'build/firmware'
        self.output.mkdir(parents=True)
        for name in ('boot', 'detect', 'removed'):
            (self.output / f'{name}.elf').write_text('old image')
        self.compiler = self.root / 'fake-compiler'
        self.compiler.write_text('#!' + sys.executable + '\n' + '''
import os, sys
from pathlib import Path
args = sys.argv[1:]
Path(args[args.index('-o') + 1]).write_text('new image')
if 'firmware/tests/detect.c' in args and os.environ.get('FAIL_COMPILE'):
    raise SystemExit(1)
''')
        self.compiler.chmod(0o755)

    def build(self, fail=False):
        env = dict(os.environ, RISCV_CC=str(self.compiler))
        if fail:
            env['FAIL_COMPILE'] = '1'
        return subprocess.run([sys.executable, str(self.root / 'scripts/build-firmware.py')],
                              env=env, capture_output=True, text=True)

    def test_partial_compile_failure_invalidates_all_images(self):
        self.assertNotEqual(self.build(fail=True).returncode, 0)
        self.assertEqual(list(self.output.glob('*.elf')), [])

    def test_invalid_manifest_invalidates_previous_images(self):
        self.manifest.write_text('[]')
        self.assertNotEqual(self.build().returncode, 0)
        self.assertEqual(list(self.output.glob('*.elf')), [])

    def test_success_publishes_only_current_images(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(p.name for p in self.output.iterdir()), ['boot.elf', 'detect.elf'])
        self.assertTrue(all(p.read_text() == 'new image' for p in self.output.iterdir()))


if __name__ == '__main__':
    unittest.main()
