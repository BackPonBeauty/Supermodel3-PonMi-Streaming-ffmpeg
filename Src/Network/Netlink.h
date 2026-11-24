// 11/23 main6 netlink, phase 1 (LAN prototype). Requirements: the "netlink 要件定義書" doc (HO-*, CL-*).
//
// (Phase 1 wire as below; phase 2 packs it - see NetlinkSetIdentity.)
// Host: the linked cabinet of slot k (ini NetlinkServe = 1) serves its link data on TCP (slot base port + 2)
// to one client. Every frame, after the ring loop, the N segments it holds are queued - laid out as 08 sends
// them on the ring (segs[i] = node N - i), with the same framing (native int length + bytes) - and a thread of
// its own sends them. The ring never waits for the client: a client that falls 600 frames behind is dropped.
// Before the first frame the client gets what 08 sends in the link test: a GUID (8 bytes), our machine index
// (1 byte) and the number of other machines (1 byte), so the satellite clone's tap parser reads it unchanged.
// The client's inputs come back on the same socket (NetlinkInputMsg) and go to CRemoteInputSystem
// (the host cabinet runs InputSystem = NONE). Client gone: inputs cleared (the character stands, D-4).
//
// Client: a satellite clone (SatelliteClone = 1) with ini NetlinkConnect = host:port takes its link data from
// that socket instead of the 09 relay; it follows the host's node k, with the game's own camera and HUD.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Client -> host, after the int length: magic 'N','L','I','1', then the pad in the host's STREAM layout
// (PonLuncher RemoteControls: Start JOY1_BUTTON8, Coin 7, Shift 1, Beat 3, Charge 4, Jump 6, lever = POV1).
#pragma pack(push, 1)
struct NetlinkInputMsg
{
  char magic[4] = { 'N', 'L', 'I', '1' };
  uint32_t buttons = 0;     // bit n = JOY1_BUTTON(n+1)
  uint8_t hat = 0;          // 1 up, 2 right, 4 down, 8 left
  uint8_t pad = 1;
  uint16_t reserved = 0;
};
#pragma pack(pop)

// The world (R-1, seen 11/23: a client started after the ring stopped at the game's NETWORK CHECKING): a new client
// gets node k's save state first. The frame after the client connects (and the link is up) the net board asks
// for it; Main.cpp saves at the end of that frame F and hands the file over; frames F+1.. follow it. On the wire,
// before the ring messages: 'N','L','S','1', u32 size, the state file.
bool NetlinkHostTakeSaveRequest();                // Main.cpp, after RunFrame: save now?
void NetlinkHostSetState(std::vector<char> &&st); // Main.cpp: the saved file (empty = failed: client dropped)
bool NetlinkClientTakeState(std::vector<char> &st); // Main.cpp, before RunFrame: a state to load?
void NetlinkClientStateLoaded();                    // Main.cpp: loaded, the link data may flow

// The settings (R-4 / LB-3, seen 11/23: the client had SpikeJr / EnemyKnockMul of its own and played another world):
// the ini keys that change what the game computes are sent the moment a client connects, before anything else -
// 'N','L','C','1', u32 size, "key=value\n" lines - and the client puts them over its own before the game is loaded.
// kNetlinkGameKeys lists them (Model3.cpp reads them in LoadGame / GetCPUClockFrequencyInHz). 11/23: + NetlinkBuffer
// (Main.cpp), so the host launcher's NET BUFFER is what the client uses.
extern const char *const kNetlinkGameKeys[];
extern const int kNetlinkGameKeyCount;
// Client, in Main.cpp before the game is loaded: connect and wait (up to timeoutMs) for the host's settings.
// false = refused (error says why: another exe / ROM / protocol) or no answer.
bool NetlinkClientWaitSettings(const std::string &host, int port, int timeoutMs, std::vector<std::pair<std::string, std::string>> &out, std::string &error);

// Phase 2 (11/23). Who we are, set by Main.cpp once the ROM set is loaded and before the net board starts: protocol
// version, game, SHA-256 of our exe file and of the ROM regions. A client sends it first ('N','L','H','2', u32,
// "key=value\n" lines); the host refuses ('N','L','E','1', u32, why) anything that is not its own, or answers
// with its settings. The state then goes zlib'ed ('N','L','S','2', u32 packed, u32 raw, data), and the ring data
// as chunks: u32 packed size + one zlib stream (Z_SYNC_FLUSH each) of u8 type (0 bytes, 1 XOR with the last
// frame) + payload - about 0.13 Mbps for an 8-link instead of 11.3.
void NetlinkSetIdentity(const std::string &game, const std::vector<std::pair<const uint8_t *, size_t>> &romParts);
// 11/24 (owner): the joining timeline - "[NLT hh:mm:ss.mmm +<ms since this process started>] text" to the log
// (and the console). Host and client logs line up by the wall clock.
void NetlinkTime(const char *fmt, ...);

bool NetlinkHostStart(int port, const std::string &settings);
// 11/23 (owner): the satellite outside the ring. A node with ini NetlinkWatchPort > 0 also serves its link data on
// that TCP port to one spectator clone (09: ini SatelliteConnect = host:port) - this PC / the home network only, no
// inputs, no room session. Same wire as a client: hello, settings, state, ring frames. The ring is 01..08 only, so
// 09 can start late and close any time; it switches between the nodes from the link data, as before (no new state).
bool NetlinkWatchStart(int port, const std::string &settings);
void NetlinkHostStop();                           // (both channels)
// Every ready frame, after the ring loop and before the bank swap: commRAM = the net board's bank (slot j at
// 0x100 + j * segSize, 0 = ours going out, 1..N received), node = our 1-based index.
void NetlinkHostFrame(const uint8_t *commRAM, int numMachines, int node, int segSize);
void NetlinkHostBroken();

bool NetlinkClientStart(const std::string &host, int port);
void NetlinkClientStop();

// Phase 3 (11/23): a client started from the lobby first joins the room on UDP like the video client - HELLO with
// codecs = 0 (netlink) -> entry ticket (Discord) / room password -> WELCOME (session key), heartbeat, INPUT to base + 0,
// BYE - and then asks for the link data with "key=<session key>" in its hello; the host gives it only to its session's
// client (same IP, same key). Without a room session the host serves only this PC / the home network (a test).
// Call before NetlinkClientWaitSettings. slot 0 = no session (home test).
void NetlinkClientSetSession(int slot, const std::string &name, const std::string &password);
// The client was playing and is not any more (the host's BYE / kick / idle kick / no answer / link data stopped):
// why, for the lobby. Main.cpp checks it every frame and goes back to the lobby.
bool NetlinkClientEnded(std::string &why);
// Host, Main.cpp: the room session of our client ended (BYE, kick, idle kick, timeout): stop its link data too.
void NetlinkHostDropClient();

// 11/23: the client's latency display (F3 in Main.cpp).
struct NetlinkStats
{
  bool on = false;          // playing (link data flowing)
  bool session = false;     // joined through the room (else a home test without one)
  int slot = 0, node = 0;
  int rttMs = -1;           // room session PING -> PONG (UDP), -1 = none yet / no session
  int backlog = 0;          // frames received and not played yet
  int inputMs = -1;         // the last "our pad changed -> the host's node played it -> we have that frame"
  int inputAvgMs = -1;      // the average of the last 10 of those
  double kbps = 0;          // link data received, kbit/s (the last second)
};
NetlinkStats NetlinkClientStats();
// Main.cpp, every frame: the pad we send (a change starts a measurement)
void NetlinkLatencyMarkInput(uint32_t buttons, uint8_t hat);
// net board, every frame: the node's input record for the frame we are about to play (a change ends it)
void NetlinkLatencyInputPlayed(uint32_t sig);
bool NetlinkClientOn();
void NetlinkClientSendInput(const NetlinkInputMsg &m);
// 11/23: the client is the satellite (SatelliteConnect): it sends no inputs. Set before NetlinkClientStart.
void NetlinkClientSetWatch(bool watch);
bool NetlinkClientIsWatch();
