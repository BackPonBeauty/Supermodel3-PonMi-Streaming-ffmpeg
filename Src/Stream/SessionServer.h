/*
 * SessionServer.h
 *
 * Streaming (P4, 2026-11-16): one slot's session on the server (design 4.2, requirements SV-01..16).
 * Replaces the old HandshakeServer (text HELLO/OK, spectators, LinkPlay=0 two-slot mode, 1-min idle kick).
 *
 *   empty --HELLO ok--> connected --BYE / no HB for 6 s--> standing (nobody) --HELLO--> connected ...
 *
 * - One client per slot (no spectators). A second client gets REJECT(slot busy).
 * - Version check; login: Discord is verified in P6 (until then refused unless from this PC / home network);
 *   no login is accepted only from 127.0.0.1 / private addresses (requirement LB-07).
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

  bool IsConnected() const { return m_connected.load(); }
  std::string GetPlayerName();

  // Adaptive bitrate (fed by STATUS packets)
  float GetLatestLossRate() const { return m_lossRate.load(); }
  uint32_t GetLastStatusTime() const { return m_lastStatusTime.load(); }   // GetTickCount() of the last STATUS

private:
  void SessionLoop();
  void InputLoop();
  void HandleHello(const char *buf, int len, const sockaddr_in &from);
  void Disconnect(const char *why);
  void Kick();   // P7: T_KICK from the launcher on this PC
  void SendTo(const void *data, int len, const sockaddr_in &to);
  bool FromClient(const sockaddr_in &from, uint32_t key);
  bool ValidateMediaHello(const char *buf, int len, const sockaddr_in &from);
  static bool IsLocalOrPrivate(const sockaddr_in &addr);

  int m_slot = 0;
  int m_width = 0, m_height = 0;
  uint8_t m_codecBit = StreamProto::CODEC_H264;
  Callbacks m_cb;

  SOCKET m_sessionSock = INVALID_SOCKET;
  SOCKET m_inputSock = INVALID_SOCKET;
  std::thread m_sessionThread;
  std::thread m_inputThread;
  std::atomic<bool> m_running{false};

  std::mutex m_lock;                      // guards everything below
  std::atomic<bool> m_connected{false};
  uint32_t m_key = 0;
  sockaddr_in m_clientAddr = {};          // session address of the client
  std::string m_playerName;
  std::string m_playerId;                 // Discord user ID (empty without login)
  std::string m_pendingName, m_pendingId; // result of the Discord check (session thread only)
  void *m_userLock = nullptr;             // HANDLE of the named mutex "this Discord user is connected here"
  uint64_t m_lastHb = 0;
  uint64_t m_lastKeyReq = 0;
  bool m_haveSeq = false;
  uint16_t m_lastSeq = 0;
  uint8_t m_lastMeta = 0;

  std::atomic<float> m_lossRate{0.0f};
  std::atomic<uint32_t> m_lastStatusTime{0};
};

#endif // INCLUDED_STREAM_SESSIONSERVER_H
