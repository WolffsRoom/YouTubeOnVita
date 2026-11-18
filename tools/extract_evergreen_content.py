from pathlib import Path
import io,zipfile
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
crx=root/'experiments/cobalt/evergreen/cobalt_evergreen_5.50.2_arm-hardfp_sbversion-15_release.crx'
raw=crx.read_bytes();i=raw.find(b'PK\x03\x04')
assert i>0
z=zipfile.ZipFile(io.BytesIO(raw[i:]))
out=root/'experiments/cobalt/evergreen/core25/content'
count=0; bytes_written=0
for item in z.infolist():
 if not item.filename.startswith('content/') or item.is_dir():
  continue
 name=item.filename[len('content/'):]
 dest=out/name
 assert name and '..' not in Path(name).parts and not Path(name).is_absolute()
 dest.parent.mkdir(parents=True,exist_ok=True)
 data=z.read(item)
 assert len(data)==item.file_size
 dest.write_bytes(data)
 count+=1;bytes_written+=len(data)
print('EXTRACTED_OFFICIAL_COBALT_ASSETS',count,'BYTES',bytes_written)
for folder in ('ssl/certs','web/cobalt/browser/splash_screen','fonts'):
 p=out/folder
 print('FOLDER',folder,'FILES',len(list(p.iterdir())) if p.exists() else 'missing')

