"""Inspect, stage, or explicitly activate/recover local account TLS certificates.

Status/stage/inspect do not change live files or service lifecycle. Activation
and recovery require --allow-service-restart and retain private rollback files.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import socket
import ssl
import subprocess
import sys
import time
import uuid

import bootstrap_local_postgres as pg

ROOT = pg.ROOT
CERTS = ROOT / 'output/services/account/tls'
CLIENT_CA = ROOT / 'output/next/Release/lumalive-auth-ca.pem'
STAGING = ROOT / 'output/services/account-certificate-staging'
PORT = 9100
DAY = 86400


def openssl():
    for path in (pg.RUNTIME / 'bin/openssl.exe',
                 Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Git/usr/bin/openssl.exe'):
        if path.is_file():
            return path
    raise RuntimeError('OpenSSL command-line tool is unavailable')


def run_ssl(arguments, *, check=True):
    env = {k: v for k, v in pg.environment().items()
           if not k.startswith(('OPENSSL_', 'LUMALIVE_', 'SSL_CERT_'))}
    env['OPENSSL_CONF'] = str(ROOT / 'scripts/windows/account-openssl.cnf')
    return pg.run([openssl(), *arguments], env=env, timeout=15, check=check)


def pem_bytes(path):
    if not 0 < path.stat().st_size <= 128 * 1024:
        raise RuntimeError('Certificate file is empty or unexpectedly large')
    data = path.read_bytes()
    if data.count(b'-----BEGIN CERTIFICATE-----') != 1:
        raise RuntimeError('Exactly one certificate is required in each local TLS file')
    return data


def certificate(path):
    data = pem_bytes(path)
    der = ssl.PEM_cert_to_DER_cert(data.decode('ascii'))
    dates = dict(line.split('=', 1) for line in run_ssl(['x509', '-in', path, '-noout', '-dates']).stdout.splitlines())
    return {'sha256': hashlib.sha256(der).hexdigest(),
            'not_before': ssl.cert_time_to_seconds(dates['notBefore']),
            'not_after': ssl.cert_time_to_seconds(dates['notAfter'])}


def describe(record, now, warning_days):
    state = ('not_yet_valid' if now < record['not_before'] else
             'expired' if now >= record['not_after'] else
             'renewal_due' if record['not_after'] - now <= warning_days * DAY else 'valid')
    return {'sha256': record['sha256'], 'state': state,
            'not_before_utc': datetime.fromtimestamp(record['not_before'], timezone.utc).isoformat(),
            'not_after_utc': datetime.fromtimestamp(record['not_after'], timezone.utc).isoformat(),
            'days_remaining': int((record['not_after'] - now) // DAY)}


def validate_pair(ca, cert, key, *, check_time=True):
    result = run_ssl(['verify', *([] if check_time else ['-no_check_time']), '-no-CApath', '-no-CAstore', '-CAfile', ca,
                      '-purpose', 'sslserver', '-verify_ip', '127.0.0.1', cert], check=False)
    if result.returncode:
        raise RuntimeError('Certificate chain, server purpose, validity or loopback IP verification failed')
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    # Explicit empty password prevents interactive prompting for unexpected encrypted keys.
    context.load_cert_chain(str(cert), str(key), password='')


def probe(ca):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(cafile=str(ca))
    deadline = time.monotonic() + 3
    def remaining():
        value = deadline - time.monotonic()
        if value <= 0:
            raise TimeoutError('TLS probe deadline exceeded')
        return value
    with socket.create_connection(('127.0.0.1', PORT), timeout=remaining()) as raw:
        raw.settimeout(remaining())
        with context.wrap_socket(raw, server_hostname='127.0.0.1') as stream:
            fingerprint = hashlib.sha256(stream.getpeercert(binary_form=True)).hexdigest()
            stream.settimeout(remaining())
            stream.sendall(b'13|\n')
            response = b''
            while b'\n' not in response and len(response) < 256:
                stream.settimeout(remaining())
                chunk = stream.recv(256 - len(response))
                if not chunk:
                    break
                response += chunk
            return {'sha256': fingerprint,
                    'protocol_ready': response.startswith(b'14|') and b'\n' in response}


def status(warning_days=30):
    now = time.time()
    report = {'checked_at': datetime.fromtimestamp(now, timezone.utc).isoformat(),
              'warning_days': warning_days, 'issues': [], 'warnings': []}
    report['recovery_required'] = (CERTS.parent / 'certificate-transactions/pending.json').exists()
    if report['recovery_required']:
        report['issues'].append('certificate_recovery_pending')
    records = {}
    for label, path in [('ca', CERTS / 'ca.pem'), ('server', CERTS / 'server.pem'), ('client_ca', CLIENT_CA)]:
        try:
            records[label] = certificate(path)
            report[label] = describe(records[label], now, warning_days)
            state = report[label]['state']
            if state == 'renewal_due':
                report['warnings'].append(label + '_renewal_due')
            elif state != 'valid':
                report['issues'].append(label + '_' + state)
        except (OSError, ValueError, RuntimeError, KeyError, subprocess.TimeoutExpired):
            report['issues'].append(label + '_unreadable_or_invalid')
    if 'ca' in records and 'client_ca' in records:
        if records['ca']['sha256'] != records['client_ca']['sha256']:
            report['issues'].append('client_ca_mismatch')
    try:
        validate_pair(CERTS / 'ca.pem', CERTS / 'server.pem', CERTS / 'server.key')
        report['disk_chain_ip_purpose_and_key_valid'] = True
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired):
        report['disk_chain_ip_purpose_and_key_valid'] = False
        report['issues'].append('disk_chain_ip_purpose_or_key_invalid')
    try:
        report['served'] = probe(CLIENT_CA)
        if not report['served']['protocol_ready']:
            report['issues'].append('account_protocol_unready')
        if 'server' in records and report['served']['sha256'] != records['server']['sha256']:
            report['issues'].append('served_certificate_differs_from_disk')
    except (OSError, ValueError):
        report['issues'].append('client_tls_probe_failed')
    report['state'] = 'invalid' if report['issues'] else 'renewal_due' if report['warnings'] else 'healthy'
    return report


def private_staging():
    if not STAGING.resolve().is_relative_to(ROOT.resolve() / 'output/services'):
        raise RuntimeError('Certificate staging resolves outside this project')
    STAGING.mkdir(parents=True, exist_ok=True)
    sid = pg.current_sid()
    pg.run(['icacls', STAGING, '/inheritance:r', '/grant:r',
            f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'])


def candidate_path(identifier):
    if not re.fullmatch(r'[0-9a-f]{32}', identifier):
        raise RuntimeError('Use a candidate ID, not a path')
    path = STAGING / identifier
    if path.resolve().parent != STAGING.resolve():
        raise RuntimeError('Candidate resolves outside the private staging directory')
    return path


def inspect_candidate(path):
    manifest = json.loads((path / 'manifest.json').read_text(encoding='utf-8'))
    ca = certificate(path / 'ca.pem')
    leaf = certificate(path / 'server.pem')
    if (manifest.get('format') != 1 or manifest.get('ca_sha256') != ca['sha256']
            or manifest.get('server_sha256') != leaf['sha256']):
        raise RuntimeError('Candidate manifest or certificate fingerprint mismatch')
    if (certificate(CERTS / 'ca.pem')['sha256'] != ca['sha256']
            or certificate(CLIENT_CA)['sha256'] != ca['sha256']
            or certificate(CERTS / 'server.pem')['sha256'] != manifest.get('source_server_sha256')):
        raise RuntimeError('Live certificate or client trust changed; this candidate is stale')
    validate_pair(path / 'ca.pem', path / 'server.pem', path / 'server.key')
    if leaf['not_after'] > ca['not_after']:
        raise RuntimeError('Candidate outlives its CA')
    return {'verified': True, 'activated': False, 'ca_sha256': ca['sha256'],
            'server': describe(leaf, time.time(), 30)}


def stage(days=365):
    if not 1 <= days <= 365:
        raise RuntimeError('Candidate lifetime must be between 1 and 365 days')
    ca = certificate(CERTS / 'ca.pem')
    source = certificate(CERTS / 'server.pem')
    if certificate(CLIENT_CA)['sha256'] != ca['sha256']:
        raise RuntimeError('Client trust differs from the active CA; inspect deployment first')
    now = time.time()
    if now < ca['not_before'] or now + days * DAY + 60 >= ca['not_after']:
        raise RuntimeError('CA validity cannot cover the requested lifetime; CA rotation is a separate operation')
    private_staging()
    identifier = uuid.uuid4().hex
    directory = STAGING / (identifier + '.partial')
    directory.mkdir()
    # Only public CA material is copied. Existing CA key/serial/live leaf are never written.
    (directory / 'ca.pem').write_bytes(pem_bytes(CERTS / 'ca.pem'))
    extension = directory / 'server.ext'
    extension.write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n'
                         'extendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.1\n', encoding='ascii')
    run_ssl(['req', '-new', '-newkey', 'rsa:3072', '-nodes', '-subj', '/CN=LumaLive Local Account',
             '-keyout', directory / 'server.key', '-out', directory / 'server.csr'])
    run_ssl(['x509', '-req', '-in', directory / 'server.csr', '-CA', directory / 'ca.pem',
             '-CAkey', CERTS / 'ca.key', '-passin', 'pass:', '-set_serial', '0x' + secrets.token_hex(19),
             '-days', str(days), '-sha256', '-extfile', extension, '-out', directory / 'server.pem'])
    leaf = certificate(directory / 'server.pem')
    pg.write_json(directory / 'manifest.json', {'format': 1, 'created_at': datetime.now(timezone.utc).isoformat(),
                  'ca_sha256': ca['sha256'], 'source_server_sha256': source['sha256'],
                  'server_sha256': leaf['sha256'], 'requested_days': days, 'activated': False})
    report = inspect_candidate(directory)
    directory.rename(candidate_path(identifier))
    return {'candidate_id': identifier, 'path': str(candidate_path(identifier)), **report}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    check = sub.add_parser('status')
    check.add_argument('--warning-days', type=int, default=30)
    renew = sub.add_parser('stage')
    renew.add_argument('--days', type=int, default=365)
    inspect = sub.add_parser('inspect')
    inspect.add_argument('--candidate-id', required=True)
    activate = sub.add_parser('activate')
    activate.add_argument('--candidate-id', required=True)
    activate.add_argument('--allow-service-restart', action='store_true')
    recover = sub.add_parser('recover')
    recover.add_argument('--allow-service-restart', action='store_true')
    args = parser.parse_args()
    if args.action in ('activate', 'recover'):
        if not args.allow_service_restart:
            parser.error('This operation invalidates active sessions; --allow-service-restart is required')
        import account_certificate_transaction as transaction
        report = transaction.activate(args.candidate_id) if args.action == 'activate' else transaction.recover()
        code = 0
    elif args.action == 'status':
        if not 1 <= args.warning_days <= 365:
            parser.error('--warning-days must be between 1 and 365')
        report = status(args.warning_days)
        code = 2 if report['issues'] else 1 if report['warnings'] else 0
    else:
        report = stage(args.days) if args.action == 'stage' else inspect_candidate(candidate_path(args.candidate_id))
        code = 0
    print(json.dumps(report, indent=2))
    return code


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f'Account certificate operation failed: {error}', file=sys.stderr)
        raise SystemExit(2)
