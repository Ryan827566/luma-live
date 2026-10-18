"""Local launch must reject an unavailable service and isolate inherited settings."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import start_local_lumalive as launcher


class LocalLaunchTests(unittest.TestCase):
    def test_foreign_endpoint_and_test_tls_do_not_leak_into_local_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            foreign = base / 'remote-client.conf'
            foreign.write_text('preserve remote deployment', encoding='utf-8')
            inherited = {'LUMALIVE_ACCOUNT_SETTINGS_PATH': str(foreign), 'LUMALIVE_AUTH_ENV': 'test',
                         'LUMALIVE_AUTH_TLS_CA_FILE': 'wrong-ca.pem', 'LUMALIVE_AUTH_TLS_LIBRARY_DIR': 'wrong-libs',
                         'LUMALIVE_AUTH_DATABASE_URL': 'private-test-database', 'LUMALIVE_UI_LANGUAGE': 'zh-CN'}
            with patch.object(launcher.service, 'BASE', base), patch.dict(os.environ, inherited, clear=True):
                env = launcher.client_environment()
                self.assertEqual(env['LUMALIVE_AUTH_ENV'], 'production')
                self.assertEqual(env['LUMALIVE_UI_LANGUAGE'], 'zh-CN')
                self.assertEqual([key for key in env if key.startswith('LUMALIVE_AUTH_')], ['LUMALIVE_AUTH_ENV'])
                self.assertEqual(Path(env['LUMALIVE_ACCOUNT_SETTINGS_PATH']).read_text(),
                                 'LumaLiveAccountConnection 1\nhost=127.0.0.1\nport=9100\n')
                self.assertEqual(foreign.read_text(), 'preserve remote deployment')
                self.assertEqual(os.environ['LUMALIVE_AUTH_ENV'], 'test')
                self.assertEqual(launcher.client_environment(), env)

    def test_not_ready_never_opens_the_client(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'luma_studio.exe').touch()
            with patch.object(launcher.service, 'BASE', base), patch.object(launcher.service, 'BIN', base), \
                 patch.object(launcher.service, 'start'), patch.object(launcher.service, 'ready', return_value=False), \
                 patch.object(launcher.subprocess, 'Popen') as launch, patch('sys.argv', ['start_local_lumalive.py']):
                with self.assertRaisesRegex(RuntimeError, 'not ready'):
                    launcher.main()
                launch.assert_not_called()

    def test_immediate_client_failure_is_reported_to_the_command_script(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'luma_studio.exe').touch()
            with patch.object(launcher.service, 'BASE', base), patch.object(launcher.service, 'BIN', base), \
                 patch.object(launcher.service, 'start'), patch.object(launcher.service, 'ready', return_value=True), \
                 patch.object(launcher.subprocess, 'Popen') as launch, patch('sys.argv', ['start_local_lumalive.py']):
                launch.return_value.wait.return_value = 0xc0000135
                with self.assertRaisesRegex(RuntimeError, 'Client exited during startup'):
                    launcher.main()


if __name__ == '__main__':
    unittest.main()
