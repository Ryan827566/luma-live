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
    assert u.GetWindowLongW(child, -16) & 0x10000000
    u.SendMessageW(root, 0x111, 117, 0)
    assert not (u.GetWindowLongW(child, -16) & 0x10000000), "AI page not hidden"
    u.SendMessageW(root, 0x111, 127, 0)
    assert u.FindWindowExW(root, None, "LumaSessionAi", None) == child, "AI page recreated on navigation"
    assert u.GetWindowLongW(child, -16) & 0x10000000
    print("PASS: left sidebar, embedded AI, hide/return preserves page")
finally:
    if root: u.PostMessageW(root, 0x10, 0, 0)
    try: process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait(); raise RuntimeError("Studio did not shut down cleanly")
assert process.returncode == 0, process.returncode
print("PASS: clean shutdown")
