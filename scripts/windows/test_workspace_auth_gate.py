"""Actual EXE: locked launch, account identity, logout and server-loss validation."""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from workspace_auth_fixture import WorkspaceAuthFixture, wait, u

binary = Path(sys.argv[1]).resolve()
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [callback, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]


with tempfile.TemporaryDirectory(prefix='luma-gate-') as profile:
    env = {k.upper(): v for k, v in os.environ.items()}
    env.update(LOCALAPPDATA=profile, LUMALIVE_UI_LANGUAGE='en-US',
               LUMALIVE_ACCOUNT_SETTINGS_PATH=str(Path(profile) / 'account.conf'))
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    fixture = WorkspaceAuthFixture(binary, env['LUMALIVE_ACCOUNT_SETTINGS_PATH'])
    # Legacy diagnostic switches must no longer expose an interactive meeting.
    client = subprocess.Popen([str(binary), '--meeting', '--ui-regression'], env=env, startupinfo=startup)
    root = None
    try:
        def find_root():
            found = []
            @callback
            def visit(h, _):
                pid = w.DWORD(); u.GetWindowThreadProcessId(h, c.byref(pid))
                if pid.value == client.pid and u.GetDlgItem(h, 129): found.append(h)
                return True
            u.EnumWindows(visit, 0)
            return found[0] if found else None
        root = wait(find_root, 'Client missing')
        panel = fixture.login(root)
        def visible(i): return bool(u.GetWindowLongW(u.GetDlgItem(root, i), -16) & 0x10000000)
        def text(h):
            value = c.create_unicode_buffer(512)
            u.SendMessageW(h, 0xD, len(value), c.cast(value, c.c_void_p).value)
            return value.value
        assert text(u.GetDlgItem(root, 129)) == 'workspace.tester', 'Account identity missing'
        u.SendMessageW(root, 0x111, 127, 0)
        assert u.FindWindowExW(root, None, 'LumaSessionAi', None)
        u.SendMessageW(root, 0x111, 129, 0)
        u.SendMessageW(panel, 0x111, 2100, 0)
        u.SendMessageW(panel, 0x111, 2161, 0)
        wait(lambda: not visible(117), 'Logout did not lock workspace')
        assert not u.FindWindowExW(root, None, 'LumaSessionAi', None), 'Logout retained AI page'
        for i in (117, 126, 127, 100, 101): u.SendMessageW(root, 0x111, i, 0)
        assert not visible(117) and not u.FindWindowExW(root, None, 'LumaMeetingPreview', None)
        fixture.login(root)
        # An unanswered confirmation must not suspend background validation,
        # including when its account page is hidden behind another module.
        u.SendMessageW(root, 0x111, 129, 0)
        u.SendMessageW(panel, 0x111, 2100, 0)
        u.SendMessageW(panel, 0x111, 2162, 0)
        assert u.GetWindowLongW(u.GetDlgItem(panel,2166),-16)&0x10000000
        u.SendMessageW(root, 0x111, 117, 0)
        fixture.close()
        wait(lambda: not visible(117), 'Periodic validation retained a disconnected account', timeout=75)
        assert 'Sign in' in text(u.GetDlgItem(panel, 2100))
        assert not visible(126) and not visible(127) and not visible(129)
        print('PASS: locked launch/commands, real sign-in, identity, logout, re-login and server-loss validation with hidden pending confirmation')
    finally:
        if root: u.PostMessageW(root, 0x10, 0, 0)
        try: client.wait(timeout=15)
        except subprocess.TimeoutExpired: client.kill(); client.wait(); raise
        if fixture: fixture.close()
