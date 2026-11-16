/*
 * Protocol.h
 *
 * Streaming protocol, version 1 (P4, 2026-11-16; design Docs/streaming_design.md chapter 3).
 * Shared by the server (SessionServer, RtpSender, UdpAudioSender) and the client (P5).
 *
 * All packets are UDP. Every packet except the RTP video and the Opus audio payloads starts with
 * Header. Multi-byte fields are little-endian (both ends are x86/x64 Windows).
 *
 * Ports of slot n (1..8): base = 55000 + (n - 1) * 4
 *   base + 0  client -> server  INPUT
 *   base + 1  both              session: HELLO / WELCOME / REJECT / HB / PING / PONG / KEYREQ / BYE / STATUS
 *   base + 2  server -> client  video (RTP). The client first sends MEDIA_HELLO here (opens NAT, tells the port)
 *   base + 3  server -> client  audio (Opus). Same MEDIA_HELLO first.
 */

#ifndef INCLUDED_STREAM_PROTOCOL_H
#define INCLUDED_STREAM_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <winsock2.h>

namespace StreamProto
{
  constexpr char    kMagic[4] = { 'S', 'P', 'K', 'S' };
  constexpr uint8_t kVersion  = 1;

  constexpr int kBasePort = 55000;
  enum PortOffset { PORT_INPUT = 0, PORT_SESSION = 1, PORT_VIDEO = 2, PORT_AUDIO = 3 };
  inline int SlotBasePort(int slot) { return kBasePort + (slot - 1) * 4; }

  enum Type : uint8_t
  {
    T_HELLO = 1,        // C->S
    T_WELCOME = 2,      // S->C
    T_REJECT = 3,       // S->C
    T_HB = 4,           // C->S every 2 s, S->C echo
    T_PING = 5,         // C->S
    T_PONG = 6,         // S->C (echo of the client time)
    T_KEYREQ = 7,       // C->S
    T_BYE = 8,          // both
    T_NOTICE = 9,       // S->C (text to show)
    T_INPUT = 10,       // C->S (input port)
    T_MEDIA_HELLO = 11, // C->S (video / audio port)
    T_STATUS = 12,      // C->S (video loss rate, for the adaptive bitrate)
    T_KICK = 13,        // launcher -> S, only from 127.0.0.1 (P7): disconnect the client of this slot
  };

  enum RejectReason : uint8_t
  {
    R_VERSION = 1,          // protocol version differs
    R_NOT_MEMBER = 2,       // not a member of the Discord server
    R_SLOT_BUSY = 3,        // somebody is connected to this slot
    R_LOGIN_REQUIRED = 4,   // no login, and not from this PC / the home network
    R_CODEC = 5,            // the client cannot decode the codec this server encodes
    R_BAD_REQUEST = 6,
    R_LOGIN_UNAVAILABLE = 7, // the host could not ask Discord now (network, rate limit) / not set up on the host
    R_LOGIN_INVALID = 8,     // Discord said the token is invalid or expired
    R_ALREADY_CONNECTED = 9  // this Discord account is already in another slot of this host
  };

  enum CodecBits : uint8_t { CODEC_H264 = 1, CODEC_H265 = 2 };
  enum Login : uint8_t { LOGIN_NONE = 0, LOGIN_DISCORD = 1 };
  enum MetaBits : uint8_t { META_ALT_D = 1, META_BITRATE_UP = 2, META_BITRATE_DOWN = 4 };

  constexpr uint32_t kHeartbeatIntervalMs  = 2000;
  constexpr uint32_t kDropTimeoutMs        = 6000;   // requirement SV-08 (Q34)
  // Idle kick (SV-17, 2026-11-16): no change of the game input for 3 minutes -> NOTICE + BYE, the slot is freed.
  // Fixed in the code on purpose (no ini switch): a player must not be able to sit in a slot forever.
  constexpr uint32_t kIdleKickMs           = 180000;
  constexpr uint32_t kIdleWarnMs           = 150000; // from here a warning NOTICE every kIdleWarnEveryMs
  constexpr uint32_t kIdleWarnEveryMs      = 5000;
  constexpr int      kIdleAxisMove         = 8000;   // an axis counts as moved only by this much (stick noise is far less)
  constexpr uint32_t kKeyReqMinIntervalMs  = 200;
  constexpr uint32_t kInputIntervalMs      = 8;
  constexpr int      kMaxNameLength        = 32;

#pragma pack(push, 1)
  struct Header
  {
    char     magic[4];
    uint8_t  version;
    uint8_t  type;
    uint32_t key;       // session key from WELCOME (0 in HELLO)
  };

  struct Hello          // + tokenLen bytes of Discord access token
  {
    Header   h;
    uint8_t  codecs;    // CodecBits the client can decode
    uint8_t  login;     // Login
    char     name[kMaxNameLength];   // display name (UTF-8, zero padded)
    uint16_t tokenLen;
  };

  struct Welcome
  {
    Header   h;
    uint16_t width;
    uint16_t height;
    uint8_t  codec;     // one CodecBits value
    uint8_t  slot;
  };

  struct Reject
  {
    Header  h;
    uint8_t reason;     // RejectReason
  };

  struct Ping           // PING and PONG
  {
    Header   h;
    uint64_t clientTime;
  };

  struct Status
  {
    Header h;
    float  lossRate;    // 0..1
  };

  struct Notice
  {
    Header h;
    char   text[64];
  };

  struct Input
  {
    Header   h;
    uint16_t seq;
    uint16_t keys;          // bit0 W, bit1 A, bit2 S, bit3 D
    uint8_t  meta;          // MetaBits
    uint8_t  padConnected;
    uint32_t buttons;       // bit n = SDL joystick button n
    int16_t  axes[6];       // SDL joystick axes 0..5
    uint8_t  hat;           // SDL hat bits (1 up, 2 right, 4 down, 8 left)
  };
#pragma pack(pop)

  inline void FillHeader(Header &h, uint8_t type, uint32_t key)
  {
    memcpy(h.magic, kMagic, 4);
    h.version = kVersion;
    h.type = type;
    h.key = key;
  }

  // True if buf starts with our magic. Version and type are returned for the caller to check.
  inline bool ParseHeader(const char *buf, int len, Header *out)
  {
    if (len < (int) sizeof(Header) || memcmp(buf, kMagic, 4) != 0)
      return false;
    memcpy(out, buf, sizeof(Header));
    return true;
  }

  // The RTP / audio senders accept a MEDIA_HELLO only if this says so (requirement SV-13).
  // SessionServer installs it; with none installed every MEDIA_HELLO is refused.
  using MediaHelloValidator = std::function<bool(const char *buf, int len, const sockaddr_in &from)>;
  void SetMediaHelloValidator(MediaHelloValidator validator);
  bool IsValidMediaHello(const char *buf, int len, const sockaddr_in &from);
}

#endif // INCLUDED_STREAM_PROTOCOL_H
