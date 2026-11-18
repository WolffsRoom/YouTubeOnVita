from pathlib import Path
import struct
f=Path(r"C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen\core25\lib\libcobalt.so").read_bytes()
addr=0x1ffaeac
# in RW PT_LOAD from vaddr 0x1faf570 file offset 0x1f8f570
off=0x1f8f570+(addr-0x1faf570)
a=struct.unpack_from("<74I",f,off)
print("INIT_ARRAY_RAW_COUNT",len(a),"ZERO",sum(x==0 for x in a),"LOWEST",hex(min(a)),"HIGHEST",hex(max(a)),"THUMB",sum(x&1 for x in a))
for i,x in enumerate(a[:19]): print("CTOR",i,hex(x))
