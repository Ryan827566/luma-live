"""Per-user encrypted provider settings. No remote configuration-write API."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path

FIELDS = ('BASE_URL', 'API_KEY', 'TRANSCRIBE_MODEL', 'CHAT_MODEL', 'TTS_MODEL', 'TTS_VOICE')
MAX_SETTINGS_BYTES = 64 * 1024

class SettingsError(ValueError):
    """Safe diagnostic without saved content or credentials."""


def settings_path():
    if os.name != 'nt':
        return None
    local = os.environ.get('LOCALAPPDATA')
    if not local:
        raise SettingsError('saved_settings_location_unavailable')
    return Path(local) / 'LumaLive' / 'ai-provider.dat'


def parse_payload(payload):
    try:
        text = payload.decode('utf-8', errors='strict')
    except UnicodeError:
        raise SettingsError('invalid_saved_settings') from None
    values = {}
    if len(payload) > MAX_SETTINGS_BYTES:
        raise SettingsError('invalid_saved_settings')
    for line in text.split('\n'):
        if not line:
            continue
        key, sep, value = line.partition('=')
        if (not sep or key not in FIELDS or key in values or
                any(ord(c) < 32 or ord(c) == 127 for c in value)):
            raise SettingsError('invalid_saved_settings')
        values[key] = value
    if set(values) != set(FIELDS):
        raise SettingsError('invalid_saved_settings')
    return values


def unprotect(encrypted):
    if os.name != 'nt':
        raise SettingsError('saved_settings_require_windows')
    class Blob(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('data', ctypes.POINTER(ctypes.c_ubyte))]
    crypt32 = ctypes.WinDLL('crypt32', use_last_error=True)
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    crypt32.CryptUnprotectData.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p,
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    crypt32.CryptUnprotectData.restype = wintypes.BOOL
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p
    buffer = (ctypes.c_ubyte * len(encrypted)).from_buffer_copy(encrypted)
    source = Blob(len(encrypted), buffer)
    target = Blob()
    if not crypt32.CryptUnprotectData(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(target)):
        raise SettingsError('cannot_decrypt_saved_settings')
    try:
        if target.size > MAX_SETTINGS_BYTES:
            raise SettingsError('invalid_saved_settings')
        return ctypes.string_at(target.data, target.size)
    finally:
        ctypes.memset(target.data, 0, target.size)
        kernel32.LocalFree(target.data)


def read_settings(path=None):
    path = settings_path() if path is None else Path(path)
    if path is None:
        return {}
    try:
        with path.open('rb') as stream:
            encrypted = stream.read(MAX_SETTINGS_BYTES + 1)
    except FileNotFoundError:
        return {}
    except OSError:
        raise SettingsError('cannot_read_saved_settings') from None
    if not encrypted or len(encrypted) > MAX_SETTINGS_BYTES:
        raise SettingsError('invalid_saved_settings')
    return parse_payload(unprotect(encrypted))
