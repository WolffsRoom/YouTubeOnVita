"""Isolated Vita3K smoke for v0.62 revised download metadata and quality controls.
Backs up the installed app and small non-download data and restores both.
"""
from pathlib import Path
import subprocess,shutil,time,zipfile,hashlib,os,re,sys
root=Path(__file__).resolve().parents[1]
variant=sys.argv[1] if len(sys.argv)>1 else 'full'
assert variant in ('full','lite')
title='YTVCB0001' if variant=='full' else 'YTVCB0002'
vpk='YouTube.vpk' if variant=='full' else 'YouTube-Lite.vpk'
profile=Path(os.environ['APPDATA'])/'Vita3K/Vita3K'
app=profile/'ux0/app'/title
data=profile/'ux0/data/youtubevita'
backup=root/('archives/v062-review/vita3k-backup-'+variant)
backup.mkdir(parents=True,exist_ok=True)
def running():
    q=subprocess.run(['tasklist','/FI','IMAGENAME eq Vita3K.exe'],
       capture_output=True,text=True,encoding='cp1252',errors='replace',timeout=13)
    return 'Vita3K.exe' in (q.stdout or '')
assert not running(),'Vita3K running; refusing test'
assert app.is_dir() and data.is_dir()
if not (backup/'app').exists():shutil.copytree(app,backup/'app')
apps={str(f.relative_to(app)).lower():f.relative_to(app) for f in app.rglob('*') if f.is_file()}
files={}
for f in data.rglob('*'):
    if not f.is_file():continue
    rel=f.relative_to(data)
    if 'downloads' in [s.lower() for s in rel.parts] or f.stat().st_size>30*1024*1024:continue
    dest=backup/'data'/rel
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(f,dest)
    files[str(rel).lower()]=rel
print('BACKUP_READY',len(apps),'app',len(files),'non-download files',flush=True)
build=f'build-youtube-v062-review-{variant}-review-JIT-on'
out=root/'output/v062-review'
out.mkdir(parents=True,exist_ok=True)
cmd=['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',
     str(root/'tools/test_cobalt_boot_vita3k.ps1'),
     '-BuildDirectory',build,'-Vpk',vpk,
     '-TitleId',title,'-Seconds','39']
try:
    z=subprocess.run(cmd,cwd=root,capture_output=True,text=True,
                     encoding='utf8',errors='replace',timeout=95)
    msg='EXIT='+str(z.returncode)+'\nSTDOUT:\n'+z.stdout+'\nSTDERR:\n'+z.stderr
    (out/('vita3k-'+variant+'-boot-test.txt')).write_text(msg,encoding='utf8')
    print('VITA3K',msg[-1200:],flush=True)
    hits=re.findall(r'CAPTURE=([^\s]+)',msg)
    if hits:
        cap=Path(hits[-1])
        for fn in ['boot.log','sb.log']:
            f=cap/fn
            if not f.exists():continue
            ls=f.read_text(encoding='utf8',errors='replace').splitlines()
            keys=['V061_RX_','V061_CPU_SAFE_MODE','COBALT_TEXT_RX_PROTECTION',
                  'COBALT_EVENT_START_RETURN','YTV_LIFECYCLE_CPU_CORES',
                  'YTV_VGL_INIT_RETURN','COBALT_HEARTBEAT']
            selected=[x[:220] for x in ls if any(k in x for k in keys)]
            print('LOG',fn,len(ls),'match',len(selected),'last',selected[-12:],flush=True)
except Exception as ex:
    print('SMOKE_FAILED',type(ex).__name__,str(ex),flush=True)
finally:
    for _ in range(30):
        if not running():break
        time.sleep(1)
    if running():print('RESTORE_DEFERRED Emulator still running',backup,flush=True)
    else:
        for f in list(app.rglob('*')):
            if f.is_file() and str(f.relative_to(app)).lower() not in apps:f.unlink()
        for rel in apps.values():
            dst=app/rel;dst.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(backup/'app'/rel,dst)
        for rel in files.values():
            dst=data/rel;dst.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(backup/'data'/rel,dst)
        for f in list(data.rglob('*')):
            if not f.is_file():continue
            rel=f.relative_to(data)
            if 'downloads' in [x.lower() for x in rel.parts] or f.stat().st_size>30*1024*1024:continue
            if str(rel).lower() not in files:f.unlink()
        assert hashlib.sha256((app/'eboot.bin').read_bytes()).digest()==hashlib.sha256((backup/'app/eboot.bin').read_bytes()).digest()
        print('RESTORED_ORIGINAL_INSTALLED_APP_AND_USER_DATA',flush=True)
