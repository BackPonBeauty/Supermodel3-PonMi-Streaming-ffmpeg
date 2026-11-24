// 11/23 main6 netlink (phase 1 LAN prototype, phase 2 compression / identity check). See Netlink.h.
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <bcrypt.h>
#include <zlib.h>
#include "Netlink.h"
#include "Stream/Satellite.h"
#include "Stream/RemoteInputSystem.h"
#include "Stream/Protocol.h"
#include "Stream/DiscordAuth.h"
#include "Stream/RoomPassword.h"
#include "Stream/Ticket.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdarg>
#include "OSD/Logger.h"

// 11/24 NetlinkTime: process start = static initialisation of this file (before main)
static const std::chrono::steady_clock::time_point s_nltStart = std::chrono::steady_clock::now();

void NetlinkTime(const char *fmt, ...)
{
  char text[512];
  va_list vl;
  va_start(vl, fmt);
  vsnprintf(text, sizeof(text), fmt, vl);
  va_end(vl);
  SYSTEMTIME lt;
  GetLocalTime(&lt);
  const long long ms = (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_nltStart).count();
  printf("[NLT %02d:%02d:%02d.%03d +%lldms] %s\n", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds, ms, text);
  InfoLog("[NLT %02d:%02d:%02d.%03d +%lldms] %s", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds, ms, text);
}

namespace
{
  const uint64_t kGuid = 0x5bf177da34872;   // the ring's link test GUID (SimNetBoard.cpp netGUID)
  const size_t kMaxQueue = 1800;              // frames (~30 s, the state transfer included) before a slow client is dropped
  const int kProtoVersion = 2;
  const char kHelloMagic[4] = { 'N', 'L', 'H', '2' };    // client -> host: who we are (ver, game, exe, rom)
  const char kErrorMagic[4] = { 'N', 'L', 'E', '1' };    // host -> client: refused, and why
  const char kConfigMagic[4] = { 'N', 'L', 'C', '1' };   // host -> client: game settings
  const char kStateMagic[4] = { 'N', 'L', 'S', '2' };    // host -> client: zlib'ed save state (u32 packed, u32 raw)
  std::string s_hostSettings;
  std::string s_identity;                     // "ver=..\ngame=..\nexe=..\nrom=..\n" (NetlinkSetIdentity)

  void PutMsg(std::vector<char> &out, const void *p, int len)
  {
    const char *l = (const char *) &len;
    out.insert(out.end(), l, l + 4);
    out.insert(out.end(), (const char *) p, (const char *) p + len);
  }

  bool SendAll(SOCKET s, const char *p, size_t n)
  {
    while (n)
    {
      int k = send(s, p, (int) (n > 0x40000 ? 0x40000 : n), 0);
      if (k <= 0)
        return false;
      p += k;
      n -= (size_t) k;
    }
    return true;
  }

  bool SendBlock(SOCKET s, const char magic[4], const std::string &text)
  {
    uint32_t size = (uint32_t) text.size();
    return SendAll(s, magic, 4) && SendAll(s, (const char *) &size, 4) && SendAll(s, text.data(), text.size());
  }

  // recv exactly n bytes, giving up when run goes false, the socket closes, or (timeoutMs > 0) the time is up
  bool RecvAll(SOCKET s, char *p, size_t n, const std::atomic<bool> &run, int timeoutMs = 0)
  {
    auto t0 = std::chrono::steady_clock::now();
    while (n)
    {
      fd_set rs;
      FD_ZERO(&rs);
      FD_SET(s, &rs);
      timeval tv = { 0, 200000 };
      int r = select(0, &rs, nullptr, nullptr, &tv);
      if (r < 0 || !run.load())
        return false;
      if (r == 0)
      {
        if (timeoutMs > 0 && std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(timeoutMs))
          return false;
        continue;
      }
      int k = recv(s, p, (int) (n > 0x40000 ? 0x40000 : n), 0);
      if (k <= 0)
        return false;
      p += k;
      n -= (size_t) k;
    }
    return true;
  }

  // a block: 4-byte magic, u32 size, size bytes of text
  bool RecvBlock(SOCKET s, char magic[4], std::string &text, const std::atomic<bool> &run, int timeoutMs, uint32_t maxSize)
  {
    char pre[8];
    if (!RecvAll(s, pre, 8, run, timeoutMs))
      return false;
    memcpy(magic, pre, 4);
    uint32_t size;
    memcpy(&size, pre + 4, 4);
    if (size > maxSize)
      return false;
    text.assign(size, '\0');
    return size == 0 || RecvAll(s, &text[0], size, run, timeoutMs);
  }

  std::vector<std::pair<std::string, std::string>> ParseLines(const std::string &text)
  {
    std::vector<std::pair<std::string, std::string>> kv;
    size_t p = 0;
    while (p < text.size())
    {
      size_t e = text.find('\n', p);
      if (e == std::string::npos) e = text.size();
      std::string line = text.substr(p, e - p);
      size_t eq = line.find('=');
      if (eq != std::string::npos && eq > 0)
        kv.emplace_back(line.substr(0, eq), line.substr(eq + 1));
      p = e + 1;
    }
    return kv;
  }

  std::string Field(const std::string &text, const char *key)
  {
    for (const auto &p : ParseLines(text))
      if (p.first == key)
        return p.second;
    return std::string();
  }

  // ---- SHA-256 (Windows CNG) ----
  struct Sha256
  {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    Sha256()
    {
      if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
        BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    }
    ~Sha256()
    {
      if (h) BCryptDestroyHash(h);
      if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    void Add(const void *p, size_t n)
    {
      const uint8_t *b = (const uint8_t *) p;
      while (h && n)
      {
        ULONG k = (ULONG) (n > 0x40000000 ? 0x40000000 : n);
        BCryptHashData(h, (PUCHAR) b, k, 0);
        b += k;
        n -= k;
      }
    }
    std::string Hex()
    {
      uint8_t d[32] = {};
      if (h) BCryptFinishHash(h, d, 32, 0);
      char out[65];
      for (int i = 0; i < 32; i++)
        snprintf(out + i * 2, 3, "%02x", d[i]);
      return std::string(out, 64);
    }
  };

  // ---- zlib: one deflate / inflate stream per connection; every chunk is flushed (Z_SYNC_FLUSH) ----
  struct Packer
  {
    z_stream z = {};
    bool ok = false;
    Packer() { ok = deflateInit(&z, 6) == Z_OK; }
    ~Packer() { if (ok) deflateEnd(&z); }
    bool Pack(const std::vector<char> &in, std::vector<char> &out)
    {
      out.resize(deflateBound(&z, (uLong) in.size()) + 64);
      z.next_in = (Bytef *) in.data();
      z.avail_in = (uInt) in.size();
      size_t used = 0;
      for (;;)
      {
        z.next_out = (Bytef *) out.data() + used;
        z.avail_out = (uInt) (out.size() - used);
        int r = deflate(&z, Z_SYNC_FLUSH);
        if (r != Z_OK && r != Z_BUF_ERROR)
          return false;
        used = out.size() - z.avail_out;
        if (z.avail_out != 0)
          break;
        out.resize(out.size() * 2);
      }
      out.resize(used);
      return true;
    }
  };

  struct Unpacker
  {
    z_stream z = {};
    bool ok = false;
    Unpacker() { ok = inflateInit(&z) == Z_OK; }
    ~Unpacker() { if (ok) inflateEnd(&z); }
    bool Unpack(const std::vector<char> &in, std::vector<char> &out)
    {
      out.resize(in.size() * 8 + 0x10000);
      z.next_in = (Bytef *) in.data();
      z.avail_in = (uInt) in.size();
      size_t used = 0;
      for (;;)
      {
        z.next_out = (Bytef *) out.data() + used;
        z.avail_out = (uInt) (out.size() - used);
        int r = inflate(&z, Z_SYNC_FLUSH);
        if (r != Z_OK && r != Z_BUF_ERROR && r != Z_STREAM_END)
          return false;
        used = out.size() - z.avail_out;
        if (z.avail_in == 0 && z.avail_out != 0)
          break;
        if (z.avail_out == 0)
          out.resize(out.size() * 2);
        else if (r == Z_BUF_ERROR)
          return false;
      }
      out.resize(used);
      return true;
    }
  };

  // A chunk of what the tap parser reads: u8 type (0 = the bytes, 1 = XOR with the previous frame's bytes) + payload.
  // On the wire: u32 packed size + the packed chunk.
  bool SendChunk(SOCKET c, Packer &pk, const std::vector<char> &bytes, std::vector<char> &prev, bool frame)
  {
    std::vector<char> plain;
    plain.reserve(bytes.size() + 1);
    if (frame && prev.size() == bytes.size())
    {
      plain.push_back(1);
      for (size_t i = 0; i < bytes.size(); i++)
        plain.push_back((char) (bytes[i] ^ prev[i]));
    }
    else
    {
      plain.push_back(0);
      plain.insert(plain.end(), bytes.begin(), bytes.end());
    }
    if (frame)
      prev = bytes;
    std::vector<char> packed;
    if (!pk.Pack(plain, packed))
      return false;
    uint32_t n = (uint32_t) packed.size();
    return SendAll(c, (const char *) &n, 4) && SendAll(c, packed.data(), packed.size());
  }

  // 127.0.0.0/8, 10/8, 172.16/12, 192.168/16, 169.254/16: this PC or the home network (like SessionServer's LB-07)
  bool IsLocalOrPrivate(const sockaddr_in &a)
  {
    uint32_t ip = ntohl(a.sin_addr.s_addr);
    return (ip >> 24) == 127 || (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 16) == 0xA9FE;
  }

  // ---- Host ----
  // 11/23 (owner): two channels on the host node - [0] the NETLINK player (NetlinkServe, slot base + 2) and [1] the
  // satellite (NetlinkWatchPort: a spectator clone outside the ring, this PC / the home network only, no inputs).
  // Each has its own queue and gets its own state when it connects; one save serves the channels asking that frame.
  struct HostChan
  {
    bool watch = false;
    std::thread thread;
    std::atomic<bool> streaming{ false };       // a client has its header: frames are queued
    std::atomic<bool> overflow{ false };
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::vector<char>> queue;
    std::atomic<bool> wantState{ false };       // thread -> net board: save at the end of the next frame
    std::atomic<bool> awaitState{ false };      // net board: this channel's save is under way
    std::atomic<bool> stateReady{ false };      // Main.cpp -> thread
    std::vector<char> state;                    // (under mutex)
  };
  HostChan s_chan[2];
  std::atomic<bool> s_hostDrop{ false };        // phase 3: the room session of our client ended (Main.cpp) - channel 0
  std::atomic<bool> s_hostRun{ false };
  std::atomic<bool> s_hostBroken{ false };
  std::atomic<int> s_hostN{ 0 }, s_hostNode{ 0 };
  std::atomic<bool> s_hostSaveNow{ false };     // net board -> Main.cpp

  void HostInput(const NetlinkInputMsg &m)
  {
    CRemoteInputSystem *ri = CRemoteInputSystem::GetInstance();
    if (!ri)
      return;
    CRemoteInputSystem::RemoteState st;
    st.padConnected = m.pad != 0;
    st.buttons = m.buttons;
    st.hat = m.hat;
    ri->SetRemoteState(st);
  }

  void HostClear()
  {
    if (CRemoteInputSystem *ri = CRemoteInputSystem::GetInstance())
      ri->ClearRemoteState();
  }

  // The client says who it is; anything that would make its world another one is refused (LB-3).
  std::string HostCheckHello(const std::string &theirs)
  {
    const char *keys[] = { "ver", "game", "exe", "rom" };
    const char *what[] = { "protocol version", "game", "Supermodel build (exe)", "ROM set" };
    for (int i = 0; i < 4; i++)
    {
      std::string a = Field(s_identity, keys[i]), b = Field(theirs, keys[i]);
      if (a != b)
        return std::string("different ") + what[i] + " (host " + a.substr(0, 16) + ", client " + b.substr(0, 16) + ")";
    }
    return std::string();
  }

  void HostThread(int port, int ci)
  {
    HostChan &ch = s_chan[ci];
    const char *who = ch.watch ? "satellite" : "client";
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_port = htons((u_short) port);
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    if (ls == INVALID_SOCKET || bind(ls, (sockaddr *) &la, sizeof(la)) != 0 || listen(ls, 1) != 0)
    {
      printf("[Netlink] Host: cannot listen on TCP %d for the %s (%d)\n", port, who, WSAGetLastError());
      if (ls != INVALID_SOCKET) closesocket(ls);
      WSACleanup();
      return;
    }
    printf("[Netlink] Host: listening on TCP %d (%s)\n", port, who);

    while (s_hostRun.load())
    {
      // 1. a client
      SOCKET c = INVALID_SOCKET;
      sockaddr_in ca = {};
      while (s_hostRun.load())
      {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(ls, &rs);
        timeval tv = { 0, 200000 };
        if (select(0, &rs, nullptr, nullptr, &tv) > 0)
        {
          int cl = sizeof(ca);
          c = accept(ls, (sockaddr *) &ca, &cl);
          if (c != INVALID_SOCKET)
            break;
        }
      }
      if (c == INVALID_SOCKET)
        break;
      int one = 1;
      setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));
      char ip[64] = "?";
      inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));
      NetlinkTime("host: %s %s connected", who, ip);
      if (!ch.watch)
        s_hostDrop.store(false);
      bool sessionMode = false;

      // 2. who it is (10 s), then our game settings at once (it waits for them before it loads the game)
      {
        char magic[4];
        std::string hello;
        if (!RecvBlock(c, magic, hello, s_hostRun, 10000, 0x10000) || memcmp(magic, kHelloMagic, 4) != 0)
        {
          printf("[Netlink] Host: client %s did not say who it is (an older client?), closed\n", ip);
          SendBlock(c, kErrorMagic, "the host speaks netlink protocol version 2; update the client");
          closesocket(c);
          continue;
        }
        std::string why = HostCheckHello(hello);
        // phase 3: with the room's session running here (a NETLINK slot), only its client - same IP, its session
        // key from WELCOME - gets the link data; without one (a test on this PC / the home network) no outsider
        sessionMode = !ch.watch && StreamProto::HasMediaHelloValidator();
        if (why.empty() && ch.watch && !IsLocalOrPrivate(ca))
          why = "the satellite port is for this PC / the home network only";
        else if (why.empty() && sessionMode)
        {
          StreamProto::Header mh;
          StreamProto::FillHeader(mh, StreamProto::T_MEDIA_HELLO, (uint32_t) strtoul(Field(hello, "key").c_str(), nullptr, 10));
          if (!StreamProto::IsValidMediaHello((const char *) &mh, (int) sizeof(mh), ca))
            why = "join this slot from the lobby (no room session for this address)";
        }
        else if (why.empty() && !IsLocalOrPrivate(ca))
          why = "this slot has no room session: only this PC / the home network can join";
        if (!why.empty())
        {
          printf("[Netlink] Host: client %s refused: %s\n", ip, why.c_str());
          SendBlock(c, kErrorMagic, why);
          closesocket(c);
          continue;
        }
        if (!SendBlock(c, kConfigMagic, s_hostSettings))
        {
          closesocket(c);
          continue;
        }
        NetlinkTime("host: settings sent to the %s", who);
      }

      // 3. wait for the link (N and our node are known once the ring is ready); our state at the end of frame F
      // (the net board arms the queue for F+1.. in the same frame), packed
      while (s_hostRun.load() && (s_hostN.load() < 2 || s_hostBroken.load()))
        Sleep(50);
      bool ok = s_hostRun.load();
      if (ok)
      {
        {
          std::lock_guard<std::mutex> lk(ch.mutex);
          ch.queue.clear();
          ch.overflow.store(false);
          ch.state.clear();
          ch.stateReady.store(false);
        }
        ch.wantState.store(true);
        NetlinkTime("host: save state asked for (the %s)", who);
        auto t0 = std::chrono::steady_clock::now();
        while (s_hostRun.load() && !ch.stateReady.load() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(15))
          Sleep(5);
        std::vector<char> st;
        {
          std::lock_guard<std::mutex> lk(ch.mutex);
          st.swap(ch.state);
        }
        ch.wantState.store(false);
        NetlinkTime("host: save state ready (%u bytes)", (unsigned) st.size());
        if (!ch.stateReady.load() || st.empty())
        {
          printf("[Netlink] Host: no save state for the %s, dropped\n", who);
          ok = false;
        }
        else
        {
          uLongf packedSize = compressBound((uLong) st.size());
          std::vector<char> packed(packedSize);
          if (compress2((Bytef *) packed.data(), &packedSize, (const Bytef *) st.data(), (uLong) st.size(), 1) != Z_OK)
            ok = false;
          else
          {
            uint32_t sizes[2] = { (uint32_t) packedSize, (uint32_t) st.size() };
            NetlinkTime("host: save state packed (%u -> %u bytes)", sizes[1], sizes[0]);
            ok = SendAll(c, kStateMagic, 4) && SendAll(c, (const char *) sizes, 8) && SendAll(c, packed.data(), packedSize);
            NetlinkTime("host: save state sent to the %s (%u bytes, %u packed)", who, sizes[1], sizes[0]);
          }
        }
      }

      // 4. the ring's link header, then frames out (XOR with the last one, one zlib stream), inputs in
      Packer pk;
      std::vector<char> prev;
      if (ok)
      {
        std::vector<char> head;
        uint8_t idx = (uint8_t) (s_hostNode.load() - 1), others = (uint8_t) (s_hostN.load() - 1);
        PutMsg(head, &kGuid, 8);
        PutMsg(head, &idx, 1);
        PutMsg(head, &others, 1);
        ok = pk.ok && SendChunk(c, pk, head, prev, false);
        printf("[Netlink] Host: link of %d machines, we are node %d (%s)\n", s_hostN.load(), s_hostNode.load(), who);
      }
      std::vector<char> in;
      uint64_t frames = 0;
      while (ok && s_hostRun.load())
      {
        std::deque<std::vector<char>> batch;
        {
          std::unique_lock<std::mutex> lk(ch.mutex);
          ch.cv.wait_for(lk, std::chrono::milliseconds(5), [&ch] { return !ch.queue.empty() || !s_hostRun.load(); });
          batch.swap(ch.queue);
        }
        for (auto &f : batch)
        {
          if (!SendChunk(c, pk, f, prev, true)) { ok = false; break; }
          frames++;
        }
        if (!ok)
          break;
        if (ch.overflow.load())
        {
          printf("[Netlink] Host: %s too slow (%zu frames behind), dropped\n", who, kMaxQueue);
          break;
        }
        if (s_hostBroken.load())
        {
          std::vector<char> zero(4, 0);           // "link broken", as on the ring
          SendChunk(c, pk, zero, prev, false);
          printf("[Netlink] Host: the link is broken, %s told\n", who);
          break;
        }
        if (!ch.watch && s_hostDrop.load())
        {
          printf("[Netlink] Host: the client's room session ended\n");
          break;
        }
        // inputs (non-blocking look)
        for (;;)
        {
          fd_set rs;
          FD_ZERO(&rs);
          FD_SET(c, &rs);
          timeval tv = { 0, 0 };
          if (select(0, &rs, nullptr, nullptr, &tv) <= 0)
            break;
          char buf[4096];
          int n = recv(c, buf, sizeof(buf), 0);
          if (n <= 0) { ok = false; break; }
          in.insert(in.end(), buf, buf + n);
        }
        size_t pos = 0;
        while (in.size() - pos >= 4)
        {
          int len;
          memcpy(&len, in.data() + pos, 4);
          if (len < 0 || len > 4096) { ok = false; break; }
          if (in.size() - pos < 4 + (size_t) len)
            break;
          // inputs on this socket only without a room session (a home test); with one they come over UDP to the
          // SessionServer (idle kick and all)
          if (!ch.watch && !sessionMode && len == (int) sizeof(NetlinkInputMsg) && memcmp(in.data() + pos + 4, "NLI1", 4) == 0)
          {
            NetlinkInputMsg m;
            memcpy(&m, in.data() + pos + 4, sizeof(m));
            HostInput(m);
          }
          pos += 4 + (size_t) len;
        }
        in.erase(in.begin(), in.begin() + pos);
      }
      ch.streaming.store(false);
      {
        std::lock_guard<std::mutex> lk(ch.mutex);
        ch.queue.clear();
      }
      if (!ch.watch && !sessionMode)
        HostClear();                              // (with a session the SessionServer clears the input)
      closesocket(c);
      printf("[Netlink] Host: %s %s gone after %llu frames\n", who, ip, (unsigned long long) frames);
    }
    closesocket(ls);
    WSACleanup();
  }

  // ---- Client state ----
  std::thread s_clientThread;
  std::atomic<bool> s_clientRun{ false };
  std::atomic<bool> s_clientOn{ false };
  std::atomic<bool> s_clientEverOn{ false };    // phase 3: we were playing (the link data flowed) at some point
  std::mutex s_clientSendMutex;
  SOCKET s_clientSock = INVALID_SOCKET;
  std::mutex s_clientStateMutex;
  std::condition_variable s_clientSettingsCv;
  std::vector<std::pair<std::string, std::string>> s_clientSettings;
  bool s_clientSettingsHave = false;            // (under s_clientStateMutex)
  bool s_clientFailed = false;                  // (under s_clientStateMutex) refused / no answer / ended
  std::string s_clientError;
  std::vector<char> s_clientState;              // the host's save state, waiting for Main.cpp
  bool s_clientStateHave = false;
  std::atomic<bool> s_clientStateLoaded{ false };
  std::atomic<bool> s_clientWatch{ false };     // 11/23: a satellite (spectator) - no inputs, no room session

  void ClientFail(const std::string &why)
  {
    printf("[Netlink] Client: %s\n", why.c_str());
    {
      std::lock_guard<std::mutex> lk(s_clientStateMutex);
      if (!s_clientFailed)
        s_clientError = why;                    // the first reason counts
      s_clientFailed = true;
    }
    s_clientSettingsCv.notify_all();
  }

  // ---- Client: the room's UDP session (phase 3), as the video client has it (Stream/Protocol.h, StreamClient.cpp) ----
  // HELLO (codecs = 0: a netlink client) -> nonce challenge -> entry ticket (Discord, Worker) / room password ->
  // WELCOME (session key); heartbeat every 2 s; INPUT to base + 0 (the SessionServer applies it and does the idle
  // kick); BYE when leaving; no answer for 6 s = gone.
  struct NlSession
  {
    bool want = false;                          // a lobby join (NetlinkClientSetSession), not a home test
    int slot = 0;
    std::string name, password;
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in sessAddr = {}, inputAddr = {};
    uint32_t key = 0;
    std::atomic<bool> up{ false };
    std::atomic<bool> run{ false };
    std::atomic<uint64_t> lastReply{ 0 };
    std::mutex lock;                            // seq, notice
    uint16_t seq = 0;
    std::string notice;
    uint64_t noticeTime = 0;
    std::thread thread;
  };
  NlSession s_sess;

  // ---- the client's latency display ----
  uint64_t NowUs()
  {
    static LARGE_INTEGER freq = []() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t) (c.QuadPart * 1000000 / freq.QuadPart);
  }
  std::atomic<int> s_rttMs{ -1 };
  std::atomic<uint64_t> s_wireBytes{ 0 };
  std::mutex s_latLock;
  bool s_latPending = false;
  uint64_t s_latStart = 0;
  uint32_t s_latButtons = 0xFFFFFFFF;
  uint8_t s_latHat = 0xFF;
  bool s_latHaveSig = false;
  uint32_t s_latSig = 0;
  int s_latLast = -1;
  int s_latHist[10] = {};
  int s_latCount = 0;

  const char *RejectText(uint8_t reason)
  {
    using namespace StreamProto;
    switch (reason)
    {
    case R_VERSION:           return "The host runs a different version. Please update.";
    case R_NOT_MEMBER:        return "Refused: you are not a member of the Discord server.";
    case R_SLOT_BUSY:         return "That slot is in use. Please choose another slot.";
    case R_LOGIN_REQUIRED:    return "Refused: please log in to Discord (top of the lobby). Without login only this PC / home network can connect.";
    case R_LOGIN_UNAVAILABLE: return "Refused: the entry-ticket server could not check your Discord login now. Please try again later.";
    case R_TICKET_INVALID:    return "Refused: the host did not accept the entry ticket. Please try again.";
    case R_LOGIN_INVALID:     return "Refused: your Discord login has expired. Please log out and log in again.";
    case R_ALREADY_CONNECTED: return "Refused: your Discord account is already connected to another slot of this host.";
    case R_STREAM_SLOT:       return "That slot is a video (STREAM) slot.";
    case R_PASSWORD_WRONG:    return "Wrong password.";
    case R_PASSWORD_LOCKED:   return "Too many wrong passwords. Please wait 5 minutes and try again.";
    case R_PASSWORD_REQUIRED: return "This host needs a password.";
    default:                  return "Refused by the host.";
    }
  }

  void SessSend(const void *p, int n, const sockaddr_in &to)
  {
    sendto(s_sess.sock, (const char *) p, n, 0, (const sockaddr *) &to, sizeof(to));
  }

  // The handshake (5 s for each answer). True = WELCOME; else why.
  bool SessionHandshake(const std::string &host, std::string &why)
  {
    using namespace StreamProto;
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
    {
      why = "Cannot find the host " + host + ".";
      return false;
    }
    sockaddr_in addr = *(sockaddr_in *) res->ai_addr;
    freeaddrinfo(res);
    int base = SlotBasePort(s_sess.slot);
    s_sess.sessAddr = addr;
    s_sess.sessAddr.sin_port = htons((u_short) (base + PORT_SESSION));
    s_sess.inputAddr = addr;
    s_sess.inputAddr.sin_port = htons((u_short) (base + PORT_INPUT));

    s_sess.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sess.sock == INVALID_SOCKET)
    {
      why = "Cannot open a network socket.";
      return false;
    }
    BOOL newBehavior = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s_sess.sock, SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior), nullptr, 0, &bytes, nullptr, nullptr);
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    bind(s_sess.sock, (sockaddr *) &local, sizeof(local));
    DWORD timeout = 200;
    setsockopt(s_sess.sock, SOL_SOCKET, SO_RCVTIMEO, (const char *) &timeout, sizeof(timeout));

    Hello hello = {};
    FillHeader(hello.h, T_HELLO, 0);
    hello.codecs = 0;                           // a netlink client: no video
    hello.login = LOGIN_NONE;
    memcpy(hello.name, s_sess.name.c_str(), std::min(s_sess.name.size(), (size_t) kMaxNameLength));
    std::string token;
    bool loggedIn = DiscordAuth::GetAccessToken(token) && !token.empty() && token.size() <= 256;
    if (loggedIn)
      hello.login = LOGIN_DISCORD;
    char hostIp[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, (void *) &addr.sin_addr, hostIp, sizeof(hostIp));

    std::string ticketPayload, ticketSig;
    std::vector<char> packet;
    auto build = [&]() {
      size_t tl = ticketPayload.size();
      hello.ticketLen = (uint16_t) tl;
      packet.assign(sizeof(hello) + (tl ? tl + Ticket::kSigLength : 0), 0);
      memcpy(packet.data(), &hello, sizeof(hello));
      if (tl)
      {
        memcpy(packet.data() + sizeof(hello), ticketPayload.data(), tl);
        memcpy(packet.data() + sizeof(hello) + tl, ticketSig.data(), Ticket::kSigLength);
      }
    };
    build();

    char buf[512];
    uint64_t start = GetTickCount64(), lastSend = 0;
    int answers = 0;
    uint8_t ticketProblem = 0;
    while (GetTickCount64() - start < 5000 && s_clientRun.load())
    {
      if (GetTickCount64() - lastSend >= 500)
      {
        SessSend(packet.data(), (int) packet.size(), s_sess.sessAddr);
        lastSend = GetTickCount64();
      }
      sockaddr_in from = {};
      int fromLen = sizeof(from);
      int n = recvfrom(s_sess.sock, buf, sizeof(buf), 0, (sockaddr *) &from, &fromLen);
      Header h;
      if (n <= 0 || !ParseHeader(buf, n, &h))
        continue;
      if (h.type == T_REJECT && n >= (int) sizeof(Reject))
      {
        Reject r;
        memcpy(&r, buf, sizeof(r));
        if (r.reason == R_CHALLENGE && r.nonce != 0)
        {
          if (r.nonce == hello.nonce || ++answers > 3)
            continue;
          if ((r.needs & NEED_PASSWORD) && s_sess.password.empty())
          {
            why = RejectText(R_PASSWORD_REQUIRED);
            return false;
          }
          hello.nonce = r.nonce;
          memset(hello.pwProof, 0, sizeof(hello.pwProof));
          if (r.needs & NEED_PASSWORD)
            RoomPassword::Proof(r.nonce, s_sess.password, hello.pwProof);
          ticketPayload.clear();
          ticketSig.clear();
          if ((r.needs & NEED_TICKET) && loggedIn)
          {
            Ticket::Fetch f = Ticket::Request(token, hostIp, s_sess.slot, r.nonce, ticketPayload, ticketSig);
            if (f != Ticket::Fetch::Ok)
            {
              ticketPayload.clear();
              ticketSig.clear();
              ticketProblem = f == Ticket::Fetch::LoginInvalid ? R_LOGIN_INVALID : f == Ticket::Fetch::NotMember ? R_NOT_MEMBER : R_LOGIN_UNAVAILABLE;
            }
          }
          build();
          SessSend(packet.data(), (int) packet.size(), s_sess.sessAddr);
          lastSend = start = GetTickCount64();
          continue;
        }
        uint8_t reason = r.reason;
        if ((reason == R_LOGIN_REQUIRED || reason == R_TICKET_INVALID) && ticketProblem)
          reason = ticketProblem;
        why = RejectText(reason);
        return false;
      }
      if (h.type == T_WELCOME && n >= (int) sizeof(Welcome))
      {
        s_sess.key = h.key;
        s_sess.lastReply.store(GetTickCount64());
        return true;
      }
    }
    why = "No answer from " + host + " (slot " + std::to_string(s_sess.slot) + "). Is the host running?";
    return false;
  }

  void SessionBye()
  {
    if (s_sess.sock == INVALID_SOCKET || !s_sess.up.load())
      return;
    StreamProto::Header bye;
    StreamProto::FillHeader(bye, StreamProto::T_BYE, s_sess.key);
    for (int i = 0; i < 3; i++)
      SessSend(&bye, sizeof(bye), s_sess.sessAddr);
  }

  // the session or the link data is over: say why (the lobby shows it) and stop the client thread
  void ClientEnd(const std::string &why)
  {
    ClientFail(why);
    s_clientRun.store(false);
  }

  // Heartbeat, and what the host says (NOTICE, BYE); no answer for 6 s = gone
  void SessionThread()
  {
    using namespace StreamProto;
    uint64_t lastHb = 0, lastPing = 0;
    char buf[1024];
    while (s_sess.run.load())
    {
      uint64_t now = GetTickCount64();
      if (now - lastHb >= kHeartbeatIntervalMs)
      {
        Header h;
        FillHeader(h, T_HB, s_sess.key);
        SessSend(&h, sizeof(h), s_sess.sessAddr);
        lastHb = now;
      }
      if (now - lastPing >= 500)                // the round trip for the latency display (the host echoes it)
      {
        Ping p;
        FillHeader(p.h, T_PING, s_sess.key);
        p.clientTime = NowUs();
        SessSend(&p, sizeof(p), s_sess.sessAddr);
        lastPing = now;
      }
      if (now - s_sess.lastReply.load() > kDropTimeoutMs)
      {
        ClientEnd("The host stopped answering (no reply for 6 s).");
        break;
      }
      sockaddr_in from = {};
      int fromLen = sizeof(from);
      int n = recvfrom(s_sess.sock, buf, sizeof(buf), 0, (sockaddr *) &from, &fromLen);
      Header h;
      if (n <= 0 || !ParseHeader(buf, n, &h) || h.version != kVersion || h.key != s_sess.key)
        continue;
      s_sess.lastReply.store(GetTickCount64());
      if (h.type == T_PONG && n >= (int) sizeof(Ping))
      {
        Ping p;
        memcpy(&p, buf, sizeof(p));
        s_rttMs.store((int) ((NowUs() - p.clientTime + 500) / 1000));
      }
      else if (h.type == T_NOTICE && n >= (int) sizeof(Notice))
      {
        Notice nt;
        memcpy(&nt, buf, sizeof(nt));
        nt.text[sizeof(nt.text) - 1] = '\0';
        printf("[Netlink] Host says: %s\n", nt.text);
        std::lock_guard<std::mutex> lk(s_sess.lock);
        s_sess.notice = nt.text;
        s_sess.noticeTime = GetTickCount64();
      }
      else if (h.type == T_BYE)
      {
        std::string why;
        {
          std::lock_guard<std::mutex> lk(s_sess.lock);
          if (!s_sess.notice.empty() && GetTickCount64() - s_sess.noticeTime < 3000)
            why = s_sess.notice;            // the reason sent right before BYE (kick, no input)
        }
        s_sess.up.store(false);
        ClientEnd(why.empty() ? "The host ended the session." : why);
        break;
      }
    }
  }

  // ---- Client ----
  void ClientThread(std::string host, int port)
  {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = INVALID_SOCKET;

    // phase 3: a lobby join goes into the room first (entry ticket / password), then asks for the link data with
    // the session key
    if (s_sess.want)
    {
      std::string why;
      NetlinkTime("client: joining slot %d of %s", s_sess.slot, host.c_str());
      if (!SessionHandshake(host, why))
      {
        ClientFail(why);
        SatelliteTapFeedBroken();
        WSACleanup();
        return;
      }
      s_sess.up.store(true);
      s_sess.run.store(true);
      s_sess.thread = std::thread(SessionThread);
      NetlinkTime("client: in the room (slot %d)", s_sess.slot);
    }

    NetlinkTime("client: connecting to %s:%d", host.c_str(), port);
    uint64_t connectStart = GetTickCount64();
    while (s_clientRun.load())
    {
      if (s_sess.want && GetTickCount64() - connectStart > 10000)
        break;                                  // in the room but the link data port does not answer
      addrinfo hints = {}, *res = nullptr;
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      char ps[16];
      snprintf(ps, sizeof(ps), "%d", port);
      if (getaddrinfo(host.c_str(), ps, &hints, &res) == 0 && res)
      {
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET && connect(s, res->ai_addr, (int) res->ai_addrlen) != 0)
        {
          closesocket(s);
          s = INVALID_SOCKET;
        }
        freeaddrinfo(res);
      }
      if (s != INVALID_SOCKET)
        break;
      Sleep(500);
    }
    if (s == INVALID_SOCKET)
    {
      if (s_clientRun.load())
        ClientFail("The host's link data port (TCP " + std::to_string(port) + ") does not answer.");
      SatelliteTapFeedBroken();
      WSACleanup();
      return;
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));
    NetlinkTime("client: connected");

    auto quit = [&](const std::string &why)
    {
      ClientFail(why);
      closesocket(s);
      SatelliteTapFeedBroken();
      WSACleanup();
    };

    // who we are, then the host's answer: refused, or its game settings (Main.cpp waits for them)
    {
      std::string hello = s_identity;
      if (s_sess.want)
        hello += "key=" + std::to_string(s_sess.key) + "\n";   // our room session (the host checks it and our IP)
      if (!SendBlock(s, kHelloMagic, hello))
        return quit("cannot talk to the host");
      char magic[4];
      std::string text;
      if (!RecvBlock(s, magic, text, s_clientRun, 0, 0x10000))
        return quit("no answer from the host");
      if (memcmp(magic, kErrorMagic, 4) == 0)
        return quit("the host refused: " + text);
      if (memcmp(magic, kConfigMagic, 4) != 0)
        return quit("the host speaks another netlink protocol");
      {
        std::lock_guard<std::mutex> lk(s_clientStateMutex);
        s_clientSettings = ParseLines(text);
        s_clientSettingsHave = true;
      }
      s_clientSettingsCv.notify_all();
      NetlinkTime("client: host settings received");
    }

    // then the host's save state (packed); the link data only once Main.cpp has loaded it
    printf("[Netlink] Client: waiting for the host's state\n");
    {
      char pre[12];
      uint32_t sizes[2] = {};
      bool ok = RecvAll(s, pre, 12, s_clientRun) && memcmp(pre, kStateMagic, 4) == 0;
      if (ok)
      {
        memcpy(sizes, pre + 4, 8);
        ok = sizes[0] > 0 && sizes[0] < 0x10000000 && sizes[1] > 0 && sizes[1] < 0x10000000;
      }
      std::vector<char> packed, st;
      if (ok)
      {
        packed.resize(sizes[0]);
        ok = RecvAll(s, packed.data(), sizes[0], s_clientRun);
        NetlinkTime("client: state downloaded (%u bytes packed)", sizes[0]);
      }
      if (ok)
      {
        st.resize(sizes[1]);
        uLongf rawSize = sizes[1];
        ok = uncompress((Bytef *) st.data(), &rawSize, (const Bytef *) packed.data(), sizes[0]) == Z_OK && rawSize == sizes[1];
      }
      if (!ok)
        return quit("no state from the host");
      NetlinkTime("client: state unpacked (%u bytes, %u packed)", sizes[1], sizes[0]);
      s_clientStateLoaded.store(false);
      {
        std::lock_guard<std::mutex> lk(s_clientStateMutex);
        s_clientState = std::move(st);
        s_clientStateHave = true;
      }
      while (s_clientRun.load() && !s_clientStateLoaded.load())
        Sleep(5);
    }
    SatelliteTapFeedReset();
    {
      std::lock_guard<std::mutex> lk(s_clientSendMutex);
      s_clientSock = s;
    }
    s_clientOn.store(true);
    s_clientEverOn.store(true);

    // chunks: u32 packed size + packed (u8 type + payload)
    Unpacker up;
    std::vector<char> packed, plain, prev, bytes;
    uint64_t wire = 0, raw = 0;
    bool ok = up.ok;
    while (ok && s_clientRun.load())
    {
      uint32_t n;
      if (!RecvAll(s, (char *) &n, 4, s_clientRun) || n == 0 || n > 0x1000000)
        break;
      packed.resize(n);
      if (!RecvAll(s, packed.data(), n, s_clientRun))
        break;
      if (!up.Unpack(packed, plain) || plain.empty())
      {
        printf("[Netlink] Client: bad data from the host\n");
        break;
      }
      wire += 4 + n;
      s_wireBytes.fetch_add(4 + n);
      if (plain[0] == 1)
      {
        if (prev.size() != plain.size() - 1)
        {
          printf("[Netlink] Client: a frame without the one before it\n");
          break;
        }
        bytes.resize(prev.size());
        for (size_t i = 0; i < prev.size(); i++)
          bytes[i] = (char) (prev[i] ^ plain[i + 1]);
        prev = bytes;
      }
      else
      {
        bytes.assign(plain.begin() + 1, plain.end());
        if (bytes.size() > 0x100)
          prev = bytes;                             // a whole frame (not the header or "broken")
      }
      raw += bytes.size();
      SatelliteTapFeed(bytes.data(), (int) bytes.size());
    }
    s_clientOn.store(false);
    {
      std::lock_guard<std::mutex> lk(s_clientSendMutex);
      s_clientSock = INVALID_SOCKET;
    }
    closesocket(s);
    SatelliteTapFeedBroken();
    printf("[Netlink] Client: disconnected (%llu bytes received for %llu bytes of link data)\n", (unsigned long long) wire, (unsigned long long) raw);
    if (s_clientRun.load())
      ClientEnd("The link data from the host stopped.");   // (a session end has said why already)
    WSACleanup();
  }
}

const char *const kNetlinkGameKeys[] = {
  "PowerPCFrequency", "IdleSkip", "P1Invincible", "TimerStop", "SpikeJr", "Spikeout8Link", "Spikeout8Players",
  "SameStart", "BossHPx1", "PlayerAttackMul", "EnemyKnockMul", "EnemyKnockMulY", "EnemyKnockMulXZ", "InfiniteSpecial",
  "StartStage", "StartArea", "StageTestMenu",
  "NetlinkBuffer" };   // 11/23 (owner): not a game value - the host's launcher picks how many frames the client keeps in hand
const int kNetlinkGameKeyCount = (int) (sizeof(kNetlinkGameKeys) / sizeof(kNetlinkGameKeys[0]));

void NetlinkSetIdentity(const std::string &game, const std::vector<std::pair<const uint8_t *, size_t>> &romParts)
{
  Sha256 exe;
  char path[MAX_PATH] = {};
  if (GetModuleFileNameA(nullptr, path, MAX_PATH))
    if (FILE *f = fopen(path, "rb"))
    {
      std::vector<char> buf(1 << 20);
      size_t n;
      while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
        exe.Add(buf.data(), n);
      fclose(f);
    }
  Sha256 rom;
  for (const auto &p : romParts)
  {
    uint64_t size = p.second;
    rom.Add(&size, sizeof(size));
    if (p.first && p.second)
      rom.Add(p.first, p.second);
  }
  s_identity = "ver=" + std::to_string(kProtoVersion) + "\ngame=" + game + "\nexe=" + exe.Hex() + "\nrom=" + rom.Hex() + "\n";
  printf("[Netlink] Identity: %s exe %s rom %s\n", game.c_str(), Field(s_identity, "exe").substr(0, 12).c_str(), Field(s_identity, "rom").substr(0, 12).c_str());
}

bool NetlinkHostStart(int port, const std::string &settings)
{
  if (s_chan[0].thread.joinable())
    return true;
  s_hostSettings = settings;
  s_hostRun.store(true);
  s_chan[0].watch = false;
  s_chan[0].thread = std::thread(HostThread, port, 0);
  return true;
}

bool NetlinkWatchStart(int port, const std::string &settings)
{
  if (s_chan[1].thread.joinable())
    return true;
  s_hostSettings = settings;
  s_hostRun.store(true);
  s_chan[1].watch = true;
  s_chan[1].thread = std::thread(HostThread, port, 1);
  return true;
}

void NetlinkHostStop()
{
  s_hostRun.store(false);
  for (auto &ch : s_chan)
  {
    ch.cv.notify_all();
    if (ch.thread.joinable())
      ch.thread.join();
  }
}

void NetlinkHostFrame(const uint8_t *commRAM, int numMachines, int node, int segSize)
{
  s_hostN.store(numMachines);
  s_hostNode.store(node);
  s_hostBroken.store(false);
  if (numMachines < 2 || segSize <= 0)
    return;
  // a channel that asks for a state this frame F: it is saved after F (Main.cpp), the channel gets F+1 on
  bool anyStreaming = false;
  bool armed[2] = {};
  for (int c = 0; c < 2; c++)
  {
    HostChan &ch = s_chan[c];
    if (ch.wantState.exchange(false))
    {
      ch.awaitState.store(true);
      ch.streaming.store(true);
      s_hostSaveNow.store(true);
      armed[c] = true;
    }
    else if (ch.streaming.load())
      anyStreaming = true;
  }
  if (!anyStreaming)
    return;
  // segs[i] = node (N - i), as 08 sends them: our slot j holds node ((k - 1 - j) mod N) + 1, so that is
  // slot (k + i) mod N (slot N, the copy of our own that came round, for 0)
  std::vector<char> f;
  f.reserve((size_t) numMachines * (segSize + 4));
  for (int i = 0; i < numMachines; i++)
  {
    int j = (node + i) % numMachines;
    if (j == 0)
      j = numMachines;
    PutMsg(f, commRAM + 0x100 + j * segSize, segSize);
  }
  for (int c = 0; c < 2; c++)
  {
    HostChan &ch = s_chan[c];
    if (armed[c] || !ch.streaming.load())
      continue;
    {
      std::lock_guard<std::mutex> lk(ch.mutex);
      if (ch.queue.size() >= kMaxQueue)
      {
        ch.overflow.store(true);
        continue;
      }
      ch.queue.push_back(f);
    }
    ch.cv.notify_all();
  }
}

bool NetlinkHostTakeSaveRequest()
{
  return s_hostSaveNow.exchange(false);
}

void NetlinkHostSetState(std::vector<char> &&st)
{
  for (auto &ch : s_chan)
  {
    if (!ch.awaitState.exchange(false))
      continue;
    std::lock_guard<std::mutex> lk(ch.mutex);
    ch.state = st;                              // (a copy: both channels may have asked in the same frame)
    ch.stateReady.store(true);
  }
}

void NetlinkHostBroken()
{
  s_hostBroken.store(true);
  s_hostN.store(0);
}

bool NetlinkClientStart(const std::string &host, int port)
{
  if (s_clientThread.joinable())
    return true;
  SatelliteTapExternalStart();
  s_clientRun.store(true);
  s_clientThread = std::thread(ClientThread, host, port);
  return true;
}

void NetlinkClientStop()
{
  s_clientRun.store(false);                     // we are leaving: the host closing the link data is no news
  SessionBye();                                 // phase 3: leave the room at once (the slot is free again)
  s_sess.up.store(false);
  s_sess.run.store(false);
  if (s_sess.thread.joinable())
    s_sess.thread.join();
  if (s_sess.sock != INVALID_SOCKET)
  {
    closesocket(s_sess.sock);
    s_sess.sock = INVALID_SOCKET;
  }
  if (!s_clientThread.joinable())
    return;
  s_clientRun.store(false);
  s_clientThread.join();
}

void NetlinkClientSetSession(int slot, const std::string &name, const std::string &password)
{
  s_sess.want = slot >= 1 && slot <= 8;
  s_sess.slot = slot;
  s_sess.name = name;
  s_sess.password = password;
}

bool NetlinkClientEnded(std::string &why)
{
  std::lock_guard<std::mutex> lk(s_clientStateMutex);
  if (!s_clientEverOn.load() || s_clientOn.load() || !s_clientFailed)
    return false;
  why = s_clientError;
  return true;
}

void NetlinkHostDropClient()
{
  s_hostDrop.store(true);
}

NetlinkStats NetlinkClientStats()
{
  NetlinkStats st;
  st.on = s_clientOn.load();
  st.session = s_sess.want;
  st.slot = s_sess.slot;
  st.node = g_satClone.node;
  st.rttMs = s_sess.want ? s_rttMs.load() : -1;
  st.backlog = SatelliteTapBacklog();
  // link data in the last second (measured over about one second between calls)
  static uint64_t lastT = 0, lastBytes = 0;
  static double kbps = 0;
  uint64_t t = GetTickCount64(), b = s_wireBytes.load();
  if (lastT == 0)
  {
    lastT = t;
    lastBytes = b;
  }
  else if (t - lastT >= 1000)
  {
    kbps = (double) (b - lastBytes) * 8.0 / (double) (t - lastT);
    lastT = t;
    lastBytes = b;
  }
  st.kbps = kbps;
  std::lock_guard<std::mutex> lk(s_latLock);
  st.inputMs = s_latLast;
  if (s_latCount > 0)
  {
    int n = s_latCount < 10 ? s_latCount : 10, sum = 0;
    for (int i = 0; i < n; i++)
      sum += s_latHist[i];
    st.inputAvgMs = sum / n;
  }
  return st;
}

void NetlinkLatencyMarkInput(uint32_t buttons, uint8_t hat)
{
  std::lock_guard<std::mutex> lk(s_latLock);
  if (buttons == s_latButtons && hat == s_latHat)
    return;
  static uint64_t lastChange = 0;
  bool first = s_latButtons == 0xFFFFFFFF;
  s_latButtons = buttons;
  s_latHat = hat;
  uint64_t now = NowUs();
  if (s_latPending && now - s_latStart > 1000000)
    s_latPending = false;                       // never came back (a press too short for the game to read): drop it
  // only a change after 0.5 s of no change is timed: with mashing, the echo of an earlier change would end it early
  if (!first && !s_latPending && now - lastChange >= 500000)
  {
    s_latPending = true;
    s_latStart = now;
  }
  lastChange = now;
}

void NetlinkLatencyInputPlayed(uint32_t sig)
{
  std::lock_guard<std::mutex> lk(s_latLock);
  bool changed = s_latHaveSig && sig != s_latSig;
  s_latHaveSig = true;
  s_latSig = sig;
  if (!changed || !s_latPending)
    return;
  int ms = (int) ((NowUs() - s_latStart + 500) / 1000);
  s_latPending = false;
  s_latLast = ms;
  s_latHist[s_latCount % 10] = ms;
  s_latCount++;
}

bool NetlinkClientWaitSettings(const std::string &host, int port, int timeoutMs, std::vector<std::pair<std::string, std::string>> &out, std::string &error)
{
  NetlinkClientStart(host, port);
  std::unique_lock<std::mutex> lk(s_clientStateMutex);
  if (!s_clientSettingsCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [] { return s_clientSettingsHave || s_clientFailed; }))
  {
    error = "no answer from the host in time";
    return false;
  }
  if (s_clientFailed)
  {
    error = s_clientError;
    return false;
  }
  out = s_clientSettings;
  return true;
}

bool NetlinkClientTakeState(std::vector<char> &st)
{
  std::lock_guard<std::mutex> lk(s_clientStateMutex);
  if (!s_clientStateHave)
    return false;
  st = std::move(s_clientState);
  s_clientState.clear();
  s_clientStateHave = false;
  return true;
}

void NetlinkClientStateLoaded()
{
  s_clientStateLoaded.store(true);
}

bool NetlinkClientOn()
{
  return s_clientOn.load();
}

void NetlinkClientSetWatch(bool watch)
{
  s_clientWatch.store(watch);
}

bool NetlinkClientIsWatch()
{
  return s_clientWatch.load();
}

void NetlinkClientSendInput(const NetlinkInputMsg &m)
{
  if (s_clientWatch.load())
    return;                                     // a satellite only watches
  // phase 3: in a room, the input goes to the SessionServer over UDP (as the video client's does)
  if (s_sess.want)
  {
    if (!s_sess.up.load())
      return;
    StreamProto::Input in = {};
    {
      std::lock_guard<std::mutex> lk(s_sess.lock);
      StreamProto::FillHeader(in.h, StreamProto::T_INPUT, s_sess.key);
      in.seq = ++s_sess.seq;
    }
    in.padConnected = m.pad;
    in.buttons = m.buttons;
    in.hat = m.hat;
    SessSend(&in, sizeof(in), s_sess.inputAddr);
    return;
  }
  std::lock_guard<std::mutex> lk(s_clientSendMutex);
  if (s_clientSock == INVALID_SOCKET)
    return;
  char out[4 + sizeof(NetlinkInputMsg)];
  int len = (int) sizeof(NetlinkInputMsg);
  memcpy(out, &len, 4);
  memcpy(out + 4, &m, sizeof(m));
  SendAll(s_clientSock, out, sizeof(out));
}
