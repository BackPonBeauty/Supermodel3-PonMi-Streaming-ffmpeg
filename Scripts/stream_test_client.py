#!/usr/bin/env python3
"""
stream_test_client.py - a small test sender for the streaming protocol v1 (P4, 2026-11-16).

Talks to one SessionServer (one slot) and checks the server side without the real client (P5):
  1. a HELLO with the wrong version is refused (REJECT version)
  2. HELLO (no login, from this PC) -> WELCOME (width, height, codec, slot, session key)
  3. a second client is refused (REJECT slot busy)
  4. MEDIA_HELLO to the video and audio ports -> RTP video and audio arrive
  5. PING -> PONG (round trip), HB -> HB echo, STATUS
  6. KEYREQ -> a keyframe (IDR) arrives soon after
  7. INPUT: holds W and pad button 1 for a moment (watch the server's log for "first INPUT")
  8. BYE -> the slot is free again; a new HELLO is accepted
  9. stop sending HB -> the server drops the client after 6 s and the slot is free again

Usage:  python stream_test_client.py [host] [session_port]
        default: 127.0.0.1 55001 (input = session-1, video = session+1, audio = session+2)
"""

import socket
import struct
import sys
import time

MAGIC = b"SPKS"
VERSION = 3   # 11/16: v2 room password (nonce, pwProof), v3 entry tickets (ticketLen + ticket + signature)
T_HELLO, T_WELCOME, T_REJECT, T_HB, T_PING, T_PONG, T_KEYREQ, T_BYE, T_NOTICE, T_INPUT, T_MEDIA_HELLO, T_STATUS = range(1, 13)
REJECT_NAMES = {1: "version", 2: "not member", 3: "slot busy", 4: "login required", 5: "codec", 6: "bad request", 7: "login unavailable", 8: "login invalid", 9: "already connected", 10: "challenge", 11: "wrong password", 12: "password locked", 13: "ticket invalid"}

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
SESSION = int(sys.argv[2]) if len(sys.argv) > 2 else 55001
INPUT_PORT, VIDEO_PORT, AUDIO_PORT = SESSION - 1, SESSION + 1, SESSION + 2

results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(("  OK   " if ok else "  FAIL ") + name + (("  - " + detail) if detail else ""))


def header(t, key, version=VERSION):
    return MAGIC + struct.pack("<BBI", version, t, key)


def hello(name="tester", codecs=3, version=VERSION):
    nm = name.encode("utf-8")[:32].ljust(32, b"\0")
    return header(T_HELLO, 0, version) + struct.pack("<BB", codecs, 0) + nm + struct.pack("<I", 0) + bytes(32) + struct.pack("<H", 0)


def recv_type(sock, want, timeout=1.0):
    sock.settimeout(timeout)
    end = time.time() + timeout
    while time.time() < end:
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            break
        if len(data) >= 10 and data[:4] == MAGIC and data[5] in want:
            return data
    return None


def udp():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("0.0.0.0", 0))
    return s


def connect(sock, name="tester"):
    sock.sendto(hello(name), (HOST, SESSION))
    d = recv_type(sock, (T_WELCOME, T_REJECT), 2.0)
    if d and d[5] == T_WELCOME:
        key = struct.unpack_from("<I", d, 6)[0]
        w, h, codec, slot = struct.unpack_from("<HHBB", d, 10)
        return key, (w, h, codec, slot)
    return None, d


def nal_types(pkt, h265):
    """NAL unit type of an RTP payload (unwrapping FU fragments). Returns (type, is_start)."""
    if len(pkt) < 14:
        return None, False
    p = pkt[12:]
    if h265:
        t = (p[0] >> 1) & 0x3F
        if t == 49 and len(p) >= 3:     # FU
            return p[2] & 0x3F, bool(p[2] & 0x80)
        return t, True
    t = p[0] & 0x1F
    if t == 28 and len(p) >= 2:         # FU-A
        return p[1] & 0x1F, bool(p[1] & 0x80)
    return t, True


def is_idr(t, h265):
    return (t in (19, 20)) if h265 else (t == 5)


def has_idr(pkt, h265):
    """True if the RTP payload holds an IDR slice: either as its (first) NAL type, or after a start code
    inside it. The server's RtpSender sends a whole encoder output (SPS + PPS + slice, with start codes
    between them) as one NAL, so the IDR usually sits behind the SPS/PPS."""
    t, _ = nal_types(pkt, h265)
    if t is not None and is_idr(t, h265):
        return True
    p = pkt[12:]
    i = p.find(b"\x00\x00\x01")
    while i >= 0 and i + 3 < len(p):
        b = p[i + 3]
        tt = ((b >> 1) & 0x3F) if h265 else (b & 0x1F)
        if is_idr(tt, h265):
            return True
        i = p.find(b"\x00\x00\x01", i + 3)
    return False


def main():
    print("Streaming test client -> %s session %d (input %d, video %d, audio %d)" % (HOST, SESSION, INPUT_PORT, VIDEO_PORT, AUDIO_PORT))

    # 1. wrong version
    s0 = udp()
    s0.sendto(hello("old-client", version=99), (HOST, SESSION))
    d = recv_type(s0, (T_REJECT, T_WELCOME), 2.0)
    check("wrong version is refused", bool(d and d[5] == T_REJECT and d[10] == 1),
          "no answer" if not d else ("REJECT " + REJECT_NAMES.get(d[10], str(d[10])) if d[5] == T_REJECT else "got WELCOME"))

    # 2. connect
    sess = udp()
    key, info = connect(sess)
    check("HELLO -> WELCOME", key is not None, str(info) if key is None else "size %dx%d codec %s slot %d key %08x" %
          (info[0], info[1], "H265" if info[2] == 2 else "H264", info[3], key))
    if key is None:
        return
    h265 = info[2] == 2

    # 3. second client
    s2 = udp()
    k2, d2 = connect(s2, "intruder")
    check("second client is refused (slot busy)", k2 is None and d2 is not None and d2[5] == T_REJECT and d2[10] == 3,
          "accepted!" if k2 else ("no answer" if d2 is None else "REJECT " + REJECT_NAMES.get(d2[10], str(d2[10]))))

    # 4. media
    vid, aud = udp(), udp()
    vid.sendto(header(T_MEDIA_HELLO, key), (HOST, VIDEO_PORT))
    aud.sendto(header(T_MEDIA_HELLO, key), (HOST, AUDIO_PORT))
    # a media hello with a wrong key must not be accepted (checked indirectly: only the right one gets data)
    bad = udp()
    bad.sendto(header(T_MEDIA_HELLO, key ^ 0x5A5A5A5A), (HOST, VIDEO_PORT))
    # like the real client: once the video path is open, ask for a keyframe to start decoding from
    time.sleep(0.1)
    sess.sendto(header(T_KEYREQ, key), (HOST, SESSION))

    vcount, vbytes, idr_seen, acount = 0, 0, False, 0
    t_end = time.time() + 3.0
    vid.settimeout(0.05); aud.settimeout(0.01); bad.settimeout(0.01)
    bad_got = 0
    last_hb = time.time()
    while time.time() < t_end:
        try:
            pkt, _ = vid.recvfrom(65536)
            vcount += 1; vbytes += len(pkt)
            if has_idr(pkt, h265):
                idr_seen = True
        except socket.timeout:
            pass
        try:
            aud.recvfrom(65536); acount += 1
        except socket.timeout:
            pass
        try:
            bad.recvfrom(65536); bad_got += 1
        except socket.timeout:
            pass
        if time.time() - last_hb > 1.0:
            sess.sendto(header(T_HB, key), (HOST, SESSION)); last_hb = time.time()
    check("video arrives after MEDIA_HELLO", vcount > 0, "%d RTP packets, %.1f kB in 3 s" % (vcount, vbytes / 1024))
    check("first video contains a keyframe (IDR)", idr_seen)
    check("audio arrives after MEDIA_HELLO", acount > 0, "%d packets in 3 s" % acount)
    check("wrong-key MEDIA_HELLO gets nothing", bad_got == 0, "%d packets" % bad_got)

    # 5. ping / hb / status
    t0 = time.perf_counter_ns()
    sess.sendto(header(T_PING, key) + struct.pack("<Q", t0), (HOST, SESSION))
    d = recv_type(sess, (T_PONG,), 1.0)
    rtt = (time.perf_counter_ns() - struct.unpack_from("<Q", d, 10)[0]) / 1e6 if d else None
    check("PING -> PONG", d is not None, "round trip %.2f ms" % rtt if rtt is not None else "no PONG")
    sess.sendto(header(T_HB, key), (HOST, SESSION))
    check("HB -> HB echo", recv_type(sess, (T_HB,), 1.0) is not None)
    sess.sendto(header(T_STATUS, key) + struct.pack("<f", 0.0), (HOST, SESSION))

    # 6. keyframe request: drain, then ask and look for an IDR within 1 s
    vid.settimeout(0.01)
    try:
        while True:
            vid.recvfrom(65536)
    except socket.timeout:
        pass
    sess.sendto(header(T_KEYREQ, key), (HOST, SESSION))
    t_req = time.time(); got_idr_at = None
    vid.settimeout(0.05)
    while time.time() - t_req < 1.0 and got_idr_at is None:
        try:
            pkt, _ = vid.recvfrom(65536)
            if has_idr(pkt, h265):
                got_idr_at = time.time() - t_req
        except socket.timeout:
            pass
    check("KEYREQ -> keyframe", got_idr_at is not None, "after %.0f ms" % (got_idr_at * 1000) if got_idr_at is not None else "none within 1 s")

    # 7. input: W + button 1 for 0.5 s, then release
    inp = udp()
    seq = 0
    def send_input(keys, buttons, meta=0):
        nonlocal seq
        seq = (seq + 1) & 0xFFFF
        pkt = header(T_INPUT, key) + struct.pack("<HHBBI6hB", seq, keys, meta, 1, buttons, 0, 0, 0, 0, 0, 0, 0)
        inp.sendto(pkt, (HOST, INPUT_PORT))
    t_in = time.time()
    while time.time() - t_in < 0.5:
        send_input(0x0001, 0x00000001)
        time.sleep(0.008)
    send_input(0, 0, meta=0x02)   # also tap "bitrate up" (meta bit 1)
    send_input(0, 0, meta=0x00)
    check("INPUT sent (see the server log for 'first INPUT received')", True)

    # 8. bye -> slot free -> reconnect
    sess.sendto(header(T_BYE, key), (HOST, SESSION))
    time.sleep(0.3)
    s3 = udp()
    k3, d3 = connect(s3, "second-player")
    check("after BYE the slot is free again", k3 is not None, "" if k3 else ("REJECT " + REJECT_NAMES.get(d3[10], "?") if d3 else "no answer"))

    # 9. drop by silence: no HB from s3 for > 6 s
    if k3:
        print("  ... waiting 7 s without heartbeats")
        time.sleep(7.0)
        s4 = udp()
        k4, d4 = connect(s4, "third-player")
        check("silent client dropped after 6 s, slot free", k4 is not None, "" if k4 else ("REJECT " + REJECT_NAMES.get(d4[10], "?") if d4 else "no answer"))
        if k4:
            s4.sendto(header(T_BYE, k4), (HOST, SESSION))

    ok = sum(1 for _, r in results if r)
    print("%d / %d checks passed" % (ok, len(results)))


if __name__ == "__main__":
    main()
