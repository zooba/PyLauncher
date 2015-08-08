import ctypes
import ctypes.wintypes

import msvcrt
import os

from builtins import open as _open

_CreateFileW = ctypes.windll.kernel32.CreateFileW
_CreateFileW.argtypes = [
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.DWORD,
    ctypes.wintypes.DWORD,
    ctypes.wintypes.LPCVOID,
    ctypes.wintypes.DWORD,
    ctypes.wintypes.DWORD,
    ctypes.wintypes.HANDLE,
]
_CreateFileW.restype = ctypes.wintypes.HANDLE

def open(path, writable=False, encoding='utf-8', append=False, delete_on_close=False):
    attrib = 0x00000080 # FILE_ATTRIBUTES_NORMAL

    if append:
        mode = 'a'
        flags = os.O_APPEND
        access = 0x40000000 # GENERIC_WRITE
        creation = 1 # CREATE_NEW
        share = 1 # FILE_SHARE_READ
    elif writable:
        mode = 'w+'
        flags = os.O_RDWR
        access = 0x40000000 # GENERIC_WRITE
        creation = 2 # CREATE_ALWAYS
        share = 1 # FILE_SHARE_READ
    else:
        mode = 'r'
        flags = os.O_RDONLY
        access = 0x80000000 # GENERIC_READ
        creation = 3 # OPEN_EXISTING
        share = 3 # FILE_SHARE_READ|WRITE

    if encoding is None:
        mode += 'b'
    else:
        flags |= os.O_TEXT

    if delete_on_close:
        flags |= os.O_TEMPORARY
        attrib |= 0x04000000 # FILE_FLAG_DELETE_ON_CLOSE
        share |= 4 # FILE_SHARE_DELETE

    handle = _CreateFileW(path, access, share, None, creation, 0x00000080, None)
    if handle == 0xFFFFFFFF:
        raise IOError("Unable to open file: 0x{:08X}".format(ctypes.GetLastError()))

    fd = msvcrt.open_osfhandle(handle, flags & 0xFFFFFFFF)
    f = _open(fd, mode, encoding=encoding)
    f.path = path
    return f
