"""Exercise the actual native TLS client/server with isolated keys and memory storage."""
import argparse
import contextlib
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def run(command, env=None, timeout=15, expected=0):
    result = subprocess.run([str(part) for part in command], env=env, capture_output=True,
                            text=True, timeout=timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    require(result.returncode == expected,
            f'{Path(str(command[0])).name} exited {result.returncode}, expected {expected}: {result.stdout} {result.stderr}')
    return result.stdout


def certificates(openssl, directory):
    for name in ('ca', 'untrusted'):
        run([openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
             '-subj', f'/CN=Luma TLS test {name}', '-addext', 'basicConstraints=critical,CA:TRUE',
             '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
             '-keyout', directory / f'{name}.key', '-out', directory / f'{name}.pem'])
    for name, ip in (('server', '127.0.0.1'), ('wrong-ip', '127.0.0.2')):
        run([openssl, 'req', '-new', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=TLS fixture',
             '-keyout', directory / f'{name}.key', '-out', directory / f'{name}.csr'])
        extension = directory / f'{name}.ext'
        extension.write_text(f'basicConstraints=critical,CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\n'
                             f'extendedKeyUsage=serverAuth\nsubjectAltName=IP:{ip}\n', encoding='ascii')
        run([openssl, 'x509', '-req', '-in', directory / f'{name}.csr', '-CA', directory / 'ca.pem',
             '-CAkey', directory / 'ca.key', '-CAcreateserial', '-days', '1', '-extfile', extension,
             '-out', directory / f'{name}.pem'])


@contextlib.contextmanager
def native_server(binary, env):
    endpoint = port()
    process = subprocess.Popen([str(binary), 'server', str(endpoint)], env=env, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    ready = []
    waiter = threading.Thread(target=lambda: ready.append(process.stdout.readline()), daemon=True)
    waiter.start()
    try:
        waiter.join(10)
        require(not waiter.is_alive() and ready == [f'READY {endpoint}\n'], 'TLS fixture server did not become ready')
        yield endpoint
        stdout, stderr = process.communicate('stop\n', timeout=4)
        require(process.returncode == 0 and 'STOPPED' in stdout, f'TLS fixture shutdown failed: {stdout} {stderr}')
    finally:
        if process.poll() is None:
            process.kill()
        process.wait(timeout=5)


def tls_socket(endpoint, ca):
    context = ssl.create_default_context(cafile=str(ca))
    raw = socket.create_connection(('127.0.0.1', endpoint), timeout=8)
    return context.wrap_socket(raw, server_hostname='127.0.0.1')


def closed(connection):
    try:
        require(connection.recv(1024) == b'', 'rejected transport returned application data')
    except (ConnectionResetError, ConnectionAbortedError, ssl.SSLEOFError):
        pass


def timed(action, name):
    started = time.monotonic()
    action()
    elapsed = time.monotonic() - started
    require(4.5 <= elapsed < 8, f'{name} took {elapsed:.2f}s instead of the fixed 5s deadline')


def stub_server(binary, env, directory, tls=False, stall=False):
    """A peer records any downgrade attempt and can drip a TLS application frame."""
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(9)
        endpoint = listener.getsockname()[1]
        errors = []
        count = []
        def serve():
            try:
                with listener.accept()[0] as raw:
                    count.append(1)
                    raw.settimeout(8)
                    if tls:
                        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                        context.load_cert_chain(directory / 'server.pem', directory / 'server.key')
                        with context.wrap_socket(raw, server_side=True) as peer:
                            line = b''
                            while not line.endswith(b'\n'):
                                line += peer.recv(1)
                            # The frame never completes; each new byte must not reset the deadline.
                            for byte in b'30|fixture-response':
                                try:
                                    peer.send(bytes([byte]))
                                except (OSError, ssl.SSLError):
                                    break
                                time.sleep(.8)
                    else:
                        hello = b''
                        while len(hello) < 5:
                            chunk = raw.recv(5 - len(hello))
                            require(bool(chunk), 'TLS client closed before its record header')
                            hello += chunk
                        require(hello[0] == 22 and hello[1] == 3,
                                'configured TLS client sent plaintext')
                        remaining = int.from_bytes(hello[3:5], 'big')
                        while remaining:
                            chunk = raw.recv(remaining)
                            require(bool(chunk), 'TLS client closed before its handshake record')
                            remaining -= len(chunk)
                        if stall:
                            time.sleep(5.5)
                        else:
                            raw.sendall(b'14|\n')
                            closed(raw)
                listener.settimeout(.3)
                try:
                    extra, _ = listener.accept()
                    extra.close()
                    raise AssertionError('TLS failure retried a plaintext connection')
                except socket.timeout:
                    pass
            except BaseException as error:
                errors.append(error)
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        command = [binary, 'client', endpoint, 'probe']
        if stall or tls:
            timed(lambda: run(command, env, expected=3 if tls else 2), 'client frame' if tls else 'client handshake')
        else:
            output = run(command, env, expected=2)
            require('tls_failed' in output, 'plaintext server was not rejected by TLS handshake')
        thread.join(10)
        require(not thread.is_alive(), 'TLS stub did not finish')
        if errors:
            raise errors[0]
        require(count == [1], 'TLS stub accepted an unexpected connection count')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--openssl', type=Path, required=True)
    parser.add_argument('--library-dir', type=Path, required=True)
    args = parser.parse_args()
    binary = args.fixture.resolve()
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('LUMALIVE_AUTH_', 'LUMALIVE_EMAIL_', 'LUMALIVE_SMS_'))}
    env.update(LUMALIVE_AUTH_ENV='production', LUMALIVE_AUTH_BIND_ADDRESS='127.0.0.1',
               LUMALIVE_AUTH_TLS_LIBRARY_DIR=str(args.library_dir.resolve()))
    with tempfile.TemporaryDirectory(prefix='luma-auth-tls-') as temporary:
        directory = Path(temporary)
        certificates(args.openssl, directory)
        run([binary, 'guard', port()], env)
        require('tls_configuration' in run([binary, 'client', port()], env, expected=2), 'missing CA did not fail closed')
        env['LUMALIVE_AUTH_TLS_CA_FILE'] = str(directory / 'ca.pem')
        server_env = dict(env, LUMALIVE_AUTH_TLS_CERT_FILE=str(directory / 'server.pem'),
                          LUMALIVE_AUTH_TLS_KEY_FILE=str(directory / 'server.key'))
        with native_server(binary, server_env) as endpoint:
            run([binary, 'client', endpoint, 'probe'], env)
            for ca in ('untrusted.pem', 'missing.pem'):
                bad_env = dict(env, LUMALIVE_AUTH_TLS_CA_FILE=str(directory / ca))
                require('tls_' in run([binary, 'client', endpoint, 'probe'], bad_env, expected=2), 'invalid CA accepted')
            adjacent = directory / 'adjacent'
            adjacent.mkdir()
            copied = adjacent / binary.name
            shutil.copy2(binary, copied)
            shutil.copy2(directory / 'ca.pem', adjacent / 'lumalive-auth-ca.pem')
            automatic = {key: value for key, value in env.items() if key != 'LUMALIVE_AUTH_TLS_CA_FILE'}
            run([copied, 'client', endpoint, 'probe'], automatic)
            override = dict(automatic, LUMALIVE_AUTH_TLS_CA_FILE=str(directory / 'missing.pem'))
            require('tls_configuration' in run([copied, 'client', endpoint, 'probe'], override, expected=2), 'adjacent CA overrode explicit setting')
            fixture_env = dict(automatic, LUMALIVE_AUTH_ENV='test')
            with native_server(binary, fixture_env) as plain_endpoint:
                run([copied, 'client', plain_endpoint, 'probe'], fixture_env)
            with socket.create_connection(('127.0.0.1', endpoint), timeout=8) as plain:
                plain.sendall(b'13|\n\n')
                closed(plain)
            with socket.create_connection(('127.0.0.1', endpoint), timeout=8) as silent:
                timed(lambda: closed(silent), 'server handshake')
            with tls_socket(endpoint, directory / 'ca.pem') as peer:
                peer.sendall(b'13|\n13|\n')
                response = b''
                while response.count(b'\n') < 2:
                    response += peer.recv(1024)
                require(response == b'14|\n14|\n', 'coalesced TLS frames lost buffered data')
                peer.sendall(b'1')
                started = time.monotonic()
                for byte in b'3|   ':
                    time.sleep(.8)
                    peer.sendall(bytes([byte]))
                closed(peer)
                elapsed = time.monotonic() - started
                require(4.5 <= elapsed < 8, f'server fragmented frame deadline was {elapsed:.2f}s')
            print('PASS trust, IP-ready endpoint, plaintext rejection, buffered frames and server deadlines', flush=True)
            run([binary, 'client', endpoint, 'idle'], env, timeout=75)
            # Stop must wake both an idle TLS peer and a peer stuck in handshake.
            idle = tls_socket(endpoint, directory / 'ca.pem')
            pending = socket.create_connection(('127.0.0.1', endpoint), timeout=8)
        closed(idle)
        idle.close()
        closed(pending)
        pending.close()
        wrong_ip = dict(server_env, LUMALIVE_AUTH_TLS_CERT_FILE=str(directory / 'wrong-ip.pem'),
                        LUMALIVE_AUTH_TLS_KEY_FILE=str(directory / 'wrong-ip.key'))
        with native_server(binary, wrong_ip) as endpoint:
            require('tls_failed' in run([binary, 'client', endpoint, 'probe'], env, expected=2), 'IP mismatch was accepted')
        stub_server(binary, env, directory)
        stub_server(binary, env, directory, stall=True)
        stub_server(binary, env, directory, tls=True)
    print('PASS native TLS transport: chain/IP verification, no downgrade, 5s deadlines, 61s idle and shutdown')


if __name__ == '__main__':
    main()
