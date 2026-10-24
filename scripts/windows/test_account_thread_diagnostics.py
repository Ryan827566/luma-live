"""Focused Windows diagnostics tests. Never launches or contacts an auth service.

Run: python -B work/test_account_thread_diagnostics.py -v
The real Win32 tests inspect only this Python process and its controlled thread.
The churn script is imported with an inert base dependency; CLI propagation and
gates use fakes, not a production service or a shortened substitute soak.
"""
import ctypes as c
from ctypes import wintypes as w
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import types
import unittest
from unittest.mock import Mock, patch

import account_thread_diagnostics as diag


HERE = Path(__file__).resolve().parent


def require(condition, code):
    if not condition:
        raise AssertionError(code)


base = types.ModuleType('test_account_reliability')
base.ROOT = HERE
base.ownership = types.SimpleNamespace()
base.tls = types.SimpleNamespace()
base.require = require
base.save = lambda path, value: None
spec = importlib.util.spec_from_file_location('churn_under_test', HERE / 'test_account_connection_churn.py')
churn = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, test_account_reliability=base):
    spec.loader.exec_module(churn)


kernel = diag.kernel
kernel.GetCurrentProcess.argtypes, kernel.GetCurrentProcess.restype = [], w.HANDLE
kernel.GetProcessHandleCount.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
kernel.GetProcessHandleCount.restype = w.BOOL


def handle_count():
    count = w.DWORD()
    require(kernel.GetProcessHandleCount(kernel.GetCurrentProcess(), c.byref(count)), 'handle_count_failed')
    return count.value


class Win32OriginsTests(unittest.TestCase):
    def setUp(self):
        self.ready = threading.Event()
        self.stop = threading.Event()
        def worker():
            self.ready.set()
            self.stop.wait(10)
        self.worker = threading.Thread(target=worker)
        self.worker.start()
        self.assertTrue(self.ready.wait(3))
        self.tid = self.worker.native_id

    def tearDown(self):
        self.stop.set()
        self.worker.join(3)
        self.assertFalse(self.worker.is_alive())

    def test_real_module_mapping_lifecycle_and_no_handle_growth(self):
        self.assertEqual(c.sizeof(diag.ModuleEntry), 1080 if c.sizeof(c.c_void_p) == 8 else 1064)
        retained = kernel.OpenThread(0x40, False, self.tid)
        self.assertTrue(retained)
        try:
            result = diag.thread_origins(os.getpid(), [self.tid])
            self.assertTrue(result['complete'], result)
            row = result['threads'][0]
            self.assertGreater(row['created_filetime'], 0)
            self.assertEqual(row['exited_filetime'], 0)
            modules, error = diag.module_ranges(os.getpid())
            self.assertIsNone(error)
            self.assertTrue(any(name == row['start_module'] and 0 <= int(row['start_rva'], 16) < size
                                for _, size, name in modules))
            self.assertNotIn('\\', json.dumps(result))
            self.assertNotIn('start_address', row)
            before = handle_count()
            for _ in range(80):
                self.assertTrue(diag.thread_origins(os.getpid(), [self.tid])['complete'])
            self.assertEqual(handle_count(), before)
            self.stop.set()
            self.worker.join(3)
            self.assertFalse(self.worker.is_alive())
            ended = diag.thread_origins(os.getpid(), [self.tid])
            self.assertFalse(ended['complete'])
            self.assertEqual(ended['threads'][0]['created_filetime'], row['created_filetime'])
            self.assertGreater(ended['threads'][0]['exited_filetime'], 0)
            print('Real owned-thread origin:', row['start_module'], row['start_rva'],
                  '; 80 snapshots: no handle growth; lifecycle verified')
        finally:
            self.assertTrue(kernel.CloseHandle(retained))

    def test_real_owner_rejection_without_querying_another_process(self):
        # Deliberately mismatched expected PID, but the only opened thread is ours.
        with patch.object(diag, 'module_ranges', return_value=([], None)), \
                patch.object(diag, 'query_start', side_effect=AssertionError('must not query')) as query:
            before = handle_count()
            for _ in range(20):
                result = diag.thread_origins(os.getpid() + 1, [self.tid])
                self.assertFalse(result['complete'])
                self.assertTrue(result['threads'][0]['owner_changed'])
            self.assertEqual(handle_count(), before)
            query.assert_not_called()

    def test_real_thread_handle_closed_when_query_raises(self):
        diag.thread_origins(os.getpid(), [self.tid])  # Warm up module enumeration.
        with patch.object(diag, 'query_start', side_effect=RuntimeError('C:\\secret\\0x1234')):
            before = handle_count()
            for _ in range(20):
                result = diag.thread_origins(os.getpid(), [self.tid])
                self.assertFalse(result['complete'])
                self.assertEqual(result['threads'][0]['error_type'], 'RuntimeError')
                self.assertNotIn('secret', json.dumps(result))
            self.assertEqual(handle_count(), before)

    def test_real_snapshot_handle_closed_on_enumeration_exception(self):
        with patch.object(kernel, 'Module32FirstW', side_effect=OSError('sensitive')):
            before = handle_count()
            for _ in range(20):
                result = diag.thread_origins(os.getpid(), [self.tid])
                self.assertFalse(result['complete'])
                self.assertEqual(result['module_error_type'], 'OSError')
            self.assertEqual(handle_count(), before)

    def test_missing_optional_query_and_invalid_thread_are_incomplete(self):
        with patch.object(diag, 'query_start', None):
            result = diag.thread_origins(os.getpid(), [self.tid])
            self.assertFalse(result['complete'])
            self.assertTrue(result['threads'][0]['start_query_unavailable'])
        result = diag.thread_origins(os.getpid(), [0])
        self.assertFalse(result['complete'])
        self.assertIn('open_error', result['threads'][0])


class SamplingAndGateTests(unittest.TestCase):
    def test_three_original_samples_precede_single_diagnostic(self):
        events = []
        process = types.SimpleNamespace(pid=123, thread_diagnostics=True)
        def count(*_, diagnose):
            self.assertFalse(diagnose)
            events.append('sample')
            return dict(handles=100, live_threads=3, thread_ids=[len(events)], _sampled_at=0)
        def origins(pid, tids):
            events.append(('origins', pid, tids))
            return dict(complete=True, threads=[])
        with patch.object(churn, 'sample', side_effect=count), \
                patch.object(churn.time, 'sleep', side_effect=lambda seconds: events.append(seconds)), \
                patch.object(diag, 'thread_origins', side_effect=origins):
            result = churn.settled(process)
        self.assertEqual(events, [.5, 'sample', .1, 'sample', .1, 'sample', .1,
                                  ('origins', 123, [6])])
        self.assertNotIn('_sampled_at', result)
        self.assertEqual((result['handles'], result['live_threads']), (100, 3))
        self.assertIn('count_snapshot_age_ms', result['thread_origins'])

    def test_unsettled_counts_still_fail_before_diagnostics(self):
        readings = [dict(handles=100, live_threads=n) for n in (3, 5, 5)]
        with patch.object(churn, 'sample', side_effect=readings), \
                patch.object(churn.time, 'sleep'), patch.object(diag, 'thread_origins') as origins:
            with self.assertRaisesRegex(AssertionError, '^resource_counts_not_settled$'):
                churn.settled(types.SimpleNamespace(thread_diagnostics=True))
            origins.assert_not_called()

    def test_diagnostic_failure_or_import_failure_does_not_fail_sample(self):
        process = types.SimpleNamespace(pid=123, thread_diagnostics=True)
        for kind in ('query', 'import'):
            reading = dict(handles=100, live_threads=3, thread_ids=[123], _sampled_at=0)
            context = (patch.object(diag, 'thread_origins', side_effect=RuntimeError('secret path'))
                       if kind == 'query' else patch.dict(sys.modules, account_thread_diagnostics=None))
            with context:
                churn.annotate_threads(process, reading)
            self.assertEqual((reading['handles'], reading['live_threads']), (100, 3))
            self.assertFalse(reading['thread_origins']['complete'])
            self.assertNotIn('secret', json.dumps(reading))

    def test_real_count_and_diagnostic_snapshot_of_only_own_process(self):
        stop, ready = threading.Event(), threading.Event()
        worker = threading.Thread(target=lambda: (ready.set(), stop.wait(5)))
        worker.start()
        self.assertTrue(ready.wait(2))
        process = types.SimpleNamespace(pid=os.getpid(), _handle=kernel.GetCurrentProcess(),
                                        poll=lambda: None, thread_diagnostics=True)
        try:
            reading = churn.sample(process)
            self.assertIn(worker.native_id, reading['thread_ids'])
            self.assertEqual(len(reading['thread_ids']), reading['live_threads'])
            self.assertEqual([r['thread_id'] for r in reading['thread_origins']['threads']],
                             reading['thread_ids'])
            self.assertNotIn('_sampled_at', reading)
        finally:
            stop.set()
            worker.join(3)
        self.assertFalse(worker.is_alive())

    def test_original_soak_gates_reject_thread_growth_and_handle_growth(self):
        # Exercise actual soak control flow with fake I/O and a single failing
        # checkpoint. Do not run a short soak and claim long-soak acceptance.
        process = types.SimpleNamespace()
        user = {'email': 'fake'}
        for threads, handles, expected in ((5, 100, 'soak_thread_bound_exceeded'),
                                           (3, 109, 'soak_handle_bound_exceeded')):
            readings = [dict(handles=100, live_threads=3), dict(handles=handles, live_threads=threads)]
            client = Mock()
            fake_base = types.SimpleNamespace(Client=Mock(return_value=client), register=Mock(return_value=user),
                                             authenticated_cycle=Mock(), expect=Mock(return_value=['invalid_session']))
            result = {}
            with patch.object(churn, 'base', fake_base), patch.object(churn, 'wait_marker'), \
                    patch.object(churn, 'live_login'), patch.object(churn, 'assert_sessions'), \
                    patch.object(churn, 'sample', return_value=dict(handles=100, live_threads=3)), \
                    patch.object(churn, 'settled', side_effect=readings), \
                    patch.object(churn, 'soak_interval'), patch.object(churn.time, 'sleep'), \
                    patch.object(churn, 'save') as save:
                with self.assertRaisesRegex(AssertionError, '^' + expected + '$'):
                    churn.soak(process, 1, None, None, result, HERE)
            self.assertEqual(result['soak_intervals'][0]['silent_resources']['live_threads'], threads)
            self.assertNotIn('soak_passed', result)
            save.assert_called_once()
        self.assertEqual((churn.SOAK_INTERVALS, churn.SOAK_SECONDS, churn.SOAK_WATCHDOG,
                          churn.SOAK_QUIET_WARMUP_SECONDS), (10, 30, 420, 35))

    def test_controller_propagates_opt_in_cli_to_actor(self):
        for enabled in (False, True):
            with tempfile.TemporaryDirectory(prefix='thread-diagnostic-cli-', dir=HERE) as temp:
                root = Path(temp)
                fixture = root / 'fixture.exe'
                fixture.write_bytes(b'not executable; Popen is mocked')
                directory = root / 'account-churn-tests-test'
                directory.mkdir()
                (directory / 'result.json').write_text('{"passed": true}')
                args = types.SimpleNamespace(expect='bounded', scenario='soak', thread_diagnostics=enabled,
                                             fixture=fixture, report=root / 'report.json')
                process = Mock(returncode=0, poll=Mock(return_value=0))
                owner = types.SimpleNamespace(child_job=Mock(return_value=123), terminate_startup=Mock(),
                                              kernel=types.SimpleNamespace(CloseHandle=Mock()))
                tls = types.SimpleNamespace(pg=types.SimpleNamespace(current_sid=Mock(return_value='test'), run=Mock()))
                with patch.object(churn, 'checked_directory', side_effect=Path), \
                        patch.object(churn.tempfile, 'mkdtemp', return_value=str(directory)), \
                        patch.object(churn.subprocess, 'Popen', return_value=process) as popen, \
                        patch.object(churn, 'ownership', owner), patch.object(churn, 'tls', tls), \
                        patch('builtins.print'):
                    self.assertEqual(churn.main(args), 0)
                argv = popen.call_args.args[0]
                self.assertEqual(argv.count('--thread-diagnostics'), int(enabled))
                self.assertEqual(argv[argv.index('--scenario') + 1], 'soak')
                self.assertIn('--exercise', argv)
                process.communicate.assert_called_once_with(churn.PERMIT, timeout=420)


if __name__ == '__main__':
    unittest.main()
