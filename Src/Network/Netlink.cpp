// 11/23 main6 netlink, phase 1 (LAN prototype). See Netlink.h.
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
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
  const char kStateMagic[4] = { 'N', 'L', 'S', '1' };
  const char kConfigMagic[4] = { 'N', 'L', 'C', '1' };
  std::string s_hostSettings;

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

  // recv exactly n bytes, giving up when run goes false or the socket closes
  bool RecvAll(SOCKET s, char *p, size_t n, const std::atomic<bool> &run)
  {
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
        continue;
      int k = recv(s, p, (int) (n > 0x40000 ? 0x40000 : n), 0);
      if (k <= 0)
        return false;
      p += k;
      n -= (size_t) k;
    }
    return true;
  }

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

      // 2. our game settings at once (the client is waiting for them before it loads the game)
      {
        uint32_t size = (uint32_t) s_hostSettings.size();
        if (!(SendAll(c, kConfigMagic, 4) && SendAll(c, (const char *) &size, 4) && SendAll(c, s_hostSettings.data(), size)))
        {
          closesocket(c);
          continue;
        }
      }

      // then wait for the link (N and our node are known once the ring is ready), then the header
      while (s_hostRun.load() && (s_hostN.load() < 2 || s_hostBroken.load()))
        Sleep(50);
      bool ok = s_hostRun.load();
      if (ok)
      {
        // our state at the end of frame F (the net board arms the queue for F+1.. in the same frame)
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
          uint32_t size = (uint32_t) st.size();
          ok = SendAll(c, kStateMagic, 4) && SendAll(c, (const char *) &size, 4) && SendAll(c, st.data(), st.size());
          printf("[Netlink] Host: save state sent (%u bytes)\n", size);
        }
      }
      if (ok)
      {
        std::vector<char> head;
        uint8_t idx = (uint8_t) (s_hostNode.load() - 1), others = (uint8_t) (s_hostN.load() - 1);
        PutMsg(head, &kGuid, 8);
        PutMsg(head, &idx, 1);
        PutMsg(head, &others, 1);
        ok = SendAll(c, head.data(), head.size());
        printf("[Netlink] Host: link of %d machines, we are node %d\n", s_hostN.load(), s_hostNode.load());
      }

      // 3. frames out, inputs in
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
          if (!SendAll(c, f.data(), f.size())) { ok = false; break; }
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
          int zero = 0;
          SendAll(c, (const char *) &zero, 4);   // "link broken", as on the ring
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
  std::vector<char> s_clientState;              // the host's save state, waiting for Main.cpp
  bool s_clientStateHave = false;
  std::atomic<bool> s_clientStateLoaded{ false };

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

    // the host's game settings (Main.cpp is waiting for them before it loads the game)
    {
      char pre[8];
      uint32_t size = 0;
      bool ok = RecvAll(s, pre, 8, s_clientRun) && memcmp(pre, kConfigMagic, 4) == 0;
      if (ok)
      {
        memcpy(&size, pre + 4, 4);
        ok = size < 0x10000;
      }
      std::string text(size, '\0');
      if (ok && size)
        ok = RecvAll(s, &text[0], size, s_clientRun);
      if (!ok)
      {
        printf("[Netlink] Client: no settings from the host\n");
        closesocket(s);
        SatelliteTapFeedBroken();
        WSACleanup();
        return;
      }
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
      {
        std::lock_guard<std::mutex> lk(s_clientStateMutex);
        s_clientSettings = std::move(kv);
        s_clientSettingsHave = true;
      }
      s_clientSettingsCv.notify_all();
    }

    // then the host's save state; the link data only once Main.cpp has loaded it
    printf("[Netlink] Client: waiting for the host's state\n");
    {
      char pre[8];
      uint32_t size = 0;
      bool ok = RecvAll(s, pre, 8, s_clientRun) && memcmp(pre, kStateMagic, 4) == 0;
      if (ok)
      {
        memcpy(&size, pre + 4, 4);
        ok = size > 0 && size < 0x10000000;
      }
      std::vector<char> st;
      if (ok)
      {
        st.resize(size);
        ok = RecvAll(s, st.data(), size, s_clientRun);
      }
      if (!ok)
      {
        printf("[Netlink] Client: no state from the host\n");
        closesocket(s);
        SatelliteTapFeedBroken();
        WSACleanup();
        return;
      }
      printf("[Netlink] Client: state received (%u bytes)\n", size);
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

    std::vector<char> buf(65536);
    uint64_t bytes = 0;
    while (s_clientRun.load())
    {
      fd_set rs;
      FD_ZERO(&rs);
      FD_SET(s, &rs);
      timeval tv = { 0, 200000 };
      int r = select(0, &rs, nullptr, nullptr, &tv);
      if (r < 0)
        break;
      if (r == 0)
        continue;
      int n = recv(s, buf.data(), (int) buf.size(), 0);
      if (n <= 0)
        break;
      bytes += (uint64_t) n;
      SatelliteTapFeed(buf.data(), n);
    }
    s_clientOn.store(false);
    {
      std::lock_guard<std::mutex> lk(s_clientSendMutex);
      s_clientSock = INVALID_SOCKET;
    }
    closesocket(s);
    SatelliteTapFeedBroken();
    printf("[Netlink] Client: disconnected (%llu bytes received)\n", (unsigned long long) bytes);
    WSACleanup();
  }
}

const char *const kNetlinkGameKeys[] = {
  "PowerPCFrequency", "IdleSkip", "P1Invincible", "TimerStop", "SpikeJr", "Spikeout8Link", "Spikeout8Players",
  "SameStart", "BossHPx1", "PlayerAttackMul", "EnemyKnockMul", "EnemyKnockMulY", "EnemyKnockMulXZ", "InfiniteSpecial",
  "StartStage", "StartArea", "StageTestMenu" };
const int kNetlinkGameKeyCount = (int) (sizeof(kNetlinkGameKeys) / sizeof(kNetlinkGameKeys[0]));

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

bool NetlinkClientWaitSettings(const std::string &host, int port, int timeoutMs, std::vector<std::pair<std::string, std::string>> &out)
{
  NetlinkClientStart(host, port);
  std::unique_lock<std::mutex> lk(s_clientStateMutex);
  if (!s_clientSettingsCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [] { return s_clientSettingsHave; }))
    return false;
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
