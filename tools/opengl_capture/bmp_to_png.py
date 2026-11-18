"""Convert frame_*.bmp written by capture_swap.dll to PNG (stdlib only)."""
import os, struct, sys, zlib
def convert(path):
    b = open(path, 'rb').read()
    off = struct.unpack_from('<I', b, 10)[0]; w, h = struct.unpack_from('<ii', b, 18)
    px = b[off:]; rows = []
    for y in range(h - 1, -1, -1):
        r = px[y * w * 4:(y + 1) * w * 4]
        rgb = bytearray(w * 3); rgb[0::3] = r[2::4]; rgb[1::3] = r[1::4]; rgb[2::3] = r[0::4]
        rows.append(b'\x00' + bytes(rgb))
    def ch(t, x): return struct.pack('>I', len(x)) + t + x + struct.pack('>I', zlib.crc32(t + x) & 0xffffffff)
    out = path[:-4] + '.png'
    open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                          ch(b'IDAT', zlib.compress(b''.join(rows), 6)) + ch(b'IEND', b''))
    return out
if __name__ == '__main__':
    for f in sorted(os.listdir(sys.argv[1])):
        if f.endswith('.bmp'): convert(os.path.join(sys.argv[1], f))
