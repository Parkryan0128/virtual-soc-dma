"""A failed demo build must preserve results from the last full suite."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class ContainerTests(unittest.TestCase):
    def test_failed_demo_build_preserves_full_suite_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "scripts").mkdir()
            shutil.copyfile(
                Path(__file__).parents[1] / "scripts/test-in-container.sh",
                root / "scripts/test-in-container.sh",
            )
            (root / "artifacts/demo").mkdir(parents=True)
            full_summary = root / "artifacts/summary.json"
            full_log = root / "artifacts/build.log"
            full_summary.write_text('[{"passed": true}]')
            full_log.write_text("full suite build output")
            demo_summary = root / "artifacts/demo/summary.json"
            demo_summary.write_text('[{"passed": true}]')
            (root / "bin").mkdir()
            docker = root / "bin/docker"
            docker.write_text("#!/bin/sh\nexit 42\n")
            docker.chmod(0o755)
            result = subprocess.run(
                ["sh", str(root / "scripts/test-in-container.sh"), "demo"],
                env={
                    **os.environ,
                    "PATH": str(root / "bin") + os.pathsep + os.environ["PATH"],
                },
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 42, result.stderr)
            self.assertTrue(full_summary.exists(), "demo deleted full-suite summary")
            self.assertEqual(full_summary.read_text(), '[{"passed": true}]')
            self.assertEqual(full_log.read_text(), "full suite build output")
            self.assertFalse(demo_summary.exists(), "failed demo retained an old PASS")
