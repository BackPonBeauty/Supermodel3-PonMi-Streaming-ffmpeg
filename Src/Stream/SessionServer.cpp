/*
 * SessionServer.cpp
 *
 * Streaming (P4, 2026-11-16): one slot's session on the server. See SessionServer.h.
 */

#include "SessionServer.h"
#include "RemoteInputSystem.h"
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <cstdio>
#include <random>
#include <string>

std::atomic<bool> g_streamHumanJoined{false};

// ---------------------------------------------------------------------------------------------------------
// Media HELLO validator (used by RtpSender / UdpAudioSender)
// ---------------------------------------------------------------------------------------------------------

namespace StreamProto
{
  static std::mutex s_validatorLock;
  static MediaHelloValidator s_validator;

  void SetMediaHelloValidator(MediaHelloValidator validator)
  {
    std::lock_guard<std::mutex> lock(s_validatorLock);
    s_validator = validator;
  }

  bool IsValidMediaHello(const char *buf, int len, const sockaddr_in &from)
  {
    MediaHelloValidator v;
    {
      std::lock_guard<std::mutex> lock(s_validatorLock);
      v = s_validator;
    }
    return v ? v(buf, len, from) : false;
  }
}

using namespace StreamProto;

static uint64_t NowMs()
{
  return GetTickCount64();
}

static std::string AddrToString(const sockaddr_in &a)
{
  char ip[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, (void *) &a.sin_addr, ip, sizeof(ip));
  return std::string(ip);
}

static SOCKET OpenUdp(int port)
{
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET)
    return s;
  // Ignore ICMP "port unreachable" (otherwise recvfrom fails after sending to a closed client port)
  BOOL newBehavior = FALSE;
  DWORD bytes = 0;
  WSAIoctl(s, SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior), nullptr, 0, &bytes, nullptr, nullptr);
  sockaddr_in local = {};
  local.sin_family = AF_INET;
  local.sin_port = htons((u_short) port);
  local.sin_addr.s_addr = INADDR_ANY;
  if (bind(s, (sockaddr *) &local, sizeof(local)) != 0)
  {
    printf("[Session] bind(%d) failed: %d\n", port, WSAGetLastError());
    closesocket(s);
    return INVALID_SOCKET;
  }
  DWORD timeout = 200;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *) &timeout, sizeof(timeout));
  return s;
}

// ---------------------------------------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------------------------------------

bool SessionServer::Start(int slot, int sessionPort, int inputPort, int width, int height, const std::string &codec, Callbacks cb)
{
  Stop();

  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);

  m_slot = slot;
  m_width = width;
  m_height = height;
  m_codecBit = (codec == "H265") ? CODEC_H265 : CODEC_H264;
  m_cb = cb;

  m_sessionSock = OpenUdp(sessionPort);
  m_inputSock = OpenUdp(inputPort);
  if (m_sessionSock == INVALID_SOCKET || m_inputSock == INVALID_SOCKET)
  {
    Stop();
    return false;
  }

  SetMediaHelloValidator([this](const char *buf, int len, const sockaddr_in &from) {
    return ValidateMediaHello(buf, len, from);
  });

  m_running.store(true);
  m_sessionThread = std::thread(&SessionServer::SessionLoop, this);
  m_inputThread = std::thread(&SessionServer::InputLoop, this);

  printf("[Session] Slot %d ready: session port %d, input port %d, %dx%d %s (protocol v%d)\n",
         slot, sessionPort, inputPort, width, height, m_codecBit == CODEC_H265 ? "H265" : "H264", (int) kVersion);
  return true;
}

void SessionServer::Stop()
{
  bool wasRunning = m_running.exchange(false);
  if (wasRunning)
  {
    SetMediaHelloValidator(nullptr);
    if (m_connected.load())
    {
      // Tell the client we are going away
      Header bye;
      sockaddr_in to;
      {
        std::lock_guard<std::mutex> lock(m_lock);
        FillHeader(bye, T_BYE, m_key);
        to = m_clientAddr;
      }
      SendTo(&bye, sizeof(bye), to);
    }
  }
  if (m_sessionThread.joinable()) m_sessionThread.join();
  if (m_inputThread.joinable()) m_inputThread.join();
  if (m_sessionSock != INVALID_SOCKET) { closesocket(m_sessionSock); m_sessionSock = INVALID_SOCKET; }
  if (m_inputSock != INVALID_SOCKET) { closesocket(m_inputSock); m_inputSock = INVALID_SOCKET; }
  m_connected.store(false);
}

std::string SessionServer::GetPlayerName()
{
  std::lock_guard<std::mutex> lock(m_lock);
  return m_playerName;
}

// ---------------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------------

void SessionServer::SendTo(const void *data, int len, const sockaddr_in &to)
{
  if (m_sessionSock != INVALID_SOCKET)
    sendto(m_sessionSock, (const char *) data, len, 0, (const sockaddr *) &to, sizeof(to));
}

bool SessionServer::IsLocalOrPrivate(const sockaddr_in &addr)
{
  uint32_t ip = ntohl(addr.sin_addr.s_addr);
  uint8_t a = (uint8_t) (ip >> 24), b = (uint8_t) (ip >> 16);
  if (a == 127) return true;                          // this PC
  if (a == 10) return true;                           // 10.0.0.0/8
  if (a == 172 && b >= 16 && b <= 31) return true;    // 172.16.0.0/12
  if (a == 192 && b == 168) return true;              // 192.168.0.0/16
  if (a == 169 && b == 254) return true;              // link-local
  return false;
}

// Caller holds m_lock.
bool SessionServer::FromClient(const sockaddr_in &from, uint32_t key)
{
  return m_connected.load() && key == m_key && from.sin_addr.s_addr == m_clientAddr.sin_addr.s_addr;
}

bool SessionServer::ValidateMediaHello(const char *buf, int len, const sockaddr_in &from)
{
  Header h;
  if (!ParseHeader(buf, len, &h) || h.version != kVersion || h.type != T_MEDIA_HELLO)
    return false;
  std::lock_guard<std::mutex> lock(m_lock);
  return FromClient(from, h.key);
}

void SessionServer::Disconnect(const char *why)
{
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_connected.load())
      return;
    m_connected.store(false);
    printf("[Session] Slot %d: %s disconnected (%s) -> standing still\n", m_slot, m_playerName.c_str(), why);
    m_playerName.clear();
    m_key = 0;
    m_haveSeq = false;
    m_lastMeta = 0;
  }
  if (CRemoteInputSystem *ri = CRemoteInputSystem::GetInstance())
    ri->ClearRemoteState();
  if (m_cb.onDisconnected)
    m_cb.onDisconnected(why);
}

// ---------------------------------------------------------------------------------------------------------
// Session port
// ---------------------------------------------------------------------------------------------------------

void SessionServer::HandleHello(const char *buf, int len, const sockaddr_in &from)
{
  auto reject = [&](uint8_t reason, const char *text) {
    Reject r;
    FillHeader(r.h, T_REJECT, 0);
    r.reason = reason;
    SendTo(&r, sizeof(r), from);
    printf("[Session] Slot %d: HELLO from %s:%d rejected (%s)\n", m_slot, AddrToString(from).c_str(), ntohs(from.sin_port), text);
  };

  Header h = {};
  ParseHeader(buf, len, &h);   // already checked by the caller
  if (h.version != kVersion)
  {
    reject(R_VERSION, "protocol version differs");
    return;
  }
  if (len < (int) sizeof(Hello))
  {
    reject(R_BAD_REQUEST, "short HELLO");
    return;
  }
  Hello hello;
  memcpy(&hello, buf, sizeof(hello));
  char nameBuf[kMaxNameLength + 1] = {};
  memcpy(nameBuf, hello.name, kMaxNameLength);
  std::string name(nameBuf);
  if (name.empty())
    name = "guest";

  if (!(hello.codecs & m_codecBit))
  {
    reject(R_CODEC, "client cannot decode our codec");
    return;
  }

  bool resend = false;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_connected.load())
    {
      // A repeated HELLO from the connected client (WELCOME lost) just gets the same WELCOME again
      if (from.sin_addr.s_addr == m_clientAddr.sin_addr.s_addr && from.sin_port == m_clientAddr.sin_port)
        resend = true;
      else
      {
        reject(R_SLOT_BUSY, "slot busy");
        return;
      }
    }
  }

  if (!resend)
  {
    if (hello.login == LOGIN_DISCORD)
    {
      // Discord membership check comes in P6 (SV-16). Until then only local / home-network clients.
      if (!IsLocalOrPrivate(from))
      {
        reject(R_LOGIN_UNAVAILABLE, "Discord check not available yet");
        return;
      }
    }
    else if (!IsLocalOrPrivate(from))
    {
      reject(R_LOGIN_REQUIRED, "no login from the internet");
      return;
    }
  }

  Welcome w;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (!resend)
    {
      std::random_device rd;
      do { m_key = ((uint32_t) rd() << 16) ^ (uint32_t) rd(); } while (m_key == 0);
      m_clientAddr = from;
      m_playerName = name;
      m_lastHb = NowMs();
      m_lastKeyReq = 0;
      m_haveSeq = false;
      m_lastMeta = 0;
      m_connected.store(true);
    }
    FillHeader(w.h, T_WELCOME, m_key);
    w.width = (uint16_t) m_width;
    w.height = (uint16_t) m_height;
    w.codec = m_codecBit;
    w.slot = (uint8_t) m_slot;
  }
  SendTo(&w, sizeof(w), from);

  if (!resend)
  {
    g_streamHumanJoined.store(true);
    if (CRemoteInputSystem *ri = CRemoteInputSystem::GetInstance())
      ri->ClearRemoteState();
    else
      printf("[Session] Note: InputSystem is not NONE, so the client's input is not used on this server.\n");
    printf("[Session] Slot %d: %s connected from %s:%d\n", m_slot, name.c_str(), AddrToString(from).c_str(), ntohs(from.sin_port));
    if (m_cb.onConnected)
      m_cb.onConnected(name, AddrToString(from));
  }
}

void SessionServer::SessionLoop()
{
  char buf[2048];
  while (m_running.load())
  {
    sockaddr_in from = {};
    int fromLen = sizeof(from);
    int n = recvfrom(m_sessionSock, buf, sizeof(buf), 0, (sockaddr *) &from, &fromLen);

    // Drop check (SV-08): no heartbeat for 6 s
    if (m_connected.load())
    {
      uint64_t last;
      {
        std::lock_guard<std::mutex> lock(m_lock);
        last = m_lastHb;
      }
      if (NowMs() - last > kDropTimeoutMs)
        Disconnect("no heartbeat for 6 s");
    }

    if (n <= 0)
      continue;
    Header h;
    if (!ParseHeader(buf, n, &h))
      continue;

    if (h.type == T_HELLO)
    {
      HandleHello(buf, n, from);
      continue;
    }

    bool ok;
    {
      std::lock_guard<std::mutex> lock(m_lock);
      ok = (h.version == kVersion) && FromClient(from, h.key);
      if (ok)
        m_clientAddr.sin_port = from.sin_port;   // follow the client's session port (NAT may change it)
    }
    if (!ok)
      continue;

    switch (h.type)
    {
    case T_HB:
    {
      {
        std::lock_guard<std::mutex> lock(m_lock);
        m_lastHb = NowMs();
      }
      Header echo;
      FillHeader(echo, T_HB, h.key);
      SendTo(&echo, sizeof(echo), from);
      break;
    }
    case T_PING:
    {
      if (n < (int) sizeof(Ping))
        break;
      Ping p;
      memcpy(&p, buf, sizeof(p));
      p.h.type = T_PONG;
      SendTo(&p, sizeof(p), from);
      {
        std::lock_guard<std::mutex> lock(m_lock);
        m_lastHb = NowMs();   // any packet from the client also keeps it alive
      }
      break;
    }
    case T_KEYREQ:
    {
      bool fire = false;
      {
        std::lock_guard<std::mutex> lock(m_lock);
        uint64_t now = NowMs();
        if (now - m_lastKeyReq >= kKeyReqMinIntervalMs)
        {
          m_lastKeyReq = now;
          fire = true;
        }
      }
      if (fire && m_cb.onKeyframeRequest)
        m_cb.onKeyframeRequest();
      break;
    }
    case T_STATUS:
    {
      if (n < (int) sizeof(Status))
        break;
      Status s;
      memcpy(&s, buf, sizeof(s));
      m_lossRate.store(s.lossRate);
      m_lastStatusTime.store(GetTickCount());
      break;
    }
    case T_BYE:
      Disconnect("client said bye");
      break;
    default:
      break;
    }
  }
}

// ---------------------------------------------------------------------------------------------------------
// Input port
// ---------------------------------------------------------------------------------------------------------

void SessionServer::InputLoop()
{
  char buf[256];
  bool firstInputLogged = false;
  while (m_running.load())
  {
    sockaddr_in from = {};
    int fromLen = sizeof(from);
    int n = recvfrom(m_inputSock, buf, sizeof(buf), 0, (sockaddr *) &from, &fromLen);
    if (n < (int) sizeof(Input))
      continue;
    Header h;
    if (!ParseHeader(buf, n, &h) || h.version != kVersion || h.type != T_INPUT)
      continue;
    Input in;
    memcpy(&in, buf, sizeof(in));

    uint8_t risen = 0;
    {
      std::lock_guard<std::mutex> lock(m_lock);
      if (!FromClient(from, h.key))
        continue;
      if (m_haveSeq && (int16_t) (in.seq - m_lastSeq) <= 0)
        continue;   // old or repeated packet
      m_haveSeq = true;
      m_lastSeq = in.seq;
      risen = in.meta & ~m_lastMeta;
      m_lastMeta = in.meta;
    }

    if (CRemoteInputSystem *ri = CRemoteInputSystem::GetInstance())
    {
      CRemoteInputSystem::RemoteState st;
      st.keys = in.keys;
      st.padConnected = in.padConnected != 0;
      st.buttons = in.buttons;
      for (int a = 0; a < 6; a++)
        st.axes[a] = in.axes[a];
      st.hat = in.hat;
      ri->SetRemoteState(st);
    }
    if (!firstInputLogged)
    {
      printf("[Session] Slot %d: first INPUT received (keys=%04x buttons=%08x)\n", m_slot, in.keys, in.buttons);
      firstInputLogged = true;
    }
    if (risen && m_cb.onMetaKeys)
      m_cb.onMetaKeys(risen);
  }
}
