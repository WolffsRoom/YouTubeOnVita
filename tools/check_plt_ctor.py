from pathlib import Path
import struct
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen\core25\lib\libcobalt.so')
b=p.read_bytes(); e=struct.unpack_from('<16sHHIIIIIHHHHHH',b)
ph=[struct.unpack_from('<IIIIIIII',b,e[5]+i*e[9]) for i in range(e[10])]
def view(v,n):
 for p in ph:
  if p[0]==1 and p[2]<=v and v+n<=p[2]+p[4]:return memoryview(b)[p[1]+v-p[2]:p[1]+v-p[2]+n]
 raise ValueError(hex(v))
dph=next(p for p in ph if p[0]==2);d=dict(struct.unpack_from('<II',b,dph[1]+i*8) for i in range(dph[4]//8))
for i in (11,12):
 off,inf=struct.unpack('<II',view(d[23]+i*8,8))
 sym=struct.unpack('<IIIBBH',view(d[6]+(inf>>8)*16,16))
 print('SYM',i,'GOT',hex(off),'ST_VALUE',hex(sym[1]),'SHNDX',sym[-1],'ST_INFO',sym[-2],'ORIG_SLOT',hex(struct.unpack('<I',view(off,4))[0]))

