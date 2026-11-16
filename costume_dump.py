# Dump Spikeout FE player costume textures (4 characters x 4 colours) from banked_crom.bin to BMP/PNG.
# Table at CPU 0xFFF6C878 (crom.bin): per character a pointer to 4 words "bank<<24 | address in the banked window".
# One costume = 10 pieces of 128x128 (16-bit 1555, 8x8 tiles), each followed by its mipmaps 64..4 (5456 px),
# placed by the slot area headers (slot 0 list at 0xA1924): 5 columns x 2 rows -> 640x256.
import struct, sys, zlib, os
CROM = r'C:\Users\vf2_c\Desktop\8p\01\crom.bin'; BANK = r'C:\Users\vf2_c\Desktop\8p\01\banked_crom.bin'
OUT = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\vf2_c\Desktop\8p\costume'
c = open(CROM, 'rb').read(); bk = open(BANK, 'rb')
W = lambda a: struct.unpack('>I', c[a - 0xFF800000:a - 0xFF800000 + 4])[0]
NAMES = ['spike', 'white', 'linda', 'tenshin']
# natural (big-endian) 8x8 tile order: decode8x8 with the 16-bit pairs un-swapped
D = [1,0,5,4,9,8,13,12, 3,2,7,6,11,10,15,14, 17,16,21,20,25,24,29,28, 19,18,23,22,27,26,31,30,
     33,32,37,36,41,40,45,44, 35,34,39,38,43,42,47,46, 49,48,53,52,57,56,61,60, 51,50,55,54,59,58,63,62]
D = [d ^ 1 for d in D]
code = open('code.bin', 'rb').read()
HDR = [struct.unpack('>I', code[0xA1924 + 4*i:0xA1928 + 4*i])[0] for i in range(10)]
def piece_xy(h): return 32 * (h & 0x3F), 32 * ((h >> 7) & 0x1F)
xs = [piece_xy(h) for h in HDR]; x0 = min(x for x, y in xs); y0 = min(y for x, y in xs)
PW = 640; PH = 256; PIX = 21840
def decode(px16):   # 128x128 from tiled data
    img = [0] * (128 * 128); k = 0
    for ty in range(0, 128, 8):
        for tx in range(0, 128, 8):
            for yy in range(8):
                for xx in range(8):
                    img[(ty + yy) * 128 + tx + xx] = px16[k + D[yy * 8 + xx]]
            k += 64
    return img
def rgb(p): return (((p >> 10) & 31) * 255 // 31, ((p >> 5) & 31) * 255 // 31, (p & 31) * 255 // 31)
def save_bmp(path, w, h, pix):
    row = (w * 3 + 3) & ~3; data = bytearray()
    for y in range(h - 1, -1, -1):
        r = bytearray()
        for x in range(w): R, G, B = rgb(pix[y * w + x]); r += bytes((B, G, R))
        data += r + b'\0' * (row - w * 3)
    hdr = struct.pack('<2sIHHI', b'BM', 54 + len(data), 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    open(path, 'wb').write(hdr + data)
def save_png(path, w, h, pix):
    raw = b''.join(b'\0' + bytes(v for x in range(w) for v in rgb(pix[y * w + x])) for y in range(h))
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
os.makedirs(OUT, exist_ok=True)
for ci in range(4):
    p = W(0xFFF6C878 + 4 * ci)
    for col in range(4):
        v = W(p + 4 * col); bank = v >> 24; idx = 7; off = idx * 0x800000 + (v & 0x7FFFFF)   # bank byte 0x0D = banked_crom.bin bank 7 (matched against a texture RAM dump, 11/14)
        bk.seek(off); raw = bk.read(10 * PIX * 2); px = struct.unpack('>%dH' % (10 * PIX), raw)
        canvas = [0] * (PW * PH)
        for i in range(10):
            img = decode(px[i * PIX:i * PIX + 16384]); x, y = xs[i]
            for yy in range(128):
                canvas[(y - y0 + yy) * PW + (x - x0):(y - y0 + yy) * PW + (x - x0) + 128] = img[yy * 128:(yy + 1) * 128]
        base = os.path.join(OUT, '%d_%s_col%d' % (ci, NAMES[ci], col))
        save_bmp(base + '.bmp', PW, PH, canvas); save_png(base + '.png', PW, PH, canvas)
        print(base, 'rom %08X -> banked off %07X' % (v, off), 'alpha0 px:', sum(1 for q in canvas if not (q & 0x8000)))
