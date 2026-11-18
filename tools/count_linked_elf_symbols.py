from pathlib import Path
import runpy,re
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
source=(r/'experiments/cobalt/vita/import_lookup.cpp').read_text(encoding='utf-8')
exports=set(re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,',source))
d=runpy.run_path(str(r/'experiments/cobalt/evergreen/symbol_frequency.py'))
freq=d['freq']
linked=sum(v for k,v in freq.items() if k in exports)
missing=sum(v for k,v in freq.items() if k not in exports)
print('LOOKUP_EXPORTS',len(exports),'ELF_REFERENCES_LINKED',linked,'ELF_IMPORTS_MISSING',missing)
print('UNRESOLVED_FUNCTIONS',len([k for k in freq if k.startswith('Sb') and k not in exports]))
print('UNRESOLVED_VARIABLES',len([k for k in freq if k.startswith('kSb') and k not in exports]))
print('TOP_MISSING',sorted(x for x in freq if x not in exports)[:12])
assert linked+missing==211

