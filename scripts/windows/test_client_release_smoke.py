"""Bounded real-release launcher smoke; does not log in or touch user services.

The parent agent runs this explicitly. The child receives a permit only after
being put in a kill-on-close Job. The launcher and GUI inherit that Job.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
WORK = None
sys.path.insert(0, str(ROOT / 'scripts/windows'))
import manage_account_service as ownership

PERMIT = b'LumaLive isolated release smoke 1\n'
K = ownership.kernel
U = c.WinDLL('user32', use_last_error=True)
CALLBACK = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
U.EnumWindows.argtypes = [CALLBACK, w.LPARAM]
U.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
U.GetDlgItem.argtypes = [w.HWND, c.c_int]
U.GetDlgItem.restype = w.HWND
U.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]
U.FindWindowExW.restype = w.HWND
U.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
for name, args, result in [
    ('OpenProcess', [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
    ('QueryFullProcessImageNameW', [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)], w.BOOL),
    ('GetExitCodeProcess', [w.HANDLE, c.POINTER(w.DWORD)], w.BOOL),
]:
    function = getattr(K, name)
    function.argtypes = args
    function.restype = result


def write_report(path, record):
    with path.open('x', encoding='utf-8') as output:
        output.write(json.dumps(record, ensure_ascii=False, indent=2) + '\n')


def actor(args):
    if sys.stdin.buffer.readline(64) != PERMIT:
        raise RuntimeError('Controller permit missing')
    job = K.OpenJobObjectW(4, False, args.job)
    member = w.BOOL()
    if not job or not K.IsProcessInJob(K.GetCurrentProcess(), job, c.byref(member)) or not member.value:
        raise RuntimeError('Actor is not in the owned Job')
    record = {'status': 'failed', 'scope': 'actual release launcher and Win32 client process; no authentication, media capture or room join',
              'job_name': args.job, 'actor_pid': os.getpid(), 'owned_job_membership_confirmed': True,
              'binary': str(args.package / 'luma_studio.exe'), 'launcher': str(args.package / 'Start-LumaLive.cmd'),
              'profile': str(args.profile)}
    handle = None
    started = time.monotonic()
    try:
        binary = args.package / 'luma_studio.exe'
        launcher = args.package / 'Start-LumaLive.cmd'
        if not binary.is_file() or not launcher.is_file():
            raise AssertionError('Release binary or launcher is missing')
        record['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        record['launcher_sha256'] = hashlib.sha256(launcher.read_bytes()).hexdigest()
        args.profile.mkdir(exist_ok=False)
        for name in ['Local', 'Roaming', 'Temp']:
            (args.profile / name).mkdir()
        env = {key.upper(): value for key, value in os.environ.items()
               if not key.upper().startswith(('LUMALIVE_', 'OPENAI_', 'ANTHROPIC_'))}
        env.update(LOCALAPPDATA=str(args.profile / 'Local'), APPDATA=str(args.profile / 'Roaming'),
                   TEMP=str(args.profile / 'Temp'), TMP=str(args.profile / 'Temp'),
                   LUMALIVE_AUTH_ENV='test', LUMALIVE_AUTH_TLS_CA_FILE='',
                   LUMALIVE_ACCOUNT_SETTINGS_PATH=str(args.profile / 'account.conf'),
                   LUMALIVE_UI_LANGUAGE='en-US', LUMALIVE_START_NONINTERACTIVE='1')
        with socket.socket() as reserved:
            reserved.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            reserved.bind(('127.0.0.1', 0))
            port = reserved.getsockname()[1]
            (args.profile / 'account.conf').write_text(
                f'LumaLiveAccountConnection 1\nhost=127.0.0.1\nport={port}\n', encoding='utf-8')
            record['private_endpoint'] = {'host': '127.0.0.1', 'port': port, 'listening': False, 'exclusive_bind': True}
            launcher_process = subprocess.Popen([env.get('COMSPEC', r'C:\Windows\System32\cmd.exe'),
                '/d', '/c', 'Start-LumaLive.cmd'], cwd=args.package, env=env,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW)
            record['launcher_pid'] = launcher_process.pid
            found = []
            @CALLBACK
            def find(window, _):
                pid = w.DWORD()
                U.GetWindowThreadProcessId(window, c.byref(pid))
                process_handle = K.OpenProcess(0x100000 | 0x1000, False, pid.value)
                if not process_handle:
                    return True
                transferred = False
                try:
                    inside = w.BOOL()
                    if not K.IsProcessInJob(process_handle, job, c.byref(inside)) or not inside.value:
                        return True
                    path_buffer = c.create_unicode_buffer(32768)
                    size = w.DWORD(len(path_buffer))
                    if not K.QueryFullProcessImageNameW(process_handle, 0, path_buffer, c.byref(size)):
                        return True
                    if Path(path_buffer.value).resolve() != binary.resolve():
                        return True
                    if U.GetDlgItem(window, 129) and U.GetDlgItem(window, 118) and U.FindWindowExW(window, None, 'LumaAccountPage', None):
                        found.append((int(window), int(pid.value), process_handle, path_buffer.value))
                        transferred = True
                        return False
                    return True
                finally:
                    if not transferred:
                        K.CloseHandle(process_handle)
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline and not found:
                U.EnumWindows(find, 0)
                if launcher_process.poll() is not None:
                    break
                time.sleep(.05)
            if not found:
                raise AssertionError('Owned release client login window was not ready')
            window, pid, handle, image_path = found[0]
            record.update(client_pid=pid, client_image_path=image_path, window_handle=window,
                          login_window_loaded=True, client_job_membership_confirmed=True)
            # Final actor evidence is written once after cleanup.
            if not U.PostMessageW(window, 0x10, 0, 0):
                raise AssertionError('WM_CLOSE could not be posted')
            record['wm_close_posted'] = True
            if K.WaitForSingleObject(handle, 8000) != 0:
                raise AssertionError('Client did not exit normally after WM_CLOSE')
            exit_code = w.DWORD()
            if not K.GetExitCodeProcess(handle, c.byref(exit_code)):
                raise AssertionError('Client exit code unavailable')
            record['client_exit_code'] = exit_code.value
            launcher_process.communicate(timeout=5)
            record['launcher_exit_code'] = launcher_process.returncode
            if exit_code.value != 0 or launcher_process.returncode != 0:
                raise AssertionError('Client or launcher exited nonzero')
            record['status'] = 'passed'
    except BaseException as error:
        record.update(status='failed', error_type=type(error).__name__, error=str(error))
    finally:
        if handle:
            K.CloseHandle(handle)
        K.CloseHandle(job)
        record['elapsed_seconds'] = round(time.monotonic() - started, 3)
        write_report(args.report, record)
    return 0 if record['status'] == 'passed' else 1


def main():
    global WORK
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--evidence-dir', required=True, type=Path)
    parser.add_argument('--actor', action='store_true')
    parser.add_argument('--job')
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    args.package = args.package.resolve()
    WORK = args.evidence_dir.resolve()
    WORK.mkdir(parents=True, exist_ok=True)
    if args.actor:
        return actor(args)
    token = uuid.uuid4().hex
    profile = WORK / ('private-profile-' + token)
    report = WORK / ('real-release-smoke-' + token + '.json')
    log_path = report.with_suffix('.log')
    job_name = 'Local\\LumaLive.ReleaseSmoke.' + token
    job = process = None
    empty = False
    controller_error = None
    code = 1
    started = time.monotonic()
    try:
        with log_path.open('x', encoding='utf-8') as log:
            process = subprocess.Popen([sys.executable, '-B', str(Path(__file__).resolve()),
                str(args.package), '--evidence-dir', str(WORK), '--actor', '--job', job_name, '--profile', str(profile),
                '--report', str(report)], cwd=WORK, stdin=subprocess.PIPE, stdout=log,
                stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            job = ownership.child_job(process, job_name)
            process.communicate(PERMIT, timeout=32)
            code = process.returncode
    except BaseException as error:
        controller_error = type(error).__name__
    finally:
        if job:
            try:
                ownership.terminate_startup(job, process, timeout=8)
                empty = True
            except BaseException as error:
                controller_error = type(error).__name__
            finally:
                K.CloseHandle(job)
        if process is not None and process.stdin:
            process.stdin.close()
    record = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {'status': 'failed'}
    cleanup = False
    try:
        if profile.resolve().parent != WORK.resolve() or not profile.name.startswith('private-profile-'):
            raise AssertionError('Private-profile cleanup path escaped QA workspace')
        if profile.exists():
            shutil.rmtree(profile)
        cleanup = not profile.exists()
    except BaseException as error:
        controller_error = type(error).__name__
    record.update(owned_job_empty=empty, private_profile_removed=cleanup, controller_error_type=controller_error,
                  controller_elapsed_seconds=round(time.monotonic() - started, 3), report_path=str(report))
    if code != 0 or not empty or not cleanup or controller_error:
        record['status'] = 'failed'
    report = report.with_name(report.stem + '-final.json')
    record['report_path'] = str(report)
    write_report(report, record)
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
