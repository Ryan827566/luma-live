"""Capture only this test-launched client's own rendering; no desktop capture."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct, subprocess, sys, time, os
u=c.WinDLL("user32",use_last_error=True);g=c.WinDLL("gdi32",use_last_error=True)
cbtype=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.EnumWindows.argtypes=[cbtype,w.LPARAM];u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
u.GetDlgItem.argtypes=[w.HWND,c.c_int];u.GetDlgItem.restype=w.HWND
u.FindWindowExW.argtypes=[w.HWND,w.HWND,w.LPCWSTR,w.LPCWSTR];u.FindWindowExW.restype=w.HWND
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=c.c_ssize_t
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
u.GetWindowLongW.argtypes=[w.HWND,c.c_int];u.GetWindowLongW.restype=w.LONG
u.GetClientRect.argtypes=[w.HWND,c.POINTER(w.RECT)];u.GetDC.argtypes=[w.HWND];u.GetDC.restype=w.HDC
u.ReleaseDC.argtypes=[w.HWND,w.HDC];u.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT]
g.CreateCompatibleDC.argtypes=[w.HDC];g.CreateCompatibleDC.restype=w.HDC
g.CreateDIBSection.argtypes=[w.HDC,c.c_void_p,w.UINT,c.POINTER(c.c_void_p),w.HANDLE,w.DWORD];g.CreateDIBSection.restype=w.HBITMAP
g.SelectObject.argtypes=[w.HDC,w.HGDIOBJ];g.SelectObject.restype=w.HGDIOBJ
g.DeleteObject.argtypes=[w.HGDIOBJ];g.DeleteDC.argtypes=[w.HDC]
startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=4
process=subprocess.Popen([sys.argv[1]],startupinfo=startup);root=None
try:
    deadline=time.monotonic()+20
    while not root and time.monotonic()<deadline:
        found=[]
        @cbtype
        def visit(hwnd,_):
            pid=w.DWORD();u.GetWindowThreadProcessId(hwnd,c.byref(pid))
            if pid.value==process.pid and u.GetDlgItem(hwnd,127):found.append(hwnd)
            return True
        u.EnumWindows(visit,0)
        if found:root=found[0]
        time.sleep(.1)
    assert root,"No test client"
    if os.environ.get("LUMALIVE_TEST_WINDOW"):
        width,height=map(int,os.environ["LUMALIVE_TEST_WINDOW"].split("x"))
        u.GetDpiForWindow.argtypes=[w.HWND];u.GetDpiForWindow.restype=w.UINT
        scale=u.GetDpiForWindow(root)/96
        u.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
        assert u.SetWindowPos(root,None,0,0,round(width*scale),round(height*scale),0x16)
    directory=Path(sys.argv[2]);directory.mkdir(parents=True,exist_ok=True)
    for name,control in [("calls",117),("meetings",126),("assistant",127)]:
        u.SendMessageW(root,0x111,control,0);time.sleep(.5)
        if os.environ.get("LUMALIVE_TEST_ADVANCED"):
            if name=="calls":u.SendMessageW(root,0x111,128,0)
            if name=="meetings":
                meeting=u.FindWindowExW(root,None,"LumaMeetingPreview",None)
                u.SendMessageW(meeting,0x111,222,0)
            time.sleep(.1)
        if os.environ.get("LUMALIVE_TEST_FOCUS"):
            edit=u.GetDlgItem(root,116) if name=="calls" else None
            if name=="meetings":edit=u.GetDlgItem(u.FindWindowExW(root,None,"LumaMeetingPreview",None),201)
            if name=="assistant":
                panel=u.FindWindowExW(root,None,"LumaSessionAi",None)
                # The most recently opened panel belongs to the active meeting context.
                while panel:
                    if u.GetWindowLongW(panel,-16)&0x10000000:break
                    panel=u.FindWindowExW(root,panel,"LumaSessionAi",None)
                if panel:edit=u.GetDlgItem(panel,4)
            if edit:
                u.SendMessageW(edit,0x201,1,(8<<16)|8);u.SendMessageW(edit,0x202,0,(8<<16)|8)
                time.sleep(.1)
        rect=w.RECT();u.GetClientRect(root,c.byref(rect));width,height=rect.right,rect.bottom
        reference=u.GetDC(root);dc=g.CreateCompatibleDC(reference)
        header=struct.pack("<IiiHHIIiiII",40,width,-height,1,32,0,width*height*4,0,0,0,0)
        info=c.create_string_buffer(header);memory=c.c_void_p()
        bitmap=g.CreateDIBSection(dc,info,0,c.byref(memory),None,0);assert bitmap and memory.value
        old=g.SelectObject(dc,bitmap)
        try:
            assert u.PrintWindow(root,dc,1),"PrintWindow failed"
            g.GdiFlush()
            data=c.string_at(memory,width*height*4)
            (directory/(name+".bmp")).write_bytes(struct.pack("<HIHHI",0x4d42,54+len(data),0,0,54)+header+data)
        finally:
            g.SelectObject(dc,old);g.DeleteObject(bitmap);g.DeleteDC(dc);u.ReleaseDC(root,reference)
        print("CAPTURE:",name,width,height,flush=True)
finally:
    if root:u.PostMessageW(root,0x10,0,0)
    try:process.wait(timeout=15)
    except subprocess.TimeoutExpired:process.terminate();process.wait();raise RuntimeError("Client did not close")
