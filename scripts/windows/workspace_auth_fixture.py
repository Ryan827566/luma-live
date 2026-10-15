"""Sign the actual client into an isolated in-memory account service for UI tests.

There is no production authentication bypass: this drives the registration and
login controls and keeps the real protocol service alive for the test lifetime.
"""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import queue
import socket
import subprocess
import threading
import time

u = c.WinDLL('user32', use_last_error=True)
u.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]
u.FindWindowExW.restype = w.HWND
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = c.c_ssize_t
u.IsWindowEnabled.argtypes = [w.HWND]
u.GetWindowLongW.argtypes = [w.HWND, c.c_int]


def wait(predicate, message, timeout=15):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError(message)


class WorkspaceAuthFixture:
    def __init__(self, binary, settings_path):
        self.settings_path = Path(settings_path)
        assert self.settings_path.is_absolute(), 'Use an isolated absolute settings path'
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            self.port = reservation.getsockname()[1]
        env = {k.upper(): v for k, v in os.environ.items()
               if not k.upper().startswith('LUMALIVE_AUTH_')}
        self.process = subprocess.Popen(
            [str(Path(binary).resolve().parent / 'luma_account_ui_fixture.exe'), str(self.port)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env,
            creationflags=subprocess.CREATE_NO_WINDOW)
        ready = queue.Queue()
        threading.Thread(target=lambda: ready.put(self.process.stdout.readline()), daemon=True).start()
        try:
            assert ready.get(timeout=10).strip() == str(self.port), 'Account fixture failed to start'
            # The production entrance receives its endpoint before launch. Tests
            # never expose a connection form or write the user's real profile.
            self.settings_path.parent.mkdir(parents=True, exist_ok=True)
            self.settings_path.write_text(
                f'LumaLiveAccountConnection 1\nhost=127.0.0.1\nport={self.port}\n',
                encoding='utf-8')
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.communicate('\n', timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.communicate()

    def login(self, root):
        panel = wait(lambda: u.FindWindowExW(root, None, 'LumaAccountPage', None), 'Sign-in page missing')
        def control(i): return u.GetDlgItem(panel, i)
        def click(i): u.SendMessageW(panel, 0x111, i, 0)
        def fill(*values):
            for index, value in enumerate(values):
                data = c.create_unicode_buffer(value)
                u.SendMessageW(control(2120 + index), 0xC, 0, c.cast(data, c.c_void_p).value)
        def complete(): wait(lambda: u.IsWindowEnabled(control(2160)), 'Account request timed out')
        assert not u.GetWindowLongW(u.GetDlgItem(root, 117), -16) & 0x10000000, 'Workspace exposed before login'
        # Even a direct command must not open the AI page before authentication.
        u.SendMessageW(root, 0x111, 127, 0)
        assert not u.FindWindowExW(root, None, 'LumaSessionAi', None), 'Pre-login command opened AI'
        for i in range(2100, 2104):
            assert not u.GetWindowLongW(control(i), -16) & 0x10000000, 'Signed-out tab exposed'
            click(i)
        assert not u.GetWindowLongW(control(2164), -16) & 0x10000000, 'Signed-out detail exposed'
        click(2170); fill('workspace.tester', 'workspace@example.com', '', 'local test password', 'local test password')
        click(2160); complete()
        assert not u.GetWindowLongW(u.GetDlgItem(root, 117), -16) & 0x10000000, 'Registration bypassed login'
        click(2172); fill('workspace.tester', 'local test password', ''); click(2160); complete()
        wait(lambda: u.GetWindowLongW(u.GetDlgItem(root, 117), -16) & 0x10000000, 'Valid login did not unlock workspace')
        return panel
