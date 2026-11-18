"""Inject a diagnostic DLL into an explicitly owned test process; 64-bit Windows only.
This script does not patch binaries on disk or modify any persistent emulator files.
"""
import ctypes,ctypes.wintypes as wintypes,sys
from pathlib import Path
k=ctypes.WinDLL("kernel32",use_last_error=True)
k.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
k.OpenProcess.restype=wintypes.HANDLE
k.GetModuleHandleW.argtypes=[wintypes.LPCWSTR]
k.GetModuleHandleW.restype=wintypes.HMODULE
k.GetProcAddress.argtypes=[wintypes.HMODULE,ctypes.c_char_p]
k.GetProcAddress.restype=ctypes.c_void_p
k.VirtualAllocEx.argtypes=[wintypes.HANDLE,ctypes.c_void_p,ctypes.c_size_t,wintypes.DWORD,wintypes.DWORD]
k.VirtualAllocEx.restype=ctypes.c_void_p
k.WriteProcessMemory.argtypes=[wintypes.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
k.WriteProcessMemory.restype=wintypes.BOOL
k.CreateRemoteThread.argtypes=[wintypes.HANDLE,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD)]
k.CreateRemoteThread.restype=wintypes.HANDLE
k.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD]
k.WaitForSingleObject.restype=wintypes.DWORD
k.GetExitCodeThread.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)]
k.GetExitCodeThread.restype=wintypes.BOOL
k.VirtualFreeEx.argtypes=[wintypes.HANDLE,ctypes.c_void_p,ctypes.c_size_t,wintypes.DWORD]
k.VirtualFreeEx.restype=wintypes.BOOL
k.CloseHandle.argtypes=[wintypes.HANDLE]
k.CloseHandle.restype=wintypes.BOOL
def check(v,msg):
 if not v:raise OSError(ctypes.get_last_error(),msg)
 return v
def inject(pid:int,path:Path):
 path=path.resolve()
 assert path.is_file()
 rights=0x0002|0x0008|0x0010|0x0020|0x0400
 handle=check(k.OpenProcess(rights,False,pid),"OpenProcess")
 try:
  data=str(path).encode('utf-16le')+b'\x00\x00'
  mem=check(k.VirtualAllocEx(handle,None,len(data),0x1000|0x2000,0x04),'VirtualAllocEx')
  try:
   buf=ctypes.create_string_buffer(data)
   written=ctypes.c_size_t()
   check(k.WriteProcessMemory(handle,mem,buf,len(data),ctypes.byref(written)),'WriteProcessMemory')
   assert written.value==len(data)
   lib=check(k.GetModuleHandleW('kernel32.dll'),'GetModuleHandle')
   proc=check(k.GetProcAddress(lib,b'LoadLibraryW'),'GetProcAddress')
   t=check(k.CreateRemoteThread(handle,None,0,proc,mem,0,None),'CreateRemoteThread')
   try:
    wait=k.WaitForSingleObject(t,8000)
    assert wait==0, ('DLL load thread timed out',wait)
    code=wintypes.DWORD()
    check(k.GetExitCodeThread(t,ctypes.byref(code)),'GetExitCodeThread')
    print('DLL_LOAD_RETURN_32BIT',hex(code.value),flush=True)
    assert code.value!=0,'Remote LoadLibraryW returned NULL'
   finally:k.CloseHandle(t)
  finally:k.VirtualFreeEx(handle,mem,0,0x8000)
 finally:k.CloseHandle(handle)
if __name__=='__main__':
 inject(int(sys.argv[1]),Path(sys.argv[2]))
