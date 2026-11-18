#!/bin/sh
# Navigation benchmark in Vita3K: guest -> Music tab -> move across cards.
# Usage: tools/bench_cobalt_vita3k.sh <label> [cobalt_config lines...]
label="$1"; shift
D="$APPDATA/Vita3K/Vita3K/ux0/data/youtubevita"
printf '30 cross\n44 down\n46 down\n48 down\n51 cross\n75 left\n85 down\n85.5 down\n86 down\n86.5 down\n110 right\n' > "$D/input_script.txt"
t=120; while [ $t -le 160 ]; do echo "$t right" >> "$D/input_script.txt"; t=$((t+2)); done
echo 170 > "$D/pump_seconds.txt"
rm -f "$D/cobalt_config.txt"; for l in "$@"; do echo "$l" >> "$D/cobalt_config.txt"; done
powershell -NoProfile -File tools/test_cobalt_boot_vita3k.ps1 -Seconds 175 > /dev/null
d=$(ls -td tools/capture/cobalt-boot-* | head -1)
python -I - "$d/sb.log" "$label" <<'PY'
import sys,re
lines=open(sys.argv[1],errors='replace').read().splitlines()
fps=[(int(m.group(1)),int(m.group(2))) for l in lines for m in [re.match(r'YTV_FPS t=(\d+)ms frames=(\d+)',l)] if m]
press=[int(m.group(1)) for l in lines for m in [re.match(r'YTV_INPUT_SCRIPT_PRESS t=(\d+)ms',l)] if m]
cfg=[l for l in lines if l.startswith('YTV_CONFIG')]
brk=[l for l in lines if 'BREAK' in l]
if not fps: print(sys.argv[2],'no fps data',cfg,brk); sys.exit()
t0=fps[0][0]
# navigation window: presses after 110 s of script time
nav=[f for t,f in fps if t-t0>=0]
tail=[f for t,f in fps][-40:]
print(sys.argv[2], cfg, 'seconds_with_frames',len(fps),'avg_fps_last40s',round(sum(tail)/max(1,len(tail)),2),'max',max(tail) if tail else 0, 'breaks',len(brk))
PY
echo "$d"
