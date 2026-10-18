"""Start this project's local account deployment, then open its production client."""
import argparse
import os
import subprocess
import sys
import tempfile

import manage_account_service as service


def client_environment():
    # A dedicated local launcher must not inherit another deployment's endpoint,
    # TLS overrides or test mode. The user's ordinary client settings stay intact.
    config = service.BASE / 'local-client.conf'
    expected = f'LumaLiveAccountConnection 1\nhost=127.0.0.1\nport={service.PORT}\n'
    if not config.exists() or config.read_text(encoding='utf-8') != expected:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n',
                                             dir=service.BASE, prefix='local-client-', delete=False) as stream:
                temporary = stream.name
                stream.write(expected)
            os.replace(temporary, config)
        finally:
            if temporary and os.path.exists(temporary):
                os.unlink(temporary)
    env = {key.upper(): value for key, value in os.environ.items()
           if not key.upper().startswith('LUMALIVE_AUTH_')}
    env.update(LUMALIVE_AUTH_ENV='production', LUMALIVE_ACCOUNT_SETTINGS_PATH=str(config))
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server-only', action='store_true', help='Prepare and check the service without opening a client')
    args = parser.parse_args()
    binary = service.BIN / 'luma_studio.exe'
    if not binary.is_file():
        raise RuntimeError(f'Client executable is missing: {binary}')
    print('Starting PostgreSQL and the local TLS account service...', flush=True)
    service.start()
    env = client_environment()
    if not service.ready():
        raise RuntimeError('The local account service is not ready; the client was not opened')
    print(f'Account service ready: TLS 127.0.0.1:{service.PORT}; PostgreSQL {service.pg.PORT}.', flush=True)
    print('Register with a username, email and password. Then sign in with your email and password.', flush=True)
    print('Email/SMS code delivery requires a configured provider; email + password sign-in does not.', flush=True)
    if args.server_only:
        return
    process = subprocess.Popen([str(binary)], cwd=service.BIN, env=env,
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP)
    try:
        result = process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        print(f'Opened: {binary}', flush=True)
    else:
        if result:
            raise RuntimeError(f'Client exited during startup (exit code {result}): {binary}')
        print('The client was closed normally.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'LumaLive startup failed: {error}', file=sys.stderr)
        raise SystemExit(1)
