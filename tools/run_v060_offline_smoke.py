"""Vita3K network recovery smoke: force offline, then online, restore original app and user files."""
from pathlib import Path
import subprocess,shutil,os,time,zipfile,hashlib
root=Path(__file__).resolve().parents[1]
profile=Path(os.environ['APPDATA'])/'Vita3K/Vita3K'
app=profile/'ux0/app/YTVCB0001'
data=profile/'ux0/data/youtubevita'
backup=root/'archives/v060-offline-smoke-vita3k-backup'
backup.mkdir(parents=True,exist_ok=True)
def running():
    q=subprocess.run(['tasklist','/FI','IMAGENAME eq Vita3K.exe'],capture_output=True,text=True,timeout=14)
    return 'Vita3K.exe' in q.stdout
assert not running(),'Vita3K is already running'
assert app.is_dir() and data.is_dir()
if not (backup/'app').exists():shutil.copytree(app,backup/'app')
original_apps={str(f.relative_to(app)).lower():f.relative_to(app) for f in app.rglob('*') if f.is_file()}
original_files={}
for f in data.rglob('*'):
    if not f.is_file():continue
    rel=f.relative_to(data)
    if 'downloads' in [x.lower() for x in rel.parts] or f.stat().st_size>30*1024*1024:continue
    dest=backup/'data'/rel
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(f,dest)
    original_files[str(rel).lower()]=rel
print('BACKUP_READY',len(original_apps),len(original_files),flush=True)
offline=data/'test_force_offline.txt'
online=data/'test_force_online.txt'
offline.write_text('1',encoding='ascii')
print('NETWORK_SIMULATED_OFFLINE=1',flush=True)
cmd=['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',
      str(root/'tools/test_cobalt_boot_vita3k.ps1'),
      '-BuildDirectory','build-youtube-v060-offline-test-full-offline-test-JIT-on',
      '-Vpk','YouTube.vpk','-TitleId','YTVCB0001','-Seconds','51']
proc=None
try:
    proc=subprocess.Popen(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                          text=True,encoding='utf8',errors='replace')
    time.sleep(25)
    offline.unlink(missing_ok=True)
    online.write_text('1',encoding='ascii')
    print('NETWORK_SIMULATED_ONLINE=1',flush=True)
    out,err=proc.communicate(timeout=110)
    report=f'EXIT={proc.returncode}\nSTDOUT:\n{out}\nSTDERR:\n{err}'
    dest=root/'output/v060-offline-smoke/network-recovery-test.txt'
    dest.write_text(report,encoding='utf8')
    print('CAPTURE',report[-2400:],flush=True)
except Exception as e:
    print('TEST_EXCEPTION',type(e).__name__,str(e),flush=True)
    if proc is not None and proc.poll() is None:
        proc.kill()
        proc.communicate()
finally:
    offline.unlink(missing_ok=True)
    online.unlink(missing_ok=True)
    for i in range(26):
        if not running():break
        time.sleep(1)
    if running():
        print('RESTORE_DEFERRED_VITA3K_RUNNING backups='+str(backup),flush=True)
    else:
        for f in app.rglob('*'):
            if f.is_file() and str(f.relative_to(app)).lower() not in original_apps:f.unlink()
        for rel in original_apps.values():
            src=backup/'app'/rel;dst=app/rel;dst.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(src,dst)
        for rel in original_files.values():
            src=backup/'data'/rel;dst=data/rel;dst.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(src,dst)
        for f in data.rglob('*'):
            if not f.is_file():continue
            rel=f.relative_to(data)
            if 'downloads' in [x.lower() for x in rel.parts] or f.stat().st_size>30*1024*1024:continue
            if str(rel).lower() not in original_files:f.unlink()
        assert hashlib.sha256((app/'eboot.bin').read_bytes()).digest()==hashlib.sha256((backup/'app/eboot.bin').read_bytes()).digest()
        print('RESTORED_ORIGINAL_APP_AND_USER_FILES, downloads untouched',flush=True)
