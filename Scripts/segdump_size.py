# 11/23 main6 (netlink): how small does the link data get?  python segdump_size.py <segdump.bin>
# segdump.bin is written by the satellite clone 09 (ini SatelliteSegDump = 1): per frame u32 frame, u8 n,
# n x (u16 len, bytes). Compares raw / XOR delta vs the previous frame, each through zlib and lzma,
# per frame (what one packet would be) and as one stream (what a TCP stream with a running compressor would be).
import sys, struct, zlib, lzma

FPS = 57.524

def read_frames(path):
    data = open(path, 'rb').read()
    p, frames = 0, []
    while p + 5 <= len(data):
        f, n = struct.unpack_from('<IB', data, p); p += 5
        segs = []
        for _ in range(n):
            if p + 2 > len(data): return frames
            (ln,) = struct.unpack_from('<H', data, p); p += 2
            if p + ln > len(data): return frames
            segs.append(data[p:p + ln]); p += ln
        frames.append(b''.join(segs))
    return frames

def xor(a, b):
    if len(a) != len(b): return a
    return (int.from_bytes(a, 'little') ^ int.from_bytes(b, 'little')).to_bytes(len(a), 'little')

def main():
    frames = read_frames(sys.argv[1])
    if len(frames) < 2:
        print('too few frames'); return
    nf = len(frames)
    raw = sum(len(f) for f in frames)
    deltas = [frames[0]] + [xor(frames[i], frames[i - 1]) for i in range(1, nf)]
    changed = sum(len(d) - d.count(0) for d in deltas[1:]) / (nf - 1)

    def per_frame(blobs, fn): return sum(len(fn(b)) for b in blobs)
    z = lambda b: zlib.compress(b, 6)
    rows = [
        ('raw', raw),
        ('raw  zlib / frame', per_frame(frames, z)),
        ('delta zlib / frame', per_frame(deltas, z)),
    ]
    # running compressor across frames (a TCP stream: each frame flushed, the dictionary kept)
    for name, blobs in (('raw', frames), ('delta', deltas)):
        c = zlib.compressobj(6)
        tot = sum(len(c.compress(b) + c.flush(zlib.Z_SYNC_FLUSH)) for b in blobs)
        rows.append((name + ' zlib stream', tot))
    rows.append(('delta lzma whole', len(lzma.compress(b''.join(deltas)))))

    print(f'frames {nf}  ({nf / FPS:.1f} s)  bytes/frame {raw / nf:.0f}  changed bytes/frame {changed:.0f}')
    for name, tot in rows:
        bpf = tot / nf
        print(f'{name:22s} {bpf:9.0f} B/frame  {bpf * FPS / 1024:8.1f} KB/s  {bpf * FPS * 8 / 1e6:7.3f} Mbps')

if __name__ == '__main__':
    main()
