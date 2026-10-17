"""Manage LumaLive's private local PostgreSQL + TLS account service on Windows.

Does not install a system CA, expose a network port, or print credentials.
The background supervisor owns only the child process it started.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import getpass
from urllib.parse import urlsplit
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import bootstrap_local_postgres as pg

ROOT=pg.ROOT
BASE=ROOT/'output/services/account'
CERTS=BASE/'tls'
BIN=ROOT/'output/next/Release'
SERVER=BIN/'luma_auth_server.exe'
STATE=BASE/'process.json'
PROVIDERS=BASE/'providers.dpapi'
PORT=9100
NAME='Local\\LumaLive.Account.'+hashlib.sha256(str(BASE.resolve()).lower().encode()).hexdigest()[:20]
kernel=c.WinDLL('kernel32',use_last_error=True)
for name,args,result in [
 ('CreateMutexW',[c.c_void_p,w.BOOL,w.LPCWSTR],w.HANDLE),
 ('CreateEventW',[c.c_void_p,w.BOOL,w.BOOL,w.LPCWSTR],w.HANDLE),
 ('OpenEventW',[w.DWORD,w.BOOL,w.LPCWSTR],w.HANDLE),
 ('SetEvent',[w.HANDLE],w.BOOL),('CloseHandle',[w.HANDLE],w.BOOL),
 ('WaitForSingleObject',[w.HANDLE,w.DWORD],w.DWORD),
 ('CreateJobObjectW',[c.c_void_p,w.LPCWSTR],w.HANDLE),
 ('SetInformationJobObject',[w.HANDLE,c.c_int,c.c_void_p,w.DWORD],w.BOOL),
 ('AssignProcessToJobObject',[w.HANDLE,w.HANDLE],w.BOOL)]:
 fn=getattr(kernel,name);fn.argtypes=args;fn.restype=result


def private_directory():
    if not BASE.resolve().is_relative_to(ROOT.resolve()/'output/services'):
        raise RuntimeError('Account directory is outside this workspace')
    BASE.mkdir(parents=True,exist_ok=True)
    sid=pg.current_sid()
    pg.run(['icacls',BASE,'/inheritance:r','/grant:r',f'*{sid}:(OI)(CI)F','*S-1-5-18:(OI)(CI)F'])


def prepare():
    private_directory()
    if not SERVER.is_file():raise RuntimeError('Build luma_auth_server first')
    pg.run([sys.executable,ROOT/'scripts/windows/bootstrap_local_postgres.py','--start'],timeout=90)
    openssl=pg.RUNTIME/'bin/openssl.exe'
    if not openssl.is_file():openssl=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Git/usr/bin/openssl.exe'
    if not openssl.is_file():raise RuntimeError('OpenSSL command-line tool required to provision or validate local certificates')
    required=['ca.pem','ca.key','server.pem','server.key']
    if CERTS.exists():
        if any(not (CERTS/name).is_file() for name in required):
            raise RuntimeError('Incomplete TLS directory; refusing to replace existing keys')
    else:
        with tempfile.TemporaryDirectory(prefix='tls-new-',dir=BASE) as temporary:
            directory=Path(temporary)
            pg.run([openssl,'req','-x509','-newkey','rsa:3072','-nodes','-days','1825',
                '-subj','/CN=LumaLive Local Account CA','-addext','basicConstraints=critical,CA:TRUE',
                '-addext','keyUsage=critical,keyCertSign,cRLSign','-keyout',directory/'ca.key','-out',directory/'ca.pem'])
            pg.run([openssl,'req','-new','-newkey','rsa:3072','-nodes','-subj','/CN=LumaLive Local Account',
                '-keyout',directory/'server.key','-out',directory/'server.csr'])
            extension=directory/'server.ext'
            extension.write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n'
                'extendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.1\n',encoding='ascii')
            pg.run([openssl,'x509','-req','-in',directory/'server.csr','-CA',directory/'ca.pem',
                '-CAkey',directory/'ca.key','-CAcreateserial','-days','365','-sha256',
                '-extfile',extension,'-out',directory/'server.pem'])
            CERTS.mkdir()
            for name in required:shutil.copy2(directory/name,CERTS/name)
    pg.run([openssl,'verify','-CAfile',CERTS/'ca.pem','-verify_ip','127.0.0.1',CERTS/'server.pem'])
    pg.run([openssl,'x509','-checkend','86400','-noout','-in',CERTS/'server.pem'])
    # Pin trust to this application, never to the Windows global trust store.
    for source,destination in [(CERTS/'ca.pem',BIN/'lumalive-auth-ca.pem')]+[
            (pg.RUNTIME/'bin'/name,BIN/name) for name in ('libssl-3-x64.dll','libcrypto-3-x64.dll')]:
        if destination.exists():
            if source.read_bytes()!=destination.read_bytes():
                raise RuntimeError(f'Existing packaged {destination.name} differs; refusing silent replacement')
        else:shutil.copy2(source,destination)
    if not PROVIDERS.exists():
        temporary=PROVIDERS.with_suffix('.new')
        temporary.write_bytes(pg.dpapi(json.dumps({'email':{'mode':'disabled'},'sms':{'mode':'disabled'}}).encode()))
        os.replace(temporary,PROVIDERS)


def provider_environment():
    config=json.loads(pg.dpapi(PROVIDERS.read_bytes(),decrypt=True))
    if set(config)!={'email','sms'}:raise RuntimeError('Invalid delivery configuration')
    result={}
    for channel,values in config.items():
        if not isinstance(values,dict) or values.get('mode') not in ('disabled','webhook'):
            raise RuntimeError('Delivery mode must be disabled or webhook')
        prefix='LUMALIVE_'+channel.upper()
        result[prefix+'_PROVIDER']=values['mode']
        if values['mode']=='webhook':
            for key in ('url','token'):
                value=values.get(key)
                if not isinstance(value,str) or not value or any(x in value for x in '\r\n'):
                    raise RuntimeError('Invalid delivery gateway configuration')
                result[prefix+'_WEBHOOK_'+key.upper()]=value
    return result


def service_environment():
    values={k:v for k,v in pg.connection_environment().items() if not k.startswith('LUMALIVE_EMAIL_') and not k.startswith('LUMALIVE_SMS_') and not k.startswith('LUMALIVE_AUTH_TLS_')}
    values.update(LUMALIVE_AUTH_ENV='production',LUMALIVE_AUTH_BIND_ADDRESS='127.0.0.1',
        LUMALIVE_AUTH_LIBPQ_PATH=str(pg.RUNTIME/'bin/libpq.dll'),
        LUMALIVE_AUTH_TLS_LIBRARY_DIR=str(pg.RUNTIME/'bin'),
        LUMALIVE_AUTH_TLS_CERT_FILE=str(CERTS/'server.pem'),LUMALIVE_AUTH_TLS_KEY_FILE=str(CERTS/'server.key'))
    values.update(provider_environment())
    return values


def ready():
    if not (CERTS/'ca.pem').exists():return False
    try:
        context=ssl.create_default_context(cafile=str(CERTS/'ca.pem'))
        with socket.create_connection(('127.0.0.1',PORT),timeout=1) as raw:
            with context.wrap_socket(raw,server_hostname='127.0.0.1') as stream:
                stream.sendall(b'13|\n')
                return stream.recv(256).startswith(b'14|')
    except (OSError,ssl.SSLError):return False


def status():
    return {'ready':ready(),'address':'127.0.0.1','port':PORT,'transport':'TLS',
        'database':pg.DATABASE,'database_port':pg.PORT,'server':str(SERVER),
        'log':str(BASE/'service.log'),'delivery':'See protected configuration; disabled until explicitly configured',
        'windows_service_installed':False}


def child_job(process):
    class Basic(c.Structure):
        _fields_=[('process_time',c.c_longlong),('job_time',c.c_longlong),('flags',w.DWORD),
            ('minimum',c.c_size_t),('maximum',c.c_size_t),('active',w.DWORD),
            ('affinity',c.c_size_t),('priority',w.DWORD),('scheduling',w.DWORD)]
    class Extended(c.Structure):
        _fields_=[('basic',Basic),('io',c.c_ulonglong*6),('process_memory',c.c_size_t),
            ('job_memory',c.c_size_t),('peak_process',c.c_size_t),('peak_job',c.c_size_t)]
    job=kernel.CreateJobObjectW(None,None);limits=Extended();limits.basic.flags=0x2000
    if not job or not kernel.SetInformationJobObject(job,9,c.byref(limits),c.sizeof(limits)) or not kernel.AssignProcessToJobObject(job,w.HANDLE(int(process._handle))):
        if job:kernel.CloseHandle(job)
        process.terminate();process.wait(timeout=10)
        raise RuntimeError('Unable to own the account service process')
    return job


def run_supervisor():
    mutex=kernel.CreateMutexW(None,True,NAME+'.owner')
    if not mutex or c.get_last_error()==183:
        if mutex:kernel.CloseHandle(mutex)
        raise RuntimeError('An account supervisor already owns this deployment')
    event=kernel.CreateEventW(None,True,False,NAME+'.stop')
    if not event:kernel.CloseHandle(mutex);raise RuntimeError('Unable to create service control event')
    process=None;job=None
    try:
        with (BASE/'service.log').open('ab',buffering=0) as log:
            process=subprocess.Popen([str(SERVER),str(PORT)],stdin=subprocess.PIPE,stdout=log,stderr=log,
                env=service_environment(),cwd=BIN,creationflags=subprocess.CREATE_NO_WINDOW)
            job=child_job(process)
            pg.write_json(STATE,{'supervisor_pid':os.getpid(),'server_pid':process.pid,'port':PORT})
            while process.poll() is None and kernel.WaitForSingleObject(event,500)==258:pass
            if process.poll() is None:
                try:process.communicate(b'\n',timeout=15)
                except subprocess.TimeoutExpired:kernel.CloseHandle(job);job=None;process.wait(timeout=10)
            if process.returncode:raise RuntimeError('Account server stopped with an error; see private service.log')
    finally:
        if job:kernel.CloseHandle(job)
        kernel.CloseHandle(event);kernel.CloseHandle(mutex)
        if STATE.exists():
            try:
                if json.loads(STATE.read_text())['supervisor_pid']==os.getpid():STATE.unlink()
            except (OSError,ValueError,KeyError):pass


def start():
    prepare()
    if ready():return
    with (BASE/'supervisor.log').open('ab',buffering=0) as log:
        process=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'run'],cwd=ROOT,
            stdin=subprocess.DEVNULL,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    until=time.monotonic()+25
    while time.monotonic()<until:
        if ready():return
        if process.poll() is not None:raise RuntimeError('Account supervisor could not start; inspect private logs')
        time.sleep(.2)
    raise RuntimeError('Account service did not become ready within25 seconds')


def stop():
    event=kernel.OpenEventW(2,False,NAME+'.stop')
    if not event:
        if ready():raise RuntimeError('Service is reachable but is not owned by this supervisor')
        return
    try:
        if not kernel.SetEvent(event):raise RuntimeError('Unable to request service shutdown')
    finally:kernel.CloseHandle(event)
    until=time.monotonic()+20
    while STATE.exists() and time.monotonic()<until:time.sleep(.1)
    if STATE.exists():raise RuntimeError('Service shutdown is pending; no unrelated process was killed')


def configure_client():
    path=Path(os.environ['LOCALAPPDATA'])/'LumaLive/account-connection.conf'
    expected=f'LumaLiveAccountConnection 1\nhost=127.0.0.1\nport={PORT}\n'
    if path.exists() and path.read_text(encoding='utf-8')!=expected:
        raise RuntimeError('Existing client endpoint differs; preserve it and configure Connection after sign-in or via deployment settings')
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists():
        temporary=path.with_suffix('.new');temporary.write_text(expected,encoding='utf-8');os.replace(temporary,path)



def configure_provider(channel,mode,url):
    private_directory()
    config=json.loads(pg.dpapi(PROVIDERS.read_bytes(),decrypt=True)) if PROVIDERS.exists() else {'email':{'mode':'disabled'},'sms':{'mode':'disabled'}}
    value={'mode':mode}
    if mode=='webhook':
        parsed=urlsplit(url or '')
        if parsed.scheme!='https' or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or len(url)>2048:
            raise RuntimeError('Use an HTTPS endpoint without credentials, query or fragment')
        token=getpass.getpass('Gateway bearer token (hidden): ')
        if not token or len(token)>4096 or any(ord(x)<33 or ord(x)>126 for x in token):raise RuntimeError('Invalid gateway token')
        value.update(url=url,token=token)
    config[channel]=value
    temporary=PROVIDERS.with_suffix('.new');temporary.write_bytes(pg.dpapi(json.dumps(config).encode()));os.replace(temporary,PROVIDERS)
    print('Delivery configuration saved with DPAPI. Restart the account service to apply it.')


def install_startup():
    pythonw=Path(sys.executable).with_name('pythonw.exe')
    if not pythonw.is_file():raise RuntimeError('pythonw.exe is required for background startup')
    target=Path(os.environ['APPDATA'])/'Microsoft/Windows/Start Menu/Programs/Startup/LumaLiveAccount.vbs'
    command=f'"{pythonw}" "{Path(__file__).resolve()}" start'
    script="' LumaLive managed account startup\n"+'CreateObject("WScript.Shell").Run "'+command.replace('"','""')+'", 0, False\n'
    if target.exists() and not target.read_text(encoding='utf-8').startswith("' LumaLive managed account startup"):
        raise RuntimeError('Existing startup file is not managed by LumaLive')
    target.parent.mkdir(parents=True,exist_ok=True);target.write_text(script,encoding='utf-8')
    print('Installed current-user sign-in startup; no system service or global certificate was installed.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['status','start','stop','restart','run','configure-client','configure-provider','install-startup'])
    parser.add_argument("--channel",choices=["email","sms"])
    parser.add_argument("--mode",choices=["disabled","webhook"])
    parser.add_argument("--url")
    args=parser.parse_args()
    if args.action=='configure-provider':
        if not args.channel or not args.mode:parser.error('--channel and --mode are required')
        configure_provider(args.channel,args.mode,args.url);return
    if args.action=='install-startup':install_startup();return
    if args.action=='restart':stop();start()
    if args.action=='run':run_supervisor();return
    if args.action=='start':start()
    if args.action=='stop':stop()
    if args.action=='configure-client':configure_client()
    print(json.dumps(status(),indent=2))


if __name__=='__main__':
    try:main()
    except Exception as error:
        print(f'Account deployment failed: {error}',file=sys.stderr);raise SystemExit(1)
