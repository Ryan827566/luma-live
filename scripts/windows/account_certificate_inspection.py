"""Read-only, bounded diagnostics for private certificate transaction records.

Historical outcome, current disk observations and pending recovery eligibility
are separate. No service, network, launch configuration or private bytes appear
in the report. Eligibility is only a file assessment, never restart assurance.
"""
from datetime import datetime, timezone
import json
import re
import subprocess
import time

import account_certificate_transaction as tx
import manage_account_certificates as tls
import manage_account_service as service

ID = r'[0-9a-f]{32}'
HASH = r'[0-9a-f]{64}'


class DiagnosticError(Exception):
    pass


def read(path):
    with path.open('rb') as stream:
        data = stream.read(tx.MAX_FILE + 1)
    if not 0 < len(data) <= tx.MAX_FILE:
        raise DiagnosticError('record_size_invalid')
    return data


def object_from(raw):
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise DiagnosticError('record_shape_invalid')
    return value


def valid_hex(value, pattern):
    return isinstance(value, str) and re.fullmatch(pattern, value) is not None


def intent(path):
    record = object_from(read(path))
    if (set(record) != {'format', 'transaction_id', 'manifest_sha256'}
            or record['format'] != 1 or not valid_hex(record['transaction_id'], ID)
            or not valid_hex(record['manifest_sha256'], HASH)):
        raise DiagnosticError('intent_invalid')
    return record


def remaining(deadline):
    value = deadline - time.monotonic()
    if value <= 0:
        raise TimeoutError()
    return value


def inspect(identifier=None):
    report = {'checked_at': datetime.now(timezone.utc).isoformat(),
              'state': 'unavailable', 'pending': None, 'issues': [], 'warnings': [],
              'recovery_assessment': 'not_assessed', 'next_action': 'inspect_records',
              'runtime_checked': False, 'served_certificate_checked': False}
    if identifier is not None and not valid_hex(identifier, ID):
        report['issues'].append('transaction_id_invalid')
        return report
    acquired = False
    try:
        with service.operation_lock(timeout=0):
            acquired = True
            inspect_locked(identifier, report, time.monotonic() + 10)
    except DiagnosticError as error:
        # Only our fixed codes are emitted; never echo file content or OS errors.
        report['issues'].append(error.args[0])
        report['state'] = 'incomplete' if error.args[0] == 'outcome_record_missing' else 'invalid'
        report['recovery_assessment'] = 'blocked'
    except (TimeoutError, subprocess.TimeoutExpired):
        report['issues'].append('inspection_timeout')
        report['recovery_assessment'] = 'not_assessed'
        report['next_action'] = 'retry_inspection'
    except Exception as error:
        if not acquired:
            if str(error) == 'Another account deployment operation is still running':
                report.update(state='busy', next_action='retry_after_operation')
            else:
                report['issues'].append('operation_lock_unavailable')
        else:
            report['issues'].append('record_or_certificate_unreadable')
            report['state'] = 'invalid'
            report['recovery_assessment'] = 'blocked'
    return report


def inspect_locked(identifier, report, deadline):
    root = tx.root()
    pending_path = tx.checked_file(root, 'pending.json')
    report['pending'] = pending_path.exists()
    pending = intent(pending_path) if report['pending'] else None
    if pending:
        report['pending_transaction_id'] = pending['transaction_id']
    if identifier is None:
        if pending is None:
            report.update(state='no_pending', recovery_assessment='not_applicable', next_action='none')
            return
        identifier = pending['transaction_id']
    report['transaction_id'] = identifier
    directory = tx.checked_file(root, identifier)
    if not directory.is_dir():
        raise DiagnosticError('transaction_directory_missing')
    terminal = [(state, tx.checked_file(directory, name)) for state, name in
                (('committed', 'committed.json'), ('rolled_back', 'rolled-back.json'))]
    terminal = [(state, path) for state, path in terminal if path.exists()]
    selected_pending = pending is not None and pending['transaction_id'] == identifier
    if len(terminal) > 1 or selected_pending and terminal:
        raise DiagnosticError('outcome_records_conflict')
    if selected_pending:
        report['state'] = 'pending'
        record = pending
    elif terminal:
        report['state'], path = terminal[0]
        record = intent(path)
        if record['transaction_id'] != identifier:
            raise DiagnosticError('terminal_identity_mismatch')
    else:
        raise DiagnosticError('outcome_record_missing')
    raw = read(tx.checked_file(directory, 'manifest.json'))
    if tx.digest(raw) != record['manifest_sha256']:
        raise DiagnosticError('manifest_hash_mismatch')
    manifest = object_from(raw)
    if manifest.get('format') != 1 or manifest.get('deployment') != tx.binding():
        raise DiagnosticError('manifest_deployment_mismatch')
    now = time.time()
    ca = tx.checked_file(directory, 'ca.pem')
    if tx.digest(read(ca)) != manifest.get('ca_bytes_sha256'):
        raise DiagnosticError('saved_ca_hash_mismatch')
    report['certificates'] = {'ca': tls.describe(tls.certificate(ca, timeout=remaining(deadline)), now, 1)}
    for side in ('old', 'new'):
        folder = tx.checked_file(directory, side)
        saved = manifest.get(side)
        if not isinstance(saved, dict) or set(saved) != set(tx.PAIR):
            raise DiagnosticError('snapshot_manifest_invalid')
        for name in tx.PAIR:
            if not valid_hex(saved[name], HASH) or tx.digest(read(tx.checked_file(folder, name))) != saved[name]:
                raise DiagnosticError('snapshot_hash_mismatch')
        certificate = tls.certificate(folder / 'server.pem', timeout=remaining(deadline))
        if certificate['sha256'] != manifest.get(side + '_fingerprint'):
            raise DiagnosticError('snapshot_fingerprint_mismatch')
        # Terminal history remains valid after expiry or later renewals.
        tls.validate_pair(ca, folder / 'server.pem', folder / 'server.key',
                          check_time=False, timeout=remaining(deadline))
        report['certificates'][side] = tls.describe(certificate, now, 1)
    report['snapshot_integrity_valid'] = True
    observations(directory, manifest, report)
    if selected_pending:
        blockers = []
        if report['certificates']['ca']['state'] in ('expired', 'not_yet_valid'):
            blockers.append('saved_ca_not_currently_valid')
        if report['certificates']['old']['state'] in ('expired', 'not_yet_valid'):
            blockers.append('old_certificate_not_currently_valid')
        if not report['trust_matches_snapshot']:
            blockers.append('current_trust_changed_or_unreadable')
        if report['current_live_pair'] not in ('old', 'new', 'mixed'):
            blockers.append('current_pair_changed_or_unreadable')
        report['issues'].extend(blockers)
        report['recovery_assessment'] = 'blocked' if blockers else 'files_eligible'
        report['next_action'] = 'resolve_blockers_without_overwriting_files' if blockers else 'recover_in_maintenance_window'
        if report['current_live_pair'] == 'mixed':
            report['warnings'].append('partial_pair_requires_recovery')
        if report['certificates']['new']['state'] in ('expired', 'not_yet_valid'):
            report['warnings'].append('unused_candidate_not_currently_valid')
    else:
        report['recovery_assessment'] = 'not_applicable'
        report['next_action'] = 'inspect_current_pending' if pending else 'none'
        expected = 'new' if report['state'] == 'committed' else 'old'
        if report['current_live_pair'] != expected or not report['trust_matches_snapshot']:
            report['warnings'].append('historical_record_differs_from_current_deployment')
    remaining(deadline)


def observations(directory, manifest, report):
    try:
        ca = read(tx.checked_file(directory, 'ca.pem'))
        report['trust_matches_snapshot'] = (read(tx.checked_file(tls.CERTS, 'ca.pem')) == ca
                                            and read(tls.CLIENT_CA) == ca)
    except (OSError, RuntimeError, DiagnosticError):
        report['trust_matches_snapshot'] = False
    try:
        active = {name: tx.digest(read(tx.checked_file(tls.CERTS, name))) for name in tx.PAIR}
        state = ('old' if active == manifest['old'] else 'new' if active == manifest['new'] else
                 'mixed' if all(active[name] in (manifest['old'][name], manifest['new'][name]) for name in tx.PAIR)
                 else 'external')
    except (OSError, RuntimeError, DiagnosticError):
        state = 'unreadable'
    report['current_live_pair'] = state
