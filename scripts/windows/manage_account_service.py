"""Manage LumaLive's private local PostgreSQL + TLS account service on Windows.

Does not install a system CA, expose a network port, or print credentials.
The background supervisor owns only the child process it started.
"""
import argparse
from contextlib import contextmanager
import ctypes as c
from ctypes import wintypes as w
import errno
import hashlib
import getpass
from urllib.parse import urlsplit
import json
import math
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import uuid
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
 ('OpenMutexW',[w.DWORD,w.BOOL,w.LPCWSTR],w.HANDLE),
 ('ReleaseMutex',[w.HANDLE],w.BOOL),
 ('CreateEventW',[c.c_void_p,w.BOOL,w.BOOL,w.LPCWSTR],w.HANDLE),
 ('OpenEventW',[w.DWORD,w.BOOL,w.LPCWSTR],w.HANDLE),
 ('SetEvent',[w.HANDLE],w.BOOL),('CloseHandle',[w.HANDLE],w.BOOL),
 ('WaitForSingleObject',[w.HANDLE,w.DWORD],w.DWORD),
 ('CreateJobObjectW',[c.c_void_p,w.LPCWSTR],w.HANDLE),
 ('OpenJobObjectW',[w.DWORD,w.BOOL,w.LPCWSTR],w.HANDLE),
 ('GetCurrentProcess',[],w.HANDLE),
 ('IsProcessInJob',[w.HANDLE,w.HANDLE,c.POINTER(w.BOOL)],w.BOOL),
 ('TerminateJobObject',[w.HANDLE,w.UINT],w.BOOL),
 ('QueryInformationJobObject',[w.HANDLE,c.c_int,c.c_void_p,w.DWORD,c.c_void_p],w.BOOL),
 ('SetInformationJobObject',[w.HANDLE,c.c_int,c.c_void_p,w.DWORD],w.BOOL),
 ('AssignProcessToJobObject',[w.HANDLE,w.HANDLE],w.BOOL)]:
 fn=getattr(kernel,name);fn.argtypes=args;fn.restype=result


@contextmanager
def operation_lock(timeout=30):
    """Serialize deployment commands; Windows mutex recursion supports transactions."""
    if not math.isfinite(timeout) or not 0<=timeout<=300:
        raise ValueError('Operation lock timeout must be between 0 and 300 seconds')
    mutex=kernel.CreateMutexW(None,False,NAME+'.operation')
    if not mutex:raise RuntimeError('Unable to create account operation lock')
    acquired=False
    try:
        result=kernel.WaitForSingleObject(mutex,math.ceil(timeout*1000))
        if result==258:raise RuntimeError('Another account deployment operation is still running')
        if result not in (0,0x80):raise RuntimeError('Unable to acquire account operation lock')
        acquired=True
        yield
    finally:
        if acquired:kernel.ReleaseMutex(mutex)
        kernel.CloseHandle(mutex)


def private_directory():
    if not BASE.resolve().is_relative_to(ROOT.resolve()/'output/services'):
        raise RuntimeError('Account directory is outside this workspace')
    BASE.mkdir(parents=True,exist_ok=True)
    sid=pg.current_sid()
    pg.run(['icacls',BASE,'/inheritance:r','/grant:r',f'*{sid}:(OI)(CI)F','*S-1-5-18:(OI)(CI)F'])


def prepare():
    with operation_lock():
        require_no_certificate_transaction()
        _prepare()


def require_no_certificate_transaction():
    if (BASE/'certificate-transactions/pending.json').exists():
        raise RuntimeError('Certificate recovery is pending; use manage_account_certificates.py recover --allow-service-restart')


def _prepare():
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
        'certificate_recovery_pending':(BASE/'certificate-transactions/pending.json').exists(),
        'database':pg.DATABASE,'database_port':pg.PORT,'server':str(SERVER),
        'log':str(BASE/'service.log'),'delivery':'See protected configuration; disabled until explicitly configured',
        'windows_service_installed':False}


def set_job_kill_on_close(job, enabled):
    class Basic(c.Structure):
        _fields_=[('process_time',c.c_longlong),('job_time',c.c_longlong),('flags',w.DWORD),
            ('minimum',c.c_size_t),('maximum',c.c_size_t),('active',w.DWORD),
            ('affinity',c.c_size_t),('priority',w.DWORD),('scheduling',w.DWORD)]
    class Extended(c.Structure):
        _fields_=[('basic',Basic),('io',c.c_ulonglong*6),('process_memory',c.c_size_t),
            ('job_memory',c.c_size_t),('peak_process',c.c_size_t),('peak_job',c.c_size_t)]
    limits=Extended();limits.basic.flags=0x2000 if enabled else 0
    if not kernel.SetInformationJobObject(job,9,c.byref(limits),c.sizeof(limits)):
        raise RuntimeError('Unable to configure account process ownership')


def child_job(process, name=None):
    job=kernel.CreateJobObjectW(None,name)
    try:
        if not job or (name and c.get_last_error()==183):raise RuntimeError('Account process ownership collision')
        set_job_kill_on_close(job,True)
        if not kernel.AssignProcessToJobObject(job,w.HANDLE(int(process._handle))):
            raise RuntimeError('Unable to assign account process ownership')
    except BaseException:
        if job:kernel.CloseHandle(job)
        process.terminate();process.wait(timeout=10)
        raise
    return job


def terminate_startup(job, process, timeout=10):
    # Keep the job queryable until every descendant has exited. Waiting for
    # just the worker would leave its server's asynchronous termination unproven.
    class Accounting(c.Structure):
        _fields_=[('times',c.c_longlong*4),('faults',w.DWORD),('total',w.DWORD),
                  ('active',w.DWORD),('terminated',w.DWORD)]
    if not kernel.TerminateJobObject(job,1):raise RuntimeError('Unable to cancel owned account startup')
    until=time.monotonic()+timeout
    while True:
        value=Accounting()
        if not kernel.QueryInformationJobObject(job,1,c.byref(value),c.sizeof(value),None):
            raise RuntimeError('Unable to confirm owned startup cleanup')
        if value.active==0:
            process.wait(timeout=max(.01,until-time.monotonic()))
            return
        if time.monotonic()>=until:raise RuntimeError('Owned account startup cleanup is still pending')
        time.sleep(.02)


LAUNCH_PERMIT=b'LumaLive owned launch 1\n'


def run_worker(job_name):
    prefix=NAME+'.launch.'
    suffix=job_name[len(prefix):] if job_name and job_name.startswith(prefix) else ''
    if len(suffix)!=32 or any(x not in '0123456789abcdef' for x in suffix):
        raise RuntimeError('Internal account worker requires its launch job; use start')
    if sys.stdin.isatty() or sys.stdin.buffer.readline(64)!=LAUNCH_PERMIT:
        raise RuntimeError('Internal account worker was not released by its controller')
    job=kernel.OpenJobObjectW(4,False,job_name)
    if not job:raise RuntimeError('Internal account launch job is unavailable')
    try:
        member=w.BOOL()
        if not kernel.IsProcessInJob(kernel.GetCurrentProcess(),job,c.byref(member)) or not member.value:
            raise RuntimeError('Internal account worker is outside its launch job')
    finally:kernel.CloseHandle(job)
    run_supervisor()


def run_supervisor():
    mutex=kernel.CreateMutexW(None,True,NAME+'.owner')
    if not mutex or c.get_last_error()==183:
        if mutex:kernel.CloseHandle(mutex)
        raise RuntimeError('An account supervisor already owns this deployment')
    event=kernel.CreateEventW(None,True,False,NAME+'.stop')
    if not event:
        kernel.ReleaseMutex(mutex);kernel.CloseHandle(mutex)
        raise RuntimeError('Unable to create service control event')
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
        try:
            if job:kernel.CloseHandle(job)
            if process is not None:process.wait(timeout=10)
            if STATE.exists():
                try:
                    if json.loads(STATE.read_text())['supervisor_pid']==os.getpid():STATE.unlink()
                except (OSError,ValueError,KeyError):pass
        finally:
            # Closing a mutex handle alone does not release ownership while
            # another controller still has it open. Publish stopped last.
            kernel.CloseHandle(event)
            kernel.ReleaseMutex(mutex);kernel.CloseHandle(mutex)


def start():
    with operation_lock():_start()


def supervisor_command(job_name):
    return [sys.executable,'-B',str(Path(__file__).resolve()),'_run-owned','--launch-job',job_name]


def _start(timeout=25, *, prepare_service=True):
    # Only the certificate transaction controller skips preparation, while it
    # holds operation_lock and has validated its journal and complete TLS pair.
    if prepare_service:prepare()
    if ready():return
    process=None;job=None;handed_off=False
    job_name=NAME+'.launch.'+uuid.uuid4().hex
    try:
        with (BASE/'supervisor.log').open('ab',buffering=0) as log:
            process=subprocess.Popen(supervisor_command(job_name),cwd=ROOT,
                stdin=subprocess.PIPE,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        # The worker cannot start a server until it is inside this temporary
        # subtree job. Its supervisor retains its separate server job afterward.
        job=child_job(process,job_name)
        process.stdin.write(LAUNCH_PERMIT);process.stdin.close()
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            if process.poll() is not None:raise RuntimeError('Account supervisor could not start; inspect private logs')
            try:owned=json.loads(STATE.read_text())['supervisor_pid']==process.pid
            except (OSError,ValueError,KeyError,TypeError):owned=False
            if owned and ready() and process.poll() is None:
                set_job_kill_on_close(job,False)
                handed_off=True
                return
            time.sleep(.05)
        raise RuntimeError('Account service did not become ready before the startup deadline')
    finally:
        try:
            if job and not handed_off:terminate_startup(job,process)
            elif process is not None and not job and process.poll() is None:
                # No permit was issued, so this worker cannot own a server yet.
                process.terminate();process.wait(timeout=10)
        finally:
            if job:kernel.CloseHandle(job)
            if process is not None and process.stdin is not None:process.stdin.close()


def restart():
    with operation_lock():
        require_no_certificate_transaction()
        stop()
        start()


def port_occupied():
    """Check vacancy independently of the listener's TLS trust or protocol."""
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
        try:probe.bind(('127.0.0.1',PORT))
        except OSError as error:
            if error.errno in (errno.EACCES,errno.EADDRINUSE) or getattr(error,'winerror',None) in (10013,10048):
                return True
            raise
    return False


def stop(timeout=20):
    until=time.monotonic()+timeout
    with operation_lock(timeout):_stop(until)


def _stop(until):
    # The state file is diagnostic metadata, not a process-lifetime signal.
    owner=kernel.OpenMutexW(0x100001,False,NAME+'.owner')
    if not owner:
        if c.get_last_error()!=2:raise RuntimeError('Unable to inspect account supervisor ownership')
        if port_occupied():raise RuntimeError('Account port is occupied without an owned supervisor; no process was stopped')
        return
    acquired=False
    requested=False
    try:
        while True:
            result=kernel.WaitForSingleObject(owner,0)
            if result in (0,0x80):  # Signalled or abandoned: this thread owns it now.
                acquired=True
                break
            if result!=258:raise RuntimeError('Unable to wait for account supervisor shutdown')
            if not requested:
                event=kernel.OpenEventW(2,False,NAME+'.stop')
                if event:
                    try:
                        if not kernel.SetEvent(event):raise RuntimeError('Unable to request service shutdown')
                        requested=True
                    finally:kernel.CloseHandle(event)
                elif c.get_last_error()!=2:
                    raise RuntimeError('Unable to open account supervisor shutdown event')
            if time.monotonic()>=until:
                raise RuntimeError('Service shutdown is pending; no unrelated process was killed')
            time.sleep(min(.05,max(0,until-time.monotonic())))
        while port_occupied():
            if time.monotonic()>=until:
                raise RuntimeError('Supervisor exited but the account port remains occupied; no unrelated process was killed')
            time.sleep(min(.05,max(0,until-time.monotonic())))
    finally:
        if acquired:kernel.ReleaseMutex(owner)
        kernel.CloseHandle(owner)


def configure_client():
    path=Path(os.environ['LOCALAPPDATA'])/'LumaLive/account-connection.conf'
    expected=f'LumaLiveAccountConnection 1\nhost=127.0.0.1\nport={PORT}\n'
    if path.exists() and path.read_text(encoding='utf-8')!=expected:
        raise RuntimeError('Existing client endpoint differs; preserve it and configure Connection after sign-in or via deployment settings')
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists():
        temporary=path.with_suffix('.new');temporary.write_text(expected,encoding='utf-8');os.replace(temporary,path)



def configure_provider(channel,mode,url):
    value={'mode':mode}
    if mode=='webhook':
        parsed=urlsplit(url or '')
        if parsed.scheme!='https' or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or len(url)>2048:
            raise RuntimeError('Use an HTTPS endpoint without credentials, query or fragment')
        token=getpass.getpass('Gateway bearer token (hidden): ')
        if not token or len(token)>4096 or any(ord(x)<33 or ord(x)>126 for x in token):raise RuntimeError('Invalid gateway token')
        value.update(url=url,token=token)
    # Do not hold the deployment lock while waiting for human input. Merge with
    # the latest configuration only after acquiring it, preserving the other channel.
    with operation_lock():
        private_directory()
        config=json.loads(pg.dpapi(PROVIDERS.read_bytes(),decrypt=True)) if PROVIDERS.exists() else {'email':{'mode':'disabled'},'sms':{'mode':'disabled'}}
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
    parser.add_argument('action',choices=['status','start','stop','restart','run','_run-owned','configure-client','configure-provider','install-startup'])
    parser.add_argument('--launch-job',help=argparse.SUPPRESS)
    parser.add_argument("--channel",choices=["email","sms"])
    parser.add_argument("--mode",choices=["disabled","webhook"])
    parser.add_argument("--url")
    args=parser.parse_args()
    if args.action=='configure-provider':
        if not args.channel or not args.mode:parser.error('--channel and --mode are required')
        configure_provider(args.channel,args.mode,args.url);return
    if args.action=='install-startup':install_startup();return
    if args.action=='restart':restart()
    if args.action=='run':raise RuntimeError('Direct supervisor launch is unsupported; use start')
    if args.action=='_run-owned':run_worker(args.launch_job);return
    if args.action=='start':start()
    if args.action=='stop':stop()
    if args.action=='configure-client':configure_client()
    print(json.dumps(status(),indent=2))


if __name__=='__main__':
    try:main()
    except Exception as error:
        print(f'Account deployment failed: {error}',file=sys.stderr);raise SystemExit(1)
