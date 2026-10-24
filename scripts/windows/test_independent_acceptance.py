"""Independent synthetic fault tests. Does not run client, build, or service."""
import argparse
import ast
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

PARSER = argparse.ArgumentParser()
PARSER.add_argument('--verify', required=True, type=Path)
PARSER.add_argument('--diagnostics', required=True, type=Path)
PARSER.add_argument('--original', required=True, type=Path)
PARSER.add_argument('--result', required=True, type=Path)
PARSER.add_argument('--churn', type=Path)
PARSER.add_argument('--original-churn', type=Path)
ARGS = PARSER.parse_args()
def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
V = load(ARGS.verify, 'independent_verify')
D = load(ARGS.diagnostics, 'independent_diagnostics')

class ReportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='independent-', dir=Path(__file__).parent)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.r = V.VerificationReport(self.root, self.root / 'build', self.root / 'bin')
        self.path = self.root / 'output/client-verification.json'

    def test_old_bytes_archive_and_unique_logs(self):
        for old in (b'', b'{"status":"passed"}\r\n', b'\xffinvalid\x00'):
            with self.subTest(old=old):
                self.path.parent.mkdir(exist_ok=True)
                self.path.write_bytes(old)
                report = V.VerificationReport(self.root, self.root / 'build', self.root / 'bin')
                report.start()
                new = json.loads(self.path.read_text())
                self.assertEqual(new['status'], 'running')
                self.assertEqual(Path(new['previous_report']).read_bytes(), old)
        report.run('probe', [sys.executable, '-c', 'print("previous evidence")'], 5, os.environ.copy())
        previous_log = Path(report.record['checks'][0]['log'])
        previous_bytes = previous_log.read_bytes()
        self.r.start()
        self.r.run('probe', [sys.executable, '-c', 'print("current evidence")'], 5, os.environ.copy())
        self.assertNotEqual(previous_log, Path(self.r.record['checks'][0]['log']))
        self.assertEqual(previous_log.read_bytes(), previous_bytes)

    def test_atomic_replace_failure_preserves_bytes_and_cleans_temp(self):
        self.r.start()
        prior = self.path.read_bytes()
        with patch.object(V.os, 'replace', side_effect=PermissionError('injected')):
            with self.assertRaises(PermissionError):
                self.r.finish('passed')
        self.assertEqual(prior, self.path.read_bytes())
        self.assertFalse(list(self.path.parent.glob('*.tmp')))

    def test_serialization_failure_preserves_bytes(self):
        self.r.start()
        prior = self.path.read_bytes()
        self.r.record['bad'] = object()
        with self.assertRaises(TypeError):
            self.r.save()
        self.assertEqual(prior, self.path.read_bytes())

    def test_archive_failure_never_runs_checks(self):
        self.path.parent.mkdir()
        old = b'{"status":"passed"}'
        self.path.write_bytes(old)
        with patch.object(V, '__file__', str(self.root / 'scripts/windows/verify_client.py')), \
             patch.object(V, 'atomic_write', side_effect=PermissionError('injected')), \
             patch.object(V.subprocess, 'run') as run:
            self.assertEqual(V.main([]), 1)
            run.assert_not_called()
        self.assertEqual(old, self.path.read_bytes())

    def test_preflight_marks_current_failure(self):
        self.path.parent.mkdir()
        self.path.write_text('{"status":"passed"}')
        with patch.object(V, '__file__', str(self.root / 'scripts/windows/verify_client.py')):
            self.assertEqual(V.main([]), 1)
        current = json.loads(self.path.read_text())
        self.assertEqual(current['status'], 'failed')
        self.assertEqual(current['checks'], [])

    def setup_main(self):
        build = self.root / 'build/d-drive'
        binary = self.root / 'output/next/Release'
        build.mkdir(parents=True)
        binary.mkdir(parents=True)
        (build / 'CMakeCache.txt').write_text('CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE:PATH=' + str(binary))

    def test_main_keyboardinterrupt_records_active_check(self):
        self.setup_main()
        with patch.object(V, '__file__', str(self.root / 'scripts/windows/verify_client.py')), \
             patch.object(V.subprocess, 'run', side_effect=KeyboardInterrupt):
            self.assertEqual(V.main([]), 130)
        current = json.loads(self.path.read_text())
        self.assertEqual(current['status'], 'interrupted')
        self.assertEqual(current['checks'][0]['status'], 'interrupted')
        self.assertIn('finished_utc', current)

    def test_start_failure_nonzero_timeout_keep_prior_checks(self):
        self.r.start()
        self.r.run('completed', [sys.executable, '-c', 'pass'], 5, os.environ.copy())
        cases = [('missing', [str(self.root / 'does-not-exist.exe')], 1, OSError),
                 ('nonzero', [sys.executable, '-c', 'raise SystemExit(7)'], 5, RuntimeError),
                 ('timeout', [sys.executable, '-c', 'import time; time.sleep(30)'], .1, subprocess.TimeoutExpired)]
        for name, command, timeout, error in cases:
            with self.subTest(name=name), self.assertRaises(error):
                self.r.run(name, command, timeout, os.environ.copy())
            saved = json.loads(self.path.read_text())
            self.assertEqual(saved['checks'][0]['status'], 'passed')
            self.assertEqual(saved['checks'][-1]['status'], 'failed')
            self.assertIn('finished_utc', saved['checks'][-1])
        self.assertEqual(self.r.record['checks'][2]['exit_code'], 7)

    def test_auth_environment_does_not_use_production_database(self):
        self.r.start()
        with patch.object(V.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as run:
            self.r.run('auth-integration', ['dummy'], 1, {'LUMALIVE_AUTH_DATABASE_URL': 'private', 'OTHER': 'ok'})
        env = run.call_args.kwargs['env']
        self.assertNotIn('LUMALIVE_AUTH_DATABASE_URL', env)
        self.assertEqual(env['LUMALIVE_AUTH_ENV'], 'test')
        self.assertEqual(env['LUMALIVE_AUTH_TLS_CA_FILE'], '')

    def test_27_check_commands_and_timeouts_unchanged(self):
        def calls(path):
            tree = ast.parse(path.read_text(encoding='utf-8-sig'))
            return [ast.dump(n, include_attributes=False) for n in ast.walk(tree)
                    if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'run'
                    and n.args and isinstance(n.args[0], ast.Constant)]
        old, new = calls(ARGS.original), calls(ARGS.verify)
        self.assertEqual(len(old), 27)
        self.assertEqual(old, new)

class DiagnosticTests(unittest.TestCase):
    def test_module_mapping_does_not_expose_paths_or_absolute_address(self):
        self.assertEqual(D.locate(0x1010, [(0x1000, 0x100, 'ntdll.dll')]),
                         {'start_module': 'ntdll.dll', 'start_rva': '0x10'})
        self.assertTrue(D.locate(0x1100, [(0x1000, 0x100, 'ntdll.dll')])['start_unmapped'])

    def test_thread_handle_closed_on_owner_change(self):
        with patch.object(D, 'module_ranges', return_value=([], None)), \
             patch.object(D.kernel, 'OpenThread', return_value=123) as opened, \
             patch.object(D.kernel, 'GetProcessIdOfThread', return_value=456), \
             patch.object(D.kernel, 'CloseHandle') as closed, \
             patch.object(D, 'query_start') as query:
            result = D.thread_origins(999, [88])
            self.assertFalse(result['complete'])
            self.assertTrue(result['threads'][0]['owner_changed'])
            opened.assert_called_once_with(0x40, False, 88)
            closed.assert_called_once_with(123)
            query.assert_not_called()

    def test_thread_open_failure_is_incomplete(self):
        with patch.object(D, 'module_ranges', return_value=([], None)), \
             patch.object(D.kernel, 'OpenThread', return_value=0), \
             patch.object(D.kernel, 'CloseHandle') as closed:
            result = D.thread_origins(999, [88])
            self.assertFalse(result['complete'])
            self.assertIn('open_error', result['threads'][0])
            closed.assert_not_called()

    def test_query_missing_and_times_failure_are_unknown(self):
        with patch.object(D, 'module_ranges', return_value=([], 5)), \
             patch.object(D.kernel, 'OpenThread', return_value=123), \
             patch.object(D.kernel, 'GetProcessIdOfThread', return_value=999), \
             patch.object(D.kernel, 'GetThreadTimes', return_value=0), \
             patch.object(D.kernel, 'CloseHandle') as closed, \
             patch.object(D, 'query_start', None):
            result = D.thread_origins(999, [88])
            self.assertFalse(result['complete'])
            self.assertEqual(result['module_snapshot_error'], 5)
            self.assertIn('times_error', result['threads'][0])
            self.assertNotIn('start_module', result['threads'][0])
            closed.assert_called_once_with(123)

    def test_snapshot_retry_bounded_and_failure_not_success(self):
        invalid = ctypes.c_void_p(-1).value
        with patch.object(D.kernel, 'CreateToolhelp32Snapshot', return_value=invalid) as create, \
             patch.object(D.c, 'get_last_error', return_value=24), \
             patch.object(D.kernel, 'CloseHandle') as closed:
            rows, error = D.module_ranges(999)
            self.assertEqual((rows, error), ([], 24))
            self.assertEqual(create.call_count, 3)
            closed.assert_not_called()

    def test_module_snapshot_closes_on_exception(self):
        with patch.object(D.kernel, 'CreateToolhelp32Snapshot', return_value=123), \
             patch.object(D.kernel, 'Module32FirstW', side_effect=OSError('injected')), \
             patch.object(D.kernel, 'CloseHandle') as closed:
            with self.assertRaises(OSError):
                D.module_ranges(999)
            closed.assert_called_once_with(123)

    def test_native_current_thread_readonly_and_no_handle_leak(self):
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetCurrentProcess.restype = ctypes.wintypes.HANDLE
        kernel.GetProcessHandleCount.argtypes = [ctypes.wintypes.HANDLE, ctypes.POINTER(ctypes.wintypes.DWORD)]
        def count():
            value = ctypes.wintypes.DWORD()
            self.assertTrue(kernel.GetProcessHandleCount(kernel.GetCurrentProcess(), ctypes.byref(value)))
            return value.value
        tid = kernel.GetCurrentThreadId()
        D.thread_origins(os.getpid(), [tid])
        before = count()
        for _ in range(20):
            result = D.thread_origins(os.getpid(), [tid])
            self.assertEqual(result['threads'][0]['thread_id'], tid)
            self.assertNotIn('owner_changed', result['threads'][0])
            self.assertIn('created_filetime', result['threads'][0])
        self.assertEqual(before, count())

@unittest.skipUnless(ARGS.churn and ARGS.original_churn, 'churn candidate paths not supplied')
class ChurnIntegrationTests(unittest.TestCase):
    def functions(self, names, namespace):
        tree = ast.parse(ARGS.churn.read_text(encoding='utf-8-sig'))
        nodes = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in names]
        exec(compile(ast.Module(body=nodes, type_ignores=[]), str(ARGS.churn), 'exec'), namespace)
        return namespace

    def test_gate_logic_and_duration_constants_unchanged(self):
        old = ast.parse(ARGS.original_churn.read_text(encoding='utf-8-sig'))
        new = ast.parse(ARGS.churn.read_text(encoding='utf-8-sig'))
        for name in ('soak', 'soak_interval', 'workload', 'stop_during_churn', 'reuse', 'churn'):
            old_fn = next(n for n in old.body if isinstance(n, ast.FunctionDef) and n.name == name)
            new_fn = next(n for n in new.body if isinstance(n, ast.FunctionDef) and n.name == name)
            self.assertEqual(ast.dump(old_fn), ast.dump(new_fn), name)
        def constants(tree):
            return [ast.dump(n) for n in tree.body if isinstance(n, ast.Assign)
                    and any(isinstance(v, ast.Name) and (v.id.startswith('SOAK_') or v.id in ('BATCHES','PER_BATCH','WARMUP'))
                            for target in n.targets for v in ast.walk(target))]
        self.assertEqual(constants(old), constants(new))

    def test_three_count_samples_precede_single_annotation(self):
        events = []
        def sample(process, *, diagnose):
            events.append(('sample', diagnose))
            return {'handles': 100, 'live_threads': 3}
        def require(value, reason):
            if not value:
                raise AssertionError(reason)
        namespace = self.functions({'settled'}, {'time': types.SimpleNamespace(sleep=lambda t: events.append(('sleep', t))),
                                                'sample': sample, 'require': require,
                                                'annotate_threads': lambda p, r: events.append(('annotate', r.copy()))})
        result = namespace['settled'](object())
        self.assertEqual(result, {'handles':100, 'live_threads':3})
        self.assertEqual(events[:-1], [('sleep',.5), ('sample',False), ('sleep',.1),
                                      ('sample',False), ('sleep',.1), ('sample',False), ('sleep',.1)])
        self.assertEqual(events[-1][0], 'annotate')

    def test_annotation_failure_retains_counts_and_hides_sensitive_exception(self):
        namespace = self.functions({'annotate_threads'}, {'time': types.SimpleNamespace(monotonic=lambda: 2.0)})
        module = types.SimpleNamespace(thread_origins=lambda *a: (_ for _ in ()).throw(OSError('secret path/address')))
        reading = {'handles':100, 'live_threads':3, 'thread_ids':[1,2,3], '_sampled_at':1.0}
        with patch.dict(sys.modules, {'account_thread_diagnostics':module}):
            namespace['annotate_threads'](types.SimpleNamespace(thread_diagnostics=True, pid=123), reading)
        self.assertEqual(reading['handles'], 100)
        self.assertEqual(reading['live_threads'], 3)
        self.assertFalse(reading['thread_origins']['complete'])
        self.assertEqual(reading['thread_origins']['error_type'], 'OSError')
        self.assertNotIn('secret', json.dumps(reading))
        self.assertNotIn('_sampled_at', reading)

    def test_diagnostics_disabled_does_not_add_fields(self):
        namespace = self.functions({'annotate_threads'}, {})
        reading = {'handles':100, 'live_threads':3}
        namespace['annotate_threads'](types.SimpleNamespace(thread_diagnostics=False), reading)
        self.assertEqual(reading, {'handles':100, 'live_threads':3})

if __name__ == '__main__':
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (ARGS.verify, ARGS.diagnostics, ARGS.original, ARGS.churn, ARGS.original_churn) if p}
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    payload = {'scope': 'independent synthetic report faults, mock API faults, native current-process readonly diagnostic API; no service/GUI/build',
               'file_sha256': hashes, 'tests_run': result.testsRun,
               'failures': [(str(t), e) for t,e in result.failures],
               'errors': [(str(t), e) for t,e in result.errors],
               'skipped': result.skipped, 'passed': result.wasSuccessful()}
    ARGS.result.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
