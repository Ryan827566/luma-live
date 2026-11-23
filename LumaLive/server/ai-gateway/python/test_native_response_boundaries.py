"""Real WinHTTP framing checks against a local synthetic gateway; no provider."""
import argparse
import io
import json
import os
from pathlib import Path
import subprocess
import threading
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class LocalGateway(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_POST(self):
        self.rfile.read(int(self.headers['Content-Length']))
        self.server.routes.append(self.path)
        payload = {'/transcribe': b'HTTP transcript', '/summary': b'HTTP summary',
                   '/translate': b'HTTP translation', '/keywords': b'HTTP keywords'}.get(self.path)
        if self.path == '/speech':
            buf = io.BytesIO()
            with wave.open(buf, 'wb') as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(24000)
                wav.writeframes(b'\0\0' * 2400)
            payload = buf.getvalue()
        failing = self.path == self.server.target and not self.server.injected
        if failing:
            self.server.injected = True
            payload = b'' if self.server.mode == 'empty-truncated' else b'PARTIAL'
        self.send_response(200)
        self.send_header('Content-Type', 'audio/wav' if self.path == '/speech' else 'text/plain; charset=utf-8')
        if self.server.mode == 'chunked':
            self.send_header('Transfer-Encoding', 'chunked')
        elif self.server.mode != 'close-delimited':
            length = len(payload)
            if failing:
                length = {'oversized': 200000, 'malformed': 'invalid',
                          'huge': '18446744073709551616'}.get(self.server.mode, length + 128)
            self.send_header('Content-Length', str(length))
        self.send_header('Connection', 'close')
        self.end_headers()
        try:
            if self.server.mode == 'chunked':
                for part in (payload[:3], payload[3:]):
                    self.wfile.write(f'{len(part):x}\r\n'.encode() + part + b'\r\n')
                self.wfile.write(b'0\r\n\r\n')
            else:
                self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Rejection may close the client before the fixture finishes writing.
        self.close_connection = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    cases = [(mode, None) for mode in ('exact', 'chunked', 'close-delimited')]
    cases += [('truncated', route) for route in ('/transcribe', '/summary', '/translate', '/keywords', '/speech')]
    cases += [('empty-truncated', '/transcribe'), ('oversized', '/summary'),
              ('malformed', '/summary'), ('huge', '/summary')]
    results = []
    args.report.parent.mkdir(parents=True, exist_ok=True)
    for mode, target in cases:
        server = ThreadingHTTPServer(('127.0.0.1', 0), LocalGateway)
        server.mode, server.target, server.injected, server.routes = mode, target, False, []
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        item = {'case': mode, 'route': target, 'status': 'failed'}
        try:
            env = dict(os.environ, LUMALIVE_AI_GATEWAY_PORT=str(server.server_port))
            command = [str(args.executable.resolve())] + ([target] if target else [])
            result = subprocess.run(command, env=env, capture_output=True, timeout=25)
            # Every failure scenario must actually reach the chosen route, then retry it.
            injected_and_retried = not target or (server.injected and server.routes.count(target) == 2)
            item.update(exit_code=result.returncode, stdout=result.stdout.decode('utf-8', 'replace'),
                        stderr=result.stderr.decode('utf-8', 'replace'), routes=server.routes,
                        injected_and_retried=injected_and_retried,
                        status='passed' if result.returncode == 0 and injected_and_retried else 'failed')
        except subprocess.TimeoutExpired:
            item['error'] = 'native client timed out'
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)
            item['server_thread_joined'] = not thread.is_alive()
            if thread.is_alive():
                item['status'] = 'failed'
            results.append(item)
            args.report.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
        print(f"{item['status']}: {mode} {target or 'all routes'}", flush=True)
    return 0 if all(item['status'] == 'passed' for item in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
