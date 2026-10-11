"""Smoke-test native sidebar and embedded AI lifetime without enabling capture/AI."""
import ctypes as c
from ctypes import wintypes as w
import subprocess
import sys
import os
import time
import uuid
import shutil
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "LumaLive/server/ai-gateway/python"))
from gateway import Config, GatewayServer, Handler

class HealthHandler(Handler):
    def do_GET(self):
        self.server.health_checks += 1
        super().do_GET()

u = c.WinDLL("user32", use_last_error=True)
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [callback, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetDlgItem.argtypes = [w.HWND, c.c_int]; u.GetDlgItem.restype = w.HWND
u.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]; u.FindWindowExW.restype = w.HWND
u.GetAncestor.argtypes = [w.HWND, w.UINT]; u.GetAncestor.restype = w.HWND
u.GetWindowLongW.argtypes = [w.HWND, c.c_int]; u.GetWindowLongW.restype = w.LONG
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]; u.SendMessageW.restype = c.c_ssize_t
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.MapWindowPoints.argtypes = [w.HWND, w.HWND, c.c_void_p, w.UINT]
startup = subprocess.STARTUPINFO(); startup.dwFlags = subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
profile_parent = Path(sys.argv[1]).resolve().parents[2] / "tmp"
profile_parent.mkdir(parents=True, exist_ok=True)
profile = profile_parent / ("luma-ui-profile-" + uuid.uuid4().hex)
profile.mkdir()
env = {key.upper(): value for key, value in os.environ.items()}; env["LOCALAPPDATA"] = str(profile)
archive_fixture = Path(sys.argv[1]).resolve().parent / "luma_session_archive_tests.exe"
subprocess.run([str(archive_fixture), "--write-fixture", str(profile / "LumaLive/records")], env=env, check=True)
health_server = GatewayServer(Config(), port=0)
health_server.RequestHandlerClass = HealthHandler
health_server.health_checks = 0
health_thread = threading.Thread(target=health_server.serve_forever, kwargs={"poll_interval": .01}, daemon=True)
health_thread.start()
env["LUMALIVE_AI_GATEWAY_PORT"] = str(health_server.server_port)
process = subprocess.Popen([sys.argv[1]], startupinfo=startup, env=env)
root = None
fixture = None
try:
    until = time.monotonic() + 20
    while time.monotonic() < until and not root:
        found = []
        @callback
        def visit(hwnd, _):
            pid = w.DWORD(); u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            if pid.value == process.pid: found.append(hwnd)
            return True
        u.EnumWindows(visit, 0)
        for hwnd in found:
            if u.GetDlgItem(hwnd, 127): root = hwnd; break
        if process.poll() is not None: raise RuntimeError("Studio exited before initialization")
        time.sleep(.1)
    assert root, "No studio window"
    time.sleep(2)
    u.GetDpiForWindow.argtypes = [w.HWND]; u.GetDpiForWindow.restype = w.UINT
    scale = u.GetDpiForWindow(root) / 96
    if os.environ.get("LUMALIVE_TEST_WINDOW"):
        width, height = map(int, os.environ["LUMALIVE_TEST_WINDOW"].split("x"))
        u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
        assert u.SetWindowPos(root, None, 0, 0, round(width * scale), round(height * scale), 0x16)
    u.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    if os.environ.get("LUMALIVE_TEST_WINDOW"):
        actual = w.RECT(); assert u.GetClientRect(root, c.byref(actual))
        assert actual.right <= round(width * scale) and actual.bottom <= round(height * scale), "Window minimum prevented compact verification"
    def fits(parent, ids):
        bounds = w.RECT(); assert u.GetClientRect(parent, c.byref(bounds))
        for control_id in ids:
            h = u.GetDlgItem(parent, control_id)
            if not h or not (u.GetWindowLongW(h, -16) & 0x10000000): continue
            rect = w.RECT(); assert u.GetWindowRect(h, c.byref(rect))
            u.MapWindowPoints(None, parent, c.byref(rect), 2)
            assert 0 <= rect.left < rect.right <= bounds.right and 0 <= rect.top < rect.bottom <= bounds.bottom, f"Control {control_id} clipped: {(rect.left, rect.top, rect.right, rect.bottom)} in {(bounds.right, bounds.bottom)}"
    fits(root, range(100, 129))
    volume = u.GetDlgItem(root, 104)
    before_volume = u.SendMessageW(volume, 0x400, 0, 0)
    u.SendMessageW(volume, 0x100, 0x27, 0)
    assert u.SendMessageW(volume, 0x400, 0, 0) == before_volume + 1, "Volume lost native Right key adjustment"
    u.SendMessageW(volume, 0x100, 0x25, 0)
    assert u.SendMessageW(volume, 0x400, 0, 0) == before_volume, "Volume lost native Left key adjustment"
    u.SendMessageW(root, 0x111, 128, 0)
    fits(root, range(100, 129))
    u.SendMessageW(root, 0x111, 128, 0)
    u.GetNextDlgTabItem.argtypes = [w.HWND, w.HWND, w.BOOL]; u.GetNextDlgTabItem.restype = w.HWND
    def sidebar_order():
        assert u.GetNextDlgTabItem(root, None, False) == u.GetDlgItem(root, 117), "Sidebar Tab entry is not Calls"
        for before, after in ((117, 126), (126, 127)):
            assert u.GetNextDlgTabItem(root, u.GetDlgItem(root, before), False) == u.GetDlgItem(root, after), "Sidebar Tab order differs from visual order"
            assert u.GetNextDlgTabItem(root, u.GetDlgItem(root, after), True) == u.GetDlgItem(root, before), "Sidebar reverse Tab order differs"
    sidebar_order()
    for control_id in (117, 126, 127):
        rect = w.RECT(); assert u.GetWindowRect(u.GetDlgItem(root, control_id), c.byref(rect))
        u.MapWindowPoints(None, root, c.byref(rect), 2)
        assert 0 <= rect.left < rect.right <= 176 * scale, f"Module {control_id} outside sidebar: {rect.left}, {rect.right}, scale={scale}"
    # Native owner-drawn combos include non-client borders in their outer height.
    # Validate geometry as well as CB_GETITEMHEIGHT, so taller fields cannot cover actions.
    for combo_id, button_id in ((107, 101), (108, 102), (119, 120)):
        combo = u.GetDlgItem(root, combo_id)
        assert u.SendMessageW(combo, 0x154, c.c_size_t(-1).value, 0) == round(34 * scale)
        assert u.SendMessageW(combo, 0x154, 0, 0) == round(30 * scale)
        field, action = w.RECT(), w.RECT()
        assert u.GetWindowRect(combo, c.byref(field))
        assert u.GetWindowRect(u.GetDlgItem(root, button_id), c.byref(action))
        assert action.top - field.bottom >= round(8 * scale), "Call dropdown crowds its action"
    u.SendMessageW(root, 0x111, 127, 0)
    child = u.FindWindowExW(root, None, "LumaSessionAi", None)
    assert child and u.GetAncestor(child, 2) == root, "AI opened outside the studio"
    sidebar_order()
    fits(child, range(1, 11))
    u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
    result = c.c_size_t()
    assert u.SendMessageTimeoutW(root, 0x111, 100, 0, 2, 2000, c.byref(result)), "Hidden call file action opened a modal dialog from AI"

    u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    label = c.create_unicode_buffer(128)
    u.GetWindowTextW(u.GetDlgItem(child, 2), label, 128)
    language = c.WinDLL("kernel32").GetUserDefaultUILanguage() & 0x3ff
    if os.environ.get("LUMALIVE_UI_LANGUAGE"): language = 4 if os.environ["LUMALIVE_UI_LANGUAGE"].startswith("zh") else 0
    expected = "\u542f\u7528\u5b57\u5e55" if language == 4 else "Start captions"
    traditional = os.environ.get("LUMALIVE_UI_LANGUAGE") in ("zh-TW", "zh-HK")
    if traditional: expected = "啟用字幕"
    assert label.value == expected, "Caption action does not follow Windows display language"
    accessibility = Path(sys.argv[1]).resolve().parent / "luma_ui_accessibility_tests.exe"
    expected_name = ("翻譯目標語言" if traditional else "翻译目标语言") if language == 4 else "Translation language"
    subprocess.run([str(accessibility), str(u.GetDlgItem(child, 5)), expected_name], check=True)
    language_combo = u.GetDlgItem(child, 5)
    assert u.SendMessageW(language_combo, 0x154, c.c_size_t(-1).value, 0) == round(34 * scale), "AI selection height differs from shared metrics"
    assert u.SendMessageW(language_combo, 0x154, 0, 0) == round(30 * scale), "AI list row height differs from shared metrics"
    assert u.SendMessageW(language_combo, 0x146, 0, 0) == 5, "Translation languages missing"
    u.SendMessageW(language_combo, 0x14E, 0, 0)
    u.SendMessageW(language_combo, 0x100, 0x28, 0)  # native Down selection
    assert u.SendMessageW(language_combo, 0x147, 0, 0) == 1, "Styled combo lost keyboard selection"
    u.SendMessageW(language_combo, 0x100, 0x26, 0)
    assert u.SendMessageW(language_combo, 0x147, 0, 0) == 0, "Styled combo lost reverse selection"
    assert u.GetDlgItem(child, 9), "Keyword extraction action missing"
    u.GetNextDlgTabItem.argtypes = [w.HWND, w.HWND, w.BOOL]; u.GetNextDlgTabItem.restype = w.HWND
    order = (2, 3, 9, 7, 5, 6, 10, 8, 4)
    for previous, following in zip(order, order[1:]):
        assert u.GetNextDlgTabItem(child, u.GetDlgItem(child, previous), False) == u.GetDlgItem(child, following), "AI Tab order differs from visual order"
        assert u.GetNextDlgTabItem(child, u.GetDlgItem(child, following), True) == u.GetDlgItem(child, previous), "AI reverse Tab order differs"
    # Queued Enter exercises the production IsDialogMessage path.
    before = c.create_unicode_buffer(128)
    u.SendMessageW(u.GetDlgItem(child, 4), 0xD, 128, c.cast(before, c.c_void_p).value)
    u.PostMessageW(u.GetDlgItem(child, 9), 0x100, 0x0D, 0)
    until = time.monotonic() + 2
    while time.monotonic() < until:
        u.SendMessageW(u.GetDlgItem(child, 4), 0xD, 128, c.cast(label, c.c_void_p).value)
        if label.value != before.value: break
        time.sleep(.05)
    u.SendMessageW(u.GetDlgItem(child, 4), 0xD, 128, c.cast(label, c.c_void_p).value)
    guidance = "\u8bf7\u5148\u542f\u7528\u5b57\u5e55" if language == 4 else "Start captions and collect"
    if traditional: guidance = "請先啟用字幕"
    assert label.value.startswith(guidance), "Empty keyword guidance mismatch: " + ascii(label.value) + " expected " + ascii(guidance)


    transcript = u.GetDlgItem(child, 4)
    original = c.create_unicode_buffer(4096)
    u.SendMessageW(transcript, 0xD, 4096, c.cast(original, c.c_void_p).value)
    assert not (u.GetWindowLongW(transcript, -16) & 0x200000), "Short transcript has an unnecessary scrollbar"
    long_record = c.create_unicode_buffer("\r\n".join("Transcript line " + str(i) for i in range(100)))
    u.SendMessageW(transcript, 0xC, 0, c.cast(long_record, c.c_void_p).value)
    u.SendMessageW(child, 0x5, 0, 0)
    assert u.GetWindowLongW(transcript, -16) & 0x200000, "Long transcript cannot be scrolled"
    u.SendMessageW(transcript, 0x20A, (0xFF88 << 16), 0)  # wheel down
    assert u.SendMessageW(transcript, 0xCE, 0, 0) > 0, "Native transcript wheel scrolling failed"
    u.SendMessageW(transcript, 0xC, 0, c.cast(original, c.c_void_p).value)
    u.SendMessageW(child, 0x5, 0, 0)
    assert not (u.GetWindowLongW(transcript, -16) & 0x200000), "Scrollbar did not hide after restoring short text"

    # Exercise actual hosted settings and archive views in an isolated Windows profile.
    def text_of(hwnd):
        value = c.create_unicode_buffer(16384)
        u.SendMessageW(hwnd, 0xD, len(value), c.cast(value, c.c_void_p).value)
        return value.value
    def set_text(hwnd, value):
        buffer = c.create_unicode_buffer(value)
        u.SendMessageW(hwnd, 0xC, 0, c.cast(buffer, c.c_void_p).value)
    u.SendMessageW(child, 0x111, 14, 0)
    assert text_of(u.GetDlgItem(child, 15)), "Empty record save lacks inline guidance"
    u.SendMessageW(child, 0x111, 12, 0)
    fits(child, range(1, 47))
    assert not (u.GetWindowLongW(transcript, -16) & 0x10000000), "Live transcript overlaps history"
    archived = u.GetDlgItem(child, 17)
    saved_text = text_of(archived)
    assert saved_text, "Saved fixture record was not loaded"
    assert u.SendMessageW(archived, 0xBA, 0, 0) >= 3, "Archive line breaks were collapsed"
    time.sleep(.4)
    assert text_of(archived) == saved_text, "Live refresh overwrote historical record"
    call_records = profile / 'LumaLive/records/call'
    meeting_records = profile / 'LumaLive/records/meeting'
    saved_call_files = {p.name: p.read_bytes() for p in call_records.glob('*.md')}
    saved_meeting_files = {p.name: p.read_bytes() for p in meeting_records.glob('*.md')}
    export_root = profile / 'export destination'
    set_text(u.GetDlgItem(child, 43), str(export_root))
    u.SendMessageW(child, 0x111, 44, 0)
    deadline = time.monotonic() + 8
    while str(export_root) not in text_of(u.GetDlgItem(child, 45)) and time.monotonic() < deadline: time.sleep(.05)
    exported = {p.name: p.read_bytes() for p in (export_root / 'call').glob('*.md')}
    assert exported == saved_call_files, 'Inline export changed saved bytes or context'
    export_result = text_of(u.GetDlgItem(child, 45))
    assert str(export_root) in export_result, 'Export path missing from inline result'
    u.SendMessageW(child, 0x111, 44, 0)
    deadline = time.monotonic() + 8
    while text_of(u.GetDlgItem(child, 45)) == export_result or 'Exporting.' in text_of(u.GetDlgItem(child, 45)) or '\u6b63\u5728\u5bfc\u51fa' in text_of(u.GetDlgItem(child, 45)):
        assert time.monotonic() < deadline, 'Export did not finish'
        time.sleep(.05)
    assert {p.name: p.read_bytes() for p in (export_root / 'call').glob('*.md')} == exported
    assert text_of(u.GetDlgItem(child, 45)) != export_result, 'Duplicate export was reported as success'
    assert {p.name: p.read_bytes() for p in call_records.glob('*.md')} == saved_call_files
    hidden_export = profile / 'hidden export'
    set_text(u.GetDlgItem(child, 43), str(hidden_export))
    u.SendMessageW(child, 0x111, 11, 0)
    u.SendMessageW(child, 0x111, 44, 0)
    assert not hidden_export.exists(), 'Hidden history page exported a file'
    u.SendMessageW(child, 0x111, 12, 0)
    u.SendMessageW(u.GetDlgItem(child, 36), 0xF5, 0, 0)
    u.SendMessageW(child, 0x111, 44, 0)
    assert not hidden_export.exists(), 'Pending deletion allowed export'
    print('PASS: inline archive export, duplicate preservation and hidden-page protection')
    confirmation = u.GetDlgItem(child, 38)
    assert u.GetWindowLongW(confirmation, -16) & 0x10000000, 'Archive confirmation is not inline'
    assert u.GetAncestor(confirmation, 2) == root, 'Archive deletion opened another window'
    fits(child, range(1, 47))
    u.PostMessageW(u.GetDlgItem(child, 39), 0x100, 0x1B, 0)
    deadline = time.monotonic() + 2
    while u.GetWindowLongW(confirmation, -16) & 0x10000000 and time.monotonic() < deadline: time.sleep(.05)
    assert not (u.GetWindowLongW(confirmation, -16) & 0x10000000), 'Escape did not cancel archive deletion'
    assert {p.name: p.read_bytes() for p in call_records.glob('*.md')} == saved_call_files
    u.SendMessageW(u.GetDlgItem(child, 36), 0xF5, 0, 0)
    u.SendMessageW(child, 0x111, 11, 0)
    u.SendMessageW(child, 0x111, 12, 0)
    assert not (u.GetWindowLongW(confirmation, -16) & 0x10000000), 'Navigation retained stale deletion confirmation'
    u.SendMessageW(child, 0x111, 38, 0)
    assert {p.name: p.read_bytes() for p in call_records.glob('*.md')} == saved_call_files, 'Hidden confirmation deleted a record'
    u.SendMessageW(u.GetDlgItem(child, 36), 0xF5, 0, 0)
    u.SendMessageW(confirmation, 0xF5, 0, 0)
    assert not list(call_records.glob('*.md')), 'Confirmed archive was not deleted'
    assert {p.name: p.read_bytes() for p in meeting_records.glob('*.md')} == saved_meeting_files, 'Call deletion changed meeting archives'
    assert not text_of(archived), 'Deleted record remains visible'
    assert text_of(transcript) == original.value, 'Deleting an archive changed the live session'
    fits(child, range(1, 47))
    print('PASS: inline archive deletion, Escape/navigation cancellation and context isolation')
    u.SendMessageW(child, 0x111, 13, 0)
    fits(child, range(1, 47))
    health_label = u.GetDlgItem(child, 29)
    def check_health(english, chinese):
        u.SendMessageW(child, 0x111, 28, 0)
        deadline = time.monotonic() + 7
        while time.monotonic() < deadline:
            message = text_of(health_label)
            if english in message or chinese in message:
                fits(child, range(1, 47))
                return message
            time.sleep(.05)
        raise AssertionError("Local service state was not displayed: " + text_of(health_label))
    check_health('incomplete', '\u4e0d\u5b8c\u6574')
    assert health_server.health_checks == 1, "Local check did not reach the fixture gateway"
    assert u.GetWindowLongW(u.GetDlgItem(child, 21), -16) & 0x20, "Provider key is not masked"
    values = ("https://example.invalid/v1", "ui-fixture-secret", "asr-fixture", "chat-fixture", "", "")
    for control_id, value in zip(range(20, 26), values): set_text(u.GetDlgItem(child, control_id), value)
    u.SendMessageW(child, 0x111, 26, 0)
    config_path = profile / "LumaLive/ai-provider.dat"
    assert config_path.is_file(), "Settings page did not save configuration"
    encrypted_config = config_path.read_bytes()
    assert b"ui-fixture-secret" not in encrypted_config, "Provider key saved as plaintext"
    health_server.config = Config('https://example.invalid/v1', 'fixture-key', 'asr', 'chat')
    ready_text = check_health('not verified', '\u5c1a\u672a')
    assert 'ui-fixture-secret' not in ready_text
    set_text(u.GetDlgItem(child, 20), "http://example.invalid/v1")
    assert text_of(health_label) != ready_text, "Editing settings retained stale readiness"
    checks_before_edit = health_server.health_checks
    u.SendMessageW(child, 0x111, 28, 0)
    time.sleep(.4)
    assert health_server.health_checks == checks_before_edit, "Unsaved settings triggered a stale-config check"
    u.SendMessageW(child, 0x111, 26, 0)
    assert config_path.read_bytes() == encrypted_config, "Invalid URL replaced saved settings"
    set_text(u.GetDlgItem(child, 20), values[0])
    u.SendMessageW(child, 0x111, 26, 0)
    health_server.shutdown(); health_server.server_close(); health_thread.join(2)
    check_health('unavailable', '\u4e0d\u53ef\u7528')
    print("PASS: inline local AI service status, dirty-state invalidation and offline guidance")
    u.SendMessageW(child, 0x111, 11, 0)
    fits(child, range(1, 47))
    assert text_of(transcript) == original.value, "Subpage navigation changed active session"
    print("PASS: inline AI settings, encrypted save, archive viewer and session preservation")

    assert u.GetWindowLongW(child, -16) & 0x10000000
    u.SendMessageW(root, 0x111, 117, 0)
    assert not (u.GetWindowLongW(child, -16) & 0x10000000), "AI page not hidden"
    u.SendMessageW(root, 0x111, 127, 0)
    assert u.FindWindowExW(root, None, "LumaSessionAi", None) == child, "AI page recreated on navigation"
    assert u.GetWindowLongW(child, -16) & 0x10000000
    u.SendMessageW(root, 0x111, 126, 0)
    meeting = u.FindWindowExW(root, None, "LumaMeetingPreview", None)
    assert meeting and u.GetAncestor(meeting, 2) == root, "Meeting opened outside the studio"
    sidebar_order()
    fits(meeting, range(200, 225))
    u.SendMessageW(meeting, 0x111, 222, 0)
    fits(meeting, range(200, 225))
    u.SendMessageW(meeting, 0x111, 222, 0)
    assert u.SendMessageTimeoutW(root, 0x111, 100, 0, 2, 2000, c.byref(result)), "Hidden call file action opened a modal dialog from meeting"

    u.SendMessageW(root, 0x111, 117, 0)
    assert not (u.GetWindowLongW(meeting, -16) & 0x10000000)
    u.SendMessageW(root, 0x111, 126, 0)
    assert u.FindWindowExW(root, None, "LumaMeetingPreview", None) == meeting
    u.SendMessageW(meeting, 0x111, 221, 0)
    meeting_ai = u.FindWindowExW(root, None, "LumaSessionAi", None)
    if meeting_ai == child: meeting_ai = u.FindWindowExW(root, child, "LumaSessionAi", None)
    assert meeting_ai and meeting_ai != child, "Meeting AI context not separated from call"
    assert not (u.GetWindowLongW(meeting, -16) & 0x10000000)
    style = u.GetWindowLongW(root, -16)
    u.SendMessageW(root, 0x111, 113, 0)
    assert u.GetWindowLongW(root, -16) == style, "AI should not enter unsupported fullscreen"
    u.SendMessageW(root, 0x111, 126, 0)
    u.SendMessageW(meeting, 0x111, 218, 0)
    assert u.GetWindowLongW(root, -16) != style, "Meeting fullscreen did not affect root"
    u.SendMessageW(meeting, 0x111, 218, 0)
    assert u.GetWindowLongW(root, -16) == style, "Meeting fullscreen did not restore root"
    assert not (u.GetWindowLongW(meeting_ai, -16) & 0x10000000)
    if len(sys.argv) > 2:
        fixture = subprocess.Popen([sys.argv[2]], startupinfo=startup, stdout=subprocess.DEVNULL)
        time.sleep(1)
        def text(control_id, value):
            buffer = c.create_unicode_buffer(value)
            u.SendMessageW(u.GetDlgItem(meeting, control_id), 0xC, 0, c.cast(buffer, c.c_void_p).value)
        text(200, "127.0.0.1:19730"); text(201, "ui-review"); text(202, "review-host")
        u.SendMessageW(meeting, 0x111, 203, 0)
        until = time.monotonic() + 8
        while u.SendMessageW(u.GetDlgItem(meeting, 210), 0x18B, 0, 0) < 3 and time.monotonic() < until: time.sleep(.1)
        assert u.SendMessageW(u.GetDlgItem(meeting, 210), 0x18B, 0, 0) == 3, "Real meeting did not join"
        u.SendMessageW(root, 0x111, 117, 0)
        u.SendMessageW(root, 0x111, 114, 0)  # call join must be blocked while meeting owns session
        u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
        label = c.create_unicode_buffer(128); u.GetWindowTextW(u.GetDlgItem(root, 114), label, 128)
        assert label.value not in ("\u79bb\u5f00\u623f\u95f4", "Leave room"), "Call joined concurrently with meeting"
        u.SendMessageW(root, 0x111, 126, 0)
        assert u.SendMessageW(u.GetDlgItem(meeting, 210), 0x18B, 0, 0) == 3, "Navigation lost membership"
        fits(meeting, range(200, 225))
        class GuiThreadInfo(c.Structure):
            _fields_ = [("cbSize", w.DWORD), ("flags", w.DWORD), ("active", w.HWND), ("focus", w.HWND), ("capture", w.HWND), ("menu", w.HWND), ("moveSize", w.HWND), ("caret", w.HWND), ("caretRect", w.RECT)]
        u.GetGUIThreadInfo.argtypes = [w.DWORD, c.POINTER(GuiThreadInfo)]
        u.ShowWindow.argtypes = [w.HWND, c.c_int]
        u.ShowWindow(root, 4)  # Visible test window without activating it.
        style = u.GetWindowLongW(root, -16)
        thread_id = u.GetWindowThreadProcessId(root, None)
        def focused():
            info = GuiThreadInfo(); info.cbSize = c.sizeof(info)
            assert u.GetGUIThreadInfo(thread_id, c.byref(info))
            return info.focus
        for source in (117, 127):
            u.SendMessageW(root, 0x111, source, 0)
            u.SendMessageW(root, 0x10, 0, 0)
            try:
                assert u.GetWindowLongW(meeting, -16) & 0x10000000, "Close did not reveal the meeting"
                assert focused() == u.GetDlgItem(meeting, 224), "Close confirmation did not focus its visible Cancel action"
            finally:
                u.SendMessageW(u.GetDlgItem(meeting, 224), 0xF5, 0, 0)
            assert u.SendMessageW(u.GetDlgItem(meeting, 210), 0x18B, 0, 0) == 3, "Close cancellation lost membership"
        u.SendMessageW(meeting, 0x111, 218, 0)
        fullscreen_style = u.GetWindowLongW(root, -16)
        assert fullscreen_style != style, "Meeting did not enter fullscreen"
        u.SendMessageW(u.GetDlgItem(meeting, 212), 0xF5, 0, 0)  # native button click
        u.IsWindowVisible.argtypes = [w.HWND]; u.IsWindowVisible.restype = w.BOOL
        confirmation = u.GetDlgItem(meeting, 223)
        until = time.monotonic() + 3
        while confirmation and not (u.GetWindowLongW(confirmation, -16) & 0x10000000) and time.monotonic() < until: time.sleep(.05)
        assert confirmation and (u.GetWindowLongW(confirmation, -16) & 0x10000000), "Inline end confirmation missing"
        u.PostMessageW(u.GetDlgItem(meeting, 224), 0x100, 0x1B, 0)  # Escape cancels
        until = time.monotonic() + 2
        while u.GetWindowLongW(confirmation, -16) & 0x10000000 and time.monotonic() < until: time.sleep(.05)
        assert not (u.GetWindowLongW(confirmation, -16) & 0x10000000), "Escape did not cancel inline confirmation"
        assert focused() == u.GetDlgItem(meeting, 212), "Cancelling End did not return focus to its initiating action"
        assert u.SendMessageW(u.GetDlgItem(meeting, 210), 0x18B, 0, 0) == 3, "Cancel ended the meeting"
        assert u.GetWindowLongW(root, -16) == fullscreen_style, "Escape left fullscreen instead of only cancelling confirmation"
        u.PostMessageW(u.GetDlgItem(meeting, 205), 0x100, 0x1B, 1 << 30)
        time.sleep(.1)
        assert u.GetWindowLongW(root, -16) == fullscreen_style, "Held Escape also exited fullscreen"
        u.PostMessageW(u.GetDlgItem(meeting, 205), 0x100, 0x1B, 0)
        until = time.monotonic() + 2
        while u.GetWindowLongW(root, -16) != style and time.monotonic() < until: time.sleep(.05)
        assert u.GetWindowLongW(root, -16) == style, "Second Escape did not leave fullscreen"
        u.SendMessageW(u.GetDlgItem(meeting, 212), 0xF5, 0, 0)
        u.SendMessageW(confirmation, 0xF5, 0, 0)
        assert fixture.wait(timeout=8) == 0, "Fixture did not observe meeting end"
        time.sleep(.2)
        print("PASS: three real participants retained across tabs, call exclusivity and end flow")
    print("PASS: left sidebar, embedded call/meeting AI, retained meeting page and isolated contexts")
finally:
    health_server.shutdown(); health_server.server_close(); health_thread.join(2)
    if fixture and fixture.poll() is None: fixture.terminate(); fixture.wait()
    if root: u.PostMessageW(root, 0x10, 0, 0)
    try: process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait(); raise RuntimeError("Studio did not shut down cleanly")
assert profile.resolve().is_relative_to(profile_parent.resolve())
shutil.rmtree(profile)
assert process.returncode == 0, process.returncode
print("PASS: clean shutdown")
