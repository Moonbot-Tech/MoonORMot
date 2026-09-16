"""Native pointers: tests and timings call the public mORMot entries, without wrapper calls."""
import ctypes as C
from pathlib import Path

class Library:
    def __init__(self, path):
        self.path = Path(path).resolve()
        self.lib = C.CDLL(str(self.path))
        self.lib.ParserAddress.argtypes = [C.c_int]
        self.lib.ParserAddress.restype = C.c_void_p
        self.lib.SetRounding.argtypes = [C.c_int]
        self.addresses = [self.lib.ParserAddress(i) for i in range(5)]
        self.string = C.CFUNCTYPE(C.c_double, C.c_void_p, C.POINTER(C.c_int))(self.addresses[0])
        self.json = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p, C.c_bool)(self.addresses[1])
        self.bounded = C.CFUNCTYPE(C.c_int64, C.c_void_p, C.c_void_p)(self.addresses[2])
        self.text = C.CFUNCTYPE(C.c_int64, C.c_void_p)(self.addresses[3])
        self.checked = C.CFUNCTYPE(C.c_int64, C.c_void_p, C.POINTER(C.c_int))(self.addresses[4])
        self.lib.SetRounding(0)

class Pages:
    """Two readable pages followed by a guard, for exact ending-byte placement."""
    def __init__(self):
        import os
        self.windows = os.name == 'nt'
        if self.windows:
            k = C.WinDLL('kernel32', use_last_error=True)
            k.VirtualAlloc.restype = C.c_void_p
            self.address = k.VirtualAlloc(None, 12288, 0x3000, 4)
            old = C.c_ulong()
            if not self.address or not k.VirtualProtect(C.c_void_p(self.address + 8192), 4096, 1, C.byref(old)):
                raise C.WinError(C.get_last_error())
            self.system = k
        else:
            k = C.CDLL(None, use_errno=True)
            k.mmap.restype = C.c_void_p
            self.address = k.mmap(None, 12288, 3, 0x22, -1, 0)
            if self.address == C.c_void_p(-1).value or k.mprotect(C.c_void_p(self.address + 8192), 4096, 0):
                raise OSError(C.get_errno(), 'mmap/mprotect')
            self.system = k

    def put(self, raw, tail=False, offset=0):
        assert len(raw) < 8000
        address = self.address + (8191 - len(raw) if tail else offset)
        C.memmove(address, raw + b'\0', len(raw) + 1)
        return address

    def close(self):
        if self.windows:
            self.system.VirtualFree(C.c_void_p(self.address), 0, 0x8000)
        else:
            self.system.munmap(C.c_void_p(self.address), 12288)
