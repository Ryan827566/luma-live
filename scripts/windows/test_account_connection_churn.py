"""Bounded TLS connection churn on an owned, in-memory Windows fixture.

The retained mode characterizes the old binary; bounded mode is the regression
gate. Neither mode connects to or restarts the deployed account service.
"""
import argparse
import ctypes
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import hmac
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import uuid

import test_account_reliability as base

ROOT, ownership, tls = base.ROOT, base.ownership, base.tls
require, save = base.require, base.save
PERMIT = b'LumaLive isolated connection churn\n'
BATCHES, PER_BATCH, WARMUP = 5, 128, 32
kernel = ctypes.WinDLL('kernel32', use_last_error=True)


class Memory(ctypes.Structure):
    _fields_ = [('cb', w.DWORD), ('PageFaultCount', w.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            'PeakWorkingSetSize', 'WorkingSetSize', 'QuotaPeakPagedPoolUsage',
            'QuotaPagedPoolUsage', 'QuotaPeakNonPagedPoolUsage',
            'QuotaNonPagedPoolUsage', 'PagefileUsage', 'PeakPagefileUsage', 'PrivateUsage')]


class ThreadEntry(ctypes.Structure):
    _fields_ = [(name, w.DWORD) for name in (
        'dwSize', 'cntUsage', 'th32ThreadID', 'th32OwnerProcessID')] + [
        ('tpBasePri', w.LONG), ('tpDeltaPri', w.LONG), ('dwFlags', w.DWORD)]


kernel.GetProcessHandleCount.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
kernel.GetProcessHandleCount.restype = w.BOOL
kernel.K32GetProcessMemoryInfo.argtypes = [w.HANDLE, ctypes.POINTER(Memory), w.DWORD]
kernel.K32GetProcessMemoryInfo.restype = w.BOOL
kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
kernel.Thread32First.argtypes = [w.HANDLE, ctypes.POINTER(ThreadEntry)]
kernel.Thread32First.restype = w.BOOL
kernel.Thread32Next.argtypes = kernel.Thread32First.argtypes
kernel.Thread32Next.restype = w.BOOL
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.CloseHandle.restype = w.BOOL


def sample(process):
    require(process.poll() is None, 'native_exited_before_measurement')
    handles, memory = w.DWORD(), Memory()
    memory.cb = ctypes.sizeof(memory)
    require(kernel.GetProcessHandleCount(int(process._handle), ctypes.byref(handles)),
            'handle_count_failed')
    require(kernel.K32GetProcessMemoryInfo(int(process._handle), ctypes.byref(memory), memory.cb),
            'memory_count_failed')
    snapshot = kernel.CreateToolhelp32Snapshot(4, 0)  # TH32CS_SNAPTHREAD, read only.
    require(snapshot != ctypes.c_void_p(-1).value, 'thread_snapshot_failed')
    threads, entry = 0, ThreadEntry()
    entry.dwSize = ctypes.sizeof(entry)
    try:
        available = kernel.Thread32First(snapshot, ctypes.byref(entry))
        while available:
            if entry.th32OwnerProcessID == process.pid:
                threads += 1
            entry.dwSize = ctypes.sizeof(entry)
            available = kernel.Thread32Next(snapshot, ctypes.byref(entry))
        require(ctypes.get_last_error() == 18 and threads >= 2, 'thread_enumeration_failed')
    finally:
        kernel.CloseHandle(snapshot)
    return {'handles': handles.value, 'live_threads': threads,
            'private_bytes': memory.PrivateUsage, 'working_set_bytes': memory.WorkingSetSize}


def settled(process):
    # No probe connections during settling: idle reaping must work on its own.
    time.sleep(.5)
    readings = []
    for _ in range(3):
        readings.append(sample(process))
        time.sleep(.1)
    require(len({(r['handles'], r['live_threads']) for r in readings}) == 1,
            'resource_counts_not_settled')
    return readings[-1]


def workload(process, endpoint, context, connections, result, expected):
    control = base.Client(endpoint, context, connections)
    base.expect(control.exchange(13), 14)
    def ping():
        client = base.Client(endpoint, context, connections)
        try:
            base.expect(client.exchange(13), 14)
        finally:
            client.close()

    for _ in range(WARMUP):
        ping()
    result['baseline'] = baseline = settled(process)
    result['batches'] = []
    for index in range(BATCHES):
        for _ in range(PER_BATCH):
            ping()
        reading = settled(process)
        reading.update(completed_connections=(index + 1) * PER_BATCH,
                       handle_delta=reading['handles'] - baseline['handles'])
        result['batches'].append(reading)
        require(reading['live_threads'] == baseline['live_threads'], 'client_threads_still_running')
        base.expect(control.exchange(13), 14)
    # The main acceptance signal is retained handles; memory is observational.
    deltas = [r['handle_delta'] for r in result['batches']]
    result['resource_bound_satisfied'] = all(abs(d) <= 8 for d in deltas)
    result['retained_handles_reproduced'] = all(
        abs(d - (i + 1) * PER_BATCH) <= 8 for i, d in enumerate(deltas))
    require(result['resource_bound_satisfied'] if expected == 'bounded'
            else result['retained_handles_reproduced'], 'unexpected_handle_behavior')
    ping()
    result['final_ping_passed'] = True
    # Keep both a completed TLS peer and an unfinished handshake alive for Stop.
    # Stop must join active workers after periodic reaping has removed old ones.
    raw = connections.add(socket.create_connection(('127.0.0.1', endpoint), timeout=3))
    time.sleep(.2)
    result['stop_with_open_peers'] = sample(process)['live_threads'] >= baseline['live_threads'] + 1
    require(result['stop_with_open_peers'], 'open_stop_peers_not_accepted')
    result['persistent_control_ping_passed'] = True
    return control.peer, raw


def checked_directory(directory):
    directory = Path(directory).resolve()
    require(directory.parent == (ROOT / 'output/services').resolve()
            and directory.name.startswith('account-churn-tests-'), 'fixture_directory_outside_scope')
    return directory


def stop_during_churn(process, endpoint, context, result):
    """Exercise Stop while accept/completion/reaping are still overlapping."""
    stopping = threading.Event()
    connections = base.Connections()
    ready = [threading.Event(), threading.Event()]
    cycles, closed, errors = [0, 0], [False, False], []

    def worker(index):
        try:
            until = time.monotonic() + 8
            while time.monotonic() < until:
                client = None
                try:
                    client = base.Client(endpoint, context, connections)
                    base.expect(client.exchange(13), 14)
                    cycles[index] += 1
                    if cycles[index] >= 4:
                        ready[index].set()
                except (OSError, AssertionError) as error:
                    if not stopping.is_set() or (isinstance(error, AssertionError)
                                                  and str(error) != 'response_closed_early'):
                        raise
                    closed[index] = True
                    return
                finally:
                    if client is not None:
                        client.close()
            raise AssertionError('stop_race_peer_deadline')
        except BaseException as error:
            errors.append(type(error).__name__)
            ready[index].set()

    threads = [threading.Thread(target=worker, args=(index,), daemon=True) for index in range(2)]
    try:
        for thread in threads:
            thread.start()
        until = time.monotonic() + 5
        for event in ready:
            require(event.wait(max(0, until - time.monotonic())), 'stop_race_not_ready')
        require(not errors and all(n >= 4 for n in cycles), 'stop_race_failed_before_stop')
        stopping.set()
        started = time.monotonic()
        process.communicate(b'\n', timeout=5)
        result['stop_seconds'] = round(time.monotonic() - started, 3)
        require(process.returncode == 0 and result['stop_seconds'] < 2, 'native_shutdown_failed')
        until = time.monotonic() + 5
        for thread in threads:
            thread.join(max(0, until - time.monotonic()))
        require(not errors and all(closed) and not any(t.is_alive() for t in threads),
                'stop_race_failed')
        result.update(stop_during_churn_passed=True, stop_race_cycles=cycles)
    finally:
        stopping.set()
        connections.close_all()
        until = time.monotonic() + 5
        for thread in threads:
            if thread.ident is not None:
                thread.join(max(0, until - time.monotonic()))
        require(not any(t.is_alive() for t in threads), 'stop_race_cleanup_incomplete')


def exercise(args):
    directory = checked_directory(args.directory)
    require(args.job.startswith('Local\\LumaLive.ChurnTest.')
            and sys.stdin.buffer.readline(64) == PERMIT, 'fixture_launch_not_permitted')
    job = ownership.kernel.OpenJobObjectW(4, False, args.job)
    member = w.BOOL()
    try:
        require(job and ownership.kernel.IsProcessInJob(
            ownership.kernel.GetCurrentProcess(), job, ctypes.byref(member)) and member.value,
            'fixture_not_in_owned_job')
    finally:
        if job:
            ownership.kernel.CloseHandle(job)
    result = {'passed': False, 'native_shutdown_complete': False}
    connections, process, phase = base.Connections(), None, 'certificate_setup'
    try:
        base.material(directory)
        context = ssl.create_default_context(cafile=str(directory / 'ca.pem'))
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            endpoint = reservation.getsockname()[1]
        require(endpoint != 9100, 'fixture_port_collision')
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith('LUMALIVE_')}
        env.update(LUMALIVE_AUTH_ENV='test', LUMALIVE_AUTH_TLS_CA_FILE='',
                   LUMALIVE_AUTH_BIND_ADDRESS='127.0.0.1',
                   LUMALIVE_AUTH_TLS_LIBRARY_DIR=str(tls.pg.RUNTIME / 'bin'),
                   LUMALIVE_AUTH_TLS_CERT_FILE=str(directory / 'server.pem'),
                   LUMALIVE_AUTH_TLS_KEY_FILE=str(directory / 'server.key'))
        phase = 'native_startup'
        with (directory / 'native.log').open('wb') as log:
            try:
                process = subprocess.Popen([str(args.fixture),
                                           'server' if args.scenario == 'churn' else 'reuse', str(endpoint)], cwd=ROOT,
                                           env=env, stdin=subprocess.PIPE, stdout=log, stderr=log,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                if args.scenario != 'churn':
                    phase = args.scenario
                    reuse(process, endpoint, context, connections, result, directory / 'native.log',
                          session_restart=args.scenario == 'session-restart')
                else:
                    churn(process, endpoint, context, connections, result, directory / 'native.log', args.expect)
                result['native_shutdown_complete'] = True
            finally:
                connections.close_all()
                if process is not None:
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=5)
                    process.stdin.close()
        require('STOPPED\n' in (directory / 'native.log').read_text(), 'stop_ack_missing')
        vacant(endpoint)
        result.update(passed=True, port_vacant=True)
    except BaseException as error:
        result.update(failed_phase=phase, error_type=type(error).__name__)
    save(directory / 'result.json', result)
    return 0 if result['passed'] else 1


def vacant(endpoint):
    with socket.socket() as vacancy:
        vacancy.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        vacancy.bind(('127.0.0.1', endpoint))


def wait_marker(process, log, marker):
    until = time.monotonic() + 10
    while marker + '\n' not in log.read_text():
        require(process.poll() is None and time.monotonic() < until, 'native_marker_timeout')
        time.sleep(.02)


def closed_peers(peers):
    for peer in peers:
        peer.settimeout(1)
        try:
            require(peer.recv(1) == b'', 'open_peer_not_closed_by_stop')
        except (ConnectionResetError, ConnectionAbortedError, ssl.SSLEOFError):
            pass


def remember_credentials(fields, seen, *, refreshed=False):
    require(all(fields[:3]), 'empty_session_credentials')
    for category, value in zip(('access', 'refresh', 'session')[:2 if refreshed else 3], fields):
        digest = (category, hashlib.sha256(value.encode('ascii')).hexdigest())
        require(digest not in seen, 'duplicate_session_credentials')
        seen.add(digest)


def live_login(client, user, seen):
    challenge = base.expect(client.exchange(5, user['email'], user['device'], user['display']), 6, 4)
    require(challenge[2] == '0' and len(challenge[1]) == 64, 'unexpected_login_challenge')
    proof = hmac.new(base.verifier(user['password'], challenge),
                     bytes.fromhex(challenge[1]), 'sha256').hexdigest()
    fields = base.expect(client.exchange(7, proof, ''), 8, 11)
    require(fields[3:9] == [user['device'], user['display'], user['id'],
                            user['username'], user['email'], user['display']], 'login_identity_mismatch')
    remember_credentials(fields, seen)
    validate_live(client, fields)
    return fields


def validate_live(client, fields):
    require(min(int(fields[9]), int(fields[10])) > time.time() + 60, 'session_near_expiry')
    require(base.expect(client.exchange(11, fields[0]), 8, 11) == [fields[0], '', *fields[2:]],
            'validated_identity_mismatch')
    require(base.expect(client.exchange(15, fields[0]), 16, 6)[:4] == fields[5:9],
            'profile_identity_mismatch')


def refresh_live(client, fields, seen):
    refreshed = base.expect(client.exchange(54, fields[0], fields[1]), 55, 11)
    require(refreshed[2:9] == fields[2:9], 'refresh_identity_mismatch')
    remember_credentials(refreshed, seen, refreshed=True)
    validate_live(client, refreshed)
    return refreshed


def assert_sessions(client, current, expected):
    fields = base.expect(client.exchange(39, current[0]), 40)
    require(fields and fields[0] == str(len(expected)) and len(fields) == 1 + 7 * len(expected),
            'session_list_count_mismatch')
    rows = [fields[i:i + 7] for i in range(1, len(fields), 7)]
    require({row[0] for row in rows} == {session[2] for session in expected},
            'session_list_identity_mismatch')
    require(all(row[6] == ('1' if row[0] == current[2] else '0') for row in rows),
            'session_list_current_mismatch')


def reuse(process, endpoint, context, connections, result, log, *, session_restart=False):
    user, seen, lock = None, set(), threading.Lock()
    old_sessions = []  # Sensitive fields stay in memory and never enter the report.
    ping_connections = 0 if session_restart else 64
    result['reuse_cycles'] = []
    for cycle in range(3):
        if cycle:
            process.stdin.write(b'start\n')
            process.stdin.flush()
        wait_marker(process, log, f'READY {cycle} {endpoint}')
        client = base.Client(endpoint, context, connections)
        if user is None:
            user = base.register(client, 0)
        if session_restart:
            # These negative wire checks alone only establish connection binding.
            # The session-list comparison below independently checks server state.
            for previous in old_sessions:
                require(min(int(previous[9]), int(previous[10])) > time.time() + 60,
                        'old_session_expired_before_restart_check')
                for kind, values in ((11, previous[:1]), (54, previous[:2])):
                    rejected = base.expect(client.exchange(kind, *values), 12, 2)
                    require(rejected[0] == 'invalid_session', 'old_credentials_accepted_after_restart')
            current = live_login(client, user, seen)
            assert_sessions(client, current, [current])
            other = base.Client(endpoint, context, connections)
            detached = live_login(other, {**user, 'device': 'isolated-disconnected-peer'}, seen)
            detached = refresh_live(other, detached, seen)
            before_disconnect = settled(process)
            other.close()  # No Logout; the session must remain in server memory.
            after_disconnect = settled(process)
            require(after_disconnect['live_threads'] < before_disconnect['live_threads'],
                    'disconnected_worker_not_reaped')
            assert_sessions(client, current, [current, detached])
            current = refresh_live(client, current, seen)
            assert_sessions(client, current, [current, detached])
            old_count = len(old_sessions)
            old_sessions.extend((current, detached))
        else:
            # Same stored account, fresh and unique credentials after every restart.
            base.authenticated_cycle(client, user, seen, lock)
        for _ in range(ping_connections):
            peer = base.Client(endpoint, context, connections)
            try:
                base.expect(peer.exchange(13), 14)
            finally:
                peer.close()
        running = settled(process)
        base.expect(client.exchange(13), 14)
        raw = connections.add(socket.create_connection(('127.0.0.1', endpoint), timeout=3))
        time.sleep(.1)
        require(sample(process)['live_threads'] >= running['live_threads'] + 1,
                'reuse_handshake_not_accepted')
        if session_restart:
            validate_live(client, current)  # Still valid immediately before Stop, without Logout.
        started = time.monotonic()
        process.stdin.write(b'stop\n')
        process.stdin.flush()
        wait_marker(process, log, f'STOPPED {cycle}')
        elapsed = time.monotonic() - started
        require(elapsed < 2, 'reuse_stop_too_slow')
        closed_peers((client.peer, raw))
        connections.close_all()
        vacant(endpoint)
        stopped = settled(process)
        require(stopped['live_threads'] <= running['live_threads'] - 2,
                'reuse_threads_survived_stop')
        if cycle:
            baseline = result['reuse_cycles'][0]['stopped']
            require(abs(stopped['handles'] - baseline['handles']) <= 8
                    and stopped['live_threads'] <= baseline['live_threads'], 'reuse_resources_accumulated')
        detail = {'cycle': cycle + 1, 'ping_connections': ping_connections,
            'fresh_authentication_passed': True, 'port_vacant': True,
            'stop_seconds': round(elapsed, 3), 'running': running, 'stopped': stopped}
        if session_restart:
            detail.update(live_sessions_before_stop=2, disconnected_session_retained=True,
                          refresh_before_stop_passed=True, no_logout_sent=True,
                          prior_sessions_absent_after_restart=old_count,
                          old_access_rejections=old_count, old_refresh_rejections=old_count,
                          current_session_only_after_login=True)
        result['reuse_cycles'].append(detail)
    process.communicate(b'quit\n', timeout=5)
    require(process.returncode == 0, 'reuse_fixture_exit_failed')
    result['same_instance_reuse_passed'] = True
    if session_restart:
        result.update(active_session_restart_passed=True, restart_boundaries_verified=2,
                      credential_rejection_scope='connection binding; session lists verify old records cleared')


def churn(process, endpoint, context, connections, result, log, expected):
    until = time.monotonic() + 10
    while f'READY {endpoint}\n' not in log.read_text():
        require(process.poll() is None and time.monotonic() < until, 'native_not_ready')
        time.sleep(.05)
    peers = workload(process, endpoint, context, connections, result, expected)
    if expected == 'bounded':
        stop_during_churn(process, endpoint, context, result)
    else:
        started = time.monotonic()
        process.communicate(b'\n', timeout=5)
        result['stop_seconds'] = round(time.monotonic() - started, 3)
        require(process.returncode == 0 and result['stop_seconds'] < 2, 'native_shutdown_failed')
    for peer in peers:
        peer.settimeout(1)
        try:
            require(peer.recv(1) == b'', 'open_peer_not_closed_by_stop')
        except (ConnectionResetError, ConnectionAbortedError, ssl.SSLEOFError):
            pass


def main(args):
    started = time.monotonic()
    report = {'passed': False, 'status': 'running', 'fixture_cleanup_complete': False,
              'started_at': datetime.now(timezone.utc).isoformat(), 'expected_behavior': args.expect,
              'warmup_connections': WARMUP, 'batches_planned': BATCHES, 'connections_per_batch': PER_BATCH,
              'watchdog_seconds': 120, 'production_service_restarted': False,
              'scope': 'bounded isolated TLS Ping churn; memory observational, not a production soak'}
    report['scenario'] = args.scenario
    if args.scenario != 'churn':
        for field in ('warmup_connections', 'batches_planned', 'connections_per_batch'):
            report.pop(field)
        report.update(scope='three Start/Stop cycles on one isolated TLS service instance',
                      reuse_cycles_planned=3, ping_connections_per_cycle=64)
        if args.scenario == 'session-restart':
            report.update(scope='two restart boundaries with live and disconnected sessions on one TLS service instance',
                          ping_connections_per_cycle=0)
    save(args.report, report)
    directory, process, job, empty = None, None, None, False
    try:
        report['fixture_sha256'] = hashlib.sha256(args.fixture.read_bytes()).hexdigest()
        report['fixture_path'] = str(args.fixture)
        directory = checked_directory(tempfile.mkdtemp(prefix='account-churn-tests-', dir=ROOT / 'output/services'))
        report['fixture_directory'] = str(directory)
        sid = tls.pg.current_sid()
        tls.pg.run(['icacls', directory, '/inheritance:r', '/grant:r',
                    f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])
        job_name = 'Local\\LumaLive.ChurnTest.' + uuid.uuid4().hex
        with (directory / 'actor.log').open('wb') as log:
            try:
                process = subprocess.Popen([sys.executable, '-B', str(Path(__file__).resolve()),
                    '--exercise', '--directory', str(directory), '--job', job_name,
                    '--fixture', str(args.fixture), '--expect', args.expect, '--scenario', args.scenario], cwd=ROOT,
                    stdin=subprocess.PIPE, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
                job = ownership.child_job(process, job_name)
                try:
                    process.communicate(PERMIT, timeout=120)
                except subprocess.TimeoutExpired:
                    report['watchdog_expired'] = True
                finally:
                    ownership.terminate_startup(job, process, timeout=10)
                    empty = True
                if (directory / 'result.json').is_file():
                    report.update(json.loads((directory / 'result.json').read_text()))
                report['passed'] = bool(report['passed'] and process.returncode == 0
                                        and not report.get('watchdog_expired'))
            finally:
                if job:
                    ownership.kernel.CloseHandle(job)
                if process is not None:
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=5)
                    process.stdin.close()
    except BaseException as error:
        report.update(passed=False, controller_error_type=type(error).__name__)
    finally:
        report['owned_job_empty'] = empty
        try:
            if directory is not None and (empty or process is None):
                shutil.rmtree(checked_directory(directory))
                report['fixture_cleanup_complete'] = not directory.exists()
        except OSError as error:
            report.update(passed=False, cleanup_error_type=type(error).__name__)
        report['passed'] = bool(report['passed'] and empty and report['fixture_cleanup_complete'])
        report.update(status='passed' if report['passed'] else 'failed',
                      elapsed_seconds=round(time.monotonic() - started, 3),
                      finished_at=datetime.now(timezone.utc).isoformat())
        save(args.report, report)
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scenario', choices=('churn', 'reuse', 'session-restart'), default='churn')
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--expect', choices=('bounded', 'retained'), default='bounded')
    parser.add_argument('--exercise', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--directory', help=argparse.SUPPRESS)
    parser.add_argument('--job', help=argparse.SUPPRESS)
    args = parser.parse_args()
    require(args.scenario == 'churn' or args.expect == 'bounded', 'reuse_requires_bounded_mode')
    if args.fixture is None:
        fixture_group = 'churn' if args.scenario == 'churn' else 'reuse'
        args.fixture = ROOT / f'output/auth-{fixture_group}-20261021/Release/luma_auth_tls_fixture.exe'
    if args.report is None:
        args.report = ROOT / {'churn': 'output/account-connection-churn.json',
                             'reuse': 'output/account-instance-reuse.json',
                             'session-restart': 'output/account-session-restart.json'}[args.scenario]
    args.fixture = args.fixture.resolve()
    require(args.fixture.is_relative_to(ROOT / 'output')
            and args.fixture.name == 'luma_auth_tls_fixture.exe', 'not_an_isolated_fixture')
    args.report = args.report.resolve()
    require(args.report.parent == ROOT / 'output' and args.report.suffix == '.json', 'report_outside_output')
    raise SystemExit(exercise(args) if args.exercise else main(args))
