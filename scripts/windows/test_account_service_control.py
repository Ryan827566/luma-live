"""Isolated Windows ownership/event/port tests. Never controls the real deployment."""
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
import uuid

import manage_account_service as service


def wait(check, timeout=5):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        if check():return
        time.sleep(.01)
    raise AssertionError('Isolated control fixture timed out')


def fixture(name, directory, port, mode):
    # Reject accidental invocation with production identity or paths.
    directory=Path(directory).resolve()
    if (not name.startswith('Local\\LumaLive.ControlTest.') or
            directory.parent!= (service.ROOT/'output/services').resolve() or
            not directory.name.startswith('account-control-tests-') or port==9100):
        raise RuntimeError('Refusing non-isolated service control fixture')
    service.NAME=name
    service.BASE=directory
    service.STATE=directory/'process.json'
    service.PORT=port
    if mode=='supervisor':
        service.SERVER=service.BIN/'luma_account_ui_fixture.exe'
        env={k.upper():v for k,v in os.environ.items()}
        env.update(LUMALIVE_AUTH_ENV='test',LUMALIVE_AUTH_TLS_CA_FILE='',
                   LUMALIVE_AUTH_TLS_CERT_FILE='',LUMALIVE_AUTH_TLS_KEY_FILE='')
        service.service_environment=lambda:env
        service.run_supervisor()
        return
    owner=service.kernel.CreateMutexW(None,True,name+'.owner')
    if not owner or ctypes.get_last_error()==183:raise RuntimeError('Fixture owner collision')
    event=None
    listener=socket.socket()
    listener.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
    listener.bind(('127.0.0.1',port));listener.listen(1)
    try:
        if mode!='missing-state':
            service.STATE.write_text(json.dumps({'supervisor_pid':os.getpid()}),encoding='utf-8')
        if mode=='early-state':service.STATE.unlink()
        (directory/'ready').write_text('ready',encoding='ascii')
        if mode=='delayed-event':time.sleep(.25)
        if mode=='no-event':
            time.sleep(5)
            return
        event=service.kernel.CreateEventW(None,True,False,name+'.stop')
        if not event:raise RuntimeError('Fixture event creation failed')
        if mode=='hang':
            time.sleep(5)
            return
        if service.kernel.WaitForSingleObject(event,5000)!=0:raise RuntimeError('Fixture stop was not requested')
        if mode=='abandon':os._exit(0)
        if mode in ('early-state','missing-state'):time.sleep(.3)
        if service.STATE.exists():service.STATE.unlink()
        if mode in ('port-delay','foreign-after-owner'):
            service.kernel.ReleaseMutex(owner)
            (directory/'owner-released').write_text('released',encoding='ascii')
            time.sleep(.3 if mode=='port-delay' else 5)
        else:
            listener.close()
            service.kernel.ReleaseMutex(owner)
    finally:
        listener.close()
        if event:service.kernel.CloseHandle(event)
        service.kernel.CloseHandle(owner)


class ServiceControlTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='account-control-tests-',dir=service.ROOT/'output/services')
        self.directory=Path(self.temporary.name)
        self.name='Local\\LumaLive.ControlTest.'+uuid.uuid4().hex
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1',0));self.port=reserve.getsockname()[1]
        self.addCleanup(self.cleanup_directory)
        for key,value in [('NAME',self.name),('STATE',self.directory/'process.json'),('PORT',self.port)]:
            item=patch.object(service,key,value)
            item.start();self.addCleanup(item.stop)

    def cleanup_directory(self):
        if self.directory.resolve().parent!=(service.ROOT/'output/services').resolve():
            raise RuntimeError('Fixture cleanup escaped its intended directory')
        self.temporary.cleanup()

    @contextmanager
    def actor(self, mode):
        with (self.directory/'actor.log').open('wb') as log:
            process=subprocess.Popen([sys.executable,'-B',str(Path(__file__).resolve()),'--fixture',
                                      self.name,str(self.directory),str(self.port),mode],
                                     cwd=service.ROOT,stdout=log,stderr=log,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            pinned=None
            try:
                marker=service.STATE if mode=='supervisor' else self.directory/'ready'
                wait(lambda:marker.exists() or process.poll() is not None)
                self.assertIsNone(process.poll(),'Control fixture failed; inspect its log')
                # Keep the object alive until cross-process ownership assertions
                # finish, regardless of how quickly the actor exits.
                pinned=service.kernel.OpenMutexW(0x100001,False,self.name+'.owner')
                self.assertTrue(pinned,'Fixture owner is unavailable')
                yield process
            finally:
                try:
                    if process.poll() is None:
                        # This is exclusively the subprocess created by this fixture.
                        process.terminate()
                    process.wait(timeout=5)
                finally:
                    if pinned:service.kernel.CloseHandle(pinned)

    def assert_owner_released(self):
        # Use a different process: recursive waits on the test thread would
        # hide leaked ownership. The actor context pins the object until done.
        code=("import ctypes,sys;from ctypes import wintypes as w;"
              "k=ctypes.WinDLL('kernel32',use_last_error=True);"
              "k.OpenMutexW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR];k.OpenMutexW.restype=w.HANDLE;"
              "k.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];"
              "k.ReleaseMutex.argtypes=[w.HANDLE];k.CloseHandle.argtypes=[w.HANDLE];"
              "h=k.OpenMutexW(0x100001,False,sys.argv[1]);"
              "r=k.WaitForSingleObject(h,0) if h else (0 if ctypes.get_last_error()==2 else 1);"
              "k.ReleaseMutex(h) if h and r in (0,128) else None;"
              "k.CloseHandle(h) if h else None;sys.exit(0 if r in (0,128) else 1)")
        result=subprocess.run([sys.executable,'-B','-c',code,self.name+'.owner'],cwd=service.ROOT,
                              capture_output=True,timeout=5,creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode,0,'stop leaked mutex ownership')

    def assert_ready_frame(self, client):
        client.sendall(b'13|\n')
        response=bytearray()
        while not response.endswith(b'\n') and len(response)<256:
            part=client.recv(256-len(response))
            self.assertTrue(part,'Fixture closed before returning a complete readiness frame')
            response.extend(part)
        self.assertTrue(response.startswith(b'14|') and response.endswith(b'\n'))

    def test_state_disappearing_does_not_complete_stop_early(self):
        with self.actor('early-state') as actor:
            self.assertFalse(service.STATE.exists())
            started=time.monotonic();service.stop(timeout=2)
            self.assertGreaterEqual(time.monotonic()-started,.25)
            self.assertFalse(service.port_occupied())
            self.assert_owner_released()
            actor.wait(timeout=2)
            self.assertEqual(actor.returncode,0)

    def test_missing_state_still_stops_the_owner(self):
        with self.actor('missing-state'):
            service.stop(timeout=2)
            self.assertFalse(service.port_occupied())
            self.assert_owner_released()

    def test_stop_waits_for_event_during_startup(self):
        with self.actor('delayed-event'):
            service.stop(timeout=2)
            self.assertFalse(service.port_occupied())
            self.assert_owner_released()

    def test_missing_event_uses_same_bounded_deadline(self):
        with self.actor('no-event') as actor:
            started=time.monotonic()
            with self.assertRaisesRegex(RuntimeError,'shutdown is pending'):service.stop(timeout=.15)
            self.assertLess(time.monotonic()-started,.8)
            self.assertIsNone(actor.poll())
            self.assertTrue(service.port_occupied())

    def test_unresponsive_owner_times_out_without_killing_it(self):
        with self.actor('hang') as actor:
            with self.assertRaisesRegex(RuntimeError,'shutdown is pending'):service.stop(timeout=.15)
            self.assertIsNone(actor.poll())
            self.assertTrue(service.port_occupied())

    def test_waits_for_port_after_owner_release(self):
        with self.actor('port-delay'):
            started=time.monotonic();service.stop(timeout=2)
            self.assertGreaterEqual(time.monotonic()-started,.25)
            self.assertFalse(service.port_occupied())
            self.assert_owner_released()

    def test_foreign_port_after_owner_release_fails_without_killing(self):
        with self.actor('foreign-after-owner') as actor:
            with self.assertRaisesRegex(RuntimeError,'port remains occupied'):service.stop(timeout=.2)
            self.assertIsNone(actor.poll())
            self.assertTrue(service.port_occupied())
            self.assert_owner_released()

    def test_abandoned_owner_is_released_and_port_is_checked(self):
        with self.actor('abandon') as actor:
            service.stop(timeout=2)
            self.assertFalse(service.port_occupied())
            self.assert_owner_released()
            actor.wait(timeout=2)

    def test_foreign_plaintext_listener_is_not_mistaken_for_stopped(self):
        with socket.socket() as listener:
            listener.bind(('127.0.0.1',self.port));listener.listen(1)
            with patch.object(service,'ready',side_effect=AssertionError('TLS is not an ownership check')):
                with self.assertRaisesRegex(RuntimeError,'without an owned supervisor'):service.stop(timeout=.15)
            self.assertTrue(service.port_occupied())

    def test_repeated_stop_preserves_stale_state_and_leaves_no_mutex(self):
        service.STATE.write_text('{"unrelated":"diagnostic"}',encoding='utf-8')
        before=service.STATE.read_bytes()
        for _ in range(3):service.stop(timeout=.15)
        self.assertEqual(service.STATE.read_bytes(),before)
        self.assert_owner_released()

    def test_permission_errors_do_not_become_success(self):
        with patch.object(service.kernel,'OpenMutexW',return_value=0),patch.object(service.c,'get_last_error',return_value=5):
            with self.assertRaisesRegex(RuntimeError,'inspect account supervisor ownership'):service.stop(timeout=.15)
        with self.actor('delayed-event'):
            with patch.object(service.kernel,'OpenEventW',return_value=0),patch.object(service.c,'get_last_error',return_value=5):
                with self.assertRaisesRegex(RuntimeError,'open account supervisor shutdown event'):service.stop(timeout=.15)

    def test_real_supervisor_job_stops_memory_fixture_even_without_state(self):
        with self.actor('supervisor') as actor:
            wait(service.port_occupied)
            # Only this fixture's metadata is removed, after its owner is live.
            service.STATE.unlink()
            service.stop(timeout=4)
            self.assertFalse(service.port_occupied())
            actor.wait(timeout=3)
            self.assertEqual(actor.returncode,0)
            self.assert_owner_released()

    def test_real_supervisor_stops_with_an_accepted_connection(self):
        with self.actor('supervisor') as actor:
            wait(service.port_occupied)
            with socket.create_connection(('127.0.0.1',self.port),timeout=2) as client:
                self.assert_ready_frame(client)
                service.stop(timeout=4)
                self.assertEqual(client.recv(256),b'')
            self.assertFalse(service.port_occupied())
            actor.wait(timeout=3)
            self.assertEqual(actor.returncode,0)

    def test_real_supervisor_stops_after_an_accepted_connection_closes(self):
        with self.actor('supervisor') as actor:
            wait(service.port_occupied)
            with socket.create_connection(('127.0.0.1',self.port),timeout=2) as client:
                self.assert_ready_frame(client)
            service.stop(timeout=4)
            self.assertFalse(service.port_occupied())
            actor.wait(timeout=3)
            self.assertEqual(actor.returncode,0)


if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='--fixture':
        fixture(sys.argv[2],sys.argv[3],int(sys.argv[4]),sys.argv[5])
    else:
        result=unittest.main(exit=False).result
        service.pg.write_json(service.ROOT/'output/account-service-control-tests.json',{
            'passed':result.wasSuccessful(),'tests_run':result.testsRun,
            'failures':len(result.failures),'errors':len(result.errors),
            'scope':'Random Windows ownership/events, dynamic ports, isolated metadata and in-memory server; no production lifecycle commands'})
        sys.exit(0 if result.wasSuccessful() else 1)
