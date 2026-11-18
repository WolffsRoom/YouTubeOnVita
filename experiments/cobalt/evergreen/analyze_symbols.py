import re, pathlib
root=pathlib.Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen')
lines=(root/'dynsym.txt').read_text().splitlines()
pairs=[]
for x in lines:
    m=re.search(r'^\s*\d+:\s+[0-9A-Fa-f]+\s+\d+\s+\w+\s+\w+\s+\w+\s+(\w+)\s+(.+)$',x)
    if m:pairs.append((m.group(1),m.group(2).strip()))
defined=[n for loc,n in pairs if loc!='UND']
undefined=sorted(set(n for loc,n in pairs if loc=='UND'))
print('DEFINED_EXPORTS',len(defined))
print('INTERESTING',[x for x in defined if any(y in x.lower() for y in ('starboardmain','cobalt','application','start','main'))][:65])
print('UNDEFINED_TOTAL',len(undefined))
print('API_Sb',len([x for x in undefined if x.startswith('Sb')]))
print('CONFIG',len([x for x in undefined if not x.startswith('Sb')]))
vitadir=pathlib.Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita')
source='\n'.join((vitadir/x).read_text(errors='ignore') for x in ('platform15.cpp','port_window.cpp'))
implemented=sorted(x for x in undefined if re.search(r'\b'+re.escape(x)+r'\s*\(',source))
print('IMPLEMENTED_IN_SUBSET',len(implemented),implemented)
print('UNIMPLEMENTED',len(undefined)-len(implemented))
print('TOP_MISSING',[x for x in undefined if x not in implemented][:60])
(root/'external_requirements.txt').write_text('\n'.join(undefined)+'\n',encoding='utf-8')

