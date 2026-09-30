import http.client
import io
import json
import threading
import unittest
import wave
from email import policy
from email.parser import BytesParser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch
import gateway

class MockProvider(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        self.server.received.append((self.path, self.headers, self.rfile.read(int(self.headers['Content-Length']))))
        self.send_response(self.server.status)
        if self.server.status == 302:
            self.send_header('Location', self.server.redirect_url)
        self.send_header('Content-Length', str(len(self.server.payload)))
        self.end_headers()
        self.wfile.write(self.server.payload)

class Tests(unittest.TestCase):
    def setUp(self):
        self.upstream = ThreadingHTTPServer(('127.0.0.1', 0), MockProvider)
        self.upstream.received = []
        self.upstream.status = 200
        self.upstream.payload = b'{}'
        self.upstream.redirect_url = 'http://127.0.0.1:1/leak'
        self.config = gateway.Config(f'http://127.0.0.1:{self.upstream.server_port}/v1', 'test-secret', 'asr-model', 'chat-model')
        self.server = gateway.GatewayServer(self.config, port=0)
        self.threads = []
        for server in (self.upstream, self.server):
            thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': .01}, daemon=True)
            thread.start()
            self.threads.append(thread)

    def tearDown(self):
        for server in (self.server, self.upstream):
            server.shutdown()
            server.server_close()
        for thread in self.threads:
            thread.join(2)

    def request(self, path='/summary', body=b'meeting', headers=None, method='POST'):
        conn = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        try:
            conn.request(method, path, body, headers or {'Content-Type': 'text/plain; charset=utf-8'})
            response = conn.getresponse()
            return response.status, response.read().decode('utf-8')
        finally:
            conn.close()

    def test_multipart_transcription(self):
        buf = io.BytesIO()
        with wave.open(buf, 'wb') as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            wav.writeframes(b'\0\0' * 240000)
        audio = buf.getvalue()
        self.upstream.payload = json.dumps({'text': '你好，会议开始'}).encode()
        status, text = self.request('/transcribe', audio, {'Content-Type': 'audio/wav'})
        self.assertEqual((status, text), (200, '你好，会议开始'))
        path, headers, body = self.upstream.received[0]
        self.assertEqual(path, '/v1/audio/transcriptions')
        self.assertEqual(headers['Authorization'], 'Bearer test-secret')
        message = BytesParser(policy=policy.default).parsebytes(
            ('Content-Type: ' + headers['Content-Type'] + '\r\n\r\n').encode() + body)
        fields = {part.get_param('name', header='content-disposition'): part.get_payload(decode=True)
                  for part in message.iter_parts()}
        self.assertEqual(fields, {'model': b'asr-model', 'response_format': b'json', 'file': audio})

    def test_summary_json_and_utf8(self):
        self.upstream.payload = json.dumps({'choices': [{'message': {'content': '待办：明天提交报告'}}]}).encode()
        self.assertEqual(self.request(body='小李明天提交报告'.encode()), (200, '待办：明天提交报告'))
        path, headers, body = self.upstream.received[0]
        data = json.loads(body)
        self.assertEqual(path, '/v1/chat/completions')
        self.assertEqual(data['model'], 'chat-model')
        self.assertEqual(data['messages'][1], {'role': 'user', 'content': '小李明天提交报告'})
        self.assertFalse(data['stream'])

    def test_configuration_health_redacts_secrets(self):
        status, text = self.request('/health', method='GET')
        self.assertEqual(status, 200)
        self.assertTrue(json.loads(text)['configured'])
        self.assertNotIn('test-secret', text)
        self.server.config = gateway.Config()
        self.assertEqual(self.request()[0], 503)
        health = json.loads(self.request('/health', method='GET')[1])
        self.assertFalse(health['configured'])
        self.assertIn('missing_api_key', health['issues'])
        self.assertEqual(self.upstream.received, [])

    def test_request_validation(self):
        cases = [('/summary', b'x', {'Content-Type': 'application/json'}, 415),
                 ('/summary', b'\xff', None, 400), ('/summary', b' ', None, 400),
                 ('/summary', b'', None, 400), ('/transcribe', b'notwav', {'Content-Type': 'audio/wav'}, 400),
                 ('/summary', b'x', {'Content-Type': 'text/plain', 'Content-Length': str(gateway.TEXT_LIMIT + 1)}, 413),
                 ('/transcribe', b'x', {'Content-Type': 'audio/wav', 'Content-Length': str(gateway.AUDIO_LIMIT + 1)}, 413),
                 ('/summary', b'x', {'Content-Type': 'text/plain', 'Content-Length': '-1'}, 411),
                 ('/summary', b'x', {'Content-Type': 'text/plain', 'Transfer-Encoding': 'chunked'}, 400),
                 ('/summary', b'x', {'Content-Type': 'text/plain', 'Origin': 'https://evil.example'}, 403),
                 ('/summary', b'x', {'Content-Type': 'text/plain', 'Host': 'evil.example'}, 403),
                 ('/other', b'x', None, 404)]
        for path, body, headers, expected in cases:
            with self.subTest(path=path, expected=expected, headers=headers):
                self.assertEqual(self.request(path, body, headers)[0], expected)
        self.assertEqual(self.upstream.received, [])

    def test_upstream_failures_are_redacted(self):
        for status, payload in [(401, b'test-secret private transcript'), (302, b'redirect'),
                                (200, b'invalid json'), (200, b'{}'),
                                (200, b'x' * (gateway.UPSTREAM_LIMIT + 1))]:
            with self.subTest(status=status, payload_size=len(payload)):
                self.upstream.status, self.upstream.payload = status, payload
                code, text = self.request()
                self.assertEqual(code, 502)
                self.assertNotIn('test-secret', text)
                self.assertNotIn('private transcript', text)
        self.assertEqual(len(self.upstream.received), 5)

    def test_config_url_policy(self):
        for url in ('http://example.com/v1', 'https://user:pass@example.com/v1',
                    'https://example.com/v1?key=bad', 'file:///tmp/file', 'https://example.com:bad'):
            self.assertIn('invalid_base_url', gateway.Config(url, 'k', 'a', 'c').problems())
        for url in ('https://example.com/v1', 'http://127.0.0.1:8080/v1', 'http://localhost/v1'):
            self.assertEqual(gateway.Config(url, 'k', 'a', 'c').problems(), [])
        self.assertNotIn('test-secret', repr(self.config))
        with patch.dict('os.environ', {'LUMALIVE_AI_BASE_URL': 'https://example.com/v1',
             'LUMALIVE_AI_API_KEY': 'k', 'LUMALIVE_AI_TRANSCRIBE_MODEL': 'a', 'LUMALIVE_AI_CHAT_MODEL': 'c'}):
            self.assertEqual(gateway.Config.from_env().problems(), [])

    def test_concurrency_limit(self):
        for _ in range(4):
            self.assertTrue(self.server.slots.acquire(False))
        try:
            conn = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
            conn.connect()
            response = http.client.HTTPResponse(conn.sock)
            response.begin()
            self.assertEqual(response.status, 503)
            response.close()
            conn.close()
        finally:
            for _ in range(4):
                self.server.slots.release()

    def test_provider_timeout(self):
        with patch.object(self.server.provider.opener, 'open', side_effect=TimeoutError()):
            self.assertEqual(self.request()[0], 504)

if __name__ == '__main__':
    unittest.main()
