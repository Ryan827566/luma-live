"""Provision only LumaLive's dedicated, loopback-only PostgreSQL cluster on Windows.

Credentials stay in a current-user DPAPI file under an owner/SYSTEM-only directory.
No existing database, Windows service, firewall rule or global PATH is changed.
"""
import argparse
import csv
import ctypes
from ctypes import wintypes
import hashlib
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import sys
import time
import urllib.error
import urllib.request
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'output/services/postgresql'
VERSION = '17.11-3'
ARCHIVE = f'postgresql-{VERSION}-windows-x64-binaries.zip'
URL = 'https://get.enterprisedb.com/postgresql/' + ARCHIVE
SHA256 = '4b8db0930c38f6ef845db919551dedda3b6b845aeb0927b3d79a6e8e9e4537cf'
CHECKSUM_SOURCE = 'https://github.com/EnterpriseDB/edb-installers/issues/682'
PORT = 15432
ADMIN = 'luma_pg_admin'
APPLICATION = DATABASE = 'luma_auth'
RUNTIME = BASE / 'runtime/pgsql'
DATA = BASE / 'data'
MARKER = BASE / 'managed-cluster.json'
SECRET_FILE = BASE / 'credentials.dpapi'
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def run(command, *, env=None, input=None, timeout=45, check=True):
    # A daemon started by pg_ctl can inherit pipe handles. Waiting for pipe EOF
    # would hang after pg_ctl has exited; regular files keep the process timeout real.
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        result = subprocess.run([str(value) for value in command], input=input, stdout=stdout, stderr=stderr,
                                encoding='utf-8', errors='replace', env=env,
                                timeout=timeout, creationflags=NO_WINDOW)
        stdout.seek(0);stderr.seek(0)
        result.stdout=stdout.read().decode('utf-8',errors='replace')
        result.stderr=stderr.read().decode('utf-8',errors='replace')
    if check and result.returncode:
        # SQL input and connection secrets are deliberately never included in errors.
        raise RuntimeError(f'{Path(str(command[0])).name} failed with exit code {result.returncode}')
    return result


def current_sid():
    rows = list(csv.reader(run(['whoami', '/user', '/fo', 'csv', '/nh']).stdout.splitlines()))
    if not rows or len(rows[0]) < 2 or not rows[0][1].startswith('S-1-'):
        raise RuntimeError('Cannot identify the Windows account for private directory access')
    return rows[0][1]


def private_directory():
    if not BASE.resolve().is_relative_to(ROOT.resolve() / 'output/services'):
        raise RuntimeError('Service directory resolves outside the intended workspace')
    BASE.mkdir(parents=True, exist_ok=True)
    sid = current_sid()
    run(['icacls', BASE, '/inheritance:r', '/grant:r', f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])
    return sid


def dpapi(value, decrypt=False):
    class Blob(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('data', ctypes.POINTER(ctypes.c_ubyte))]
    crypt = ctypes.WinDLL('crypt32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    buffer = ctypes.create_string_buffer(value)
    source = Blob(len(value), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte)))
    result = Blob()
    if decrypt:
        function = crypt.CryptUnprotectData
        function.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p,
                             ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
        ok = function(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(result))
    else:
        function = crypt.CryptProtectData
        function.argtypes = [ctypes.POINTER(Blob), wintypes.LPCWSTR, ctypes.c_void_p,
                             ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
        ok = function(ctypes.byref(source), 'LumaLive local PostgreSQL', None, None, None, 1,
                      ctypes.byref(result))
    if not ok:
        raise RuntimeError('Windows could not protect/unprotect this service credential file')
    try:
        return ctypes.string_at(result.data, result.size)
    finally:
        kernel.LocalFree(result.data)


def read_credentials():
    value = json.loads(dpapi(SECRET_FILE.read_bytes(), decrypt=True))
    if set(value) != {'admin_password', 'app_password'} or any(
            not isinstance(password, str) or len(password) < 32 for password in value.values()):
        raise RuntimeError('Invalid managed PostgreSQL credential record')
    return value


def write_json(path, value):
    temporary = path.with_name(path.name + '.new')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def verify_archive(path):
    with path.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    if digest != SHA256:
        raise RuntimeError('PostgreSQL archive SHA-256 does not match the vendor-published checksum')


def prepare_runtime(allow_download):
    archive = BASE / ARCHIVE
    stamp = BASE / 'runtime-source.json'
    if RUNTIME.joinpath('bin/postgres.exe').is_file() and stamp.is_file():
        recorded = json.loads(stamp.read_text(encoding='utf-8'))
        if recorded.get('sha256') != SHA256:
            raise RuntimeError('Existing runtime provenance does not match the pinned archive')
        return
    if (BASE / 'runtime').exists():
        raise RuntimeError('An incomplete/unrecognized runtime exists; preserve it and inspect before retrying')
    if not archive.is_file():
        if not allow_download:
            raise RuntimeError('Pinned runtime is missing; use --download to retrieve the verified vendor archive')
        temporary = archive.with_suffix('.zip.partial')
        headers = {'User-Agent': 'LumaLive-local-deployment/1'}
        with urllib.request.urlopen(urllib.request.Request(URL, method='HEAD', headers=headers), timeout=30) as response:
            total = int(response.headers.get('Content-Length', '0'))
        if not 0 < total <= 1024 * 1024 * 1024:
            raise RuntimeError('Vendor archive length is absent or unexpectedly large')
        downloaded = temporary.stat().st_size if temporary.exists() else 0
        if downloaded > total:
            raise RuntimeError('Partial download exceeds the vendor archive length; preserve and inspect it')
        # Bounded ranges avoid silently accepting an interrupted long response and
        # retain contiguous partial progress. Only the final pinned digest is trusted.
        with temporary.open('ab') as target:
            interrupted = 0
            while downloaded < total:
                end = min(total - 1, downloaded + 8 * 1024 * 1024 - 1)
                request = urllib.request.Request(URL, headers={**headers, 'Range': f'bytes={downloaded}-{end}'})
                try:
                    with urllib.request.urlopen(request, timeout=45) as response:
                        if (not response.geturl().startswith('https://get.enterprisedb.com/') or response.status != 206 or
                                response.headers.get('Content-Range') != f'bytes {downloaded}-{end}/{total}'):
                            raise RuntimeError('Vendor did not return the requested contiguous archive range')
                        expected = end - downloaded + 1
                        while expected:
                            chunk = response.read(min(expected, 1024 * 1024))
                            if not chunk:
                                raise ConnectionError('Archive response ended early')
                            target.write(chunk)
                            downloaded += len(chunk)
                            expected -= len(chunk)
                except (OSError, urllib.error.URLError, http.client.HTTPException):
                    target.flush()
                    interrupted += 1
                    if interrupted >= 3:
                        raise RuntimeError('Archive download interrupted repeatedly; rerun --download to resume safely') from None
                    time.sleep(interrupted)
                    continue
                interrupted = 0
                target.flush()
                print(f'Downloaded {downloaded // (1024 * 1024)} / {total // (1024 * 1024)} MiB', flush=True)
        verify_archive(temporary)
        os.replace(temporary, archive)
    verify_archive(archive)
    staging = BASE / ('runtime-staging-' + uuid.uuid4().hex)
    staging.mkdir()
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            parts = Path(entry.filename.replace('\\', '/')).parts
            if not parts or parts[0] != 'pgsql':
                continue
            if len(parts) > 1 and parts[1] not in {'bin', 'lib', 'share', 'COPYRIGHT', 'LICENSE'}:
                continue
            destination = staging.joinpath(*parts).resolve()
            if not destination.is_relative_to(staging.resolve()) or (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise RuntimeError('Unsafe archive entry')
            if entry.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                with package.open(entry) as source, destination.open('wb') as target:
                    while chunk := source.read(1024 * 1024):
                        target.write(chunk)
    if not staging.joinpath('pgsql/bin/initdb.exe').is_file():
        raise RuntimeError('Vendor archive does not contain the expected PostgreSQL layout')
    staging.rename(BASE / 'runtime')
    write_json(stamp, {'version': VERSION, 'url': URL, 'sha256': SHA256,
                      'checksum_source': CHECKSUM_SOURCE})


def environment(password=None):
    values = {key.upper(): value for key, value in os.environ.items()
              if not key.upper().startswith('PG')}
    values['PATH'] = str(RUNTIME / 'bin') + os.pathsep + values.get('PATH', '')
    values['PGCONNECT_TIMEOUT'] = '5'
    values['PGCLIENTENCODING'] = 'UTF8'
    if password is not None:
        values['PGPASSWORD'] = password
    return values


def initialize(sid):
    if MARKER.exists():
        recorded = json.loads(MARKER.read_text(encoding='utf-8'))
        if (recorded.get('owner_sid') != sid or recorded.get('data') != str(DATA.resolve()) or
                recorded.get('port') != PORT or recorded.get('major') != 17):
            raise RuntimeError('Managed cluster identity does not match this account/path/port/version')
        credentials = read_credentials()
    else:
        if DATA.exists() and any(DATA.iterdir()):
            raise RuntimeError('Unrecognized database directory; no existing cluster will be overwritten')
        if SECRET_FILE.exists():
            raise RuntimeError('Credential file has no matching managed-cluster record; inspect it before proceeding')
        credentials = {'admin_password': secrets.token_urlsafe(36), 'app_password': secrets.token_urlsafe(36)}
        SECRET_FILE.write_bytes(dpapi(json.dumps(credentials).encode('utf-8')))
        write_json(MARKER, {'owner_sid': sid, 'data': str(DATA.resolve()), 'port': PORT,
                            'major': 17, 'cluster_id': uuid.uuid4().hex})
    if DATA.joinpath('PG_VERSION').is_file():
        if DATA.joinpath('PG_VERSION').read_text().strip() != '17':
            raise RuntimeError('Cluster major version mismatch; automatic upgrade is forbidden')
        return credentials
    if DATA.exists() and any(DATA.iterdir()):
        raise RuntimeError('Interrupted initialization left files; preserve them for inspection')
    password_file = BASE / ('init-password-' + uuid.uuid4().hex)
    try:
        password_file.write_text(credentials['admin_password'] + '\n', encoding='utf-8')
        run([RUNTIME / 'bin/initdb.exe', '-D', DATA, '--username=' + ADMIN,
             '--pwfile=' + str(password_file), '--auth-host=scram-sha-256',
             '--auth-local=scram-sha-256', '--encoding=UTF8', '--locale=C'],
            env=environment(), timeout=120)
    finally:
        password_file.unlink(missing_ok=True)
    with DATA.joinpath('postgresql.conf').open('a', encoding='utf-8') as output:
        output.write("\n# LumaLive dedicated local cluster\nlisten_addresses = '127.0.0.1'\n"
                     f"port = {PORT}\npassword_encryption = 'scram-sha-256'\n")
    return credentials


def running():
    if not RUNTIME.joinpath('bin/pg_ctl.exe').is_file() or not DATA.joinpath('PG_VERSION').is_file():
        return False
    return run([RUNTIME / 'bin/pg_ctl.exe', '-D', DATA, 'status'], check=False).returncode == 0


def query(sql, password, *, user=ADMIN, database='postgres'):
    return run([RUNTIME / 'bin/psql.exe', '-X', '-w', '-h', '127.0.0.1', '-p', str(PORT),
                '-U', user, '-d', database, '-A', '-t', '-v', 'ON_ERROR_STOP=1'],
               env=environment(password), input=sql, timeout=30).stdout.strip()


def check_identity(credentials):
    actual = query('SHOW data_directory;', credentials['admin_password'])
    if Path(actual).resolve() != DATA.resolve():
        raise RuntimeError('Connected database is not the managed LumaLive cluster')
    if query('SHOW listen_addresses;', credentials['admin_password']) != '127.0.0.1':
        raise RuntimeError('Managed database is not restricted to the required loopback address')


def start(credentials):
    if not running():
        with socket.socket() as probe:
            if probe.connect_ex(('127.0.0.1', PORT)) == 0:
                raise RuntimeError(f'Port {PORT} is already in use by another server')
        run([RUNTIME / 'bin/pg_ctl.exe', '-D', DATA, '-l', BASE / 'postgresql.log',
             '-o', f'-p {PORT} -h 127.0.0.1', '-w', '-t', '30', 'start'], env=environment(), timeout=40)
    check_identity(credentials)
    if query(f"SELECT 1 FROM pg_roles WHERE rolname='{APPLICATION}';", credentials['admin_password']) != '1':
        # Password travels only through the private stdin pipe, never process arguments.
        password = credentials['app_password'].replace("'", "''")
        query(f"CREATE ROLE {APPLICATION} LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION PASSWORD '{password}';",
              credentials['admin_password'])
    owner = query(f"SELECT pg_get_userbyid(datdba) FROM pg_database WHERE datname='{DATABASE}';", credentials['admin_password'])
    if not owner:
        query(f'CREATE DATABASE {DATABASE} OWNER {APPLICATION} ENCODING \'UTF8\';', credentials['admin_password'])
    elif owner != APPLICATION:
        raise RuntimeError('Existing application database has an unexpected owner; it was not altered')
    if query(f"SELECT rolsuper OR rolcreatedb OR rolcreaterole OR rolreplication FROM pg_roles WHERE rolname='{APPLICATION}';",
             credentials['admin_password']) != 'f':
        raise RuntimeError('Application role has unexpected administrative privileges')
    result = query('SELECT current_database(),current_user,inet_server_addr(),inet_server_port();',
                   credentials['app_password'], user=APPLICATION, database=DATABASE)
    if result != f'{DATABASE}|{APPLICATION}|127.0.0.1|{PORT}':
        raise RuntimeError('Application database readiness check did not match the managed endpoint')


def connection_environment():
    """For the local auth launcher only. Returns credentials in memory; never print."""
    credentials = read_credentials()
    values = environment()
    values['LUMALIVE_AUTH_DATABASE_URL'] = (
        f'host=127.0.0.1 port={PORT} dbname={DATABASE} user={APPLICATION} '
        f"password={credentials['app_password']} connect_timeout=5")
    return values


def status():
    return {'version': VERSION, 'runtime_ready': RUNTIME.joinpath('bin/postgres.exe').is_file(),
            'initialized': DATA.joinpath('PG_VERSION').is_file(), 'running': running(),
            'address': '127.0.0.1', 'port': PORT, 'database': DATABASE,
            'data_directory': str(DATA), 'client_library': str(RUNTIME / 'bin/libpq.dll'),
            'credentials': 'Windows current-user DPAPI; never printed',
            'windows_service_installed': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true', help='Allow the pinned vendor runtime download')
    parser.add_argument('--start', action='store_true', help='Initialize/start only the managed local cluster')
    parser.add_argument('--stop', action='store_true', help='Gracefully stop only the managed local cluster')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('This bootstrap is Windows-only')
    if args.start and args.stop:
        parser.error('Choose --start or --stop, not both')
    if not (args.start or args.stop or args.download):
        print(json.dumps(status(), indent=2));return 0
    sid = private_directory()
    import msvcrt
    with BASE.joinpath('bootstrap.lock').open('a+b') as lock:
        lock.seek(0);lock.write(b'1');lock.flush();lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            raise RuntimeError('Another PostgreSQL bootstrap is in progress') from None
        if args.stop:
            if not MARKER.is_file() or not DATA.joinpath('PG_VERSION').is_file():
                raise RuntimeError('No managed cluster exists to stop')
            credentials = initialize(sid)
            if running():
                check_identity(credentials)
                run([RUNTIME / 'bin/pg_ctl.exe', '-D', DATA, '-m', 'fast', '-w', '-t', '30', 'stop'], timeout=40)
        else:
            prepare_runtime(args.download)
            if args.start:
                start(initialize(sid))
        print(json.dumps(status(), indent=2))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f'PostgreSQL bootstrap failed: {error}', file=sys.stderr)
        raise SystemExit(1)
