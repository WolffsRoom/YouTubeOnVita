import socket,time
HOST='127.0.0.1';PORT=2159
def checksum(b):return sum(b)&255
def getpkt(s,t=5):
 s.settimeout(t)
 while True:
  c=s.recv(1)
  if not c:raise EOFError
  if c==b'$':break
 data=bytearray()
 while True:
  c=s.recv(1)
  if c==b'#':break
  data+=c
 recvcs=s.recv(2)
 s.sendall(b'+')
 return data.decode('ascii','replace')
def cmd(s,t,timeout=5):
 data=t.encode();s.sendall(b'$'+data+b'#'+f'{checksum(data):02x}'.encode())
 return getpkt(s,timeout)
with socket.create_connection((HOST,PORT),timeout=5) as s:
 print('CONNECT',cmd(s,'qSupported'))
 print('INITIAL',cmd(s,'?'))
 print('THREADS',cmd(s,'qfThreadInfo'))
 print('CONTINUE_SENT',flush=True)
 data=b'c';s.sendall(b'$'+data+b'#'+f'{checksum(data):02x}'.encode())
 time.sleep(9)
 print('BREAK_SENT',flush=True)
 s.sendall(b'\x03')
 try:print('STOP',getpkt(s,7),flush=True)
 except Exception as e:print('STOP_ERROR',repr(e),flush=True)
 for q in ('qfThreadInfo','qC','p0f','p0d','p0e','g'):
  try: print('QUERY',q,'RESULT',cmd(s,q,5)[:120],flush=True)
  except Exception as e:print('ERR',q,str(e),flush=True);break

