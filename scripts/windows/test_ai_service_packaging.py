"""Verify packaging refuses linked destinations without changing their targets."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/windows"))
from package_ai_service import ARCHIVE


class PackagingLinks(unittest.TestCase):
    def test_link_destinations_preserve_external_files(self):
        for name in ("service", "runtime", "manifest"):
            with self.subTest(name=name), tempfile.TemporaryDirectory(prefix="luma-package-", dir=ROOT / "output/tmp") as temporary:
                base = pathlib.Path(temporary)
                binary = base / "client"
                binary.mkdir()
                (binary / "luma_studio.exe").touch()
                outside = base / "outside"
                outside.mkdir()
                sentinel = outside / "keep.txt"
                sentinel.write_bytes(b"unchanged")
                service = binary / "ai-service"
                try:
                    if name == "service":
                        service.symlink_to(outside, target_is_directory=True)
                    elif name == "runtime":
                        service.mkdir()
                        (service / "runtime").symlink_to(outside, target_is_directory=True)
                    else:
                        service.mkdir()
                        (service / "manifest.json").symlink_to(sentinel)
                except OSError as error:
                    self.skipTest(f"Symbolic links unavailable: {error.winerror}")
                result = subprocess.run([sys.executable, str(ROOT / "scripts/windows/package_ai_service.py"),
                    "--binary-dir", str(binary), "--archive", str(ROOT / "output" / ARCHIVE)],
                    capture_output=True, timeout=20)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"must not be", result.stderr)
                self.assertEqual(sentinel.read_bytes(), b"unchanged")
                self.assertEqual([item.name for item in outside.iterdir()], ["keep.txt"])
                if name == "manifest":
                    self.assertFalse((service / "runtime").exists())


if __name__ == "__main__":
    unittest.main()
