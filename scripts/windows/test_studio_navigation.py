"""Smoke-test native sidebar and embedded AI lifetime without enabling capture/AI."""
import ctypes as c
from ctypes import wintypes as w
import subprocess
import sys
import os
import time
from pathlib import Path

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
process = subprocess.Popen([sys.argv[1]], startupinfo=startup)
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

    assert u.GetWindowLongW(child, -16) & 0x10000000
    u.SendMessageW(root, 0x111, 117, 0)
    assert not (u.GetWindowLongW(child, -16) & 0x10000000), "AI page not hidden"
    u.SendMessageW(root, 0x111, 127, 0)
    assert u.FindWindowExW(root, None, "LumaSessionAi", None) == child, "AI page recreated on navigation"
    assert u.GetWindowLongW(child, -16) & 0x10000000
    u.SendMessageW(root, 0x111, 126, 0)
    meeting = u.FindWindowExW(root, None, "LumaMeetingPreview", None)
    assert meeting and u.GetAncestor(meeting, 2) == root, "Meeting opened outside the studio"
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
        assert u.SendMessageW(u.GetDlgItem(meeting, 210), 0x18B, 0, 0) == 3, "Cancel ended the meeting"
        u.SendMessageW(u.GetDlgItem(meeting, 212), 0xF5, 0, 0)
        u.SendMessageW(confirmation, 0xF5, 0, 0)
        assert fixture.wait(timeout=8) == 0, "Fixture did not observe meeting end"
        time.sleep(.2)
        print("PASS: three real participants retained across tabs, call exclusivity and end flow")
    print("PASS: left sidebar, embedded call/meeting AI, retained meeting page and isolated contexts")
finally:
    if fixture and fixture.poll() is None: fixture.terminate(); fixture.wait()
    if root: u.PostMessageW(root, 0x10, 0, 0)
    try: process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait(); raise RuntimeError("Studio did not shut down cleanly")
assert process.returncode == 0, process.returncode
print("PASS: clean shutdown")
