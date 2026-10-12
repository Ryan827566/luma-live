import os
import tempfile
import uuid
import unittest
from pathlib import Path
from unittest.mock import patch
import http.client
import json
import threading
import subprocess
import gateway
import provider_settings as settings

PAYLOAD=b'BASE_URL=https://example.invalid/v1\nAPI_KEY=fixture-secret\nTRANSCRIBE_MODEL=asr-fixture\nCHAT_MODEL=chat-fixture\nTTS_MODEL=\nTTS_VOICE=\n'
class Tests(unittest.TestCase):
    def test_payload_validation(self):
        self.assertEqual(settings.parse_payload(PAYLOAD)['API_KEY'],'fixture-secret')
        for bad in (b'garbage',PAYLOAD+b'API_KEY=duplicate\n',PAYLOAD.replace(b'fixture-secret',b'bad\x00value'),PAYLOAD.replace(b'asr-fixture',b'\xff'),PAYLOAD+b'UNKNOWN=x\n'):
            with self.assertRaises(settings.SettingsError):settings.parse_payload(bad)
    def test_corruption_and_overrides(self):
        with patch.dict(os.environ,{},clear=True),patch('gateway.read_settings',side_effect=settings.SettingsError('invalid_saved_settings')):
            self.assertIn('invalid_saved_settings',gateway.Config.from_settings().problems())
        env={'LUMALIVE_AI_'+k:v for k,v in settings.parse_payload(PAYLOAD).items()}
        with patch.dict(os.environ,env,clear=True),patch('gateway.read_settings',side_effect=AssertionError('must not read desktop file')):
            self.assertEqual(gateway.Config.from_settings().problems(),[])
    def test_request_reload(self):
        current=[gateway.Config()]
        server=gateway.GatewayServer(current[0],port=0,config_loader=lambda:current[0])
        worker=threading.Thread(target=server.serve_forever,kwargs={'poll_interval':.01},daemon=True);worker.start()
        def health():
            conn=http.client.HTTPConnection('127.0.0.1',server.server_port,timeout=2)
            try:conn.request('GET','/health');return json.loads(conn.getresponse().read())
            finally:conn.close()
        try:
            self.assertFalse(health()['configured'])
            current[0]=gateway.Config('https://example.invalid/v1','fixture-secret','asr','chat')
            result=health();self.assertTrue(result['configured']);self.assertNotIn('fixture-secret',str(result))
            current[0]=gateway.Config(settings_issue='invalid_saved_settings')
            self.assertFalse(health()['configured'])
        finally:server.shutdown();server.server_close();worker.join()
    @unittest.skipUnless(os.name=='nt','Windows DPAPI interoperability')
    def test_native_writer_interoperability(self):
        exe=Path(__file__).resolve().parents[4]/'output/next/Release/luma_ai_provider_settings_tests.exe'
        if not exe.is_file():self.fail('Build native settings target before interoperability test')
        directory=Path(tempfile.gettempdir())/('luma-provider-'+uuid.uuid4().hex)
        directory.mkdir()  # Inherit the approved directory ACL; mkdtemp mode 0700 breaks restricted-token child access.
        file=directory/'settings.dat'
        try:
            subprocess.run([str(exe),'--write-fixture',str(file)],check=True)
            values=settings.read_settings(file);self.assertEqual(values['API_KEY'],'fixture-secret');self.assertEqual(values['CHAT_MODEL'],'chat-fixture')
            with patch.dict(os.environ,{},clear=True):self.assertEqual(gateway.Config.from_settings(file).problems(),[])
        finally:
            file.unlink(missing_ok=True)
            directory.rmdir()
if __name__=='__main__':unittest.main()
