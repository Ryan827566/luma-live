"""Online, current-user encrypted account backups and isolated restore drills.

Never replaces the live database, restarts a service, or changes client settings.
Only this project's two account tables and their schema are supported in v1.
"""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import time
import uuid

import bootstrap_local_postgres as pg

BASE = pg.ROOT / 'output/services/account-backups'
TABLES = {'luma_auth_users': 'user_id', 'luma_auth_security_events': 'event_id'}
BACKUP_ID = re.compile(r'[0-9]{8}T[0-9]{6}Z-[0-9a-f]{32}')
DRILL_ID = re.compile(r'luma_restore_[0-9a-f]{32}')
MAX_ARCHIVE = 256 * 1024 * 1024
DPAPI_OVERHEAD_LIMIT = 1024 * 1024


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def private_directory():
    if not BASE.resolve().is_relative_to(pg.ROOT.resolve() / 'output/services'):
        raise RuntimeError('Backup directory resolves outside this project')
    BASE.mkdir(parents=True, exist_ok=True)
    sid = pg.current_sid()
    pg.run(['icacls', BASE, '/inheritance:r', '/grant:r',
            f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])
    return sid


def deployment(sid):
    marker = json.loads(pg.MARKER.read_text(encoding='utf-8'))
    if (marker.get('owner_sid') != sid or marker.get('data') != str(pg.DATA.resolve())
            or marker.get('port') != pg.PORT or marker.get('major') != 17
            or not re.fullmatch(r'[0-9a-f]{32}', marker.get('cluster_id', ''))):
        raise RuntimeError('Managed cluster identity mismatch')
    if not pg.running():
        raise RuntimeError('Start the local deployment with Start-LumaLive.cmd first')
    credentials = pg.read_credentials()
    pg.check_identity(credentials)
    owner = pg.query(f"SELECT pg_get_userbyid(datdba) FROM pg_database WHERE datname='{pg.DATABASE}';",
                     credentials['admin_password'])
    if owner != pg.APPLICATION:
        raise RuntimeError('Application database owner does not match this managed deployment')
    if pg.query(f"SELECT rolsuper OR rolcreatedb OR rolcreaterole OR rolreplication "
                f"FROM pg_roles WHERE rolname='{pg.APPLICATION}';", credentials['admin_password']) != 'f':
        raise RuntimeError('Application role has unexpected administrative privileges')
    return marker, credentials


def connection(tool, user, database):
    return [pg.RUNTIME / ('bin/' + tool + '.exe'), '-w', '-h', '127.0.0.1',
            '-p', str(pg.PORT), '-U', user, '-d', database]


def environment(password):
    values = pg.environment(password)
    values['PGOPTIONS'] = ('-c statement_timeout=90000 -c lock_timeout=5000 '
                           '-c idle_in_transaction_session_timeout=120000')
    return values


def state_expression():
    # Only hashes/counts leave PostgreSQL, including for password/MFA columns.
    tables = []
    for table, key in TABLES.items():
        tables.append(f"'{table}', (SELECT json_build_object('rows', count(*), 'sha256', "
                      "encode(sha256(convert_to(COALESCE(string_agg("
                      "encode(sha256(convert_to(row_to_json(t)::text, 'UTF8')), 'hex'), "
                      f"'' ORDER BY {key} COLLATE \"C\"), ''), 'UTF8')), 'hex')) FROM public.{table} t)")
    # Types, defaults, nullability, constraints and index definitions must survive.
    schema = """(SELECT encode(sha256(convert_to(string_agg(item, E'\\n' ORDER BY item COLLATE "C"),
        'UTF8')), 'hex') FROM (
        SELECT c.relname || ':column:' || a.attnum || ':' || a.attname || ':' ||
          format_type(a.atttypid, a.atttypmod) || ':' || a.attnotnull || ':' ||
          COALESCE(pg_get_expr(d.adbin, d.adrelid), '') AS item
        FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace
        JOIN pg_attribute a ON a.attrelid=c.oid AND a.attnum>0 AND NOT a.attisdropped
        LEFT JOIN pg_attrdef d ON d.adrelid=c.oid AND d.adnum=a.attnum
        WHERE n.nspname='public' AND c.relname IN ('luma_auth_users','luma_auth_security_events')
        UNION ALL
        SELECT tablename || ':index:' || indexdef FROM pg_indexes WHERE schemaname='public'
          AND tablename IN ('luma_auth_users','luma_auth_security_events')
        UNION ALL
        SELECT c.relname || ':constraint:' || p.conname || ':' || pg_get_constraintdef(p.oid)
        FROM pg_constraint p JOIN pg_class c ON c.oid=p.conrelid
        JOIN pg_namespace n ON n.oid=c.relnamespace
        WHERE n.nspname='public' AND c.relname IN ('luma_auth_users','luma_auth_security_events')
        ) definitions)"""
    return "json_build_object('tables', json_build_object(" + ', '.join(tables) + "), 'schema_sha256', " + schema + ')'


def read_state(password, user, database):
    return json.loads(pg.query('SELECT ' + state_expression() + ';', password,
                              user=user, database=database))


@contextmanager
def snapshot(password, directory):
    output = directory / 'snapshot.tmp'
    error = directory / 'snapshot-error.tmp'
    process = None
    try:
        with output.open('wb') as stdout, error.open('wb') as stderr:
            process = subprocess.Popen(connection('psql', pg.APPLICATION, pg.DATABASE) +
                                       ['-X', '-q', '-A', '-t', '-v', 'ON_ERROR_STOP=1'],
                                       stdin=subprocess.PIPE, stdout=stdout, stderr=stderr,
                                       env=environment(password), creationflags=pg.NO_WINDOW)
            sql = ('BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY;\n'
                   "SELECT json_build_object('snapshot', pg_export_snapshot(), 'state', " +
                   state_expression() + ");\n\\echo LUMA_SNAPSHOT_READY\n")
            process.stdin.write(sql.encode('utf-8'))
            process.stdin.flush()
            until = time.monotonic() + 30
            while True:
                lines = output.read_text(encoding='utf-8').splitlines()
                if lines and lines[-1] == 'LUMA_SNAPSHOT_READY':
                    record = json.loads(lines[-2])
                    if not re.fullmatch(r'[0-9A-Fa-f]+-[0-9A-Fa-f]+-[0-9]+', record['snapshot']):
                        raise RuntimeError('Invalid exported snapshot identity')
                    break
                if process.poll() is not None or time.monotonic() >= until:
                    raise RuntimeError('Could not obtain a consistent account snapshot within 30 seconds')
                time.sleep(.05)
            yield record
    finally:
        if process is not None:
            try:
                process.communicate(b'ROLLBACK;\n\\q\n', timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                # This handle belongs only to the snapshot keeper created above.
                process.kill()
                process.wait(timeout=5)
        output.unlink(missing_ok=True)
        error.unlink(missing_ok=True)


def archive_bytes(path, *, encrypted=False):
    limit = MAX_ARCHIVE + (DPAPI_OVERHEAD_LIMIT if encrypted else 0)
    if not 0 < path.stat().st_size <= limit:
        raise RuntimeError('Backup archive is empty or exceeds the supported size limit')
    return path.read_bytes()


def owned_drill(database, oid, password):
    if not DRILL_ID.fullmatch(database):
        raise RuntimeError('Refusing cleanup of a database outside the generated restore namespace')
    actual = pg.query(f"SELECT d.oid || '|' || r.rolname FROM pg_database d "
                      f"JOIN pg_roles r ON r.oid=d.datdba WHERE d.datname='{database}';", password)
    if actual != f'{oid}|{database}':
        raise RuntimeError('Restore database identity changed; preserved for inspection: ' + database)


def restore_drill(archive, expected, credentials):
    # A separate unprivileged role cannot write to the production database.
    name = 'luma_restore_' + uuid.uuid4().hex
    password = secrets.token_urlsafe(36)
    admin = credentials['admin_password']
    role_created = database_created = False
    oid = None
    try:
        pg.query(f"CREATE ROLE {name} LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION "
                 f"PASSWORD '{password}';", admin)
        role_created = True
        pg.query(f'CREATE DATABASE {name} OWNER {name} TEMPLATE template0;', admin)
        database_created = True
        oid = pg.query(f"SELECT oid FROM pg_database WHERE datname='{name}';", admin)
        pg.query(f'REVOKE CONNECT ON DATABASE {name} FROM PUBLIC;', admin)
        pg.run(connection('pg_restore', name, name) + ['--exit-on-error', '--single-transaction',
               '--no-owner', '--no-acl', archive], env=environment(password), timeout=100)
        actual = read_state(password, name, name)
        if actual != expected:
            raise RuntimeError('Restored account data/schema differs from the exported snapshot')
        report = {'verified_at': utc_now(), 'snapshot_matches': True,
                  'schema_matches': True, 'tables': actual['tables']}
    finally:
        original_error = sys.exc_info()[1]
        try:
            if database_created:
                owned_drill(name, oid, admin)
                # No FORCE or backend termination: never interrupt another connection.
                pg.query(f'DROP DATABASE {name};', admin)
                database_created = False
            if role_created:
                pg.query(f'DROP ROLE {name};', admin)
                role_created = False
        except Exception as error:
            raise RuntimeError('Restore drill cleanup incomplete; inspect only the generated '
                               f'database/role {name} (database pending={database_created}, '
                               f'role pending={role_created}): {error}; '
                               f'original operation error: {original_error or "none"}') from None
    return {**report, 'temporary_database_removed': True, 'temporary_role_removed': True}


def create_backup(marker, credentials):
    name = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex
    directory = BASE / (name + '.partial')
    directory.mkdir()
    plain = directory / 'accounts.dump.tmp'
    try:
        with snapshot(credentials['app_password'], directory) as record:
            command = connection('pg_dump', pg.APPLICATION, pg.DATABASE) + [
                '--format=custom', '--no-owner', '--no-acl', '--strict-names', '--lock-wait-timeout=5s',
                '--snapshot=' + record['snapshot'], '--file=' + str(plain)]
            command += ['--table=public.' + table for table in TABLES]
            pg.run(command, env=environment(credentials['app_password']), timeout=100)
        raw = archive_bytes(plain)
        encrypted = pg.dpapi(raw)
        (directory / 'accounts.dump.dpapi').write_bytes(encrypted)
        manifest = {'format': 1, 'created_at': utc_now(), 'cluster_id': marker['cluster_id'],
                    'owner_sid': marker['owner_sid'], 'database': pg.DATABASE, 'postgres_major': 17,
                    'scope': sorted(TABLES), 'protection': 'Windows current-user DPAPI',
                    'archive_sha256': digest(encrypted), 'plaintext_sha256': digest(raw),
                    'state': record['state']}
        # Exercise the actual encrypted artifact, not the original plaintext dump.
        plain.unlink()
        report = verify_archive(directory, manifest, marker, credentials)
        pg.write_json(directory / 'manifest.json', manifest)
        pg.write_json(directory / 'verification.json', report)
        directory.rename(BASE / name)
        return {'backup_id': name, 'path': str(BASE / name), **report}
    finally:
        plain.unlink(missing_ok=True)


def verify_archive(directory, manifest, marker, credentials):
    if (manifest.get('format') != 1 or manifest.get('cluster_id') != marker['cluster_id']
            or manifest.get('owner_sid') != marker['owner_sid'] or manifest.get('postgres_major') != 17
            or manifest.get('database') != pg.DATABASE or manifest.get('scope') != sorted(TABLES)):
        raise RuntimeError('Backup does not belong to this managed deployment/format')
    encrypted = archive_bytes(directory / 'accounts.dump.dpapi', encrypted=True)
    if digest(encrypted) != manifest.get('archive_sha256'):
        raise RuntimeError('Encrypted backup checksum mismatch; nothing was restored')
    raw = pg.dpapi(encrypted, decrypt=True)
    if not 0 < len(raw) <= MAX_ARCHIVE or not raw.startswith(b'PGDMP') or digest(raw) != manifest.get('plaintext_sha256'):
        raise RuntimeError('Decrypted backup checksum/format mismatch; nothing was restored')
    plain = directory / ('restore-' + uuid.uuid4().hex + '.tmp')
    try:
        plain.write_bytes(raw)
        return restore_drill(plain, manifest['state'], credentials)
    finally:
        plain.unlink(missing_ok=True)


def backup_directory(name):
    if not BACKUP_ID.fullmatch(name):
        raise RuntimeError('Use a complete backup ID from this project, not a path')
    directory = BASE / name
    if directory.resolve().parent != BASE.resolve():
        raise RuntimeError('Backup path resolves outside its private directory')
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['create', 'verify'])
    parser.add_argument('--backup-id', help='Required for verify; never accepts an arbitrary restore target')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows current-user DPAPI is required')
    if (args.action == 'verify') != bool(args.backup_id):
        parser.error('Only verify requires --backup-id')
    sid = private_directory()
    import msvcrt
    with (BASE / 'backup.lock').open('a+b') as lock:
        lock.seek(0); lock.write(b'1'); lock.flush(); lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            raise RuntimeError('Another account backup/restore drill is running') from None
        marker, credentials = deployment(sid)
        if args.action == 'create':
            report = create_backup(marker, credentials)
        else:
            directory = backup_directory(args.backup_id)
            manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
            report = verify_archive(directory, manifest, marker, credentials)
            pg.write_json(directory / 'verification.json', report)
            report = {'backup_id': args.backup_id, **report}
        print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        # Subprocess errors are sanitized by pg.run; never print SQL or secrets.
        print(f'Account backup failed: {error}', file=sys.stderr)
        raise SystemExit(1)
