import struct, pathlib,collections
p=pathlib.Path(r"C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen\core25\lib\libcobalt.so")
b=p.read_bytes(); hdr=struct.unpack_from("<16sHHIIIIIHHHHHH",b,0); _,etype,machine,version,entry,phoff,shoff,flags,ehsize,phentsize,phnum,*rest=hdr
phdrs=[struct.unpack_from("<IIIIIIII",b,phoff+i*phentsize) for i in range(phnum)]
print("ELF",etype,machine,"PHDR",phnum)
for ph in phdrs:
    if ph[0] in (1,2):print("PT",ph[0],"offset/vaddr/filesz/memsz",*[hex(q) for q in (ph[1],ph[2],ph[4],ph[5])],"flags",ph[6])
dyn=[ph for ph in phdrs if ph[0]==2][0]
d={}
for i in range(dyn[4]//8):
  tag,val=struct.unpack_from("<iI",b,dyn[1]+i*8)
  if tag==0:break
  d[tag]=val
print("dyn",[(hex(x),hex(y)) for x,y in d.items()])
def extract(vaddr,size):
 for ph in phdrs:
  if ph[0]==1 and ph[2]<=vaddr and vaddr+size<=ph[2]+ph[4]:
   return memoryview(b)[ph[1]+vaddr-ph[2]:ph[1]+vaddr-ph[2]+size]
 raise ValueError(hex(vaddr))
rel=extract(d[17],d[18])
plt=extract(d[23],d[2])
counts=collections.Counter(); bad=collections.Counter(); syms=collections.Counter(); ranges=collections.Counter(); samples=[]
for table in (rel,plt):
 for off in range(0,len(table),8):
  addr,info=struct.unpack_from("<II",table,off)
  typ=info&255;sym=info>>8
  counts[typ]+=1
  target=any(x[0]==1 and x[2]<=addr< x[2]+x[5] for x in phdrs)
  if not target:bad[typ]+=1
  if typ in (2,21,22):
   st=struct.unpack("<IIIBBH",extract(d[6]+sym*16,16))
   syms[(typ,'defined' if st[5]!=0 else 'undefined')]+=1
   if len(samples)<20:samples.append((typ,sym,hex(addr),st[5],hex(st[1])))
  ranges[(typ,addr>>16)]+=1
print("COUNTS",counts,"INVALID_TARGETS",bad,"SYM",syms)
print("SAMPLES",samples)
print("N_RELATIVE",counts[23],"RELATIVE_HEAD",[(hex(*struct.unpack_from("<II",rel,i*8)[:1]),) for i in range(0)] )