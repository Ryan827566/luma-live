"""Same-CA leaf activation with a write-ahead rollback journal.

Pending intent always means restore the old pair, even if the new server became
ready before the caller exited. Only the final atomic journal rename commits.
The files and operation mutex belong to the same Windows user/deployment.
"""
import ctypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import os
import uuid

import manage_account_certificates as tls
import manage_account_service as service

PAIR = ('server.pem', 'server.key')
MAX_FILE = 128 * 1024


def root():
    path = service.BASE / 'certificate-transactions'
    if (not service.BASE.resolve().is_relative_to(tls.ROOT.resolve() / 'output/services')
            or path.resolve().parent != service.BASE.resolve()
            or tls.CERTS.resolve().parent != service.BASE.resolve()
            or service.CERTS.resolve() != tls.CERTS.resolve() or service.PORT != tls.PORT):
        raise RuntimeError('Certificate transaction deployment paths disagree')
    return path


def checked_file(directory, name):
    path = directory / name
    if path.resolve().parent != directory.resolve():
        raise RuntimeError('Certificate transaction file escapes its directory')
    return path


def read_file(path):
    if not 0 < path.stat().st_size <= MAX_FILE:
        raise RuntimeError('Certificate transaction file has an invalid size')
    return path.read_bytes()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode('utf-8')


def move_durable(source, destination):
    # Same-volume NTFS rename; fsync below flushes contents first. This is not a
    # claim of recovery from disk failure or hardware ignoring write barriers.
    move = service.kernel.MoveFileExW
    move.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_ulong]
    move.restype = ctypes.c_int
    if not move(str(source), str(destination), 0x1 | 0x8):
        raise ctypes.WinError(ctypes.get_last_error())


def atomic_bytes(path, data):
    temporary = checked_file(path.parent, path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        with temporary.open('xb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        move_durable(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def binding():
    return digest(encoded({'base': str(service.BASE.resolve()).lower(),
                           'certs': str(tls.CERTS.resolve()).lower(),
                           'client_ca': str(tls.CLIENT_CA.resolve()).lower(),
                           'port': service.PORT, 'owner': service.NAME}))


def require_owner():
    owner = service.kernel.OpenMutexW(0x100001, False, service.NAME + '.owner')
    if not owner:
        raise RuntimeError('Activation requires an already running owned account service')
    try:
        state = service.kernel.WaitForSingleObject(owner, 0)
        if state in (0, 0x80):
            service.kernel.ReleaseMutex(owner)
        if state != 258:
            raise RuntimeError('Activation requires a live account supervisor')
    finally:
        service.kernel.CloseHandle(owner)


def verify_served(fingerprint):
    observed = tls.probe(tls.CLIENT_CA)
    if not observed['protocol_ready'] or observed['sha256'] != fingerprint:
        raise RuntimeError('Served account certificate or protocol does not match the transaction')


def validate_saved(directory, manifest, *, recovering=False):
    if manifest.get('format') != 1 or manifest.get('deployment') != binding():
        raise RuntimeError('Certificate transaction belongs to a different deployment or format')
    ca = checked_file(directory, 'ca.pem')
    if digest(read_file(ca)) != manifest.get('ca_bytes_sha256'):
        raise RuntimeError('Saved CA content changed')
    for active in (checked_file(tls.CERTS, 'ca.pem'), tls.CLIENT_CA):
        if read_file(active) != read_file(ca):
            raise RuntimeError('Active CA or client trust changed; refusing certificate recovery')
    for side in ('old', 'new'):
        folder = directory / side
        if folder.resolve().parent != directory.resolve():
            raise RuntimeError('Saved certificate directory escaped the transaction')
        for name in PAIR:
            if digest(read_file(checked_file(folder, name))) != manifest.get(side, {}).get(name):
                raise RuntimeError('Saved certificate/key content changed')
        # A candidate that expired while recovery was unattended must not block
        # restoring a still-valid old certificate. Never skip time for old.
        tls.validate_pair(ca, folder / 'server.pem', folder / 'server.key',
                          check_time=not (recovering and side == 'new'))
        if tls.certificate(folder / 'server.pem')['sha256'] != manifest.get(side + '_fingerprint'):
            raise RuntimeError('Saved certificate fingerprint changed')


def load_pending():
    pending = checked_file(root(), 'pending.json')
    if not pending.exists():
        return None
    record = json.loads(read_file(pending))
    identifier = record.get('transaction_id')
    if record.get('format') != 1 or not isinstance(identifier, str) or not re.fullmatch(r'[0-9a-f]{32}', identifier):
        raise RuntimeError('Invalid pending certificate transaction')
    directory = root() / identifier
    if directory.resolve().parent != root().resolve():
        raise RuntimeError('Certificate transaction directory escaped its root')
    raw = read_file(checked_file(directory, 'manifest.json'))
    if digest(raw) != record.get('manifest_sha256'):
        raise RuntimeError('Pending certificate manifest changed')
    manifest = json.loads(raw)
    validate_saved(directory, manifest, recovering=True)
    # A crash between two atomic replacements can leave one old and one new
    # file. Any other content is an external change and must not be overwritten.
    for name in PAIR:
        active = digest(read_file(checked_file(tls.CERTS, name)))
        if active not in (manifest['old'][name], manifest['new'][name]):
            raise RuntimeError('Active certificate/key changed outside the pending transaction')
    return directory, manifest


def save_intent(candidate_id):
    service.require_no_certificate_transaction()
    candidate = tls.candidate_path(candidate_id)
    checked = tls.inspect_candidate(candidate)
    old = tls.certificate(tls.CERTS / 'server.pem')
    # Keep a usable rollback target, including the normal launcher's one-day
    # safety margin. Emergency renewal after expiry needs a separate procedure.
    if old['not_after'] <= tls.time.time() + tls.DAY:
        raise RuntimeError('Old certificate needs more than one day of validity for safe rollback')
    if tls.certificate(candidate / 'server.pem')['not_after'] <= tls.time.time() + tls.DAY:
        raise RuntimeError('Candidate needs more than one day of remaining validity')
    tls.validate_pair(tls.CERTS / 'ca.pem', tls.CERTS / 'server.pem', tls.CERTS / 'server.key')
    validate_launch_inputs()
    require_owner()
    verify_served(old['sha256'])
    service.private_directory()
    root().mkdir(exist_ok=True)
    identifier = uuid.uuid4().hex
    directory = root() / identifier
    directory.mkdir()
    manifest = {'format': 1, 'deployment': binding(), 'candidate_id': candidate_id,
                'created_at': datetime.now(timezone.utc).isoformat(),
                'old_fingerprint': old['sha256'], 'new_fingerprint': checked['server']['sha256']}
    ca = read_file(checked_file(tls.CERTS, 'ca.pem'))
    atomic_bytes(directory / 'ca.pem', ca)
    manifest['ca_bytes_sha256'] = digest(ca)
    for side, source in (('old', tls.CERTS), ('new', candidate)):
        (directory / side).mkdir()
        manifest[side] = {}
        for name in PAIR:
            data = read_file(checked_file(source, name))
            atomic_bytes(directory / side / name, data)
            manifest[side][name] = digest(data)
    validate_saved(directory, manifest)
    raw = encoded(manifest)
    atomic_bytes(directory / 'manifest.json', raw)
    atomic_bytes(root() / 'pending.json', encoded({'format': 1, 'transaction_id': identifier,
                                                 'manifest_sha256': digest(raw)}))
    return directory, manifest


def validate_launch_inputs():
    # Do not persist or print this environment: it contains decrypted secrets.
    environment = service.service_environment()
    required = [service.SERVER]
    if environment.get('LUMALIVE_AUTH_LIBPQ_PATH'):
        required.append(Path(environment['LUMALIVE_AUTH_LIBPQ_PATH']))
    library_dir = environment.get('LUMALIVE_AUTH_TLS_LIBRARY_DIR')
    if library_dir:
        required.extend(Path(library_dir) / name for name in ('libssl-3-x64.dll', 'libcrypto-3-x64.dll'))
    if any(not path.is_file() for path in required):
        raise RuntimeError('Account executable or required runtime is missing')


def install_pair(directory, manifest, side):
    for name in PAIR:
        atomic_bytes(checked_file(tls.CERTS, name), read_file(directory / side / name))
    tls.validate_pair(tls.CERTS / 'ca.pem', tls.CERTS / 'server.pem', tls.CERTS / 'server.key')
    for name in PAIR:
        if digest(read_file(tls.CERTS / name)) != manifest[side][name]:
            raise RuntimeError('Installed certificate pair did not match its saved content')


def finish(directory, outcome):
    move_durable(root() / 'pending.json', checked_file(directory, outcome + '.json'))


def finalize(directory, manifest, side, outcome):
    try:
        finish(directory, outcome)
    except Exception:
        # A failed acknowledgement after the rename is not a rollback. Confirm
        # the terminal record and live service before accepting that outcome.
        if (root() / 'pending.json').exists():
            raise
        terminal = json.loads(read_file(checked_file(directory, outcome + '.json')))
        expected = {'format': 1, 'transaction_id': directory.name,
                    'manifest_sha256': digest(encoded(manifest))}
        if terminal != expected or read_file(directory / 'manifest.json') != encoded(manifest):
            raise RuntimeError('Certificate transaction completion could not be verified')
        for name in PAIR:
            if digest(read_file(checked_file(tls.CERTS, name))) != manifest[side][name]:
                raise RuntimeError('Completed certificate transaction differs from live files')
        if (digest(read_file(tls.CERTS / 'ca.pem')) != manifest['ca_bytes_sha256']
                or digest(read_file(tls.CLIENT_CA)) != manifest['ca_bytes_sha256']):
            raise RuntimeError('Completed certificate transaction trust changed')
        verify_served(manifest[side + '_fingerprint'])


def restore_pending():
    saved = load_pending()
    if saved is None:
        return {'recovered': False, 'pending': False}
    directory, manifest = saved
    validate_launch_inputs()
    service.stop()
    install_pair(directory, manifest, 'old')
    service._start(prepare_service=False)
    verify_served(manifest['old_fingerprint'])
    finalize(directory, manifest, 'old', 'rolled-back')
    return {'recovered': True, 'pending': False, 'transaction_id': directory.name,
            'server_sha256': manifest['old_fingerprint']}


def recover():
    with service.operation_lock():
        return restore_pending()


def activate(candidate_id):
    with service.operation_lock():
        directory, manifest = save_intent(candidate_id)
        try:
            # Re-read the durable journal before any lifecycle or live-file write.
            load_pending()
            service.stop()
            install_pair(directory, manifest, 'new')
            service._start(prepare_service=False)
            verify_served(manifest['new_fingerprint'])
            finalize(directory, manifest, 'new', 'committed')
        except Exception as failure:
            try:
                recovery = restore_pending()
            except Exception as recovery_failure:
                if (root() / 'pending.json').exists():
                    raise RuntimeError('Activation failed; recovery remains pending. Run recover --allow-service-restart; inspect the private transaction files if recovery fails') from recovery_failure
                raise RuntimeError('Activation failed; recovery could not be confirmed. No pending record remains; inspect the transaction record and TLS status') from recovery_failure
            if not recovery['recovered']:
                raise RuntimeError('Activation completion is uncertain; no pending rollback was found. Inspect the transaction record and TLS status') from failure
            raise RuntimeError('Activation failed; the old certificate and owned service were restored and verified') from failure
        return {'activated': True, 'transaction_id': directory.name,
                'server_sha256': manifest['new_fingerprint'], 'pending': False}
