"""Exercise interrupted-checkout recovery using a real local Git remote."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class FetchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.remote = self.root / 'remote'
        self.remote.mkdir()
        self.git(self.remote, 'init', '-q')
        (self.remote / 'source').write_text('pinned dependency')
        self.git(self.remote, 'add', 'source')
        self.git(self.remote, '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                 '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture')
        self.revision = self.git(self.remote, 'rev-parse', 'HEAD').strip()
        (self.root / 'qemu').mkdir()
        (self.root / 'qemu/base-version.txt').write_text('fixture\n' + self.revision + '\n')
        (self.root / 'scripts').mkdir()
        shutil.copyfile(Path(__file__).parents[1] / 'scripts/fetch-qemu.sh', self.root / 'scripts/fetch-qemu.sh')
        self.checkout = self.root / 'build/qemu-src'
        self.checkout.mkdir(parents=True)
        self.git(self.checkout, 'init', '-q')
        self.git(self.checkout, 'remote', 'add', 'origin', str(self.remote))

    def git(self, cwd, *args):
        return subprocess.check_output(['git', '-C', str(cwd), *args], text=True, stderr=subprocess.PIPE)

    def fetch(self):
        return subprocess.run(['sh', str(self.root / 'scripts/fetch-qemu.sh')], capture_output=True, text=True)

    def test_interrupted_initial_fetch_recovers_and_is_idempotent(self):
        first = self.fetch()
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'HEAD').strip(), self.revision)
        self.assertEqual(self.fetch().returncode, 0)

    def test_wrong_existing_revision_is_rejected_without_reset(self):
        self.assertEqual(self.fetch().returncode, 0)
        (self.root / 'qemu/base-version.txt').write_text('fixture\n' + '0' * 40 + '\n')
        result = self.fetch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Wrong QEMU revision', result.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'HEAD').strip(), self.revision)


if __name__ == '__main__':
    unittest.main()
