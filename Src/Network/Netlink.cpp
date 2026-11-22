// 11/23 main6 netlink (phase 1 LAN prototype, phase 2 compression / identity check). See Netlink.h.
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <zlib.h>
#include "Netlink.h"
#include "Stream/Satellite.h"
#include "Stream/RemoteInputSystem.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

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

  // ---- Host ----
  std::thread s_hostThread;
  std::atomic<bool> s_hostRun{ false };
  std::atomic<bool> s_hostStreaming{ false };   // a client has its header: frames are queued
  std::atomic<bool> s_hostOverflow{ false };
  std::atomic<bool> s_hostBroken{ false };
  std::atomic<int> s_hostN{ 0 }, s_hostNode{ 0 };
  std::mutex s_hostMutex;
  std::condition_variable s_hostCv;
  std::deque<std::vector<char>> s_hostQueue;
  std::atomic<bool> s_hostWantState{ false };   // thread -> net board: save at the end of the next frame
  std::atomic<bool> s_hostSaveNow{ false };     // net board -> Main.cpp
  std::atomic<bool> s_hostStateReady{ false };  // Main.cpp -> thread
  std::vector<char> s_hostState;                // (under s_hostMutex)

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

  void HostThread(int port)
  {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_port = htons((u_short) port);
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    if (ls == INVALID_SOCKET || bind(ls, (sockaddr *) &la, sizeof(la)) != 0 || listen(ls, 1) != 0)
    {
      printf("[Netlink] Host: cannot listen on TCP %d (%d)\n", port, WSAGetLastError());
      if (ls != INVALID_SOCKET) closesocket(ls);
      WSACleanup();
      return;
    }
    printf("[Netlink] Host: listening on TCP %d\n", port);

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
      printf("[Netlink] Host: client %s connected\n", ip);

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
      }

      // 3. wait for the link (N and our node are known once the ring is ready); our state at the end of frame F
      // (the net board arms the queue for F+1.. in the same frame), packed
      while (s_hostRun.load() && (s_hostN.load() < 2 || s_hostBroken.load()))
        Sleep(50);
      bool ok = s_hostRun.load();
      if (ok)
      {
        {
          std::lock_guard<std::mutex> lk(s_hostMutex);
          s_hostQueue.clear();
          s_hostOverflow.store(false);
          s_hostState.clear();
          s_hostStateReady.store(false);
        }
        s_hostWantState.store(true);
        auto t0 = std::chrono::steady_clock::now();
        while (s_hostRun.load() && !s_hostStateReady.load() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(15))
          Sleep(5);
        std::vector<char> st;
        {
          std::lock_guard<std::mutex> lk(s_hostMutex);
          st.swap(s_hostState);
        }
        s_hostWantState.store(false);
        if (!s_hostStateReady.load() || st.empty())
        {
          printf("[Netlink] Host: no save state for the client, dropped\n");
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
            ok = SendAll(c, kStateMagic, 4) && SendAll(c, (const char *) sizes, 8) && SendAll(c, packed.data(), packedSize);
            printf("[Netlink] Host: save state sent (%u bytes, %u packed)\n", sizes[1], sizes[0]);
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
        printf("[Netlink] Host: link of %d machines, we are node %d\n", s_hostN.load(), s_hostNode.load());
      }
      std::vector<char> in;
      uint64_t frames = 0;
      while (ok && s_hostRun.load())
      {
        std::deque<std::vector<char>> batch;
        {
          std::unique_lock<std::mutex> lk(s_hostMutex);
          s_hostCv.wait_for(lk, std::chrono::milliseconds(5), [] { return !s_hostQueue.empty() || !s_hostRun.load(); });
          batch.swap(s_hostQueue);
        }
        for (auto &f : batch)
        {
          if (!SendChunk(c, pk, f, prev, true)) { ok = false; break; }
          frames++;
        }
        if (!ok)
          break;
        if (s_hostOverflow.load())
        {
          printf("[Netlink] Host: client too slow (%zu frames behind), dropped\n", kMaxQueue);
          break;
        }
        if (s_hostBroken.load())
        {
          std::vector<char> zero(4, 0);           // "link broken", as on the ring
          SendChunk(c, pk, zero, prev, false);
          printf("[Netlink] Host: the link is broken, client told\n");
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
          if (len == (int) sizeof(NetlinkInputMsg) && memcmp(in.data() + pos + 4, "NLI1", 4) == 0)
          {
            NetlinkInputMsg m;
            memcpy(&m, in.data() + pos + 4, sizeof(m));
            HostInput(m);
          }
          pos += 4 + (size_t) len;
        }
        in.erase(in.begin(), in.begin() + pos);
      }
      s_hostStreaming.store(false);
      {
        std::lock_guard<std::mutex> lk(s_hostMutex);
        s_hostQueue.clear();
      }
      HostClear();
      closesocket(c);
      printf("[Netlink] Host: client %s gone after %llu frames\n", ip, (unsigned long long) frames);
    }
    closesocket(ls);
    WSACleanup();
  }

  // ---- Client ----
  std::thread s_clientThread;
  std::atomic<bool> s_clientRun{ false };
  std::atomic<bool> s_clientOn{ false };
  std::mutex s_clientSendMutex;
  SOCKET s_clientSock = INVALID_SOCKET;
  std::mutex s_clientStateMutex;
  std::condition_variable s_clientSettingsCv;
  std::vector<std::pair<std::string, std::string>> s_clientSettings;
  bool s_clientSettingsHave = false;            // (under s_clientStateMutex)
  bool s_clientFailed = false;                  // (under s_clientStateMutex) refused / no answer
  std::string s_clientError;
  std::vector<char> s_clientState;              // the host's save state, waiting for Main.cpp
  bool s_clientStateHave = false;
  std::atomic<bool> s_clientStateLoaded{ false };

  void ClientFail(const std::string &why)
  {
    printf("[Netlink] Client: %s\n", why.c_str());
    {
      std::lock_guard<std::mutex> lk(s_clientStateMutex);
      s_clientFailed = true;
      s_clientError = why;
    }
    s_clientSettingsCv.notify_all();
  }

  void ClientThread(std::string host, int port)
  {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = INVALID_SOCKET;
    printf("[Netlink] Client: connecting to %s:%d\n", host.c_str(), port);
    while (s_clientRun.load())
    {
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
      WSACleanup();
      return;
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));
    printf("[Netlink] Client: connected\n");

    auto quit = [&](const std::string &why)
    {
      ClientFail(why);
      closesocket(s);
      SatelliteTapFeedBroken();
      WSACleanup();
    };

    // who we are, then the host's answer: refused, or its game settings (Main.cpp waits for them)
    {
      if (!SendBlock(s, kHelloMagic, s_identity))
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
      }
      if (ok)
      {
        st.resize(sizes[1]);
        uLongf rawSize = sizes[1];
        ok = uncompress((Bytef *) st.data(), &rawSize, (const Bytef *) packed.data(), sizes[0]) == Z_OK && rawSize == sizes[1];
      }
      if (!ok)
        return quit("no state from the host");
      printf("[Netlink] Client: state received (%u bytes, %u packed)\n", sizes[1], sizes[0]);
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
    WSACleanup();
  }
}

const char *const kNetlinkGameKeys[] = {
  "PowerPCFrequency", "IdleSkip", "P1Invincible", "TimerStop", "SpikeJr", "Spikeout8Link", "Spikeout8Players",
  "SameStart", "BossHPx1", "PlayerAttackMul", "EnemyKnockMul", "EnemyKnockMulY", "EnemyKnockMulXZ", "InfiniteSpecial",
  "StartStage", "StartArea", "StageTestMenu" };
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
  if (s_hostThread.joinable())
    return true;
  s_hostSettings = settings;
  s_hostRun.store(true);
  s_hostThread = std::thread(HostThread, port);
  return true;
}

void NetlinkHostStop()
{
  if (!s_hostThread.joinable())
    return;
  s_hostRun.store(false);
  s_hostCv.notify_all();
  s_hostThread.join();
}

void NetlinkHostFrame(const uint8_t *commRAM, int numMachines, int node, int segSize)
{
  s_hostN.store(numMachines);
  s_hostNode.store(node);
  s_hostBroken.store(false);
  if (numMachines >= 2 && segSize > 0 && s_hostWantState.exchange(false))
  {
    // frame F: the state is saved after it (Main.cpp), the client gets F+1 on
    s_hostSaveNow.store(true);
    s_hostStreaming.store(true);
    return;
  }
  if (!s_hostStreaming.load() || numMachines < 2 || segSize <= 0)
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
  {
    std::lock_guard<std::mutex> lk(s_hostMutex);
    if (s_hostQueue.size() >= kMaxQueue)
    {
      s_hostOverflow.store(true);
      return;
    }
    s_hostQueue.push_back(std::move(f));
  }
  s_hostCv.notify_all();
}

bool NetlinkHostTakeSaveRequest()
{
  return s_hostSaveNow.exchange(false);
}

void NetlinkHostSetState(std::vector<char> &&st)
{
  std::lock_guard<std::mutex> lk(s_hostMutex);
  s_hostState = std::move(st);
  s_hostStateReady.store(true);
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
  if (!s_clientThread.joinable())
    return;
  s_clientRun.store(false);
  s_clientThread.join();
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

void NetlinkClientSendInput(const NetlinkInputMsg &m)
{
  std::lock_guard<std::mutex> lk(s_clientSendMutex);
  if (s_clientSock == INVALID_SOCKET)
    return;
  char out[4 + sizeof(NetlinkInputMsg)];
  int len = (int) sizeof(NetlinkInputMsg);
  memcpy(out, &len, 4);
  memcpy(out + 4, &m, sizeof(m));
  SendAll(s_clientSock, out, sizeof(out));
}
