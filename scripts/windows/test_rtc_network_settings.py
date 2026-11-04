"""Actual client RTC settings acceptance in an owned Job and private profile.

Reuses the release-smoke process owner and real in-memory authentication fixture.
No call/meeting is joined and no capture is enabled. This proves settings UI and
DPAPI persistence, not native RTC connectivity or cross-network TURN behavior.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import uuid

PERMIT = b'LumaLive isolated RTC settings 1\n'
ENTRY, ENABLE, STUN, TURN, USER, PASSWORD, STATUS = 130, 3100, 3101, 3102, 3103, 3104, 3105
VALUES = ('stun:qa-network.invalid:3478', 'turns:qa-relay.invalid:5349?transport=tcp',
          'qa-network-user', 'qa-network-secret-synthetic-7319')


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def text(window):
    buffer = c.create_unicode_buffer(4096)
    u.SendMessageW(window, 0xD, len(buffer), c.addressof(buffer))
    return buffer.value


def fill(window, value):
    buffer = c.create_unicode_buffer(value)
    u.SendMessageW(window, 0xC, 0, c.addressof(buffer))


def click(window, identifier):
    require(u.PostMessageW(window, 0x111, identifier, 0), 'UI command could not be posted')


def windows(pid, predicate):
    found = []
    @callback
    def visit(window, _):
        owner = w.DWORD()
        u.GetWindowThreadProcessId(window, c.byref(owner))
        if owner.value == pid and predicate(window):
            found.append(window)
        return True
    u.EnumWindows(visit, 0)
    return found


def capture(window, destination):
    # Adapted from capture_studio_ui.py: PrintWindow only the owned dialog.
    g = c.WinDLL('gdi32', use_last_error=True)
    for name, args, result in [
        ('CreateCompatibleDC', [w.HDC], w.HDC),
        ('CreateDIBSection', [w.HDC, c.c_void_p, w.UINT, c.POINTER(c.c_void_p), w.HANDLE, w.DWORD], w.HBITMAP),
        ('SelectObject', [w.HDC, w.HGDIOBJ], w.HGDIOBJ),
        ('DeleteObject', [w.HGDIOBJ], w.BOOL), ('DeleteDC', [w.HDC], w.BOOL),
    ]:
        function = getattr(g, name); function.argtypes = args; function.restype = result
    rect = w.RECT(); require(u.GetClientRect(window, c.byref(rect)), 'Dialog bounds unavailable')
    width, height = rect.right, rect.bottom
    reference = u.GetDC(window); dc = g.CreateCompatibleDC(reference)
    header = struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
    info = c.create_string_buffer(header); memory = c.c_void_p()
    bitmap = g.CreateDIBSection(dc, info, 0, c.byref(memory), None, 0)
    require(bitmap and memory.value, 'Dialog bitmap allocation failed')
    old = g.SelectObject(dc, bitmap)
    try:
        require(u.PrintWindow(window, dc, 1), 'Owned dialog PrintWindow failed')
        g.GdiFlush()
        pixels = c.string_at(memory, width * height * 4)
        destination.write_bytes(struct.pack('<HIHHI', 0x4d42, 54 + len(pixels), 0, 0, 54) + header + pixels)
    finally:
        g.SelectObject(dc, old); g.DeleteObject(bitmap); g.DeleteDC(dc); u.ReleaseDC(window, reference)
    return {'file': str(destination), 'width': width, 'height': height, 'method': 'owned dialog PrintWindow'}


def check_dpapi(encrypted):
    # Recognize the standard Windows DPAPI provider header, allowing an app envelope.
    marker = bytes.fromhex('01000000d08c9ddf0115d1118c7a00c04fc297eb')
    offset = encrypted.find(marker, 0, 128)
    require(offset >= 0, 'Saved record does not contain a Windows DPAPI blob')
    for value in VALUES[2:]:
        require(value.encode() not in encrypted and value.encode('utf-16-le') not in encrypted,
                'Saved record contains a credential in plaintext')
    class Blob(c.Structure):
        _fields_ = [('size', w.DWORD), ('data', c.POINTER(c.c_ubyte))]
    crypt = c.WinDLL('crypt32', use_last_error=True)
    crypt.CryptUnprotectData.argtypes = [c.POINTER(Blob), c.c_void_p, c.c_void_p, c.c_void_p,
                                        c.c_void_p, w.DWORD, c.POINTER(Blob)]
    crypt.CryptUnprotectData.restype = w.BOOL
    data = (c.c_ubyte * (len(encrypted) - offset)).from_buffer_copy(encrypted[offset:])
    source, output = Blob(len(data), data), Blob()
    require(crypt.CryptUnprotectData(c.byref(source), None, None, None, None, 1, c.byref(output)),
            'Saved credential blob could not be independently decrypted with DPAPI')
    try:
        plain = c.string_at(output.data, output.size)
        require(VALUES[3].encode() in plain or VALUES[3].encode('utf-16-le') in plain,
                'DPAPI plaintext does not contain the synthetic credential')
    finally:
        c.memset(output.data, 0, output.size); K.LocalFree(output.data)


def actor(args):
    require(sys.stdin.buffer.readline(64) == PERMIT, 'Controller permit missing')
    job = K.OpenJobObjectW(4, False, args.job)
    member = w.BOOL()
    require(job and K.IsProcessInJob(K.GetCurrentProcess(), job, c.byref(member)) and member.value,
            'Actor is not in the owned Job')
    record = {'status': 'failed', 'scope': 'real client settings UI and DPAPI; no media capture or room join',
              'native_rtc_and_cross_network_turn': 'not tested', 'checks': [], 'captures': [],
              'job_name': args.job, 'actor_pid': os.getpid(), 'owned_job_membership_confirmed': True,
              'binary': str(args.binary), 'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest()}
    root = client = fixture = None
    started = time.monotonic()
    def passed(name): record['checks'].append(name)
    try:
        args.profile.mkdir(exist_ok=False)
        for name in ('Local', 'Roaming', 'Temp'): (args.profile / name).mkdir()
        env = {key.upper(): value for key, value in os.environ.items()
               if not key.upper().startswith(('LUMALIVE_', 'OPENAI_', 'ANTHROPIC_'))}
        env.update(LOCALAPPDATA=str(args.profile / 'Local'), APPDATA=str(args.profile / 'Roaming'),
                   TEMP=str(args.profile / 'Temp'), TMP=str(args.profile / 'Temp'),
                   LUMALIVE_AUTH_ENV='test', LUMALIVE_AUTH_TLS_CA_FILE='',
                   LUMALIVE_ACCOUNT_SETTINGS_PATH=str(args.profile / 'account.conf'),
                   LUMALIVE_UI_LANGUAGE='en-US')
        fixture = auth.WorkspaceAuthFixture(args.binary, env['LUMALIVE_ACCOUNT_SETTINGS_PATH'])
        startup = subprocess.STARTUPINFO(); startup.dwFlags = subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
        client = subprocess.Popen([str(args.binary)], cwd=args.binary.parent, env=env, startupinfo=startup,
                                  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        require(K.IsProcessInJob(w.HANDLE(int(client._handle)), job, c.byref(member)) and member.value,
                'Client did not inherit owned Job')
        require(K.IsProcessInJob(w.HANDLE(int(fixture.process._handle)), job, c.byref(member)) and member.value,
                'Authentication fixture did not inherit owned Job')
        record.update(client_pid=client.pid, fixture_pid=fixture.process.pid, client_job_membership_confirmed=True,
                      fixture_job_membership_confirmed=True)
        root = wait(lambda: next(iter(windows(client.pid, lambda h: u.GetDlgItem(h, 129) and
                    u.FindWindowExW(h, None, 'LumaAccountPage', None))), None), 'Owned client login window missing')
        settings = args.profile / 'Local/LumaLive/rtc-network.dat'
        def dialog(): return next(iter(windows(client.pid, lambda h: u.GetDlgItem(h, ENABLE) and u.GetDlgItem(h, STATUS))), None)
        def open_dialog():
            click(root, ENTRY)
            window = wait(dialog, 'Network settings dialog did not open')
            require(all(u.GetDlgItem(window, i) for i in range(ENABLE, STATUS + 1)), 'Settings control missing')
            # EnumWindows can see controls midway through WM_INITDIALOG,
            # while DPAPI loading has not populated every edit yet.
            wait(lambda: text(u.GetDlgItem(window, STATUS)) not in ('', 'You have unsaved changes.'),
                 'Network settings initialization did not finish')
            return window
        def cancel(window):
            click(window, 2); wait(lambda: not dialog(), 'Cancel did not close settings')
        def save(window):
            click(window, 1)
            wait(lambda: text(u.GetDlgItem(window, STATUS)).startswith('Saved.'),
                 'Valid save did not report success')
        def values(window): return tuple(text(u.GetDlgItem(window, i)) for i in (STUN, TURN, USER, PASSWORD))
        def set_values(window, fields=VALUES, enabled=True):
            u.SendMessageW(u.GetDlgItem(window, ENABLE), 0xF1, int(enabled), 0)
            u.SendMessageW(window, 0x111, ENABLE, u.GetDlgItem(window, ENABLE))
            for i, value in zip((STUN, TURN, USER, PASSWORD), fields): fill(u.GetDlgItem(window, i), value)
        def rejected(window, previous):
            before = text(u.GetDlgItem(window, STATUS)); click(window, 1)
            wait(lambda: not dialog() or text(u.GetDlgItem(window, STATUS)) != before,
                 'Save did not produce a readable validation error')
            require(dialog() == window, 'Invalid save closed the dialog')
            error = text(u.GetDlgItem(window, STATUS))
            require(len(error.strip()) >= 8 and VALUES[3] not in error, 'Validation error missing or exposes password')
            require(settings.read_bytes() == previous, 'Failed save changed the previous encrypted record')

        click(root, ENTRY); time.sleep(.2)
        require(not dialog() and not settings.exists(), 'Signed-out command exposed or persisted network settings')
        passed('signed-out direct command blocked')
        panel = fixture.login(root)
        click(root, 117); click(root, 128)
        # The owned main window intentionally starts hidden. Check the control's
        # own layout visibility, not the hidden parent's effective visibility.
        wait(lambda: u.GetWindowLongW(u.GetDlgItem(root, ENTRY), -16) & 0x10000000,
             'Network settings entrance is not exposed by the signed-in layout')
        window = open_dialog(); set_values(window); cancel(window)
        require(not settings.exists(), 'Cancel created settings file')
        passed('unsaved cancel leaves profile unchanged')
        window = open_dialog(); set_values(window)
        require(u.GetWindowLongW(u.GetDlgItem(window, PASSWORD), -16) & 0x20, 'Password field is not masked')
        record['captures'].append(capture(window, args.report.with_name(args.report.stem + '-dialog.bmp')))
        save(window); cancel(window)
        require(settings.is_file(), 'Valid save did not create settings file')
        encrypted = settings.read_bytes(); check_dpapi(encrypted)
        passed('valid save uses independently decrypted DPAPI and no plaintext credential')
        window = open_dialog()
        observed = values(window)
        record['reopen_observation'] = {'public_field_matches': [a == b for a, b in zip(observed[:3], VALUES[:3])],
                                      'enabled': int(u.SendMessageW(u.GetDlgItem(window, ENABLE), 0xF0, 0, 0))}
        require(observed[:3] == VALUES[:3] and u.SendMessageW(u.GetDlgItem(window, ENABLE), 0xF0, 0, 0) == 1,
                'Reopened saved settings differ')
        # Windows masks password edit contents from a different process. Prove
        # that the reopened password was loaded by saving without touching it,
        # then independently decrypt the new record and compare the fixture.
        save(window); encrypted = settings.read_bytes(); check_dpapi(encrypted)
        passed('reopened masked password survives unchanged save and DPAPI validation')
        set_values(window, (VALUES[0], VALUES[1], VALUES[2], 'cancelled-private-edit')); cancel(window)
        require(settings.read_bytes() == encrypted, 'Cancel modified existing settings')
        passed('saved values round-trip; cancelled edit preserves ciphertext')
        invalid = [
            ('bad STUN scheme', ('https://invalid.example', VALUES[1], VALUES[2], VALUES[3])),
            ('bad TURN URI', (VALUES[0], 'turn:invalid.example:0', VALUES[2], VALUES[3])),
            ('credentials without TURN', (VALUES[0], '', VALUES[2], VALUES[3])),
            ('partial TURN credentials', (VALUES[0], VALUES[1], VALUES[2], '')),
        ]
        for name, fields in invalid:
            window = open_dialog(); set_values(window, fields); rejected(window, encrypted); cancel(window); passed(name)
        window = open_dialog(); set_values(window, (VALUES[0], VALUES[1], VALUES[2], 'valid-unsaved-replacement'))
        locked = K.CreateFileW(str(settings), 0x80000000, 1, None, 3, 0x80, None)
        require(locked not in (None, c.c_void_p(-1).value), 'Could not lock private settings for write-failure test')
        try: rejected(window, encrypted)
        finally: K.CloseHandle(locked)
        cancel(window); passed('save failure retains previous encrypted settings and readable error')
        settings.write_bytes(b'corrupt private RTC settings fixture')
        corrupted = settings.read_bytes(); window = open_dialog()
        error = text(u.GetDlgItem(window, STATUS))
        require(len(error.strip()) >= 8 and values(window)[3] == '', 'Corrupt settings did not report error safely')
        record['captures'].append(capture(window, args.report.with_name(args.report.stem + '-corrupt.bmp')))
        cancel(window); require(settings.read_bytes() == corrupted, 'Opening/cancelling corrupt settings rewrote them')
        settings.write_bytes(encrypted); passed('corrupt load reports error; cancel preserves corrupted file')
        window = open_dialog(); set_values(window, enabled=False); save(window); cancel(window)
        window = open_dialog()
        require(u.SendMessageW(u.GetDlgItem(window, ENABLE), 0xF0, 0, 0) == 0, 'Disabled preference did not reload')
        cancel(window); passed('disabled custom preference persists')
        window = open_dialog(); click(root, 129); click(panel, 2100); click(panel, 2161)
        wait(lambda: not u.GetWindowLongW(u.GetDlgItem(root, 117), -16) & 0x10000000, 'Sign-out did not lock workspace')
        wait(lambda: not dialog(), 'Sign-out left network settings open')
        click(root, ENTRY); time.sleep(.2); require(not dialog(), 'Signed-out command reopened settings')
        passed('sign-out closes settings and blocks reopening')
        require(hashlib.sha256(args.binary.read_bytes()).hexdigest() == record['binary_sha256'], 'Binary changed during test')
        record['status'] = 'passed'
    except BaseException as error:
        # Assertion strings are static; never serialize control values or plaintext.
        record.update(status='failed', error_type=type(error).__name__, error=str(error))
    finally:
        try:
            if root: u.PostMessageW(root, 0x10, 0, 0)
            if client:
                client.wait(timeout=12); record['client_exit_code'] = client.returncode
                require(client.returncode == 0, 'Client exited nonzero')
            if fixture: fixture.close()
        except BaseException as error:
            record.update(status='failed', cleanup_error_type=type(error).__name__)
        K.CloseHandle(job)
        record['elapsed_seconds'] = round(time.monotonic() - started, 3)
        smoke.write_report(args.report, record)
    return 0 if record['status'] == 'passed' else 1


def main():
    global smoke, auth, K, u, wait, callback
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--repo-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--evidence-dir', required=True, type=Path)
    parser.add_argument('--actor', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--job', help=argparse.SUPPRESS)
    parser.add_argument('--profile', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--report', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    args.binary = args.binary.resolve(); args.repo_root = args.repo_root.resolve()
    args.evidence_dir = args.evidence_dir.resolve(); args.evidence_dir.mkdir(parents=True, exist_ok=True)
    require(args.binary.is_file() and (args.binary.parent / 'luma_account_ui_fixture.exe').is_file(),
            'Client and actual account fixture must be built in the same directory')
    sys.path.insert(0, str(args.repo_root / 'scripts/windows'))
    smoke = importlib.import_module('test_client_release_smoke'); auth = importlib.import_module('workspace_auth_fixture')
    K, u, wait, callback = smoke.K, auth.u, auth.wait, smoke.CALLBACK
    for name, arguments, result in [
        ('IsWindowVisible', [w.HWND], w.BOOL), ('GetClientRect', [w.HWND, c.POINTER(w.RECT)], w.BOOL),
        ('GetDC', [w.HWND], w.HDC), ('ReleaseDC', [w.HWND, w.HDC], c.c_int),
        ('PrintWindow', [w.HWND, w.HDC, w.UINT], w.BOOL),
    ]:
        function = getattr(u, name); function.argtypes = arguments; function.restype = result
    u.EnumWindows.argtypes = [callback, w.LPARAM]
    u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    K.LocalFree.argtypes = [c.c_void_p]; K.LocalFree.restype = c.c_void_p
    K.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    K.CreateFileW.restype = w.HANDLE
    if args.actor: return actor(args)
    token = uuid.uuid4().hex
    profile = args.evidence_dir / ('private-profile-' + token)
    report = args.evidence_dir / ('rtc-network-ui-' + token + '.json')
    job_name = 'Local\\LumaLive.RtcNetworkSettings.' + token
    job = process = None; empty = cleanup = False; controller_error = None; code = 1
    started = time.monotonic()
    try:
        with report.with_suffix('.log').open('x', encoding='utf-8') as log:
            process = subprocess.Popen([sys.executable, '-B', str(Path(__file__).resolve()), str(args.binary),
                '--repo-root', str(args.repo_root), '--evidence-dir', str(args.evidence_dir), '--actor',
                '--job', job_name, '--profile', str(profile), '--report', str(report)],
                stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            job = smoke.ownership.child_job(process, job_name)
            process.communicate(PERMIT, timeout=120); code = process.returncode
    except BaseException as error:
        controller_error = type(error).__name__
    finally:
        if job:
            try: smoke.ownership.terminate_startup(job, process, timeout=8); empty = True
            except BaseException as error: controller_error = type(error).__name__
            finally: K.CloseHandle(job)
        if process is not None and process.stdin: process.stdin.close()
    result = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {'status': 'failed'}
    try:
        require(profile.resolve().parent == args.evidence_dir and profile.name.startswith('private-profile-'),
                'Private profile cleanup escaped the evidence directory')
        if profile.exists(): shutil.rmtree(profile)
        cleanup = not profile.exists()
    except BaseException as error: controller_error = type(error).__name__
    result.update(owned_job_empty=empty, private_profile_removed=cleanup, controller_error_type=controller_error,
                  controller_elapsed_seconds=round(time.monotonic() - started, 3))
    if code != 0 or not empty or not cleanup or controller_error: result['status'] = 'failed'
    final = report.with_name(report.stem + '-final.json'); result['report_path'] = str(final)
    smoke.write_report(final, result); print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
