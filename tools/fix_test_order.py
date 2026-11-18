from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\main_test.cpp')
s=p.read_text(encoding='utf-8')
begin=s.index('  // Real Vita filesystem flush + truncation, exercised only after readback.')
end=s.index('  // Synchronization: test nonrecursive mutex',begin)
block=s[begin:end]
s=s[:begin]+s[end:]
needle='  check(SbFileDelete(filepath),"Starboard file is deleted from Vita filesystem");'
assert needle in s
s=s.replace(needle,block+needle)
p.write_text(s,encoding='utf-8')
print('TEST_ORDER_FIXED')

