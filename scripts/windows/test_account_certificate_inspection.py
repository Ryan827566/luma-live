"""Private file-only certificate diagnostics fixtures; no account server is used."""
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import uuid

import account_certificate_inspection as diagnostic
import account_certificate_transaction as tx
import manage_account_certificates as tls
import manage_account_service as service


def cleanup(directory):
    if (directory.resolve().parent != (tls.ROOT / 'output/services').resolve()
            or not directory.name.startswith('certificate-inspection-tests-')):
        raise RuntimeError('Inspection fixture cleanup escaped its root')
    shutil.rmtree(directory)


def hashes(directory):
    return {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob('*') if path.is_file()}


class InspectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.material = Path(tempfile.mkdtemp(prefix='certificate-inspection-tests-material-', dir=tls.ROOT / 'output/services'))
        cls.addClassCleanup(cleanup, cls.material)
        sid = tls.pg.current_sid()
        tls.pg.run(['icacls', cls.material, '/inheritance:r', '/grant:r',
                    f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])
        tls.run_ssl(['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '20',
                     '-subj', '/CN=Isolated Diagnostic CA', '-addext', 'basicConstraints=critical,CA:TRUE',
                     '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
                     '-keyout', cls.material / 'ca.key', '-out', cls.material / 'ca.pem'])
        extension = cls.material / 'leaf.ext'
        extension.write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n'
                             'extendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.1\n', encoding='ascii')
        for side, days in (('old', 10), ('new', 5)):
            folder = cls.material / side
            folder.mkdir()
            tls.run_ssl(['req', '-new', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=Isolated Leaf',
                         '-keyout', folder / 'server.key', '-out', folder / 'leaf.csr'])
            tls.run_ssl(['x509', '-req', '-in', folder / 'leaf.csr', '-CA', cls.material / 'ca.pem',
                         '-CAkey', cls.material / 'ca.key', '-set_serial', '0x' + uuid.uuid4().hex,
                         '-days', str(days), '-extfile', extension, '-out', folder / 'server.pem'])

    def setUp(self):
        self.directory = Path(tempfile.mkdtemp(prefix='certificate-inspection-tests-', dir=tls.ROOT / 'output/services'))
        self.addCleanup(cleanup, self.directory)
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.active = self.directory / 'active'
        shutil.copytree(self.material / 'old', self.active)
        shutil.copyfile(self.material / 'ca.pem', self.active / 'ca.pem')
        self.client_ca = self.directory / 'client-ca.pem'
        shutil.copyfile(self.material / 'ca.pem', self.client_ca)
        self.name = 'Local\\LumaLive.InspectionTest.' + uuid.uuid4().hex
        for module, key, value in ((service, 'BASE', self.directory), (service, 'CERTS', self.active),
                                   (service, 'NAME', self.name), (tls, 'CERTS', self.active),
                                   (tls, 'CLIENT_CA', self.client_ca)):
            self.stack.enter_context(patch.object(module, key, value))
        self.identifier = uuid.uuid4().hex
        self.transaction = tx.root() / self.identifier
        self.transaction.mkdir(parents=True)
        shutil.copyfile(self.material / 'ca.pem', self.transaction / 'ca.pem')
        self.manifest = {'format': 1, 'deployment': tx.binding(),
                         'ca_bytes_sha256': tx.digest((self.transaction / 'ca.pem').read_bytes())}
        for side in ('old', 'new'):
            shutil.copytree(self.material / side, self.transaction / side)
            self.manifest[side] = {name: tx.digest((self.transaction / side / name).read_bytes()) for name in tx.PAIR}
            self.manifest[side + '_fingerprint'] = tls.certificate(self.transaction / side / 'server.pem')['sha256']
        (self.transaction / 'manifest.json').write_bytes(tx.encoded(self.manifest))
        self.record = {'format': 1, 'transaction_id': self.identifier,
                       'manifest_sha256': tx.digest(tx.encoded(self.manifest))}
        self.pending = tx.root() / 'pending.json'
        self.pending.write_bytes(tx.encoded(self.record))
        # Any accidental production or filesystem mutation path is a hard failure.
        self.forbidden = []
        for module, names in ((service, ('stop', 'start', '_start', 'prepare', 'private_directory', 'service_environment')),
                              (tls, ('probe', 'stage', 'private_staging')),
                              (tx, ('recover', 'activate', 'atomic_bytes', 'move_durable'))):
            for name in names:
                self.forbidden.append(self.stack.enter_context(patch.object(module, name, side_effect=AssertionError('forbidden operation'))))

    def inspect(self, identifier=None):
        before = hashes(self.directory)
        result = diagnostic.inspect(identifier)
        self.assertEqual(before, hashes(self.directory))
        for operation in self.forbidden:
            operation.assert_not_called()
        self.assertFalse(result['runtime_checked'])
        self.assertFalse(result['served_certificate_checked'])
        self.assertNotIn('PRIVATE KEY', json.dumps(result))
        return result

    def terminal(self, name='committed.json'):
        self.pending.rename(self.transaction / name)

    def test_pending_old_new_and_both_mixed_pairs_are_read_only(self):
        for cert_side, key_side, expected in (('old', 'old', 'old'), ('new', 'new', 'new'),
                                               ('old', 'new', 'mixed'), ('new', 'old', 'mixed')):
            with self.subTest(expected=expected, cert_side=cert_side):
                for name, side in zip(tx.PAIR, (cert_side, key_side)):
                    shutil.copyfile(self.transaction / side / name, self.active / name)
                result = self.inspect()
                self.assertEqual(result['state'], 'pending')
                self.assertEqual(result['current_live_pair'], expected)
                self.assertEqual(result['recovery_assessment'], 'files_eligible')
                self.assertEqual(result['issues'], [])

    def test_no_pending_does_not_infer_history_success(self):
        self.terminal()
        result = self.inspect()
        self.assertEqual(result['state'], 'no_pending')
        self.assertNotIn('transaction_id', result)
        self.assertEqual(result['next_action'], 'none')

    def test_historical_terminals_survive_later_files_trust_and_expiry(self):
        for name, state in (('committed.json', 'committed'), ('rolled-back.json', 'rolled_back')):
            with self.subTest(state=state):
                (self.transaction / name).write_bytes(tx.encoded(self.record))
                if self.pending.exists():
                    self.pending.unlink()
                (self.active / 'server.key').write_text('later-key')
                self.client_ca.write_text('later-trust')
                with patch.object(diagnostic.time, 'time', return_value=time.time() + 30 * tls.DAY):
                    result = self.inspect(self.identifier)
                self.assertEqual(result['state'], state)
                self.assertEqual(result['recovery_assessment'], 'not_applicable')
                self.assertEqual(result['issues'], [])
                self.assertEqual(result['certificates']['old']['state'], 'expired')
                self.assertIn('historical_record_differs_from_current_deployment', result['warnings'])
                (self.transaction / name).unlink()

    def test_expired_old_and_ca_block_but_unused_expired_new_does_not(self):
        for days, eligible in ((6, True), (11, False), (21, False), (-1, False)):
            with self.subTest(days=days), patch.object(diagnostic.time, 'time', return_value=time.time() + days * tls.DAY):
                result = self.inspect()
                self.assertEqual(result['recovery_assessment'], 'files_eligible' if eligible else 'blocked')
                if days == 6:
                    self.assertIn('unused_candidate_not_currently_valid', result['warnings'])

    def test_external_missing_pair_and_changed_trust_block_recovery(self):
        for path in (self.active / 'server.key', self.active / 'server.pem', self.client_ca, self.active / 'ca.pem'):
            original = path.read_bytes()
            for replacement in (None, b'external-change'):
                with self.subTest(path=path.name, replacement=replacement):
                    if replacement is None:
                        path.unlink()
                    else:
                        path.write_bytes(replacement)
                    result = self.inspect()
                    self.assertEqual(result['recovery_assessment'], 'blocked')
                    path.write_bytes(original)

    def test_bad_json_shape_size_ids_and_hashes_do_not_leak_content(self):
        original = self.pending.read_bytes()
        for data in (b'not-json SECRET-CANARY', b'[]', b'x' * (tx.MAX_FILE + 1),
                     tx.encoded({**self.record, 'transaction_id': '../SECRET-CANARY'}),
                     tx.encoded({**self.record, 'manifest_sha256': 'x' * 64})):
            self.pending.write_bytes(data)
            result = self.inspect()
            self.assertEqual(result['state'], 'invalid')
            self.assertNotIn('SECRET-CANARY', json.dumps(result))
        self.pending.write_bytes(original)
        self.assertEqual(self.inspect('../SECRET-CANARY')['issues'], ['transaction_id_invalid'])

    def test_manifest_and_snapshot_corruption_are_blocked(self):
        for path in (self.transaction / 'manifest.json', self.transaction / 'ca.pem',
                     self.transaction / 'old/server.key', self.transaction / 'new/server.pem'):
            original = path.read_bytes()
            path.write_bytes(b'corrupt SECRET-CANARY')
            result = self.inspect()
            self.assertEqual(result['state'], 'invalid')
            self.assertNotIn('SECRET-CANARY', json.dumps(result))
            path.write_bytes(original)

    def test_wrong_deployment_and_malformed_snapshot_manifest(self):
        for manifest in ({**self.manifest, 'deployment': '0' * 64}, {**self.manifest, 'old': []}):
            raw = tx.encoded(manifest)
            (self.transaction / 'manifest.json').write_bytes(raw)
            self.pending.write_bytes(tx.encoded({**self.record, 'manifest_sha256': tx.digest(raw)}))
            result = self.inspect()
            self.assertEqual(result['state'], 'invalid')

    def test_conflicting_and_incomplete_outcome_records(self):
        terminal = self.transaction / 'committed.json'
        terminal.write_bytes(tx.encoded(self.record))
        self.assertEqual(self.inspect()['issues'], ['outcome_records_conflict'])
        self.pending.unlink()
        (self.transaction / 'rolled-back.json').write_bytes(tx.encoded(self.record))
        self.assertEqual(self.inspect(self.identifier)['issues'], ['outcome_records_conflict'])
        terminal.unlink()
        (self.transaction / 'rolled-back.json').unlink()
        self.assertEqual(self.inspect(self.identifier)['state'], 'incomplete')

    def test_other_pending_does_not_make_historical_record_recoverable(self):
        self.terminal()
        other = uuid.uuid4().hex
        self.pending.write_bytes(tx.encoded({**self.record, 'transaction_id': other}))
        result = self.inspect(self.identifier)
        self.assertEqual(result['state'], 'committed')
        self.assertTrue(result['pending'])
        self.assertEqual(result['pending_transaction_id'], other)
        self.assertEqual(result['next_action'], 'inspect_current_pending')
        self.assertEqual(result['recovery_assessment'], 'not_applicable')

    def test_io_error_and_deadline_do_not_expose_exception_text(self):
        with patch.object(diagnostic, 'read', side_effect=OSError('SECRET-CANARY')):
            result = self.inspect()
        self.assertEqual(result['issues'], ['record_or_certificate_unreadable'])
        self.assertNotIn('SECRET-CANARY', json.dumps(result))
        with patch.object(tls, 'certificate', side_effect=subprocess.TimeoutExpired('SECRET-CANARY', 10)):
            result = self.inspect()
        self.assertEqual(result['issues'], ['inspection_timeout'])
        self.assertEqual(result['recovery_assessment'], 'not_assessed')
        self.assertNotIn('SECRET-CANARY', json.dumps(result))

    def test_busy_lock_does_not_read_files_and_abandonment_is_released(self):
        script = ("import os,sys;sys.path.insert(0,'scripts/windows');import manage_account_service as s;"
                  "s.NAME=sys.argv[1];lock=s.operation_lock();lock.__enter__();print('ready',flush=True);input();os._exit(0)")
        process = subprocess.Popen([sys.executable, '-B', '-c', script, self.name], cwd=service.ROOT,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        handle = service.kernel.CreateMutexW(None, False, self.name + '.operation')
        try:
            self.assertEqual(process.stdout.readline().strip(), 'ready')
            started = time.monotonic()
            with patch.object(diagnostic, 'read') as read:
                result = self.inspect()
            self.assertEqual(result['state'], 'busy')
            self.assertIsNone(result['pending'])
            self.assertLess(time.monotonic() - started, 2)
            read.assert_not_called()
            process.communicate('\n', timeout=5)  # exits while holding the mutex
            original_wait = service.kernel.WaitForSingleObject
            states = []
            def tracked_wait(*args):
                state = original_wait(*args)
                states.append(state)
                return state
            with patch.object(service.kernel, 'WaitForSingleObject', side_effect=tracked_wait):
                self.assertEqual(self.inspect()['recovery_assessment'], 'files_eligible')
            self.assertEqual(states, [0x80])
            self.assertEqual(service.kernel.WaitForSingleObject(handle, 0), 0)
            service.kernel.ReleaseMutex(handle)
        finally:
            if process.poll() is None:
                process.communicate('\n', timeout=5)
            service.kernel.CloseHandle(handle)

    def test_lock_error_is_not_reported_busy(self):
        with patch.object(service, 'operation_lock', side_effect=RuntimeError('SECRET-CANARY')):
            result = self.inspect()
        self.assertEqual(result['issues'], ['operation_lock_unavailable'])
        self.assertEqual(result['state'], 'unavailable')

    def test_cli_no_pending_is_read_only(self):
        self.pending.unlink()
        with patch.object(sys, 'argv', ['manage_account_certificates.py', 'transaction-status']), \
                patch('builtins.print') as output:
            before = hashes(self.directory)
            self.assertEqual(tls.main(), 0)
            self.assertEqual(hashes(self.directory), before)
        result = json.loads(output.call_args.args[0])
        self.assertEqual(result['state'], 'no_pending')


if __name__ == '__main__':
    result = unittest.main(exit=False).result
    tls.pg.write_json(tls.ROOT / 'output/account-certificate-inspection-tests.json', {
        'passed': result.wasSuccessful(), 'tests_run': result.testsRun,
        'failures': len(result.failures), 'errors': len(result.errors),
        'fixture_cleanup_complete': result.wasSuccessful(),
        'scope': 'Isolated certificate files and native operation mutex; no account service or network'})
    sys.exit(0 if result.wasSuccessful() else 1)
