from pathlib import Path
import struct,collections
b=Path(r"C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen\core25\lib\libcobalt.so").read_bytes()
eh=struct.unpack_from("<16sHHIIIIIHHHHHH",b)
ph=[struct.unpack_from("<IIIIIIII",b,eh[5]+i*eh[9]) for i in range(eh[10])]
dyn=next(p for p in ph if p[0]==2)
d={t:v for t,v in [struct.unpack_from("<II",b,dyn[1]+i*8) for i in range(dyn[4]//8)]}
def view(v,n):
 for p in ph:
  if p[0]==1 and p[2]<=v and v+n<=p[2]+p[4]:
   return memoryview(b)[p[1]+v-p[2]:p[1]+v-p[2]+n]
 raise ValueError(hex(v))
freq=collections.Counter()
for ty,sz in [(17,18),(23,2)]:
 rel=view(d[ty],d[sz])
 for i in range(0,len(rel),8):
  off,info=struct.unpack_from("<II",rel,i)
  t=info&255; sym=info>>8
  if t not in (21,22):continue
  st=struct.unpack("<IIIBBH",view(d[6]+sym*16,16))
  if st[5]!=0:continue
  s=view(d[5]+st[0],180).tobytes().split(b"\x00")[0].decode("ascii")
  freq[s]+=1
wanted="SbLog SbLogRaw SbSystemGetPath SbTimeGetMonotonicNow SbTimeGetMonotonicThreadNow SbTimeGetNow SbWindowCreate SbWindowDestroy SbWindowGetDiagonalSizeInInches SbWindowGetPlatformHandle SbWindowGetSize SbWindowSetDefaultOptions".split()
print("EFFECTIVE_COUNT",len(freq),sum(freq.values()))
print("OUR_API_HITS",[(name,freq.get(name,0)) for name in wanted])
print("REMAINING",sum(freq.values())-sum(freq.get(s,0) for s in wanted))
