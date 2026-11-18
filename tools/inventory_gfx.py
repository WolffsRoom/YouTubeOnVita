import pathlib,re,json
r=pathlib.Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
s=r/'experiments/cobalt/upstream-25/starboard'
symbols=(r/'experiments/cobalt/vita/vitagl_symbols.txt').read_text()
exports=set(re.findall(r'(?m)^[0-9a-fA-F]+ [A-Z] (\w+)$',symbols))
data={}
for key,h,struct in [('egl','egl.h','SbEglInterface'),('gles','gles.h','SbGlesInterface')]:
    txt=(s/h).read_text()
    txt=txt[txt.index('typedef struct '+struct):txt.index('} '+struct+';')]
    names=re.findall(r'\(\*(\w+)\)\s*\(',txt)
    present=[n for n in names if n in exports]
    missing=[n for n in names if n not in exports]
    data[key]={'fields':names,'available':present,'missing':missing}
    print(key.upper(),'fields',len(names),'available',len(present),'missing',len(missing))
    print('REQUIRED_MISSING_GLES2:',[n for n in missing if n.startswith('gl')][:14] if key=='gles' else missing[:20])
(r/'experiments/cobalt/vita/vitagl_api_inventory.json').write_text(json.dumps(data,indent=2)+'\n')

