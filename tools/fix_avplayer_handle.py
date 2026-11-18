from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\avplayer_probe.c')
s=p.read_text(encoding='utf-8-sig')
a='''  check(player>=0,"SceAvPlayer library created an actual playback handle");
  if(player<0)goto complete;'''
b='''  uint32_t handle=(uint32_t)player;
  int valid=handle>=0x81000000u && handle<0xA0000000u && (handle&3u)==0;
  check(valid,"SceAvPlayer returned a valid Vita handle (unsigned pointer ABI)");
  if(!valid)goto complete;'''
assert a in s
p.write_text(s.replace(a,b),encoding='utf-8')
print('VALID_HANDLE_CHECK_CORRECTED')

