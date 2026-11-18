from pathlib import Path
import struct
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen\core25\lib\libcobalt.so')
b=p.read_bytes()
e=struct.unpack_from("<16sHHIIIIIHHHHHH",b)
ph=[struct.unpack_from("<IIIIIIII",b,e[5]+i*e[9]) for i in range(e[10])]
def view(v,n):
 for p in ph:
  if p[0]==1 and p[2]<=v and v+n<=p[2]+p[4]:return memoryview(b)[p[1]+v-p[2]:p[1]+v-p[2]+n]
 raise Exception(hex(v))
dph=next(p for p in ph if p[0]==2)
dyn=dict(struct.unpack_from("<II",b,dph[1]+i*8) for i in range(dph[4]//8))
print('PLT_ADDRESS_SYMBOLS')
for x in [0x1f9cbf0,0x1f9cca0,0x1f9cbf0+11*16,0x1f9cbf0+12*16,0x1f9cce0,0x1f9cc10]:
 i=(x-0x1f9cbf0)//16
 off,inf=struct.unpack('<II',view(dyn[23]+i*8,8))
 sym=struct.unpack('<IIIBBH',view(dyn[6]+(inf>>8)*16,16))
 name=view(dyn[5]+sym[0],180).tobytes().split(b'\0')[0].decode()
 print(hex(x),'PLT_INDEX',i,'GOT',hex(off),'NAME',name,'TYPE',inf&255)

