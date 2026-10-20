"""Bounded concurrent authentication through TLS while other peers stall.

Uses the existing in-memory native fixture, never the deployed account service.
The controller owns the entire exercise subtree in a Windows Job before issuing
its launch permit. A 120-second actor watchdog covers fixture setup and exercise;
controller preparation and bounded Job termination/cleanup run separately.
"""
import ctypes
from datetime import datetime, timezone
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import uuid

import manage_account_certificates as tls
import manage_account_service as ownership

ROOT = ownership.ROOT
FIXTURE = ROOT / 'output/auth-tls-20261017/Release/luma_auth_tls_fixture.exe'
REPORT = ROOT / 'output/account-reliability-tests.json'
WORKERS, WAVES, WAVE_SECONDS = 4, 3, 10
PERMIT = b'LumaLive isolated reliability exercise\n'


def require(value, code):
    if not value:
        raise AssertionError(code)


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def checked_directory(directory):
    directory = Path(directory).resolve()
    require(directory.parent == (ROOT / 'output/services').resolve()
            and directory.name.startswith('account-reliability-tests-'),
            'fixture_directory_outside_scope')
    return directory


class Connections:
    """Track sockets so every worker can be interrupted before bounded joins."""
    def __init__(self):
        self.lock = threading.Lock()
        self.items = set()

    def add(self, peer):
        with self.lock:
            self.items.add(peer)
        return peer

    def close(self, peer):
        with self.lock:
            self.items.discard(peer)
        try:
            peer.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        peer.close()

    def close_all(self):
        with self.lock:
            peers = list(self.items)
        for peer in peers:
            self.close(peer)


class Client:
    def __init__(self, endpoint, context, connections):
        self.connections = connections
        raw = connections.add(socket.create_connection(('127.0.0.1', endpoint), timeout=3))
        try:
            self.peer = context.wrap_socket(raw, server_hostname='127.0.0.1',
                                            do_handshake_on_connect=False)
            connections.add(self.peer)
            connections.close(raw)  # wrap_socket transferred the descriptor.
            self.peer.do_handshake()
        except BaseException:
            connections.close(raw)
            if hasattr(self, 'peer'):
                connections.close(self.peer)
            raise

    def close(self):
        self.connections.close(self.peer)

    def exchange(self, kind, *fields):
        require(all(not any(c in field for c in '|\r\n\t') for field in fields),
                'invalid_fixture_field')
        until = time.monotonic() + 3
        self.peer.settimeout(3)
        self.peer.sendall((str(kind) + '|' + '|'.join(fields) + '\n').encode('ascii'))
        data = bytearray()
        while not data.endswith(b'\n'):
            remaining = until - time.monotonic()
            require(remaining > 0 and len(data) < 16384, 'response_deadline_or_size')
            self.peer.settimeout(remaining)
            part = self.peer.recv(16384 - len(data))
            require(part, 'response_closed_early')
            data.extend(part)
        require(data.count(b'\n') == 1, 'unexpected_extra_frame')
        parts = data.decode('ascii').rstrip('\r\n').split('|')
        return int(parts[0]), parts[1:]


def expect(packet, kind, count=None):
    require(packet[0] == kind and (count is None or len(packet[1]) == count),
            'unexpected_protocol_response')
    return packet[1]


def verifier(password, challenge):
    require(len(challenge[0]) == 32 and int(challenge[-1]) == 600000,
            'unexpected_password_challenge')
    return hashlib.pbkdf2_hmac('sha256', password.encode('ascii'),
                              bytes.fromhex(challenge[0]), int(challenge[-1]))


def register(client, index):
    user = {'username': 'reliability.' + uuid.uuid4().hex[:16],
            'password': secrets.token_hex(16), 'display': f'Reliability fixture {index}',
            'device': f'isolated-worker-{index}'}
    user['email'] = user['username'] + '@example.invalid'
    challenge = expect(client.exchange(1, user['username'], user['email'], user['display']), 2, 3)
    fields = expect(client.exchange(3, verifier(user['password'], challenge).hex()), 4, 4)
    require(fields[1:] == [user['username'], user['email'], user['display']],
            'registration_identity_mismatch')
    user['id'] = fields[0]
    return user


def authenticated_cycle(client, user, seen, seen_lock):
    challenge = expect(client.exchange(5, user['email'], user['device'], user['display']), 6, 4)
    require(challenge[2] == '0' and len(challenge[1]) == 64, 'unexpected_login_challenge')
    proof = hmac.new(verifier(user['password'], challenge),
                     bytes.fromhex(challenge[1]), 'sha256').hexdigest()
    fields = expect(client.exchange(7, proof, ''), 8, 11)
    require(fields[3:9] == [user['device'], user['display'], user['id'],
                            user['username'], user['email'], user['display']],
            'login_identity_mismatch')
    require(all(fields[:3]), 'empty_session_credentials')
    # Compare hashes only; credentials and user data never enter the report.
    with seen_lock:
        for category, value in zip(('access', 'refresh', 'session'), fields[:3]):
            digest = (category, hashlib.sha256(value.encode('ascii')).hexdigest())
            require(digest not in seen, 'duplicate_session_credentials')
            seen.add(digest)
    token = fields[0]
    validated = expect(client.exchange(11, token), 8, 11)
    require(validated == [fields[0], '', *fields[2:]], 'validated_identity_mismatch')
    profile = expect(client.exchange(15, token), 16, 6)
    require(profile[:4] == [user['id'], user['username'], user['email'], user['display']],
            'profile_identity_mismatch')
    expect(client.exchange(9, token), 10)
    rejected = expect(client.exchange(11, token), 12, 2)
    require(rejected[0] == 'invalid_session', 'logged_out_session_accepted')


def wave(endpoint, context, users, connections, seen, seen_lock):
    peers, intervals, cycles, errors, threads = [], [None] * 4, [[] for _ in users], [], []
    stop = threading.Event()
    gate = threading.Barrier(len(users) + 1, timeout=5)
    until = None

    def observe(index, peer, opened):
        try:
            peer.settimeout(8)
            try:
                require(peer.recv(1) == b'', 'stalled_peer_received_application_data')
            except (ConnectionResetError, ConnectionAbortedError, ssl.SSLEOFError):
                pass
            closed = time.monotonic()
            require(4.3 <= closed - opened < 8, 'stalled_peer_deadline')
            intervals[index] = (opened, closed)
        except BaseException as error:
            errors.append(type(error).__name__)
            stop.set()
        finally:
            connections.close(peer)

    def worker(index, user):
        try:
            gate.wait()
            while not stop.is_set() and time.monotonic() < until:
                started = time.monotonic()
                client = Client(endpoint, context, connections)
                try:
                    authenticated_cycle(client, user, seen, seen_lock)
                finally:
                    client.close()
                cycles[index].append((started, time.monotonic()))
                stop.wait(.15)
        except BaseException as error:
            errors.append(type(error).__name__)
            stop.set()

    try:
        for index in range(4):
            if index < 2:
                opened = time.monotonic()
                peer = connections.add(socket.create_connection(('127.0.0.1', endpoint), timeout=3))
            else:
                client = Client(endpoint, context, connections)
                peer = client.peer
                opened = time.monotonic()
                peer.sendall(b'1')  # Start a frame without its terminating newline.
            peers.append(peer)
            thread = threading.Thread(target=observe, args=(index, peer, opened), daemon=True)
            threads.append(thread)
            thread.start()
        started = time.monotonic()
        until = started + WAVE_SECONDS
        for index, user in enumerate(users):
            thread = threading.Thread(target=worker, args=(index, user), daemon=True)
            threads.append(thread)
            thread.start()
        gate.wait()
        for thread in threads:
            thread.join(max(0, until + 5 - time.monotonic()))
        require(not any(thread.is_alive() for thread in threads), 'wave_threads_not_finished')
        require(not errors, 'concurrent_peer_or_worker_failure')
        require(all(intervals), 'missing_stalled_peer_result')
        all_open = max(item[0] for item in intervals)
        first_close = min(item[1] for item in intervals)
        all_closed = max(item[1] for item in intervals)
        overlap = [sum(a >= all_open and b < first_close for a, b in items) for items in cycles]
        after = [sum(a > all_closed for a, _ in items) for items in cycles]
        require(all(n >= 1 for n in overlap), 'normal_auth_did_not_finish_while_all_peers_stalled')
        require(all(n >= 1 for n in after), 'normal_auth_did_not_continue_after_peer_timeouts')
        require(all(len(items) >= 3 for items in cycles), 'insufficient_completed_cycles')
        return {'elapsed_seconds': round(time.monotonic() - started, 3),
                'cycles_per_worker': [len(items) for items in cycles],
                'cycles_while_all_four_peers_stalled': overlap,
                'cycles_started_after_all_peer_timeouts': after,
                'stalled_peer_seconds': [round(b - a, 3) for a, b in intervals]}
    finally:
        stop.set()
        gate.abort()
        connections.close_all()
        deadline = time.monotonic() + 5
        for thread in threads:
            thread.join(max(0, deadline - time.monotonic()))
        require(not any(thread.is_alive() for thread in threads), 'worker_cleanup_incomplete')


def material(directory):
    tls.run_ssl(['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
                 '-subj', '/CN=Isolated reliability CA',
                 '-addext', 'basicConstraints=critical,CA:TRUE',
                 '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
                 '-keyout', directory / 'ca.key', '-out', directory / 'ca.pem'])
    tls.run_ssl(['req', '-new', '-newkey', 'rsa:2048', '-nodes',
                 '-subj', '/CN=Isolated reliability leaf',
                 '-keyout', directory / 'server.key', '-out', directory / 'server.csr'])
    extension = directory / 'server.ext'
    extension.write_text('basicConstraints=critical,CA:FALSE\n'
                         'keyUsage=critical,digitalSignature,keyEncipherment\n'
                         'extendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.1\n', encoding='ascii')
    tls.run_ssl(['x509', '-req', '-in', directory / 'server.csr', '-CA', directory / 'ca.pem',
                 '-CAkey', directory / 'ca.key', '-set_serial', '0x' + secrets.token_hex(16),
                 '-days', '1', '-sha256', '-extfile', extension, '-out', directory / 'server.pem'])


def exercise(directory, job_name):
    directory = checked_directory(directory)
    require(job_name.startswith('Local\\LumaLive.ReliabilityTest.')
            and sys.stdin.buffer.readline(64) == PERMIT, 'fixture_launch_not_permitted')
    job = ownership.kernel.OpenJobObjectW(4, False, job_name)
    member = ownership.w.BOOL()
    try:
        require(job and ownership.kernel.IsProcessInJob(
            ownership.kernel.GetCurrentProcess(), job, ctypes.byref(member)) and member.value,
            'fixture_not_in_owned_job')
    finally:
        if job:
            ownership.kernel.CloseHandle(job)
    result = {'passed': False, 'waves': [], 'native_shutdown_complete': False}
    phase, process = 'certificate_setup', None
    connections = Connections()
    try:
        material(directory)
        context = ssl.create_default_context(cafile=str(directory / 'ca.pem'))
        with socket.socket() as reserved:
            reserved.bind(('127.0.0.1', 0))
            endpoint = reserved.getsockname()[1]
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
                process = subprocess.Popen([str(FIXTURE), 'server', str(endpoint)], cwd=ROOT,
                                           env=env, stdin=subprocess.PIPE, stdout=log, stderr=log,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                deadline = time.monotonic() + 10
                while f'READY {endpoint}\n' not in (directory / 'native.log').read_text(encoding='utf-8'):
                    require(process.poll() is None and time.monotonic() < deadline, 'native_not_ready')
                    time.sleep(.05)
                phase = 'registration'
                users = []
                for index in range(WORKERS):
                    client = Client(endpoint, context, connections)
                    try:
                        users.append(register(client, index))
                    finally:
                        client.close()
                seen, seen_lock = set(), threading.Lock()
                for index in range(WAVES):
                    phase = f'concurrent_wave_{index + 1}'
                    result['waves'].append(wave(endpoint, context, users, connections, seen, seen_lock))
                    save(directory / 'result.json', result)
                phase = 'final_health'
                client = Client(endpoint, context, connections)
                try:
                    expect(client.exchange(13), 14)
                    authenticated_cycle(client, users[0], seen, seen_lock)
                finally:
                    client.close()
                result['final_authenticated_cycle_passed'] = True
                phase = 'native_shutdown'
                process.communicate(b'\n', timeout=5)
                require(process.returncode == 0, 'native_shutdown_failed')
                result['native_shutdown_complete'] = True
            finally:
                connections.close_all()
                if process is not None:
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=5)
                    process.stdin.close()
        require('STOPPED\n' in (directory / 'native.log').read_text(encoding='utf-8'),
                'native_stop_acknowledgement_missing')
        with socket.socket() as vacancy:
            vacancy.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            vacancy.bind(('127.0.0.1', endpoint))
        result.update(passed=True, port_vacant=True)
    except BaseException as error:
        # Do not dump exceptions/frames: they can contain credentials.
        result.update(failed_phase=phase, error_type=type(error).__name__)
    save(directory / 'result.json', result)
    return 0 if result['passed'] else 1


def main():
    started = time.monotonic()
    report = {'passed': False, 'status': 'running', 'fixture_cleanup_complete': False,
              'started_at': datetime.now(timezone.utc).isoformat(),
              'scope': '30-second isolated concurrent TLS authentication; not a production soak',
              'normal_workers': WORKERS, 'stalled_peers_per_wave': 4,
              'planned_waves': WAVES, 'wave_seconds': WAVE_SECONDS,
              'watchdog_seconds': 120, 'production_service_restarted': False}
    save(REPORT, report)
    directory, process, job, empty = None, None, None, False
    try:
        report['fixture_sha256'] = hashlib.sha256(FIXTURE.read_bytes()).hexdigest()
        directory = checked_directory(tempfile.mkdtemp(prefix='account-reliability-tests-',
                                                      dir=ROOT / 'output/services'))
        report['fixture_directory'] = str(directory)
        sid = tls.pg.current_sid()
        tls.pg.run(['icacls', directory, '/inheritance:r', '/grant:r',
                    f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])
        job_name = 'Local\\LumaLive.ReliabilityTest.' + uuid.uuid4().hex
        with (directory / 'actor.log').open('wb') as log:
            try:
                process = subprocess.Popen([sys.executable, '-B', str(Path(__file__).resolve()),
                                            '--exercise', str(directory), job_name], cwd=ROOT,
                                           stdin=subprocess.PIPE, stdout=log, stderr=log,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                job = ownership.child_job(process, job_name)
                try:
                    process.communicate(PERMIT, timeout=120)
                except subprocess.TimeoutExpired:
                    report['watchdog_expired'] = True
                finally:
                    ownership.terminate_startup(job, process, timeout=10)
                    empty = True
                result_path = directory / 'result.json'
                if result_path.is_file():
                    report.update(json.loads(result_path.read_text(encoding='utf-8')))
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
        save(REPORT, report)
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == '--exercise':
        raise SystemExit(exercise(sys.argv[2], sys.argv[3]))
    require(len(sys.argv) == 1, 'unexpected_arguments')
    raise SystemExit(main())
