"""Capture only this test-launched client's own rendering; no desktop capture."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct, subprocess, sys, time, os
import hashlib, json
from datetime import datetime, timezone
from workspace_auth_fixture import WorkspaceAuthFixture
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
# Isolate saved provider secrets and archived records from visual diagnostics.
profile=Path(sys.argv[2]).resolve()/".test-profile"
profile.mkdir(parents=True,exist_ok=True)
env={key.upper():value for key,value in os.environ.items()};env["LOCALAPPDATA"]=str(profile)
env["LUMALIVE_AUTH_ENV"]="test"; env["LUMALIVE_AUTH_TLS_CA_FILE"]=""
env["LUMALIVE_ACCOUNT_SETTINGS_PATH"]=str(profile/"account.conf")
fixture=Path(sys.argv[1]).resolve().parent/"luma_session_archive_tests.exe"
if fixture.is_file():subprocess.run([str(fixture),"--write-fixture",str(profile/"LumaLive/records")],env=env,check=True)
binary=Path(sys.argv[1]).resolve()
manifest={"created_utc":datetime.now(timezone.utc).isoformat(),"binary":str(binary),
          "sha256":hashlib.sha256(binary.read_bytes()).hexdigest(),
          "language_override":env.get("LUMALIVE_UI_LANGUAGE"),"captures":[]}
auth_fixture=WorkspaceAuthFixture(binary,env["LUMALIVE_ACCOUNT_SETTINGS_PATH"])
process=subprocess.Popen([str(binary)],startupinfo=startup,env=env);root=None
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
    def requested_size():
        if not os.environ.get("LUMALIVE_TEST_WINDOW"):return
        width,height=map(int,os.environ["LUMALIVE_TEST_WINDOW"].split("x"))
        u.GetDpiForWindow.argtypes=[w.HWND];u.GetDpiForWindow.restype=w.UINT
        scale=u.GetDpiForWindow(root)/96
        u.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
        class MinMaxInfo(c.Structure):
            _fields_=[('reserved',w.POINT),('max_size',w.POINT),('max_position',w.POINT),('min_track',w.POINT),('max_track',w.POINT)]
        limits=MinMaxInfo();u.SendMessageW(root,0x24,0,c.addressof(limits))
        width=max(limits.min_track.x,min(round(width*scale),limits.max_track.x or round(width*scale)))
        height=max(limits.min_track.y,min(round(height*scale),limits.max_track.y or round(height*scale)))
        assert u.SetWindowPos(root,None,0,0,width,height,0x16)
    requested_size()
    dwm=c.WinDLL("dwmapi")
    dwm.DwmGetWindowAttribute.argtypes=[w.HWND,w.DWORD,c.c_void_p,w.DWORD]
    colors={}
    for attr,label in [(35,"caption"),(36,"text"),(34,"border")]:
        value=w.DWORD()
        hr=dwm.DwmGetWindowAttribute(root,attr,c.byref(value),c.sizeof(value))
        colors[label]={"hresult":hr,"colorref":value.value}
    manifest["native_caption_attributes"]=colors
    # PrintWindow draws legacy non-client fallback on some Windows versions;
    # requested DWM colors are recorded separately, not inferred from this bitmap.
    directory=Path(sys.argv[2]);directory.mkdir(parents=True,exist_ok=True)
    for name,control,subpage in [("account",129,None),("account-register",129,2170),("account-recovery",129,2171),("account-recovery-code",129,2175),("account-more",129,2176),("account-sms",129,2173),("account-sms-code",129,2161),("calls",117,None),("account-profile",129,2100),("account-delete",129,2100),("account-security",129,2101),("account-password",129,2101),("account-devices",129,2102),("account-service",129,2103),("meetings",126,None),("assistant",127,11),("assistant-history",127,12),("assistant-history-delete",127,12),("assistant-settings",127,13)]:
        if name=="calls":
            account=u.FindWindowExW(root,None,"LumaAccountPage",None)
            u.SendMessageW(account,0x111,2173,0)
            auth_fixture.login(root)
            requested_size()
        u.SendMessageW(root,0x111,control,0);time.sleep(.5)
        if subpage:
            panel=u.FindWindowExW(root,None,"LumaAccountPage" if control==129 else "LumaSessionAi",None)
            while panel and not (u.GetWindowLongW(panel,-16)&0x10000000):panel=u.FindWindowExW(root,panel,"LumaSessionAi",None)
            assert panel and u.GetDlgItem(panel,subpage),"AI subpage not present in this build"
            if name in ('account-recovery','account-more'):u.SendMessageW(panel,0x111,2172,0)
            if name=='account-sms-code':
                # The isolated fixture returns the same response for an unlinked number.
                # Capture the real progressive code stage, without fabricating an MFA prompt.
                phone=c.create_unicode_buffer('+14155552670')
                u.SendMessageW(u.GetDlgItem(panel,2120),0xC,0,c.cast(phone,c.c_void_p).value)
            u.SendMessageW(panel,0x111,subpage,0);time.sleep(.2)
            if name=='account-sms-code':
                deadline=time.monotonic()+10
                while not u.GetWindowLongW(u.GetDlgItem(panel,2121),-16)&0x10000000 and time.monotonic()<deadline:time.sleep(.05)
                assert u.GetWindowLongW(u.GetDlgItem(panel,2121),-16)&0x10000000,'SMS code stage missing'
            if name == 'assistant-history-delete':
                u.SendMessageW(panel, 0x111, 36, 0);time.sleep(.1)
            if name == 'account-delete':
                u.SendMessageW(panel,0x111,2162,0);time.sleep(.1)
            if name == 'account-password':
                u.SendMessageW(u.GetDlgItem(panel,2165),0x14E,2,0)
                u.SendMessageW(panel,0x111,(1<<16)|2165,0);time.sleep(.1)
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
        rect=w.RECT();full_frame=bool(os.environ.get("LUMALIVE_CAPTURE_FRAME"));
        (u.GetWindowRect if full_frame else u.GetClientRect)(root,c.byref(rect));width,height=rect.right-rect.left,rect.bottom-rect.top
        reference=u.GetDC(root);dc=g.CreateCompatibleDC(reference)
        header=struct.pack("<IiiHHIIiiII",40,width,-height,1,32,0,width*height*4,0,0,0,0)
        info=c.create_string_buffer(header);memory=c.c_void_p()
        bitmap=g.CreateDIBSection(dc,info,0,c.byref(memory),None,0);assert bitmap and memory.value
        old=g.SelectObject(dc,bitmap)
        try:
            assert u.PrintWindow(root,dc,0 if full_frame else 1),"PrintWindow failed"
            g.GdiFlush()
            data=c.string_at(memory,width*height*4)
            (directory/(name+".bmp")).write_bytes(struct.pack("<HIHHI",0x4d42,54+len(data),0,0,54)+header+data)
        finally:
            g.SelectObject(dc,old);g.DeleteObject(bitmap);g.DeleteDC(dc);u.ReleaseDC(root,reference)
        print("CAPTURE:",name,width,height,flush=True)
        manifest["captures"].append({"file":name+".bmp","width":width,"height":height,"includes_native_frame":full_frame})
    assert hashlib.sha256(binary.read_bytes()).hexdigest()==manifest["sha256"],"Client changed during capture"
    (directory/"capture-manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
finally:
    if auth_fixture:auth_fixture.close()
    if root:u.PostMessageW(root,0x10,0,0)
    try:process.wait(timeout=15)
    except subprocess.TimeoutExpired:process.terminate();process.wait();raise RuntimeError("Client did not close")
