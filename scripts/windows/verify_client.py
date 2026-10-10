"""Build and verify the actual standalone Windows client, not just its library."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build/d-drive")
    parser.add_argument("--binary-dir", default="output/next/Release")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = (root / args.build_dir).resolve()
    binary = (root / args.binary_dir).resolve()
    if os.name != "nt" or not (build / "CMakeCache.txt").is_file():
        parser.error("Requires Windows and an already configured standalone CMake build.")
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    configured = cache.get("CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE")
    if not configured or (build / configured).resolve() != binary:
        parser.error("--binary-dir must match CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE in this build's cache.")
    cmake = shutil.which("cmake") or str(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "CMake/bin/cmake.exe")
    env = {key.upper(): value for key, value in os.environ.items()}
    temporary = root / "output/tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    env["TEMP"] = env["TMP"] = str(temporary)
    env["MSBUILDDISABLENODEREUSE"] = "1"
    report = root / "output/client-verification.json"
    record = {"started_utc": datetime.now(timezone.utc).isoformat(), "status": "running", "checks": []}
    def save():
        report.write_text(json.dumps(record, indent=2), encoding="utf-8")
    def run(name, command, timeout):
        log_path = root / ("output/verify-" + name + ".log")
        with log_path.open("w", encoding="utf-8") as log:
            result = subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        record["checks"].append({"name": name, "exit_code": result.returncode, "log": str(log_path)})
        save()
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}); see {log_path}")
        print("PASS:", name, flush=True)
    save()
    try:
        targets = ["luma_studio", "luma_signaling_server", "luma_meeting_ui_fixture", "luma_session_ai_tests", "luma_session_ai_gateway_tests", "luma_caption_overlay_tests", "luma_ui_locale_tests", "luma_ui_accessibility_tests", "luma_transcript_view_tests"]
        run("build", [cmake, "--build", str(build), "--config", "Release", "--target", *targets, "--parallel", "1", "--", "/nr:false"], 1800)
        # The configured output must also be newer than the linked preview library.
        executable = binary / "luma_studio.exe"
        library = build / "Release/luma_studio_preview.lib"
        if not executable.is_file() or not library.is_file() or executable.stat().st_mtime_ns < library.stat().st_mtime_ns:
            raise RuntimeError("Client executable is missing or older than the preview library; check the CMake runtime output directory.")
        record["client"] = {"path": str(executable), "sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}
        run("accessibility", [str(binary / "luma_ui_accessibility_tests.exe")], 20)
        run("transcript-view", [str(binary / "luma_transcript_view_tests.exe")], 20)
        run("locale", [str(binary / "luma_ui_locale_tests.exe")], 20)
        run("ai-state", [str(binary / "luma_session_ai_tests.exe")], 30)
        run("captions", [str(binary / "luma_caption_overlay_tests.exe"), str(root / "output/caption-overlay.bmp")], 20)
        run("gateway", [sys.executable, "-m", "unittest", "discover", "-s", "LumaLive/server/ai-gateway/python", "-p", "test_gateway.py"], 60)
        run("native-ai", [sys.executable, "LumaLive/server/ai-gateway/python/test_native_client.py", str(binary / "luma_session_ai_gateway_tests.exe")], 60)
        run("navigation", [sys.executable, "scripts/windows/test_studio_navigation.py", str(executable), str(binary / "luma_meeting_ui_fixture.exe")], 60)
        run("meeting-ui", [sys.executable, "scripts/windows/test_meeting_ui.py", str(binary)], 90)
        record["status"] = "passed"
    except Exception as error:
        record["status"] = "failed"
        record["error"] = str(error)
        print(error, file=sys.stderr)
    finally:
        record["finished_utc"] = datetime.now(timezone.utc).isoformat()
        save()
    return 0 if record["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
