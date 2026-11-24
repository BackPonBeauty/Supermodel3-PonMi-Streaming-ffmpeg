/*
 * SessionServer.h
 *
 * Streaming (P4, 2026-11-16): one slot's session on the server (design 4.2, requirements SV-01..16).
 * Replaces the old HandshakeServer (text HELLO/OK, spectators, LinkPlay=0 two-slot mode, 1-min idle kick).
 *
 *   empty --HELLO ok--> connected --BYE / no HB for 6 s / no input for 3 min--> standing (nobody) --HELLO--> ...
 *
 * - Idle kick (SV-17): if the game input (keys, buttons, hat, or an axis by kIdleAxisMove) does not change for
 *   3 minutes, the client is warned during the last 30 s and then removed. Not configurable.
 * - Room password (SV-18): with SetPassword(non-empty), a HELLO first gets REJECT(R_PASSWORD_REQUIRED, nonce) and
 *   must come again with the proof (RoomPassword.h). 5 wrong answers from one IP in 10 min -> 5 min refused.
 *
 * - One client per slot (no spectators). A second client gets REJECT(slot busy).
 * - Version check; login (11/16, design 3.8): from the internet a signed entry ticket from the ticket Worker is
 *   required (the host never sees a Discord token); no login is accepted only from 127.0.0.1 / private addresses (LB-07).
 * - INPUT, HB, PING, KEYREQ, STATUS and the media HELLOs are accepted only from the connected client's IP
 *   with the right session key (SV-13).
 * - Client input goes to CRemoteInputSystem (InputSystem = NONE). When the client leaves, the input is
 *   cleared, so the character just stands (BT-01).
 */

#ifndef INCLUDED_STREAM_SESSIONSERVER_H
#define INCLUDED_STREAM_SESSIONSERVER_H

#include "Protocol.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

// Set once a client has ever connected to this slot; the AIPlayer bot then stops for good (SV-11).
extern std::atomic<bool> g_streamHumanJoined;

class SessionServer
{
public:
  struct Callbacks
  {
    std::function<void(const std::string &name, const std::string &ip)> onConnected;
    std::function<void(const char *why)> onDisconnected;
    std::function<void()> onKeyframeRequest;
    std::function<void(uint8_t risenMetaBits)> onMetaKeys;
  };

  SessionServer() = default;
  ~SessionServer() { Stop(); }

  // slot: number reported in WELCOME. codec: "H264" / "H265" (what the encoder produces).
  bool Start(int slot, int sessionPort, int inputPort, int width, int height, const std::string &codec, Callbacks cb);
  void Stop();

  // SV-18 (11/16): room password, "" = public. Set before Start().
  void SetPassword(const std::string &password) { m_password = password; }
  // 11/23 netlink: this slot sends link data (Network/Netlink.h), not video: only netlink clients (Hello.codecs = 0).
  // Set before Start().
  void SetNetlink(bool on) { m_netlink = on; }
  // 11/24 (owner) REMOTE slot: takes either kind of client; the HELLO says which (codecs = 0: netlink, else video).
  // Set before Start(). ClientIsNetlink() = the connected client is a netlink one.
  void SetAuto(bool on) { m_auto = on; }
  bool ClientIsNetlink() const { return m_clientNetlink.load(); }

  bool IsConnected() const { return m_connected.load(); }
  std::string GetPlayerName();

  // Adaptive bitrate (fed by STATUS packets)
  float GetLatestLossRate() const { return m_lossRate.load(); }
  uint32_t GetLastStatusTime() const { return m_lastStatusTime.load(); }   // GetTickCount() of the last STATUS

  // 11/24: a one-line message the client shows near the top for 6 s (the bitrate notice)
  void Notify(const char *text) { SendNotice(text); }

private:
  void SessionLoop();
  void InputLoop();
  void HandleHello(const char *buf, int len, const sockaddr_in &from);
  void Disconnect(const char *why);
  void Kick();   // P7: T_KICK from the launcher on this PC
  void SendNotice(const char *text);
  void SendNoticeAndBye(const char *text, const char *why);
  void CheckIdle();   // SV-17: no input change for kIdleKickMs -> removed
  void SendTo(const void *data, int len, const sockaddr_in &to);
  bool FromClient(const sockaddr_in &from, uint32_t key);
  // 11/16 (design 3.6 / 3.8): nonce, room password and entry ticket. True = let in; ticketId / ticketName are
  // filled when a valid ticket came. Otherwise rejectReason (+ nonceOut / needsOut for R_CHALLENGE) say what to answer.
  bool CheckChallenge(const StreamProto::Hello &hello, const char *buf, int len, const sockaddr_in &from, bool local,
                      uint8_t &rejectReason, uint32_t &nonceOut, uint8_t &needsOut,
                      std::string &ticketId, std::string &ticketName);
  void PublicIpLoop();   // this host's public IP (what the tickets name), from api.ipify.org
  bool ValidateMediaHello(const char *buf, int len, const sockaddr_in &from);
  static bool IsLocalOrPrivate(const sockaddr_in &addr);

  int m_slot = 0;
  bool m_netlink = false;
  bool m_auto = false;                       // 11/24
  std::atomic<bool> m_clientNetlink{false};  // 11/24
  int m_width = 0, m_height = 0;
  uint8_t m_codecBit = StreamProto::CODEC_H264;
  Callbacks m_cb;

  SOCKET m_sessionSock = INVALID_SOCKET;
  SOCKET m_inputSock = INVALID_SOCKET;
  std::thread m_sessionThread;
  std::thread m_inputThread;
  std::thread m_ipThread;
  std::atomic<bool> m_running{false};
  std::mutex m_ipLock;
  std::string m_publicIp;                 // "" until known (guarded by m_ipLock)

  std::mutex m_lock;                      // guards everything below
  std::atomic<bool> m_connected{false};
  uint32_t m_key = 0;
  sockaddr_in m_clientAddr = {};          // session address of the client
  std::string m_playerName;
  std::string m_playerId;                 // Discord user ID (empty without login)
  std::string m_pendingId;                // Discord ID from the entry ticket (session thread only)
  void *m_userLock = nullptr;             // HANDLE of the named mutex "this Discord user is connected here"
  uint64_t m_lastHb = 0;
  uint64_t m_lastKeyReq = 0;
  bool m_haveSeq = false;
  uint16_t m_lastSeq = 0;
  uint8_t m_lastMeta = 0;

  // Idle kick (SV-17): the input at the last activity, and when it was
  uint64_t m_lastActivity = 0;
  uint64_t m_lastIdleWarn = 0;
  bool m_haveActInput = false;
  uint16_t m_actKeys = 0;
  uint32_t m_actButtons = 0;
  uint8_t m_actHat = 0;
  int16_t m_actAxes[6] = {};

  // Room password (SV-18). Used by the session thread only.
  std::string m_password;
  struct PwNonce { uint32_t nonce; uint64_t time; };
  struct PwFail { int count; uint64_t first; uint64_t lockUntil; };
  std::map<uint32_t, PwNonce> m_pwNonces;   // by client IPv4 address
  std::map<uint32_t, PwFail> m_pwFails;

  std::atomic<float> m_lossRate{0.0f};
  std::atomic<uint32_t> m_lastStatusTime{0};
};

#endif // INCLUDED_STREAM_SESSIONSERVER_H
