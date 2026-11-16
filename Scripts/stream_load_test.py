"""
stream_load_test.py - P8 (2026-11-16): receive N streams at once without decoding, to measure the host.

    python Scripts/stream_load_test.py [HOST] [SLOTS] [SECONDS]
        HOST     default 127.0.0.1
        SLOTS    default 1-6  (e.g. "1-6" or "1,3,5")
        SECONDS  default 30

For every slot it connects like a client (HELLO without login - so only from this PC / the home network),
sends MEDIA_HELLO to the video and audio ports, keeps the heartbeat and PINGs once a second.
Every 10 s it prints, per slot: video frames/s (RTP timestamps), video Mbit/s, keyframes, RTP loss,
audio packets/s, and the PING round trip. At the end it says BYE.
"""
import select
import socket
import struct
import sys
import time

MAGIC = b"SPKS"
VERSION = 1
T_HELLO, T_WELCOME, T_REJECT, T_HB, T_PING, T_PONG, T_KEYREQ, T_BYE = 1, 2, 3, 4, 5, 6, 7, 8
T_MEDIA_HELLO = 11

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
SLOTSPEC = sys.argv[2] if len(sys.argv) > 2 else "1-6"
SECONDS = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0


def parse_slots(spec):
    out = []
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def header(t, key):
    return MAGIC + struct.pack("<BBI", VERSION, t, key)


def udp():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
    s.bind(("0.0.0.0", 0))
    s.setblocking(False)
    return s


class Slot:
    def __init__(self, n):
        self.n = n
        self.base = 55000 + (n - 1) * 4
        self.sess, self.vid, self.aud = udp(), udp(), udp()
        self.key = None
        self.info = None
        self.reset()
        self.rtts = []
        self.last_seq = None

    def reset(self):
        self.vbytes = 0
        self.vframes = set()
        self.kf = 0
        self.lost = 0
        self.vpkts = 0
        self.apkts = 0
        self.rtt_window = []

    def connect(self):
        nm = b"loadtest".ljust(32, b"\0")
        hello = header(T_HELLO, 0) + struct.pack("<BB", 3, 0) + nm + struct.pack("<H", 0)
        end = time.time() + 3
        self.sess.setblocking(True)
        self.sess.settimeout(0.5)
        while time.time() < end:
            self.sess.sendto(hello, (HOST, self.base + 1))
            try:
                d, _ = self.sess.recvfrom(512)
            except socket.timeout:
                continue
            if d[:4] == MAGIC and d[5] == T_WELCOME:
                self.key = struct.unpack_from("<I", d, 6)[0]
                self.info = struct.unpack_from("<HHBB", d, 10)
                break
            if d[:4] == MAGIC and d[5] == T_REJECT:
                print("slot %d: REJECT reason %d" % (self.n, d[10]))
                break
        self.sess.setblocking(False)
        return self.key is not None

    def media_hello(self):
        self.vid.sendto(header(T_MEDIA_HELLO, self.key), (HOST, self.base + 2))
        self.aud.sendto(header(T_MEDIA_HELLO, self.key), (HOST, self.base + 3))


def has_idr(p, h265):
    i = p.find(b"\x00\x00\x01")
    if not p:
        return False
    t0 = ((p[0] >> 1) & 0x3F) if h265 else (p[0] & 0x1F)
    if (t0 in (19, 20)) if h265 else (t0 == 5):
        return True
    while i >= 0 and i + 3 < len(p):
        b = p[i + 3]
        t = ((b >> 1) & 0x3F) if h265 else (b & 0x1F)
        if (t in (19, 20)) if h265 else (t == 5):
            return True
        i = p.find(b"\x00\x00\x01", i + 3)
    return False


def main():
    slots = [Slot(n) for n in parse_slots(SLOTSPEC)]
    for s in slots:
        if s.connect():
            w, h, codec, sl = s.info
            print("slot %d: connected %dx%d %s" % (s.n, w, h, "H265" if codec == 2 else "H264"))
        else:
            print("slot %d: no WELCOME" % s.n)
    slots = [s for s in slots if s.key is not None]
    if not slots:
        return
    for s in slots:
        s.media_hello()

    socks = {}
    for s in slots:
        socks[s.sess] = (s, "sess")
        socks[s.vid] = (s, "vid")
        socks[s.aud] = (s, "aud")

    start = time.time()
    last_hb = last_ping = last_report = last_media = start
    while time.time() - start < SECONDS:
        r, _, _ = select.select(list(socks.keys()), [], [], 0.05)
        now = time.time()
        for sock in r:
            s, kind = socks[sock]
            while True:
                try:
                    d, _ = sock.recvfrom(65536)
                except (BlockingIOError, OSError):
                    break
                if kind == "vid":
                    if len(d) < 13:
                        continue
                    seq, ts = struct.unpack_from(">HI", d, 2)
                    if s.last_seq is not None:
                        gap = (seq - s.last_seq) & 0xFFFF
                        if 1 < gap < 1000:
                            s.lost += gap - 1
                    s.last_seq = seq
                    s.vbytes += len(d)
                    s.vpkts += 1
                    s.vframes.add(ts)
                    if has_idr(d[12:], s.info[2] == 2):
                        s.kf += 1
                elif kind == "aud":
                    s.apkts += 1
                else:
                    if d[:4] == MAGIC and d[5] == T_PONG and len(d) >= 18:
                        t0 = struct.unpack_from("<Q", d, 10)[0]
                        s.rtt_window.append((time.perf_counter_ns() // 1000 - t0) / 1000.0)
        if now - last_hb >= 2:
            for s in slots:
                s.sess.sendto(header(T_HB, s.key), (HOST, s.base + 1))
            last_hb = now
        if now - last_ping >= 1:
            for s in slots:
                s.sess.sendto(header(T_PING, s.key) + struct.pack("<Q", time.perf_counter_ns() // 1000), (HOST, s.base + 1))
            last_ping = now
        if now - last_media >= 5:
            for s in slots:
                s.media_hello()
            last_media = now
        if now - last_report >= 10:
            dt = now - last_report
            print("--- %.0f s" % (now - start))
            for s in slots:
                rtt = (sum(s.rtt_window) / len(s.rtt_window)) if s.rtt_window else -1
                print("slot %d: %5.1f fps  %6.2f Mbit/s  keyframes %d  lost %d/%d  audio %5.1f pkt/s  ping %.2f ms" %
                      (s.n, len(s.vframes) / dt, s.vbytes * 8 / dt / 1e6, s.kf, s.lost, s.vpkts, s.apkts / dt, rtt))
                s.reset()
            last_report = now
    for s in slots:
        s.sess.sendto(header(T_BYE, s.key), (HOST, s.base + 1))
    print("done")


if __name__ == "__main__":
    main()
