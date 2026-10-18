"""Opt-in real local deployment acceptance, with optional service restart.

Creates one uniquely named test account through the actual GUI, verifies database
persistence and deletes that account through the GUI. Never uses a memory fixture.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import tempfile
import time
from workspace_auth_fixture import u,wait
import manage_account_service as service
import bootstrap_local_postgres as pg
from start_local_lumalive import client_environment


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--allow-service-restart',action='store_true')
    mode.add_argument('--keep-service-running',action='store_true',help='Verify registration and email login without restarting the service')
    args=parser.parse_args()
    binary=service.BIN/'luma_studio.exe'
    username='deploy.'+secrets.token_hex(6)
    password=secrets.token_urlsafe(24)
    email=username+'@example.invalid'
    callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    u.EnumWindows.argtypes=[callback,w.LPARAM]
    u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
    u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    def text(hwnd):
        buffer=c.create_unicode_buffer(2048);u.SendMessageW(hwnd,0xD,len(buffer),c.cast(buffer,c.c_void_p).value);return buffer.value
    def put(hwnd,value):
        buffer=c.create_unicode_buffer(value);u.SendMessageW(hwnd,0xC,0,c.cast(buffer,c.c_void_p).value)
    def query(sql):
        return pg.query(sql,pg.read_credentials()['app_password'],user=pg.APPLICATION,database=pg.DATABASE)
    service.start()
    evidence={'client_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'transport':'TLS',
              'storage':'PostgreSQL','service_restarted':args.allow_service_restart,'checks':[]}
    with tempfile.TemporaryDirectory(prefix='luma-production-ui-') as temporary:
        profile=Path(temporary)
        env=client_environment()
        env.update(LUMALIVE_UI_LANGUAGE='en-US',LOCALAPPDATA=str(profile))
        # No explicit CA/library override: validate the same adjacent package used
        # when the user double-clicks the production executable.
        for phase in ('register','relogin'):
            if phase=='relogin':
                if args.allow_service_restart:service.stop();service.start()
                assert query(f"SELECT count(*) FROM luma_auth_users WHERE username='{username}';")=='1'
                evidence['checks'].append('account retained after '+('real service restart' if args.allow_service_restart else 'client restart'))
            start=subprocess.STARTUPINFO();start.dwFlags=subprocess.STARTF_USESHOWWINDOW;start.wShowWindow=0
            process=subprocess.Popen([str(binary)],env=env,startupinfo=start)
            root=None
            try:
                def find():
                    found=[]
                    @callback
                    def visit(hwnd,_):
                        pid=w.DWORD();u.GetWindowThreadProcessId(hwnd,c.byref(pid))
                        if pid.value==process.pid and u.GetDlgItem(hwnd,129):found.append(hwnd)
                        return True
                    u.EnumWindows(visit,0);return found[0] if found else None
                root=wait(find,'Production client did not open')
                panel=wait(lambda:u.FindWindowExW(root,None,'LumaAccountPage',None),'Sign-in panel missing')
                control=lambda i:u.GetDlgItem(panel,i)
                click=lambda i:u.SendMessageW(panel,0x111,i,0)
                wait(lambda:control(2160) and u.GetWindowLongW(control(2160),-16)&0x10000000 and control(2121),"Sign-in controls not ready")
                def complete(expected):
                    wait(lambda:u.IsWindowEnabled(control(2160)),'Account request stuck',timeout=20)
                    wait(lambda:expected in text(control(2163)),f'{phase}: expected {expected}; actual: {text(control(2163))}',timeout=3)
                if phase=='register':
                    click(2170)
                    for index,value in ((0,username),(1,email),(3,password),(4,password)):put(control(2120+index),value)
                    click(2160);complete('account is ready')
                    assert not u.GetWindowLongW(u.GetDlgItem(root,117),-16)&0x10000000,'Registration bypassed sign-in'
                    assert query(f"SELECT count(*) FROM luma_auth_users WHERE username='{username}' AND length(password_verifier)>0 AND length(password_salt)>0;")=='1'
                    evidence['checks'].append('GUI registration saved verifier and salt to PostgreSQL')
                    click(2172)
                put(control(2120),email);put(control(2121),password);click(2160);complete('signed in')
                wait(lambda:u.GetWindowLongW(u.GetDlgItem(root,117),-16)&0x10000000,'Authenticated workspace remained locked')
                assert text(u.GetDlgItem(root,129))==username
                evidence['checks'].append(phase+': GUI email/password login unlocked workspace and displayed account identity')
                u.SendMessageW(root,0x111,129,0)
                if phase=='relogin':
                    click(2162);click(2166);complete('account has been deleted')
                    assert query(f"SELECT count(*) FROM luma_auth_users WHERE username='{username}';")=='0'
                    evidence['checks'].append('GUI deletion removed only the generated test account')
                else:
                    click(2161)
                    wait(lambda:not u.GetWindowLongW(u.GetDlgItem(root,117),-16)&0x10000000,'Logout did not lock workspace')
                    evidence['checks'].append('GUI logout locked workspace')
            finally:
                if root:u.PostMessageW(root,0x10,0,0)
                try:process.wait(timeout=15)
                except subprocess.TimeoutExpired:process.terminate();process.wait();raise
    evidence['service_left_running']=service.ready()
    assert evidence['service_left_running']
    report='local-account-verification.json' if args.allow_service_restart else 'local-account-email-verification.json'
    (pg.ROOT/'output'/report).write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    print(json.dumps(evidence,indent=2))


if __name__=='__main__':main()
