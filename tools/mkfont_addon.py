# Usage: mkfont_addon.py font.ttf out.h Prefix size  (DejaVu Sans from /usr/share/fonts/truetype/dejavu)
import sys
from PIL import Image, ImageDraw, ImageFont
P = sys.argv[3] if len(sys.argv) > 3 else 'Addon'
SZ = int(sys.argv[4]) if len(sys.argv) > 4 else 20
f = ImageFont.truetype(sys.argv[1], SZ)
asc, desc = f.getmetrics()
H = asc + desc
chars = list(range(32, 127)) + list(range(160, 256))
cols, meta, x = [], [], 0
for c in chars:
    ch = chr(c)
    adv = int(round(f.getlength(ch)))
    bbox = f.getbbox(ch)
    w = max(adv, bbox[2]) + 1
    im = Image.new('L', (w, H), 0)
    ImageDraw.Draw(im).text((0, 0), ch, font=f, fill=255)
    meta.append((c, x, w, adv))
    cols.append(im)
    x += w
W = x
atlas = Image.new('L', (W, H), 0)
xx = 0
for im in cols:
    atlas.paste(im, (xx, 0))
    xx += im.size[0]
data = atlas.tobytes()
out = open(sys.argv[2], 'w', newline='\n')
out.write('// Generated from DejaVu Sans (%s, %dpx)' % (sys.argv[1], SZ) + ' (Bitstream Vera / DejaVu fonts license,\n'
          '// free to embed). Glyph atlas for the native add-on menu: Latin-1 32..126,\n'
          '// 160..255, 8-bit coverage.\n#pragma once\n#include <cstdint>\n')
out.write('constexpr int k%sFontW = %d;\nconstexpr int k%sFontH = %d;\n' % (P, W, P, H))
out.write('#ifndef ADDON_GLYPH_DEFINED\n#define ADDON_GLYPH_DEFINED\n'
          'struct AddonGlyph { uint16_t x; uint8_t w, adv; };\n#endif\n')
idx = {m[0]: m for m in meta}
out.write('static const AddonGlyph k%sGlyphs[256] = {\n' % P)
for c in range(256):
    if c in idx:
        _, gx, w, adv = idx[c]
        out.write('{%d,%d,%d},' % (gx, w, adv))
    else:
        out.write('{0,0,0},')
    if c % 8 == 7:
        out.write('\n')
out.write('};\nstatic const uint8_t k%sFont[] = {\n' % P)
for i in range(0, len(data), 40):
    out.write(','.join(str(b) for b in data[i:i + 40]) + ',\n')
out.write('};\n')
print(W, H, len(data))
