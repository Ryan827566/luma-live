"""Bounded observational experiment, NOT an account stability acceptance test.

Three independent owned Jobs and TLS in-memory fixtures compare no clients,
warmed idle, and the existing paced workload. No production service is touched.
--smoke validates launch/measurement/cleanup only, not the 300-second behavior.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import threading
import time
import uuid

PERMIT = b'LumaLive thread lifetime observation\n'
ARMS = ('zero-clients', 'warm-idle', 'loaded')
OBSERVE_SECONDS = 420
WATCHDOG_SECONDS = 510


def save(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_project(root):
    sys.path.insert(0, str(root / 'scripts/windows'))
    global churn, base, owner, diag
    import test_account_connection_churn as churn
    import test_account_reliability as base
    import manage_account_service as owner
    import account_thread_diagnostics as diag
    base.require(base.ROOT.resolve() == root, 'project_import_mismatch')
    owner.kernel.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
    owner.kernel.GetProcessTimes.restype = w.BOOL


def times(handle, *, process=False):
    values = [w.FILETIME() for _ in range(4)]
    function = owner.kernel.GetProcessTimes if process else diag.kernel.GetThreadTimes
    base.require(function(handle, *(c.byref(value) for value in values)), 'time_query_failed')
    return [diag.filetime(value) for value in values]


class Lifetimes:
    """Retain only QUERY_INFORMATION handles to observed ntdll entry threads.

    These handles live in the observer, not the measured process. Holding them
    preserves thread-object identity after exit; it does not keep threads alive.
    """
    def __init__(self, pid, process_created):
        self.pid, self.process_created = pid, process_created
        self.held = {}
        self.rows = []

    def observe(self, reading, age):
        for row in reading.get('thread_origins', {}).get('threads', []):
            if row.get('start_module') != 'ntdll.dll' or not row.get('created_filetime'):
                continue
            key = (row['thread_id'], row['created_filetime'])
            if key in self.held:
                continue
            evidence = {k: row[k] for k in ('thread_id', 'created_filetime', 'start_module', 'start_rva')}
            evidence.update(first_seen_process_seconds=round(age, 3),
                            created_process_seconds=(key[1] - self.process_created) / 10_000_000)
            self.rows.append(evidence)
            handle = diag.kernel.OpenThread(0x40, False, key[0])
            if not handle:
                evidence['retain_error'] = c.get_last_error()
                continue
            try:
                base.require(diag.kernel.GetProcessIdOfThread(handle) == self.pid, 'thread_owner_changed')
                base.require(times(handle)[0] == key[1], 'thread_identity_changed')
                self.held[key] = (handle, evidence)
                handle = None
            except Exception as error:
                evidence['retain_error_type'] = type(error).__name__
            finally:
                if handle:
                    diag.kernel.CloseHandle(handle)

    def refresh(self, age, stage):
        for handle, row in self.held.values():
            if row.get('exit_filetime'):
                continue
            try:
                created, exited, kernel_time, user_time = times(handle)
                base.require(created == row['created_filetime'], 'retained_thread_identity_changed')
                row.update(kernel_100ns=kernel_time, user_100ns=user_time)
                if exited:
                    row.update(exit_filetime=exited, exit_observed_stage=stage,
                               lifetime_seconds=(exited - created) / 10_000_000,
                               exited_process_seconds=(exited - self.process_created) / 10_000_000)
                else:
                    row['last_alive_process_seconds'] = round(age, 3)
            except Exception as error:
                row['time_error_type'] = type(error).__name__

    def close(self):
        closed = True
        for handle, _ in self.held.values():
            closed = bool(diag.kernel.CloseHandle(handle)) and closed
        self.held.clear()
        return closed


def prepare_control(port, context, connections):
    control = base.Client(port, context, connections)
    users = [base.register(control, index) for index in range(2)]
    seen, lock = set(), threading.Lock()
    for user in users:
        base.authenticated_cycle(control, user, seen, lock)
    audit(control, users, seen)
    for _ in range(churn.WARMUP):
        peer = base.Client(port, context, connections)
        try:
            base.expect(peer.exchange(13), 14)
        finally:
            peer.close()
    return control, users, seen, lock


def audit(control, users, seen):
    for user in users:
        current = churn.live_login(control, user, seen)
        churn.assert_sessions(control, current, [current])
        base.expect(control.exchange(9, current[0]), 10)
        rejected = base.expect(control.exchange(11, current[0]), 12, 2)
        base.require(rejected[0] == 'invalid_session', 'observation_audit_logout_failed')


def actor(args):
    directory = args.output_directory
    base.require(args.arm in ARMS and directory.name == args.arm, 'invalid_actor_directory')
    base.require(args.job.startswith('Local\\LumaLive.ThreadObservation.'), 'invalid_job_name')
    base.require(sys.stdin.buffer.readline(64) == PERMIT, 'launch_not_permitted')
    job = owner.kernel.OpenJobObjectW(4, False, args.job)
    member = w.BOOL()
    base.require(job and owner.kernel.IsProcessInJob(owner.kernel.GetCurrentProcess(), job,
                                                   c.byref(member)) and member.value, 'actor_not_owned')
    result = dict(arm=args.arm, diagnostic_completed=False, stability_acceptance=False,
                  observations=[], intervals=[], workload_errors=[], normal_stop=False,
                  fixture_sha256=sha256(args.fixture), job=args.job,
                  observation_seconds=8 if args.smoke else OBSERVE_SECONDS, smoke=args.smoke)
    process = lifetimes = worker = None
    connections = base.Connections()
    detail_lock = threading.Lock()
    private = directory / 'private'
    log = None
    started = None
    try:
        private.mkdir()
        sid = base.tls.pg.current_sid()
        base.tls.pg.run(['icacls', private, '/inheritance:r', '/grant:r',
                         f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])
        base.material(private)
        context = ssl.create_default_context(cafile=str(private / 'ca.pem'))
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        base.require(port != 9100, 'reserved_service_port')
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith('LUMALIVE_')}
        env.update(LUMALIVE_AUTH_ENV='test', LUMALIVE_AUTH_TLS_CA_FILE='',
                   LUMALIVE_AUTH_BIND_ADDRESS='127.0.0.1',
                   LUMALIVE_AUTH_TLS_LIBRARY_DIR=str(base.tls.pg.RUNTIME / 'bin'),
                   LUMALIVE_AUTH_TLS_CERT_FILE=str(private / 'server.pem'),
                   LUMALIVE_AUTH_TLS_KEY_FILE=str(private / 'server.key'))
        native_command = [str(args.fixture), 'server', str(port)]
        log = (directory / 'native.log').open('wb')
        started = time.monotonic()
        process = subprocess.Popen(native_command, cwd=private, env=env, stdin=subprocess.PIPE,
                                   stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
        base.require(owner.kernel.IsProcessInJob(int(process._handle), job, c.byref(member)) and member.value,
                     'native_not_in_owned_job')
        process.thread_diagnostics = True
        process_created = times(int(process._handle), process=True)[0]
        result.update(native_command=native_command, native_pid=process.pid, port=port,
                      process_created_filetime=process_created)
        lifetimes = Lifetimes(process.pid, process_created)
        churn.wait_marker(process, directory / 'native.log', f'READY {port}')
        control = users = seen = seen_lock = None
        if args.arm != 'zero-clients':
            control, users, seen, seen_lock = prepare_control(port, context, connections)
        result['warmup_complete_process_seconds'] = round(time.monotonic() - started, 3)
        result['warmup_client_connections'] = 0 if args.arm == 'zero-clients' else churn.WARMUP + 1
        quiet_until = time.monotonic() + (0.5 if args.smoke else churn.SOAK_QUIET_WARMUP_SECONDS)
        baseline = None

        def load():
            try:
                for index in range(churn.SOAK_INTERVALS):
                    detail = dict(interval=index + 1, planned_active_seconds=churn.SOAK_SECONDS)
                    churn.soak_interval(port, context, users, seen, seen_lock, detail)
                    reading = churn.settled(process)
                    detail['silent_resources'] = reading
                    detail['handle_bound_satisfied'] = abs(reading['handles'] - baseline['handles']) <= 8
                    detail['thread_bound_satisfied'] = reading['live_threads'] == baseline['live_threads']
                    # Observe all predeclared intervals even if the ORIGINAL
                    # gate would fail. This does not turn this run into a pass.
                    audit(control, users, seen)
                    base.expect(control.exchange(13), 14)
                    detail['audit_completed'] = True
                    with detail_lock:
                        result['intervals'].append(detail)
            except Exception as error:
                with detail_lock:
                    result['workload_errors'].append(type(error).__name__)

        next_sample = time.monotonic()
        until = started + result['observation_seconds']
        while time.monotonic() < until:
            now = time.monotonic()
            if baseline is None and now >= quiet_until:
                baseline = churn.settled(process)
                result['baseline'] = baseline
                result['baseline_process_seconds'] = round(time.monotonic() - started, 3)
                if args.arm == 'loaded' and not args.smoke:
                    worker = threading.Thread(target=load, daemon=True)
                    worker.start()
            if now >= next_sample:
                age = now - started
                reading = churn.sample(process)
                reading['process_elapsed_seconds'] = round(age, 3)
                result['observations'].append(reading)
                lifetimes.observe(reading, age)
                lifetimes.refresh(age, 'before_stop')
                result['ntdll_lifetimes'] = lifetimes.rows
                with detail_lock:
                    save(directory / 'result.json', result)
                next_sample = now + (0.5 if args.smoke or 285 <= age <= 335 else 5)
            time.sleep(.05)
        if worker:
            worker.join(timeout=1)
            base.require(not worker.is_alive(), 'workload_not_finished_in_observation_window')
        result['observation_completed'] = True
        lifetimes.refresh(time.monotonic() - started, 'before_stop')
        result['stop_requested_process_seconds'] = round(time.monotonic() - started, 3)
        result['stop_requested_filetime'] = time.time_ns() // 100 + 116444736000000000
        stopping = time.monotonic()
        process.communicate(b'\n', timeout=5)
        result['stop_seconds'] = round(time.monotonic() - stopping, 3)
        result['normal_stop'] = process.returncode == 0 and result['stop_seconds'] < 2
        lifetimes.refresh(time.monotonic() - started, 'after_stop')
        connections.close_all()
        churn.vacant(port)
        result['port_vacant'] = True
        base.require(result['normal_stop'], 'normal_stop_failed')
        base.require(not result['workload_errors'], 'workload_failed')
        if args.arm == 'loaded' and not args.smoke:
            base.require(len(result['intervals']) == churn.SOAK_INTERVALS, 'incomplete_load')
        result['diagnostic_completed'] = True
    except Exception as error:
        result['error_type'] = type(error).__name__
    finally:
        connections.close_all()
        if process:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=5)
            process.stdin.close()
        if worker:
            worker.join(timeout=5)
            result['workload_thread_joined'] = not worker.is_alive()
        if lifetimes:
            result['observer_thread_handles_closed'] = lifetimes.close()
            if not result['observer_thread_handles_closed']:
                result['diagnostic_completed'] = False
        if log:
            log.close()
        owner.kernel.CloseHandle(job)
        result['elapsed_process_seconds'] = round(time.monotonic() - started, 3) if started else None
        save(directory / 'result.json', result)
    return 0 if result['diagnostic_completed'] else 1


def controller(args):
    directory = args.output_directory
    directory.mkdir(parents=True, exist_ok=False)
    report = dict(started_at=datetime.now(timezone.utc).isoformat(), diagnostic_completed=False,
                  stability_acceptance=False, smoke=args.smoke, command=[sys.executable, *sys.argv],
                  fixture=str(args.fixture), fixture_sha256=sha256(args.fixture), arms={},
                  observation_seconds=8 if args.smoke else OBSERVE_SECONDS,
                  watchdog_seconds=90 if args.smoke else WATCHDOG_SECONDS,
                  meanings={'zero-clients': 'TLS context and listener initialized; no TCP client ever opened',
                            'warm-idle': 'Original auth/list/logout and 32 Ping warmup; existing TLS control only',
                            'loaded': 'Same warmup, then original 10 x 30 second workload; observations are not acceptance'},
                  source_sha256={name: sha256(args.project_root / 'scripts/windows' / name) for name in
                                 ('test_account_connection_churn.py', 'account_thread_diagnostics.py',
                                  'test_account_reliability.py', 'manage_account_service.py')})
    report['observer_sha256'] = sha256(Path(__file__))
    active = []
    save(directory / 'experiment.json', report)
    deadline = time.monotonic() + report['watchdog_seconds']
    try:
        for arm in ARMS:
            arm_directory = directory / arm
            arm_directory.mkdir()
            job_name = 'Local\\LumaLive.ThreadObservation.' + uuid.uuid4().hex
            command = [sys.executable, '-B', str(Path(__file__).resolve()), '--actor', '--arm', arm,
                       '--job', job_name, '--project-root', str(args.project_root),
                       '--fixture', str(args.fixture), '--output-directory', str(arm_directory)]
            if args.smoke:
                command.append('--smoke')
            log = (arm_directory / 'actor.log').open('wb')
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=log,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            job = None
            active.append((arm, process, log, job))
            job = owner.child_job(process, job_name)
            active[-1] = (arm, process, log, job)
            report['arms'][arm] = dict(actor_command=command, job=job_name, actor_pid=process.pid)
            process.stdin.write(PERMIT)
            process.stdin.flush()
        while any(process.poll() is None for _, process, _, _ in active):
            if time.monotonic() >= deadline:
                report['watchdog_expired'] = True
                break
            time.sleep(.2)
    except Exception as error:
        report['controller_error_type'] = type(error).__name__
    finally:
        for arm, process, log, job in active:
            record = report['arms'].setdefault(arm, {})
            record['actor_exit_before_cleanup'] = process.poll()
            try:
                if job:
                    owner.terminate_startup(job, process, timeout=10)
                    record['owned_job_empty'] = True
                elif process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
            except Exception as error:
                record['cleanup_error_type'] = type(error).__name__
            finally:
                if job:
                    owner.kernel.CloseHandle(job)
                process.stdin.close()
                log.close()
            try:
                result_path = directory / arm / 'result.json'
                if result_path.exists():
                    record['result'] = json.loads(result_path.read_text(encoding='utf-8'))
                # Delete only this experiment's private material after confirmed
                # Job emptiness. Leave JSON/native logs intact, no recursion.
                private = (directory / arm / 'private').resolve()
                if record.get('owned_job_empty') and private.exists():
                    base.require(private.parent == (directory / arm).resolve() and not private.is_symlink(),
                                 'private_directory_outside_scope')
                    for child in private.iterdir():
                        base.require(child.is_file() and not child.is_symlink(), 'unexpected_private_entry')
                        child.unlink()
                    private.rmdir()
                record['private_material_removed'] = not private.exists()
            except Exception as error:
                # A damaged result/private directory must not skip cleanup of
                # the remaining independently owned Jobs.
                record['evidence_cleanup_error_type'] = type(error).__name__
        report['finished_at'] = datetime.now(timezone.utc).isoformat()
        report['diagnostic_completed'] = (len(report['arms']) == 3 and not report.get('watchdog_expired')
            and all(r.get('actor_exit_before_cleanup') == 0 and r.get('owned_job_empty')
                    and r.get('private_material_removed') and r.get('result', {}).get('diagnostic_completed')
                    and r.get('result', {}).get('observer_thread_handles_closed')
                    for r in report['arms'].values()))
        save(directory / 'experiment.json', report)
    print(json.dumps({k: report[k] for k in ('diagnostic_completed', 'stability_acceptance', 'smoke')}, indent=2))
    return 0 if report['diagnostic_completed'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output-directory', type=Path, required=True)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--actor', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--arm', choices=ARMS, help=argparse.SUPPRESS)
    parser.add_argument('--job', help=argparse.SUPPRESS)
    args = parser.parse_args()
    args.project_root = args.project_root.resolve()
    args.fixture = args.fixture.resolve()
    args.output_directory = args.output_directory.resolve()
    load_project(args.project_root)
    base.require(args.fixture.is_relative_to(args.project_root / 'output')
                 and args.fixture.name == 'luma_auth_tls_fixture.exe', 'not_an_isolated_fixture')
    raise SystemExit(actor(args) if args.actor else controller(args))
