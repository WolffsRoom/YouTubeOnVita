from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\port_runtime.cpp')
s=p.read_text(encoding='utf-8-sig')
old='bool trace=enabled && seq<=250;'
new='bool trace=enabled && (seq<=20 || (seq>=1000 && seq<=1500));'
assert old in s;s=s.replace(old,new)
p.write_text(s,encoding='utf-8')
print('TRACE_RANGE 1000 THROUGH 1500 ENABLED')

