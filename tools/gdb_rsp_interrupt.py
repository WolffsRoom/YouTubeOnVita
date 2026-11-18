from pathlib import Path
import socket,time,json
host='127.0.0.1';port=2159
def send(s,txt):
 b=txt.encode('ascii');s.sendall(b'$'+b+b'#'+f'{sum(b)%256:02x}'.encode())
def read_packet(s, timeout=7):
 s.settimeout(timeout)
 while True:
  c=s.recv(1)
  if not c:raise ConnectionError('remote closed')
  if c==b'$':break
 data=bytearray()
 while True:
  c=s.recv(1)
  if c==b'#':break
  if not c:raise ConnectionError('remote closed in packet')
  data+=c
 checksum=s.recv(2)
 s.sendall(b'+')
 return data.decode('latin1','replace')
def cmd(s,text,timeout=7):
 send(s,text)
 return read_packet(s,timeout)
def registers(hexdata):
 try:
  reg=bytes.fromhex(hexdata[:128])
  return {name:hex(int.from_bytes(reg[i*4:(i+1)*4],'little')) for i,name in ((0,'r0'),(1,'r1'),(2,'r2'),(3,'r3'),(11,'fp'),(13,'sp'),(14,'lr'),(15,'pc'))}
 except Exception as e:return {'invalid':str(e),'len':len(hexdata)}
folder=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\output\gdb_cobalt')
folder.mkdir(parents=True,exist_ok=True)
records=[]
with socket.create_connection((host,port),timeout=8) as s:
 print('RSP',cmd(s,'qSupported:swbreak+;hwbreak+')[:230])
 print('STOPPED_BEFORE_START',cmd(s,'?')[:160])
 print('TARGET',cmd(s,'qfThreadInfo'))
 print('BEGIN_CONTINUE')
 send(s,'c')
 time.sleep(12)
 print('REQUEST_INTERRUPT')
 s.sendall(b'\x03')
 response=read_packet(s,8)
 print('INTERRUPTED',response[:150])
 for c in ('qfThreadInfo','qsThreadInfo'):
  txt=cmd(s,c)
  print('THREADS',c,txt[:300])
  if txt.startswith('m'):
   ids=[x for x in txt[1:].split(',') if x]
   for th in ids[:24]:
    status=cmd(s,'Hg'+th)
    if status=='OK':
     regdata=cmd(s,'g')
     result={'thread':th,'regs':registers(regdata)}
    else:result={'thread':th,'err':status}
    print('REG',result)
    records.append(result)
 outfile=folder/'last_start_pc.json'
 outfile.write_text(json.dumps(records,indent=2),encoding='utf-8')
 print('RESULT_FILE',outfile)

