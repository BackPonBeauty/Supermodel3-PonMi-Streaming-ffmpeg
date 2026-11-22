// 11/23 main6 netlink, phase 1 (LAN prototype). Requirements: the "netlink 要件定義書" doc (HO-*, CL-*).
//
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
// kNetlinkGameKeys lists them (Model3.cpp reads them in LoadGame / GetCPUClockFrequencyInHz).
extern const char *const kNetlinkGameKeys[];
extern const int kNetlinkGameKeyCount;
// Client, in Main.cpp before the game is loaded: connect and wait (up to timeoutMs) for the host's settings.
bool NetlinkClientWaitSettings(const std::string &host, int port, int timeoutMs, std::vector<std::pair<std::string, std::string>> &out);

bool NetlinkHostStart(int port, const std::string &settings);
void NetlinkHostStop();
// Every ready frame, after the ring loop and before the bank swap: commRAM = the net board's bank (slot j at
// 0x100 + j * segSize, 0 = ours going out, 1..N received), node = our 1-based index.
void NetlinkHostFrame(const uint8_t *commRAM, int numMachines, int node, int segSize);
void NetlinkHostBroken();

bool NetlinkClientStart(const std::string &host, int port);
void NetlinkClientStop();
bool NetlinkClientOn();
void NetlinkClientSendInput(const NetlinkInputMsg &m);
