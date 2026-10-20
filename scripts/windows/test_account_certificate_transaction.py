"""Real Windows owned TLS fixture, isolated certificates, and crash recovery."""
from contextlib import ExitStack
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import uuid

import account_certificate_transaction as tx
import manage_account_certificates as tls
import manage_account_service as service

FIXTURE = service.ROOT / 'output/auth-tls-20261017/Release/luma_auth_tls_fixture.exe'
CANDIDATE = 'a' * 32


def fixture_environment():
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('LUMALIVE_')}
    env.update(LUMALIVE_AUTH_ENV='test', LUMALIVE_AUTH_TLS_CA_FILE='',
               LUMALIVE_AUTH_TLS_CERT_FILE=str(tls.CERTS / 'server.pem'),
               LUMALIVE_AUTH_TLS_KEY_FILE=str(tls.CERTS / 'server.key'),
               LUMALIVE_AUTH_TLS_LIBRARY_DIR=str(tls.pg.RUNTIME / 'bin'))
    return env


def configure(directory, name, port, stack):
    directory = Path(directory).resolve()
    if (directory.parent != (service.ROOT / 'output/services').resolve()
            or not directory.name.startswith('certificate-transaction-tests-')
            or not name.startswith('Local\\LumaLive.CertificateTest.') or not 1024 < port < 65536 or port == 9100):
        raise RuntimeError('Refusing non-isolated certificate transaction fixture')
    for module, key, value in [
        (service, 'BASE', directory), (service, 'CERTS', directory / 'active'),
        (service, 'STATE', directory / 'process.json'), (service, 'NAME', name),
        (service, 'PORT', port), (service, 'SERVER', FIXTURE),
        (tls, 'CERTS', directory / 'active'), (tls, 'CLIENT_CA', directory / 'client-ca.pem'),
        (tls, 'STAGING', directory / 'staging'), (tls, 'PORT', port),
        (service, 'service_environment', fixture_environment),
        (service, 'supervisor_command', lambda job: command(directory, name, port, 'worker', job))
    ]:
        stack.enter_context(patch.object(module, key, value))


def command(directory, name, port, action, mode=''):
    return [sys.executable, '-B', str(Path(__file__).resolve()), '--actor',
            str(directory), name, str(port), action, mode]


def actor(directory, name, port, action, mode):
    with ExitStack() as stack:
        configure(directory, name, port, stack)
        if action == 'worker':
            # Existing TLS test binary takes "server PORT"; preserve the actual
            # production supervisor, process object, Job and shutdown path.
            original = subprocess.Popen
            def fixture_popen(args, **kwargs):
                if args == [str(FIXTURE), str(port)]:
                    args = [str(FIXTURE), 'server', str(port)]
                return original(args, **kwargs)
            stack.enter_context(patch.object(subprocess, 'Popen', side_effect=fixture_popen))
            service.run_worker(mode)
            return
        if action != 'activate':
            raise RuntimeError('Unknown fixture actor')
        if mode == 'intent':
            original = tx.save_intent
            def crash_after_intent(candidate):
                original(candidate)
                os._exit(73)
            stack.enter_context(patch.object(tx, 'save_intent', side_effect=crash_after_intent))
        elif mode == 'stopped':
            original = service.stop
            def crash_after_stop():
                original()
                os._exit(73)
            stack.enter_context(patch.object(service, 'stop', side_effect=crash_after_stop))
        elif mode in ('half-pair', 'rollback-half'):
            original = tx.atomic_bytes
            def crash_after_file(path, data):
                original(path, data)
                if path == tls.CERTS / 'server.pem':
                    if mode == 'half-pair' or data == (tx.root() / json.loads((tx.root() / 'pending.json').read_text())['transaction_id'] / 'old/server.pem').read_bytes():
                        os._exit(73)
            stack.enter_context(patch.object(tx, 'atomic_bytes', side_effect=crash_after_file))
            if mode == 'rollback-half':
                original_start = service._start
                starts = []
                def fail_new_start(*args, **kwargs):
                    if not starts:
                        starts.append(True)
                        raise RuntimeError('injected new-start failure')
                    return original_start(*args, **kwargs)
                stack.enter_context(patch.object(service, '_start', side_effect=fail_new_start))
        elif mode == 'new-ready':
            stack.enter_context(patch.object(tx, 'finish', side_effect=lambda *args: os._exit(73)))
        else:
            raise RuntimeError('Unknown crash phase')
        tx.activate(CANDIDATE)


def cleanup_directory(directory):
    if (directory.resolve().parent != (service.ROOT / 'output/services').resolve()
            or not directory.name.startswith('certificate-transaction-tests-')):
        raise RuntimeError('Fixture cleanup escaped its intended directory')
    shutil.rmtree(directory)


class TransactionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not FIXTURE.is_file():
            raise RuntimeError('Build the isolated native auth TLS fixture first')
        cls.material = Path(tempfile.mkdtemp(prefix='certificate-transaction-tests-material-', dir=service.ROOT / 'output/services'))
        cls.addClassCleanup(cleanup_directory, cls.material)
        sid = tls.pg.current_sid()
        tls.pg.run(['icacls', cls.material, '/inheritance:r', '/grant:r',
                    f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])
        (cls.material / 'active').mkdir()
        active = cls.material / 'active'
        tls.run_ssl(['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '20',
                     '-subj', '/CN=Isolated Transaction Test CA', '-addext', 'basicConstraints=critical,CA:TRUE',
                     '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
                     '-keyout', active / 'ca.key', '-out', active / 'ca.pem'])
        tls.run_ssl(['req', '-new', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=Isolated Old Leaf',
                     '-keyout', active / 'server.key', '-out', cls.material / 'server.csr'])
        extension = cls.material / 'server.ext'
        extension.write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n'
                             'extendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.1\n', encoding='ascii')
        tls.run_ssl(['x509', '-req', '-in', cls.material / 'server.csr', '-CA', active / 'ca.pem',
                     '-CAkey', active / 'ca.key', '-passin', 'pass:', '-set_serial', '0x123456', '-days', '10',
                     '-sha256', '-extfile', extension, '-out', active / 'server.pem'])
        (active / 'ca.srl').write_text('unchanged-sentinel', encoding='ascii')
        (cls.material / 'client-ca.pem').write_bytes((active / 'ca.pem').read_bytes())
        with patch.object(tls, 'CERTS', active), patch.object(tls, 'CLIENT_CA', cls.material / 'client-ca.pem'), patch.object(tls, 'STAGING', cls.material / 'staging'):
            staged = tls.stage(5)
            Path(staged['path']).rename(cls.material / 'staging' / CANDIDATE)
        cls.old_fingerprint = tls.certificate(active / 'server.pem')['sha256']
        cls.new_fingerprint = tls.certificate(cls.material / 'staging' / CANDIDATE / 'server.pem')['sha256']

    def setUp(self):
        self.directory = Path(tempfile.mkdtemp(prefix='certificate-transaction-tests-', dir=service.ROOT / 'output/services'))
        self.addCleanup(cleanup_directory, self.directory)
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        for folder in ('active', 'staging'):
            shutil.copytree(self.material / folder, self.directory / folder)
        shutil.copyfile(self.material / 'client-ca.pem', self.directory / 'client-ca.pem')
        self.name = 'Local\\LumaLive.CertificateTest.' + uuid.uuid4().hex
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            self.port = reserve.getsockname()[1]
        configure(self.directory, self.name, self.port, self.stack)
        self.launched = []
        original_popen = subprocess.Popen
        def retained_popen(*args, **kwargs):
            process = original_popen(*args, **kwargs)
            self.launched.append(process)
            return process
        self.stack.enter_context(patch.object(subprocess, 'Popen', side_effect=retained_popen))
        service.private_directory()
        # Stop while patched identities are still installed, before deletion.
        self.addCleanup(self.stop_fixture)
        service._start(prepare_service=False)
        tx.verify_served(self.old_fingerprint)

    def stop_fixture(self):
        service.stop(timeout=6)
        self.assertFalse(service.port_occupied())
        # Owner release confirms the server is stopped, but Python can still be
        # closing the supervisor's log handle. Wait our retained Popen handles
        # before removing fixture files; never enumerate/kill saved PIDs.
        for process in self.launched:
            process.wait(timeout=5)

    def assert_old_ready(self):
        tx.verify_served(self.old_fingerprint)
        for name in tx.PAIR:
            self.assertEqual((tls.CERTS / name).read_bytes(), (self.material / 'active' / name).read_bytes())
        self.assertFalse((tx.root() / 'pending.json').exists())

    def test_activation_commits_actual_served_certificate_and_keeps_ca(self):
        ca_before = {name: (tls.CERTS / name).read_bytes() for name in ('ca.pem', 'ca.key', 'ca.srl')}
        result = tx.activate(CANDIDATE)
        self.assertTrue(result['activated'])
        tx.verify_served(self.new_fingerprint)
        self.assertTrue((tx.root() / result['transaction_id'] / 'committed.json').is_file())
        self.assertEqual(ca_before, {name: (tls.CERTS / name).read_bytes() for name in ca_before})
        self.assertEqual(tls.CLIENT_CA.read_bytes(), ca_before['ca.pem'])
        self.assertEqual(tx.recover(), {'recovered': False, 'pending': False})

    def test_failed_new_start_restores_old_server(self):
        original = service._start
        calls = []
        def fail_once(*args, **kwargs):
            calls.append(True)
            if len(calls) == 1:
                raise RuntimeError('injected startup failure')
            return original(*args, **kwargs)
        with patch.object(service, '_start', side_effect=fail_once), self.assertRaisesRegex(RuntimeError, 'restored and verified'):
            tx.activate(CANDIDATE)
        self.assertEqual(len(calls), 2)
        self.assert_old_ready()

    def test_failed_certificate_verification_restores_old_server(self):
        original = tx.verify_served
        def reject_new(fingerprint):
            original(fingerprint)
            if fingerprint == self.new_fingerprint:
                raise RuntimeError('injected served mismatch')
        with patch.object(tx, 'verify_served', side_effect=reject_new), self.assertRaisesRegex(RuntimeError, 'restored and verified'):
            tx.activate(CANDIDATE)
        self.assert_old_ready()

    def test_second_live_file_failure_restores_complete_pair(self):
        original = tx.atomic_bytes
        failed = []
        def fail_second(path, data):
            if path == tls.CERTS / 'server.key' and not failed:
                failed.append(True)
                raise OSError('injected replacement error')
            return original(path, data)
        with patch.object(tx, 'atomic_bytes', side_effect=fail_second), self.assertRaisesRegex(RuntimeError, 'restored and verified'):
            tx.activate(CANDIDATE)
        self.assert_old_ready()

    def test_failed_rollback_retains_journal_then_recover_succeeds(self):
        with patch.object(service, '_start', side_effect=RuntimeError('injected unavailable launch')), self.assertRaisesRegex(RuntimeError, 'recovery remains pending'):
            tx.activate(CANDIDATE)
        self.assertTrue((tx.root() / 'pending.json').is_file())
        self.assertTrue(tx.recover()['recovered'])
        self.assert_old_ready()

    def test_journal_publication_failure_does_not_stop_service(self):
        original = tx.atomic_bytes
        def reject_pending(path, data):
            if path.name == 'pending.json':
                raise OSError('injected journal write failure')
            return original(path, data)
        with patch.object(tx, 'atomic_bytes', side_effect=reject_pending), patch.object(service, 'stop') as stop, self.assertRaises(OSError):
            tx.activate(CANDIDATE)
        stop.assert_not_called()
        self.assert_old_ready()

    def test_pending_blocks_prepare_start_restart_before_side_effects(self):
        with service.operation_lock():
            tx.save_intent(CANDIDATE)
        with patch.object(service, '_prepare') as prepare, patch.object(service, 'stop') as stop:
            for action in (service.prepare, service.start, service.restart):
                with self.assertRaisesRegex(RuntimeError, 'recovery is pending'):
                    action()
            prepare.assert_not_called()
            stop.assert_not_called()
        report = tls.status()
        self.assertTrue(report['recovery_required'])
        self.assertIn('certificate_recovery_pending', report['issues'])
        self.assertTrue(tx.recover()['recovered'])
        self.assert_old_ready()

    def test_corrupt_journal_and_snapshots_fail_before_stop(self):
        with service.operation_lock():
            directory, _ = tx.save_intent(CANDIDATE)
        for path in (tx.root() / 'pending.json', directory / 'manifest.json', directory / 'old/server.key', directory / 'new/server.pem'):
            original = path.read_bytes()
            path.write_bytes(b'corrupt')
            try:
                with patch.object(service, 'stop') as stop, self.assertRaises((ValueError, RuntimeError)):
                    tx.recover()
                stop.assert_not_called()
                self.assertTrue((tx.root() / 'pending.json').exists())
            finally:
                path.write_bytes(original)
        tx.recover()
        self.assert_old_ready()

    def test_external_live_or_trust_changes_are_not_overwritten(self):
        with service.operation_lock():
            tx.save_intent(CANDIDATE)
        for path in (tls.CERTS / 'server.key', tls.CLIENT_CA):
            original = path.read_bytes()
            path.write_bytes(b'external-change')
            try:
                with patch.object(service, 'stop') as stop, self.assertRaises(RuntimeError):
                    tx.recover()
                stop.assert_not_called()
                self.assertEqual(path.read_bytes(), b'external-change')
            finally:
                path.write_bytes(original)
        tx.recover()
        self.assert_old_ready()

    def test_bad_next_launch_configuration_is_rejected_before_stop(self):
        with patch.object(service, 'service_environment', side_effect=RuntimeError('configuration unreadable')), patch.object(service, 'stop') as stop, self.assertRaisesRegex(RuntimeError, 'configuration unreadable'):
            tx.activate(CANDIDATE)
        stop.assert_not_called()
        self.assert_old_ready()

    def test_short_lived_rollback_target_is_rejected_before_stop(self):
        original = tls.certificate
        def old_near_expiry(path):
            result = original(path)
            if path == tls.CERTS / 'server.pem':
                result['not_after'] = tls.time.time() + 3600
            return result
        with patch.object(tls, 'certificate', side_effect=old_near_expiry), patch.object(service, 'stop') as stop, self.assertRaisesRegex(RuntimeError, 'safe rollback'):
            tx.activate(CANDIDATE)
        stop.assert_not_called()
        self.assert_old_ready()

    def test_recovery_does_not_require_unused_new_leaf_to_be_unexpired(self):
        with service.operation_lock():
            directory, _ = tx.save_intent(CANDIDATE)
        original = tls.run_ssl
        future = str(int(tls.time.time() + 6 * tls.DAY))
        def future_verification(arguments, **kwargs):
            if arguments[0] == 'verify' and '-no_check_time' not in arguments:
                arguments = ['verify', '-attime', future, *arguments[1:]]
            return original(arguments, **kwargs)
        with patch.object(tls, 'run_ssl', side_effect=future_verification):
            with self.assertRaises(RuntimeError):
                tls.validate_pair(directory / 'ca.pem', directory / 'new/server.pem', directory / 'new/server.key')
            self.assertTrue(tx.recover()['recovered'])
        self.assert_old_ready()

    def test_failed_commit_record_rolls_back(self):
        original = tx.finish
        def reject_commit(directory, outcome):
            if outcome == 'committed':
                raise OSError('injected commit rename failure')
            return original(directory, outcome)
        with patch.object(tx, 'finish', side_effect=reject_commit), self.assertRaisesRegex(RuntimeError, 'restored and verified'):
            tx.activate(CANDIDATE)
        self.assert_old_ready()

    def test_commit_rename_followed_by_error_is_verified_as_committed(self):
        original = tx.finish
        def error_after_rename(directory, outcome):
            original(directory, outcome)
            raise OSError('injected lost completion acknowledgement')
        with patch.object(tx, 'finish', side_effect=error_after_rename):
            result = tx.activate(CANDIDATE)
        self.assertTrue(result['activated'])
        self.assertFalse((tx.root() / 'pending.json').exists())
        tx.verify_served(self.new_fingerprint)

    def test_failed_rollback_record_remains_pending_until_recovered(self):
        original = tx.finish
        def reject_all_records(directory, outcome):
            raise OSError('injected terminal rename failure')
        with patch.object(tx, 'finish', side_effect=reject_all_records), self.assertRaisesRegex(RuntimeError, 'recovery remains pending'):
            tx.activate(CANDIDATE)
        self.assertTrue((tx.root() / 'pending.json').exists())
        tx.verify_served(self.old_fingerprint)
        self.assertTrue(tx.recover()['recovered'])
        self.assert_old_ready()

    def test_rollback_rename_followed_by_error_is_verified_as_restored(self):
        original = tx.finish
        def lose_acknowledgement(directory, outcome):
            if outcome == 'rolled-back':
                original(directory, outcome)
            raise OSError('injected terminal acknowledgement failure')
        with patch.object(tx, 'finish', side_effect=lose_acknowledgement), self.assertRaisesRegex(RuntimeError, 'restored and verified'):
            tx.activate(CANDIDATE)
        self.assert_old_ready()

    def test_unconfirmed_completed_rollback_does_not_claim_pending(self):
        original_finish = tx.finish
        original_verify = tx.verify_served
        old_probes = []
        def lose_acknowledgement(directory, outcome):
            if outcome == 'rolled-back':
                original_finish(directory, outcome)
            raise OSError('injected terminal acknowledgement failure')
        def fail_rollback_confirmation(fingerprint):
            if fingerprint == self.old_fingerprint:
                old_probes.append(True)
                if len(old_probes) == 3:
                    raise RuntimeError('injected confirmation probe failure')
            return original_verify(fingerprint)
        with patch.object(tx, 'finish', side_effect=lose_acknowledgement), \
                patch.object(tx, 'verify_served', side_effect=fail_rollback_confirmation), \
                self.assertRaisesRegex(RuntimeError, 'recovery could not be confirmed. No pending record remains'):
            tx.activate(CANDIDATE)
        self.assertEqual(len(old_probes), 3)
        self.assertEqual(len(list(tx.root().glob('*/rolled-back.json'))), 1)
        self.assert_old_ready()

    def test_crashes_recover_each_switch_and_rollback_boundary(self):
        for phase in ('intent', 'stopped', 'half-pair', 'new-ready', 'rollback-half'):
            with self.subTest(phase=phase):
                # Pin the mutex to ensure its abandoned state survives actor exit.
                handle = service.kernel.CreateMutexW(None, False, self.name + '.operation')
                self.assertTrue(handle)
                try:
                    result = subprocess.run(command(self.directory, self.name, self.port, 'activate', phase),
                                            cwd=service.ROOT, capture_output=True, timeout=30,
                                            creationflags=subprocess.CREATE_NO_WINDOW)
                    self.assertEqual(result.returncode, 73, result.stderr.decode(errors='replace'))
                    state = service.kernel.WaitForSingleObject(handle, 0)
                    self.assertEqual(state, 0x80)
                    service.kernel.ReleaseMutex(handle)
                    self.assertTrue(tx.recover()['recovered'])
                    self.assert_old_ready()
                finally:
                    service.kernel.CloseHandle(handle)

    def test_cli_requires_explicit_restart_acknowledgement(self):
        for args in (['activate', '--candidate-id', CANDIDATE], ['recover']):
            result = subprocess.run([sys.executable, '-B', str(tls.ROOT / 'scripts/windows/manage_account_certificates.py'), *args],
                                    cwd=service.ROOT, capture_output=True, timeout=5,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(result.returncode, 2)
            self.assertIn(b'--allow-service-restart is required', result.stderr)
        self.assert_old_ready()


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--actor':
        actor(sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5], sys.argv[6])
    else:
        result = unittest.main(exit=False).result
        service.pg.write_json(service.ROOT / 'output/account-certificate-transaction-tests.json', {
            'passed': result.wasSuccessful(), 'tests_run': result.testsRun,
            'failures': len(result.failures), 'errors': len(result.errors),
            'fixture_cleanup_complete': result.wasSuccessful(),
            'fixture_sha256': hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),
            'scope': 'Isolated native memory-store TLS fixture, Windows owned lifecycle and process-exit crash injection; no production changes'})
        sys.exit(0 if result.wasSuccessful() else 1)
