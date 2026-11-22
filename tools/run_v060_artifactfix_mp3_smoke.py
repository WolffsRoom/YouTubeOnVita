# One-shot Vita3K native AAC->MP3 integration smoke, always restores user data.
# Diagnostic VPK only. The public v0.59 Full/Lite builds do NOT auto-download.
from pathlib import Path
import os,subprocess,shutil,time,zipfile,hashlib,json,sys
root=Path(__file__).resolve().parents[1]
profile=Path(os.environ['APPDATA'])/'Vita3K/Vita3K'
app=profile/'ux0/app/YTVCB0001'
data=profile/'ux0/data/youtubevita'
downloads=data/'downloads'
backup=root/'archives/v060-artifactfix-mp3-vita3k-backup'
output=root/'output/v060-artifactfix-mp3-smoke'
backup.mkdir(parents=True,exist_ok=True)
output.mkdir(parents=True,exist_ok=True)
result=output/'test-result.txt'
def running():
 p=subprocess.run(['tasklist','/FI','IMAGENAME eq Vita3K.exe'],capture_output=True,text=True,encoding='cp1252',errors='replace',timeout=15)
 return 'Vita3K.exe' in (p.stdout or '')
assert not running(),'Refusing to run while Vita3K is open'
assert app.is_dir() and data.is_dir()
user_files={}
for f in data.rglob('*'):
 if not f.is_file():continue
 rel=f.relative_to(data)
 if 'downloads' in [x.lower() for x in rel.parts]:continue
 if f.stat().st_size>30*1024*1024:continue
 dest=backup/'data'/rel
 dest.parent.mkdir(parents=True,exist_ok=True)
 shutil.copy2(f,dest)
 user_files[str(rel).lower()]=rel
if not (backup/'app').exists():shutil.copytree(app,backup/'app')
original_downloads={f.name for f in downloads.iterdir() if f.is_file()}
target=downloads/'Me at the zoo.mp3'
if target.exists():
 shutil.copy2(target,backup/'Me at the zoo.mp3')
 original_target_digest=hashlib.sha256(target.read_bytes()).hexdigest()
else:original_target_digest=None
print('BACKUP_READY files',len(user_files),'target_mp3_saved',target.exists(),flush=True)
marker=data/'native_test_url.txt'
marker.write_text('jNQXAC9IVRw\n',encoding='ascii')
forced=(len(sys.argv)>1 and sys.argv[1]=='--progressive')
if forced:(data/'test_force_progressive.txt').write_text('1',encoding='ascii')
capture_name=''
try:
 cmd=['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',
      str(root/'tools/test_cobalt_boot_vita3k.ps1'),'-BuildDirectory',
      'build-youtube-v060-artifactfix-mp3-smoke-full-artifactfix-mp3-smoke-JIT-on','-Vpk','YouTube.vpk',
      '-TitleId','YTVCB0001','-Seconds','58']
 task=subprocess.run(cmd,cwd=root,capture_output=True,text=True,
      encoding='utf8',errors='replace',timeout=152)
 print('SMOKE_TEST_RET',task.returncode,'OUTPUT',task.stdout[-900:],flush=True)
 import re
 captures=re.findall(r'CAPTURE=([^\s]+)',task.stdout)
 if captures:capture_name=captures[-1]
 if capture_name:
  c=Path(capture_name)
  for fn in ['boot.log','sb.log']:
   f=c/fn
   if not f.exists():continue
   l=f.read_text(encoding='utf8',errors='replace').splitlines()
   wanted=('YTV_LIFECYCLE_TEST_MP3_AUTOSTART','YTV_LIFECYCLE_NATIVE_DOWNLOAD',
       'YTV_LIFECYCLE_MP3_STREAM','YTV_LIFECYCLE_MP3_AUDIO','YTV_NATIVE_MP3',
       'COBALT_EVENT_START_RETURN','YTV_CUSTOM_SPLASH')
   selected=[x[:240] for x in l if any(k in x for k in wanted)]
   print('CAPTURE_LOG',fn,'TOTAL_LINES',len(l),'EVENTS',len(selected),flush=True)
   for x in selected[-35:]:print('EVENT',x,flush=True)
 if target.exists():
  b=target.read_bytes()
  print('CURRENT_MP3',len(b),'HEADER',b[:6].hex(' '),'MATCHES_PREVIOUS',
        hashlib.sha256(b).hexdigest()==original_target_digest,flush=True)
  if hashlib.sha256(b).hexdigest()!=original_target_digest:
   generated=output/('test-generated-progressive-stream.mp3' if forced else 'test-generated-direct-stream.mp3')
   generated.write_bytes(b)
   print('SAVED_TEST_OUTPUT',generated,len(b),flush=True)
 else:print('NO_MP3_FILE_GENERATED',flush=True)
except Exception as e:
 print('SMOKE_EXCEPTION',type(e).__name__,str(e),flush=True)
finally:
 for _ in range(35):
  if not running():break
  time.sleep(1)
 if running():
  print('APP_STILL_RUNNING_RESTORE_DEFERRED_DO_NOT_MODIFY_RUNNING_APP',backup,flush=True)
 else:
  with zipfile.ZipFile(root/'current/YouTube.vpk') as z:
   for info in z.infolist():
    if info.is_dir():continue
    dest=app/info.filename
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_bytes(z.read(info.filename))
   expected=hashlib.sha256(z.read('eboot.bin')).hexdigest()
  actual=hashlib.sha256((app/'eboot.bin').read_bytes()).hexdigest()
  assert actual==expected
  for rel in user_files.values():
   src=backup/'data'/rel;dest=data/rel
   dest.parent.mkdir(parents=True,exist_ok=True)
   shutil.copy2(src,dest)
  for f in list(data.rglob('*')):
   if not f.is_file():continue
   rel=f.relative_to(data)
   if 'downloads' in [x.lower() for x in rel.parts]:continue
   if f.stat().st_size>30*1024*1024:continue
   if str(rel).lower() not in user_files:
    f.unlink()
  if original_target_digest and (backup/'Me at the zoo.mp3').exists():
   shutil.copy2(backup/'Me at the zoo.mp3',target)
   assert hashlib.sha256(target.read_bytes()).hexdigest()==original_target_digest
  for f in downloads.iterdir():
   if not f.is_file():continue
   if f.name not in original_downloads:f.unlink()
  print('ORIGINAL_EMULATOR_APP_AND_USERFILES_RESTORED','mp3_digest_ok',not original_target_digest or hashlib.sha256(target.read_bytes()).hexdigest()==original_target_digest,flush=True)
print('TEST_FINISHED capture',capture_name,flush=True)
