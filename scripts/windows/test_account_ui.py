"""Actual workspace account UI against the test-only in-memory auth server."""
import ctypes as c
from ctypes import wintypes as w
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import tempfile

u = c.WinDLL('user32', use_last_error=True)
g = c.WinDLL('gdi32', use_last_error=True)
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
u.GetDC.argtypes=[w.HWND];u.GetDC.restype=w.HDC
u.ReleaseDC.argtypes=[w.HWND,w.HDC]
u.DrawTextW.argtypes=[w.HDC,w.LPCWSTR,c.c_int,c.POINTER(w.RECT),w.UINT]
g.GetObjectW.argtypes=[w.HGDIOBJ,c.c_int,c.c_void_p]
g.CreateFontIndirectW.argtypes=[c.c_void_p];g.CreateFontIndirectW.restype=w.HFONT
g.SelectObject.argtypes=[w.HDC,w.HGDIOBJ];g.SelectObject.restype=w.HGDIOBJ
g.GetTextExtentPoint32W.argtypes=[w.HDC,w.LPCWSTR,c.c_int,c.POINTER(w.SIZE)]
g.DeleteObject.argtypes=[w.HGDIOBJ]
class LogFont(c.Structure):
    _fields_=[('height',w.LONG),('width',w.LONG),('escapement',w.LONG),('orientation',w.LONG),('weight',w.LONG),
              ('italic',w.BYTE),('underline',w.BYTE),('strikeout',w.BYTE),('charset',w.BYTE),('out_precision',w.BYTE),
              ('clip_precision',w.BYTE),('quality',w.BYTE),('pitch_family',w.BYTE),('face',w.WCHAR*32)]
class MinMaxInfo(c.Structure):
    _fields_=[('reserved',w.POINT),('max_size',w.POINT),('max_position',w.POINT),('min_track',w.POINT),('max_track',w.POINT)]

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

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--real-sms-resend', action='store_true',
                    help='Wait at least 61 real seconds and verify successful SMS resend after an MFA challenge')
args=parser.parse_args()
binary=args.binary.resolve()
resend_report=Path(__file__).resolve().parents[2]/'output/account-ui-sms-resend.json'
resend_evidence={}
def save_resend_report():
    resend_report.parent.mkdir(parents=True,exist_ok=True)
    resend_report.write_text(json.dumps(resend_evidence,indent=2)+'\n',encoding='utf-8')
if args.real_sms_resend:
    resend_evidence={'status':'running','started_utc':datetime.now(timezone.utc).isoformat(),
                     'client_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
                     'fixture_sha256':hashlib.sha256((binary.parent/'luma_account_ui_fixture.exe').read_bytes()).hexdigest(),
                     'scope':'Actual client with isolated in-memory server and private delivery mailbox; no external SMS delivery',
                     'auth_env':'test','tls_ca_file':'','production_service_restarted':False,'checks':[]}
    save_resend_report()
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
settings_path.write_text(f'LumaLiveAccountConnection 1\nhost=127.0.0.1\nport={port}\n',encoding='utf-8')
env={k.upper():v for k,v in os.environ.items()};env['LUMALIVE_UI_LANGUAGE']='en-US'
env["LUMALIVE_AUTH_ENV"]="test"; env["LUMALIVE_AUTH_TLS_CA_FILE"]=""
env['LUMALIVE_ACCOUNT_SETTINGS_PATH']=str(settings_path)
startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
process=subprocess.Popen([str(binary),'--account'],env=env,startupinfo=startup)
root=None
ui_passed=False
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
    initial=w.RECT();u.GetWindowRect(root,c.byref(initial))
    assert (initial.right-initial.left,initial.bottom-initial.top)==(round(560*scale),round(560*scale)), 'Sign-in default is not the compact Codex-style size'
    limits=MinMaxInfo();u.SendMessageW(root,0x24,0,c.addressof(limits))
    assert (limits.min_track.x,limits.min_track.y)==(round(480*scale),round(560*scale)), 'Sign-in minimum is incorrect'
    assert limits.min_track.x<=limits.max_track.x<=round(960*scale) and limits.min_track.y<=limits.max_track.y<=round(800*scale), 'Sign-in maximum is not bounded to the desktop'
    def control(i):return u.GetDlgItem(panel,i)
    def click(i):u.SendMessageW(panel,0x111,i,0)
    def select_mode(index):
        u.SendMessageW(control(2165),0x14E,index,0);u.SendMessageW(panel,0x111,(1<<16)|2165,0)
    def fields(*values):
        for i,value in enumerate(values):put(control(2120+i),value)
    def country(iso,prefix):
        combo=control(2177)
        assert combo and visible(combo),'SMS country selector is missing'
        count=u.SendMessageW(combo,0x146,0,0)
        for index in range(count):
            label=c.create_unicode_buffer(128)
            u.SendMessageW(combo,0x148,index,c.cast(label,c.c_void_p).value)
            if iso in label.value and prefix in label.value:
                u.SendMessageW(combo,0x14E,index,0);u.SendMessageW(panel,0x111,(1<<16)|2177,0)
                return
        raise AssertionError(f'Country option {iso} {prefix} missing')
    def sms_state(code=False,mfa=False,notice=False):
        expected={2120,2140,2161,2173,2177}
        if code:expected.update((2121,2141,2160))
        if mfa:expected.update((2122,2142))
        if notice:expected.add(2163)
        actual={i for i in range(2100,2178) if control(i) and visible(control(i))}
        assert actual==expected,('Unexpected SMS controls',actual,expected)
        for i in range(2120,2126):
            if i not in expected:assert text(control(i))=='',('Hidden SMS input retained text',i)
        entrance_sizes()
    def done(expected):
        wait(lambda:u.IsWindowEnabled(control(2160)))
        assert expected in text(control(2163)),text(control(2163))
        authenticated=text(control(2100))=='Profile'
        wait(lambda:bool(u.GetWindowLongW(u.GetDlgItem(root,117),-16)&0x10000000)==authenticated)
        if authenticated:u.SendMessageW(root,0x111,129,0)
        fits()
    def password_visibility(*indices):
        assert visible(control(2168)), 'Password reveal action is missing'
        for index in indices:assert u.SendMessageW(control(2120+index),0xD2,0,0)!=0
        click(2168)
        for index in indices:assert u.SendMessageW(control(2120+index),0xD2,0,0)==0
        click(2168)
        for index in indices:assert u.SendMessageW(control(2120+index),0xD2,0,0)!=0
    def label_fits(i,h,rect):
        label=text(h)
        if not label:return
        font=c.c_void_p(u.SendMessageW(h,0x31,0,0));description=LogFont()
        assert font.value and g.GetObjectW(font,c.sizeof(description),c.byref(description))==c.sizeof(description),('Could not inspect control font',i)
        local_font=g.CreateFontIndirectW(c.byref(description));assert local_font
        dc=u.GetDC(h);assert dc
        previous=g.SelectObject(dc,local_font)
        try:
            width=rect.right-rect.left;height=rect.bottom-rect.top
            if i==2163:
                measured=w.RECT(0,0,width,0)
                u.DrawTextW(dc,label,len(label),c.byref(measured),0x400|0x10|0x800) # CALCRECT, WORDBREAK, NOPREFIX
                assert measured.bottom<=height,('Status text clipped',label,measured.bottom,height)
            else:
                size=w.SIZE();assert g.GetTextExtentPoint32W(dc,label,len(label),c.byref(size))
                inset_x=0;inset_y=0
                if 2160<=i<=2168 and i!=2163:inset_x=8;inset_y=2
                if i>=2170 and i<=2176 or i==2168:inset_x=round(8*scale);inset_y=round(2*scale)
                if i==2177:inset_x=round(23*scale)
                assert size.cx<=width-2*inset_x and size.cy<=height-2*inset_y,('Control text clipped',i,label,(size.cx,size.cy),(width-2*inset_x,height-2*inset_y))
        finally:
            g.SelectObject(dc,previous);u.ReleaseDC(h,dc);g.DeleteObject(local_font)
    def fits():
        bounds=w.RECT();u.GetClientRect(panel,c.byref(bounds))
        visible_bounds=[]
        for i in [*range(2100,2104),*range(2120,2126),*range(2140,2146),*range(2160,2178)]:
            h=control(i)
            if not h or not visible(h):continue
            rect=w.RECT();u.GetWindowRect(h,c.byref(rect));u.MapWindowPoints(None,panel,c.byref(rect),2)
            assert 0<=rect.left<rect.right<=bounds.right and 0<=rect.top<rect.bottom<=bounds.bottom,(i,rect.bottom,bounds.bottom)
            visible_bounds.append((i,rect))
            if text(control(2100))=='Sign in' and (2140<=i<=2145 or i>=2160 and i not in (2164,2165,2168)):
                label_fits(i,h,rect)
        for index,(first,a) in enumerate(visible_bounds):
            for second,b in visible_bounds[index+1:]:
                if second==2168 and 2120<=first<=2125 and a.left<b.left<b.right<a.right and a.top<b.top<b.bottom<a.bottom:
                    margin=(u.SendMessageW(control(first),0xD4,0,0)>>16)&0xffff
                    assert margin>=a.right-b.left, 'Password text runs under the eye'
                    continue
                assert min(a.right,b.right)<=max(a.left,b.left) or min(a.bottom,b.bottom)<=max(a.top,b.top),f'Account controls overlap: {first}, {second}'
        rects=dict(visible_bounds)
        if text(control(2100))=='Sign in':
            assert not any(i in rects for i in (*range(2100,2104),2164,2165)), 'Signed-out entrance exposed tabs/details/modes'
            column=rects.get(2163,rects.get(2160,rects.get(2161)))
            assert abs(column.left+column.right-bounds.right)<=2,'Signed-out form is not centered'
            for i in range(2120,2126):
                if i in rects:assert column.left<=rects[i].left<rects[i].right<=column.right,'Input left the form column'
            if 2163 in rects:
                action_bottom=max(rects[i].bottom for i in (2160,2161) if i in rects)
                assert rects[2163].top>=action_bottom+round(8*scale),'Result crowds the primary action'
        else:
            # Results belong to the form, not the remote bottom corner of the page.
            if 2163 not in rects:return
            primary=rects[2160];status=rects[2163]
            action_bottom=max(rects[i].bottom for i in (2160,2161,2162) if i in rects)
            action_height=primary.bottom-primary.top
            assert status.left<=primary.left<primary.right<=status.right,'Result left the form column'
            if 2120 in rects:
                field=rects[2120]
                assert status.left<=field.left<field.right<=status.right,'Result is detached from its fields'
            assert 0<status.top-action_bottom<=action_height,'Result is not next to its form actions'
            if 2166 in rects or 2167 in rects:
                assert 2166 in rects and 2167 in rects,'Confirmation is missing its cancel action'
                for i in (2166,2167):
                    action=rects[i]
                    assert status.left<=action.left<action.right<=status.right,'Confirmation left the form column'
                    assert 0<action.top-status.bottom<=action_height,'Confirmation is detached from its prompt'
    def entrance_sizes():
        """Exercise every entrance state at the supported outer sizes."""
        original=w.RECT();u.GetWindowRect(root,c.byref(original))
        try:
            for width,height in ((limits.min_track.x,limits.min_track.y),(round(560*scale),round(560*scale)),(limits.max_track.x,limits.max_track.y)):
                assert u.SetWindowPos(root,None,0,0,width,height,0x16)
                actual=w.RECT();u.GetWindowRect(root,c.byref(actual))
                assert (actual.right-actual.left,actual.bottom-actual.top)==(width,height),'Entrance size not exercised'
                fits()
        finally:u.SetWindowPos(root,None,original.left,original.top,original.right-original.left,original.bottom-original.top,0x14)
        fits()
    def at_sizes(check=None):
        """Check compact and attainable wide sizes, then restore the current size."""
        original=w.RECT();u.GetWindowRect(root,c.byref(original))
        try:
            for width,height in ((1024,700),(1440,900)):
                # Windows clamps oversized windows to the current desktop's tracking
                # limit. Exercise the available wide size and report it accurately.
                requested=(min(round(width*scale),u.GetSystemMetrics(59)),
                           min(round(height*scale),u.GetSystemMetrics(60)))
                assert u.SetWindowPos(root,None,0,0,*requested,0x16),'Account resize failed'
                actual=w.RECT();u.GetWindowRect(root,c.byref(actual))
                assert (actual.right-actual.left,actual.bottom-actual.top)==requested,('Account size was not exercised',requested,(actual.right-actual.left,actual.bottom-actual.top),scale)
                fits()
                if check:check()
        finally:
            u.SetWindowPos(root,None,0,0,original.right-original.left,original.bottom-original.top,0x16)
        fits()
    def short_details():
        assert not u.GetWindowLongW(control(2164),-16)&0x200000,'Short profile has an unnecessary scrollbar'
    def scrolling_activity():
        assert u.GetWindowLongW(control(2164),-16)&0x200000,'Overflowing activity cannot scroll'
        u.SendMessageW(control(2164),0xB6,0,-100000)
        assert u.SendMessageW(control(2164),0xCE,0,0)==0,'Activity could not scroll back to the beginning'
        u.SendMessageW(control(2164),0xB6,0,1000)
        assert u.SendMessageW(control(2164),0xCE,0,0)>0,'Activity scroll position did not advance'
    assert not visible(u.GetDlgItem(root,117)), 'Workspace visible before login'
    u.SendMessageW(root,0x111,126,0)
    assert not u.FindWindowExW(root,None,'LumaMeetingPreview',None), 'Hidden meeting command bypassed sign-in'
    assert not visible(control(2164)) and not visible(control(2165)), 'Entrance exposed account details or method dropdown'
    assert visible(control(2120)) and visible(control(2121)) and not visible(control(2122)), 'Sign-in must initially show exactly two fields'
    assert not visible(control(2173)) and not visible(control(2174)), 'Optional sign-in methods were not collapsed'
    entrance_sizes()
    for tab in (2100,2101,2102,2103):
        assert not visible(control(tab)), 'Signed-out navigation tab exposed'
        click(tab);fits()
        assert text(control(2160))=='Sign in' and not visible(control(2122)), 'Hidden tab command navigated the entrance'
    click(2176);assert visible(control(2173)) and visible(control(2174));entrance_sizes()
    click(2174);assert visible(control(2122));entrance_sizes()
    click(2173);assert not visible(control(2122));entrance_sizes()
    assert visible(control(2177)) and '+' in text(control(2177)), 'SMS entry has no automatic calling code'
    sms_state()
    for hidden in (2170,2171,2172,2174,2175,2176):click(hidden)
    sms_state()
    click(2173);assert not visible(control(2122)) and not visible(control(2177));entrance_sizes()
    click(2171);entrance_sizes();assert not visible(control(2121)) and not visible(control(2122)), 'Recovery exposed later steps before request'
    click(2175);assert not visible(control(2120));entrance_sizes()
    click(2172);click(2170);entrance_sizes();assert text(control(2160))=='Register';assert not visible(control(2122));password_visibility(3,4)
    fields('x','ui@example.com','','secure ui password','secure ui password');click(2160);done('3–32');entrance_sizes()
    fields('ui.person','invalid-email','','secure ui password','secure ui password');click(2160);done('valid email');entrance_sizes()
    fields('ui.person','ui@example.com','','short','short');click(2160);done('too short');entrance_sizes()
    fields('ui.person','ui@example.com','','secure ui password','different');click(2160);done('do not match');entrance_sizes()
    mailbox('reject-storage');fields('ui.person','ui@example.com','','secure ui password','secure ui password');click(2160);done('server could not save');entrance_sizes()
    assert not visible(u.GetDlgItem(root,117)), 'Failed persistence unlocked workspace'
    mailbox('accept-storage')
    fields('ui.person','ui@example.com','','secure ui password','secure ui password');click(2160);done('Your account is ready')
    click(2170);fields('ui.person','ui@example.com','','secure ui password','secure ui password');click(2160);done('username or email is registered');entrance_sizes()
    click(2172)
    assert text(control(2121))=='','Password was retained after registration'
    click(2160);done('required')
    fields('ui.person','wrong password','');click(2160);done('incorrect');entrance_sizes()
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

    assert text(control(2100))=='Profile';fits();assert not visible(control(2168))
    signedin_limits=MinMaxInfo();u.SendMessageW(root,0x24,0,c.addressof(signedin_limits))
    assert (signedin_limits.min_track.x,signedin_limits.min_track.y)==(round(1024*scale),round(700*scale)), 'Workspace kept sign-in sizing restrictions'
    assert text(u.GetDlgItem(root,129))=='ui.person', 'Sidebar did not show the username as initial display name'
    fields('ui.person','ui@example.com','Updated UI','');click(2160);done('Done')
    assert 'Updated UI' in text(control(2164))
    at_sizes(short_details)
    # Every authenticated page and security mode must fit before submitting anything.
    click(2101)
    for mode_index in range(5):
        select_mode(mode_index)
        if mode_index==4:done('Done')
        at_sizes()
    click(2102);done('Done');at_sizes()
    click(2103);at_sizes()
    fields('127.0.0.1','70000');click(2160);done('Port must be')
    saved=settings_path.read_bytes()
    click(2100)
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
    at_sizes()
    u.PostMessageW(control(2167),0x100,0x1B,0);wait(lambda:not visible(control(2166)))
    fits()
    click(2102);done('Done');assert u.SendMessageW(control(2165),0x146,0,0)==1
    for action in (2161,2162):
        click(action);assert visible(control(2166)) and visible(control(2167))
        at_sizes();click(2167)
        assert not visible(control(2166)) and not visible(control(2167))
        fits()
    click(2101)
    u.SendMessageW(control(2165),0x14E,3,0);u.SendMessageW(panel,0x111,(1<<16)|2165,0)
    click(2160);done('Two-step verification is on')
    assert text(control(2160))=='Turn off verification'
    at_sizes()
    recovery_codes=text(control(2164)).split('Recovery codes (keep safe)\r\n')[1].strip().split(',')
    recovery=recovery_codes[1]
    click(2100);click(2161);done('signed out')
    fields('ui.person','secure ui password');click(2160);done('authenticator or recovery code')
    assert visible(control(2122)), 'Server MFA requirement did not reveal the verification field'
    assert u.SendMessageW(control(2121),0xD2,0,0)!=0, 'MFA challenge exposed the password'
    click(2168)
    assert text(control(2120))=='ui.person' and text(control(2121))=='secure ui password', 'MFA reveal discarded sign-in input'
    click(2168);entrance_sizes()
    # Submit only the additional factor: successful login proves masked credentials survived.
    put(control(2122),recovery_codes[0]);click(2160);done('signed in')
    click(2100);click(2161);done('signed out')
    click(2176);click(2173);country('US','+1');sms_state()
    # IME Enter cannot send an SMS. Regular Enter requests a code in the first stage.
    put(control(2120),'4155552673')
    u.SendMessageW(control(2120),0x10D,0,0);u.PostMessageW(control(2120),0x100,0x0D,0)
    time.sleep(.2);sms_state()
    u.SendMessageW(control(2120),0x10E,0,0);u.PostMessageW(control(2120),0x100,0x0D,0)
    wait(lambda:'linked to an account' in text(control(2163)));done('linked to an account');sms_state(code=True,notice=True)
    # Equivalent formatting retains the challenge; changing its calling code does not.
    put(control(2121),'old-code');put(control(2120),'(415) 555-2673')
    assert text(control(2121))=='old-code';sms_state(code=True,notice=True)
    country('GB','+44');sms_state()
    # Unknown numbers have the same conditional response, without claiming SMS delivery.
    put(control(2120),'+14155552674');u.PostMessageW(control(2120),0x100,0x0D,0)
    wait(lambda:'linked to an account' in text(control(2163)));done('linked to an account');sms_state(code=True,notice=True)
    put(control(2121),'old-code');put(control(2120),'+14155552671');sms_state()
    u.PostMessageW(control(2120),0x100,0x0D,0)
    wait(lambda:'linked to an account' in text(control(2163)));done('linked to an account');sms_state(code=True,notice=True)
    phone_login=mailbox('phone_login')
    phone_login_received_at=time.monotonic()
    # Explicit international input ignores the selector; equivalent national input preserves its challenge.
    country('US','+1');put(control(2120),'4155552671');sms_state(code=True,notice=True)
    put(control(2121),phone_login);click(2161);done('Too many attempts')
    assert text(control(2121))==phone_login,'Rejected resend discarded the outstanding code'
    sms_state(code=True,notice=True)
    # SMS failures must not claim the account/password is incorrect.
    put(control(2121),'not-a-code');click(2160);done('text message code is incorrect');sms_state(code=True,notice=True)
    put(control(2121),phone_login);u.PostMessageW(control(2121),0x100,0x0D,0)
    wait(lambda:visible(control(2122)));done('authenticator or recovery code');sms_state(code=True,mfa=True,notice=True)
    assert text(control(2121))==phone_login,'MFA challenge discarded the text message code'
    put(control(2122),'not-a-recovery-code');click(2160);done('authenticator or recovery code')
    assert text(control(2121))==phone_login,'Incorrect MFA discarded the text message code'
    sms_state(code=True,mfa=True,notice=True)
    if args.real_sms_resend:
        # Start after receiving the original delivery, so the lower bound also
        # holds for server issuance. Do not advance/inject the server's clock.
        print('Waiting for the real SMS resend cooldown (61 seconds after delivery receipt)',flush=True)
        while True:
            remaining=61-(time.monotonic()-phone_login_received_at)
            if remaining<=0:break
            assert process.poll() is None and server.poll() is None,'SMS fixture exited during cooldown'
            time.sleep(min(.25,remaining))
        assert text(control(2121))==phone_login and text(control(2122))=='not-a-recovery-code', 'Cooldown changed the pending inputs'
        preserved_phone=text(control(2120))
        resend_evidence['elapsed_before_resend_seconds']=time.monotonic()-phone_login_received_at
        click(2161);done('linked to an account');sms_state(code=True,notice=True)
        assert text(control(2120))==preserved_phone,'Successful resend changed the phone number'
        assert text(control(2121))=='' and text(control(2122))=='','Successful resend retained SMS/MFA input'
        # Mailbox reads consume their message. A second reply proves a new
        # delivery even if the two independent random OTP values happen to match.
        resent_login=mailbox('phone_login')
        resend_evidence['checks'].extend(['real_cooldown_elapsed','new_fixture_delivery',
                                         'phone_preserved','sms_input_cleared','mfa_input_cleared_and_hidden'])
        if resent_login!=phone_login:
            put(control(2121),phone_login);click(2160);done('text message code is incorrect')
            sms_state(code=True,notice=True)
            resend_evidence['old_code_on_new_challenge']='rejected'
        else:
            resend_evidence['old_code_on_new_challenge']='not_distinguishable_random_code_collision'
        phone_login=resent_login
        put(control(2121),phone_login);u.PostMessageW(control(2121),0x100,0x0D,0)
        wait(lambda:visible(control(2122)));done('authenticator or recovery code');sms_state(code=True,mfa=True,notice=True)
        assert text(control(2121))==phone_login and text(control(2122))=='','New MFA challenge retained stale input'
        resend_evidence['checks'].append('new_code_requires_fresh_mfa')
        save_resend_report()
    put(control(2122),recovery_codes[2]);u.PostMessageW(control(2122),0x100,0x0D,0)
    wait(lambda:'signed in' in text(control(2163)));done('signed in')
    if args.real_sms_resend:
        resend_evidence['checks'].append('new_code_and_mfa_unlock_workspace')
    click(2101);select_mode(3)
    fields('');click(2160);at_sizes();click(2166);done('required')
    assert u.IsWindowEnabled(control(2165)), 'Rejected confirmation left mode disabled'
    fields(recovery);click(2160);click(2166);done('Done')
    assert text(control(2160))=='Turn on verification', 'MFA toggle did not refresh'
    u.SendMessageW(control(2165),0x14E,4,0);u.SendMessageW(panel,0x111,(1<<16)|2165,0)
    done('Done');assert 'Security activity' in text(control(2164)) and 'Password sign-in succeeded' in text(control(2164))
    assert u.GetWindowLongW(control(2164),-16)&0x200000, 'Overflowing activity cannot scroll'
    u.SendMessageW(control(2164),0xB6,0,1000)
    assert u.SendMessageW(control(2164),0xCE,0,0)>0, 'Activity scroll position did not advance'
    at_sizes(scrolling_activity)
    click(2100)
    assert not u.GetWindowLongW(control(2164),-16)&0x200000, 'Short profile retained the activity scrollbar'
    at_sizes(short_details)
    click(2100);click(2161);done('signed out');assert text(control(2100))=='Sign in'
    signedout=w.RECT();u.GetWindowRect(root,c.byref(signedout))
    assert (signedout.right-signedout.left,signedout.bottom-signedout.top)==(round(560*scale),round(560*scale)), 'Logout did not restore entrance dimensions'
    fields('ui@example.com','secure ui password');click(2160);done('signed in')
    assert text(control(2100))=='Profile'
    click(2101);select_mode(2);fits();password_visibility(0,1,2);fields('wrong password','changed password','changed password');click(2160);done('incorrect')
    fields('secure ui password','changed password','changed password');click(2160);done('Password updated')
    assert text(control(2100))=='Sign in'
    fields('ui.person','secure ui password','');click(2160);done('incorrect')
    fields('ui.person','changed password','');click(2160);done('signed in')
    click(2161);done('signed out')
    click(2171);entrance_sizes();fields('ui.person');click(2161);done('check your email');entrance_sizes()
    assert visible(control(2121)) and visible(control(2122)) and visible(control(2123)), 'Successful reset request did not reveal reset fields'
    password_visibility(2,3)
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
    assert text(control(2120))=='' and text(control(2121))=='' and text(control(2160))=='Sign in', 'Hidden connection command exposed service fields'
    # Complete recovery in a different client; consumed reset tokens cannot be reused.
    click(2171);click(2175);fields('',reset_code,'recovered password','recovered password');click(2160);done('Password updated')
    click(2171);click(2175);fields('',reset_code,'other password','other password');click(2160);done('invalid or expired')
    click(2172);fields('ui.person','recovered password','');click(2160);done('signed in')
    click(2103);fields('invalid address',str(port));click(2160);done('IPv4')
    assert settings_path.read_bytes()==saved, 'Failed connection replaced the saved endpoint'
    fields('ui.person','recovered password');click(2160);done('signed in')
    # Explicitly changing the authenticated service endpoint clears that session.
    # The new endpoint is persisted and re-login uses it without a public editor.
    click(2103);fields('127.0.0.1',str(port));click(2160);done('Connected')
    # An explicit successful save canonicalizes line endings. Subsequent
    # authentication must preserve this new byte-for-byte baseline.
    assert settings_path.read_text(encoding='utf-8')==f'LumaLiveAccountConnection 1\nhost=127.0.0.1\nport={port}\n'
    saved=settings_path.read_bytes()
    assert not visible(control(2103)), 'Changing service exposed signed-out settings'
    fields('ui.person','recovered password');click(2160);done('signed in')
    click(2162);click(2166);done('account has been deleted')
    fields('ui.person','recovered password','');click(2160);done('incorrect')
    assert settings_path.read_bytes()==saved, 'Authentication modified service configuration'
    u.PostMessageW(root,0x10,0,0);process.wait(timeout=10);root=None
    settings_path.write_text('malformed settings',encoding='utf-8')
    process=subprocess.Popen([str(binary),'--account'],env=env,startupinfo=startup)
    root=wait(find);panel=wait(lambda:u.FindWindowExW(root,None,'LumaAccountPage',None))
    wait(lambda:control(2163) and text(control(2163)))
    click(2103)
    assert text(control(2120))=='' and text(control(2121))=='' and not visible(control(2103)), 'Invalid configuration exposed a connection editor'
    assert settings_path.read_text(encoding='utf-8')=='malformed settings', 'Loading overwrote invalid settings'
    print('PASS: account UI, authenticated page/confirmation layouts at compact and desktop-limited wide sizes',
          (min(round(1440*scale),u.GetSystemMetrics(59)),min(round(900*scale),u.GetSystemMetrics(60))),
          'minimal signed-out entrance, progressive MFA/recovery, hidden command rejection, form results, short/long detail scrolling, delivery failure, email/phone verification, SMS sign-in, password change, cross-client recovery, deletion, keyboard and preconfigured endpoint persistence')
    ui_passed=True
finally:
    cleanup_complete=False
    try:
        if root:u.PostMessageW(root,0x10,0,0)
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:process.terminate();process.wait();raise
        server.communicate('\n',timeout=10)
        settings_dir.cleanup()
        cleanup_complete=True
    finally:
        if args.real_sms_resend:
            resend_evidence['status']='passed' if ui_passed and cleanup_complete else 'failed'
            resend_evidence['fixture_cleanup_complete']=cleanup_complete
            resend_evidence['finished_utc']=datetime.now(timezone.utc).isoformat()
            save_resend_report()
