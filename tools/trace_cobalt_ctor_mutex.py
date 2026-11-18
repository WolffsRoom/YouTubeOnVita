from pathlib import Path
import struct
b=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen\core25\lib\libcobalt.so').read_bytes();e=struct.unpack_from('<16sHHIIIIIHHHHHH',b);ph=[struct.unpack_from('<IIIIIIII',b,e[5]+i*e[9]) for i in range(e[10])]
def view(v,n):
 for p in ph:
  if p[0]==1 and p[2]<=v and v+n<=p[2]+p[4]:return memoryview(b)[p[1]+v-p[2]:p[1]+v-p[2]+n]
 raise ValueError(hex(v))
dph=next(p for p in ph if p[0]==2)
d=dict(struct.unpack_from('<II',b,dph[1]+i*8) for i in range(dph[4]//8))
for plt in [0x1f9df00,0x1f9d3e0,0x1f9dda0,0x1f9cd40,0x1f9ec20]:
 i=(plt-0x1f9cbf0)//16
 off,info=struct.unpack('<II',view(d[23]+i*8,8))
 sym=struct.unpack('<IIIBBH',view(d[6]+(info>>8)*16,16))
 name=view(d[5]+sym[0],180).tobytes().split(b'\0')[0].decode()
 print(hex(plt),'symbol',name,'type',info&255,'internal',sym[5]!=0,'value',hex(sym[1]))

