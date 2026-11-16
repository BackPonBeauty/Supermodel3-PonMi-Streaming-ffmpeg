/*
 * SessionServer.cpp
 *
 * Streaming (P4, 2026-11-16): one slot's session on the server. See SessionServer.h.
 */

#include "SessionServer.h"
#include "RemoteInputSystem.h"
#include "RoomPassword.h"
#include "Ticket.h"
#include "Http.h"
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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
  m_ipThread = std::thread(&SessionServer::PublicIpLoop, this);
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
  if (m_ipThread.joinable()) m_ipThread.join();
  if (m_sessionSock != INVALID_SOCKET) { closesocket(m_sessionSock); m_sessionSock = INVALID_SOCKET; }
  if (m_inputSock != INVALID_SOCKET) { closesocket(m_inputSock); m_inputSock = INVALID_SOCKET; }
  m_connected.store(false);
  if (m_userLock)
  {
    CloseHandle((HANDLE) m_userLock);
    m_userLock = nullptr;
  }
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

void SessionServer::SendNotice(const char *text)
{
  if (!m_connected.load())
    return;
  Notice notice = {};
  sockaddr_in to;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    FillHeader(notice.h, T_NOTICE, m_key);
    to = m_clientAddr;
  }
  snprintf(notice.text, sizeof(notice.text), "%s", text);
  SendTo(&notice, sizeof(notice), to);
}

void SessionServer::SendNoticeAndBye(const char *text, const char *why)
{
  if (!m_connected.load())
    return;
  Header bye;
  sockaddr_in to;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    FillHeader(bye, T_BYE, m_key);
    to = m_clientAddr;
  }
  SendNotice(text);
  SendTo(&bye, sizeof(bye), to);
  Disconnect(why);
}

void SessionServer::Kick()
{
  SendNoticeAndBye("You were removed from the slot by the host.", "kicked by the host");
}

// SV-17: called from the session loop (about every 200 ms)
void SessionServer::CheckIdle()
{
  if (!m_connected.load())
    return;
  uint64_t now = NowMs();
  uint64_t idle;
  bool warn = false;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    idle = now - m_lastActivity;
    if (idle >= kIdleWarnMs && idle < kIdleKickMs && now - m_lastIdleWarn >= kIdleWarnEveryMs)
    {
      m_lastIdleWarn = now;
      warn = true;
    }
  }
  if (idle >= kIdleKickMs)
  {
    SendNoticeAndBye("Disconnected: no input for 3 minutes.", "no input for 3 minutes");
    return;
  }
  if (warn)
  {
    char text[64];
    unsigned left = (unsigned) ((kIdleKickMs - idle + 999) / 1000);
    snprintf(text, sizeof(text), "No input: you will be disconnected in %u s.", left);
    SendNotice(text);
  }
}

// 11/16 (design 3.6 / 3.8): one challenge per client IP for the room password and the entry ticket.
bool SessionServer::CheckChallenge(const Hello &hello, const char *buf, int len, const sockaddr_in &from, bool local,
                                   uint8_t &rejectReason, uint32_t &nonceOut, uint8_t &needsOut,
                                   std::string &ticketId, std::string &ticketName)
{
  ticketId.clear();
  ticketName.clear();
  uint8_t needs = 0;
  if (!m_password.empty())
    needs |= NEED_PASSWORD;
  // From the internet a ticket is required. From this PC / the home network it is optional (LB-07): asked for only
  // when the client is logged in, for its Discord name.
  if (!local || hello.login == LOGIN_DISCORD)
    needs |= NEED_TICKET;
  if (needs == 0)
    return true;   // public room, no login, from this PC / the home network: nothing to check

  uint64_t now = NowMs();
  uint32_t ip = from.sin_addr.s_addr;

  // keep the maps small (a flood of HELLOs from many addresses)
  if (m_pwNonces.size() > 512)
    for (auto it = m_pwNonces.begin(); it != m_pwNonces.end();)
      it = (now - it->second.time > kPwNonceLifeMs) ? m_pwNonces.erase(it) : std::next(it);
  if (m_pwFails.size() > 512)
    for (auto it = m_pwFails.begin(); it != m_pwFails.end();)
      it = (now - it->second.first > kPwFailWindowMs && now >= it->second.lockUntil) ? m_pwFails.erase(it) : std::next(it);

  auto fail = m_pwFails.find(ip);
  if ((needs & NEED_PASSWORD) && fail != m_pwFails.end() && now < fail->second.lockUntil)
  {
    rejectReason = R_PASSWORD_LOCKED;
    return false;
  }

  auto it = m_pwNonces.find(ip);
  bool fresh = it != m_pwNonces.end() && now - it->second.time < kPwNonceLifeMs;
  if (hello.nonce == 0 || !fresh || it->second.nonce != hello.nonce)
  {
    // no (valid) answer yet: (re)send the nonce. A live one is kept, so a repeated HELLO does not replace it.
    if (!fresh)
    {
      m_pwNonces[ip] = { RoomPassword::NewNonce(), now };
      it = m_pwNonces.find(ip);
    }
    rejectReason = R_CHALLENGE;
    nonceOut = it->second.nonce;
    needsOut = needs;
    return false;
  }
  m_pwNonces.erase(it);   // one try per nonce

  // --- entry ticket (design 3.8) ---
  if (needs & NEED_TICKET)
  {
    bool valid = false;
    Ticket::Info info;
    int tl = hello.ticketLen;
    if (tl > 0 && tl <= Ticket::kMaxPayload && len >= (int) sizeof(Hello) + tl + Ticket::kSigLength)
    {
      std::string payload(buf + sizeof(Hello), tl);
      std::string sig(buf + sizeof(Hello) + tl, Ticket::kSigLength);
      std::string myIp;
      {
        std::lock_guard<std::mutex> lock(m_ipLock);
        myIp = m_publicIp;
      }
      long long nowSec = (long long) time(nullptr);
      if (!Ticket::Verify(payload, sig, info))
        printf("[Session] Slot %d: ticket from %s: bad signature\n", m_slot, AddrToString(from).c_str());
      else if (info.slot != m_slot || info.nonce != hello.nonce)
        printf("[Session] Slot %d: ticket from %s: for slot %d / another nonce\n", m_slot, AddrToString(from).c_str(), info.slot);
      else if (myIp.empty() || info.host != myIp)
        printf("[Session] Slot %d: ticket from %s: for host %s, this host is %s\n", m_slot, AddrToString(from).c_str(),
               info.host.c_str(), myIp.empty() ? "(public IP not known yet)" : myIp.c_str());
      else if (nowSec > info.exp + 300)   // the nonce (30 s, used once) is the real freshness; this only allows clock skew
        printf("[Session] Slot %d: ticket from %s: expired\n", m_slot, AddrToString(from).c_str());
      else
        valid = true;
    }
    if (valid)
    {
      ticketId = info.id;
      ticketName = info.name;
    }
    else if (!local)
    {
      rejectReason = tl > 0 ? R_TICKET_INVALID : R_LOGIN_REQUIRED;
      return false;
    }
  }

  // --- room password (SV-18) ---
  if (!(needs & NEED_PASSWORD))
    return true;
  uint8_t expect[32];
  RoomPassword::Proof(hello.nonce, m_password, expect);
  if (RoomPassword::ProofEquals(expect, hello.pwProof))
  {
    m_pwFails.erase(ip);
    return true;
  }

  PwFail &f = m_pwFails[ip];
  if (f.count == 0 || now - f.first > kPwFailWindowMs)
    f = { 0, now, 0 };
  if (++f.count >= kPwMaxFailures)
  {
    f.lockUntil = now + kPwLockMs;
    f.count = 0;
    printf("[Session] Slot %d: %d wrong passwords from %s -> refused for %u s\n", m_slot, kPwMaxFailures,
           AddrToString(from).c_str(), (unsigned) (kPwLockMs / 1000));
  }
  rejectReason = R_PASSWORD_WRONG;
  return false;
}

// Design 3.8: tickets name the host by the public IP the lobby shows (the same api.ipify.org answer the Firebase
// registration uses). Asked at start, again every minute until known, then every 10 minutes (the IP may change).
void SessionServer::PublicIpLoop()
{
  uint64_t next = 0;
  while (m_running.load())
  {
    uint64_t now = NowMs();
    if (now >= next)
    {
      // same service as FirebaseMatchingCpp::GetExternalIp (the lobby's "ip"), with a short timeout so Stop() is quick
      std::string ip;
      if (StreamHttp::Request(L"api.ipify.org", L"GET", "/", "", "", &ip, 4000) != 200)
        ip.clear();
      while (!ip.empty() && (ip.back() == '\n' || ip.back() == '\r' || ip.back() == ' '))
        ip.pop_back();
      if (ip.size() > 45)
        ip.clear();
      bool changed;
      {
        std::lock_guard<std::mutex> lock(m_ipLock);
        changed = !ip.empty() && ip != m_publicIp;
        if (!ip.empty())
          m_publicIp = ip;
      }
      if (changed)
        printf("[Session] Slot %d: public IP %s (entry tickets must name it)\n", m_slot, ip.c_str());
      next = now + (ip.empty() ? 60000 : 600000);
    }
    Sleep(200);
  }
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
    m_playerId.clear();
    if (m_userLock)
    {
      CloseHandle((HANDLE) m_userLock);   // the Discord account may connect again (here or to another slot)
      m_userLock = nullptr;
    }
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
  auto reject = [&](uint8_t reason, const char *text, uint32_t nonce = 0, uint8_t needs = 0) {
    Reject r = {};
    FillHeader(r.h, T_REJECT, 0);
    r.reason = reason;
    r.nonce = nonce;
    r.needs = needs;
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
    bool local = IsLocalOrPrivate(from);
    if (!local && hello.login != LOGIN_DISCORD)
    {
      reject(R_LOGIN_REQUIRED, "no login from the internet");
      return;
    }

    // 11/16 (design 3.6 / 3.8): nonce -> entry ticket (the host never sees a Discord token) -> room password
    uint8_t reason = 0, needs = 0;
    uint32_t nonce = 0;
    std::string ticketName;
    if (!CheckChallenge(hello, buf, len, from, local, reason, nonce, needs, m_pendingId, ticketName))
    {
      reject(reason, reason == R_CHALLENGE        ? "challenge sent"
                   : reason == R_PASSWORD_WRONG   ? "wrong password"
                   : reason == R_PASSWORD_LOCKED  ? "too many wrong passwords from this IP"
                   : reason == R_TICKET_INVALID   ? "entry ticket not valid here"
                                                  : "no entry ticket from the internet", nonce, needs);
      return;
    }
    if (!ticketName.empty())
      name = ticketName;

    // 11/16 (owner): one Discord account = one slot of this host. Every slot is its own process on this PC, so a
    // named mutex per Discord user ID is held while that user is connected; a second slot finds it and refuses.
    // No setting lifts it (distribution build). The ID comes from a signed ticket, so it cannot be made up.
    if (!m_pendingId.empty())
    {
      std::string lockName = "Local\\SupermodelPonMi_DiscordUser_" + m_pendingId;
      HANDLE h = CreateMutexA(nullptr, FALSE, lockName.c_str());
      if (h && GetLastError() == ERROR_ALREADY_EXISTS)
      {
        CloseHandle(h);
        reject(R_ALREADY_CONNECTED, "this Discord account is already connected to another slot of this host");
        return;
      }
      std::lock_guard<std::mutex> lock(m_lock);
      if (m_userLock)
        CloseHandle((HANDLE) m_userLock);
      m_userLock = h;
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
      m_playerId = m_pendingId;   // from a signed entry ticket, "" without one (LB-05: same person later)
      m_lastHb = NowMs();
      m_lastKeyReq = 0;
      m_haveSeq = false;
      m_lastMeta = 0;
      m_lastActivity = NowMs();
      m_lastIdleWarn = 0;
      m_haveActInput = false;
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
      CheckIdle();
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
    if (h.type == T_KICK)
    {
      // P7: the launcher on this PC removes the player of this slot
      if (h.version == kVersion && from.sin_addr.s_addr == htonl(INADDR_LOOPBACK))
        Kick();
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

      // SV-17: activity = the game input changed (meta keys like Alt+D do not count)
      bool moved = !m_haveActInput || in.keys != m_actKeys || in.buttons != m_actButtons || in.hat != m_actHat;
      for (int a = 0; a < 6 && !moved; a++)
        moved = std::abs((int) in.axes[a] - (int) m_actAxes[a]) >= kIdleAxisMove;
      if (moved)
      {
        bool first = !m_haveActInput;
        m_haveActInput = true;
        m_actKeys = in.keys;
        m_actButtons = in.buttons;
        m_actHat = in.hat;
        for (int a = 0; a < 6; a++)
          m_actAxes[a] = in.axes[a];
        if (!first)
          m_lastActivity = NowMs();   // the first packet only sets the reference (holding a key from the start is not activity)
      }
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
    // For checking: the first 20 changes of keys / buttons / hat (axes change all the time, so not logged)
    {
      static uint16_t prevKeys = 0;
      static uint32_t prevButtons = 0;
      static uint8_t prevHat = 0;
      static int logged = 0;
      if (logged < 20 && (in.keys != prevKeys || in.buttons != prevButtons || in.hat != prevHat))
      {
        printf("[Session] Slot %d: input keys=%04x buttons=%08x hat=%x\n", m_slot, in.keys, in.buttons, in.hat);
        logged++;
      }
      prevKeys = in.keys;
      prevButtons = in.buttons;
      prevHat = in.hat;
    }
    if (risen && m_cb.onMetaKeys)
      m_cb.onMetaKeys(risen);
  }
}
