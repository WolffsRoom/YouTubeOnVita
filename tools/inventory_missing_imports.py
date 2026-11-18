from pathlib import Path
import re,collections,json
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
source=(r/'experiments/cobalt/vita/import_lookup.cpp').read_text()
exported=set(re.findall(r'\{"(Sb[A-Za-z0-9_]+)"',source))
needed=set((r/'experiments/cobalt/evergreen/external_requirements.txt').read_text().split())
unimplemented=sorted(needed-exported)
print('EXPORT_SYMBOLS',len(exported),'NEEDED_SYMBOLS',len(needed),'UNIMPLEMENTED_SYMBOLS',len(unimplemented))
print('MISSING:')
print('\n'.join(unimplemented))
(r/'experiments/cobalt/evergreen/unimplemented_2026-11-16.txt').write_text('\n'.join(unimplemented)+'\n')

