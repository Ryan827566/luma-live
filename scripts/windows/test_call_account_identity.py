"""Actual client: clear a private call alias when the account loses authentication.

Uses its own account fixture and temporary profile. Does not join a call room,
enable capture, contact a production account server, or log profile credentials.
Run inside a bounded Windows Job with the freshly built luma_studio.exe path;
luma_account_ui_fixture.exe must be in that executable's directory. Display
names deliberately never replace the protocol participant ID.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import tempfile
import time

from workspace_auth_fixture import WorkspaceAuthFixture, wait, u


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file() or not (binary.parent / 'luma_account_ui_fixture.exe').is_file():
        parser.error('Require the client and isolated account UI fixture in one directory.')
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    u.EnumWindows.argtypes = [callback, w.LPARAM]
    u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    root = client = fixture = None
    with tempfile.TemporaryDirectory(prefix='luma-call-identity-') as profile:
        env = {key.upper(): value for key, value in os.environ.items()
               if not key.upper().startswith('LUMALIVE_AUTH_')}
        env.update(LUMALIVE_AUTH_ENV='test', LUMALIVE_AUTH_TLS_CA_FILE='',
                   LOCALAPPDATA=profile, LUMALIVE_UI_LANGUAGE='en-US',
                   LUMALIVE_ACCOUNT_SETTINGS_PATH=str(Path(profile) / 'account.conf'))
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        try:
            fixture = WorkspaceAuthFixture(binary, env['LUMALIVE_ACCOUNT_SETTINGS_PATH'])
            client = subprocess.Popen([str(binary)], env=env, startupinfo=startup)

            def find_root():
                found = []
                @callback
                def visit(window, _):
                    pid = w.DWORD()
                    u.GetWindowThreadProcessId(window, c.byref(pid))
                    # Account navigation is created before the identity control. Wait
                    # for the login page built by the initial Layout as well.
                    if (pid.value == client.pid and u.GetDlgItem(window, 129)
                            and u.GetDlgItem(window, 118)
                            and u.FindWindowExW(window, None, 'LumaAccountPage', None)):
                        found.append(window)
                    return True
                u.EnumWindows(visit, 0)
                return found[0] if found else None

            root = wait(find_root, 'Client window missing')
            identity = u.GetDlgItem(root, 118)
            assert identity, 'Call display-name control missing'

            def text(window):
                value = c.create_unicode_buffer(512)
                u.SendMessageW(window, 0xD, len(value), c.cast(value, c.c_void_p).value)
                return value.value

            def fill(window, value):
                buffer = c.create_unicode_buffer(value)
                u.SendMessageW(window, 0xC, 0, c.cast(buffer, c.c_void_p).value)

            def click(window, control):
                u.SendMessageW(window, 0x111, control, 0)

            def visible(control):
                return bool(u.GetWindowLongW(u.GetDlgItem(root, control), -16) & 0x10000000)

            def unchanged(expected):
                # Observe multiple actual UI timer turns, not only one synchronous read.
                until = time.monotonic() + .3
                while time.monotonic() < until:
                    assert text(identity) == expected, 'Polling replaced the editable call name'
                    time.sleep(.05)

            fallback = f'studio-{client.pid}'
            assert text(identity) == fallback and not visible(117), 'Signed-out defaults changed'
            panel = fixture.login(root)
            assert text(identity) == fallback, 'Sign-in unexpectedly replaced the protocol participant ID'
            fill(identity, 'My call alias')
            click(root, 129)
            click(panel, 2100)
            fill(u.GetDlgItem(panel, 2122), 'Updated profile name')
            click(panel, 2160)
            wait(lambda: text(u.GetDlgItem(root, 129)) == 'Updated profile name', 'Profile update was not applied')
            click(root, 117)
            unchanged('My call alias')
            fill(identity, '')
            unchanged('')
            fill(identity, 'Alias before logout')
            click(root, 129)
            click(panel, 2100)
            click(panel, 2161)
            wait(lambda: not visible(117), 'Sign-out did not lock workspace')
            assert text(identity) == fallback, 'Sign-out retained the previous account call alias'
            # Use a second real account in the same fixture and client process.
            # No direct token/session injection or production authentication bypass.
            def fill_fields(*values):
                for index, value in enumerate(values):
                    fill(u.GetDlgItem(panel, 2120 + index), value)

            def complete():
                wait(lambda: u.IsWindowEnabled(u.GetDlgItem(panel, 2160)), 'Account request timed out')

            click(panel, 2170)
            fill_fields('workspace.other', 'other@example.com', '', 'other local test password', 'other local test password')
            click(panel, 2160)
            complete()
            assert not visible(117), 'Registration bypassed login'
            click(panel, 2172)
            fill_fields('workspace.other', 'other local test password', '')
            click(panel, 2160)
            complete()
            wait(lambda: visible(117), 'Second account did not unlock workspace')
            wait(lambda: text(u.GetDlgItem(root, 129)) == 'workspace.other', 'Second account was not active')
            assert text(identity) == fallback, 'Second account inherited the previous private call alias'
            click(root, 129)
            click(panel, 2100)
            fill(u.GetDlgItem(panel, 2122), 'Updated profile name')
            click(panel, 2160)
            wait(lambda: text(u.GetDlgItem(root, 129)) == 'Updated profile name', 'Second account profile update failed')
            click(root, 117)
            unchanged(fallback)
            fill(identity, 'Alias before server loss')
            # Closing this owned fixture exercises the same authentication-loss
            # boundary as logout, without touching any production service.
            fixture.close()
            wait(lambda: not visible(117), 'Server loss did not lock workspace', timeout=75)
            assert text(identity) == fallback, 'Authentication loss retained the previous call alias'
            print('PASS: editable call ID preserved in-session; logout, account switch and authentication loss clear private aliases')
            return 0
        finally:
            if root:
                u.PostMessageW(root, 0x10, 0, 0)
            try:
                if client is not None:
                    try:
                        client.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        client.kill()
                        client.wait(timeout=10)
                        raise
            finally:
                if fixture is not None:
                    fixture.close()


if __name__ == '__main__':
    raise SystemExit(main())
