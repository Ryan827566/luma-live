"""Build and verify the actual standalone Windows client, not just its library."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
from datetime import datetime, timezone



def utc_now():
    return datetime.now(timezone.utc).isoformat()


def atomic_write(path, content):
    """Publish complete bytes without truncating the last readable report."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        # Same-directory replacement stays on the same filesystem. Close before
        # replace because Windows cannot rename an open NamedTemporaryFile.
        with tempfile.NamedTemporaryFile(mode="wb", dir=path.parent,
                                         prefix=path.name + ".", suffix=".tmp",
                                         delete=False) as output:
            temporary = Path(output.name)
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


class VerificationReport:
    def __init__(self, root, build, binary):
        self.root = root
        self.path = root / "output/client-verification.json"
        self.record = {
            "run_id": uuid.uuid4().hex,
            "pid": os.getpid(),
            "started_utc": utc_now(),
            "build_dir": str(build),
            "binary_dir": str(binary),
            "status": "running",
            "checks": [],
        }

    def start(self):
        # Preserve the previous evidence, including legacy or damaged reports.
        # Never reuse an old passed status for a new invocation.
        if self.path.exists():
            archive = (self.path.parent / "client-verification-history" /
                       (self.record["run_id"] + "-previous.json"))
            atomic_write(archive, self.path.read_bytes())
            self.record["previous_report"] = str(archive)
        self.save()

    def save(self):
        # Serialize before opening anything, so serialization errors are safe too.
        content = (json.dumps(self.record, indent=2) + "\n").encode("utf-8")
        atomic_write(self.path, content)

    def finish(self, status, error=None):
        self.record["status"] = status
        self.record["finished_utc"] = utc_now()
        if error is not None:
            self.record["error"] = str(error) or type(error).__name__
        self.save()

    def run(self, name, command, timeout, env):
        # Run-specific logs keep archived reports' log references meaningful.
        log_path = self.path.parent / f"verify-{self.record['run_id']}-{name}.log"
        check = {"name": name, "status": "running", "started_utc": utc_now(),
                 "exit_code": None, "log": str(log_path)}
        self.record["checks"].append(check)
        self.save()
        try:
            with log_path.open("w", encoding="utf-8") as log:
                test_env = env.copy()
                test_env["LUMALIVE_AUTH_ENV"] = "test"
                test_env["LUMALIVE_AUTH_TLS_CA_FILE"] = ""
                if name.startswith("auth-"):
                    test_env.pop("LUMALIVE_AUTH_DATABASE_URL", None)
                result = subprocess.run(command, cwd=self.root, env=test_env,
                                        stdout=log, stderr=subprocess.STDOUT,
                                        timeout=timeout)
            check["exit_code"] = result.returncode
            if result.returncode:
                raise RuntimeError(f"{name} failed ({result.returncode}); see {log_path}")
        except KeyboardInterrupt:
            check["status"] = "interrupted"
            check["error"] = "KeyboardInterrupt"
            raise
        except Exception as error:
            check["status"] = "failed"
            check["error"] = str(error) or type(error).__name__
            raise
        else:
            check["status"] = "passed"
        finally:
            check["finished_utc"] = utc_now()
            self.save()
        print("PASS:", name, flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build/d-drive")
    parser.add_argument("--binary-dir", default="output/next/Release")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[2]
    build = (root / args.build_dir).resolve()
    binary = (root / args.binary_dir).resolve()
    report = VerificationReport(root, build, binary)
    record = report.record
    try:
        # Publish this run before preflight; a failed preflight must not leave
        # last week's successful verification looking like today's result.
        report.start()
    except Exception as error:
        print(f"Cannot start verification report {report.path}: {error}; no checks ran.", file=sys.stderr)
        return 1
    status, error, exit_code = "failed", None, 1
    try:
        if os.name != "nt" or not (build / "CMakeCache.txt").is_file():
            raise RuntimeError("Requires Windows and an already configured standalone CMake build.")
        cache = {}
        for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
            if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
                key, value = line.split("=", 1)
                cache[key.split(":", 1)[0]] = value
        configured = cache.get("CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE")
        if not configured or (build / configured).resolve() != binary:
            raise RuntimeError("--binary-dir must match CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE in this build's cache.")
        cmake = shutil.which("cmake") or str(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "CMake/bin/cmake.exe")
        env = {key.upper(): value for key, value in os.environ.items()}
        temporary = root / "output/tmp"
        temporary.mkdir(parents=True, exist_ok=True)
        env["TEMP"] = env["TMP"] = str(temporary)
        env["MSBUILDDISABLENODEREUSE"] = "1"
        def run(name, command, timeout):
            report.run(name, command, timeout, env)
        run("build", [cmake, "--build", str(build), "--config", "Release", "--target", "luma_client_verification", "--parallel", "1", "--", "/nr:false"], 1800)
        # The configured output must also be newer than the linked preview library.
        executable = binary / "luma_studio.exe"
        library = build / "Release/luma_studio_preview.lib"
        if not executable.is_file() or not library.is_file() or executable.stat().st_mtime_ns < library.stat().st_mtime_ns:
            raise RuntimeError("Client executable is missing or older than the preview library; check the CMake runtime output directory.")
        record["client"] = {"path": str(executable), "sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}
        run("account-connection-settings", [str(binary / "luma_account_connection_settings_tests.exe")], 20)
        run("account-entrance-layout", [str(binary / "luma_account_entrance_layout_tests.exe")], 40)
        run("auth-integration", [str(binary / "luma_auth_integration_tests.exe")], 120)
        run("auth-client", [str(binary / "luma_client_account_tests.exe")], 30)
        run("email-provider", [str(binary / "luma_email_provider_tests.exe")], 20)
        run("sms-provider", [str(binary / "luma_sms_provider_tests.exe")], 20)
        run("auth-ui", [sys.executable, "scripts/windows/test_account_ui.py", str(executable)], 90)
        run("auth-workspace-gate", [sys.executable, "scripts/windows/test_workspace_auth_gate.py", str(executable)], 120)
        run("package-ai-service", [sys.executable, "scripts/windows/package_ai_service.py", "--binary-dir", str(binary)], 60)
        run("local-ai-service", [str(binary / "luma_local_ai_service_tests.exe")], 40)
        run("local-ai-service-ui", [sys.executable, "scripts/windows/test_local_ai_service_ui.py", str(executable)], 90)
        run("accessibility", [str(binary / "luma_ui_accessibility_tests.exe")], 20)
        run("transcript-view", [str(binary / "luma_transcript_view_tests.exe")], 20)
        run("provider-settings", [str(binary / "luma_ai_provider_settings_tests.exe")], 30)
        run("session-archive", [str(binary / "luma_session_archive_tests.exe")], 30)
        run("archive-search", [str(binary / "luma_session_archive_search_tests.exe")], 20)
        run("service-health", [str(binary / "luma_ai_service_health_tests.exe")], 20)
        run("locale", [str(binary / "luma_ui_locale_tests.exe")], 20)
        run("ai-state", [str(binary / "luma_session_ai_tests.exe")], 30)
        run("captions", [str(binary / "luma_caption_overlay_tests.exe"), str(root / "output/caption-overlay.bmp")], 20)
        run("gateway", [sys.executable, "-m", "unittest", "discover", "-s", "LumaLive/server/ai-gateway/python", "-p", "test_gateway.py"], 60)
        run("provider-config", [sys.executable, "-m", "unittest", "discover", "-s", "LumaLive/server/ai-gateway/python", "-p", "test_provider_settings.py"], 60)
        run("native-health", [sys.executable, "LumaLive/server/ai-gateway/python/test_native_health.py", str(binary / "luma_ai_service_health_tests.exe")], 60)
        run("native-ai", [sys.executable, "LumaLive/server/ai-gateway/python/test_native_client.py", str(binary / "luma_session_ai_gateway_tests.exe")], 60)
        run("navigation", [sys.executable, "scripts/windows/test_studio_navigation.py", str(executable), str(binary / "luma_meeting_ui_fixture.exe")], 60)
        run("meeting-ui", [sys.executable, "scripts/windows/test_meeting_ui.py", str(binary)], 90)
        status, exit_code = "passed", 0
    except KeyboardInterrupt as caught:
        status, error, exit_code = "interrupted", caught, 130
        print("Verification interrupted; completed checks remain in the report.", file=sys.stderr)
    except Exception as caught:
        error = caught
        print(caught, file=sys.stderr)
    finally:
        try:
            report.finish(status, error)
        except Exception as save_error:
            # Keep the last complete snapshot and make publication failure fatal.
            # A hard process kill likewise leaves running, never a false passed.
            print(f"Cannot finalize verification report {report.path}: {save_error}", file=sys.stderr)
            exit_code = 1
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
