"""Offline synthetic packaging checks; never executes a client or runtime."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import unittest
from unittest.mock import patch
from uuid import uuid4

import package_client_release as packaging


class ClientReleaseTests(unittest.TestCase):
    def setUp(self):
        self.test_parent = Path(__file__).resolve().parent
        self.root = self.test_parent / ("luma-release-test-" + uuid4().hex)
        self.root.mkdir()
        self.addCleanup(self.cleanup_directory)
        self.binary = self.root / "中文 路径" / "Release"
        self.binary.mkdir(parents=True)
        self.output = self.root / "交付 output"
        self.moment = datetime(2026, 11, 1, 5, 10, 11, tzinfo=timezone.utc)
        self.destination = self.output / "2026-11-01_13-10-11"
        (self.binary / "luma_studio.exe").write_bytes(b"MZsynthetic fixture; do not execute")
        hashes = {}
        for name in packaging.SOURCE_MANIFEST_FILES:
            target = self.binary / "ai-service" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            data = ("synthetic " + name).encode()
            target.write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        self.ai_manifest = self.binary / "ai-service/manifest.json"
        self.ai_manifest.write_text(json.dumps({
            "python_version": packaging.PYTHON_VERSION,
            "source": packaging.PYTHON_SOURCE,
            "archive_sha256": packaging.PYTHON_ARCHIVE_SHA256,
            "files": hashes,
        }), encoding="utf-8")

    def cleanup_directory(self):
        self.assertEqual(self.root.resolve().parent, self.test_parent)
        shutil.rmtree(self.root)

    def package(self):
        return packaging.package_release(self.binary, self.output, now=self.moment)

    def test_beijing_timestamp_allowlist_and_all_payload_hashes(self):
        for name in ("luma_account_ui_fixture.exe", "account.json", "secret.env", "libpq.dll", "session.db", "debug.log"):
            (self.binary / name).write_text("must never ship", encoding="utf-8")
        (self.binary / "ai-service/private.json").write_text("must never ship", encoding="utf-8")
        result = self.package()
        self.assertEqual(result, self.destination)
        manifest = json.loads((result / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["packaged_at"], "2026-11-01T13:10:11+08:00")
        self.assertEqual(manifest["build"]["status"], "unverified")
        self.assertEqual(manifest["acceptance"]["status"], "unverified")
        self.assertIsNone(manifest["git_commit"])
        expected = {"luma_studio.exe", "ai-service/manifest.json", "Start-LumaLive.cmd", "README.md"}
        expected.update("ai-service/" + name for name in packaging.AI_FILES)
        actual = {path.relative_to(result).as_posix() for path in result.rglob("*") if path.is_file()}
        self.assertEqual(actual, expected | {"manifest.json"})
        self.assertEqual(set(manifest["files"]), expected)
        for name, metadata in manifest["files"].items():
            payload = (result / name).read_bytes()
            self.assertEqual(metadata["sha256"], hashlib.sha256(payload).hexdigest())
            self.assertEqual(metadata["size_bytes"], len(payload))
        self.assertEqual(manifest["source_program"]["sha256"], manifest["files"]["luma_studio.exe"]["sha256"])
        ai = json.loads((result / "ai-service/manifest.json").read_text(encoding="utf-8"))
        self.assertNotIn("runtime/pythonw.exe", ai["files"])

    def test_collision_keeps_existing_release_unchanged(self):
        self.destination.mkdir(parents=True)
        marker = self.destination / "keep.txt"
        marker.write_bytes(b"untouched")
        with self.assertRaisesRegex(packaging.PackagingError, "already exists"):
            self.package()
        self.assertEqual(marker.read_bytes(), b"untouched")
        self.assertEqual(list(self.output.iterdir()), [self.destination])

    def test_missing_or_non_executable_input_creates_no_output(self):
        (self.binary / "luma_studio.exe").write_bytes(b"text is not an executable")
        with self.assertRaisesRegex(packaging.PackagingError, "executable header"):
            self.package()
        self.assertFalse(self.output.exists())
        (self.binary / "luma_studio.exe").unlink()
        with self.assertRaisesRegex(packaging.PackagingError, "missing or empty"):
            self.package()
        self.assertFalse(self.output.exists())

    def test_missing_ai_runtime_and_corrupted_source_fail_closed(self):
        (self.binary / "ai-service/runtime/python.exe").write_bytes(b"changed")
        with self.assertRaisesRegex(packaging.PackagingError, "checksum mismatch"):
            self.package()
        self.assertFalse(self.output.exists())
        (self.binary / "ai-service/runtime/python.exe").unlink()
        with self.assertRaisesRegex(packaging.PackagingError, "missing or empty"):
            self.package()
        self.assertFalse(self.output.exists())

    def test_manifest_cannot_add_paths_or_change_runtime_version(self):
        original = json.loads(self.ai_manifest.read_text(encoding="utf-8"))
        for alteration in ("paths", "version", "array"):
            with self.subTest(alteration=alteration):
                manifest = json.loads(json.dumps(original))
                if alteration == "paths":
                    manifest["files"]["../../secret.env"] = "0" * 64
                elif alteration == "version":
                    manifest["python_version"] = "untrusted"
                else:
                    manifest = []
                self.ai_manifest.write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaises(packaging.PackagingError):
                    self.package()
                self.assertFalse(self.output.exists())

    def test_launcher_failure_removes_incomplete_directory_only(self):
        self.output.mkdir()
        sentinel = self.output / "existing-release"
        sentinel.mkdir()
        (sentinel / "keep.txt").write_bytes(b"untouched")
        with patch.object(packaging, "write_launchers", side_effect=OSError("injected disk failure")):
            with self.assertRaisesRegex(OSError, "injected disk failure"):
                self.package()
        self.assertFalse(self.destination.exists())
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual((sentinel / "keep.txt").read_bytes(), b"untouched")

    def test_source_change_during_copy_does_not_publish(self):
        copy = packaging.shutil.copyfile

        def corrupt_copy(source, destination):
            result = copy(source, destination)
            if source.name == "luma_studio.exe":
                destination.write_bytes(b"MZchanged while copying")
            return result

        with patch.object(packaging.shutil, "copyfile", side_effect=corrupt_copy):
            with self.assertRaisesRegex(packaging.PackagingError, "Source changed"):
                self.package()
        self.assertFalse(self.destination.exists())
        self.assertEqual(list(self.output.iterdir()), [])

    def test_overlap_and_naive_time_are_rejected(self):
        for output in (self.binary, self.binary / "releases", self.root):
            with self.subTest(output=output), self.assertRaises(packaging.PackagingError):
                packaging.package_release(self.binary, output, now=self.moment)
        with self.assertRaisesRegex(packaging.PackagingError, "timezone"):
            packaging.package_release(self.binary, self.output, now=datetime(2026, 11, 1))

    def test_linked_input_is_rejected(self):
        link = self.root / "linked input"
        try:
            link.symlink_to(self.binary, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Symbolic links unavailable: {error}")
        with self.assertRaisesRegex(packaging.PackagingError, "symbolic link or junction"):
            packaging.package_release(link, self.output, now=self.moment)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
