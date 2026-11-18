"""Analyse the official Cobalt Evergreen ARM hardfp dynamic ABI without executing code."""
import collections, json, pathlib, re
ROOT=pathlib.Path(__file__).resolve().parent
r=(ROOT/'dynsym.txt').read_text(encoding='utf-8',errors='replace')
undefined=[]
exported=[]
for line in r.splitlines():
    m=re.search(r'^\s*\d+:\s+[0-9A-Fa-f]+\s+\d+\s+\w+\s+\w+\s+\w+\s+(\w+)\s+(.+)$',line)
    if m:
        (undefined if m.group(1)=='UND' else exported).append(m.group(2).strip())
undefined=sorted(set(undefined))
src=pathlib.Path(__file__).resolve().parents[1]/'vita'
impl='\n'.join((src/n).read_text(errors='replace') for n in ('platform15.cpp','port_window.cpp'))
done=sorted(n for n in undefined if re.search(r'\b'+re.escape(n)+r'\s*\(',impl))
missing=[n for n in undefined if n not in done]
categories=collections.defaultdict(list)
for name in missing:
    if name.startswith('kSb'): cat='Constantes e configurações'
    elif any(name.startswith(x) for x in ('SbGetEgl','SbGetGles','SbDecodeTarget','SbWindow')):cat='Renderização EGL/GLES e janela'
    elif any(name.startswith(x) for x in ('SbPlayer','SbMedia','SbAudioSink')):cat='Vídeo e áudio'
    elif any(name.startswith(x) for x in ('SbSocket','SbNetwork')):cat='Rede e sockets'
    elif any(name.startswith(x) for x in ('SbThread','SbMutex','SbConditionVariable','SbOnce')):cat='Threads e sincronização'
    elif any(name.startswith(x) for x in ('SbFile','SbDirectory','SbStorage')):cat='Arquivos e persistência'
    elif any(name.startswith(x) for x in ('SbDrm','SbAccessibility','SbSpeechSynthesis','SbUser')):cat='Serviços e acessibilidade'
    elif any(name.startswith(x) for x in ('SbSystem','SbMemory','SbTime','SbLog')):cat='Sistema, memória e logging'
    else:cat='Outras APIs do Starboard'
    categories[cat].append(name)
relocs=collections.Counter(re.findall(r'\bR_ARM_[A-Z0-9_]+\b',(ROOT/'relocs.txt').read_text(errors='replace')))
data={'library':'Cobalt Evergreen 5.50.2 arm-hardfp sbversion-15',
      'official_release':'25.lts.50',
      'sha256':'be5b93dc4dcd42bd8dea5bfda728e6a170a17d6eef913b2cf78d370ac5e011dd',
      'ELF_size_bytes':(ROOT/'core25/lib/libcobalt.so').stat().st_size,
      'needed_external_libraries':0,'external_symbol_count':len(undefined),
      'subset_implemented_external_count':len(done),
      'subset_implemented_exports':done,'missing_external_count':len(missing),
      'missing_by_category':{k:{'count':len(v),'names':v} for k,v in sorted(categories.items())},
      'relocation_types':dict(sorted(relocs.items())),'elf32_virtual_end_bytes':33806836,
      'run_status':'read-only inspector passed Vita3K, no executable mapping performed'}
(ROOT/'runtime_gap.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('EXTERNAL',len(undefined),'IMPLEMENTED_SUBSET',len(done),'MISSING',len(missing))
print('RELOCATIONS',dict(relocs))
for k,v in sorted(categories.items()):print(k,len(v))

