"""Native WinHTTP health checks against real local HTTP servers; no provider calls."""
import os
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import gateway


class ResponseFixture(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.server.requests.append(self.path)
        self.send_response(self.server.code)
        if self.server.code == 302:
            self.send_header('Location', f'http://127.0.0.1:{self.server.server_port}/redirected')
        for name, value in self.server.headers:
            self.send_header(name, value)
        self.send_header('Content-Length', '0')
        self.end_headers()


def main(executable):
    local = gateway.GatewayServer(gateway.Config(), port=0)
    other = ThreadingHTTPServer(('127.0.0.1', 0), ResponseFixture)
    other.requests = []
    other.code = 200
    other.headers = []
    threads = []
    env = {key.upper(): value for key, value in os.environ.items()}
    def probe(port, state):
        env['LUMALIVE_AI_GATEWAY_PORT'] = str(port)
        subprocess.run([executable, '--probe', state], env=env, timeout=10, check=True)
    try:
        for server in (local, other):
            thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': .01}, daemon=True)
            thread.start()
            threads.append(thread)
        probe(local.server_port, 'configuration-required')
        # This address is never contacted: a health check is not provider validation.
        local.config = gateway.Config('https://example.invalid/v1', 'fixture-key', 'asr', 'chat')
        probe(local.server_port, 'ready')
        local.config = gateway.Config(settings_issue='invalid_saved_settings')
        probe(local.server_port, 'configuration-required')
        probe(other.server_port, 'invalid-response')
        other.headers = [('X-Luma-AI-Protocol', '2'), ('X-Luma-AI-Health', 'ready')]
        probe(other.server_port, 'invalid-response')
        other.headers = [('X-Luma-AI-Protocol', '1'), ('X-Luma-AI-Health', 'ready'),
                         ('X-Luma-AI-Health', 'configuration-required')]
        probe(other.server_port, 'invalid-response')
        other.headers = [('X-Luma-AI-Protocol', '1'), ('X-Luma-AI-Health', 'ready')]
        other.code = 302
        probe(other.server_port, 'invalid-response')
        assert '/redirected' not in other.requests, 'Native health followed an HTTP redirect'
        other.code = 503
        probe(other.server_port, 'invalid-response')
        for value in ('bad', '0', '65536', '-1'):
            probe(value, 'invalid-response')
    finally:
        for server in (local, other):
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(2)
    probe(local.server_port, 'unavailable')
    print('PASS: native health protocol, configuration transitions, redirect rejection and offline recovery')


if __name__ == '__main__':
    main(sys.argv[1])
