"""Isolated report failure tests; never build or launch the Windows client."""
from contextlib import redirect_stderr, redirect_stdout
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import verify_client as verifier


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="luma-verification-report-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.build = self.root / "build/d-drive"
        self.binary = self.root / "output/next/Release"
        self.report = verifier.VerificationReport(self.root, self.build, self.binary)

    def read(self):
        return json.loads(self.report.path.read_text(encoding="utf-8"))

    def previous_success(self):
        previous = {"started_utc": "2026-01-01T00:00:00+00:00",
                    "status": "passed", "checks": [{"name": "old", "exit_code": 0}]}
        self.report.path.parent.mkdir(parents=True, exist_ok=True)
        self.report.path.write_text(json.dumps(previous), encoding="utf-8")
        return self.report.path.read_bytes()

    def configured_build(self):
        self.build.mkdir(parents=True)
        self.binary.mkdir(parents=True, exist_ok=True)
        (self.build / "CMakeCache.txt").write_text(
            f"CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE:PATH={self.binary}\n", encoding="utf-8")
        library = self.build / "Release/luma_studio_preview.lib"
        library.parent.mkdir()
        library.write_bytes(b"fixture library, not executable")
        executable = self.binary / "luma_studio.exe"
        executable.write_bytes(b"fixture client, not executable")
        # Force deterministic freshness even on filesystems with coarse timestamps.
        os.utime(library, (100, 100))
        os.utime(executable, (200, 200))

    def invoke_main(self, side_effect=None):
        stderr = io.StringIO()
        with patch.object(verifier, "__file__", str(self.root / "scripts/windows/verify_client.py")), \
                patch.object(verifier.subprocess, "run", side_effect=side_effect,
                             return_value=subprocess.CompletedProcess([], 0)) as child, \
                redirect_stdout(io.StringIO()), redirect_stderr(stderr):
            code = verifier.main([])
        return code, child, stderr.getvalue()

    def test_new_run_archives_previous_success_and_clears_passed(self):
        previous = self.previous_success()
        self.report.start()
        current = self.read()
        self.assertEqual(current["status"], "running")
        self.assertEqual(current["checks"], [])
        self.assertNotIn("finished_utc", current)
        self.assertTrue(current["run_id"])
        self.assertEqual(Path(current["previous_report"]).read_bytes(), previous)

    def test_atomic_replace_exposes_only_complete_old_and_new_json(self):
        self.report.start()
        before = self.report.path.read_bytes()
        replace = os.replace
        self.report.record["client"] = {"path": "some client", "sha256": "abc"}

        def inspect_replace(source, target):
            self.assertEqual(Path(target).read_bytes(), before)
            self.assertEqual(json.loads(Path(source).read_bytes()), self.report.record)
            self.assertEqual(Path(source).parent, Path(target).parent)
            replace(source, target)

        with patch.object(verifier.os, "replace", side_effect=inspect_replace):
            self.report.save()
        self.assertEqual(self.read(), self.report.record)
        self.assertFalse(list(self.report.path.parent.glob("*.tmp")))

    def test_failed_flush_and_failed_replace_preserve_readable_snapshot(self):
        for function in ("fsync", "replace"):
            with self.subTest(function=function):
                self.report.start()
                before = self.report.path.read_bytes()
                self.report.record["status"] = "failed"
                with patch.object(verifier.os, function, side_effect=OSError("disk failure")):
                    with self.assertRaisesRegex(OSError, "disk failure"):
                        self.report.save()
                self.assertEqual(self.report.path.read_bytes(), before)
                self.assertIsInstance(self.read(), dict)
                self.assertFalse(list(self.report.path.parent.glob("*.tmp")))

    def test_serialization_failure_does_not_truncate_report(self):
        self.report.start()
        before = self.report.path.read_bytes()
        self.report.record["unsupported"] = object()
        with self.assertRaises(TypeError):
            self.report.save()
        self.assertEqual(self.report.path.read_bytes(), before)

    def test_damaged_previous_report_is_preserved_before_recovery(self):
        self.previous_success()
        self.report.path.write_bytes(b'{"status":')
        self.report.start()
        self.assertEqual(self.read()["status"], "running")
        self.assertEqual(Path(self.read()["previous_report"]).read_bytes(), b'{"status":')

    def test_new_run_does_not_overwrite_previous_logs(self):
        logs = []
        for contents in ("first log", "second log"):
            report = verifier.VerificationReport(self.root, self.build, self.binary)
            report.start()

            def fake_child(*args, **kwargs):
                kwargs["stdout"].write(contents)
                return subprocess.CompletedProcess(args[0], 0)

            with patch.object(verifier.subprocess, "run", side_effect=fake_child), \
                    redirect_stdout(io.StringIO()):
                report.run("build", ["never-launch"], 1, {})
            report.finish("passed")
            logs.append(Path(report.record["checks"][0]["log"]))
        self.assertNotEqual(logs[0], logs[1])
        self.assertEqual([p.read_text() for p in logs], ["first log", "second log"])
        archived = json.loads(Path(self.read()["previous_report"]).read_bytes())
        self.assertEqual(archived["status"], "passed")
        self.assertEqual(archived["checks"][0]["log"], str(logs[0]))

    def test_abrupt_exit_leaves_this_run_running_and_prior_success_archived(self):
        previous = self.previous_success()
        # A separate plain Python process exits without finally blocks. It does
        # not run main(), CMake, any client executable, or any service.
        program = (
            "import os, sys; from pathlib import Path; "
            "sys.path.insert(0, sys.argv[1]); import verify_client; "
            "root=Path(sys.argv[2]); "
            "r=verify_client.VerificationReport(root, root/'build', root/'bin'); "
            "r.start(); os._exit(23)"
        )
        child = subprocess.run([sys.executable, "-B", "-c", program,
                                str(Path(verifier.__file__).parent), str(self.root)],
                               capture_output=True, text=True, timeout=10)
        self.assertEqual(child.returncode, 23, child.stderr)
        self.assertEqual(self.read()["status"], "running")
        self.assertNotIn("finished_utc", self.read())
        self.assertEqual(Path(self.read()["previous_report"]).read_bytes(), previous)

    def test_preflight_failure_replaces_old_passed_with_current_failed(self):
        previous = self.previous_success()
        code, child, error = self.invoke_main()
        self.assertEqual(code, 1)
        child.assert_not_called()
        current = self.read()
        self.assertEqual(current["status"], "failed")
        self.assertEqual(current["checks"], [])
        self.assertIn("finished_utc", current)
        self.assertIn("configured standalone", current["error"])
        self.assertEqual(Path(current["previous_report"]).read_bytes(), previous)

    def test_report_start_failure_prevents_all_checks_and_preserves_previous(self):
        previous = self.previous_success()
        with patch.object(verifier.os, "replace", side_effect=OSError("write denied")):
            code, child, error = self.invoke_main()
        self.assertEqual(code, 1)
        child.assert_not_called()
        self.assertIn("no checks ran", error)
        self.assertEqual(self.report.path.read_bytes(), previous)

    @unittest.skipUnless(os.name == "nt", "Windows verifier preflight")
    def test_nonzero_exit_preserves_completed_checks_and_failing_log(self):
        self.configured_build()
        self.previous_success()
        count = 0

        def fake_child(command, **kwargs):
            nonlocal count
            count += 1
            snapshot = self.read()
            self.assertEqual(snapshot["status"], "running")
            self.assertEqual(snapshot["checks"][-1]["status"], "running")
            self.assertIsNone(snapshot["checks"][-1]["exit_code"])
            self.assertNotIn("finished_utc", snapshot["checks"][-1])
            kwargs["stdout"].write("diagnostic output")
            return subprocess.CompletedProcess(command, 7 if count == 2 else 0)

        code, child, error = self.invoke_main(fake_child)
        self.assertEqual(code, 1)
        self.assertEqual(child.call_count, 2)
        current = self.read()
        self.assertEqual(current["status"], "failed")
        self.assertEqual([c["status"] for c in current["checks"]], ["passed", "failed"])
        self.assertEqual(current["checks"][-1]["exit_code"], 7)
        self.assertEqual(Path(current["checks"][-1]["log"]).read_text(), "diagnostic output")
        self.assertIn("account-connection-settings failed", current["error"])

    @unittest.skipUnless(os.name == "nt", "Windows verifier preflight")
    def test_timeout_launch_failure_and_interrupt_record_active_check(self):
        self.configured_build()
        cases = [(subprocess.TimeoutExpired(["fake"], 1), "failed", 1),
                 (FileNotFoundError("fixture executable missing"), "failed", 1),
                 (KeyboardInterrupt(), "interrupted", 130)]
        for failure, status, expected_code in cases:
            with self.subTest(failure=type(failure).__name__):
                code, child, error = self.invoke_main(failure)
                self.assertEqual(code, expected_code)
                self.assertEqual(child.call_count, 1)
                current = self.read()
                self.assertEqual(current["status"], status)
                self.assertIn("finished_utc", current)
                check = current["checks"][0]
                self.assertEqual(check["name"], "build")
                self.assertEqual(check["status"], status)
                self.assertIsNone(check["exit_code"])
                self.assertIn("finished_utc", check)
                self.assertTrue(check["error"])
                self.assertTrue(Path(check["log"]).is_file())

    @unittest.skipUnless(os.name == "nt", "Windows verifier preflight")
    def test_all_27_checks_finish_passed_only_after_successful_completion(self):
        self.configured_build()

        def fake_child(command, **kwargs):
            self.assertEqual(self.read()["status"], "running")
            return subprocess.CompletedProcess(command, 0)

        code, child, error = self.invoke_main(fake_child)
        self.assertEqual(code, 0, error)
        self.assertEqual(child.call_count, 27)
        current = self.read()
        self.assertEqual(current["status"], "passed")
        self.assertEqual(len(current["checks"]), 27)
        self.assertTrue(all(c["status"] == "passed" for c in current["checks"]))
        self.assertIn("sha256", current["client"])
        self.assertEqual(current["build_dir"], str(self.build))
        self.assertEqual(current["binary_dir"], str(self.binary))

    @unittest.skipUnless(os.name == "nt", "Windows verifier preflight")
    def test_final_publish_failure_returns_failure_and_keeps_running_snapshot(self):
        self.configured_build()
        replace = os.replace

        def fail_final_replace(source, target):
            content = json.loads(Path(source).read_bytes())
            if content["status"] == "passed":
                raise OSError("final write denied")
            replace(source, target)

        with patch.object(verifier.os, "replace", side_effect=fail_final_replace):
            code, child, error = self.invoke_main()
        self.assertEqual(code, 1)
        self.assertEqual(child.call_count, 27)
        self.assertIn("Cannot finalize", error)
        self.assertEqual(self.read()["status"], "running")
        self.assertEqual(len(self.read()["checks"]), 27)
        self.assertNotIn("finished_utc", self.read())

    @unittest.skipUnless(os.name == "nt", "Windows verifier preflight")
    def test_cache_mismatch_is_a_current_failure_without_launching_checks(self):
        self.configured_build()
        self.previous_success()
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE:PATH=wrong-directory\n", encoding="utf-8")
        code, child, error = self.invoke_main()
        self.assertEqual(code, 1)
        child.assert_not_called()
        self.assertEqual(self.read()["status"], "failed")
        self.assertIn("--binary-dir", self.read()["error"])


if __name__ == "__main__":
    unittest.main()
