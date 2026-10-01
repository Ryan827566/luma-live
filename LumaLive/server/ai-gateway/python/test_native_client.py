"""Runs the real native HTTP client against gateway + mock upstream. No external API."""
import json
import io
import wave
import os
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import gateway

class Provider(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        self.server.requests.append((self.path, body))
        if self.path == '/v1/audio/speech':
            data = json.loads(body)
            assert data['input'] == 'HTTP summary' and data['response_format'] == 'wav'
            output = io.BytesIO()
            with wave.open(output, 'wb') as wav:
                wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(24000); wav.writeframes(b'\0\0' * 2400)
            payload = output.getvalue()
            self.send_response(200); self.send_header('Content-Length', str(len(payload))); self.end_headers(); self.wfile.write(payload)
            return
        if self.path == '/v1/audio/transcriptions':
            if b'RIFF' not in body or b'audio.wav' not in body:
                self.send_error(400)
                return
            response = {'text': 'HTTP transcript'}
        else:
            data = json.loads(body)
            assert '[speaker] HTTP transcript' in data['messages'][1]['content']
            prompt = data['messages'][0]['content']
            if prompt.startswith('Translate'):
                assert 'ja' in prompt
                content = 'HTTP translation'
            elif prompt.startswith('Extract keywords'):
                content = 'HTTP keywords'
            else:
                assert prompt.startswith('Summarize')
                content = 'HTTP summary'
            response = {'choices': [{'message': {'content': content}}]}
        payload = json.dumps(response).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

def main(executable):
    upstream = ThreadingHTTPServer(('127.0.0.1', 0), Provider)
    upstream.requests = []
    config = gateway.Config(f'http://127.0.0.1:{upstream.server_port}/v1', 'local-test-key', 'asr', 'chat', 'tts', 'voice')
    local = gateway.GatewayServer(config, port=0)
    threads = []
    try:
        for server in (upstream, local):
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            threads.append(thread)
        env = {k.upper(): v for k, v in os.environ.items()}
        env['LUMALIVE_AI_GATEWAY_PORT'] = str(local.server_port)
        result = subprocess.run([executable], env=env, timeout=40)
        if result.returncode:
            return result.returncode
        assert len(upstream.requests) == 5, 'Unexpected provider calls'
        print('PASS: five real HTTP provider requests; no external service used')
        return 0
    finally:
        for server in (local, upstream):
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(2)

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
