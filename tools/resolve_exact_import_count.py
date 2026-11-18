from pathlib import Path
import re,runpy
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
lookup=(r/'experiments/cobalt/vita/import_lookup.cpp').read_text()
exports=set(re.findall(r'\{"(Sb[A-Za-z0-9_]+)"',lookup))
d=runpy.run_path(str(r/'experiments/cobalt/evergreen/symbol_frequency.py'))
freq=d['freq']
linked=sum(v for k,v in freq.items() if k in exports)
missing=sum(v for k,v in freq.items() if k not in exports)
print('OBSERVED_ELF_IMPORTS',sum(freq.values()))
print('REAL_LOOKUP_ENTRIES',len(exports))
print('ELF_RESOLVED',linked)
print('ELF_UNRESOLVED',missing)
print('UNRESOLVED_NAMES',len([k for k in freq if k not in exports]))
assert linked+missing==211

