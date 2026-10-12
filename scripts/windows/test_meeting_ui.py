"""Render native meeting layouts and verify controls without a PowerShell dependency."""
import os
from pathlib import Path
import subprocess
import sys
import time

binary = Path(sys.argv[1]).resolve()
results = binary / "ui-checks"
results.mkdir(parents=True, exist_ok=True)
env = {k: v for k, v in os.environ.items() if not k.startswith("LUMALIVE_MEETING_")}
startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0

def render(name):
    path = results / (name + ".bmp")
    env["LUMALIVE_MEETING_RENDER_PATH"] = str(path)
    if path.exists():
        path.unlink()
    subprocess.run([str(binary / "luma_studio.exe"), "--meeting"],
                   env=env, startupinfo=startup, check=True, timeout=20)
    assert path.is_file() and path.stat().st_size > 54, "Render missing"
    print("PASS:", name, flush=True)

for dpi in (96, 144, 192):
    env["LUMALIVE_MEETING_RENDER_DPI"] = str(dpi)
    render("idle-" + str(dpi))

with (results / "fixture.log").open("w") as log:
    fixture = subprocess.Popen([str(binary / "luma_meeting_ui_fixture.exe")],
                               startupinfo=startup, stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if "READY:" in (results / "fixture.log").read_text(errors="replace"):
                break
            if fixture.poll() is not None:
                raise RuntimeError("Fixture exited before initialization")
            time.sleep(.05)
        else:
            raise RuntimeError("Fixture not ready")
        env.update(LUMALIVE_MEETING_RENDER_DPI="96",
                   LUMALIVE_MEETING_TEST_SERVER="127.0.0.1:19730",
                   LUMALIVE_MEETING_TEST_ROOM="ui-review",
                   LUMALIVE_MEETING_TEST_IDENTITY="review-host",
                   LUMALIVE_MEETING_TEST_ACTION="create",
                   LUMALIVE_MEETING_TEST_CONTROLS="1")
        render("joined-controls")
        assert fixture.wait(timeout=5) == 0, "Fixture failed"
    finally:
        if fixture.poll() is None:
            fixture.terminate()
            fixture.wait()
print("PASS: three-member controls and 100/150/200 percent diagnostic renders")
