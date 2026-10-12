"""Verify bundled AI process ownership through the actual single-window client."""
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

u = c.WinDLL("user32", use_last_error=True)
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [callback, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetDlgItem.argtypes = [w.HWND, c.c_int]; u.GetDlgItem.restype = w.HWND
u.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]; u.FindWindowExW.restype = w.HWND
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]; u.SendMessageW.restype = c.c_ssize_t
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.IsWindowEnabled.argtypes = [w.HWND]; u.IsWindowEnabled.restype = w.BOOL
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]

def wait_for(fn, message, timeout=15):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        result=fn()
        if result: return result
        time.sleep(.1)
    raise AssertionError(message)

def text(control):
    value=c.create_unicode_buffer(2048)
    u.GetWindowTextW(control,value,len(value))
    return value.value

def main():
    executable=Path(sys.argv[1]).resolve()
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1",0)); port=reservation.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="luma-service-ui-") as profile:
        env={k.upper():v for k,v in os.environ.items() if not k.upper().startswith("LUMALIVE_AI_")}
        env.update(LOCALAPPDATA=profile,LUMALIVE_AI_GATEWAY_PORT=str(port),LUMALIVE_UI_LANGUAGE="en-US")
        start=subprocess.STARTUPINFO(); start.dwFlags=subprocess.STARTF_USESHOWWINDOW; start.wShowWindow=0
        process=subprocess.Popen([str(executable)],env=env,startupinfo=start)
        root=None
        def find_root():
            found=[]
            @callback
            def visit(hwnd,_):
                pid=w.DWORD(); u.GetWindowThreadProcessId(hwnd,c.byref(pid))
                if pid.value==process.pid and u.GetDlgItem(hwnd,127): found.append(hwnd)
                return True
            u.EnumWindows(visit,0)
            return found[0] if found else None
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        def health():
            try:
                with opener.open(f"http://127.0.0.1:{port}/health",timeout=.4) as response:
                    assert response.headers.get("X-Luma-AI-Protocol")=="1"
                    return json.loads(response.read())
            except OSError: return None
        try:
            root=wait_for(find_root,"Client did not create its main window")
            u.SendMessageW(root,0x111,127,0)
            panel=wait_for(lambda:u.FindWindowExW(root,None,"LumaSessionAi",None),"AI page missing")
            u.SendMessageW(panel,0x111,40,0)
            time.sleep(.2)
            assert health() is None,"Hidden settings launched AI service"
            u.SendMessageW(panel,0x111,13,0)
            u.SendMessageW(panel,0x111,40,0)
            wait_for(health,"Packaged service did not start")
            wait_for(lambda:u.IsWindowEnabled(u.GetDlgItem(panel,41)),"Owned service Stop is disabled")
            wait_for(lambda:not u.IsWindowEnabled(u.GetDlgItem(panel,40)),"Start remained enabled while running")
            # The same service survives navigation, without starting caption uploads.
            u.SendMessageW(panel,0x111,11,0)
            assert "Start captions" in text(u.GetDlgItem(panel,2)),"Starting local service opted into captions"
            u.SendMessageW(root,0x111,117,0)
            assert health(),"Hiding AI workspace stopped its service"
            u.SendMessageW(root,0x111,127,0)
            u.SendMessageW(panel,0x111,13,0)
            u.SendMessageW(panel,0x111,41,0)
            wait_for(lambda:health() is None,"Stop left the owned service running")
            wait_for(lambda:u.IsWindowEnabled(u.GetDlgItem(panel,40)),"Start not restored after stopping")
            u.SendMessageW(panel,0x111,40,0)
            wait_for(health,"Service could not restart")
            u.PostMessageW(root,0x10,0,0)
            process.wait(timeout=15)
            assert process.returncode==0,"Client did not close cleanly"
            wait_for(lambda:health() is None,"Client exit orphaned the packaged AI process")
            print("PASS: packaged service start, opt-in, navigation, stop, restart, exit cleanup")
        finally:
            if process.poll() is None:
                if root: u.PostMessageW(root,0x10,0,0)
                try: process.wait(timeout=8)
                except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=5)

if __name__=="__main__": main()
