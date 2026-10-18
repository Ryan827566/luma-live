"""Isolated OpenSSL/TLS fixtures; never writes the deployed certificates or stops services."""
from contextlib import contextmanager, ExitStack
import hashlib
import json
from pathlib import Path
import shutil
import socket
import ssl
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import manage_account_certificates as tls


class CertificateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix='certificate-tests-', dir=tls.ROOT / 'output/services'))
        cls.addClassCleanup(cls.cleanup_fixture)
        sid = tls.pg.current_sid()
        tls.pg.run(['icacls', cls.root, '/inheritance:r', '/grant:r',
                    f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])
        cls.certificates = cls.root / 'active'
        cls.certificates.mkdir()
        cls.client_ca = cls.root / 'client-ca.pem'
        cls.staging = cls.root / 'staging'
        tls.run_ssl(['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '20',
                     '-subj', '/CN=Isolated LumaLive Test CA', '-addext', 'basicConstraints=critical,CA:TRUE',
                     '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
                     '-keyout', cls.certificates / 'ca.key', '-out', cls.certificates / 'ca.pem'])
        cls.client_ca.write_bytes((cls.certificates / 'ca.pem').read_bytes())
        tls.run_ssl(['req', '-new', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=Isolated Test Server',
                     '-keyout', cls.certificates / 'server.key', '-out', cls.root / 'server.csr'])
        cls.extension = cls.root / 'server.ext'
        cls.extension.write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n'
                                 'extendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.1\n', encoding='ascii')
        cls.issue(cls.certificates / 'server.pem', '10')
        (cls.certificates / 'ca.srl').write_text('sentinel-never-overwrite', encoding='ascii')

    @classmethod
    def issue(cls, output, days, extension=None):
        tls.run_ssl(['x509', '-req', '-in', cls.root / 'server.csr', '-CA', cls.certificates / 'ca.pem',
                     '-CAkey', cls.certificates / 'ca.key', '-passin', 'pass:', '-set_serial', '0x1234',
                     '-days', days, '-sha256', '-extfile', extension or cls.extension, '-out', output])

    @classmethod
    def cleanup_fixture(cls):
        # Only the unique directory created by this fixture is recursively removed.
        if cls.root.resolve().parent != (tls.ROOT / 'output/services').resolve():
            raise RuntimeError('Fixture cleanup target escaped the intended directory')
        shutil.rmtree(cls.root)

    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.object(tls, 'CERTS', self.certificates))
        self.stack.enter_context(patch.object(tls, 'CLIENT_CA', self.client_ca))
        self.stack.enter_context(patch.object(tls, 'STAGING', self.staging))

    def live_hashes(self):
        return {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in [*self.certificates.iterdir(), self.client_ca]}

    @contextmanager
    def peer(self, certificate=None, stall=False):
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]
        stopped = threading.Event()
        errors = []
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(certificate or self.certificates / 'server.pem'),
                                str(self.certificates / 'server.key'))
        def serve():
            try:
                with listener.accept()[0] as raw:
                    raw.settimeout(5)
                    with context.wrap_socket(raw, server_side=True) as stream:
                        stream.recv(64)
                        stream.sendall(b'14|')
                        if stall:
                            stopped.wait(5)
                        else:
                            time.sleep(.02)
                            stream.sendall(b'error|not_authenticated\n')
            except (OSError, ssl.SSLError) as error:
                errors.append(type(error).__name__)
        thread = threading.Thread(target=serve)
        thread.start()
        try:
            with patch.object(tls, 'PORT', port):
                yield errors
        finally:
            stopped.set()
            listener.close()
            thread.join(timeout=6)
            self.assertFalse(thread.is_alive())

    def test_validity_boundaries(self):
        record = {'sha256': 'fixture', 'not_before': 100, 'not_after': 200}
        for now, state in [(99, 'not_yet_valid'), (100, 'renewal_due'), (199, 'renewal_due'), (200, 'expired')]:
            self.assertEqual(tls.describe(record, now, 1)['state'], state)
        self.assertEqual(tls.describe({**record, 'not_after': 100 + 31 * tls.DAY}, 100, 30)['state'], 'valid')

    def test_status_reads_fragmented_ping_without_modifying_files(self):
        before = self.live_hashes()
        with self.peer() as errors, patch.object(tls, 'private_staging') as create:
            report = tls.status(1)
            self.assertEqual(report['state'], 'healthy')
            self.assertTrue(report['served']['protocol_ready'])
            create.assert_not_called()
        self.assertFalse(errors)
        self.assertEqual(before, self.live_hashes())

    def test_probe_deadline_does_not_wait_for_incomplete_frame_forever(self):
        with self.peer(stall=True):
            start = time.monotonic()
            with self.assertRaises(TimeoutError):
                tls.probe(self.client_ca)
            self.assertLess(time.monotonic() - start, 4)

    def test_ca_and_online_certificate_mismatches_are_reported(self):
        original = tls.certificate
        def certificate(path):
            record = original(path)
            if path == self.client_ca:
                record['sha256'] = 'different-client-ca'
            return record
        with patch.object(tls, 'certificate', side_effect=certificate), \
             patch.object(tls, 'probe', return_value={'protocol_ready': True, 'sha256': 'different-served-leaf'}):
            report = tls.status(1)
        self.assertIn('client_ca_mismatch', report['issues'])
        self.assertIn('served_certificate_differs_from_disk', report['issues'])

    def test_status_reports_offline_and_expiry_warning(self):
        with patch.object(tls, 'probe', side_effect=ConnectionRefusedError):
            report = tls.status(30)
        self.assertIn('client_tls_probe_failed', report['issues'])
        self.assertIn('server_renewal_due', report['warnings'])

    def test_openssl_timeout_returns_structured_invalid_status(self):
        with patch.object(tls, 'run_ssl', side_effect=tls.subprocess.TimeoutExpired('openssl', 15)), \
             patch.object(tls, 'probe', side_effect=ConnectionRefusedError):
            report = tls.status(1)
        self.assertEqual(report['state'], 'invalid')
        self.assertIn('ca_unreadable_or_invalid', report['issues'])
        self.assertIn('disk_chain_ip_purpose_or_key_invalid', report['issues'])

    def test_stage_and_reinspect_preserve_live_files_and_ca_serial(self):
        before = self.live_hashes()
        with patch.object(tls, 'probe', side_effect=AssertionError('staging must work offline')):
            report = tls.stage(5)
            self.assertTrue(report['verified'])
            self.assertFalse(report['activated'])
            candidate = tls.candidate_path(report['candidate_id'])
            self.assertTrue(tls.inspect_candidate(candidate)['verified'])
        self.assertNotEqual(tls.certificate(candidate / 'server.pem')['sha256'],
                            tls.certificate(self.certificates / 'server.pem')['sha256'])
        self.assertEqual(before, self.live_hashes())
        self.assertFalse((candidate / 'ca.key').exists())
        manifest = json.loads((candidate / 'manifest.json').read_text())
        (candidate / 'manifest.json').write_text(json.dumps({**manifest, 'server_sha256': 'tampered'}))
        with self.assertRaisesRegex(RuntimeError, 'fingerprint mismatch'):
            tls.inspect_candidate(candidate)

    def test_candidate_becomes_stale_when_live_certificate_changes(self):
        report = tls.stage(5)
        original = tls.certificate
        def certificate(path):
            record = original(path)
            if path == self.certificates / 'server.pem':
                record['sha256'] = 'changed-source'
            return record
        with patch.object(tls, 'certificate', side_effect=certificate):
            with self.assertRaisesRegex(RuntimeError, 'stale'):
                tls.inspect_candidate(tls.candidate_path(report['candidate_id']))

    def test_expired_old_leaf_does_not_block_valid_renewal_candidate(self):
        original = (self.certificates / 'server.pem').read_bytes()
        try:
            self.issue(self.certificates / 'server.pem', '0')
            self.assertEqual(tls.describe(tls.certificate(self.certificates / 'server.pem'), time.time(), 1)['state'], 'expired')
            report = tls.stage(5)
            self.assertTrue(report['verified'])
        finally:
            (self.certificates / 'server.pem').write_bytes(original)

    def test_ca_lifetime_and_untrusted_client_rejected_before_creating_keys(self):
        with patch.object(tls, 'private_staging') as create:
            for days in (0, 366, 30):
                with self.assertRaises(RuntimeError):
                    tls.stage(days)
            original = tls.certificate
            def certificate(path):
                record = original(path)
                if path == self.client_ca:
                    record['sha256'] = 'wrong'
                return record
            with patch.object(tls, 'certificate', side_effect=certificate):
                with self.assertRaisesRegex(RuntimeError, 'trust differs'):
                    tls.stage(5)
            create.assert_not_called()

    def test_failed_signing_does_not_publish_or_change_live_certificates(self):
        before = self.live_hashes()
        previous = set(self.staging.iterdir()) if self.staging.exists() else set()
        original = tls.run_ssl
        def run(arguments, **kwargs):
            self.assertNotIn('-CAcreateserial', arguments)
            self.assertNotIn('-CAserial', arguments)
            if arguments[:2] == ['x509', '-req']:
                raise RuntimeError('injected signing failure')
            return original(arguments, **kwargs)
        with patch.object(tls, 'run_ssl', side_effect=run):
            with self.assertRaisesRegex(RuntimeError, 'signing failure'):
                tls.stage(5)
        added = set(self.staging.iterdir()) - previous
        self.assertEqual(len(added), 1)
        self.assertTrue(next(iter(added)).name.endswith('.partial'))
        self.assertEqual(before, self.live_hashes())

    def test_wrong_key_ip_and_purpose_are_rejected(self):
        with self.assertRaises(ssl.SSLError):
            tls.validate_pair(self.certificates / 'ca.pem', self.certificates / 'server.pem', self.certificates / 'ca.key')
        for kind, value in [('ip', 'extendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.2\n'),
                            ('purpose', 'extendedKeyUsage=clientAuth\nsubjectAltName=IP:127.0.0.1\n')]:
            extension = self.root / (kind + '.ext')
            extension.write_text('basicConstraints=critical,CA:FALSE\n' + value, encoding='ascii')
            cert = self.root / (kind + '.pem')
            self.issue(cert, '5', extension)
            with self.assertRaisesRegex(RuntimeError, 'verification failed'):
                tls.validate_pair(self.certificates / 'ca.pem', cert, self.certificates / 'server.key')

    def test_candidate_paths_cannot_select_live_deployment(self):
        for identifier in ('../account/tls', str(self.certificates), 'latest', 'a'*31, 'f'*33):
            with self.assertRaises(RuntimeError):
                tls.candidate_path(identifier)


if __name__ == '__main__':
    result = unittest.main(exit=False).result
    tls.pg.write_json(tls.ROOT / 'output/account-certificate-tests.json', {
        'finished_at': tls.datetime.now(tls.timezone.utc).isoformat(),
        'passed': result.wasSuccessful(), 'tests_run': result.testsRun,
        'failures': len(result.failures), 'errors': len(result.errors),
        'scope': 'Isolated CA, certificate files and loopback TLS peers; no deployed certificate changes'})
    sys.exit(0 if result.wasSuccessful() else 1)
