import socket,time
host,port='127.0.0.1',2159
def calc(p):return sum(p)%256
def read_packet(s, timeout=4):
 s.settimeout(timeout)
 deadline=time.monotonic()+timeout
 buf=b''
 while time.monotonic()<deadline:
  c=s.recv(1)
  if not c:raise ConnectionError('Remote closed')
  if c==b'$':break
  if c not in (b'+',b'-'):buf+=c
 data=bytearray()
 while True:
  c=s.recv(1)
  if c==b'#':break
  if not c:raise ConnectionError('Remote closed within packet')
  data.extend(c)
 checksum=s.recv(2)
 s.sendall(b'+')
 return data.decode('latin1','replace')
def query(s,txt):
 p=txt.encode();s.sendall(b'$'+p+b'#'+f'{calc(p):02x}'.encode())
 return read_packet(s)
with socket.create_connection((host,port),timeout=4) as s:
 for q in ['qSupported:swbreak+;hwbreak+','?','qfThreadInfo','qC']:
  try:print('COMMAND',q,'RESPONSE',query(s,q)[:350])
  except Exception as e:print('ERR',type(e).__name__,str(e));break

