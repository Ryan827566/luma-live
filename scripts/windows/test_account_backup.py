"""Backup failure boundaries; --integration adds an isolated PostgreSQL snapshot drill."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import uuid

import manage_account_backup as backup


class BackupTests(unittest.TestCase):
    def setUp(self):
        self.marker = {'cluster_id': 'a' * 32, 'owner_sid': 'fixture-sid'}
        self.raw = b'PGDMPsynthetic-data'
        self.encrypted = b'encrypted-fixture'
        self.manifest = {**self.marker, 'format': 1, 'database': backup.pg.DATABASE,
                         'postgres_major': 17, 'scope': sorted(backup.TABLES),
                         'archive_sha256': backup.digest(self.encrypted),
                         'plaintext_sha256': backup.digest(self.raw), 'state': {}}

    def test_path_and_production_cleanup_targets_are_rejected(self):
        for value in ('../luma_auth', 'luma_auth', 'D:/outside', 'latest', '20261018T010101Z-' + 'x'*32):
            with self.assertRaises(RuntimeError):
                backup.backup_directory(value)
            with patch.object(backup.pg, 'query') as query:
                with self.assertRaises(RuntimeError):
                    backup.owned_drill(value, '123', 'not-a-real-secret')
                query.assert_not_called()

    def test_changed_restore_database_identity_prevents_cleanup(self):
        name = 'luma_restore_' + 'a'*32
        with patch.object(backup.pg, 'query', return_value='999|luma_auth') as query:
            with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                backup.owned_drill(name, '123', 'not-a-real-secret')
            self.assertEqual(query.call_count, 1)
            self.assertTrue(query.call_args.args[0].startswith('SELECT'))

    def test_foreign_deployment_is_rejected_before_decryption(self):
        with patch.object(backup.pg, 'dpapi') as decrypt:
            with self.assertRaisesRegex(RuntimeError, 'does not belong'):
                backup.verify_archive(Path('unused'), {**self.manifest, 'cluster_id': 'b'*32}, self.marker, {})
            decrypt.assert_not_called()

    def test_corrupt_archive_never_reaches_decryption_or_restore(self):
        with tempfile.TemporaryDirectory(dir=backup.pg.ROOT / 'output') as temporary:
            directory = Path(temporary)
            (directory / 'accounts.dump.dpapi').write_bytes(b'corrupt')
            with patch.object(backup.pg, 'dpapi') as decrypt, patch.object(backup, 'restore_drill') as restore:
                with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
                    backup.verify_archive(directory, self.manifest, self.marker, {})
                decrypt.assert_not_called()
                restore.assert_not_called()

    def test_invalid_decrypted_content_never_reaches_restore(self):
        with tempfile.TemporaryDirectory(dir=backup.pg.ROOT / 'output') as temporary:
            directory = Path(temporary)
            (directory / 'accounts.dump.dpapi').write_bytes(self.encrypted)
            with patch.object(backup.pg, 'dpapi', return_value=b'not a PostgreSQL dump'), \
                 patch.object(backup, 'restore_drill') as restore:
                with self.assertRaisesRegex(RuntimeError, 'checksum/format mismatch'):
                    backup.verify_archive(directory, self.manifest, self.marker, {})
                restore.assert_not_called()
                self.assertEqual(len(list(directory.iterdir())), 1)

    def test_restore_failure_removes_only_decrypted_temporary_file(self):
        with tempfile.TemporaryDirectory(dir=backup.pg.ROOT / 'output') as temporary:
            directory = Path(temporary)
            artifact = directory / 'accounts.dump.dpapi'
            artifact.write_bytes(self.encrypted)
            with patch.object(backup.pg, 'dpapi', return_value=self.raw), \
                 patch.object(backup, 'restore_drill', side_effect=RuntimeError('restore failed')):
                with self.assertRaisesRegex(RuntimeError, 'restore failed'):
                    backup.verify_archive(directory, self.manifest, self.marker, {})
            self.assertEqual(artifact.read_bytes(), self.encrypted)
            self.assertEqual(list(directory.iterdir()), [artifact])

    def test_failed_pg_restore_cleans_up_only_new_database_and_role(self):
        name = 'luma_restore_' + 'b'*32
        def query(sql, *args, **kwargs):
            if sql.startswith('SELECT oid'): return '123'
            if sql.startswith('SELECT d.oid'): return '123|' + name
            return ''
        with patch.object(backup.uuid, 'uuid4') as identifier, \
             patch.object(backup.pg, 'query', side_effect=query) as calls, \
             patch.object(backup.pg, 'run', side_effect=RuntimeError('pg_restore failed')):
            identifier.return_value.hex = 'b'*32
            with self.assertRaisesRegex(RuntimeError, 'pg_restore failed'):
                backup.restore_drill(Path('fixture.dump'), {}, {'admin_password': 'private-fixture'})
            statements = [call.args[0] for call in calls.call_args_list]
            self.assertEqual(statements[-2:], [f'DROP DATABASE {name};', f'DROP ROLE {name};'])
            self.assertFalse(any('FORCE' in sql or 'pg_terminate_backend' in sql for sql in statements))

    def test_failure_to_create_database_does_not_drop_existing_database(self):
        def query(sql, *args, **kwargs):
            if sql.startswith('CREATE DATABASE'): raise RuntimeError('already exists')
            return ''
        with patch.object(backup.pg, 'query', side_effect=query) as calls:
            with self.assertRaisesRegex(RuntimeError, 'already exists'):
                backup.restore_drill(Path('fixture.dump'), {}, {'admin_password': 'private-fixture'})
            self.assertFalse(any(call.args[0].startswith('DROP DATABASE') for call in calls.call_args_list))

    def test_cleanup_failures_name_residue_and_preserve_original_error(self):
        name = 'luma_restore_' + 'c'*32
        for failing_statement in ('DROP DATABASE', 'DROP ROLE', 'SELECT oid'):
            with self.subTest(failing_statement=failing_statement):
                def query(sql, *args, **kwargs):
                    if sql.startswith(failing_statement): raise RuntimeError('injected cleanup failure')
                    if sql.startswith('SELECT oid'): return '123'
                    if sql.startswith('SELECT d.oid'): return '123|' + name
                    return ''
                with patch.object(backup.uuid, 'uuid4') as identifier, \
                     patch.object(backup.pg, 'query', side_effect=query) as calls, \
                     patch.object(backup.pg, 'run', side_effect=RuntimeError('injected restore failure')):
                    identifier.return_value.hex = 'c'*32
                    with self.assertRaisesRegex(RuntimeError, name) as caught:
                        backup.restore_drill(Path('fixture.dump'), {}, {'admin_password': 'private-fixture'})
                    message = str(caught.exception)
                    self.assertIn('role pending=True', message)
                    self.assertNotIn('private-fixture', message)
                    if failing_statement == 'DROP ROLE':
                        self.assertIn('database pending=False', message)
                    if failing_statement != 'SELECT oid':
                        self.assertIn('injected restore failure', message)
                    else:
                        self.assertIn('injected cleanup failure', message)
                        self.assertFalse(any(c.args[0].startswith('DROP DATABASE') for c in calls.call_args_list))

    def test_dpapi_overhead_does_not_reject_supported_plaintext_size(self):
        with tempfile.TemporaryDirectory(dir=backup.pg.ROOT / 'output') as temporary:
            directory = Path(temporary)
            encrypted = b'wrapped-' + self.raw
            (directory / 'accounts.dump.dpapi').write_bytes(encrypted)
            manifest = {**self.manifest, 'archive_sha256': backup.digest(encrypted)}
            with patch.object(backup, 'MAX_ARCHIVE', len(self.raw)), \
                 patch.object(backup.pg, 'dpapi', return_value=self.raw), \
                 patch.object(backup, 'restore_drill', return_value={'passed': True}) as restore:
                self.assertEqual(backup.verify_archive(directory, manifest, self.marker, {}), {'passed': True})
                restore.assert_called_once()
            oversized = self.raw + b'1'
            with patch.object(backup, 'MAX_ARCHIVE', len(self.raw)), \
                 patch.object(backup.pg, 'dpapi', return_value=oversized), \
                 patch.object(backup, 'restore_drill') as restore:
                with self.assertRaisesRegex(RuntimeError, 'checksum/format mismatch'):
                    backup.verify_archive(directory, {**manifest, 'plaintext_sha256': backup.digest(oversized)},
                                          self.marker, {})
                restore.assert_not_called()

    def test_failed_creation_never_publishes_an_unverified_backup(self):
        @contextmanager
        def snapshot(*args):
            yield {'snapshot': '00000001-00000001-1', 'state': {}}
        def dump(command, **kwargs):
            target = next(str(item)[7:] for item in command if str(item).startswith('--file='))
            Path(target).write_bytes(self.raw)
        with tempfile.TemporaryDirectory(dir=backup.pg.ROOT / 'output') as temporary:
            base = Path(temporary)
            with patch.object(backup, 'BASE', base), patch.object(backup, 'snapshot', snapshot), \
                 patch.object(backup.pg, 'run', side_effect=dump), \
                 patch.object(backup.pg, 'dpapi', return_value=self.encrypted), \
                 patch.object(backup, 'verify_archive', side_effect=RuntimeError('verification failed')):
                with self.assertRaisesRegex(RuntimeError, 'verification failed'):
                    backup.create_backup(self.marker, {'app_password': 'private-fixture'})
            self.assertEqual(len(list(base.iterdir())), 1)
            directory = next(base.iterdir())
            self.assertTrue(directory.name.endswith('.partial'))
            self.assertEqual([p.name for p in directory.iterdir()], ['accounts.dump.dpapi'])


def integration():
    """Never writes the production account tables; fixture role/DB are created here."""
    sid = backup.private_directory()
    marker, credentials = backup.deployment(sid)
    admin = credentials['admin_password']
    name = 'luma_restore_' + uuid.uuid4().hex
    password = backup.secrets.token_urlsafe(36)
    role_created = database_created = False
    oid = None
    try:
        backup.pg.query(f"CREATE ROLE {name} LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION "
                        f"PASSWORD '{password}';", admin)
        role_created = True
        backup.pg.query(f'CREATE DATABASE {name} OWNER {name} TEMPLATE template0;', admin)
        database_created = True
        oid = backup.pg.query(f"SELECT oid FROM pg_database WHERE datname='{name}';", admin)
        backup.pg.query(f'REVOKE CONNECT ON DATABASE {name} FROM PUBLIC;', admin)
        schema = (backup.pg.ROOT / 'LumaLive/server/auth/db/schema.sql').read_text(encoding='utf-8')
        backup.pg.query(schema, password, user=name, database=name)
        insert = ("INSERT INTO luma_auth_users (user_id, username, email, display_name, password_salt, "
                  "password_verifier, mfa_totp_secret_hex) VALUES "
                  "('fixture1','fixture1','fixture1@example.invalid','测试快照','salt','verifier','fixture-secret');")
        backup.pg.query(insert, password, user=name, database=name)
        directory = backup.BASE / ('test-' + uuid.uuid4().hex)
        directory.mkdir()
        plain = directory / 'test.dump'
        try:
            with patch.object(backup.pg, 'APPLICATION', name), patch.object(backup.pg, 'DATABASE', name):
                with backup.snapshot(password, directory) as record:
                    # A concurrent committed change must not alter the exported snapshot.
                    backup.pg.query("UPDATE luma_auth_users SET display_name='after snapshot';", password,
                                    user=name, database=name)
                    if backup.read_state(password, name, name) == record['state']:
                        raise AssertionError('Concurrent fixture update was not observed outside the snapshot')
                    backup.pg.run(backup.connection('pg_dump', name, name) + ['--format=custom', '--no-owner',
                                  '--no-acl', '--snapshot=' + record['snapshot'], '--file=' + str(plain)],
                                  env=backup.environment(password), timeout=100)
                raw = backup.archive_bytes(plain)
                if backup.pg.dpapi(backup.pg.dpapi(raw), decrypt=True) != raw:
                    raise AssertionError('DPAPI round trip changed the archive')
                report = backup.restore_drill(plain, record['state'], credentials)
        finally:
            plain.unlink(missing_ok=True)
            directory.rmdir()  # Empty, exact directory created above; never recursive.
    finally:
        if database_created:
            backup.owned_drill(name, oid, admin)
            backup.pg.query(f'DROP DATABASE {name};', admin)
        if role_created:
            backup.pg.query(f'DROP ROLE {name};', admin)
    result = {'passed': True, 'concurrent_snapshot_isolation': True, 'dpapi_round_trip': True,
              'fixture_removed': True, **report}
    backup.pg.write_json(backup.pg.ROOT / 'output/account-backup-integration.json', result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    if '--integration' in sys.argv:
        integration()
    else:
        unittest.main()
