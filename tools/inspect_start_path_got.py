from pathlib import Path
import struct
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen\core25\lib\libcobalt.so')
b=p.read_bytes(); e=struct.unpack_from('<16sHHIIIIIHHHHHH',b)
ph=[struct.unpack_from('<IIIIIIII',b,e[5]+i*e[9]) for i in range(e[10])]
def view(v,n):
 for p in ph:
  if p[0]==1 and p[2]<=v and v+n<=p[2]+p[4]:
   return memoryview(b)[p[1]+v-p[2]:p[1]+v-p[2]+n]
 raise ValueError(hex(v))
dph=next(p for p in ph if p[0]==2)
d=dict(struct.unpack_from('<II',b,dph[1]+i*8) for i in range(dph[4]//8))
slots={
 'prior_char':0x16643fc+0x998a30,
 'replacement_char':0x1664408+0x998900,
 'prefix_global':0x166439c+0x009cfc24,
}
relo=[]
for start,length in ((d[17],d[18]),(d[23],d[2])):
 for i in range(length//8):
  off,inf=struct.unpack('<II',view(start+i*8,8))
  if off in slots.values():
   typ=inf&255;ix=inf>>8
   sym=struct.unpack('<IIIBBH',view(d[6]+ix*16,16))
   name=view(d[5]+sym[0],220).tobytes().split(b'\0')[0].decode()
   relo.append((hex(off),typ,name,hex(sym[1]),sym[5]))
for name,addr in slots.items():
 print(name,hex(addr),'raw_bytes',view(addr,4).tobytes().hex(),'relocation',[row for row in relo if row[0]==hex(addr)])

