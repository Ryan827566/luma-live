"""Read-only thread origins for an owned Windows test process.

No suspension, stack/memory dumps, debugger attachment or symbol downloads.
NtQueryInformationThread is optional diagnostic evidence, never an acceptance
gate. See https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntqueryinformationthread

The caller supplies IDs from its owned child's count snapshot. These later
queries are not atomic with that snapshot. Match (thread_id, created_filetime)
across observations; neither module attribution nor a stable handle count
proves that a thread is harmless. No result from this module changes a gate.
"""
import ctypes as c
from ctypes import wintypes as w
from pathlib import PureWindowsPath

kernel = c.WinDLL('kernel32', use_last_error=True)
try:
    ntdll = c.WinDLL('ntdll')
except OSError:
    ntdll = None


class ModuleEntry(c.Structure):
    _fields_ = [(name, w.DWORD) for name in (
        'dwSize', 'th32ModuleID', 'th32ProcessID', 'GlblcntUsage', 'ProccntUsage')]
    _fields_ += [('modBaseAddr', c.c_void_p), ('modBaseSize', w.DWORD),
                ('hModule', w.HMODULE), ('szModule', w.WCHAR * 256),
                ('szExePath', w.WCHAR * 260)]


for name, args, result in [
    ('CreateToolhelp32Snapshot', [w.DWORD, w.DWORD], w.HANDLE),
    ('Module32FirstW', [w.HANDLE, c.POINTER(ModuleEntry)], w.BOOL),
    ('Module32NextW', [w.HANDLE, c.POINTER(ModuleEntry)], w.BOOL),
    ('OpenThread', [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
    ('GetProcessIdOfThread', [w.HANDLE], w.DWORD),
    ('GetThreadTimes', [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4, w.BOOL),
    ('CloseHandle', [w.HANDLE], w.BOOL),
]:
    fn = getattr(kernel, name)
    fn.argtypes, fn.restype = args, result

query_start = getattr(ntdll, 'NtQueryInformationThread', None)
if query_start is not None:
    query_start.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.ULONG, c.c_void_p]
    query_start.restype = w.LONG


def module_ranges(pid):
    # Retry only ERROR_BAD_LENGTH, with a fixed bound. Never emit full paths.
    for _ in range(3):
        snapshot = kernel.CreateToolhelp32Snapshot(0x18, pid)
        if snapshot != c.c_void_p(-1).value:
            break
        error = c.get_last_error()
        if error != 24:
            return [], error
    else:
        return [], 24
    rows, entry = [], ModuleEntry()
    entry.dwSize = c.sizeof(entry)
    try:
        available = kernel.Module32FirstW(snapshot, c.byref(entry))
        while available:
            if entry.th32ProcessID != pid:
                return rows, 87  # A module from another process is not evidence.
            rows.append((int(entry.modBaseAddr or 0), int(entry.modBaseSize),
                         PureWindowsPath(entry.szModule).name.lower()))
            entry.dwSize = c.sizeof(entry)
            available = kernel.Module32NextW(snapshot, c.byref(entry))
        error = c.get_last_error()
        return rows, None if error == 18 and rows else error
    finally:
        kernel.CloseHandle(snapshot)


def filetime(value):
    return (value.dwHighDateTime << 32) | value.dwLowDateTime


def locate(address, modules):
    for base, size, name in modules:
        if base <= address < base + size:
            return {'start_module': name, 'start_rva': hex(address - base)}
    return {'start_unmapped': True}


def thread_origins(pid, thread_ids):
    """Best-effort metadata only; ordinary query errors never escape to gates."""
    result = {'threads': [], 'complete': True, 'atomic_with_count_snapshot': False}
    try:
        _thread_origins(pid, thread_ids, result)
    except Exception as error:
        # Exception messages can include paths or addresses; emit only the type.
        result.update(complete=False, error_type=type(error).__name__)
    return result


def _thread_origins(pid, thread_ids, result):
    try:
        modules, module_error = module_ranges(pid)
        if module_error is not None:
            result.update(complete=False, module_snapshot_error=module_error)
    except Exception as error:
        modules = []
        result.update(complete=False, module_error_type=type(error).__name__)
    for tid in thread_ids:
        row = {'thread_id': tid}
        result['threads'].append(row)
        # QUERY_INFORMATION only. A TID may have exited or been recycled after
        # enumeration; verify process ownership and record creation time.
        handle = kernel.OpenThread(0x40, False, tid)
        if not handle:
            row['open_error'] = c.get_last_error()
            result['complete'] = False
            continue
        try:
            owner = kernel.GetProcessIdOfThread(handle)
            if owner != pid:
                if not owner:
                    row['owner_query_error'] = c.get_last_error()
                else:
                    row['owner_changed'] = True
                result['complete'] = False
                continue
            created, exited, kernel_time, user_time = (w.FILETIME() for _ in range(4))
            if kernel.GetThreadTimes(handle, c.byref(created), c.byref(exited),
                                     c.byref(kernel_time), c.byref(user_time)):
                row.update(created_filetime=filetime(created), exited_filetime=filetime(exited),
                           kernel_100ns=filetime(kernel_time), user_100ns=filetime(user_time))
                if row['exited_filetime']:
                    result['complete'] = False
            else:
                row['times_error'] = c.get_last_error()
                result['complete'] = False
            address = c.c_void_p()
            if query_start is None:
                row['start_query_unavailable'] = True
                result['complete'] = False
                continue
            status = query_start(handle, 9, c.byref(address), c.sizeof(address), None)
            if status == 0 and address.value:
                row.update(locate(address.value, modules))
                if row.get('start_unmapped'):
                    result['complete'] = False
            else:
                row['start_query_status'] = int(status)
                result['complete'] = False
        except Exception as error:
            row['error_type'] = type(error).__name__
            result['complete'] = False
        finally:
            if not kernel.CloseHandle(handle):
                row['close_error'] = c.get_last_error()
                result['complete'] = False
