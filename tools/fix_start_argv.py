from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_lifecycle.cpp'
s=p.read_text(encoding='utf-8-sig')
old='''  SbEventStartData info={};
  info.argument_count=0;info.argument_values=nullptr;info.link=nullptr;'''
new='''  // Cobalt command-line bootstrap expects argc>=1 and argv[0].
  static char app_name[]="youtubevita";
  static char* args[]={app_name,nullptr};
  SbEventStartData info={};
  info.argument_count=1;
  info.argument_values=args;
  info.link=nullptr;'''
assert old in s
p.write_text(s.replace(old,new),encoding='utf-8')
print('STARBOARD_START_ARGV0_SET')

