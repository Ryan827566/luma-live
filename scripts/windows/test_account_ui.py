"""Actual workspace account UI against the test-only in-memory auth server."""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import tempfile

u = c.WinDLL('user32', use_last_error=True)
cb = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [cb, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetDlgItem.argtypes = [w.HWND, c.c_int]; u.GetDlgItem.restype = w.HWND
u.FindWindowExW.argtypes = [w.HWND,w.HWND,w.LPCWSTR,w.LPCWSTR]; u.FindWindowExW.restype = w.HWND
u.SendMessageW.argtypes = [w.HWND,w.UINT,w.WPARAM,w.LPARAM]; u.SendMessageW.restype = c.c_ssize_t
u.PostMessageW.argtypes = [w.HWND,w.UINT,w.WPARAM,w.LPARAM]
u.GetWindowTextW.argtypes = [w.HWND,w.LPWSTR,c.c_int]
u.IsWindowEnabled.argtypes = [w.HWND]; u.IsWindowEnabled.restype = w.BOOL
u.GetWindowLongW.argtypes = [w.HWND,c.c_int]; u.GetWindowLongW.restype = w.LONG
def visible(h):return bool(u.GetWindowLongW(h,-16)&0x10000000)
u.GetWindowRect.argtypes = [w.HWND,c.POINTER(w.RECT)]
u.GetClientRect.argtypes = [w.HWND,c.POINTER(w.RECT)]
u.MapWindowPoints.argtypes = [w.HWND,w.HWND,c.c_void_p,w.UINT]
u.SetWindowPos.argtypes = [w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
u.GetDpiForWindow.argtypes = [w.HWND]; u.GetDpiForWindow.restype = w.UINT

def wait(check, timeout=20):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=check()
        if value:return value
        time.sleep(.05)
    raise AssertionError('UI condition timed out')

def text(h):
    out=c.create_unicode_buffer(8192);u.SendMessageW(h,0xD,len(out),c.cast(out,c.c_void_p).value);return out.value

def put(h,value):
    buf=c.create_unicode_buffer(value);u.SendMessageW(h,0xC,0,c.cast(buf,c.c_void_p).value)

binary=Path(sys.argv[1]).resolve()
with socket.socket() as reservation:
    reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
server=subprocess.Popen([str(binary.parent/'luma_account_ui_fixture.exe'),str(port)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
assert server.stdout.readline().strip()==str(port),'Account fixture failed'
def mailbox(command):
    server.stdin.write(command+'\n');server.stdin.flush()
    reply=server.stdout.readline().strip()
    assert reply and reply!='MISSING','Test delivery was not captured: '+command
    return reply

settings_dir=tempfile.TemporaryDirectory(prefix='luma-account-ui-')
settings_path=Path(settings_dir.name)/'account-connection.conf'
env={k.upper():v for k,v in os.environ.items()};env['LUMALIVE_UI_LANGUAGE']='en-US'
env['LUMALIVE_ACCOUNT_SETTINGS_PATH']=str(settings_path)
startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
process=subprocess.Popen([str(binary),'--account'],env=env,startupinfo=startup)
root=None
try:
    def find():
        found=[]
        @cb
        def visit(h,_):
            pid=w.DWORD();u.GetWindowThreadProcessId(h,c.byref(pid))
            if pid.value==process.pid and u.GetDlgItem(h,129):found.append(h)
            return True
        u.EnumWindows(visit,0);return found[0] if found else None
    root=wait(find)
    panel=wait(lambda:u.FindWindowExW(root,None,'LumaAccountPage',None))
    scale=u.GetDpiForWindow(root)/96
    u.SetWindowPos(root,None,0,0,round(1024*scale),round(700*scale),0x16)
    def control(i):return u.GetDlgItem(panel,i)
    def click(i):u.SendMessageW(panel,0x111,i,0)
    def select_mode(index):
        u.SendMessageW(control(2165),0x14E,index,0);u.SendMessageW(panel,0x111,(1<<16)|2165,0)
    def fields(*values):
        for i,value in enumerate(values):put(control(2120+i),value)
    def done(expected):
        wait(lambda:u.IsWindowEnabled(control(2160)))
        assert expected in text(control(2163)),text(control(2163))
    def fits():
        bounds=w.RECT();u.GetClientRect(panel,c.byref(bounds))
        for i in [*range(2100,2104),*range(2120,2126),*range(2140,2146),*range(2160,2169)]:
            h=control(i)
            if not h or not visible(h):continue
            rect=w.RECT();u.GetWindowRect(h,c.byref(rect));u.MapWindowPoints(None,panel,c.byref(rect),2)
            assert 0<=rect.left<rect.right<=bounds.right and 0<=rect.top<rect.bottom<=bounds.bottom,(i,rect.bottom,bounds.bottom)
    assert visible(control(2163)) and visible(control(2164)), 'Status or account details hidden'
    assert not u.GetWindowLongW(control(2164),-16)&0x200000, 'Short account details show a scrollbar'
    click(2103);fields('127.0.0.1','70000');click(2160);done('Port must be')
    fields('127.0.0.1',str(port));click(2160);done('Connected')
    click(2101);fits();fields('ui.person','ui@example.com','UI Person','secure ui password','different');click(2160);done('do not match')
    fields('ui.person','ui@example.com','UI Person','secure ui password','secure ui password');click(2160);done('Your account is ready')
    assert text(control(2121))=='','Password was retained after registration'
    click(2160);done('required')
    fields('ui.person','wrong password','');click(2160);done('incorrect')
    fields('ui.person','secure ui password','')
    u.SendMessageW(control(2121),0x10D,0,0)
    u.PostMessageW(control(2121),0x100,0x0D,0)
    time.sleep(.2)
    assert text(control(2100))=='Sign in' and 'incorrect' in text(control(2163)), 'IME Enter submitted the form'
    u.SendMessageW(control(2121),0x10E,0,0)
    fields('ui.person','secure ui password','')
    u.PostMessageW(control(2121),0x100,0x0D,0)
    wait(lambda:'signed in' in text(control(2163)))
    done('signed in')

    assert text(control(2100))=='Profile';fits()
    fields('ui.person','ui@example.com','Updated UI','');click(2160);done('Done')
    assert 'Updated UI' in text(control(2164))
    # The page and its authenticated session survive module switches.
    u.SendMessageW(root,0x111,127,0);u.SendMessageW(root,0x111,129,0)
    assert text(control(2100))=='Profile'
    # The injected delivery adapters never return OTPs through the account protocol.
    click(2101);mailbox('reject-email');click(2161);done('could not send the email')
    mailbox('accept-delivery');click(2161);done('Verification email sent')
    email_code=mailbox('email_verification')
    assert email_code not in text(control(2164))+text(control(2163))
    fields('invalid');click(2160);done('invalid or expired')
    fields(email_code);click(2160);done('Done');assert 'Email verified' in text(control(2164))
    select_mode(1);mailbox('reject-sms');fields('+14155552670','');click(2161);done('could not send the text message')
    mailbox('accept-delivery');fields('+14155552671','');click(2161);done('Text message sent')
    phone_code=mailbox('phone_verification')
    fields('+14155552672',phone_code);click(2160);done('Request a code for this phone number')
    fields('+14155552671',phone_code);click(2160);done('Done');assert 'Phone verified' in text(control(2164))
    click(2100)
    # Destructive actions stay inline and Escape uses the application's message loop.
    click(2162);assert visible(control(2166))
    u.PostMessageW(control(2167),0x100,0x1B,0);wait(lambda:not visible(control(2166)))
    click(2102);done('Done');assert u.SendMessageW(control(2165),0x146,0,0)==1
    click(2101)
    u.SendMessageW(control(2165),0x14E,3,0);u.SendMessageW(panel,0x111,(1<<16)|2165,0)
    click(2160);done('Two-step verification is on')
    assert text(control(2160))=='Turn off verification'
    recovery=text(control(2164)).split('Recovery codes (keep safe)\r\n')[1].strip().split(',')[0]
    fields('');click(2160);click(2166);done('required')
    assert u.IsWindowEnabled(control(2165)), 'Rejected confirmation left mode disabled'
    fields(recovery);click(2160);click(2166);done('Done')
    assert text(control(2160))=='Turn on verification', 'MFA toggle did not refresh'
    u.SendMessageW(control(2165),0x14E,4,0);u.SendMessageW(panel,0x111,(1<<16)|2165,0)
    done('Done');assert 'Security activity' in text(control(2164)) and 'Password sign-in succeeded' in text(control(2164))
    assert u.GetWindowLongW(control(2164),-16)&0x200000, 'Overflowing activity cannot scroll'
    u.SendMessageW(control(2164),0xB6,0,1000)
    assert u.SendMessageW(control(2164),0xCE,0,0)>0, 'Activity scroll position did not advance'
    click(2100)
    assert not u.GetWindowLongW(control(2164),-16)&0x200000, 'Short profile retained the activity scrollbar'
    click(2100);click(2161);done('signed out');assert text(control(2100))=='Sign in'
    select_mode(1);fields('+14155552671','','');click(2161);done('Text message sent')
    phone_login=mailbox('phone_login')
    fields('+14155552671',phone_login,'');click(2160);done('signed in')
    assert text(control(2100))=='Profile'
    click(2101);select_mode(2);fields('wrong password','changed password','changed password');click(2160);done('incorrect')
    fields('secure ui password','changed password','changed password');click(2160);done('Password updated')
    assert text(control(2100))=='Sign in'
    fields('ui.person','secure ui password','');click(2160);done('incorrect')
    fields('ui.person','changed password','');click(2160);done('signed in')
    click(2161);done('signed out')
    click(2102);fits();fields('ui.person');click(2161);done('check your email')
    reset_code=mailbox('password_reset')

    # A fresh client restores only the successful endpoint, without remembered credentials.
    saved=settings_path.read_bytes()
    assert b'ui.person' not in saved and b'secure ui password' not in saved
    u.PostMessageW(root,0x10,0,0);process.wait(timeout=10);root=None
    process=subprocess.Popen([str(binary),'--account'],env=env,startupinfo=startup)
    root=wait(find);panel=wait(lambda:u.FindWindowExW(root,None,'LumaAccountPage',None))
    wait(lambda:control(2100) and text(control(2100))=='Sign in')
    assert text(control(2120))=='' and text(control(2121))==''
    click(2103)
    assert text(control(2120))=='127.0.0.1' and text(control(2121))==str(port), 'Endpoint was not restored'
    # Complete recovery in a different client; consumed reset tokens cannot be reused.
    click(2102);fields('',reset_code,'recovered password','recovered password');click(2160);done('Password updated')
    click(2102);fields('',reset_code,'other password','other password');click(2160);done('invalid or expired')
    click(2100);fields('ui.person','recovered password','');click(2160);done('signed in')
    click(2162);click(2166);done('account has been deleted')
    fields('ui.person','recovered password','');click(2160);done('incorrect')
    click(2103)
    fields('invalid address',str(port));click(2160);done('IPv4')
    assert settings_path.read_bytes()==saved, 'Failed connection replaced the saved endpoint'
    u.PostMessageW(root,0x10,0,0);process.wait(timeout=10);root=None
    settings_path.write_text('malformed settings',encoding='utf-8')
    process=subprocess.Popen([str(binary),'--account'],env=env,startupinfo=startup)
    root=wait(find);panel=wait(lambda:u.FindWindowExW(root,None,'LumaAccountPage',None))
    wait(lambda:control(2163) and text(control(2163)))
    click(2103)
    assert text(control(2120))=='127.0.0.1' and text(control(2121))=='9100'
    assert settings_path.read_text(encoding='utf-8')=='malformed settings', 'Loading overwrote invalid settings'
    print('PASS: account UI, delivery failure, email/phone verification, SMS sign-in, password change, cross-client recovery, deletion, keyboard and endpoint persistence')
finally:
    if root:u.PostMessageW(root,0x10,0,0)
    try:process.wait(timeout=10)
    except subprocess.TimeoutExpired:process.terminate();process.wait();raise
    server.communicate('\n',timeout=10)
    settings_dir.cleanup()
