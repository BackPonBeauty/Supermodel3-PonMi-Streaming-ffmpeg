// 11/20 (owner): Spikeout FE satellite (spectator) cabinet "09". See Satellite.h.
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "Satellite.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------------------------------------
// 09: the link relay (08 -> 09 -> 01, unchanged) and the clone tap
// ---------------------------------------------------------------------------------------------------------
namespace
{
  // 0 = off, 1 = connecting to the next cabinet, 2 = waiting for the previous cabinet, 3 = relaying
  std::atomic<int> s_relayState{ 0 };
  std::atomic<bool> s_relayRun{ false };
  std::atomic<uint64_t> s_relayBytes{ 0 };

  // ---- Clone tap (SatelliteClone = 1): a copy of the stream 08 -> 01, cut into the link's messages ----
  // TCPSend frames every message as a native int length + data. 08 sends, during the link test: GUID (8 bytes)
  // twice, its machine index (1 byte), the number of other machines (1 byte); then every frame N segments.
  bool s_tapOn = false;
  std::mutex s_tapMutex;
  std::condition_variable s_tapCv;
  std::deque<SatLinkEvent> s_tapQueue;
  std::atomic<int> s_tapBacklog{ 0 };

  struct TapParser
  {
    std::vector<char> buf;
    size_t pos = 0;
    int phase = 0;                          // 0 = unknown, 1 = testing (got a GUID), 2 = got the index, 3 = ready
    int numMachines = 0;
    int index = 0;                          // the sender's machine index (0-based)
    SatLinkEvent frame;

    void Push(SatLinkEvent &&e)
    {
      {
        std::lock_guard<std::mutex> lk(s_tapMutex);
        s_tapQueue.push_back(std::move(e));
        s_tapBacklog.store((int) s_tapQueue.size());
      }
      s_tapCv.notify_all();
    }

    void Reset()
    {
      buf.clear();
      pos = 0;
      phase = 0;
      frame.segs.clear();
    }

    void Message(const char *p, int len)
    {
      if (len == 0)                         // "link broken"
      {
        phase = 0;
        frame.segs.clear();
        SatLinkEvent e;
        e.type = SatLinkEvent::Broken;
        Push(std::move(e));
        return;
      }
      if (len == 8)                         // GUID: a (new) link test
      {
        phase = 1;
        frame.segs.clear();
        return;
      }
      if (len == 1)
      {
        if (phase == 1)
        {
          phase = 2;                        // the sender's machine index
          index = (uint8_t) p[0];
        }
        else if (phase == 2)
        {
          numMachines = (uint8_t) p[0] + 1;
          phase = 3;
          frame.segs.clear();
          SatLinkEvent e;
          e.type = SatLinkEvent::Link;
          e.numMachines = numMachines;
          e.node = index + 1;
          Push(std::move(e));
        }
        return;
      }
      if (phase != 3 || len < 0x100)
        return;
      frame.segs.emplace_back(p, p + len);
      if ((int) frame.segs.size() == numMachines)
      {
        frame.type = SatLinkEvent::Frame;
        frame.numMachines = numMachines;
        Push(std::move(frame));
        frame = SatLinkEvent();
      }
    }

    void Feed(const char *p, int n)
    {
      buf.insert(buf.end(), p, p + n);
      for (;;)
      {
        if (buf.size() - pos < 4)
          break;
        int len;
        memcpy(&len, buf.data() + pos, 4);
        if (len < 0 || len > 0x100000)      // lost track: start over (should not happen on TCP)
        {
          Reset();
          return;
        }
        if (buf.size() - pos < 4 + (size_t) len)
          break;
        Message(buf.data() + pos + 4, len);
        pos += 4 + len;
      }
      if (pos > 65536)
      {
        buf.erase(buf.begin(), buf.begin() + pos);
        pos = 0;
      }
    }
  };

  void RelayThread(int portIn, std::string addrOut, int portOut)
  {
    TapParser tap;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_port = htons((u_short) portIn);
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    if (ls == INVALID_SOCKET || bind(ls, (sockaddr *) &la, sizeof(la)) != 0 || listen(ls, 1) != 0)
    {
      printf("[Satellite] Relay: cannot listen on port %d (%d)\n", portIn, WSAGetLastError());
      if (ls != INVALID_SOCKET) closesocket(ls);
      s_relayState.store(0);
      WSACleanup();
      return;
    }
    printf("[Satellite] Relay: %d -> %s:%d\n", portIn, addrOut.c_str(), portOut);

    std::vector<char> buf(65536);
    while (s_relayRun.load())
    {
      // 1. connect to the next cabinet (like the cabinets themselves: first out, then wait for in)
      s_relayState.store(1);
      SOCKET out = INVALID_SOCKET;
      while (s_relayRun.load())
      {
        addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port[16];
        snprintf(port, sizeof(port), "%d", portOut);
        if (getaddrinfo(addrOut.c_str(), port, &hints, &res) == 0 && res)
        {
          out = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
          if (out != INVALID_SOCKET && connect(out, res->ai_addr, (int) res->ai_addrlen) != 0)
          {
            closesocket(out);
            out = INVALID_SOCKET;
          }
          freeaddrinfo(res);
        }
        if (out != INVALID_SOCKET)
          break;
        Sleep(200);
      }
      if (out == INVALID_SOCKET)
        break;
      int one = 1;
      setsockopt(out, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));

      // 2. wait for the previous cabinet
      s_relayState.store(2);
      SOCKET in = INVALID_SOCKET;
      while (s_relayRun.load())
      {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(ls, &rs);
        timeval tv = { 0, 200000 };
        if (select(0, &rs, nullptr, nullptr, &tv) > 0)
        {
          in = accept(ls, nullptr, nullptr);
          if (in != INVALID_SOCKET)
            break;
        }
      }
      if (in == INVALID_SOCKET)
      {
        closesocket(out);
        break;
      }
      setsockopt(in, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));
      printf("[Satellite] Relay: connected\n");
      tap.Reset();

      // 3. pass the bytes through unchanged
      s_relayState.store(3);
      while (s_relayRun.load())
      {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(in, &rs);
        timeval tv = { 0, 200000 };
        int r = select(0, &rs, nullptr, nullptr, &tv);
        if (r < 0)
          break;
        if (r == 0)
          continue;
        int n = recv(in, buf.data(), (int) buf.size(), 0);
        if (n <= 0)
          break;
        int sent = 0;
        while (sent < n)
        {
          int k = send(out, buf.data() + sent, n - sent, 0);
          if (k <= 0)
            break;
          sent += k;
        }
        if (sent < n)
          break;
        s_relayBytes.fetch_add((uint64_t) n);
        if (s_tapOn)
          tap.Feed(buf.data(), n);
      }
      if (s_tapOn)
      {
        tap.Reset();
        SatLinkEvent e;
        e.type = SatLinkEvent::Broken;
        tap.Push(std::move(e));
      }
      // One side closed: close the other too, so the next cabinet sees the link go down as it would
      // without us, then wait for a new connection.
      printf("[Satellite] Relay: disconnected\n");
      closesocket(in);
      closesocket(out);
    }
    closesocket(ls);
    s_relayState.store(0);
    WSACleanup();
  }

  std::thread s_tapRelay;
}

SatCloneState g_satClone;
SatInputRecord g_satInRec;
SatViewState g_satView;
int SatInputPort(unsigned reg) { return reg == 0x04 ? 0 : reg == 0x08 ? 1 : reg == 0x0C ? 2 : -1; }

bool SatelliteTapStart(int portIn, const std::string &addrOut, int portOut)
{
  if (s_tapRelay.joinable())
    return true;
  s_tapOn = true;
  s_relayRun.store(true);
  s_relayState.store(1);
  s_tapRelay = std::thread(RelayThread, portIn, addrOut, portOut);
  return true;
}

void SatelliteTapStop()
{
  s_relayRun.store(false);
  s_tapCv.notify_all();
  if (s_tapRelay.joinable())
    s_tapRelay.join();
}

// 11/23 main6 netlink: no relay; Network/Netlink.cpp's client thread hands us the bytes.
namespace { TapParser s_extTap; std::mutex s_extMutex; }

void SatelliteTapExternalStart()
{
  s_tapOn = true;
  s_relayRun.store(true);
}

void SatelliteTapFeed(const char *p, int n)
{
  std::lock_guard<std::mutex> lk(s_extMutex);
  s_extTap.Feed(p, n);
}

void SatelliteTapFeedReset()
{
  std::lock_guard<std::mutex> lk(s_extMutex);
  s_extTap.Reset();
}

void SatelliteTapFeedBroken()
{
  std::lock_guard<std::mutex> lk(s_extMutex);
  s_extTap.Reset();
  SatLinkEvent e;
  e.type = SatLinkEvent::Broken;
  s_extTap.Push(std::move(e));
}

int SatelliteTapRelayState() { return s_relayState.load(); }
int SatelliteTapBacklog() { return s_tapBacklog.load(); }

bool SatelliteTapPop(SatLinkEvent &e, int timeoutMs)
{
  std::unique_lock<std::mutex> lk(s_tapMutex);
  if (s_tapQueue.empty() && timeoutMs > 0)
    s_tapCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [] { return !s_tapQueue.empty() || !s_relayRun.load(); });
  if (s_tapQueue.empty())
    return false;
  e = std::move(s_tapQueue.front());
  s_tapQueue.pop_front();
  s_tapBacklog.store((int) s_tapQueue.size());
  return true;
}
