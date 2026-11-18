from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/socket_tcp_test.cpp';s=p.read_text(encoding='utf-8-sig')
needle='''        static const char payload[]="VITA-COBALT-HTTP-PREREQ";'''
assert needle in s
s=s.replace(needle,'''        check(SbSocketIsConnectedAndIdle(accepted),
              "accepted idle TCP connection has no pending received data");
'''+needle)
needle='''        check(SbSocketIsConnected(client),"Starboard recognizes TCP client connection");'''
assert needle in s
s=s.replace(needle,'''        check(SbSocketIsConnectedAndIdle(accepted),
              "accepted TCP connection is idle after consuming received payload");
        check(!SbSocketSetTcpKeepAlive(accepted,true,10000000LL),
              "Vita SceNet rejects unsupported custom TCP keepalive interval");
        SbSocketAddress loop_src={},loop_mask={};
        check(SbSocketGetInterfaceAddress(&local,&loop_src,&loop_mask) &&
              loop_src.address[0]==127 && loop_mask.address[0]==255,
              "Vita Starboard detects loopback route and its real netmask");
'''+needle)
p.write_text(s,encoding='utf-8')
print('TCP_REGRESSION_EXTENDED: 4 new checks')

