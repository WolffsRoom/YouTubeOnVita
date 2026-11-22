"""Rebuild the native YTLT0001 PS Vita splash from an explicitly supplied MP4.

The container stores 60fps RGBA scanout patches, compressed individually with
zlib. Constant background and H264 macroblock noise are normalized to #252525
to keep decoding efficient. Matching PCM audio is kept in the separate native
48 kHz stereo raw S16 file. Both are *offline build assets*, not MP4 playback.
"""
from __future__ import annotations
import argparse, struct, subprocess, zlib, sys
from pathlib import Path
from PIL import Image, ImageChops
ROOT = Path(__file__).resolve().parents[1]
INPUT = ROOT/'assets/Start-up YouTube.mp4'
YSP = ROOT/'experiments/cobalt/vita/assets/splash/youtube_intro_60fps.yspl'
PCM = ROOT/'experiments/cobalt/vita/assets/splash/youtube_intro_audio.pcm'
W,H,FPS,FRAMES = 960,544,60,358
BG_RGB=(37,37,37)
BG_WORD=0xFF252525
MAX_BYTES=4*1024*1024
MAX_DIFF=10
FADE_FRAMES=48  # final 0.8 s; smooth fade of the logo into #252525

def run_ffmpeg():
    vf=('fps=fps=60:start_time=0:round=near,'
        'scale=960:540:flags=lanczos,'
        'pad=960:544:0:2:color=0x252525,'
        'tpad=stop_mode=clone:stop_duration=0.06')
    return subprocess.Popen([
        'ffmpeg','-nostdin','-hide_banner','-loglevel','error','-i',str(INPUT),
        '-vf',vf,'-frames:v',str(FRAMES),'-an','-pix_fmt','rgb24',
        '-f','rawvideo','pipe:1'],
        stdout=subprocess.PIPE,stderr=subprocess.PIPE)

def generate_video(out:Path):
    bg=Image.new('RGB',(W,H),BG_RGB)
    framebytes=W*H*3
    hdr=struct.pack('<8sIIIII',b'YTLT0001',FRAMES,FPS,W,H,BG_WORD)
    tmp=out.with_suffix('.yspl.tmp')
    first_feature=last_feature=None
    totalraw=0
    with tmp.open('wb') as stream:
        stream.write(hdr)
        proc=run_ffmpeg()
        try:
            for i in range(FRAMES):
                raw=proc.stdout.read(framebytes)
                if len(raw)!=framebytes:
                    raise RuntimeError(f'Missing reference frame {i}: got {len(raw)} bytes. ffmpeg stderr: {proc.stderr.read(400)!r}')
                frm=Image.frombytes('RGB',(W,H),raw)
                # The supplied reference holds the full YouTube logo at the
                # end, then stops abruptly. Keep the reference up to 5.17s,
                # fade the visible logo across the final 0.8s, and end on
                # #252525 (same color as the Cobalt bootstrap).
                if i>=FRAMES-FADE_FRAMES:
                    t=(i-(FRAMES-FADE_FRAMES))/(FADE_FRAMES-1)
                    t=t*t*(3-2*t)  # smoothstep, no jump at either end
                    frm=Image.blend(frm,bg,t)
                difference=ImageChops.difference(frm,bg)
                chans=difference.split()
                maxchan=ImageChops.lighter(ImageChops.lighter(chans[0],chans[1]),chans[2])
                mask=maxchan.point(lambda x: 255 if x>=MAX_DIFF else 0)
                bbox=mask.getbbox()
                if bbox is None:
                    stream.write(struct.pack('<HHHHII',0,0,0,0,0,0))
                    continue
                x0,y0,x1,y1=bbox
                # For antialiasing the complete bbox remains in color,
                # but all pixels outside the foreground mask become BG.
                # Bounding the entire active area eliminates MP4's low-bitrate
                # flat-background macroblock noise from the Vita scanout.
                clean=Image.composite(frm,bg,mask)
                crop=clean.crop(bbox).convert('RGBA')
                rgba=crop.tobytes()
                compressed=zlib.compress(rgba,level=6)
                stream.write(struct.pack('<HHHHII',
                    x0,y0,x1-x0,y1-y0,len(rgba),len(compressed)))
                stream.write(compressed)
                totalraw+=len(rgba)
                if first_feature is None:first_feature=i
                last_feature=i
                if i in (0,30,60,90,120,180,240,300,FRAMES-1):
                    print('FRAME',i,round(i/FPS,2),'bbox',bbox,'packed',len(compressed),flush=True)
        finally:
            if proc.stdout:proc.stdout.close()
            proc.wait(timeout=16)
            if proc.returncode:
                print('FFMPEG_RETURN',proc.returncode,proc.stderr.read(500),flush=True)
    size=tmp.stat().st_size
    if size>MAX_BYTES:
        raise RuntimeError(f'Intro file too big {size}>{MAX_BYTES}')
    assert size>28 and totalraw>0
    tmp.replace(out)
    print('VIDEO_READY',out,'size',size,'first_active',first_feature,
          'last_active',last_feature,'uncompressed',totalraw,flush=True)

def generate_audio(out:Path):
    tmp=out.with_suffix('.pcm.tmp')
    cmd=['ffmpeg','-y','-nostdin','-hide_banner','-loglevel','error',
         '-i',str(INPUT),'-map','0:a:0','-vn','-ar','48000','-ac','2',
         '-af','apad=whole_dur=5.966667',
         '-t','5.966667',
         '-c:a','pcm_s16le','-f','s16le',str(tmp)]
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=45)
    if p.returncode:raise RuntimeError(p.stderr[:3000])
    frames=tmp.stat().st_size//4
    if frames<(FRAMES*48000)//FPS-512:raise RuntimeError(f'Audio too short: {frames} frames')
    if frames>(FRAMES*48000)//FPS+512:raise RuntimeError(f'Audio too long: {frames} frames')
    tmp.replace(out)
    print('AUDIO_READY',out,'size',out.stat().st_size,'duration',round(frames/48000,5),flush=True)

def main():
    assert INPUT.is_file(),INPUT
    YSP.parent.mkdir(parents=True,exist_ok=True)
    generate_video(YSP)
    generate_audio(PCM)
if __name__=='__main__':main()
