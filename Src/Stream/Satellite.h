// 11/20 (owner): Spikeout FE satellite (spectator) cabinet "09".
//
// The 8 linked cabinets 01..08 run on the same PC (or anywhere on the ring). Cabinet 09 sits in the link
// ring as a plain relay (08 -> 09 -> 01, the bytes go through unchanged, so the game still counts 8
// machines) and plays nothing itself: it runs the game as a copy of a linked cabinet, from the link data
// alone, and shows that cabinet's own screen (camera, HUD, sound) - one player after another.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------
// Clone (09, ini SatelliteClone = 1, 11/20): 09 runs the game itself as a copy ("clone") of one linked
// cabinet. The relay still passes 08 -> 01 through unchanged; a copy of that stream is cut into the link's
// messages here, and the clone's net board (SimNetBoard) feeds the segments of one frame to the game as the
// cabinet it follows would see them. The clone never sends anything, so 01..08 cannot be disturbed.
// ---------------------------------------------------------------------------------------------------------
struct SatLinkEvent
{
  enum Type { Link, Frame, Broken };
  Type type = Frame;
  int numMachines = 0;                      // Link / Frame
  std::vector<std::vector<char>> segs;      // Frame: as 08 sent them; segs[i] = node (N - i)'s data
};

bool SatelliteTapStart(int portIn, const std::string &addrOut, int portOut);
void SatelliteTapStop();
int SatelliteTapRelayState();               // 0 off, 1 connecting to 01, 2 waiting for 08, 3 relaying
int SatelliteTapBacklog();                  // events not taken yet
bool SatelliteTapPop(SatLinkEvent &e, int timeoutMs);

// Shared between the clone's net board (writes) and Model3.cpp (reads, before every main board frame).
struct SatCloneState
{
  bool on = false;
  int mirror = 0;                           // ini SatelliteMirror: copy the followed node's link block into our RAM every frame
  int node = 0;                             // the cabinet we follow (1-based), 0 = not linked yet
  int numMachines = 0;
  bool switched = false;                    // node changed since Model3.cpp last looked
  bool havePacket = false;
  uint32_t packet[0x240];                   // the followed node's link block (0x900 bytes), as PPC words
  uint8_t linkState[9];                     // [1..8] each node's link state (block +4: 1 title, 2 select, 4 game)
  uint8_t group[9];                         // [1..8] block +0x3C byte (the game number = its host node), 0 = none
  int16_t hp[9] = {};                       // [1..8] each player's HP in our RAM (Rob +0x3F02), for the switches
  bool mirrorOnce = false;                  // copy the block once (after a switch)
  // The followed node's inputs for the frame we are about to run (SatInputRecord), replayed by ReadInputs
  bool haveInputs = false;
  uint8_t inpCnt[3] = {}, inp[3][4] = {}, inpIdx[3] = {}, inpLast[3] = { 0xFF, 0xFF, 0xFF };
};
extern SatCloneState g_satClone;

// Every linked cabinet (spikeofe) records what its game read from the inputs in a frame - port 0x04 bank 0
// (start / coin), 0x08 and 0x0C, up to 4 reads each - and its net board puts that in its own segment
// (PPC bytes 0x8E0.., the unused tail of the 4th history entry; 0x?FC is the bot mark, left alone), so the
// clone can play the frame exactly as that cabinet did.
struct SatInputRecord
{
  uint8_t cnt[3] = {}, val[3][4] = {};
};
extern SatInputRecord g_satInRec;
constexpr unsigned kSatInputOffset = 0x8E0;
constexpr unsigned kSatInputMagic = 0xA5;
int SatInputPort(unsigned reg);             // 0x04 -> 0, 0x08 -> 1, 0x0C -> 2, else -1

// The clone's own view (11/20 owner: "camera from behind each player, no 2D UI, no allies' HP, no minimap").
// Model3.cpp fills it each frame; Model3::RenderFrame / New3D read it. Display only - the game's RAM is
// never touched, so the clone stays in step.
// 11/20 (owner): what the overlay shows besides the title: STAGE - AREA (top left), the followed player's life bar
// under the name, every boss in the area with its name and life bar. Read from the game's RAM in SyncGPUs (display only).
struct SatHudBoss
{
  char name[16];
  int hp, hpMax;
};
struct SatHud
{
  bool valid = false;
  int stage = 0, area = 0;
  int hp = 0, hpMax = 0;                    // followed player: Rob +0x3F02 / +0x3F04
  int nBoss = 0;
  SatHudBoss boss[4] = {};                  // class (Rob +0x73) >= 4 and Rob +0x3F00 != 0x26, as the game's HUD (0x1B4E0)
};

struct SatViewState
{
  bool on = false;                          // this is the clone (spikeofe, SatelliteClone = 1)
  bool inGame = false;                      // our link state is 4 (in a game)
  int hideUI = 1;                           // ini SatelliteHideUI: no 2D top layer (HUD), no minimap viewport
  int cam = 0;                              // ini SatelliteCam: 0 the game's camera (owner's choice), 1 our chase camera, 2 test (= game's)
  int vpLog = 0;                            // ini SatelliteVpLog: satvp.txt, the viewports every 300 frames
  bool camValid = false;
  int overlay = 1;                          // ini SatelliteOverlay: our own top layer (Render2D::RenderSatelliteOverlay)
  std::string title = "SPIKEOUT SATELLITE";  // ini SatelliteTitle
  std::string ticker = "THIS IS PONMI SPIKEOUTFE STREAMING SYSTEM";   // ini SatelliteTicker
  float camFix[16] = {};                    // Real3D eye space -> eye space (column major), our camera / the game's
  SatHud hud;
};
extern SatViewState g_satView;
