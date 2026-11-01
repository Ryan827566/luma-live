"""Independent bounded synthetic release checks; never runs the real client."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from unittest.mock import patch
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--evidence-dir', required=True, type=Path)
args = parser.parse_args()
WORK = args.evidence_dir.resolve()
WORK.mkdir(parents=True, exist_ok=True)
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/windows'))
import package_client_release as P
import release_launchers as L

BASE = WORK / ('synthetic-' + uuid.uuid4().hex)
BASE.mkdir()
RESULT = {'scope': 'synthetic payload packaging and real cmd.exe with synthetic .NET Win32 executable; no LumaLive execution, services, accounts or devices',
          'candidate_sha256': {name: hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()
                               for name, module in [('package_client_release.py', P), ('release_launchers.py', L)]}, 'checks': []}
MOMENT = datetime(2026, 11, 1, 5, 10, 11, tzinfo=timezone.utc)


def check(name, function):
    started = time.monotonic()
    record = {'name': name}
    try:
        function()
        record['status'] = 'passed'
    except Exception as error:
        record.update(status='failed', error_type=type(error).__name__, error=str(error))
    record['elapsed_seconds'] = round(time.monotonic() - started, 3)
    RESULT['checks'].append(record)


def fixture(name):
    root = BASE / name
    binary = root / '中文 input dir'
    binary.mkdir(parents=True)
    (binary / 'luma_studio.exe').write_bytes(b'MZsynthetic payload, never executed')
    hashes = {}
    for item in P.SOURCE_MANIFEST_FILES:
        dest = binary / 'ai-service' / item
        dest.parent.mkdir(parents=True, exist_ok=True)
        data = ('synthetic fixture ' + item).encode()
        dest.write_bytes(data)
        hashes[item] = hashlib.sha256(data).hexdigest()
    (binary / 'ai-service/manifest.json').write_text(json.dumps({
        'python_version': P.PYTHON_VERSION, 'source': P.PYTHON_SOURCE,
        'archive_sha256': P.PYTHON_ARCHIVE_SHA256, 'files': hashes,
        'private_extra': 'CANARY_SECRET_DO_NOT_SHIP'}), encoding='utf-8')
    return binary, root / '中文 output dir'


def content_and_manifest():
    binary, output = fixture('content')
    for name in ['.env', 'private.key', 'account.conf', 'luma_auth_server.exe', 'luma_account_ui_fixture.exe', 'libpq.dll', 'private.db', 'service.log', 'providers.dpapi']:
        (binary / name).write_bytes(b'CANARY_SECRET_DO_NOT_SHIP')
    (binary / 'ai-service/private.json').write_bytes(b'CANARY_SECRET_DO_NOT_SHIP')
    dest = P.package_release(binary, output, now=MOMENT)
    assert dest.name == '2026-11-01_13-10-11'
    manifest = json.loads((dest / 'manifest.json').read_text(encoding='utf-8'))
    files = {p.relative_to(dest).as_posix() for p in dest.rglob('*') if p.is_file()}
    assert files == set(manifest['files']) | {'manifest.json'}
    assert {p for p in files if not p.startswith('ai-service/')} == {'luma_studio.exe', 'Start-LumaLive.cmd', 'README.md', 'manifest.json'}
    assert {p for p in files if p.endswith('.exe')} == {'luma_studio.exe', 'ai-service/runtime/python.exe'}
    for relative, meta in manifest['files'].items():
        data = (dest / relative).read_bytes()
        assert hashlib.sha256(data).hexdigest() == meta['sha256'] and len(data) == meta['size_bytes']
        assert b'CANARY_SECRET_DO_NOT_SHIP' not in data
    assert manifest['build']['status'] == manifest['acceptance']['status'] == 'unverified'
    assert manifest['git_commit'] is None
    assert manifest['source_program']['sha256'] == hashlib.sha256((binary / 'luma_studio.exe').read_bytes()).hexdigest()
    before = {p: hashlib.sha256((dest / p).read_bytes()).hexdigest() for p in files}
    try:
        P.package_release(binary, output, now=MOMENT)
    except P.PackagingError:
        pass
    else:
        raise AssertionError('Timestamp collision succeeded')
    assert before == {p: hashlib.sha256((dest / p).read_bytes()).hexdigest() for p in files}


def failure_cleanup():
    binary, output = fixture('failure')
    output.mkdir()
    sentinel = output / 'existing.txt'
    sentinel.write_bytes(b'keep')
    with patch.object(P, 'write_launchers', side_effect=OSError('injected write failure')):
        try:
            P.package_release(binary, output, now=MOMENT)
        except OSError:
            pass
        else:
            raise AssertionError('Injected failure succeeded')
    assert list(output.iterdir()) == [sentinel] and sentinel.read_bytes() == b'keep'


def source_corruption():
    binary, output = fixture('corruption')
    (binary / 'ai-service/runtime/python.exe').write_bytes(b'corrupted')
    try:
        P.package_release(binary, output, now=MOMENT)
    except P.PackagingError:
        pass
    else:
        raise AssertionError('Corrupt runtime packaged')
    assert not output.exists()


def collision_at_commit():
    binary, output = fixture('race')
    original = Path.rename
    def race(source, destination):
        if source.name.startswith('.incomplete-release-'):
            destination.mkdir()
            (destination / 'untouched.txt').write_bytes(b'existing concurrent release')
        return original(source, destination)
    with patch.object(Path, 'rename', race):
        try:
            P.package_release(binary, output, now=MOMENT)
        except OSError:
            pass
        else:
            raise AssertionError('Concurrent destination overwritten')
    final = output / '2026-11-01_13-10-11'
    assert (final / 'untouched.txt').read_bytes() == b'existing concurrent release'
    assert list(output.iterdir()) == [final]


def cli_failure():
    env = os.environ.copy()
    env['PYTHONPATH'] = str(Path(L.__file__).parent)
    output = BASE / 'cli failure output'
    result = subprocess.run([sys.executable, '-B', P.__file__, '--binary-dir', str(BASE / 'missing'), '--output-root', str(output)],
                            env=env, capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode != 0 and b'Packaged (' not in result.stdout and not output.exists()


def actual_cmd_synthetic_executable():
    package = BASE / '启动 中文 package'
    package.mkdir()
    caller = BASE / 'unrelated caller'
    caller.mkdir()
    L.write_launchers(package)
    source = BASE / 'LauncherProbe.cs'
    source.write_text('using System; using System.IO; using System.Diagnostics; class Probe { static int Main() { File.WriteAllLines(Environment.GetEnvironmentVariable("QA_OUT"), new string[] {Environment.CurrentDirectory, Process.GetCurrentProcess().MainModule.FileName}); return int.Parse(Environment.GetEnvironmentVariable("QA_EXIT")); }}', encoding='utf-8')
    compiler = r'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    compile_result = subprocess.run([compiler, '/nologo', '/target:winexe', '/out:' + str(package / 'luma_studio.exe'), str(source)],
                                   capture_output=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
    assert compile_result.returncode == 0, compile_result.stdout.decode(errors='replace')
    shutil.copyfile(package / 'luma_studio.exe', caller / 'luma_studio.exe')
    env = os.environ.copy()
    env.update(LUMALIVE_START_NONINTERACTIVE='1', QA_OUT=str(BASE / 'probe.txt'), QA_EXIT='0')
    env['PATH'] = str(caller) + os.pathsep + env['PATH']
    command = '"' + env.get('COMSPEC', r'C:\Windows\System32\cmd.exe') + '" /d /s /c ""' + str(package / 'Start-LumaLive.cmd') + '""'
    for expected in [0, 37]:
        env['QA_EXIT'] = str(expected)
        result = subprocess.run(command, cwd=caller, env=env, capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        assert result.returncode == expected, (expected, result.returncode, result.stdout)
        lines = Path(env['QA_OUT']).read_text(encoding='utf-8-sig').splitlines()
        assert Path(lines[0]).resolve() == package.resolve()
        assert Path(lines[1]).resolve() == (package / 'luma_studio.exe').resolve()
    (package / 'luma_studio.exe').unlink()
    result = subprocess.run(command, cwd=caller, env=env, capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode == 2


for name, function in [('payload allowlist, canary exclusion, manifest truth, collision preservation', content_and_manifest),
                       ('injected launcher failure cleans only staging', failure_cleanup),
                       ('corrupt required runtime fails closed', source_corruption),
                       ('concurrent output creation is not overwritten', collision_at_commit),
                       ('CLI missing-input returns nonzero without false success', cli_failure),
                       ('actual CMD, synthetic EXE: Chinese space path, cwd, local exe, exit codes, missing exe', actual_cmd_synthetic_executable)]:
    check(name, function)
RESULT['status'] = 'passed' if all(x['status'] == 'passed' for x in RESULT['checks']) else 'failed'
RESULT['fixture_directory'] = str(BASE)
report = WORK / ('independent-release-acceptance-' + uuid.uuid4().hex + '.json')
report.write_text(json.dumps(RESULT, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(RESULT, ensure_ascii=False, indent=2))
print('REPORT=' + str(report))
raise SystemExit(0 if RESULT['status'] == 'passed' else 1)
