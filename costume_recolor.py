# Make 4 new costume colours (pink, purple, grey, white) for each character from the dumped BMPs.
# Costume pixels = those that differ between the 4 original colours (face / skin are the same in all 4);
# their shading (HSV value / saturation) comes from the red version, the hue / saturation from the new colour.
import struct, colorsys, os, sys, zlib
SRC = r'C:\Users\vf2_c\Desktop\8p\costume'
NAMES = ['spike', 'white', 'linda', 'tenshin']; OLD = ['red', 'blue', 'green', 'orange']
NEW = [  # name, hue (0-1), saturation scale, value: v' = a + b*v
    ('pink',   330/360, 0.55, 0.20, 0.85),
    ('purple', 275/360, 0.85, 0.00, 0.95),
    ('grey',   0.0,     0.00, 0.03, 0.27),   # 11/14 (user): darker, almost black (was 0.05 + 0.75 v)
    ('white',  0.0,     0.00, 0.45, 0.60),
]
def load_bmp(p):
    d = open(p, 'rb').read(); off, = struct.unpack('<I', d[10:14]); w, h = struct.unpack('<ii', d[18:26]); row = (w * 3 + 3) & ~3
    pix = [None] * (w * h)
    for y in range(h):
        b = off + (h - 1 - y) * row
        for x in range(w): B, G, R = d[b + 3*x:b + 3*x + 3]; pix[y * w + x] = (R, G, B)
    return w, h, pix
def save_bmp(p, w, h, pix):
    row = (w * 3 + 3) & ~3; data = bytearray()
    for y in range(h - 1, -1, -1):
        r = bytearray()
        for x in range(w): R, G, B = pix[y * w + x]; r += bytes((B, G, R))
        data += r + b'\0' * (row - w * 3)
    open(p, 'wb').write(struct.pack('<2sIHHI', b'BM', 54 + len(data), 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0) + data)
def save_png(p, w, h, pix):
    raw = b''.join(b'\0' + bytes(v for x in range(w) for v in pix[y * w + x]) for y in range(h))
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(p, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
allimg = {}
for ci, nm in enumerate(NAMES):
    var = [load_bmp(os.path.join(SRC, '%d_%s_col%d_%s.bmp' % (ci, nm, k, OLD[k]))) for k in range(4)]
    w, h, red = var[0]
    mask = [any(abs(a - b) > 16 for v in var[1:] for a, b in zip(red[i], v[2][i])) for i in range(w * h)]
    for k, (cn, hue, ss, va, vb) in enumerate(NEW):
        out = []
        for i, (R, G, B) in enumerate(red):
            if not mask[i]: out.append((R, G, B)); continue
            hh, s, v = colorsys.rgb_to_hsv(R / 255, G / 255, B / 255)
            v2 = min(1.0, va + vb * v); s2 = min(1.0, s * ss)
            r, g, b = colorsys.hsv_to_rgb(hue, s2, v2); out.append((int(r * 255 + .5), int(g * 255 + .5), int(b * 255 + .5)))
        base = os.path.join(SRC, '%d_%s_col%d_%s' % (ci, nm, 4 + k, cn))
        save_bmp(base + '.bmp', w, h, out); save_png(base + '.png', w, h, out); allimg[(ci, k)] = out
    print(nm, 'costume pixels', sum(mask))
# overview of the new ones
G = 4; CW, CH = 320, 128; W = 4 * CW + 5 * G; H = 4 * CH + 5 * G
ov = [[(48, 48, 48)] * W for _ in range(H)]
for (ci, k), pix in allimg.items():
    ox = G + k * (CW + G); oy = G + ci * (CH + G)
    for y in range(CH):
        for x in range(CW): ov[oy + y][ox + x] = pix[(y * 2) * 640 + x * 2]
save_png(os.path.join(SRC, 'overview_new.png'), W, H, [p for r in ov for p in r])
