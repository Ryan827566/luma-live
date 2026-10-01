"""Smoke-test native sidebar and embedded AI lifetime without enabling capture/AI."""
import ctypes as c
from ctypes import wintypes as w
import subprocess
import sys
import time

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
    u.SendMessageW(root, 0x111, 127, 0)
    child = u.FindWindowExW(root, None, "LumaSessionAi", None)
    assert child and u.GetAncestor(child, 2) == root, "AI opened outside the studio"
    u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    label = c.create_unicode_buffer(128)
    u.GetWindowTextW(u.GetDlgItem(child, 2), label, 128)
    language = c.WinDLL("kernel32").GetUserDefaultUILanguage() & 0x3ff
    expected = "\u542f\u7528\u5b57\u5e55" if language == 4 else "Start captions"
    assert label.value == expected, "Caption action does not follow Windows display language"

    assert u.GetWindowLongW(child, -16) & 0x10000000
    u.SendMessageW(root, 0x111, 117, 0)
    assert not (u.GetWindowLongW(child, -16) & 0x10000000), "AI page not hidden"
    u.SendMessageW(root, 0x111, 127, 0)
    assert u.FindWindowExW(root, None, "LumaSessionAi", None) == child, "AI page recreated on navigation"
    assert u.GetWindowLongW(child, -16) & 0x10000000
    u.SendMessageW(root, 0x111, 126, 0)
    meeting = u.FindWindowExW(root, None, "LumaMeetingPreview", None)
    assert meeting and u.GetAncestor(meeting, 2) == root, "Meeting opened outside the studio"
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
        assert label.value != "\u79bb\u5f00\u623f\u95f4", "Call joined concurrently with meeting"
        u.SendMessageW(root, 0x111, 126, 0)
        assert u.SendMessageW(u.GetDlgItem(meeting, 210), 0x18B, 0, 0) == 3, "Navigation lost membership"
        u.PostMessageW(meeting, 0x111, 212, 0)
        u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
        dialogs = []
        @callback
        def dialog(hwnd, _):
            pid = w.DWORD(); u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(64); u.GetClassNameW(hwnd, name, 64)
            if pid.value == process.pid and name.value == "#32770": dialogs.append(hwnd)
            return True
        until = time.monotonic() + 3
        while not dialogs and time.monotonic() < until: u.EnumWindows(dialog, 0); time.sleep(.05)
        assert len(dialogs) == 1, "End confirmation missing"
        u.PostMessageW(dialogs[0], 0x111, 6, 0)
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
