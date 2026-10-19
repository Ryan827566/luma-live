"""Real Windows command locks and startup jobs, with isolated in-memory servers."""
from contextlib import contextmanager
import ctypes
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from unittest.mock import Mock
import uuid

import manage_account_service as service


def wait(check, timeout=5):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        if check():return
        time.sleep(.01)
    raise AssertionError('Isolated operation fixture timed out')


def ready():
    try:
        with socket.create_connection(('127.0.0.1',service.PORT),timeout=.2) as client:
            client.sendall(b'13|\n')
            response=bytearray()
            while not response.endswith(b'\n') and len(response)<256:
                part=client.recv(256-len(response))
                if not part:return False
                response.extend(part)
            return response.startswith(b'14|') and response.endswith(b'\n')
    except OSError:return False


def actor(name, directory, port, action, mode, job_name=None):
    directory=Path(directory).resolve()
    if (not name.startswith('Local\\LumaLive.OperationTest.') or
            directory.parent!=(service.ROOT/'output/services').resolve() or
            not directory.name.startswith('account-operation-tests-') or port==9100):
        raise RuntimeError('Refusing non-isolated operation fixture')
    service.NAME=name;service.BASE=directory;service.STATE=directory/'process.json';service.PORT=port
    if action=='lock':
        with service.operation_lock():
            (directory/'locked').write_text('locked',encoding='ascii')
            if mode=='abandon':os._exit(0)
            if mode=='brief':time.sleep(.3)
            else:wait(lambda:(directory/'release').exists())
        return
    if action=='probe':
        try:
            with service.operation_lock(timeout=.05):pass
        except RuntimeError:raise SystemExit(2)
        return
    service.SERVER=service.BIN/'luma_account_ui_fixture.exe'
    env={k.upper():v for k,v in os.environ.items()}
    env.update(LUMALIVE_AUTH_ENV='test',LUMALIVE_AUTH_TLS_CA_FILE='',
               LUMALIVE_AUTH_TLS_CERT_FILE='',LUMALIVE_AUTH_TLS_KEY_FILE='')
    service.service_environment=lambda:env
    service.prepare=lambda:None
    service.ready=ready
    if action=='caller':
        service.supervisor_command=lambda job:command(name,directory,port,'worker','normal',job)
        service.start()
        return
    if action!='worker':raise RuntimeError('Unknown isolated actor')
    if mode=='slow':
        (directory/'before-permit').write_text('waiting',encoding='ascii')
        time.sleep(5)
    if mode=='assignment-window':
        original=service.child_job
        def delayed(process, name=None):
            (directory/'server-created').write_text(str(process.pid),encoding='ascii')
            time.sleep(5)
            return original(process,name)
        service.child_job=delayed
    service.run_worker(job_name)


def command(name, directory, port, action, mode='normal', job=None):
    return [sys.executable,'-B',str(Path(__file__).resolve()),'--actor',name,str(directory),str(port),
            action,mode]+([job] if job else [])


class OperationTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='account-operation-tests-',dir=service.ROOT/'output/services')
        self.directory=Path(self.temporary.name)
        self.name='Local\\LumaLive.OperationTest.'+uuid.uuid4().hex
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1',0));self.port=reserve.getsockname()[1]
        self.addCleanup(self.cleanup_directory)
        for key,value in [('NAME',self.name),('BASE',self.directory),('STATE',self.directory/'process.json'),('PORT',self.port)]:
            item=patch.object(service,key,value);item.start();self.addCleanup(item.stop)

    def cleanup_directory(self):
        if self.directory.resolve().parent!=(service.ROOT/'output/services').resolve():
            raise RuntimeError('Operation fixture cleanup escaped its directory')
        self.temporary.cleanup()

    def cmd(self, action, mode='normal', job=None):
        return command(self.name,self.directory,self.port,action,mode,job)

    @contextmanager
    def lock_actor(self, mode='hold'):
        with (self.directory/'lock.log').open('wb') as log:
            process=subprocess.Popen(self.cmd('lock',mode),cwd=service.ROOT,stdout=log,stderr=log,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                wait(lambda:(self.directory/'locked').exists() or process.poll() is not None)
                self.assertTrue((self.directory/'locked').exists())
                yield process
            finally:
                (self.directory/'release').write_text('release',encoding='ascii')
                try:process.wait(timeout=3)
                except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=3)

    def probe(self):
        return subprocess.run(self.cmd('probe'),cwd=service.ROOT,capture_output=True,timeout=3,
                              creationflags=subprocess.CREATE_NO_WINDOW).returncode

    @contextmanager
    def launch_setup(self, mode='normal'):
        captured=[]
        original=subprocess.Popen
        def spawn(*args,**kwargs):
            result=original(*args,**kwargs);captured.append(result);return result
        with patch.object(service,'prepare'),patch.object(service,'ready',side_effect=ready),\
             patch.object(service,'supervisor_command',side_effect=lambda job:self.cmd('worker',mode,job)),\
             patch.object(service.subprocess,'Popen',side_effect=spawn):
            try:yield captured
            finally:
                # Only the random-name service and Popen handles of this fixture.
                if ready():service.stop(timeout=4)
                for process in captured:
                    if process.poll() is None:
                        process.terminate();process.wait(timeout=3)

    def assert_cancelled(self, captured):
        self.assertEqual(len(captured),1)
        self.assertIsNotNone(captured[0].poll())
        self.assertFalse(service.port_occupied())
        self.assertEqual(self.probe(),0)

    def start_until_phase(self, marker):
        # Synchronize the first diagnostic read with the fault window. Otherwise
        # a short timeout may merely measure Python startup on a busy machine.
        state=service.STATE
        def read_state():
            wait(lambda:(self.directory/marker).exists())
            return state.read_text()
        proxy=Mock(wraps=state)
        proxy.read_text.side_effect=read_state
        with patch.object(service,'STATE',proxy),service.operation_lock():
            service._start(timeout=.5)

    def test_recursive_lock_releases_every_acquisition_after_exception(self):
        with self.assertRaisesRegex(RuntimeError,'fixture'):
            with service.operation_lock():
                with service.operation_lock():
                    self.assertEqual(self.probe(),2)
                    raise RuntimeError('fixture')
        self.assertEqual(self.probe(),0)

    def test_prepare_waits_for_other_process_before_mutating(self):
        with self.lock_actor('brief'),patch.object(service,'_prepare') as prepare:
            started=time.monotonic();service.prepare()
            self.assertGreaterEqual(time.monotonic()-started,.2)
            prepare.assert_called_once()
        self.assertEqual(self.probe(),0)

    def test_stop_deadline_includes_operation_lock_wait(self):
        with self.lock_actor(),patch.object(service,'_stop') as stop:
            started=time.monotonic()
            with self.assertRaisesRegex(RuntimeError,'Another account'):service.stop(timeout=.15)
            self.assertLess(time.monotonic()-started,.8)
            stop.assert_not_called()

    def test_restart_holds_one_lock_across_stop_and_start(self):
        phases=[]
        def phase(label):
            phases.append(label);self.assertEqual(self.probe(),2)
        with patch.object(service,'stop',side_effect=lambda:phase('stop')),\
             patch.object(service,'start',side_effect=lambda:phase('start')):
            service.restart()
        self.assertEqual(phases,['stop','start'])
        self.assertEqual(self.probe(),0)

    def test_provider_prompt_is_unlocked_and_preserves_intervening_channel_update(self):
        path=self.directory/'providers.dpapi'
        path.write_text(json.dumps({'email':{'mode':'disabled'},'sms':{'mode':'webhook',
                        'url':'https://fixture.invalid','token':'synthetic'}}),encoding='utf-8')
        def prompt(_):
            self.assertEqual(self.probe(),0)
            service.configure_provider('sms','disabled',None)
            return 'synthetic-email-token'
        with patch.object(service,'PROVIDERS',path),patch.object(service,'private_directory'),\
             patch.object(service.pg,'dpapi',side_effect=lambda data,**kwargs:data),\
             patch.object(service.getpass,'getpass',side_effect=prompt),patch('builtins.print'):
            service.configure_provider('email','webhook','https://fixture.invalid')
        config=json.loads(path.read_text())
        self.assertEqual(config['sms'],{'mode':'disabled'})
        self.assertEqual(config['email']['mode'],'webhook')
        self.assertEqual(self.probe(),0)

    def test_abandoned_operation_lock_can_be_acquired_and_released(self):
        pinned=service.kernel.CreateMutexW(None,False,self.name+'.operation')
        self.assertTrue(pinned)
        try:
            with self.lock_actor('abandon') as process:process.wait(timeout=3)
            with service.operation_lock(timeout=.2):self.assertEqual(self.probe(),2)
            self.assertEqual(self.probe(),0)
        finally:service.kernel.CloseHandle(pinned)

    def test_lock_creation_and_wait_errors_fail_closed(self):
        with patch.object(service.kernel,'CreateMutexW',return_value=0):
            with self.assertRaisesRegex(RuntimeError,'create account operation'):
                with service.operation_lock():self.fail('entered without a lock')
        with patch.object(service.kernel,'WaitForSingleObject',return_value=0xffffffff):
            with self.assertRaisesRegex(RuntimeError,'acquire account operation'):
                with service.operation_lock():self.fail('entered without a lock')
        self.assertEqual(self.probe(),0)

    def test_direct_run_and_forged_worker_are_rejected(self):
        with patch('sys.argv',['manage_account_service.py','run']),patch.object(service,'run_supervisor') as run:
            with self.assertRaisesRegex(RuntimeError,'use start'):service.main()
            run.assert_not_called()
        fake_job=self.name+'.launch.'+uuid.uuid4().hex
        result=subprocess.run(self.cmd('worker',job=fake_job),input=service.LAUNCH_PERMIT,capture_output=True,
                              cwd=service.ROOT,timeout=3,creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(service.STATE.exists())
        self.assertFalse(service.port_occupied())
        unrelated=service.kernel.CreateJobObjectW(None,fake_job)
        self.assertTrue(unrelated)
        try:
            result=subprocess.run(self.cmd('worker',job=fake_job),input=service.LAUNCH_PERMIT,capture_output=True,
                                  cwd=service.ROOT,timeout=3,creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(service.STATE.exists())
            self.assertFalse(service.port_occupied())
        finally:service.kernel.CloseHandle(unrelated)

    def test_job_assignment_failure_cannot_release_worker(self):
        with self.launch_setup() as captured,patch.object(service,'child_job',side_effect=RuntimeError('assignment failure')):
            with self.assertRaisesRegex(RuntimeError,'assignment failure'):service.start()
            self.assert_cancelled(captured)
            self.assertFalse(service.STATE.exists())

    def test_permit_write_failure_cleans_owned_worker(self):
        with self.launch_setup() as captured,patch.object(service,'LAUNCH_PERMIT','not bytes'):
            with self.assertRaises(TypeError):service.start()
            self.assert_cancelled(captured)
            self.assertFalse(service.STATE.exists())

    def test_timeout_before_permit_read_cannot_start_later(self):
        with self.launch_setup('slow') as captured:
            with self.assertRaisesRegex(RuntimeError,'startup deadline'):
                self.start_until_phase('before-permit')
            self.assertTrue((self.directory/'before-permit').exists())
            self.assert_cancelled(captured)

    def test_timeout_in_server_job_assignment_window_kills_entire_subtree(self):
        with self.launch_setup('assignment-window') as captured:
            with self.assertRaisesRegex(RuntimeError,'startup deadline'):
                self.start_until_phase('server-created')
            self.assertTrue((self.directory/'server-created').exists())
            self.assert_cancelled(captured)
            self.assertFalse(service.STATE.exists())

    def test_disarm_failure_cleans_worker_and_server(self):
        original=service.set_job_kill_on_close
        def fail_disarm(job,enabled):
            if not enabled:raise RuntimeError('disarm failure')
            return original(job,enabled)
        with self.launch_setup() as captured,patch.object(service,'set_job_kill_on_close',side_effect=fail_disarm):
            with self.assertRaisesRegex(RuntimeError,'disarm failure'):service.start()
            self.assert_cancelled(captured)

    def test_startup_caller_exit_preserves_ready_service_until_owned_stop(self):
        try:
            result=subprocess.run(self.cmd('caller'),cwd=service.ROOT,capture_output=True,timeout=10,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
            self.assertTrue(ready(),'Service did not survive its startup caller')
            self.assertEqual(self.probe(),0)
            service.stop(timeout=4)
            self.assertFalse(service.port_occupied())
        finally:
            if ready():service.stop(timeout=4)


if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='--actor':
        actor(sys.argv[2],sys.argv[3],int(sys.argv[4]),sys.argv[5],sys.argv[6],sys.argv[7] if len(sys.argv)>7 else None)
    else:
        result=unittest.main(exit=False).result
        service.pg.write_json(service.ROOT/'output/account-operation-tests.json',{
            'passed':result.wasSuccessful(),'tests_run':result.testsRun,
            'failures':len(result.failures),'errors':len(result.errors),
            'scope':'Random Windows locks/startup jobs and isolated in-memory servers; no production lifecycle commands'})
        sys.exit(0 if result.wasSuccessful() else 1)
