"""Independent short fault tests of observer identity and handle ownership."""
import importlib.util
from pathlib import Path
import types
import unittest
from unittest.mock import Mock, patch

SOURCE = Path(__file__).with_name('observe_account_thread_lifecycle.py')
spec = importlib.util.spec_from_file_location('qa_observer', SOURCE)
O = importlib.util.module_from_spec(spec)
spec.loader.exec_module(O)

def require(ok, reason):
    if not ok:
        raise AssertionError(reason)

class IdentityTests(unittest.TestCase):
    def setUp(self):
        O.base = types.SimpleNamespace(require=require)
        self.kernel = Mock()
        self.kernel.OpenThread.return_value = 41
        self.kernel.GetProcessIdOfThread.return_value = 99
        self.kernel.CloseHandle.return_value = True
        O.diag = types.SimpleNamespace(kernel=self.kernel)
        self.lifetimes = O.Lifetimes(99, 100)
        self.row = {'thread_id': 12, 'created_filetime': 200, 'start_module': 'ntdll.dll', 'start_rva': '0x123'}
        self.reading = {'thread_origins': {'threads': [self.row]}}

    def test_wrong_pid_is_not_retained_and_handle_closes(self):
        self.kernel.GetProcessIdOfThread.return_value = 98
        with patch.object(O, 'times', return_value=[200,0,0,0]):
            self.lifetimes.observe(self.reading, 1)
        self.assertFalse(self.lifetimes.held)
        self.kernel.CloseHandle.assert_called_once_with(41)
        self.assertEqual(self.lifetimes.rows[0]['retain_error_type'], 'AssertionError')

    def test_recycled_tid_is_not_retained_and_handle_closes(self):
        with patch.object(O, 'times', return_value=[201,0,0,0]):
            self.lifetimes.observe(self.reading, 1)
        self.assertFalse(self.lifetimes.held)
        self.kernel.CloseHandle.assert_called_once_with(41)

    def test_stable_identity_retained_once_and_exit_records_observed_stage(self):
        with patch.object(O, 'times', return_value=[200,0,1,2]):
            self.lifetimes.observe(self.reading, 1)
            self.lifetimes.observe(self.reading, 2)
        self.kernel.OpenThread.assert_called_once_with(0x40, False, 12)
        self.assertEqual(len(self.lifetimes.rows), 1)
        with patch.object(O, 'times', return_value=[200,300,4,5]):
            self.lifetimes.refresh(3, 'before_stop')
        self.assertEqual(self.lifetimes.rows[0]['exit_observed_stage'], 'before_stop')
        self.assertEqual(self.lifetimes.rows[0]['exit_filetime'], 300)
        self.assertTrue(self.lifetimes.close())
        self.assertFalse(self.lifetimes.held)
        self.kernel.CloseHandle.assert_called_once_with(41)

    def test_handle_close_failure_is_reported(self):
        with patch.object(O, 'times', return_value=[200,0,0,0]):
            self.lifetimes.observe(self.reading, 1)
        self.kernel.CloseHandle.return_value = False
        self.assertFalse(self.lifetimes.close())

if __name__ == '__main__':
    unittest.main(verbosity=2)
