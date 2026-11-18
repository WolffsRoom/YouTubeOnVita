import requests,hashlib,zipfile,pathlib,sys
dst=pathlib.Path(r"C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\evergreen\cobalt_evergreen_5.50.2_arm-hardfp_sbversion-15_release.crx")
url="https://github.com/youtube/cobalt/releases/download/25.lts.50/"+dst.name
expected="be5b93dc4dcd42bd8dea5bfda728e6a170a17d6eef913b2cf78d370ac5e011dd"
if not dst.exists():
    with requests.get(url,stream=True,timeout=55,headers={"User-Agent":"YouTubeVita-Cobalt-Research/1.0"}) as resp:
        print("HTTP",resp.status_code,"TYPE",resp.headers.get("Content-Type"),flush=True)
        resp.raise_for_status()
        with dst.open("wb") as f:
            for chunk in resp.iter_content(1024*1024):f.write(chunk)
sha=hashlib.sha256(dst.read_bytes()).hexdigest()
print("SIZE",dst.stat().st_size,"SHA256",sha,"MATCH",sha==expected,"MAGIC",dst.read_bytes()[:8],flush=True)
if sha!=expected:
    dst.unlink(missing_ok=True)
    raise RuntimeError("Upstream digest mismatch")
try:
    with zipfile.ZipFile(dst) as z:
        print("CRX_ZIP_ENTRIES",z.namelist()[:25])
except Exception as exc:
    print("ZIP_READ_STATUS",type(exc).__name__,str(exc)[:180])