"""Failed builds must never leave runnable stale or partially updated firmware."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class FirmwareBuildTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "scripts").mkdir()
        for filename in ("build-firmware.py", "firmware_scenarios.py"):
            shutil.copyfile(
                Path(__file__).parents[1] / "scripts" / filename,
                self.root / "scripts" / filename,
            )
        (self.root / "tests/scenarios").mkdir(parents=True)
        self.manifest = self.root / "tests/scenarios/firmware.json"
        self.manifest.write_text(
            json.dumps(
                [
                    dict(name=name, stage="boot", expected=["PASS"])
                    for name in ("boot", "detect")
                ]
            )
        )
        self.output = self.root / "build/firmware"
        self.output.mkdir(parents=True)
        for name in ("boot", "detect", "removed"):
            (self.output / f"{name}.elf").write_text("old image")
        self.compiler = self.root / "fake-compiler"
        self.compiler.write_text(
            "#!"
            + sys.executable
            + "\n"
            + """
import os, sys
from pathlib import Path
args = sys.argv[1:]
Path(args[args.index('-o') + 1]).write_text('new image')
if 'firmware/tests/detect.c' in args and os.environ.get('FAIL_COMPILE'):
    raise SystemExit(1)
"""
        )
        self.compiler.chmod(0o755)

    def build(self, fail=False):
        env = dict(os.environ, RISCV_CC=str(self.compiler))
        if fail:
            env["FAIL_COMPILE"] = "1"
        return subprocess.run(
            [sys.executable, str(self.root / "scripts/build-firmware.py")],
            env=env,
            capture_output=True,
            text=True,
        )

    def test_partial_compile_failure_invalidates_all_images(self):
        self.assertNotEqual(self.build(fail=True).returncode, 0)
        self.assertEqual(list(self.output.glob("*.elf")), [])

    def test_invalid_manifest_invalidates_previous_images(self):
        self.manifest.write_text("[]")
        self.assertNotEqual(self.build().returncode, 0)
        self.assertEqual(list(self.output.glob("*.elf")), [])

    def test_success_publishes_only_current_images(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            sorted(p.name for p in self.output.iterdir()), ["boot.elf", "detect.elf"]
        )
        self.assertTrue(
            all(p.read_text() == "new image" for p in self.output.iterdir())
        )


class QemuBuildTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "scripts").mkdir()
        shutil.copyfile(
            Path(__file__).parents[1] / "scripts/build-qemu.sh",
            self.root / "scripts/build-qemu.sh",
        )
        apply = self.root / "scripts/apply-qemu.sh"
        apply.write_text('#!/bin/sh\nif [ "${FAIL_SETUP:-}" = 1 ]; then exit 1; fi\n')
        apply.chmod(0o755)
        self.output = self.root / "build/qemu"
        (self.output / "tests/qtest").mkdir(parents=True)
        (self.output / "build.ninja").touch()
        self.binaries = [
            self.output / "qemu-system-riscv32",
            self.output / "tests/qtest/virtual-dma-test",
        ]
        for binary in self.binaries:
            binary.write_text("old binary")
        (self.root / "bin").mkdir()
        ninja = self.root / "bin/ninja"
        ninja.write_text("""#!/bin/sh
printf 'new QEMU' > qemu-system-riscv32
if [ "${FAIL_BUILD:-}" = 1 ]; then exit 1; fi
printf 'new QTest' > tests/qtest/virtual-dma-test
""")
        ninja.chmod(0o755)

    def build(self, **options):
        return subprocess.run(
            ["sh", str(self.root / "scripts/build-qemu.sh")],
            env={
                **os.environ,
                "PATH": str(self.root / "bin") + os.pathsep + os.environ["PATH"],
                **options,
            },
            capture_output=True,
            text=True,
        )

    def test_failed_patch_invalidates_old_qemu_and_qtest(self):
        self.assertNotEqual(self.build(FAIL_SETUP="1").returncode, 0)
        self.assertFalse(any(p.exists() for p in self.binaries))

    def test_partial_link_failure_removes_both_binaries(self):
        self.assertNotEqual(self.build(FAIL_BUILD="1").returncode, 0)
        self.assertFalse(any(p.exists() for p in self.binaries))

    def test_success_keeps_both_new_binaries(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            [p.read_text() for p in self.binaries], ["new QEMU", "new QTest"]
        )


if __name__ == "__main__":
    unittest.main()
