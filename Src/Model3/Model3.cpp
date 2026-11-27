/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * Model3.cpp
 *
 * Implementation of the CModel3 class: a complete Model 3 machine.
 *
 * To-Do List
 * ----------
 *  - Save state format has changed slightly. No longer need dmaUnknownRegister
 *    in Real3D.cpp and security board-related variable was added to Model 3
 *    state. PowerPC timing variables have changed. Before 0.3a release,
 *    important to change format version #.
 *  - Remove FLIPENDIAN32() macros and have little endian devices flip data
 *    around themselves. Bus standard should be big endian.
 *  - ROM sets should probably be handled with a class that manages ROM
 *    loading, the game list, as well as ROM patching
 *  - Wrap up CPU emulation inside a class.
 *  - Update the to-do list! I forgot lots of other stuff here :)
 *
 * PowerPC Address Map (may be slightly out of date/incomplete)
 * -------------------
 * 00000000-007FFFFF  RAM
 * 84000000-8400003F  Real3D Status Registers
 * 88000000-88000007  Real3D Command Port
 * 8C000000-8C3FFFFF  Real3D Culling RAM (Low)
 * 8E000000-8E0FFFFF  Real3D Culling RAM (High)
 * 90000000-9000000B  Real3D VROM Texture Port
 * 94000000-940FFFFF  Real3D Texture FIFO
 * 98000000-980FFFFF  Real3D Polygon RAM
 * C0000000-C000FFFF  Netboard Shared RAM (Step 1.5+)
 * C0010000-C00101FF  Netboard Registers (Step 1.5+)
 * C0020000-C002FFFF  Netboard Program RAM (Step 1.5+)
 * C0000000-C00000FF  SCSI (Step 1.x)
 * C1000000-C10000FF  SCSI (Step 1.x) (Lost World expects it here)
 * C2000000-C20000FF  Real3D DMA (Step 2.x)
 * F0040000-F004003F  Input (Controls) Registers
 * F0080000-F0080007  Sound Board Registers
 * F00C0000-F00DFFFF  Backup RAM
 * F0100000-F010003F  System Registers
 * F0140000-F014003F  Real-Time Clock
 * F0180000-F019FFFF  Security Board RAM
 * F01A0000-F01A003F  Security Board Registers
 * F0800CF8-F0800CFF  MPC105 CONFIG_ADDR (Step 1.x)
 * F0C00CF8-F0800CFF  MPC105 CONFIG_DATA (Step 1.x)
 * F1000000-F10F7FFF  Tile Generator Pattern Table
 * F10F8000-F10FFFFF  Tile Generator Name Table
 * F1100000-F111FFFF  Tile Generator Palette
 * F1180000-F11800FF  Tile Generator Registers
 * F8FFF000-F8FFF0FF  MPC105 (Step 1.x) or MPC106 (Step 2.x) Registers
 * F9000000-F90000FF  NCR 53C810 Registers (Step 1.x?)
 * FEC00000-FEDFFFFF  MPC106 CONFIG_ADDR (Step 2.x)
 * FEE00000-FEFFFFFF  MPC106 CONFIG_DATA (Step 2.x)
 * FF000000-FF7FFFFF  Banked CROM (CROMxx)
 * FF800000-FFFFFFFF  Fixed CROM
 *
 * Endianness
 * ----------
 * We assume a little endian machine and so for speed, PowerPC RAM and ROM
 * regions are byte reversed, which means that aligned words can be read and
 * written without any conversion. Problems arise when the PowerPC accesses
 * little endian devices, like the tile generator, MPC10x, or Real3D. Then, the
 * access must be carried out carefully one byte at a time or by manually byte
 * reversing first (because the PowerPC will already have byte reversed it).
 *
 * System Registers
 * ----------------
 *
 * F0100014: IRQ Enable
 *   7   6   5   4   3   2   1   0
 * +---+---+---+---+---+---+---+---+
 * | ? |SND| ? |NET|VGP|VDP|VBL|VD0|
 * +---+---+---+---+---+---+---+---+
 *    SND   SCSP (sound)
 *    NET   Network
 *    VGP   GP done (geometry processing)
 *    VDP   DP done (display processing)
 *    VBL   VBlank start
 *    VD0   Unknown video-related
 *    0 = Disable, 1 = Enable
 *
 * Game Buttons
 * ------------
 *
 * For further information, see ReadInputs().
 *
 * Offset 0x04, bank 0:
 *
 *    7   6   5   4   3   2   1   0
 *  +---+---+---+---+---+---+---+---+
 *  | ? | ? |ST2|ST1|SVA|TSA|CN2|CN1|
 *  +---+---+---+---+---+---+---+---+
 *    CNx   Coin 1, Coin 2
 *    TSA   Test Button A
 *    SVA   Service Button A
 *    STx   Start 1, Start 2
 *
 * Offset 0x04, bank 1:
 *
 *    7   6   5   4   3   2   1   0
 *  +---+---+---+---+---+---+---+---+
 *  |TSB|SVB|EEP| ? | ? | ? | ? | ? |
 *  +---+---+---+---+---+---+---+---+
 *    EEP   Mapped to EEPROM (values written here are ignored)
 *    SVB   Service Button B
 *    TSB   Test Button B
 *
 * Offset 0x08:
 *
 *    7   6   5   4   3   2   1   0
 *  +---+---+---+---+---+---+---+---+
 *  |G27|G26|G25|G24|G23|G22|G21|G20|
 *  +---+---+---+---+---+---+---+---+
 *    G2x   Game-specific
 *
 * Offset 0x0C:
 *
 *    7   6   5   4   3   2   1   0
 *  +---+---+---+---+---+---+---+---+
 *  |G37|G36|G35|G34|G33|G32|G31|G30|
 *  +---+---+---+---+---+---+---+---+
 *    G3x   Game-specific
 *
 * Game-specific buttons:
 *
 *  Scud Race:
 *    G27   ---
 *    G26   Shift 2 when combined w/ G25, Shift 1 when combined w/ G24
 *    G25   Shift 4
 *    G24   Shift 3
 *    G23   VR4 Green
 *    G22   VR3 Yellow
 *    G21   VR2 Blue
 *    G20   VR1 Red
 *
 *  Virtua Fighter 3, Fighting Vipers 2:
 *    G27   Right
 *    G26   Left
 *    G25   Down
 *    G24   Up
 *    G23   Punch
 *    G22   Kick
 *    G21   Guard
 *    G20   Escape (VF3 only)
 *
 *  Sega Rally 2:
 *    G21   Hand Brake
 *    G20   View Change
 *
 *  Lost World, LA Machineguns:
 *    G20   Gun trigger
 *
 *  Star Wars Trilogy:
 *    G20   Event button
 *    G25   Trigger
 *
 *  Virtual On 2:
 *    G27   Left Lever Left
 *    G26   Left Lever Right
 *    G25   Left Lever Up
 *    G24   Left Lever Down
 *    G23   ---
 *    G22   ---
 *    G21   Left Turbo
 *    G20   Left Shot Trigger
 *    G37   Right Lever Left
 *    G36   Right Lever Right
 *    G35   Right Lever Up
 *    G34   Right Lever Down
 *    G33   ---
 *    G32   ---
 *    G31   Right Turbo
 *    G30   Right Shot Trigger
 *
 * Misc. Notes
 * -----------
 *
 * daytona2:
 *  - Base address of program in CROM: 0x600000
 *  - 0x10019E is the location in RAM which contains link type.
 *  - Region menu can be accessed by entering test mode, holding start, and
 *    pressing: green, green, blue, yellow, red, yellow, blue
 *   (VR4,4,2,3,1,3,2).
 *
 * magtruck:
 *    Found a way to unlock region in Magical truck
 *    If Midi data port returns 0, magtruck is locked to japan region
 *    if Midi data port returns 1, magtruck region can be changed to export, usa, australian
 *    We decided to make a rom patch instead of introducing a specific config to allow the region to be set
 *    Reminder : to change region in magtruck, use the region menu code
 *               enter Service menu with Test, then Start P1, Start P1, Service, Start P1, Service, Test (default keys : 6 then 1 1 5 1 5 6)
 *    note : rom patch is active by default, comment the patch in games.xml if you want Japan region
 *
 */

#include "Model3.h"

#include <new>
#include <cstdio>
#include <cstdarg>   // va_list for SpikeoutGateNote (11/18)
#include <cmath>   // sqrtf for the AI-player camera-relative steering
#include <cstdlib>
#include <cstring>
#include <chrono>   // 11/23 AIProfile
#include <ctime>   // wall-clock stamp in p1_state.txt (shared between the 8 emulator processes)
#include "Supermodel.h"
#include "DriveBoard/BillBoard.h"
#include "DriveBoard/JoystickBoard.h"
#include "DriveBoard/SkiBoard.h"
#include "DriveBoard/WheelBoard.h"
#include "Game.h"
#include "ROMSet.h"
#include "Network/NetBoard.h"
#include "Network/SimNetBoard.h"
#include "Model3/Spikeout8Link.h"
#include "Stream/Satellite.h"
#include "OSD/Audio.h"
#include "OSD/Video.h"
#include "Util/Format.h"
#include "Util/ByteSwap.h"
#include <functional>
#include <set>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <string>

// WatchWrite: also append log lines to watch.txt so capture does not depend on
// the console. Opened lazily on first watched write.
static FILE *g_watchFile = nullptr;
#define WLOG(...) do { printf(__VA_ARGS__); if (!g_watchFile) g_watchFile = fopen("watch.txt", "w"); if (g_watchFile) { fprintf(g_watchFile, __VA_ARGS__); fflush(g_watchFile); } } while (0)

// TaskArrayLog: log every RAM access to the player task arrays [fa_player+0x10, fa_player+0x50)
// with the accessing PC, to find every accessor empirically. fa_player = *(0x104114) & 0x7FFFFF.
bool g_taskArrayLog = false;
static FILE *g_taskLogFile = nullptr;
static unsigned char g_taskSeen[0x20000];  // dedup by PC>>2 (PC < 0x80000); log each accessor once
#define TASKLOG(A, ISW, SZ) do { if (g_taskArrayLog && (A) < 0x00800000) { \
    UINT32 _fp = (*(UINT32 *) &ram[0x104114]) & 0x7FFFFF; \
    if ((A) >= _fp+0x10 && (A) < _fp+0x50) { \
      UINT32 _pc = ppc_get_pc(); unsigned _k = (_pc>>2) & 0x1FFFF; \
      if (!g_taskSeen[_k]) { g_taskSeen[_k] = 1; \
        if (!g_taskLogFile) g_taskLogFile = fopen("tasklog.txt", "w"); \
        if (g_taskLogFile) { fprintf(g_taskLogFile, "%c%d PC=%08X off=0x%02X\n", (ISW)?'W':'R', (SZ), _pc, (unsigned)((A)-_fp)); fflush(g_taskLogFile); } \
      } } } } while (0)

// Spikeout 8-player task-array shadow (emulator-side fix for the 4-entry task arrays).
// The game's per-player task arrays live at fa_player+0x10/+0x20/+0x30/+0x40 with only 4 entries
// each (0x10 apart), so players 5-8 (indices 4-7) overrun into the next array. We leave indices 0-3
// in native RAM untouched (4-player behaviour byte-identical) and redirect indices 4-7 to this shadow,
// keyed on the accessing instruction (PC -> which array). fa_player = *(0x104114) & 0x7FFFFF.
bool g_spikeofe8task = false;
static bool g_spikeoutIdleLog = false;   // 11/13 IdleSkipLog=1 -> idlelog.txt
// 11/27 Headless (Main.cpp sets it): the tile generator's 2D layers are not drawn line by line (about 19% of the
// PPC thread). Nothing in the game reads them; only the window (and the satellite HUD, never headless) uses them.
bool g_skipTileDraw = false;

// Spikeout AI player (prototype): when AIPlayer=1, this cabinet's local player (node 0x10065B)
// is driven by a simple heuristic instead of the human controls - it steers toward the nearest
// active enemy and presses Beat in range, so cabinets 02-04 can be bots in a link game. The
// generated input replaces the human bits in ReadInputs(0x08); the net board shares it like any
// player input, so no ROM change and no desync. Robs: 0x1A0000+slot*0x4000, pos floats at +0x380
// (x) / +0x388 (z), active = flags(+0x00) bit0; players are slots 0-7, enemies 8+.
static bool g_spikeoutP1Invincible = false; // P1Invincible=1: keep player 1 invincible, to record waypoints (must be set on every cabinet)
static float g_spikeoutJumpMul = 1.0f;       // 11/26 PlayerJumpMul=X (1.0-6.0): players jump X times as high (must be the same on every cabinet)
static bool g_spikeoutSpikeJr = false;      // 11/23 SpikeJr=1: players who picked Spike play Spike Jr (character 4; must be set on every cabinet)
static bool g_spikeoutTimerStop = false;    // 11/17 TimerStop=1: hold the countdown timers at 99 with 4 or fewer players too (5+ always hold; must be set on every cabinet)
static bool g_spikeoutAI = false;
#include <atomic>
extern std::atomic<bool> g_streamHumanJoined;   // Stream/SessionServer.cpp (P4): a streaming client has taken this slot (SV-11)
static int g_spikeoutAIChar = -1;   // AIChar (11/14): the character a bot picks on a new session's select screen - 0 Spike,
                                    // 1 White, 2 Linda, 3 Tenshin (or the names); -1 / empty = whatever the cursor is on
static bool g_spikeoutAIRandSel = true;   // AIrandSel (11/17): 1 = a continue's select picks a character at random (LEFT 0-3
                                          // times), 0 = keep the same one. Only without AIChar (a set AIChar never picks at random)
static bool g_spikeoutAIShift = false;  // AIShift=1: hold Shift next to an enemy (off for now)
static int g_spikeoutAIRoute = -1;       // AIRoute=0..3 (11/10): force the Diesel Town branches - 0 front Mall, 1 back Mall,
                                         // 2 front Shipyard, 3 back Shipyard; -1 (default) = cabinet 02 picks at random
static int g_spikeoutAIRoute3 = -1;      // AIRoute3=0/1 (11/11): area 30's branch - 0 Door#1, 1 Door#42; -1 (default) = 02 picks at random
static int g_spikeoutAIRoute2 = -1;      // AIRoute2=0/1 (11/10): Mall area 39's branch - 0 Shutter#8 (on to 19), 1 Shutter#12
                                         // (A+B gate wall, on to 44); -1 (default) = cabinet 02 picks at random
static bool g_spikeoutGateLog = false;  // GateLog=1: dump area-clear / gate-openable candidate flags to gatelog.txt
// CpicLog=1 (11/15): the game's texture cache ("[CPM]: No idle cpic." stopped 05 at the character select).
// Descriptor 0x1878A0 (+4 first, +6 last entry), table 0x1873C4, 8 bytes per entry: +0 picture id (-1 free),
// +2 part number (the allocator 0x8654 only takes part 0), +4 age (0 kept, 1 used now, >1 idle; it takes the
// oldest part-0 entry with age > 1), +6 flag. cpiclog.txt: a summary line every 60 frames when it changes, the
// whole table every 600 frames, and once the game sits in the error loop (PC 0x86BC / 0x8704) the table,
// registers and stack.
static bool g_spikeoutCpicLog = false;
static void SpikeoutCpicLog(const UINT8 *ram, UINT32 frame)
{
	static FILE *f = NULL; static bool dumped = false; static char last[160] = "";
	if (dumped) return;
	const UINT32 d = 0x1878A0, t = 0x1873C4;
	int first = (short) *(const UINT16 *) &ram[(d + 4) ^ 2], lastE = (short) *(const UINT16 *) &ram[(d + 6) ^ 2];
	if (first < 0 || lastE < first || lastE > 1023) return;
	const UINT32 pc = ppc_get_pc();
	const bool err = (pc == 0x86BC || pc == 0x8704);
	if (!err && (frame % 60) != 0) return;
	if (!f) f = fopen("cpiclog.txt", "w");
	if (!f) return;
	int nFree = 0, nKept = 0, nUsed = 0, nIdle = 0, nIdleHead = 0;
	for (int i = first; i <= lastE; i++)
	{
		const UINT32 a = t + (UINT32) i * 8;
		const int id = (short) *(const UINT16 *) &ram[(a + 0) ^ 2], part = (short) *(const UINT16 *) &ram[(a + 2) ^ 2];
		const int age = (short) *(const UINT16 *) &ram[(a + 4) ^ 2];
		if (id == -1) nFree++;
		else if (age == 0) nKept++; else if (age == 1) nUsed++; else { nIdle++; if (part == 0) nIdleHead++; }
	}
	char s[160];
	snprintf(s, sizeof(s), "entries=%d free=%d kept=%d used=%d idle=%d idleHead=%d ls=%u area=%u mask=%08X",
	         lastE - first + 1, nFree, nKept, nUsed, nIdle, nIdleHead, (unsigned) ram[0x140104 ^ 3], (unsigned) ram[0x10402D ^ 3],
	         *(const UINT32 *) &ram[0x140128]);
	if (err || strcmp(s, last)) { fprintf(f, "f=%u %s\n", frame, s); strcpy(last, s); }
	if (err || (frame % 600) == 0)
	{
		fprintf(f, "%s f=%u\n", err ? "ERROR  [CPM]: No idle cpic / cpic area is NOT full" : "TABLE", frame);
		for (int i = first; i <= lastE; i++)
		{
			const UINT32 a = t + (UINT32) i * 8;
			fprintf(f, " %3d:%4d/%-2d %5d %02X%s", i, (short) *(const UINT16 *) &ram[(a + 0) ^ 2], (short) *(const UINT16 *) &ram[(a + 2) ^ 2],
			        (short) *(const UINT16 *) &ram[(a + 4) ^ 2], (unsigned) ram[(a + 6) ^ 3], ((i - first) % 8) == 7 ? "\n" : "");
		}
		fprintf(f, "\n");
	}
	if (err)
	{
		fprintf(f, "PC=%08X LR=%08X\n", pc, ppc_get_lr());
		for (unsigned r = 0; r < 32; r++) fprintf(f, "r%-2u=%08X%s", r, ppc_get_gpr(r), (r % 8) == 7 ? "\n" : " ");
		fprintf(f, "desc 0x1878A0:");
		for (unsigned k = 0; k < 0x40; k += 4) fprintf(f, " %08X", *(const UINT32 *) &ram[d + k]);
		const UINT32 sp = ppc_get_gpr(1) & 0x7FFFFC;
		fprintf(f, "\nstack %08X:", sp);
		for (unsigned k = 0; k < 0x200 && sp + k < 0x800000; k += 4) fprintf(f, "%s%08X", (k % 32) == 0 ? "\n " : " ", *(const UINT32 *) &ram[sp + k]);
		fprintf(f, "\n");
		dumped = true;
		printf("[CPICLOG] the game stopped on [CPM]: No idle cpic - see cpiclog.txt\n");
	}
	fflush(f);
}
static bool g_spikeoutAILog = false;    // AIPlayerLog=1: dump what the AI sees to ai.txt
static bool g_spikeoutAIProfile = false;   // 11/23 AIProfile=1: aiprof.txt, the AI's cost and the frame rate per 600 frames
static unsigned g_aipFnSearch = 0, g_aipWnSearch = 0, g_aipFnPops = 0;   // 11/23 AIProfile: path searches (not reused) in this period
// 11/18 (user): the gate decisions ("[GATE] ...", console only before) also go to gatelog.txt (AIPlayerLog=1), with
// the frame, so a "passed" taken from a camera zone (camsar) change short of the gate can be seen in the log.
static FILE *g_gateLogF = NULL;
static void SpikeoutGateNote(unsigned frame, const char *fmt, ...)
{
	char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
	fputs(buf, stdout);
	if (!g_spikeoutAILog) return;
	if (!g_gateLogF) { g_gateLogF = fopen("gatelog.txt", "w"); if (!g_gateLogF) return; }
	fprintf(g_gateLogF, "f=%u %s", frame, buf); fflush(g_gateLogF);
}
// 11/15 AITargetMarker=1 (bot cabinets only, display only - no game RAM touched): a ball in the bot's own colour 1.2
// above its target's Rob, and a thin line from 1.2 above its own Rob to it. Filled here each frame the AI runs; drawn
// over the picture by the OSD (Main.cpp EndFrameVideo) with the game camera and New3D's main viewport.
// 11/15 (user): area 75 start room (before its Door#1 is passed: arflg bit 0x10 at fa_scenario +0x284) - bots only target bosses.
static bool SpikeoutA75BossOnly(const UINT8 *ram)
{
	if (ram[0x10402D ^ 3] != 75) return false;
	UINT32 scen = *(const UINT32 *) &ram[0x10413C] & 0x7FFFFF;
	UINT32 arflg = (scen >= 0x10000 && scen < 0x700000) ? *(const UINT32 *) &ram[scen + 0x284] : 0;
	return !(arflg & 0x10u);
}
// 11/24 (owner): in area 75's start room the bosses-only rule holds only until this bot has once been within 1 (xz) of a
// live boss; from then on it targets small fry too, as everywhere. Cleared when the start room is left (Door#1 passed /
// another area), so the next start begins bosses-only again. Set in the target pick (each cabinet runs one bot).
static bool g_a75Near = false;
static bool g_spikeoutAITargetMarker = false;
// BotCanOpenGate=0/1 (11/21, owner; default 1 = as before): 0 = the bots never break a gate (shutters, the doors handled
// as gates, the recorded jump-kick spots, area 44's hidden-route doors). They wait 3.5 short of it until a player
// breaks it; once it is broken they go through as before. Doors that open by walking into them still open.
static bool g_spikeoutBotCanOpenGate = true;
struct SpikeoutMarker { UINT32 f; int on; float cam[3], itr[3], me[3], tg[3]; unsigned colour; };
SpikeoutMarker g_spkMarker = { 0, 0, { 0 }, { 0 }, { 0 }, { 0 }, 0 };
// 11/17 AIWallView=1 (bot cabinets, display only): the walls round the bot, from the live wall list *(*(0x106168)+8)
// (record: +0x00 flags, +0x0C/+0x14 one end x/z, +0x18/+0x20 the other, +0x10/+0x1C bottom y, +0x24/+0x28 height).
// Filled here each frame the AI runs (9 floats a wall: x0 y0 z0 x1 y1 z1 top0 top1 kind; kind 0 = can't pass, 1 =
// can be got over - a step / a jump (top within 2.3 of our feet) or a player-passable wall), drawn by the OSD
// (Main.cpp SpikeoutDrawWalls) with the game camera. The OSD runs on the render thread: hand over under a mutex.
#include <vector>
static bool g_spikeoutWallView = false;
static std::mutex g_spkWallMx;
static std::vector<float> g_spkWallBuf;
static float g_spkWallCam[6] = { 0 };
static UINT32 g_spkWallF = 0;
void SpikeoutWallViewGet(std::vector<float> &out, float *cam6, uint32_t *f)
{
	std::lock_guard<std::mutex> lk(g_spkWallMx);
	out = g_spkWallBuf;
	for (int k = 0; k < 6; k++) cam6[k] = g_spkWallCam[k];
	*f = g_spkWallF;
}
static bool g_spikeoutAICalib = false;  // AICalib=1: cycle UP/DOWN/LEFT/RIGHT to measure the
                                        // stick->world mapping (camera orientation) in ai.txt
// ScenarioLog=1: write each change in fa_scenario to scenario.txt, to learn which values mean enemies
// cleared / gate open. Only while in play. Base by pointer: the 8-player patch moves it. Skips the
// always-running counters (SC_TM1..6 +0x14, sync/synce +0x28C) and stops at a size cap.
static bool g_spikeoutScenarioLog = false;
// area_id 36 = the shared inter-area gate-passage script (every area does Jump36(ret = next area), plays
// the gate-passage sequence, then ReturnFrom36 into the next area). Nothing for the AI to do there.
#define SPIKEOUT_AREA_GATE_PASSAGE 36
// 0x10402C is the stage id (STAGE TABLE at prog 0x8B960), not a play flag: 0 BLANK, 1 SUNSET, 3 BASKET2
// (Diesel Town), 4 DEPART (Roy's Mall), 5 OPERA, 6 SHIPYARD, 7 SPKHOME; 2 and 8-15 are unused / test /
// attract-demo stages. Measured: 7 through the prologue areas 75/86/87, 3 in Diesel Town, 0 around character
// select. The title / attract sequence also shows 6, the same id as the real Shipyard, so 6 only counts as a
// stage when this cabinet is in a game: node 1 and this node both in the link presence mask 0x14002C (MSB =
// node 1; attract shows 0 or a group without node 1). Callers still check the Rob is active.
// A bot that dies drops out of the mask until it continues, so "in a game" is latched while this cabinet's
// own player has been alive in a stage within the last 35s (the 30s continue window + slack).
static inline bool SpikeoutInStage(const UINT8 *ram)
{
	static bool inGame = false; static UINT32 lastAlive = 0;
	unsigned m = ram[0x10402C ^ 3];
	if (m == 0) { inGame = false; return false; }
	if (m == 2 || m >= 8) return false;
	if (ram[0x140104 ^ 3] == 1) { inGame = false; return false; }   // own link state 1 = title / attract (11/11): the demo's Robs
	                                                               // look alive and node 1 can share the attract group
	unsigned node = ram[0x10065B ^ 3];
	if (node < 1 || node > 8) return m != 6;
	UINT32 mask = *(const UINT32 *) &ram[0x14002C];
	bool member = (mask & 0x80000000u) && (mask & (0x80000000u >> (node - 1)));
	UINT32 rob = 0x1A0000 + (node - 1) * 0x4000;
	bool alive = (*(const UINT32 *) &ram[rob] & 1) && (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2] > 0;
	if (alive && (m != 6 || member)) { inGame = true; lastAlive = g_ppcTraceFrame; }
	if (inGame && g_ppcTraceFrame - lastAlive > 2100) inGame = false;
	if (m != 6) return true;
	return member || inGame;
}
static inline bool SpikeoutAttract(const UINT8 *ram) { return ram[0x10402C ^ 3] == 6 && !SpikeoutInStage(ram); }

static const UINT8 *SpikeoutMapPtr(UINT32 ptr, UINT32 len);
// ItemLog=1 (11/12): diagnostic for items on the floor (pre-placed and enemy drops). Area objects live at
// 0x1F4000 + i*0xBC, count = halfword *(0x10411C)+0x10. Per object: +0 flags, +4 def pointer (def+0x21 =
// pickup bits: 01 HP1/4, 02 time, 04 SP, 08 invincible, 10 HP1/2, 20 HP1/8), +0x2C/30/34 x/y/z, +0x52 timer,
// +0x57 owner slot. Pickup (0x3A1B0) needs flags 0x10000000, not 0x00020000, and 0x8 or 0x00400000. Writes
// itemlog.txt: an object line when its flags / def / owner change or it moves more than 1.0, and an HP+/SP+
// line when a player's HP (Rob +0x3F02) or special stock (+0x3F25) goes up.
static bool g_spikeoutItemLog = false;
extern bool g_texGhost;   // Real3D.cpp: Spikeout costumes 4-7 in ghost texture pages (New3D)
static void SpikeoutItemLog(const UINT8 *ram)
{
	static FILE *f = nullptr;
	const int MAXO = 0x106;
	static UINT32 pFlags[MAXO], pDef[MAXO]; static int pOwner[MAXO]; static float pX[MAXO], pY[MAXO], pZ[MAXO];
	static int pCount = -1; static unsigned pArea = 0xFFFF; static int pHp[8], pSp[8]; static bool init = false;
	auto R32 = [&](UINT32 a) { a &= 0x7FFFFC; return *(const UINT32 *) &ram[a]; };
	auto R16 = [&](UINT32 a) { a &= 0x7FFFFE; return (unsigned) *(const UINT16 *) &ram[a ^ 2]; };
	auto R8  = [&](UINT32 a) { a &= 0x7FFFFF; return (unsigned) ram[a ^ 3]; };
	auto RF  = [&](UINT32 a) { a &= 0x7FFFFC; return *(const float *) &ram[a]; };
	auto B   = [&](UINT32 a) -> int { const UINT8 *q = SpikeoutMapPtr(a & ~3u, 4); return q ? q[(a & 3) ^ 3] : -1; };
	if (!f && !(f = fopen("itemlog.txt", "w"))) return;
	UINT32 mgr = R32(0x10411C) & 0x7FFFFF;
	int count = (mgr >= 0x100000 && mgr < 0x7FF000) ? (int) R16(mgr + 0x10) : 0;
	if (count > MAXO) count = MAXO;
	unsigned area = R8(0x10402D);
	bool any = false;
	if (!init || area != pArea || count != pCount)
	{
		fprintf(f, "f=%u AREA %u stg=%u count=%d (was %d)\n", g_ppcTraceFrame, area, R8(0x10402C), count, pCount);
		if (!init || area != pArea) for (int i = 0; i < MAXO; i++) { pFlags[i] = 0xFFFFFFFF; pDef[i] = 0; pOwner[i] = -2; }
		pArea = area; pCount = count; any = true;
	}
	for (int i = 0; i < count; i++)
	{
		UINT32 o = 0x1F4000 + (UINT32) i * 0xBC;
		UINT32 fl = R32(o), def = R32(o + 4);
		int own = (int) R8(o + 0x57); if (own == 0xFF) own = -1;
		float x = RF(o + 0x2C), y = RF(o + 0x30), z = RF(o + 0x34);
		float dx = x - pX[i], dy = y - pY[i], dz = z - pZ[i];
		bool moved = pFlags[i] != 0xFFFFFFFF && dx * dx + dy * dy + dz * dz > 1.0f;
		if (fl == pFlags[i] && def == pDef[i] && own == pOwner[i] && !moved) continue;
		int type = B(def + 0x21);
		bool pick = (fl & 0x10000000) && !(fl & 0x00020000) && (fl & 0x00400008);
		fprintf(f, "f=%u O%03d fl=%08X (was %08X) def=%08X type=%02X%s owner=%d t52=%d h54=%04X b56=%02X b58=%02X b59=%02X b5B=%02X pos=(%.2f,%.2f,%.2f)%s\n",
		        g_ppcTraceFrame, i, fl, pFlags[i], def, type & 0xFF, type < 0 ? "?" : "", own, (int) (INT16) R16(o + 0x52), R16(o + 0x54),
		        R8(o + 0x56), R8(o + 0x58), R8(o + 0x59), R8(o + 0x5B), x, y, z, pick ? " PICKABLE" : "");
		// 11/17 (owner): a clear TAKE line when something is taken - the "taken" flag 0x00020000 comes on, or a player
		// (slot 0-7) becomes its owner (+0x57). With the def's number in the ROM table 0xFFF6F120 (71 defs, see
		// object_defs.md), the type bits, and the nearest player within 2.0 (who most likely took it).
		const bool nowTaken = (fl & 0x00020000u) && pFlags[i] != 0xFFFFFFFF && !(pFlags[i] & 0x00020000u);
		const bool nowOwned = own >= 0 && own < 8 && own != pOwner[i] && pFlags[i] != 0xFFFFFFFF;
		if (nowTaken || nowOwned)
		{
			int tidx = -1;
			for (int k = 0; k < 96; k++)
			{
				const UINT8 *q = SpikeoutMapPtr(0xFFF6F120u + (UINT32) k * 4, 4);
				if (!q) break;
				UINT32 w = *(const UINT32 *) q;
				if (w < 0xFFF00000u) break;
				if (w == def) { tidx = k; break; }
			}
			int who = -1; float bd = 4.0f;
			for (int p = 0; p < 8; p++)
			{
				UINT32 r = 0x1A0000 + (UINT32) p * 0x4000;
				if (!(R32(r) & 1)) continue;
				float px2 = RF(r + 0x380) - x, pz2 = RF(r + 0x388) - z, d = px2 * px2 + pz2 * pz2;
				if (d < bd) { bd = d; who = p; }
			}
			int type2 = B(def + 0x21);
			fprintf(f, "f=%u TAKE O%03d by=%s%d near=p%d (%.2f) def=%08X table#%d type=%02X%s pos=(%.2f,%.2f,%.2f)\n",
			        g_ppcTraceFrame, i, nowOwned ? "owner p" : "flag ", nowOwned ? own : -1, who, who >= 0 ? sqrtf(bd) : -1.0f,
			        def, tidx, type2 & 0xFF,
			        (type2 & 0xFF) == 0x01 ? " HP1/4" : (type2 & 0xFF) == 0x10 ? " HP1/2" : (type2 & 0xFF) == 0x20 ? " HP1/8" :
			        (type2 & 0xFF) == 0x04 ? " SP" : (type2 & 0xFF) == 0x08 ? " MUTEKI" : (type2 & 0xFF) == 0x02 ? " TIME" : " (no pickup bits)",
			        x, y, z);
		}
		pFlags[i] = fl; pDef[i] = def; pOwner[i] = own; pX[i] = x; pY[i] = y; pZ[i] = z; any = true;
	}
	for (int p = 0; p < 8; p++)
	{
		UINT32 r = 0x1A0000 + (UINT32) p * 0x4000;
		int hp = (INT16) R16(r + 0x3F02), sp = (int) R8(r + 0x3F25);
		if (init && R32(r) & 1)
		{
			if (hp > pHp[p] && pHp[p] > 0) { fprintf(f, "f=%u HP+ p%d %d -> %d pos=(%.2f,%.2f,%.2f)\n", g_ppcTraceFrame, p, pHp[p], hp, RF(r + 0x380), RF(r + 0x384), RF(r + 0x388)); any = true; }
			if (sp > pSp[p]) { fprintf(f, "f=%u SP+ p%d %d -> %d pos=(%.2f,%.2f,%.2f)\n", g_ppcTraceFrame, p, pSp[p], sp, RF(r + 0x380), RF(r + 0x384), RF(r + 0x388)); any = true; }
		}
		pHp[p] = hp; pSp[p] = sp;
	}
	init = true;
	if (any) fflush(f);
}
static void SpikeoutScenarioLog(const UINT8 *ram)
{
	static UINT8 prev[0x3A0];
	static bool have = false;
	static unsigned lines = 0;
	static int prevNact = -1;
	static FILE *f = NULL;
	if (lines > 300000 || SpikeoutAttract(ram)) { have = false; return; }
	UINT32 scn = *(const UINT32 *) &ram[0x10413C] & 0x7FFFFF, cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
	if (scn < 0x110000 || scn >= 0x120000) return;
	if (!f && !(f = fopen("scenario.txt", "w"))) return;
	unsigned pcnt = ram[0x10065A ^ 3];
	int nact = 0;
	for (unsigned e = pcnt; e < pcnt + 12 && e < 20; e++)
		if (*(const UINT32 *) &ram[0x1A0000 + e * 0x4000] & 1) nact++;
	bool any = false;
	for (unsigned i = 0x14; i < 0x3A0; i++)
	{
		if (i < 0x20 || (i >= 0x28C && i < 0x294)) continue;
		UINT8 v = ram[(scn + i) ^ 3];
		if (have && v != prev[i])
		{
			fprintf(f, "f=%u +%03X %02X->%02X enemies=%d area=%u camar=%u\n", g_ppcTraceFrame, i, prev[i], v, nact, (unsigned) ram[0x10402D ^ 3], (unsigned) ram[(cam + 0xC0) ^ 3]);
			lines++; any = true;
		}
		prev[i] = v;
	}
	if (have && nact != prevNact)
	{
		fprintf(f, "f=%u ENEMIES %d->%d area=%u camar=%u\n", g_ppcTraceFrame, prevNact, nact, (unsigned) ram[0x10402D ^ 3], (unsigned) ram[(cam + 0xC0) ^ 3]);
		lines++; any = true;
	}
	prevNact = nact;
	have = true;
	if (any) fflush(f);
}


// Waypoints: a tool to note positions (e.g. where a gate is broken). WaypointRecord=1 on cabinet 01 appends
// P1's position to the file at each coin press. The bots read it back as jump-kick spots (SpikeoutWpKickLoad). The cabinets run in sibling folders,
// so it lives in their parent. Area = 0x10402D (written on area entry at 0x59C94/0x5A3C4).
#define SPIKEOUT_WAYPOINT_FILE "../waypoints.txt"
static bool g_spikeoutWaypointRecord = false;
static void SpikeoutRecordWaypoint(const UINT8 *ram)
{
	if (ram[0x10065B ^ 3] != 1 || SpikeoutAttract(ram) || !(*(const UINT32 *) &ram[0x1A0000] & 1)) return;   // P1 in play only
	UINT32 cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
	unsigned area = ram[0x10402D ^ 3];
	float x = *(const float *) &ram[0x1A0380], y = *(const float *) &ram[0x1A0384], z = *(const float *) &ram[0x1A0388];
	FILE *f = fopen(SPIKEOUT_WAYPOINT_FILE, "a");
	if (!f) { printf("[WAYPOINT] can't open %s\n", SPIKEOUT_WAYPOINT_FILE); return; }
	UINT32 scn = *(const UINT32 *) &ram[0x10413C] & 0x7FFFFF;   // fa_scenario: by pointer, the 8-player patch moves it too
	fprintf(f, "area=%u x=%.3f y=%.3f z=%.3f b2f=%u camar=%u camsar=%u cond=%08X alara=%u arflg=%08X stflg=%08X\n", area, x, y, z, (unsigned) ram[0x10402F ^ 3],
	        (unsigned) ram[(cam + 0xC0) ^ 3], (unsigned) ram[(cam + 0xC1) ^ 3], *(const UINT32 *) &ram[0x1A0000 + 0x1B34],
	        (unsigned) ram[(scn + 0x2D5) ^ 3], *(const UINT32 *) &ram[scn + 0x284], *(const UINT32 *) &ram[scn + 0x288]);
	fclose(f);
	printf("[WAYPOINT] area=%u x=%.2f z=%.2f\n", area, x, z);
}
// Diagnostic: dump candidate "area cleared / gate openable" flags to gatelog.txt on change
// (plus a heartbeat), so we can see which value flips when a wave is cleared and the gate opens.
// DF_ARE/DFDARE etc. live in the fixed 0x104000 globals (not moved by the 8-player patch); the
// gate work and fa_scenario are read through their pointers, so they follow the patch's relocation.
static void SpikeoutGateLog(const UINT8 *ram)
{
	if (!SpikeoutInStage(ram)) return;                        // only while in a live stage
	static FILE *f = NULL;
	static UINT32 prevSig = ~0u, lastBeat = 0;
	unsigned stage  = ram[0x10402C ^ 3];
	unsigned area   = ram[0x10402D ^ 3];
	unsigned dfare  = *(const UINT16 *) &ram[0x104048 ^ 2];   // kills in the current area
	unsigned dfstg  = *(const UINT16 *) &ram[0x104046 ^ 2];
	unsigned dfall  = *(const UINT16 *) &ram[0x104044 ^ 2];
	unsigned dfdare = ram[0x104068 ^ 3];                      // "area defeat complete" flag/counter
	UINT32 gwp  = *(const UINT32 *) &ram[0x1044D8] & 0x7FFFFF;   // gate_work_ptr
	UINT32 scen = *(const UINT32 *) &ram[0x10413C] & 0x7FFFFF;   // fa_scenario work (moves with the patch)
	UINT32 arflg = (scen >= 0x10000 && scen < 0x700000) ? *(const UINT32 *) &ram[scen + 0x284] : 0;
	UINT32 stflg = (scen >= 0x10000 && scen < 0x700000) ? *(const UINT32 *) &ram[scen + 0x288] : 0;
	UINT32 sig = (area << 24) ^ (dfare << 8) ^ dfdare ^ (arflg * 2654435761u) ^ (stflg * 40503u);
	bool beat = (g_ppcTraceFrame - lastBeat) >= 120;
	if (sig == prevSig && !beat) return;
	prevSig = sig; lastBeat = g_ppcTraceFrame;
	if (!f) { f = fopen("gatelog.txt", "w"); if (!f) return; }
	fprintf(f, "f=%u stage=%u area=%u DF_ARE=%u DF_STG=%u DF_ALL=%u DFDARE=%u ARflg=%08X STflg=%08X gw=%06X[",
	        g_ppcTraceFrame, stage, area, dfare, dfstg, dfall, dfdare, arflg, stflg, gwp);
	if (gwp >= 0x10000 && gwp < 0x700000) for (int i = 0; i < 48; i++) fprintf(f, "%02X", ram[(gwp + i) ^ 3]);   // dump gate work header to find now_hit/broken/endurance
	fprintf(f, "]\n");
	fflush(f);
}

// gatedump.txt (GateLog=1): the whole gate work area, to read every area's gates (count, x/y/z, endurance,
// broken) straight from memory. In area 75 its first entry is (19.64, 0, 37.24): the ROM's gate object
// 0x20A0 at 0xFFEB29D4 (endurance 100, model 0x2369), next to the gate 01 learned at (19.04, 34.71).
// A dump is written on an area / camar / area-flag / gate-pointer change, when the block's contents change
// (at most every 0.5s - a gate being hit shows its endurance dropping), and every 10s otherwise.
// Line: f stage area camar arflg p1=(x,y,z) ptrs=<0x1044C0..0x1044FF> gw=<ptr> [GATE_DUMP_BYTES bytes from gw]
// Door / shutter work tables (gatework.txt), written when any entry's flags change (checked every 15f):
//  S <count> then per shutter: i flags x y z     D <count> then per door: i flags defptr [x y z camar | def unreadable]
static const UINT8 *SpikeoutMapPtr(UINT32 ptr, UINT32 len);
static void SpikeoutGateWorkDump(const UINT8 *ram)
{
	if (g_ppcTraceFrame % 15) return;
	UINT32 sw = *(const UINT32 *) &ram[0x104514] & 0x7FFFFF, dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	unsigned sc = (sw >= 0x10000 && sw < 0x700000) ? ram[(sw + 0x10) ^ 3] : 0; if (sc > 96) sc = 96;
	unsigned dc = (dw >= 0x10000 && dw < 0x700000) ? *(const UINT16 *) &ram[(dw + 0x800) ^ 2] : 0; if (dc > 128) dc = 128;
	UINT32 h = 2166136261u ^ sw ^ (dw << 1) ^ (sc << 8) ^ (dc << 16);
	for (unsigned i = 0; i < sc; i++) h = (h ^ *(const UINT32 *) &ram[sw + 0x18 + i * 0x1C]) * 16777619u;
	for (unsigned i = 1; i <= dc; i++) h = (h ^ *(const UINT32 *) &ram[dw + 0x7EC + i * 0x18]) * 16777619u;
	static UINT32 pH = 0;
	if (h == pH) return;
	pH = h;
	FILE *f = fopen("gatework.txt", "a");
	if (!f) return;
	UINT32 scen = *(const UINT32 *) &ram[0x10413C] & 0x7FFFFF;
	UINT32 arflg = (scen >= 0x10000 && scen < 0x700000) ? *(const UINT32 *) &ram[scen + 0x284] : 0;
	fprintf(f, "f=%u stage=%u area=%u arflg=%08X sw=%06X S=%u dw=%06X D=%u\n", g_ppcTraceFrame, (unsigned) ram[0x10402C ^ 3], (unsigned) ram[0x10402D ^ 3],
	        arflg, sw, sc, dw, dc);
	for (unsigned i = 0; i < sc; i++)
	{
		UINT32 e = sw + 0x18 + i * 0x1C;
		fprintf(f, " S%u %08X (%.2f,%.2f,%.2f) w:", i, *(const UINT32 *) &ram[e], *(const float *) &ram[e + 0x0C], *(const float *) &ram[e + 0x10], *(const float *) &ram[e + 0x14]);
		for (unsigned k = 0; k < 7; k++) fprintf(f, " %08X", *(const UINT32 *) &ram[e + k * 4]);
		fputc('\n', f);
	}
	for (unsigned i = 1; i <= dc; i++)
	{
		UINT32 e = dw + 0x7EC + i * 0x18, dp = *(const UINT32 *) &ram[e + 4];
		fprintf(f, " D%u %08X def=%08X", i, *(const UINT32 *) &ram[e], dp);
		const UINT8 *d = SpikeoutMapPtr(dp, 0x34);
		if (d) fprintf(f, " (%.2f,%.2f,%.2f) camar=%02X", *(const float *) &d[8], *(const float *) &d[12], *(const float *) &d[16], d[0x2E ^ 3]);
		else fprintf(f, " def?");
		fprintf(f, " w:");
		for (unsigned k = 0; k < 6; k++) fprintf(f, " %08X", *(const UINT32 *) &ram[e + k * 4]);
		fputc('\n', f);
	}
	fclose(f);
}

#define GATE_DUMP_BYTES 1024
static void SpikeoutGateDump(const UINT8 *ram)
{
	if (!SpikeoutInStage(ram)) return;
	UINT32 gwp = *(const UINT32 *) &ram[0x1044D8] & 0x7FFFFF;
	if (gwp < 0x10000 || gwp + GATE_DUMP_BYTES > 0x800000) return;
	UINT32 cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
	UINT32 scen = *(const UINT32 *) &ram[0x10413C] & 0x7FFFFF;
	unsigned stage = ram[0x10402C ^ 3], area = ram[0x10402D ^ 3], camar = ram[(cam + 0xC0) ^ 3];
	UINT32 arflg = (scen >= 0x10000 && scen < 0x700000) ? *(const UINT32 *) &ram[scen + 0x284] : 0;
	UINT32 h = 2166136261u;                      // FNV-1a of the block: did any gate change?
	for (int i = 0; i < GATE_DUMP_BYTES; i++) h = (h ^ ram[(gwp + i) ^ 3]) * 16777619u;
	static UINT32 pKey = ~0u, pArflg = ~0u, pGw = ~0u, pHash = 0, lastF = 0;
	UINT32 key = (area << 8) | camar;
	bool major = key != pKey || arflg != pArflg || gwp != pGw;
	bool minor = h != pHash && g_ppcTraceFrame - lastF >= 30;
	bool beat = g_ppcTraceFrame - lastF >= 600;
	if (!major && !minor && !beat) return;
	pKey = key; pArflg = arflg; pGw = gwp; pHash = h; lastF = g_ppcTraceFrame;
	static FILE *f = NULL;
	if (!f) { f = fopen("gatedump.txt", "w"); if (!f) return; }
	fprintf(f, "f=%u stage=%u area=%u camar=%u arflg=%08X why=%s p1=(%.2f,%.2f,%.2f) ptrs=", g_ppcTraceFrame, stage, area, camar, arflg,
	        major ? "change" : (minor ? "content" : "beat"),
	        *(const float *) &ram[0x1A0380], *(const float *) &ram[0x1A0384], *(const float *) &ram[0x1A0388]);
	for (int i = 0; i < 64; i++) fprintf(f, "%02X", ram[(0x1044C0 + i) ^ 3]);
	fprintf(f, " gw=%06X [", gwp);
	for (int i = 0; i < GATE_DUMP_BYTES; i++) fprintf(f, "%02X", ram[(gwp + i) ^ 3]);
	fprintf(f, "]\n");
	fflush(f);
}

// Area exits from the ROM's own area scripts (prog 0xA1D24 + area*8 -> init / loop scripts; table made by
// 8p\make_area_exits.py from prog.bin + crom.bin + banked_crom.bin): "02 03 0A n" lights Door#n as the exit,
// "02 03 0C n" lights Shutter#n; "02 16 0284 v" sets arflg bits v (fa_scenario +0x284) and "01 0C n 04000000"
// waits for a player to go through Door#n. cond = the arflg bits the script set last before lighting the item
// (the scripts branch, so only that last set is sure to be there); done = the first bits it sets once a player
// went through (0 = none: shutters break into the next area). Rows are in script order per area.
// Positions: the middle of the item's own wall (gate wall list, by the def's wall id) - the def's coordinates
// are off for some (area 77 Shutter#15 by 5.4, area 78 Shutter#23 by 22); without a wall, the ROM def.
// Stage per area from the scripts' area links (02 35 / 02 00 next area, 02 01 stage + area). Verified: area 75
// Door#1 (39.9, 50.0) at 0x200 and Shutter#0/#1 at 0x219, area 86 Door#13/14 (-18.0, -5.3) at 0x209.
// Junk exits in the ROM data: lit by the script but can't be opened or passed. Skipped everywhere a shutter
// is picked as a target. (Shipyard Shutter#46 was here for a while; it is a real gate, now with its position
// from 01's waypoint and the high-gate jump kick.)
static bool SpikeoutJunkShutter(unsigned stage, unsigned id) { (void) stage; (void) id; return false; }
struct SpikeoutExit { UINT8 area, stage; char kind; UINT8 id; UINT16 cond, done; UINT32 flags; float x, z, y; };
static const SpikeoutExit s_spikeoutExits[] =
{
	// 11/09 bot filter (3D map): only the gates the bots are to open. Shutter rows (= where the players appear in the
	// next area) now point at the gate wall to break at them - Shipyard: B / A+B walls, Diesel Town and Roy's Mall:
	// A+B walls, Opera: none - with picks by hand (area 3 Shutter#2 -> its B wall, area 51 Shutter#36). Doors:
	// openable / normal / special / heavy, and automatic ones with a B or A+B wall. Background doors, big doors,
	// shutter rows with no such wall (area 25, area 50) are out.
	{   1, 3, 'D',   3, 0x0008, 0x0010, 0x00001000u,     1.500f,   -13.574f,   -0.15f },
	{   1, 3, 'D',   4, 0x0008, 0x0010, 0x00001000u,    -1.500f,   -13.978f,   -0.15f },
	{   1, 3, 'D',  15, 0x0040, 0x0080, 0x00109000u,   -24.228f,   -16.266f,    1.14f },
	{   2, 3, 'D',   7, 0x0040, 0x0010, 0x00003000u,   -59.500f,    26.000f,   -1.50f },
	{   2, 3, 'D',   8, 0x0040, 0x0010, 0x00003000u,   -59.500f,    31.000f,   -1.50f },
	{   3, 3, 'S',   2, 0x0040, 0x0000, 0x20100000u,   -73.300f,    27.800f,   -7.50f },   // ゲート壁 B型（指定）
	{   4, 3, 'D',   9, 0x0008, 0x0010, 0x00001000u,   108.077f,    40.162f,   -0.05f },
	{   4, 3, 'D',  10, 0x0008, 0x0010, 0x00001000u,   109.493f,    38.102f,   -0.05f },
	{   4, 3, 'D',  45, 0x0040, 0x0080, 0x00009000u,    54.750f,    11.750f,   -0.07f },   // 11/14 (user): back to the ROM point; treated as a gate (see teShutter)
	{   5, 3, 'D',  43, 0x0040, 0x0080, 0x00109000u,   -70.062f,    28.732f,   -1.50f },   // 11/10: same spot as area 12 (01's waypoint; was -70.748, 29.212)
	{   6, 3, 'D',  16, 0x0008, 0x0040, 0x00009000u,    18.150f,    12.000f,   -1.50f },
	{   6, 3, 'D',  11, 0x0040, 0x0010, 0x00101000u,     9.000f,    22.325f,   -1.50f },
	{   6, 3, 'D',  12, 0x0040, 0x0010, 0x00101000u,     9.000f,    25.275f,   -1.50f },
	{   6, 3, 'D',   5, 0x0040, 0x0020, 0x00101000u,    44.316f,    36.484f,   -1.50f },
	{   6, 3, 'D',   6, 0x0040, 0x0020, 0x00101000u,    42.824f,    36.484f,   -1.50f },
	{  12, 3, 'D',   7, 0x0040, 0x0480, 0x00003000u,   -59.500f,    26.000f,   -1.50f },
	{  12, 3, 'D',   8, 0x0040, 0x0480, 0x00003000u,   -59.500f,    31.000f,   -1.50f },
	{  12, 3, 'D',  43, 0x0040, 0x0880, 0x00109000u,   -70.062f,    28.732f,   -1.50f },   // 11/10: moved to 01's waypoint (was -70.748, 29.212)
	{  14, 3, 'D',  14, 0x0040, 0x0080, 0x00109000u,   -24.600f,    -4.600f,   -1.50f },
	{  15, 3, 'D',   3, 0x0000, 0x0010, 0x00001000u,     1.500f,   -13.574f,   -0.15f },
	{  15, 3, 'D',   4, 0x0000, 0x0010, 0x00001000u,    -1.500f,   -13.978f,   -0.15f },
	{  15, 3, 'D',   1, 0x0040, 0x0400, 0x00001000u,    -6.375f,    -3.450f,    0.00f },   // 11/11: cond 0x0040 (break the gate, comes with the shine); was 0x0010 (set as soon as D#3/4 open) + a 0x0004 copy
	{  15, 3, 'D',  11, 0x0040, 0x0800, 0x00101000u,     9.000f,    22.325f,   -1.50f },
	{  15, 3, 'D',  12, 0x0040, 0x0800, 0x00101000u,     9.000f,    25.275f,   -1.50f },
	{  17, 4, 'S',  15, 0x0009, 0x0000, 0x20200000u,   -10.200f,     1.200f,    8.03f },   // ゲート壁 A+B型
	{  17, 4, 'S',  16, 0x0009, 0x0000, 0x20200000u,   -10.200f,    -1.200f,    8.03f },   // ゲート壁 A+B型
	{  18, 4, 'S',  23, 0x0008, 0x0000, 0x20200000u,   -49.250f,    -3.300f,   -7.97f },   // ゲート壁 A+B型
	{  18, 4, 'S',  24, 0x0008, 0x0000, 0x20200000u,   -46.750f,    -3.300f,   -7.97f },   // ゲート壁 A+B型
	{  19, 4, 'S',   9, 0x0001, 0x0000, 0x20100000u,   -19.000f,   -63.750f,   -8.00f },   // ゲート壁 A+B型
	{  20, 4, 'D',   9, 0x0000, 0x0010, 0x00001040u,   -18.366f,     4.949f,   -8.80f },
	{  20, 4, 'D',  10, 0x0000, 0x0010, 0x00001040u,   -17.649f,     3.865f,   -8.80f },
	{  24, 4, 'S',  13, 0x0009, 0x0000, 0x20200000u,    51.950f,    -2.500f,   -7.98f },   // ゲート壁 A+B型
	{  24, 4, 'S',  14, 0x0009, 0x0000, 0x20200000u,    51.950f,    -4.900f,   -7.97f },   // ゲート壁 A+B型
	{  25, 4, 'S',   7, 0x0000, 0x0000, 0x20880000u,   -34.500f,   -10.100f,    8.45f },   // シャッター#7位置 (手動 11/10、上の階の床)
	{  26, 4, 'S',   8, 0x0008, 0x0000, 0x20100000u,   -19.000f,   -18.000f,   -8.00f },   // ゲート壁 A+B型
	{  27, 4, 'D',   6, 0x0000, 0x0001, 0x00003000u,    -5.000f,   -33.500f,  -13.60f },
	{  27, 4, 'D',  13, 0x0001, 0x0010, 0x00001040u,   -62.150f,   -69.600f,   -8.00f },
	{  27, 4, 'D',  14, 0x0001, 0x0010, 0x00001040u,   -62.150f,   -70.900f,   -8.00f },
	{  29, 5, 'D',   5, 0x0002, 0x0020, 0x00101000u,    78.494f,    15.090f,   -6.00f },
	{  30, 5, 'D',   1, 0x0004, 0x0000, 0x00101000u,    19.800f,   -25.000f,   -0.00f },   // 11/11: done was 0x0010, but 0x10 comes on with the gate (arflg 07 -> 50) -> D#1 was 'done' at once
	{  30, 5, 'D',  42, 0x0004, 0x0020, 0x00101000u,    16.200f,     0.000f,    5.00f },
	{  31, 5, 'D',  44, 0x000A, 0x0010, 0x00101000u,   -55.200f,   -29.750f,    0.00f },
	{  31, 5, 'D',  45, 0x000A, 0x0010, 0x00101000u,   -56.700f,   -29.750f,    0.00f },
	{  32, 4, 'S',  18, 0x0009, 0x0000, 0x20200000u,    72.460f,    -2.500f,    0.01f },   // ゲート壁 A+B型
	{  32, 4, 'S',  20, 0x0009, 0x0000, 0x20200000u,    72.460f,    -4.900f,    0.02f },   // ゲート壁 A+B型
	{  33, 4, 'D',   1, 0x0001, 0x0010, 0x00101000u,     7.500f,     5.250f,   -8.80f },
	{  33, 4, 'D',   2, 0x0001, 0x0010, 0x00101000u,     7.500f,     6.750f,   -8.80f },
	{  34, 4, 'S',  17, 0x0009, 0x0000, 0x20200000u,    10.250f,    -1.200f,    0.03f },   // ゲート壁 A+B型
	{  34, 4, 'S',  19, 0x0009, 0x0000, 0x20200000u,    10.250f,     1.200f,    0.03f },   // ゲート壁 A+B型
	{  35, 4, 'D',   3, 0x0009, 0x0010, 0x00101000u,   -30.500f,     7.000f,    7.95f },
	{  35, 4, 'D',   4, 0x0009, 0x0010, 0x00101000u,   -30.500f,     7.000f,    7.95f },
	{  37, 4, 'S',  29, 0x0009, 0x0000, 0x20200000u,   -52.300f,   -35.500f,    0.03f },   // ゲート壁 A+B型
	{  37, 4, 'S',  31, 0x0009, 0x0000, 0x20200000u,   -52.300f,   -31.500f,    0.03f },   // ゲート壁 A+B型
	{  39, 4, 'S',   8, 0x0009, 0x0000, 0x20100000u,   -19.000f,   -18.000f,   -8.00f },   // ゲート壁 A+B型
	{  39, 4, 'S',  12, 0x0009, 0x0000, 0x20A00000u,   -29.750f,    -3.600f,   -8.00f },   // ゲート壁 A+B型
	{  40, 4, 'D',  13, 0x0001, 0x0010, 0x00001040u,   -62.150f,   -69.600f,   -8.00f },
	{  40, 4, 'D',  14, 0x0001, 0x0010, 0x00001040u,   -62.150f,   -70.900f,   -8.00f },
	{  42, 4, 'D',  15, 0x0001, 0x0010, 0x00103000u,   -48.000f,   -18.800f,    1.40f },   // 11/10: at Door#15's own position (was -54.5, -17.5)
	{  43, 4, 'S',   0, 0x0009, 0x0000, 0x20900000u,   -58.210f,   -56.890f,    8.00f },   // ゲート壁 A+B型
	{  44, 4, 'S',  12, 0x0009, 0x0000, 0x20A00000u,   -29.750f,    -3.600f,   -8.00f },   // ゲート壁 A+B型
	{  44, 4, 'D',   5, 0x0009, 0x0000, 0x00003000u,    -5.000f,    -9.500f,   -8.20f },   // 11/11: done was 0x0008, but that bit comes on with the shine -> D#5 was 'done' at once
	{  46, 5, 'D',  43, 0x0000, 0x0010, 0x00101000u,     8.050f,    32.650f,    0.00f },
	{  46, 5, 'D',  27, 0x0004, 0x0020, 0x00101000u,   -42.850f,    29.100f,   10.50f },
	{  46, 5, 'D',  28, 0x0004, 0x0020, 0x00101000u,   -41.750f,    29.100f,   10.50f },
	{  49, 5, 'D',  72, 0x0001, 0x0020, 0x00101000u,   -15.400f,    -0.000f,   -1.00f },
	{  50, 5, 'S',  41, 0x0000, 0x0000, 0x20400000u,   -9.750f,  -21.570f,    3.50f },   // 11/09: picked by hand (Shutter#41's own position)
	{  50, 5, 'D',  74, 0x0002, 0x0020, 0x00101000u,   -12.725f,     0.005f,    4.00f },
	{  51, 5, 'S',  36, 0x0000, 0x0000, 0x20C00000u,   -52.750f,     0.000f,    0.00f },   // 開けるポイント（S#36の位置・指定）
	// 11/10: area 74's gate depends on the way in, and arflg can't tell (1C01 on both ways until the gate breaks:
	// then 1C50 + Shutter#20 alive = out to 77, or 1C60 + Shutter#21 alive = out to 76). From 91 (01's log, the 1-2-5-9
	// way through Diesel) the lit gate leads to 77; from 93 (02's log, 3-12-9) the one to 76. The area we came from
	// is put into arflg for the table as 0x4000 (from 91) / 0x8000 (from 93) - see the call (both if unknown).
	// done 0x0040 = the gate broke.
	{  74, 6, 'S',  20, 0x4001, 0x0040, 0x20A10000u,   117.250f,     5.800f,  -29.95f },   // ゲート壁 B型 (+z, to 77; came from 91)
	{  74, 6, 'S',  21, 0x8001, 0x0040, 0x20A10000u,   117.250f,    -5.800f,  -29.20f },   // ゲート壁 B型 (-z, to 76; came from 93)
	{  75, 7, 'D',   1, 0x0200, 0x0010, 0x00101000u,    40.450f,    50.000f,  -15.20f },
	{  75, 7, 'S',   0, 0x0009, 0x0000, 0x20A00000u,    17.640f,    34.970f,   -0.73f },   // ゲート壁 A+B型
	{  76, 6, 'S',  47, 0x0200, 0x0000, 0x20100000u,    74.000f,   -19.000f,  -25.00f },   // ゲート壁 B型
	{  76, 6, 'D',   1, 0x0001, 0x0020, 0x00101000u,   -66.500f,   -17.050f,  -21.00f },
	{  77, 6, 'S',  15, 0x0001, 0x0000, 0x20A00000u,   -36.500f,    21.100f,  -21.00f },   // ゲート壁 B型
	{  78, 6, 'S',  46, 0x0200, 0x0010, 0x20200000u,    46.500f,    26.300f,  -11.53f },   // ゲート壁 B型
	{  78, 6, 'S',  23, 0x0001, 0x0000, 0x20A00000u,    84.500f,    22.300f,  -16.95f },   // ゲート壁 B型
	{  79, 6, 'S',  19, 0x0000, 0x0001, 0x20210000u,   -88.700f,    21.250f,    0.05f },   // 11/10: S#19 (A+B) - broken once to reach the boss; at Shutter#19's own position. done 0x0001 = boss beaten: ignored from then on
	{  79, 6, 'S',  29, 0x0001, 0x0000, 0x20100000u,  -109.100f,   -10.250f,    0.00f },   // シャッター#29位置 (手動)
	{  80, 6, 'D',   2, 0x0001, 0x0010, 0x00101000u,    98.500f,   -17.400f,  -12.50f },
	{  86, 7, 'D',  13, 0x0009, 0x0010, 0x00101000u,   -18.005f,    -5.295f,  -15.50f },
};
// The exit the script has lit by now in this area: the latest row (script order) whose cond bits are all in
// arflg and whose group (same cond) isn't done yet - a group is done once any member's done bits are set or
// (doors) a player has gone through it. Parts
// of one exit (double doors, multi-part shutters) are averaged, separate exits of one group (branches) are split
// by node. *shutter: it must be broken.
// A table door a player has already gone through: the door work entry (*(0x10411C) + 0x7EC + id*0x18) gets
// 0x04000000 when a player passes (the bit the scripts' "01 0C n 04000000" waits on) or 0x00000400 held open.
// Some scripts set no arflg bit afterwards, so this is what tells the bots that door is done.
// The door work is per stage, so a bit left from an earlier area doesn't count: only a door whose bit was clear
// when this area began (snapshot on area change) and is set now.
static bool g_doorSetAtEntry[129];
static void SpikeoutDoorPassedTick(const UINT8 *ram, unsigned area)
{
	static unsigned lastArea = ~0u;
	if (area == lastArea) return;
	lastArea = area;
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	unsigned cnt = (dw >= 0x10000 && dw < 0x700000) ? *(const UINT16 *) &ram[(dw + 0x800) ^ 2] : 0;
	for (unsigned id = 0; id <= 128; id++)
		g_doorSetAtEntry[id] = id >= 1 && id <= cnt && (*(const UINT32 *) &ram[dw + 0x7EC + id * 0x18] & (0x04000000u | 0x00000400u)) != 0;
}
static bool SpikeoutDoorPassed(const UINT8 *ram, unsigned id)
{
	if (!ram) return false;
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw < 0x10000 || dw >= 0x700000 || id == 0 || id > 128 || g_doorSetAtEntry[id]) return false;
	unsigned cnt = *(const UINT16 *) &ram[(dw + 0x800) ^ 2];
	if (id > cnt) return false;
	UINT32 f = *(const UINT32 *) &ram[dw + 0x7EC + id * 0x18];
	// 11/18: area 6 Door#16 is broken, not gone through: 0x04000000 blinks on it whenever it is bumped (00409000 <->
	// 04409000 before the gate opens) and stays on once broken - neither is "passed". Its pass is the landing spot
	// (passTune) after it is broken (SpikeoutA6D16Broken).
	if (id == 16 && ram[0x10402D ^ 3] == 6) return false;
	return (f & (0x04000000u | 0x00000400u)) != 0;
}
// 11/18 (user, 01-08 gatelog 19:24): area 6 Door#16 opened = broken. Gate open: arflg +0x0008 and the shine (00409000 ->
// 01409000); 38 frames of kicks later 01409000 -> 04408000 (shine off, 0x00001000 off, 0x04000000 on), next frame
// 04000000 (0x00400000 / 0x00008000 off) and the gate wall #1227 off; it stays 04000000. Same as area 29 Door#5.
// Broken = 0x04000000 on with 0x00001000 off (the bump blinks keep 0x1000).
// 11/18 (user, log 20:38): area 40 D#13/14 broken = 0x00001000 off on either (00401040 -> 01401040 shine -> 04400040
// -> 00400040 / D#14 00400040; before the gate opens both have 0x1000).
static bool SpikeoutA40Broken(const UINT8 *ram)
{
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw < 0x10000 || dw >= 0x700000 || *(const UINT16 *) &ram[(dw + 0x800) ^ 2] < 14) return false;
	UINT32 f13 = *(const UINT32 *) &ram[dw + 0x7EC + 13 * 0x18], f14 = *(const UINT32 *) &ram[dw + 0x7EC + 14 * 0x18];
	return !(f13 & 0x00001000u) || !(f14 & 0x00001000u);
}
// 11/18 (user): holding a thing (weapon / chair ...): an area object whose owner (+0x57) is our slot with the held flag
// 0x00020000 (the same test as the weapon seek). 11/25: back for the combos 20 / 21.
static bool SpikeoutHolding(const UINT8 *ram, UINT32 rob)
{
	UINT32 mgr = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (mgr < 0x100000 || mgr >= 0x7FF000) return false;
	int cnt = *(const UINT16 *) &ram[(mgr + 0x10) ^ 2]; if (cnt > 0x106) cnt = 0x106;
	const unsigned my = (rob - 0x1A0000) / 0x4000;
	for (int i = 0; i < cnt; i++)
	{
		UINT32 o = 0x1F4000 + (UINT32) i * 0xBC;
		if (ram[(o + 0x57) ^ 3] == my && (*(const UINT32 *) &ram[o] & 0x00020000u)) return true;
	}
	return false;
}
static bool SpikeoutA6D16Broken(const UINT8 *ram)
{
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw < 0x10000 || dw >= 0x700000 || *(const UINT16 *) &ram[(dw + 0x800) ^ 2] < 16) return false;
	UINT32 f = *(const UINT32 *) &ram[dw + 0x7EC + 16 * 0x18];
	return (f & 0x04000000u) && !(f & 0x00001000u);
}
// Hand-tuned per-area fixes (waypoint-measured gate positions, the area 51 jump spot, the area 75 start room).
// Off for now (11/08) to see how the floor-grid nav, step jumps and the stall kick cope on their own.
static const bool SPIKEOUT_HAND_TUNED = false;
// 11/13: only the area 75 start room part of the hand-tuned fixes is back on: leave your own start room by its door
// first, also when an enemy is targeted - unless that enemy is in the same room.
static const bool AI_A75_ROOM_EXIT = true;
// 11/13: a swing door between us and the goal within 2.5 always gets the door pass (SpikeoutSwingDoorAhead).
// Off (11/13, user): the bots only got stuck at swing doors in area 75's start rooms, which AI_A75_ROOM_EXIT covers.
static const bool AI_SWING_DOOR_PASS = false;
struct SpikeoutExitTune { UINT8 area; char kind; UINT8 id; float x, z; };
static const SpikeoutExitTune s_spikeoutExitTune[] =
{
	{ 26, 'S',  8, -18.627f, -18.891f },   // from 01's waypoints (11/07)
	{ 44, 'S', 12, -28.714f,  -3.099f },
	{ 77, 'S', 15, -37.329f,  21.600f },
	{ 78, 'S', 46,  45.911f,  25.368f },
	{ 78, 'S', 23,  85.364f,  24.870f },
};
// Bot filter (11/09): a gate found in game memory counts only if a row of the filtered exit table for this area
// is within r of it - so the memory scan no longer sends bots to shutters / doors the bots are not to open.
static bool SpikeoutNearFilteredExit(unsigned area, float x, float z, float r)
{
	for (const SpikeoutExit &e : s_spikeoutExits)
		if (e.area == area && (e.x - x) * (e.x - x) + (e.z - z) * (e.z - z) < r * r) return true;
	return false;
}
static void SpikeoutExitPos(const SpikeoutExit &e, float *x, float *z)
{
	*x = e.x; *z = e.z;
	if (!SPIKEOUT_HAND_TUNED) return;
	for (const SpikeoutExitTune &t : s_spikeoutExitTune)
		if (t.area == e.area && t.kind == e.kind && t.id == e.id) { *x = t.x; *z = t.z; return; }
}
static unsigned g_spikeoutPrevArea = 0;   // the area before this one (11/10)
// Area 15 hard-coded (11/10): once the plain door D#1 (front Mall) has been opened, D#11/#12 (back Mall) are ignored
// for the rest of the visit. Opened = gone through (SpikeoutDoorPassed) or its shine seen and then gone (hit).
static bool g_a15D1Open = false, g_a15D1Lit = false;
// 11/11: the exit the table picked is a lit one (door shine / shutter shine). An unlit pick (arflg bits only) is
// taken only once no enemy has been around for 1s, like cond 0 - a boss dying in a multi-boss room sets arflg bits
// before the gate is to be broken.
static bool g_teLit = false;
static bool SpikeoutTableExit(const UINT8 *ram, unsigned area, UINT32 arflg, unsigned node, float *gx, float *gz, bool *shutter, int *condOut, int *doorOut = NULL)
{
	if (doorOut) *doorOut = 0;
	const int N = (int) (sizeof(s_spikeoutExits) / sizeof(s_spikeoutExits[0]));
	// 11/18 (user, log 20:38): area 40 D#13/14 - arflg 0x10 (the rows' "done") comes the frame the door is BROKEN
	// (with 0x40; 0x1000 off, a one-frame 0x04000000) - not when it is gone through. The bots took it as passed, stood
	// (mode 0) and slid back down the slope; nobody went through. These two rows are never done in the area: the area
	// change ends them. Broken / not: SpikeoutA40Broken (the gate kick until then, the way through after).
	auto rowDone = [&](const SpikeoutExit &x) { if (x.area == 40 && x.kind == 'D' && (x.id == 13 || x.id == 14)) return false;
		return (x.done && (arflg & x.done)) || (x.kind == 'D' && SpikeoutDoorPassed(ram, x.id)) ||
		(x.area == 15 && x.kind == 'D' && (x.id == 11 || x.id == 12) && g_a15D1Open) ||
		(x.area == 15 && x.kind == 'D' && x.id == 1 && (arflg & 0x0800u)); };   // 11/11: D#11/#12 opened (0x0800) -> D#1 (still lit) ignored
	// The game's own exit arrow decides between exits (11/09): which of two gates is the way on depends on the route
	// taken to the area (area 74: Shutter#20 or #21), not on chance. The game marks the real one with its arrow -
	// door work 0x00800000 (cmd 10; a pulse of a frame or so) / shutter work 0x00020000 (cmd 12). Marks are kept
	// for the visit to the area; a row whose door / shutter got the arrow wins over rows that did not.
	// Shutters also get the arrow when enemies come out of them (area 79: Shutter#19 lit early, the real exit #29 only
	// at gate open), so the LATEST arrow counts: each door / shutter keeps the frame its arrow was last seen rising,
	// and only the rows marked within 30 frames of the newest mark among the open rows are kept.
	// A shutter that is turned off again (its flags back to 0 - area 74 Shutter#21, area 79 Shutter#19, a frame to
	// ~100 frames after their arrow) is not the way on: only shutters still live (0x00060000 arrow / attackable) count.
	static unsigned mkArea = ~0u; static UINT32 dMk[130], sMk[100], sCur[100], dCur[130]; static bool dOn[130], sOn[100]; static bool anyMk = false;
	if (area != mkArea) { mkArea = area; memset(dMk, 0, sizeof(dMk)); memset(sMk, 0, sizeof(sMk)); memset(dOn, 0, sizeof(dOn)); memset(sOn, 0, sizeof(sOn)); memset(sCur, 0, sizeof(sCur)); memset(dCur, 0, sizeof(dCur)); anyMk = false; g_a15D1Open = g_a15D1Lit = false; }
	{
		UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
		if (dw >= 0x10000 && dw < 0x700000)
		{
			unsigned cnt = *(const UINT16 *) &ram[(dw + 0x800) ^ 2]; if (cnt > 128) cnt = 0;
			for (unsigned i = 1; i <= cnt; i++)
			{
				dCur[i] = *(const UINT32 *) &ram[dw + 0x7EC + i * 0x18];
				bool on = (dCur[i] & 0x00800000) != 0;
				if (on && !dOn[i]) { dMk[i] = g_ppcTraceFrame ? g_ppcTraceFrame : 1; anyMk = true; }
				dOn[i] = on;
			}
		}
		UINT32 sw = *(const UINT32 *) &ram[0x104514] & 0x7FFFFF;
		if (sw >= 0x10000 && sw < 0x700000)
		{
			unsigned cnt = ram[(sw + 0x10) ^ 3]; if (cnt > 96) cnt = 0;
			for (unsigned i = 0; i < cnt; i++)
			{
				sCur[i] = *(const UINT32 *) &ram[sw + 0x18 + i * 0x1C];
				bool on = (sCur[i] & 0x00020000) != 0;
				if (on && !sOn[i]) { sMk[i] = g_ppcTraceFrame ? g_ppcTraceFrame : 1; anyMk = true; }
				sOn[i] = on;
			}
		}
	}
	if (area == 15 && !g_a15D1Open)
	{
		if (dCur[1] & 0x01000000u) g_a15D1Lit = true;
		if (SpikeoutDoorPassed(ram, 1) || (g_a15D1Lit && !(dCur[1] & 0x01000000u)))
		{
			g_a15D1Open = true;
			SpikeoutGateNote(g_ppcTraceFrame, "[GATE] node %u: area 15 D#1 opened -> D#11/#12 ignored (hard-coded)\n", node);
		}
	}
	auto mkF = [&](const SpikeoutExit &x) -> UINT32
	{
		if (x.kind == 'D') return x.id < 130 ? dMk[x.id] : 0;
		return (x.id < 100 && (sCur[x.id] & 0x00060000)) ? sMk[x.id] : 0;   // shutter: only while still live
	};
	UINT32 newest = 0;
	if (anyMk)
		for (int i = 0; i < N; i++)
		{
			const SpikeoutExit &e = s_spikeoutExits[i];
			if (e.area == area && (arflg & e.cond) == e.cond && !rowDone(e) && mkF(e) > newest) newest = mkF(e);
		}
	bool useMk = newest != 0;
	auto marked = [&](const SpikeoutExit &x) { UINT32 f = mkF(x); return f && f + 30 >= newest; };
	// The lit exit (11/10, from the ROM's debug names for the door status word, bit 0 = MSB): a door to open now
	// shines - door_shine 0x01000000, set when its flags come up, gone the moment it is hit (seen in areas 75, 86,
	// 4, 6, 1, 2, 5; area 6 lights all four of D#5/6/11/12 = the free choice); a shutter gate that is the way on gets
	// 0x02000000 when the gate comes up (area 74 from 91: only Shutter#20; area 75: Shutter#0/1), 0x00040000 once
	// broken. Every area but a stage's last has one. When any exit-table row of this area is lit, only the lit rows
	// count, whatever their flag condition (this is what picks area 74's gate by the way in); else the table as before.
	auto lit = [&](const SpikeoutExit &x) -> bool
	{
		if (x.kind == 'D') return x.id < 130 && (dCur[x.id] & 0x01000000u);
		return x.id < 100 && (sCur[x.id] & 0x02000000u) && !(sCur[x.id] & 0x00040000u);
	};
	bool anyLit = false;
	g_teLit = false;
	for (int i = 0; i < N; i++)
		if (s_spikeoutExits[i].area == area && lit(s_spikeoutExits[i]) && !rowDone(s_spikeoutExits[i])) anyLit = true;
	int cond = -1;
	for (int i = N - 1; i >= 0 && cond < 0; i--)
	{
		const SpikeoutExit &e = s_spikeoutExits[i];
		if (e.area != area) continue;
		if (anyLit) { if (lit(e) && !rowDone(e)) cond = e.cond; continue; }
		if ((arflg & e.cond) != e.cond) continue;
		if (useMk && !marked(e)) continue;
		bool doneGrp = false;
		for (int j = 0; j < N; j++)
			if (s_spikeoutExits[j].area == area && s_spikeoutExits[j].cond == e.cond && rowDone(s_spikeoutExits[j])) doneGrp = true;
		if (!doneGrp) cond = e.cond;
	}
	if (cond < 0) return false;
	struct { float sx, sz; int n; bool sh; int door; int sid; } cl[8]; int ncl = 0;
	for (int i = 0; i < N; i++)
	{
		const SpikeoutExit &e = s_spikeoutExits[i];
		if (e.area != area || rowDone(e)) continue;
		if (anyLit) { if (!lit(e)) continue; g_teLit = true; }
		else
		{
			if ((int) e.cond != cond) continue;
			if (useMk && !marked(e)) continue;
		}
		float exx, ezz; SpikeoutExitPos(e, &exx, &ezz);
		int c = 0;
		for (; c < ncl; c++) { float dx = cl[c].sx / cl[c].n - exx, dz = cl[c].sz / cl[c].n - ezz; if (dx * dx + dz * dz < 64.0f) break; }
		if (c == ncl) { if (ncl == 8) continue; cl[ncl].sx = 0; cl[ncl].sz = 0; cl[ncl].n = 0; cl[ncl].sh = false; cl[ncl].door = 0; cl[ncl].sid = -1; ncl++; }
		cl[c].sx += exx; cl[c].sz += ezz; cl[c].n++; if (e.kind == 'S') { cl[c].sh = true; if (cl[c].sid < 0) cl[c].sid = e.id; } else if (!cl[c].door) cl[c].door = e.id;
	}
	if (!ncl) return false;
	int pick = 0;   // 11/11: same on every cabinet (was (area + node) % ncl)
	// AIRoute (11/10): Diesel Town's branches by door. Area 6: D#11/12 -> 1 (on to the front Shipyard) or D#5/6 -> 3;
	// area 12: D#43 -> 9 (back Shipyard) or D#7/8 -> 14; area 15: D#1 (front Mall) or D#11/12 (back Mall).
	int forced = -1;
	if (g_spikeoutAIRoute >= 0 && ncl >= 2)
	{
		int want = -1;
		if (area == 6) want = g_spikeoutAIRoute == 2 ? 11 : 5;
		else if (area == 12) want = g_spikeoutAIRoute == 3 ? 43 : 7;
		else if (area == 15) want = g_spikeoutAIRoute == 0 ? 1 : 11;
		for (int c = 0; c < ncl && want >= 0; c++)
			if (cl[c].door == want || cl[c].door == want + 1) { forced = c; break; }
		if (forced >= 0) pick = forced;
	}
	// AIRoute2: the Mall map of area 39 (also used under other area numbers): the gate at (-19,-18) (S#8, on to 19)
	// = 0 or the A+B gate wall at (-29.75,-3.6) (S#12, on to 44) = 1. Matched by position, any area, when both are open.
	if (g_spikeoutAIRoute2 >= 0 && ncl >= 2)
	{
		int c0 = -1, c1 = -1;
		for (int c = 0; c < ncl; c++)
		{
			float cx = cl[c].sx / cl[c].n, cz = cl[c].sz / cl[c].n;
			if ((cx + 19.0f) * (cx + 19.0f) + (cz + 18.0f) * (cz + 18.0f) < 9.0f) c0 = c;
			if ((cx + 29.75f) * (cx + 29.75f) + (cz + 3.6f) * (cz + 3.6f) < 9.0f) c1 = c;
		}
		if (c0 >= 0 && c1 >= 0) { forced = pick = g_spikeoutAIRoute2 == 1 ? c1 : c0; }
		// Area 44 (same map): the A+B gate wall S#12 = 1, or the heavy door D#5 at (-5,-9.5) (hidden route) = 0.
		else if (c1 >= 0)
		{
			int c5 = -1;
			for (int c = 0; c < ncl; c++)
			{
				float cx = cl[c].sx / cl[c].n, cz = cl[c].sz / cl[c].n;
				if ((cx + 5.0f) * (cx + 5.0f) + (cz + 9.5f) * (cz + 9.5f) < 9.0f) c5 = c;
			}
			if (c5 >= 0) { forced = pick = g_spikeoutAIRoute2 == 1 ? c1 : c5; }
		}
	}
	if (g_spikeoutAIRoute3 >= 0 && ncl >= 2 && area == 30)   // AIRoute3 (11/11): area 30 Door#1 (0) or Door#42 (1)
	{
		int want = g_spikeoutAIRoute3 == 1 ? 42 : 1;
		for (int c = 0; c < ncl; c++)
			if (cl[c].door == want) { forced = pick = c; break; }
	}
	// 11/11: no more ../exitpick.txt (02 wrote its pick, the others read it). Every branch is set by AIRoute/2/3 in
	// each cabinet's own ini; an unset one takes the first exit in table order, the same on every cabinet.
	if (ncl >= 2 && forced < 0) pick = 0;
	*gx = cl[pick].sx / cl[pick].n; *gz = cl[pick].sz / cl[pick].n; *shutter = cl[pick].sh; *condOut = cond;
	if (doorOut && !cl[pick].sh) *doorOut = cl[pick].door;
	return true;
}

// Exit gate read from game memory (RE notes, 2026-11-07):
//  Shutters (breakable gates): work = *(0x104514); count u8 at +0x10; ROM defs at *(+0x14), 0x34 bytes each;
//    runtime entries at +0x18, 0x1C bytes each:
//    +0x00 flags (0x00020000 = current NEXT/EXIT shutter, set by area script cmd 12), +0x0C/+0x10/+0x14 x/y/z.
//    If x/z are 0 (e.g. Stage 5 S#41, Stage 6 S#29/#47), fallback to the midpoint of its wall segment at
//    *(*(0x106168)+0x10) + wall_id*4 (wall_id = ROM def +0x2B).
//  Doors: work = *(0x10411C); count u16 at +0x800; Door#i (1-based) at +0x7EC + i*0x18:
//    +0x00 flags (0x00800000 = current NEXT/EXIT door, set by area script cmd 10), +0x04 ROM def (0x34 bytes):
//    +0x06 base angle ay (s16), +0x08/+0x0C/+0x10 hinge x/y/z, +0x14 width, +0x2D dtype.
//    For swing doors (dtype 0,1,2,5), the doorway center is offset from the hinge by -0.5*width*(sin(ay),cos(ay)),
//    or taken straight from its live wall segment endpoints (+0x0C/+0x14 and +0x18/+0x20).
// Returns the doorway/shutter center (*gx,*gz) and the push/through target (*wx,*wz).
static const UINT8 *g_spikeoutCrom = NULL;   // set by CModel3 before SpikeoutAIInputBits
static const UINT8 *g_spikeoutCromBank = NULL;   // 0xFF000000-0xFF7FFFFF window (map data, door defs may live here)
static const UINT8 *g_spikeoutRam = NULL;
static float g_aiLogTx = 0, g_aiLogTz = 0;   // for ai.txt: where the bot steers this frame
static UINT32 g_aiLogNavKey = 0;            // for ai.txt: section key (area << 8 | camar)
static int g_aiLogMemGate = 0;               // for ai.txt: 0 none, 1 shutter, 2 door, 3 table fallback
static float g_aiLogMgX = 0.0f, g_aiLogMgZ = 0.0f;
// Pointer into the PowerPC map -> host bytes (32-bit word native layout), or NULL.
static const UINT8 *SpikeoutMapPtr(UINT32 ptr, UINT32 len)
{
	if (ptr & 3) return NULL;
	if (ptr >= 0xFF800000u) { UINT32 o = ptr - 0xFF800000u; return (g_spikeoutCrom && o + len <= 0x800000u) ? g_spikeoutCrom + o : NULL; }
	if (ptr >= 0xFF000000u) { UINT32 o = ptr & 0x7FFFFF; return (g_spikeoutCromBank && o + len <= 0x800000u) ? g_spikeoutCromBank + o : NULL; }
	if (ptr < 0x800000u) return (g_spikeoutRam && ptr + len <= 0x800000u) ? g_spikeoutRam + ptr : NULL;
	return NULL;
}
// Read a wall segment's midpoint from the live wall table *(*(0x106168)+0x10) + wall_id*4.
// *(0x106168) = 0x480000 (header copied from 0xA0AD4 + stage*0x20 by 0x050154). +0x08 = full wall list (used by the
// player collision 0x050CBC); +0x10 = door/shutter wall list indexed by the ROM def's wall_id (door code 0x03E08C,
// shutters 0x070A48). Both lists end in 0xFFFFFFFF; +0x10 entries are a subset of +0x08.
static bool SpikeoutWallCenter(const UINT8 *ram, unsigned wallId, float *cx, float *cz)
{
	if (wallId >= 0xFE) return false;
	UINT32 wh = *(const UINT32 *) &ram[0x106168] & 0x7FFFFF;
	if (wh < 0x10000 || wh + 0x18 > 0x800000) return false;
	UINT32 wt = *(const UINT32 *) &ram[wh + 0x10] & 0x7FFFFF;
	if (wt < 0x10000 || wt + (wallId + 1) * 4 > 0x800000) return false;
	UINT32 wp = *(const UINT32 *) &ram[wt + wallId * 4] & 0x7FFFFF;
	if (wp < 0x10000 || wp + 0x28 > 0x800000) return false;
	float x0 = *(const float *) &ram[wp + 0x0C], z0 = *(const float *) &ram[wp + 0x14];
	float x1 = *(const float *) &ram[wp + 0x18], z1 = *(const float *) &ram[wp + 0x20];
	if (!(x0 == x0) || !(z0 == z0) || !(x1 == x1) || !(z1 == z1)) return false;
	if (fabsf(x0) >= 1000.0f || fabsf(z0) >= 1000.0f || (x0 == 0.0f && z0 == 0.0f && x1 == 0.0f && z1 == 0.0f)) return false;
	*cx = 0.5f * (x0 + x1); *cz = 0.5f * (z0 + z1);
	return true;
}
// Compute a door's doorway center (cx,cz) and leaf vector (lx,lz) from its ROM definition.
static bool SpikeoutDoorGeom(const UINT8 *ram, UINT32 dp, float *cx, float *cz, float *hx, float *hz, float *lx, float *lz)
{
	const UINT8 *d = SpikeoutMapPtr(dp, 0x34);
	if (!d) return false;
	float x = *(const float *) &d[0x08], z = *(const float *) &d[0x10], w = *(const float *) &d[0x14];
	if (!(x == x) || !(z == z) || !(w == w) || fabsf(x) >= 1000.0f || fabsf(z) >= 1000.0f) return false;
	short ay = (short) *(const UINT16 *) &d[0x06 ^ 2];
	unsigned dtype = d[0x2D ^ 3], wallId = d[0x2C ^ 3];
	float ang = (float) ay * (3.14159265f / 32768.0f);
	float s = sinf(ang), c = cosf(ang);
	*hx = x; *hz = z; *lx = -s; *lz = -c;
	if (dtype <= 2 || dtype == 5) { *cx = x - 0.5f * w * s; *cz = z - 0.5f * w * c; }
	else { *cx = x; *cz = z; }
	if (*cx == 0.0f && *cz == 0.0f && SpikeoutWallCenter(ram, wallId, cx, cz)) { *hx = *cx; *hz = *cz; }
	return (*cx != 0.0f || *cz != 0.0f);
}
// Unit normal (nx,nz) of table door #id, from its ROM definition (perpendicular to the leaf). False if unreadable.
static bool SpikeoutDoorNormal(const UINT8 *ram, unsigned id, float *nx, float *nz)
{
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw < 0x10000 || dw >= 0x700000 || id == 0 || id > 128) return false;
	if (id > *(const UINT16 *) &ram[(dw + 0x800) ^ 2]) return false;
	float cx, cz, hx, hz, lx, lz;
	if (!SpikeoutDoorGeom(ram, *(const UINT32 *) &ram[dw + 0x7EC + id * 0x18 + 4], &cx, &cz, &hx, &hz, &lx, &lz)) return false;
	float l = sqrtf(lx * lx + lz * lz);
	if (!(l > 0.01f)) return false;
	*nx = -lz / l; *nz = lx / l;
	return true;
}
// Width of table door #id (ROM def +0x14), 0 if unreadable.
static float SpikeoutDoorWidth(const UINT8 *ram, unsigned id)
{
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw < 0x10000 || dw >= 0x700000 || id == 0 || id > 128) return 0.0f;
	if (id > *(const UINT16 *) &ram[(dw + 0x800) ^ 2]) return 0.0f;
	const UINT8 *d = SpikeoutMapPtr(*(const UINT32 *) &ram[dw + 0x7EC + id * 0x18 + 4], 0x34);
	if (!d) return 0.0f;
	float w = fabsf(*(const float *) &d[0x14]);
	return (w == w && w < 20.0f) ? w : 0.0f;
}
static bool SpikeoutMemGate(const UINT8 *ram, unsigned node, unsigned area, bool flagUp, float px, float pz, float *gx, float *gz, float *wx, float *wz, int *kind)
{
	float sx = 0.0f, sz = 0.0f; int n = 0; *kind = 0;
	UINT32 sw = *(const UINT32 *) &ram[0x104514] & 0x7FFFFF;
	if (sw >= 0x10000 && sw < 0x700000)
	{
		unsigned cnt = ram[(sw + 0x10) ^ 3];
		UINT32 sdefBase = *(const UINT32 *) &ram[sw + 0x14];
		if (cnt > 96) cnt = 0;
		for (unsigned i = 0; i < cnt; i++)
		{
			UINT32 e = sw + 0x18 + i * 0x1C;
			UINT32 flg = *(const UINT32 *) &ram[e];
			if (SpikeoutJunkShutter(ram[0x10402C ^ 3], i)) continue;
			if ((flg & 0x00020000) && !(flg & 0x08000000))   // arrow lit (cmd 12) and not yet broken
			{
				float x = *(const float *) &ram[e + 0x0C], z = *(const float *) &ram[e + 0x14];
				if (x == 0.0f && z == 0.0f)
				{
					const UINT8 *sd = SpikeoutMapPtr(sdefBase + i * 0x34, 0x34);
					if (sd) SpikeoutWallCenter(ram, sd[0x2B ^ 3], &x, &z);
				}
				if (x == x && z == z && fabsf(x) < 1000.0f && fabsf(z) < 1000.0f && (x != 0.0f || z != 0.0f)) { sx += x; sz += z; n++; }
			}
		}
		if (n) { *gx = sx / n; *gz = sz / n; *wx = *gx; *wz = *gz; *kind = 1; return true; }
	}
	// Scan doors for arrow lit (0x00800000, cmd 10). Double doors light two entries next to each other (<= 8 units):
	// average their centers into the middle of the double doorway. Branching areas light doors at separate exits:
	// group lit doors into clusters within 8 units and pick one cluster per (node, area).
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw >= 0x10000 && dw < 0x700000)
	{
		unsigned cnt = *(const UINT16 *) &ram[(dw + 0x800) ^ 2];
		if (cnt > 128) cnt = 0;
		struct { float sx, sz, hsx, hsz, lx, lz; int n; } cl[8]; int ncl = 0;
		for (unsigned i = 1; i <= cnt; i++)
		{
			UINT32 e = dw + 0x7EC + i * 0x18;
			UINT32 flg = *(const UINT32 *) &ram[e];
			if (!(flg & 0x40000000) || !(flg & 0x00800000)) continue;   // placed (cmd 7) and arrow lit (cmd 10)
			float cx, cz, hx, hz, lx, lz;
			if (!SpikeoutDoorGeom(ram, *(const UINT32 *) &ram[e + 4], &cx, &cz, &hx, &hz, &lx, &lz)) continue;
			int foundCl = -1;
			for (int c = 0; c < ncl; c++)
			{
				float dx = cl[c].sx / cl[c].n - cx, dz = cl[c].sz / cl[c].n - cz;
				if (dx * dx + dz * dz < 64.0f) { foundCl = c; break; }
			}
			if (foundCl >= 0) { cl[foundCl].sx += cx; cl[foundCl].sz += cz; cl[foundCl].hsx += hx; cl[foundCl].hsz += hz; cl[foundCl].n++; }
			else if (ncl < 8) { cl[ncl].sx = cx; cl[ncl].sz = cz; cl[ncl].hsx = hx; cl[ncl].hsz = hz; cl[ncl].lx = lx; cl[ncl].lz = lz; cl[ncl].n = 1; ncl++; }
		}
		if (ncl > 0)
		{
			int pick = 0;   // 11/11: same door on every cabinet (was (area + node) % ncl)
			float cx = cl[pick].sx / cl[pick].n, cz = cl[pick].sz / cl[pick].n;
			// Normal to the closed door leaf is (-lz, lx); aim 2.5 units past the doorway on the far side from the bot
			float nx = -cl[pick].lz, nz = cl[pick].lx;
			if ((cx - px) * nx + (cz - pz) * nz < 0.0f) { nx = -nx; nz = -nz; }
			*gx = cx; *gz = cz;
			*wx = cx + nx * 2.5f; *wz = cz + nz * 2.5f;
			*kind = 2;
			return true;
		}
	}
	// Fallback 1: any active player-openable door in the area (e.g. a sub-gate door like Area 75 Door#1 or start-room doors not yet opened)
	// Only once the area is cleared (gate flag up): between waves this would send bots to unrelated doors.
	if (dw >= 0x10000 && dw < 0x700000 && flagUp)
	{
		unsigned cnt = *(const UINT16 *) &ram[(dw + 0x800) ^ 2];
		if (cnt > 128) cnt = 0;
		float bestD = 1e30f; float bcx = 0, bcz = 0, bwx = 0, bwz = 0; bool foundDoor = false;
		for (unsigned i = 1; i <= cnt; i++)
		{
			UINT32 e = dw + 0x7EC + i * 0x18;
			UINT32 flg = *(const UINT32 *) &ram[e];
			if (!(flg & 0x40000000) || !(flg & 0x00100000)) continue;    // placed and player-openable
			if (flg & (0x04000000 | 0x00000400)) continue;               // already opened by player or held open
			float cx, cz, hx, hz, lx, lz;
			if (!SpikeoutDoorGeom(ram, *(const UINT32 *) &ram[e + 4], &cx, &cz, &hx, &hz, &lx, &lz)) continue;
			float d = (cx - px) * (cx - px) + (cz - pz) * (cz - pz);
			if (d < bestD)
			{
				float nx = -lz, nz = lx;
				if ((cx - px) * nx + (cz - pz) * nz < 0.0f) { nx = -nx; nz = -nz; }
				bestD = d; bcx = cx; bcz = cz; bwx = cx + nx * 2.5f; bwz = cz + nz * 2.5f; foundDoor = true;
			}
		}
		if (foundDoor) { *gx = bcx; *gz = bcz; *wx = bwx; *wz = bwz; *kind = 4; return true; }
	}
	// Fallback 2: if gateFlag is up (or in Area 75 after the hall gate), scan for any active attackable shutter not yet broken
	if (sw >= 0x10000 && sw < 0x700000 && flagUp)
	{
		unsigned cnt = ram[(sw + 0x10) ^ 3];
		UINT32 sdefBase = *(const UINT32 *) &ram[sw + 0x14];
		if (cnt > 96) cnt = 0;
		for (unsigned i = 0; i < cnt; i++)
		{
			UINT32 e = sw + 0x18 + i * 0x1C;
			UINT32 flg = *(const UINT32 *) &ram[e];
			if (SpikeoutJunkShutter(ram[0x10402C ^ 3], i)) continue;
			if ((flg & 0x40000000) && (flg & 0x00040000) && !(flg & 0x08000000))   // placed, attackable, not broken
			{
				float x = *(const float *) &ram[e + 0x0C], z = *(const float *) &ram[e + 0x14];
				if (x == 0.0f && z == 0.0f)
				{
					const UINT8 *sd = SpikeoutMapPtr(sdefBase + i * 0x34, 0x34);
					if (sd) SpikeoutWallCenter(ram, sd[0x2B ^ 3], &x, &z);
				}
				if (x == x && z == z && fabsf(x) < 1000.0f && fabsf(z) < 1000.0f && (x != 0.0f || z != 0.0f)) { sx += x; sz += z; n++; }
			}
		}
		if (n) { *gx = sx / n; *gz = sz / n; *wx = *gx; *wz = *gz; *kind = 3; return true; }
	}
	if (flagUp && area == 75)
	{
		*gx = 0.5f * (19.64f + 15.66f); *gz = 0.5f * (37.24f + 32.50f);
		*wx = *gx; *wz = *gz; *kind = 3;
		return true;
	}
	return false;
}
// Check if any active wall segment blocks the straight line from (px,py,pz) to (tx,tz), or is right in front of the bot
// within 1.5 units. Ignore wall segments that belong to the target door/shutter (or any lit exit door/shutter) so the bot
// doesn't avoid the gate itself! If a wall blocks the path, sets (*ax,*az) to slide along the wall toward the endpoint closer
// to (tx,tz) (standing 0.8 units clear of the wall on the bot's side), or round the corner once past the endpoint.
// Wall-slide commitment (11/08): the end picked for a wall is kept while we follow that wall, instead of being
// re-picked every frame (the "nearer end" flips as we move along it, and the bot dashed back and forth).
static int g_awCommitWp = 0, g_awCommitEnd = -1, g_awLastWp = 0, g_awLastEnd = -1;
static UINT32 g_awLastFlg = 0; static float g_awLastLen = 0.0f; static bool g_awLastPush = false;   // steer.txt + door push
static UINT32 g_awPushFrame = ~0u;   // frame in which we push into a door wall: Shift held (11/08)
static float g_awPushCx = 0, g_awPushCz = 0, g_awPushNx = 0, g_awPushNz = 0;   // that wall's middle / normal toward its far side
// Door pass (11/08): Shift held = the camera stays put (and no dash). Near a door the camera swung and the lever
// went the wrong way; with Shift held the lever keeps meaning the same, so the bot walks the door's normal straight
// through. Requested by: an exit door within 4, the "door in the way" push (wn 7), a door wall in the way (wn 8).
static UINT32 g_dpReqF = ~0u; static float g_dpReqCx, g_dpReqCz, g_dpReqNx, g_dpReqNz;
static float g_teDoorCx = 1e9f, g_teDoorCz = 1e9f;   // centre of the exit door being gone through (for the stall kick)
static unsigned g_passNextArea = ~0u; static float g_passNextX = 0, g_passNextZ = 0; static bool g_passNextOn = false;   // after a hard-coded pass spot
static void SpikeoutDoorPassReq(float cx, float cz, float nx, float nz)
{
	g_dpReqF = g_ppcTraceFrame; g_dpReqCx = cx; g_dpReqCz = cz; g_dpReqNx = nx; g_dpReqNz = nz;
}
static bool SpikeoutAvoidWallOne(const UINT8 *ram, float px, float py, float pz, float tx, float tz, int skipWp, float *ax, float *az, int *hitWp)
{
	UINT32 wh = *(const UINT32 *) &ram[0x106168] & 0x7FFFFF;
	if (wh < 0x10000 || wh + 0x18 > 0x800000) return false;
	UINT32 wt = *(const UINT32 *) &ram[wh + 0x08] & 0x7FFFFF;
	if (wt < 0x10000 || wt >= 0x800000) return false;
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	UINT32 sw = *(const UINT32 *) &ram[0x104514] & 0x7FFFFF;
	float mvx = tx - px, mvz = tz - pz, mv2 = mvx * mvx + mvz * mvz;
	if (mv2 < 0.09f) return false;
	float mvl = sqrtf(mv2), dirx = mvx / mvl, dirz = mvz / mvl;
	float bestScore = 1e30f; int bestWp = 0; float bestSide = 1.0f;
	for (unsigned wid = 0; wid < 2048 && wt + (wid + 1) * 4 <= 0x800000; wid++)
	{
		UINT32 wp = *(const UINT32 *) &ram[wt + wid * 4];
		if (wp == 0xFFFFFFFFu) break;
		wp &= 0x7FFFFF;
		if (wp < 0x10000 || wp + 0x2C > 0x800000) break;
		if ((int) wp == skipWp) continue;
		UINT32 wflg = *(const UINT32 *) &ram[wp + 0x00];
		// 0x80000000 disabled, 0x01000000 player-passable, 0x00800000 skipped by the player collision (0x050EA8)
		if (wflg & (0x80000000u | 0x01000000u | 0x00800000u)) continue;
		// Skip if this wall belongs to a lit gate door or shutter. Door/shutter walls (flag 0x00010000) carry their
		// index in the gate wall list *(wh+0x10) in flag bits 8-15; the tag table dw+0x5F00 is indexed by that
		// (0x05103C), NOT by the position in this full list (wh+0x08).
		if ((wflg & 0x00010000u) && dw >= 0x10000 && dw + 0x6000 <= 0x800000)
		{
			UINT8 tag = ram[(dw + 0x5F00 + ((wflg >> 8) & 0xFF)) ^ 3];
			if (tag != 0)
			{
				if (tag & 0x80)
				{
					unsigned si = tag & 0x7F;
					if (sw >= 0x10000 && sw < 0x700000 && si < 96)
					{
						UINT32 sflg = *(const UINT32 *) &ram[sw + 0x18 + si * 0x1C];
						if (sflg & (0x00020000 | 0x00040000)) continue;  // lit exit or attackable shutter: walk right up to it
					}
				}
				else if (tag <= 128)
				{
					UINT32 dflg = *(const UINT32 *) &ram[dw + 0x7EC + (unsigned) tag * 0x18];
					if (dflg & (0x00800000 | 0x00100000)) continue;     // lit exit door or player-openable door: don't avoid
				}
			}
		}
		float x0 = *(const float *) &ram[wp + 0x0C], y0 = *(const float *) &ram[wp + 0x10], z0 = *(const float *) &ram[wp + 0x14];
		float x1 = *(const float *) &ram[wp + 0x18], y1 = *(const float *) &ram[wp + 0x1C], z1 = *(const float *) &ram[wp + 0x20];
		float dh0 = *(const float *) &ram[wp + 0x24], dh1 = *(const float *) &ram[wp + 0x28];
		if (!(x0 == x0) || !(z0 == z0) || !(x1 == x1) || !(z1 == z1)) continue;
		// Height check: skip walls that are on another floor (entirely below our feet + step or above our head)
		float wyMin = (y0 < y1 ? y0 : y1), wyMax = (y0 + dh0 > y1 + dh1 ? y0 + dh0 : y1 + dh1);
		// py is the Rob's y = feet + 1.15 (FN_BODY): the body is feet+0.6 (a walkable step's top) .. feet+1.8 (11/11:
		// was py+0.25..py+1.8 = feet+1.4..feet+2.95, which missed railings and took beams over the head for walls)
		if (wyMax < py - 1.15f + 0.6f || wyMin > py - 1.15f + 1.8f) continue;
		float wx = x1 - x0, wz = z1 - z0, wl2 = wx * wx + wz * wz;
		if (wl2 < 0.04f || wl2 > 1e6f) continue;
		float wl = sqrtf(wl2), ux = wx / wl, uz = wz / wl;              // unit vector along wall (P0 -> P1)
		float nx = -uz, nz = ux;                                        // unit normal to wall
		// Signed distance from bot (px,pz) to wall line, and projection u along wall [0..wl]
		float rx = px - x0, rz = pz - z0;
		float sBot = rx * nx + rz * nz;
		float uBot = rx * ux + rz * uz;
		float side = sBot >= 0.0f ? 1.0f : -1.0f;
		// If target (tx,tz) is within 0.8 units of this wall segment and we are within 2.5 units of target, don't avoid (it's our gate target)
		float rtx = tx - x0, rtz = tz - z0, sTgt = rtx * nx + rtz * nz, uTgt = rtx * ux + rtz * uz;
		if (mvl < 3.0f && uTgt >= -0.5f && uTgt <= wl + 0.5f && fabsf(sTgt) < 0.8f) continue;
		// Case A: Ray from (px,pz) to (tx,tz) crosses the wall segment within lookahead (up to min(mvl, 15.0f))
		float denom = dirx * nx + dirz * nz;
		bool hit = false; float score = 1e30f;
		if (denom * side < -0.05f && fabsf(sBot) >= 0.0f)               // moving toward the wall plane
		{
			float tHit = -sBot / denom;                                 // distance along movement ray to wall line
			if (tHit >= 0.0f && tHit <= (mvl < 15.0f ? mvl : 15.0f))
			{
				float uHit = uBot + tHit * (dirx * ux + dirz * uz);
				if (uHit >= -0.45f && uHit <= wl + 0.45f) { hit = true; score = tHit; }
			}
		}
		// Case B: Already brushing against the wall (|sBot| < 0.90, uBot in [-0.3, wl+0.3]) and trying to push into it
		if (!hit && fabsf(sBot) < 0.90f && uBot >= -0.3f && uBot <= wl + 0.3f && denom * side < 0.1f)
		{
			hit = true; score = fabsf(sBot);
		}
		if (hit && score < bestScore)
		{
			bestScore = score; bestWp = (int) wp; bestSide = side;
		}
	}
	if (!bestWp) return false;
	float x0 = *(const float *) &ram[bestWp + 0x0C], z0 = *(const float *) &ram[bestWp + 0x14];
	float x1 = *(const float *) &ram[bestWp + 0x18], z1 = *(const float *) &ram[bestWp + 0x20];
	float wx = x1 - x0, wz = z1 - z0, wl = sqrtf(wx * wx + wz * wz);
	float ux = wx / wl, uz = wz / wl, nx = -uz * bestSide, nz = ux * bestSide;   // nx,nz points toward the bot's side of the wall
	// Pick the endpoint (P0 or P1) that leads closer to (tx,tz)
	float e0x = x0 - ux * 1.0f + nx * 1.0f, e0z = z0 - uz * 1.0f + nz * 1.0f;
	float e1x = x1 + ux * 1.0f + nx * 1.0f, e1z = z1 + uz * 1.0f + nz * 1.0f;
	float d0 = sqrtf((tx - e0x) * (tx - e0x) + (tz - e0z) * (tz - e0z)) + sqrtf((e0x - px) * (e0x - px) + (e0z - pz) * (e0z - pz));
	float d1 = sqrtf((tx - e1x) * (tx - e1x) + (tz - e1z) * (tz - e1z)) + sqrtf((e1x - px) * (e1x - px) + (e1z - pz) * (e1z - pz));
	int end = d0 <= d1 ? 0 : 1;
	if (skipWp == 0)
	{
		g_awLastFlg = *(const UINT32 *) &ram[bestWp]; g_awLastLen = wl; g_awLastPush = false;
		// A door / shutter wall (flag 0x00010000, short) in the way: walk into its middle and through it instead of
		// sliding to one of its ends - 05 ran end to end in front of its start-room door for 15s while 04, which just
		// stood pushing at its door, got through (11/08).
		// Only B / A+B gate walls (0x40000): an A-only gate wall (0x20000) is where enemies come in - a plain wall (11/09).
		if ((g_awLastFlg & 0x00010000u) && (g_awLastFlg & 0x00040000u) && wl < 4.0f)
		{
			*ax = 0.5f * (x0 + x1) - nx * 1.5f; *az = 0.5f * (z0 + z1) - nz * 1.5f;   // nx,nz point to our side
			g_awPushCx = 0.5f * (x0 + x1); g_awPushCz = 0.5f * (z0 + z1); g_awPushNx = -nx; g_awPushNz = -nz;
			g_awLastWp = bestWp; g_awLastEnd = -1; g_awLastPush = true;
			if (hitWp) *hitWp = bestWp;
			return true;
		}
	}
	if (skipWp == 0)                                     // the first wall in the way: committed end if it's the same wall
	{
		if (bestWp == g_awCommitWp && g_awCommitEnd >= 0) end = g_awCommitEnd;
		g_awLastWp = bestWp; g_awLastEnd = end;
	}
	if (end == 0) { *ax = e0x; *az = e0z; }
	else          { *ax = e1x; *az = e1z; }
	if (hitWp) *hitWp = bestWp;
	return true;
}
static bool SpikeoutAvoidWall(const UINT8 *ram, float px, float py, float pz, float tx, float tz, float *ax, float *az)
{
	int wp1 = 0, wp2 = 0; float x1 = tx, z1 = tz, x2 = tx, z2 = tz;
	if (!SpikeoutAvoidWallOne(ram, px, py, pz, tx, tz, 0, &x1, &z1, &wp1)) return false;
	if (g_awLastPush) { *ax = x1; *az = z1; return true; }   // through a door wall: no corner lookahead
	// Multi-segment lookahead: if the path to the first wall's corner is itself blocked by another wall, steer around that one first
	if (SpikeoutAvoidWallOne(ram, px, py, pz, x1, z1, wp1, &x2, &z2, &wp2)) { *ax = x2; *az = z2; }
	else { *ax = x1; *az = z1; }
	return true;
}

// ---------------------------------------------------------------------------
// Wall navigation: a grid path over the stage's own wall-collision map
// ---------------------------------------------------------------------------
// The stage's walls are in RAM for the whole stage (*(0x106168) = 0x480000, built at stage load by 0x050154
// from the banked CROM; the same data as 8p\wallmap.bin). The full wall list is at header +0x08, records of
// 0x2C bytes: +00 flags, +0C/10/14 x0/y0/z0, +18/1C/20 x1/y1/z1, +24/+28 wall heights at each end.
// Flags: 0x80000000 disabled (an open door's wall goes off), 0x01000000 / 0x00800000 not solid for players.
// The walls that stand at the bot's height (same test as SpikeoutAvoidWall: they reach from below
// y+1.8 to above y+0.25) are drawn into a grid of WN_CELL cells, padded by WN_PAD for the body. A breadth-
// first search from the goal fills a distance field; the bot steers for the farthest cell down that path it
// can still see straight from where it stands. The grid is rebuilt when the stage or the bot's floor changes,
// or when a wall is switched on / off (doors opening); cells the bot got stuck in front of are added on top
// (WallNavMarkBlocked) for the rest of the stage. The goal's own surroundings are always open, so a closed
// door or a shutter that is the target doesn't wall itself off.
#define WN_MAXW  640
#define WN_CELL  0.4f
#define WN_PAD   0.45f
static struct
{
	bool ok; unsigned stage; float floorY; UINT32 wallHash;
	float ox, oz, cs; int w, h;
	UINT8 blk[WN_MAXW * WN_MAXW];      // walls (rebuilt)
	UINT32 learn[WN_MAXW * WN_MAXW];   // cells we got stuck at: blocked until this frame (11/11: was for the whole stage)
	int goal; UINT32 goalF;            // goal cell of the current field, frame it was made
	UINT16 dist[WN_MAXW * WN_MAXW];
} g_wn;
static int g_wnQueue[WN_MAXW * WN_MAXW];
static int g_aiLogWn = 0;              // for ai.txt: 0 not used, 1 path, 2 no path (fallback), 3 no wall data, 4 floor-grid (3D) path, 5 no path at all, 6 no floor path but a 2D one
static inline int WnCell(float x, float z)
{
	int cx = (int) floorf((x - g_wn.ox) / g_wn.cs), cz = (int) floorf((z - g_wn.oz) / g_wn.cs);
	if (cx < 0 || cz < 0 || cx >= g_wn.w || cz >= g_wn.h) return -1;
	return cz * g_wn.w + cx;
}
static UINT32 WnWallList(const UINT8 *ram)
{
	UINT32 wh = *(const UINT32 *) &ram[0x106168] & 0x7FFFFF;
	if (wh < 0x10000 || wh + 0x18 > 0x800000) return 0;
	UINT32 wt = *(const UINT32 *) &ram[wh + 0x08] & 0x7FFFFF;
	return (wt < 0x10000 || wt >= 0x800000) ? 0 : wt;
}
static bool WnSolidAt(const UINT8 *ram, UINT32 wp, float py)
{
	UINT32 f = *(const UINT32 *) &ram[wp];
	if (f & (0x80000000u | 0x01000000u | 0x00800000u)) return false;
	float y0 = *(const float *) &ram[wp + 0x10], y1 = *(const float *) &ram[wp + 0x1C];
	float h0 = *(const float *) &ram[wp + 0x24], h1 = *(const float *) &ram[wp + 0x28];
	float lo = y0 < y1 ? y0 : y1, hi = (y0 + h0 > y1 + h1) ? y0 + h0 : y1 + h1;
	return !(hi < py - 1.15f + 0.6f || lo > py - 1.15f + 1.8f);   // py = Rob y = feet + 1.15: body feet+0.6..feet+1.8 (11/11)
}
// A gate's wall gone (11/10): gate walls (0x10000 with the A 0x20000 / B 0x40000 bits) within 2.5 of (x,z): true
// when there is at least one and none of them is solid for players any more (0x80000000 off, 0x01000000 /
// 0x00800000 passable).
static bool SpikeoutGateWallOpen(const UINT8 *ram, float x, float z)
{
	UINT32 wt = WnWallList(ram);
	if (!wt) return false;
	bool found = false;
	for (int i = 0; i < 8192 && wt + (i + 1) * 4 <= 0x800000; i++)
	{
		UINT32 wp = *(const UINT32 *) &ram[wt + i * 4];
		if (wp == 0xFFFFFFFFu) break;
		wp &= 0x7FFFFF;
		if (wp < 0x10000 || wp + 0x2C > 0x800000) break;
		UINT32 f = *(const UINT32 *) &ram[wp];
		if (!(f & 0x00010000u) || !(f & 0x00060000u)) continue;
		float ax = *(const float *) &ram[wp + 0x0C], az = *(const float *) &ram[wp + 0x14];
		float bx = *(const float *) &ram[wp + 0x18], bz = *(const float *) &ram[wp + 0x20];
		if (!(ax == ax) || !(az == az) || !(bx == bx) || !(bz == bz)) continue;
		float sx = bx - ax, sz = bz - az, l2 = sx * sx + sz * sz;
		float t = l2 > 1e-6f ? ((x - ax) * sx + (z - az) * sz) / l2 : 0.0f;
		t = t < 0 ? 0 : (t > 1 ? 1 : t);
		float cx = ax + sx * t - x, cz = az + sz * t - z;
		if (cx * cx + cz * cz > 6.25f) continue;
		found = true;
		if (!(f & (0x80000000u | 0x01000000u | 0x00800000u))) return false;   // still standing
	}
	return found;
}
// Stuck at a step? Walls within 1.0 (only those toward (dx,dz) if given) that block our body and whose top is
// 0.25..3.0 above our feet = a step a high jump gets over. If a taller wall is also there it is not a step.
static bool SpikeoutStepAhead(const UINT8 *ram, float px, float py, float pz, float dx, float dz)
{
	UINT32 wt = WnWallList(ram);
	if (!wt) return false;
	float dl = sqrtf(dx * dx + dz * dz);
	bool useDir = dl > 0.01f;
	if (useDir) { dx /= dl; dz /= dl; }
	bool step = false;
	for (int i = 0; i < 8192 && wt + (i + 1) * 4 <= 0x800000; i++)
	{
		UINT32 wp = *(const UINT32 *) &ram[wt + i * 4];
		if (wp == 0xFFFFFFFFu) break;
		wp &= 0x7FFFFF;
		if (wp < 0x10000 || wp + 0x2C > 0x800000) break;
		if (*(const UINT32 *) &ram[wp] & (0x80000000u | 0x01000000u | 0x00800000u)) continue;
		float ax = *(const float *) &ram[wp + 0x0C], az = *(const float *) &ram[wp + 0x14];
		float bx = *(const float *) &ram[wp + 0x18], bz = *(const float *) &ram[wp + 0x20];
		if (!(ax == ax) || !(az == az) || !(bx == bx) || !(bz == bz)) continue;
		float sx = bx - ax, sz = bz - az, l2 = sx * sx + sz * sz;
		float t = l2 > 1e-6f ? ((px - ax) * sx + (pz - az) * sz) / l2 : 0.0f;
		t = t < 0 ? 0 : (t > 1 ? 1 : t);
		float cx = ax + sx * t - px, cz = az + sz * t - pz, d = sqrtf(cx * cx + cz * cz);
		if (d > 1.0f || (useDir && d > 0.05f && (cx * dx + cz * dz) / d < 0.3f)) continue;   // not close (and ahead)
		float y0 = *(const float *) &ram[wp + 0x10], y1 = *(const float *) &ram[wp + 0x1C];
		float h0 = *(const float *) &ram[wp + 0x24], h1 = *(const float *) &ram[wp + 0x28];
		float lo = y0 < y1 ? y0 : y1, hi = (y0 + h0 > y1 + h1) ? y0 + h0 : y1 + h1;
		if (hi < py + 0.25f || lo > py + 1.8f) continue;                     // below our feet / overhead
		if (hi > py + 3.0f) return false;                                    // a real wall
		step = true;
	}
	return step;
}
static bool WallNavBuild(const UINT8 *ram, float py)
{
	UINT32 wt = WnWallList(ram);
	unsigned stage = ram[0x10402C ^ 3];
	if (!wt) { g_wn.ok = false; return false; }
	// Which walls are on decides the grid: hash their flags (doors switch theirs) every call - cheap.
	UINT32 hsh = 2166136261u; int n = 0;
	for (; n < 8192 && wt + (n + 1) * 4 <= 0x800000; n++)
	{
		UINT32 wp = *(const UINT32 *) &ram[wt + n * 4];
		if (wp == 0xFFFFFFFFu) break;
		wp &= 0x7FFFFF;
		if (wp < 0x10000 || wp + 0x2C > 0x800000) break;
		hsh = (hsh ^ (*(const UINT32 *) &ram[wp] & 0x81800000u)) * 16777619u;
	}
	if (n == 0) { g_wn.ok = false; return false; }
	bool newStage = (stage != g_wn.stage);
	if (g_wn.ok && !newStage && hsh == g_wn.wallHash && fabsf(py - g_wn.floorY) < 0.75f) return true;
	if (newStage)
	{
		float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
		for (int i = 0; i < n; i++)
		{
			UINT32 wp = *(const UINT32 *) &ram[wt + i * 4] & 0x7FFFFF;
			float a[4] = { *(const float *) &ram[wp + 0x0C], *(const float *) &ram[wp + 0x14], *(const float *) &ram[wp + 0x18], *(const float *) &ram[wp + 0x20] };
			if (!(a[0] == a[0]) || !(a[1] == a[1]) || !(a[2] == a[2]) || !(a[3] == a[3]) || fabsf(a[0]) > 2000 || fabsf(a[1]) > 2000 || fabsf(a[2]) > 2000 || fabsf(a[3]) > 2000) continue;
			x0 = fminf(x0, fminf(a[0], a[2])); x1 = fmaxf(x1, fmaxf(a[0], a[2]));
			z0 = fminf(z0, fminf(a[1], a[3])); z1 = fmaxf(z1, fmaxf(a[1], a[3]));
		}
		if (x0 > x1) { g_wn.ok = false; return false; }
		x0 -= 2.0f; z0 -= 2.0f; x1 += 2.0f; z1 += 2.0f;
		float span = fmaxf(x1 - x0, z1 - z0);
		g_wn.cs = fmaxf(WN_CELL, span / (float) WN_MAXW);
		g_wn.ox = x0; g_wn.oz = z0;
		g_wn.w = (int) ((x1 - x0) / g_wn.cs) + 1; g_wn.h = (int) ((z1 - z0) / g_wn.cs) + 1;
		if (g_wn.w > WN_MAXW) g_wn.w = WN_MAXW;
		if (g_wn.h > WN_MAXW) g_wn.h = WN_MAXW;
		memset(g_wn.learn, 0, sizeof(g_wn.learn));
		g_wn.stage = stage;
	}
	memset(g_wn.blk, 0, (size_t) g_wn.w * g_wn.h);
	// Rounded, not rounded up (11/11): ceil gave 2 cells at cs 0.4 = walls ~1.0 thicker on each side, so 2-wide
	// passages were shut. 1 cell (+ the half cell of the drawing) is ~0.6, close to WN_PAD.
	int pad = (int) floorf(WN_PAD / g_wn.cs + 0.5f);
	if (pad < 1) pad = 1;
	for (int i = 0; i < n; i++)
	{
		UINT32 wp = *(const UINT32 *) &ram[wt + i * 4] & 0x7FFFFF;
		if (!WnSolidAt(ram, wp, py)) continue;
		float ax = *(const float *) &ram[wp + 0x0C], az = *(const float *) &ram[wp + 0x14];
		float bx = *(const float *) &ram[wp + 0x18], bz = *(const float *) &ram[wp + 0x20];
		if (!(ax == ax) || !(az == az) || !(bx == bx) || !(bz == bz)) continue;
		float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
		if (len > 500.0f) continue;
		int steps = (int) ceilf(len / (g_wn.cs * 0.5f)) + 1;
		for (int k = 0; k <= steps; k++)
		{
			float t = (float) k / (float) steps;
			int c = WnCell(ax + dx * t, az + dz * t);
			if (c < 0) continue;
			int cx = c % g_wn.w, cz = c / g_wn.w;
			for (int oz = -pad; oz <= pad; oz++)
				for (int ox = -pad; ox <= pad; ox++)
				{
					if (ox * ox + oz * oz > pad * pad + 1) continue;
					int nx = cx + ox, nz = cz + oz;
					if (nx >= 0 && nz >= 0 && nx < g_wn.w && nz < g_wn.h) g_wn.blk[nz * g_wn.w + nx] = 1;
				}
		}
	}
	g_wn.ok = true; g_wn.wallHash = hsh; g_wn.floorY = py; g_wn.goal = -1;
	return true;
}
static inline bool WnOpen(int c, int startC, int goalC)
{
	if (!g_wn.blk[c] && g_wn.learn[c] <= g_ppcTraceFrame) return true;
	// the start's and the goal's surroundings are always open (standing next to a wall / a closed gate)
	int cx = c % g_wn.w, cz = c / g_wn.w;
	int sx = startC % g_wn.w, sz = startC / g_wn.w, gx = goalC % g_wn.w, gz = goalC / g_wn.w;
	int rs = (int) ceilf(0.8f / g_wn.cs), rg = (int) ceilf(1.6f / g_wn.cs);
	return ((cx - sx) * (cx - sx) + (cz - sz) * (cz - sz) <= rs * rs) || ((cx - gx) * (cx - gx) + (cz - gz) * (cz - gz) <= rg * rg);
}
static bool WnSees(int a, int b, int startC, int goalC)   // straight line a -> b over open cells only
{
	int ax = a % g_wn.w, az = a / g_wn.w, bx = b % g_wn.w, bz = b / g_wn.w;
	int n = abs(bx - ax) > abs(bz - az) ? abs(bx - ax) : abs(bz - az);
	for (int k = 1; k <= n; k++)
	{
		int cx = ax + (bx - ax) * k / n, cz = az + (bz - az) * k / n;
		if (!WnOpen(cz * g_wn.w + cx, startC, goalC)) return false;
	}
	return true;
}
// Steering target toward (gx,gz) around the walls. Returns false when there is no wall data or no path
// (the caller keeps its own target then).
static bool SpikeoutWallNav(const UINT8 *ram, float px, float py, float pz, float gx, float gz, float *tx, float *tz)
{
	if (!WallNavBuild(ram, py)) { g_aiLogWn = 3; return false; }
	int sC = WnCell(px, pz), gC = WnCell(gx, gz);
	if (sC < 0 || gC < 0) { g_aiLogWn = 2; return false; }
	if (sC == gC || WnSees(sC, gC, sC, gC)) { *tx = gx; *tz = gz; g_aiLogWn = 1; return true; }   // in plain sight
	// distance field from the goal; reused while the goal stays within 2 cells for under 10 frames
	bool reuse = g_wn.goal >= 0 && g_ppcTraceFrame - g_wn.goalF < 10 &&
	             abs(g_wn.goal % g_wn.w - gC % g_wn.w) <= 2 && abs(g_wn.goal / g_wn.w - gC / g_wn.w) <= 2 && g_wn.dist[sC] != 0xFFFF;
	if (!reuse)
	{
		int N = g_wn.w * g_wn.h;
		g_aipWnSearch++;
		memset(g_wn.dist, 0xFF, (size_t) N * sizeof(UINT16));
		int qh = 0, qt = 0;
		g_wn.dist[gC] = 0; g_wnQueue[qt++] = gC;
		static const int ddx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, ddz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		while (qh < qt)
		{
			int c = g_wnQueue[qh++];
			if (c == sC) break;
			int cx = c % g_wn.w, cz = c / g_wn.w; UINT16 d = g_wn.dist[c];
			for (int k = 0; k < 8; k++)
			{
				int nx = cx + ddx[k], nz = cz + ddz[k];
				if (nx < 0 || nz < 0 || nx >= g_wn.w || nz >= g_wn.h) continue;
				int nc = nz * g_wn.w + nx;
				if (g_wn.dist[nc] != 0xFFFF || !WnOpen(nc, sC, gC)) continue;
				if (k >= 4 && (!WnOpen(cz * g_wn.w + nx, sC, gC) || !WnOpen(nz * g_wn.w + cx, sC, gC))) continue;   // no corner cutting
				g_wn.dist[nc] = (UINT16) (d + 1); g_wnQueue[qt++] = nc;
			}
		}
		g_wn.goal = gC; g_wn.goalF = g_ppcTraceFrame;
	}
	if (g_wn.dist[sC] == 0xFFFF) { g_aiLogWn = 2; return false; }
	// walk the field down from the start; steer for the farthest path cell still in plain sight (up to ~8 units)
	int c = sC, best = -1, maxSteps = (int) (8.0f / g_wn.cs);
	for (int step = 0; step < maxSteps && c != gC; step++)
	{
		int cx = c % g_wn.w, cz = c / g_wn.w, nb = -1; UINT16 bd = g_wn.dist[c];
		for (int oz = -1; oz <= 1; oz++)
			for (int ox = -1; ox <= 1; ox++)
			{
				int nx = cx + ox, nz = cz + oz;
				if ((ox == 0 && oz == 0) || nx < 0 || nz < 0 || nx >= g_wn.w || nz >= g_wn.h) continue;
				int nc = nz * g_wn.w + nx;
				if (g_wn.dist[nc] < bd) { bd = g_wn.dist[nc]; nb = nc; }
			}
		if (nb < 0) break;
		c = nb;
		if (WnSees(sC, c, sC, gC)) best = c;
		else if (best >= 0) break;
	}
	if (c == gC && WnSees(sC, gC, sC, gC)) { *tx = gx; *tz = gz; g_aiLogWn = 1; return true; }
	if (best < 0) best = c;
	*tx = g_wn.ox + ((float) (best % g_wn.w) + 0.5f) * g_wn.cs;
	*tz = g_wn.oz + ((float) (best / g_wn.w) + 0.5f) * g_wn.cs;
	g_aiLogWn = 1;
	return true;
}
// Stuck at (px,pz) pushing toward (tx,tz): the cells just ahead count as blocked for the rest of the stage.
static void WallNavMarkBlocked(float px, float pz, float tx, float tz)
{
	if (!g_wn.ok) return;
	float dx = tx - px, dz = tz - pz, d = sqrtf(dx * dx + dz * dz);
	if (d < 0.05f) return;
	dx /= d; dz /= d;
	int c0 = WnCell(px, pz);
	for (float step = 0.5f; step <= 1.0f; step += 0.25f)
	{
		int c = WnCell(px + dx * step, pz + dz * step);
		if (c >= 0 && c != c0) { g_wn.learn[c] = g_ppcTraceFrame + 600; g_wn.goal = -1; }   // 10s (bots bump into each other too), like FloorNav
	}
}
// ---------------------------------------------------------------------------------------------------------------
// FloorNav: 3D (layered) path finding on the stage's FLOOR collision grid + walls.
// The stage block at *(0x106168) (0x480000, built at stage load by 0x050154) also holds the floor grid:
//   +0x00 -> nx*nz cell pointers (z rows), +0x0C -> grid header { -7, 0x1F, f32 x0, z0, x1, z1, u32 nx, nz } (cell 0.5).
//   A cell's floor list: n x { u32 flags (low 16 = zone), f32 D, nx, ny, nz } then 0xFFFFFFFF; one entry per layer
//   (2+ = a bridge / upper floor over a lower one). y(x,z) = D - (nx*(x-cellX) + nz*(z-cellZ)) / ny.
// A Rob's y (+0x384) stands FN_BODY above its floor.
// Nodes = (cell, layer). Moves to the 8 neighbour cells: walk (|dy| <= FN_STEP), high jump up (dy <= FN_JUMP),
// fall off an edge (to the highest floor below). A move is blocked by a solid wall crossing it (or passing
// within FN_PAD of it) whose height overlaps the body: [top floor + 0.3, top floor + 1.8] - so a step's own face
// (top = the upper floor) never blocks, a railing does. Door walls are read live, so opened doors count at once.
// Dijkstra from the goal node; the field is reused while the goal stays the same node for FN_REUSE frames.
#include <vector>
#include <queue>
static const float FN_BODY = 1.15f, FN_STEP = 0.6f, FN_JUMP = 2.3f, FN_PAD = 0.3f;
static const float FN_ENEMY_BODY = 0.25f;   // an enemy Rob's y stands about 0.25 above its floor (players: FN_BODY)
static const unsigned FN_REUSE = 20;
struct SpikeoutFloorNavData
{
	bool ok = false; unsigned stage = ~0u; UINT32 grid = 0, gh = 0;
	float x0 = 0, z0 = 0, cs = 0.5f; int nx = 0, nz = 0;
	std::vector<int> first;            // cell -> first node (nodes of a cell are consecutive)
	std::vector<UINT8> cnt;            // cell -> node count
	std::vector<int> ncell;            // node -> cell
	std::vector<float> y;              // node -> floor y at the cell centre
	std::vector<UINT32> wfirst;        // cell -> first index in wlist (CSR, ncells+1)
	std::vector<UINT32> wlist;         // wall record addresses near the cell (within FN_PAD)
	std::vector<UINT32> learn;         // cell -> blocked until this frame (stuck there; 10s - bots bump into each other too)
	std::vector<UINT32> dist;          // node -> cost to the goal
	std::vector<int> next;             // node -> next node toward the goal
	std::vector<UINT8> how;            // node -> move to next: 0 walk, 1 jump up, 2 fall
	int goal = -1, start = -1; UINT32 goalF = 0; bool full = false;
	int learnKeep = -1;                // the goal's cell: never blocked by a stuck mark   // full: the last search ran out (no early stop)
};
static SpikeoutFloorNavData g_fn;
static int g_aiLogFn = 0;              // for ai.txt fn=: 0 not used, 1 path, 2 no path, 3 no floor data

static inline float FnRf(const UINT8 *ram, UINT32 a) { return *(const float *) &ram[a]; }
static inline UINT32 FnRw(const UINT8 *ram, UINT32 a) { return *(const UINT32 *) &ram[a]; }

static bool FloorNavBuild(const UINT8 *ram)
{
	UINT32 hdr = FnRw(ram, 0x106168) & 0x7FFFFF;
	unsigned stage = ram[0x10402C ^ 3];
	if (hdr < 0x10000 || hdr + 0x20 > 0x800000) { g_fn.ok = false; return false; }
	UINT32 grid = FnRw(ram, hdr + 0x00) & 0x7FFFFF, gh = FnRw(ram, hdr + 0x0C) & 0x7FFFFF;
	if (g_fn.ok && stage == g_fn.stage && grid == g_fn.grid && gh == g_fn.gh) return true;
	g_fn.ok = false;
	if (grid < 0x10000 || gh < 0x10000 || gh + 0x20 > 0x800000) return false;
	float x0 = FnRf(ram, gh + 0x08), z0 = FnRf(ram, gh + 0x0C), x1 = FnRf(ram, gh + 0x10);
	int nx = (int) FnRw(ram, gh + 0x18), nz = (int) FnRw(ram, gh + 0x1C);
	if (nx <= 0 || nz <= 0 || nx > 1024 || nz > 1024 || !(x1 > x0)) return false;
	int N = nx * nz;
	if (grid + (UINT32) N * 4 > 0x800000) return false;
	g_fn.x0 = x0; g_fn.z0 = z0; g_fn.nx = nx; g_fn.nz = nz; g_fn.cs = (x1 - x0) / (float) nx;
	float cs = g_fn.cs, hc = cs * 0.5f;
	g_fn.first.assign(N, 0); g_fn.cnt.assign(N, 0); g_fn.ncell.clear(); g_fn.y.clear();
	for (int c = 0; c < N; c++)
	{
		UINT32 p = FnRw(ram, grid + (UINT32) c * 4) & 0x7FFFFF;
		g_fn.first[c] = (int) g_fn.y.size();
		if (p < 0x10000 || p >= 0x800000) continue;
		for (int k = 0; k < 16 && p + 24 <= 0x800000 && FnRw(ram, p) != 0xFFFFFFFFu; k++, p += 24)
		{
			float D = FnRf(ram, p + 4), a = FnRf(ram, p + 8), b = FnRf(ram, p + 12), cc = FnRf(ram, p + 16);
			if (!(D == D) || fabsf(D) > 1000.0f) continue;
			float yy = (b > 0.05f) ? D - (a * hc + cc * hc) / b : D;
			g_fn.y.push_back(yy); g_fn.ncell.push_back(c); g_fn.cnt[c]++;
		}
	}
	// walls -> cells within FN_PAD of them (CSR)
	g_fn.wfirst.assign(N + 1, 0); g_fn.wlist.clear();
	UINT32 wt = WnWallList(ram);
	std::vector<std::pair<int, UINT32>> wc;
	if (wt)
	{
		for (int i = 0; i < 8192 && wt + (i + 1) * 4 <= 0x800000; i++)
		{
			UINT32 wp = FnRw(ram, wt + i * 4);
			if (wp == 0xFFFFFFFFu) break;
			wp &= 0x7FFFFF;
			if (wp < 0x10000 || wp + 0x2C > 0x800000) break;
			float ax = FnRf(ram, wp + 0x0C), az = FnRf(ram, wp + 0x14), bx = FnRf(ram, wp + 0x18), bz = FnRf(ram, wp + 0x20);
			if (!(ax == ax) || !(az == az) || !(bx == bx) || !(bz == bz)) continue;
			float minx = fminf(ax, bx) - FN_PAD, maxx = fmaxf(ax, bx) + FN_PAD, minz = fminf(az, bz) - FN_PAD, maxz = fmaxf(az, bz) + FN_PAD;
			int cx0 = (int) floorf((minx - x0) / cs), cx1 = (int) floorf((maxx - x0) / cs);
			int cz0 = (int) floorf((minz - z0) / cs), cz1 = (int) floorf((maxz - z0) / cs);
			if (cx1 < 0 || cz1 < 0 || cx0 >= nx || cz0 >= nz || (cx1 - cx0) * (cz1 - cz0) > 40000) continue;
			if (cx0 < 0) cx0 = 0; if (cz0 < 0) cz0 = 0; if (cx1 >= nx) cx1 = nx - 1; if (cz1 >= nz) cz1 = nz - 1;
			float sx = bx - ax, sz = bz - az, l2 = sx * sx + sz * sz;
			float r = FN_PAD + cs * 0.7072f;                              // cell centre within pad + half diagonal
			for (int cz = cz0; cz <= cz1; cz++)
				for (int cx = cx0; cx <= cx1; cx++)
				{
					float qx = x0 + (cx + 0.5f) * cs, qz = z0 + (cz + 0.5f) * cs;
					float t = l2 > 1e-6f ? ((qx - ax) * sx + (qz - az) * sz) / l2 : 0.0f;
					t = t < 0 ? 0 : (t > 1 ? 1 : t);
					float dx = ax + sx * t - qx, dz = az + sz * t - qz;
					if (dx * dx + dz * dz <= r * r) wc.push_back(std::make_pair(cz * nx + cx, wp));
				}
		}
	}
	std::sort(wc.begin(), wc.end());
	g_fn.wlist.resize(wc.size());
	for (size_t k = 0; k < wc.size(); k++) { g_fn.wfirst[wc[k].first + 1]++; g_fn.wlist[k] = wc[k].second; }
	for (int c = 0; c < N; c++) g_fn.wfirst[c + 1] += g_fn.wfirst[c];
	size_t M = g_fn.y.size();
	g_fn.dist.assign(M, 0xFFFFFFFFu); g_fn.next.assign(M, -1); g_fn.how.assign(M, 0);
	if (stage != g_fn.stage) g_fn.learn.assign(N, 0);
	else if ((int) g_fn.learn.size() != N) g_fn.learn.assign(N, 0);
	g_fn.stage = stage; g_fn.grid = grid; g_fn.gh = gh; g_fn.goal = -1; g_fn.ok = !g_fn.y.empty();
	return g_fn.ok;
}

// Does the segment (ax,az)-(bx,bz) at floor height fy hit (or pass within pad of) a solid wall listed in cell c?
static bool FnSegBlockedIn(const UINT8 *ram, int c, float ax, float az, float bx, float bz, float fy, float pad)
{
	for (UINT32 k = g_fn.wfirst[c]; k < g_fn.wfirst[c + 1]; k++)
	{
		UINT32 wp = g_fn.wlist[k];
		UINT32 f = FnRw(ram, wp);
		if (f & (0x80000000u | 0x01000000u | 0x00800000u)) continue;     // disabled / passable
		float y0 = FnRf(ram, wp + 0x10), y1 = FnRf(ram, wp + 0x1C), h0 = FnRf(ram, wp + 0x24), h1 = FnRf(ram, wp + 0x28);
		float lo = y0 < y1 ? y0 : y1, hi = (y0 + h0 > y1 + h1) ? y0 + h0 : y1 + h1;
		if (hi < fy + 0.3f || lo > fy + 1.8f) continue;                  // under the feet (a step face) / overhead
		float wx0 = FnRf(ram, wp + 0x0C), wz0 = FnRf(ram, wp + 0x14), wx1 = FnRf(ram, wp + 0x18), wz1 = FnRf(ram, wp + 0x20);
		// segment-segment: crossing?
		float d1x = bx - ax, d1z = bz - az, d2x = wx1 - wx0, d2z = wz1 - wz0;
		float den = d1x * d2z - d1z * d2x;
		if (fabsf(den) > 1e-9f)
		{
			float t = ((wx0 - ax) * d2z - (wz0 - az) * d2x) / den, u = ((wx0 - ax) * d1z - (wz0 - az) * d1x) / den;
			if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return true;
		}
		if (pad <= 0) continue;
		// closest distance: segment end points to the wall, wall end points to the segment
		auto pd = [](float px, float pz, float sx0, float sz0, float sx1, float sz1)
		{
			float sx = sx1 - sx0, sz = sz1 - sz0, l2 = sx * sx + sz * sz;
			float t = l2 > 1e-9f ? ((px - sx0) * sx + (pz - sz0) * sz) / l2 : 0.0f;
			t = t < 0 ? 0 : (t > 1 ? 1 : t);
			float dx = sx0 + sx * t - px, dz = sz0 + sz * t - pz; return dx * dx + dz * dz;
		};
		float p2 = pad * pad;
		if (pd(bx, bz, wx0, wz0, wx1, wz1) < p2 || pd(wx0, wz0, ax, az, bx, bz) < p2 || pd(wx1, wz1, ax, az, bx, bz) < p2) return true;
	}
	return false;
}
// Segment test over every cell it touches.
static bool FnSegBlocked(const UINT8 *ram, float ax, float az, float bx, float bz, float fy, float pad)
{
	float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
	int steps = (int) ceilf(len / (g_fn.cs * 0.5f)) + 1, last = -1;
	for (int k = 0; k <= steps; k++)
	{
		float t = (float) k / (float) steps;
		int cx = (int) floorf((ax + dx * t - g_fn.x0) / g_fn.cs), cz = (int) floorf((az + dz * t - g_fn.z0) / g_fn.cs);
		if (cx < 0 || cz < 0 || cx >= g_fn.nx || cz >= g_fn.nz) return true;
		int c = cz * g_fn.nx + cx;
		if (c == last) continue;
		last = c;
		if (FnSegBlockedIn(ram, c, ax, az, bx, bz, fy, pad)) return true;
	}
	return false;
}
static inline float FnCx(int c) { return g_fn.x0 + ((float) (c % g_fn.nx) + 0.5f) * g_fn.cs; }
static inline float FnCz(int c) { return g_fn.z0 + ((float) (c / g_fn.nx) + 0.5f) * g_fn.cs; }
static int FnCellAt(float x, float z)
{
	int cx = (int) floorf((x - g_fn.x0) / g_fn.cs), cz = (int) floorf((z - g_fn.z0) / g_fn.cs);
	if (cx < 0 || cz < 0 || cx >= g_fn.nx || cz >= g_fn.nz) return -1;
	return cz * g_fn.nx + cx;
}
// Node of (x,z) for feet height fy: the floor under us (highest at or below fy + FN_STEP), else the nearest one.
// Empty cell: look up to 2 cells around.
static int FnNodeAt(float x, float z, float fy, bool any)
{
	int c0 = FnCellAt(x, z);
	if (c0 < 0) return -1;
	for (int r = 0; r <= 2; r++)
	{
		int best = -1; float bs = 1e30f;
		int cx0 = c0 % g_fn.nx, cz0 = c0 / g_fn.nx;
		for (int oz = -r; oz <= r; oz++)
			for (int ox = -r; ox <= r; ox++)
			{
				if (r && abs(ox) != r && abs(oz) != r) continue;
				int cx = cx0 + ox, cz = cz0 + oz;
				if (cx < 0 || cz < 0 || cx >= g_fn.nx || cz >= g_fn.nz) continue;
				int c = cz * g_fn.nx + cx;
				for (int k = 0; k < g_fn.cnt[c]; k++)
				{
					int n = g_fn.first[c] + k; float yy = g_fn.y[n];
					float s = any ? fabsf(yy - fy) : (yy <= fy + FN_STEP ? (fy - yy) : 1000.0f + (yy - fy));
					s += (float) (abs(ox) + abs(oz)) * 0.01f;
					if (s < bs) { bs = s; best = n; }
				}
			}
		if (best >= 0) return best;
	}
	return -1;
}
// Can a bot on node u move into neighbour cell cv's node v? *how: 0 walk, 1 jump up, 2 fall. Wall test included.
static bool FnMove(const UINT8 *ram, int u, int v, int *how, bool padOn)
{
	float yu = g_fn.y[u], yv = g_fn.y[v], dy = yv - yu;
	int cu = g_fn.ncell[u], cv = g_fn.ncell[v];
	// the node of cv a body coming from yu actually ends up on
	int bestW = -1; float bw = 1e30f; int bestJ = -1; float bj = 1e30f; int bestF = -1; float bf = -1e30f; bool face = false;
	for (int k = 0; k < g_fn.cnt[cv]; k++)
	{
		int n = g_fn.first[cv] + k; float d = g_fn.y[n] - yu;
		if (fabsf(d) <= FN_STEP) { if (fabsf(d) < bw) { bw = fabsf(d); bestW = n; } }
		else if (d > FN_STEP && d <= FN_JUMP) { if (d < bj) { bj = d; bestJ = n; } face = true; }
		else if (d > FN_JUMP && d < 2.5f) face = true;                    // a floor at body height: its face blocks a fall
		else if (d < -FN_STEP) { if (g_fn.y[n] > bf) { bf = g_fn.y[n]; bestF = n; } }
	}
	if (v == bestW) *how = 0;
	else if (v == bestJ && bestW < 0) *how = 1;
	else if (v == bestF && bestW < 0 && !face) *how = 2;
	else return false;
	if (g_fn.learn[cv] > g_ppcTraceFrame && cv != g_fn.learnKeep) return false;
	float fy = *how == 2 ? yu : fmaxf(yu, yv);
	(void) dy;
	float ax = FnCx(cu), az = FnCz(cu), bx = FnCx(cv), bz = FnCz(cv);
	float pad = padOn ? FN_PAD : 0.0f;
	if (FnSegBlockedIn(ram, cu, ax, az, bx, bz, fy, pad) || FnSegBlockedIn(ram, cv, ax, az, bx, bz, fy, pad)) return false;
	// diagonal: also the two side cells (no squeezing past a wall corner)
	int ux = cu % g_fn.nx, uz = cu / g_fn.nx, vx = cv % g_fn.nx, vz = cv / g_fn.nx;
	if (ux != vx && uz != vz)
	{
		if (FnSegBlockedIn(ram, uz * g_fn.nx + vx, ax, az, bx, bz, fy, pad) || FnSegBlockedIn(ram, vz * g_fn.nx + ux, ax, az, bx, bz, fy, pad)) return false;
	}
	return true;
}
// Path from the bot (px,py,pz) toward the goal (gx,gz) whose FEET are at gfeet (Rob y minus its body height;
// NAN = unknown: our own level if there is one).
// Returns 0 no floor data (use the old 2D nav), 1 path: steer to (*tx,*tz), *jump = jump now (a step up right
// ahead), 2 no path. *pathDy = height of the node we steer to minus our floor (for logging).
static int SpikeoutFloorNav(const UINT8 *ram, float px, float py, float pz, float gx, float gfeet, float gz, float *tx, float *tz, bool *jump)
{
	*jump = false;
	if (!FloorNavBuild(ram)) { g_aiLogFn = 3; return 0; }
	float feet = py - FN_BODY;
	int s = FnNodeAt(px, pz, feet, false);
	if (s < 0) { g_aiLogFn = 3; return 0; }
	// goal: the floor under its feet (highest floor at most 0.4 above them); height unknown: our level
	int g = (gfeet == gfeet) ? FnNodeAt(gx, gz, gfeet + 0.4f - FN_STEP, false) : FnNodeAt(gx, gz, g_fn.y[s], true);
	if (g < 0) { g_aiLogFn = 2; return 2; }
	if (s == g) { *tx = gx; *tz = gz; g_aiLogFn = 1; return 1; }
	g_fn.learnKeep = g_fn.ncell[g];
	// reuse the field: same goal node, and a path within FN_REUSE frames / no path (searched all) within 90
	bool reuse = g_fn.goal == g && ((g_fn.dist[s] != 0xFFFFFFFFu && g_ppcTraceFrame - g_fn.goalF < FN_REUSE) ||
	                                (g_fn.full && g_ppcTraceFrame - g_fn.goalF < 90));
	if (!reuse)
	{
		std::fill(g_fn.dist.begin(), g_fn.dist.end(), 0xFFFFFFFFu);
		typedef std::pair<UINT32, int> QE;
		std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
		g_fn.dist[g] = 0; g_fn.next[g] = -1; q.push(QE(0, g));
		static const int ddx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, ddz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		int sc = g_fn.ncell[s];
		int pops = 0; g_fn.full = true; g_aipFnSearch++;
		while (!q.empty() && pops < 400000)
		{
			QE e = q.top(); q.pop(); pops++;
			int v = e.second;
			if (e.first != g_fn.dist[v]) continue;
			if (v == s) { g_fn.full = false; break; }
			int cv = g_fn.ncell[v], vx = cv % g_fn.nx, vz = cv / g_fn.nx;
			for (int k = 0; k < 8; k++)                                   // predecessors u: cells next to v
			{
				int ux = vx + ddx[k], uz = vz + ddz[k];
				if (ux < 0 || uz < 0 || ux >= g_fn.nx || uz >= g_fn.nz) continue;
				int cu = uz * g_fn.nx + ux;
				for (int j = 0; j < g_fn.cnt[cu]; j++)
				{
					int u = g_fn.first[cu] + j, how = 0;
					// walls hug the start: no pad on moves out of the start cell, nor into the goal's
					if (!FnMove(ram, u, v, &how, cu != sc && cv != g_fn.ncell[g])) continue;
					UINT32 w = (k < 4 ? 10u : 14u) + (how == 1 ? 30u : (how == 2 ? 40u : 0u));
					UINT32 nd = e.first + w;
					if (nd < g_fn.dist[u]) { g_fn.dist[u] = nd; g_fn.next[u] = v; g_fn.how[u] = (UINT8) how; q.push(QE(nd, u)); }
				}
			}
		}
		g_fn.goal = g; g_fn.goalF = g_ppcTraceFrame; g_aipFnPops += (unsigned) pops;
	}
	if (g_fn.dist[s] == 0xFFFFFFFFu) { g_aiLogFn = 2; return 2; }
	// Walk the path: steer for the farthest node (up to ~6 units) reachable on foot in a straight line from us.
	// A jump/fall move ends the look-ahead: steer just past it; jump when that step up is right in front of us.
	int n = s, best = -1;
	float bestTop = g_fn.y[s];
	for (int step = 0; step < 24 && n != g && g_fn.next[n] >= 0; step++)
	{
		int how = g_fn.how[n], m = g_fn.next[n];
		if (how != 0)
		{
			float ex = FnCx(g_fn.ncell[m]), ez = FnCz(g_fn.ncell[m]);
			float dx = ex - px, dz = ez - pz;
			if (best < 0 || dx * dx + dz * dz < 1.0f)                   // the jump/fall is the next thing to do
			{
				best = m;
				if (how == 1 && dx * dx + dz * dz < 1.44f) *jump = true;
			}
			break;
		}
		bestTop = fmaxf(bestTop, g_fn.y[m]);
		float mdx = FnCx(g_fn.ncell[m]) - px, mdz = FnCz(g_fn.ncell[m]) - pz;
		bool close = mdx * mdx + mdz * mdz < 0.5625f;                  // within 0.75: aiming at it makes the stick flip about (dash breaks)
		if (!FnSegBlocked(ram, px, pz, FnCx(g_fn.ncell[m]), FnCz(g_fn.ncell[m]), bestTop, 0.15f)) { if (!close || best < 0) best = m; }
		else if (best >= 0) break;
		n = m;
	}
	if (best < 0) best = g_fn.next[s] >= 0 ? g_fn.next[s] : g;
	if (best == g) { *tx = gx; *tz = gz; }
	else { *tx = FnCx(g_fn.ncell[best]); *tz = FnCz(g_fn.ncell[best]); }
	g_aiLogFn = 1;
	return 1;
}
// Floor node feet at (x, feet, z) stand on (the highest floor at most 0.4 above the feet), -1 = no floor data.
static int SpikeoutFloorUnder(const UINT8 *ram, float x, float z, float feet)
{
	if (!FloorNavBuild(ram)) return -1;
	return FnNodeAt(x, z, feet + 0.4f - FN_STEP, false);
}
// Stuck pushing toward (tx,tz): the cells just ahead count as blocked for the rest of the stage.
static void FloorNavMarkBlocked(float px, float pz, float tx, float tz)
{
	if (!g_fn.ok) return;
	float dx = tx - px, dz = tz - pz, d = sqrtf(dx * dx + dz * dz);
	if (d < 0.05f) return;
	dx /= d; dz /= d;
	int c0 = FnCellAt(px, pz);
	for (float step = 0.5f; step <= 1.0f; step += 0.25f)
	{
		int c = FnCellAt(px + dx * step, pz + dz * step);
		if (c >= 0 && c != c0) { g_fn.learn[c] = g_ppcTraceFrame + 600; g_fn.goal = -1; }
	}
}
// ---------------------------------------------------------------------------------------------------------------
// our movement toward (tx,tz). If so, steer to its doorway center and push/Beat through to the other side.
static bool SpikeoutBlockingDoor(const UINT8 *ram, float px, float py, float pz, float tx, float tz, float *dxOut, float *dzOut, bool *pushOut)
{
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw < 0x10000 || dw >= 0x700000) return false;
	unsigned cnt = *(const UINT16 *) &ram[(dw + 0x800) ^ 2];
	if (cnt > 128) cnt = 0;
	float mvx = tx - px, mvz = tz - pz;
	if (mvx * mvx + mvz * mvz < 0.25f) return false;
	float bestD = 16.0f; bool found = false;
	for (unsigned i = 1; i <= cnt; i++)
	{
		UINT32 e = dw + 0x7EC + i * 0x18;
		UINT32 flg = *(const UINT32 *) &ram[e];
		if (!(flg & 0x40000000)) continue;                              // not active in this area
		if (!(flg & (0x00100000 | 0x00800000))) continue;               // not player-openable (enemy exit door) and not the lit exit
		if (flg & (0x04000000 | 0x00000400)) continue;                  // already opened by player or held open
		UINT32 dp = *(const UINT32 *) &ram[e + 4];
		const UINT8 *d = SpikeoutMapPtr(dp, 0x34);
		if (!d) continue;
		float dy = *(const float *) &d[0x0C] - py;
		if (fabsf(dy) > 3.0f) continue;                                 // another floor
		float cx, cz, hx, hz, lx, lz;
		if (!SpikeoutDoorGeom(ram, dp, &cx, &cz, &hx, &hz, &lx, &lz)) continue;
		float dcx = cx - px, dcz = cz - pz, d2 = dcx * dcx + dcz * dcz;
		if (d2 >= bestD) continue;
		// Only intercept if the door is roughly in front of where we are trying to go (or we are right at it)
		if (d2 > 2.25f && dcx * mvx + dcz * mvz <= 0.0f) continue;
		float nx = -lz, nz = lx;
		if (mvx * nx + mvz * nz < 0.0f) { nx = -nx; nz = -nz; }
		bestD = d2; found = true;
		if (d2 > 2.25f) { *dxOut = cx; *dzOut = cz; *pushOut = false; }
		else { *dxOut = cx + nx * 2.5f; *dzOut = cz + nz * 2.5f; *pushOut = true; }
	}
	return found;
}

// Nearest active door (any open/closed state, same floor) within maxD of (px,pz): id, flags, doorway centre and
// unit normal oriented toward (gx,gz). For the "no path" case: the floor grid treats a closed door as a wall, so
// the wall slide ran the bot up and down in front of it (05's start room, 11/08).
static int g_stDoorId = 0; static UINT32 g_stDoorFlg = 0; static float g_stDoorD = -1.0f;   // steer.txt
static int SpikeoutNearestDoor(const UINT8 *ram, float px, float py, float pz, float gx, float gz, float maxD, float *cxO, float *czO, float *nxO, float *nzO, UINT32 *flgO)
{
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw < 0x10000 || dw >= 0x700000) return 0;
	unsigned cnt = *(const UINT16 *) &ram[(dw + 0x800) ^ 2];
	if (cnt > 128) cnt = 0;
	int best = 0; float bestD = maxD * maxD;
	for (unsigned i = 1; i <= cnt; i++)
	{
		UINT32 e = dw + 0x7EC + i * 0x18;
		UINT32 flg = *(const UINT32 *) &ram[e];
		if (!(flg & 0x40000000)) continue;                              // not active in this area
		UINT32 dp = *(const UINT32 *) &ram[e + 4];
		const UINT8 *d = SpikeoutMapPtr(dp, 0x34);
		if (!d) continue;
		if (fabsf(*(const float *) &d[0x0C] - py) > 3.0f) continue;     // another floor
		float cx, cz, hx, hz, lx, lz;
		if (!SpikeoutDoorGeom(ram, dp, &cx, &cz, &hx, &hz, &lx, &lz)) continue;
		float d2 = (cx - px) * (cx - px) + (cz - pz) * (cz - pz);
		if (d2 >= bestD) continue;
		float l = sqrtf(lx * lx + lz * lz); if (!(l > 0.01f)) continue;
		float nx = -lz / l, nz = lx / l;
		if ((gx - cx) * nx + (gz - cz) * nz < 0.0f) { nx = -nx; nz = -nz; }   // toward the goal side
		bestD = d2; best = (int) i;
		*cxO = cx; *czO = cz; *nxO = nx; *nzO = nz; *flgO = flg;
	}
	return best;
}

// 11/13 swing doors: a swing door (dtype 0,1,2,5) whose doorway is within maxD and lies between us and the goal
// (we are not yet through, the goal is beyond it). The path search alone never settles there - the leaf turns as it
// is pushed, so the floor grid / wall path / wall slide take turns (02 in area 75's start room: ~400 frames 0.7-1.5
// from Door#3, wn 4/5/6 flipping, the door pass never asked for). Normal is oriented toward the goal.
static bool SpikeoutSwingDoorAhead(const UINT8 *ram, float px, float py, float pz, float gx, float gz, float maxD, float *cxO, float *czO, float *nxO, float *nzO)
{
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw < 0x10000 || dw >= 0x700000) return false;
	unsigned cnt = *(const UINT16 *) &ram[(dw + 0x800) ^ 2];
	if (cnt > 128) cnt = 0;
	float bestD = maxD * maxD; bool any = false;
	for (unsigned i = 1; i <= cnt; i++)
	{
		UINT32 e = dw + 0x7EC + i * 0x18;
		UINT32 flg = *(const UINT32 *) &ram[e];
		if (!(flg & 0x40000000)) continue;                              // not active in this area
		UINT32 dp = *(const UINT32 *) &ram[e + 4];
		const UINT8 *d = SpikeoutMapPtr(dp, 0x34);
		if (!d) continue;
		unsigned dtype = d[0x2D ^ 3];
		if (!(dtype <= 2 || dtype == 5)) continue;                      // swing doors only
		if (fabsf(*(const float *) &d[0x0C] - py) > 3.0f) continue;     // another floor
		float cx, cz, hx, hz, lx, lz;
		if (!SpikeoutDoorGeom(ram, dp, &cx, &cz, &hx, &hz, &lx, &lz)) continue;
		float d2 = (cx - px) * (cx - px) + (cz - pz) * (cz - pz);
		if (d2 >= bestD) continue;
		float l = sqrtf(lx * lx + lz * lz); if (!(l > 0.01f)) continue;
		float nx = -lz / l, nz = lx / l;
		float gs = (gx - cx) * nx + (gz - cz) * nz;
		if (gs < 0.0f) { nx = -nx; nz = -nz; gs = -gs; }               // toward the goal side
		if (gs < 0.8f) continue;                                        // goal not beyond the door
		float ps = (px - cx) * nx + (pz - cz) * nz;
		if (ps > 0.3f) continue;                                        // already through
		float w = *(const float *) &d[0x14];
		// No real leaf (width under 0.5): its "normal" means nothing. Area 6 Door#16 (dtype 1, width 0, hinge 18.14/11.94,
		// normal +-x) - the way through is +z at (18.8, 15.0); pushing along x sent all 8 bots into the wall (11/13).
		if (!(fabsf(w) >= 0.5f)) continue;
		float lat = fabsf((px - cx) * nz - (pz - cz) * nx);
		if (lat > 0.5f * fabsf(w) + 1.5f) continue;                    // well off to the side of the doorway
		bestD = d2; any = true;
		*cxO = cx; *czO = cz; *nxO = nx; *nzO = nz;
	}
	return any;
}

// Auto start / continue: should this bot be pressing coin, start and beat right now? The presence mask
// 0x14002C (MSB = node 1) only holds nodes in this node's group, and groups split by game mode, so a
// node still in attract (mode 0x10402C = 6) sees a mask without node 1 once node 1 has started (measured:
// 0 in attract, then 7000000x on node 2). (11/11: "nodes that start without node 1 hang" was the area 95
// intro stall, fixed by the 0x5BD5C patch - any node can host now.) A bot that dies leaves the mask (it
// goes to 0 on 8-link, which also never shows mode 6), so continuing can't rely on the mask: keep pressing
// for 30s from when it died, then give up.
// "Has a human started?" (11/16 night, owner: no external files). Before, human cabinets wrote
// ../human_state_0N.txt and the bots read it (same PC only). Now it goes over the link itself:
//  - every cabinet's own link record (0x140100, 0x300 bytes) is copied to every other cabinet each frame; the
//    other cabinets' records are at *(0x150060 + 4n), n = 1..total-1. +4 is the link state: 1 title / attract,
//    2 character select, 4 in a game (the attract demo is 1, so it never counts as "started").
//  - +0x230..+0x2FF of the record is never used by the game (always 0 in 8 cabinets x 2 save states, no code
//    touches it) and does reach the others (checked: a byte poked at 0x1403F0 on 01 arrived at 02 every frame).
//  - so every frame a bot cabinet writes SPIKEOUT_BOT_MARK at its own +0x2FC and a human cabinet writes 0 there
//    (the game clears the record now and then, e.g. at link start; the next frame puts it back).
//  - a bot treats "some other cabinet in state 2 or 4 without the bot mark" as a human that has started.
// Works across PCs too (the file did not). Continuing after the bot's own death doesn't depend on it.
#define SPIKEOUT_BOT_MARK 0x424F5421u   // "BOT!"
// 11/13 FloorDump=1 (investigating the vanishing floor on 05-08): a coin on any cabinet writes
// ../floordump_req.txt = (sync counter 0x1000A8) + 20; every cabinet polls it and, when its own sync
// counter reaches that value (the same game moment on all linked cabinets), writes floordump_<target>.bin
// (RAM, the emulator-side task shadow, culling/polygon RAM). Save states are not used: data moved out of
// RAM by the 8-player work is not in them. If the counter never gets there (not in a linked game) the
// dump is written anyway after 180 frames.
static bool g_floorDump = false;
static UINT32 g_floorDumpTarget = 0;
#define SPIKEOUT_FLOORDUMP_REQ "../floordump_req.txt"
static void SpikeoutFloorDumpRequest(const UINT8 *ram)
{
	UINT32 target = *(const UINT32 *) &ram[0x1000A8] + 20;
	FILE *f = fopen(SPIKEOUT_FLOORDUMP_REQ, "w");
	if (f) { fprintf(f, "%u\n", target); fclose(f); }
}
static bool SpikeoutFloorDumpDue(const UINT8 *ram)
{
	static bool first = true; static UINT32 pending = 0, done = 0, seenFrame = 0;
	if ((g_ppcTraceFrame % 3) == 0 || first)
	{
		unsigned t = 0;
		FILE *f = fopen(SPIKEOUT_FLOORDUMP_REQ, "r");
		if (f) { if (fscanf(f, "%u", &t) != 1) t = 0; fclose(f); }
		if (first) { done = t; first = false; }			// a request left over from an earlier run
		else if (t && t != done && t != pending) { pending = t; seenFrame = g_ppcTraceFrame; }
	}
	if (!pending) return false;
	UINT32 sync = *(const UINT32 *) &ram[0x1000A8];
	if (sync >= pending || g_ppcTraceFrame - seenFrame > 180)
	{
		g_floorDumpTarget = pending; done = pending; pending = 0;
		return true;
	}
	return false;
}

// 11/17 (owner): the games running in the link, for the lobby / the Discord post ("GAME A: STAGE 7, area 75, 6").
// Every cabinet's link record reaches every cabinet: +4 link state (4 = in a game), +0x3C game number (the games of
// GAME SELECT are grouped by it, 0x4AD08), +0x3E stage (= that cabinet's 0x10402C), +0x3F area (= its 0x10402D).
// Own record 0x140100, the others *(0x150060 + 4n), n = 1..total-1, cabinet = self - n (ring). Once a second the
// cabinets in a game are grouped by game number, ordered by their lowest cabinet number (A, B, ...), and kept as
// "stage,area,players;stage,area,players..." ("" = no game). Read by the Firebase status writer (FirebaseMatchingCpp).
static std::mutex g_sessLock;
static std::string g_sessSummary;
std::string SpikeoutSessionSummary()
{
	std::lock_guard<std::mutex> lock(g_sessLock);
	return g_sessSummary;
}
static void SpikeoutSessionTick(const UINT8 *ram)
{
	if (!g_spikeofe8task || (g_ppcTraceFrame % 60) != 0) return;
	unsigned total = ram[0x10065A ^ 3], self = ram[0x10065B ^ 3];
	if (total < 1 || total > 8 || self < 1 || self > total) return;
	struct Game { unsigned id, stage, area, players, firstNode; } games[8];
	int ng = 0;
	for (unsigned n = 0; n < total; n++)
	{
		UINT32 rec = 0x140100;
		if (n)
		{
			rec = *(const UINT32 *) &ram[0x150060 + 4 * n];
			if (rec < 0x150000 || rec > 0x160000 - 0x300 || (rec & 3)) continue;
		}
		if (ram[(rec + 4) ^ 3] != 4) continue;
		const unsigned id = *(const UINT16 *) &ram[(rec + 0x3C) ^ 2];
		const unsigned node = ((self - 1 + 8 * total - n) % total) + 1;
		int g = 0;
		while (g < ng && games[g].id != id) g++;
		if (g == ng) { games[ng++] = { id, ram[(rec + 0x3E) ^ 3], ram[(rec + 0x3F) ^ 3], 0, node }; }
		games[g].players++;
		if (node < games[g].firstNode) { games[g].firstNode = node; games[g].stage = ram[(rec + 0x3E) ^ 3]; games[g].area = ram[(rec + 0x3F) ^ 3]; }
	}
	std::sort(games, games + ng, [](const Game &a, const Game &b) { return a.firstNode < b.firstNode; });
	std::string out;
	for (int g = 0; g < ng; g++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%s%u,%u,%u", g ? ";" : "", games[g].stage, games[g].area, games[g].players);
		out += buf;
	}
	std::lock_guard<std::mutex> lock(g_sessLock);
	if (out != g_sessSummary)
		InfoLog("Session summary: \"%s\" (own link state %u)", out.c_str(), (unsigned) ram[0x140104 ^ 3]);
	g_sessSummary = out;
}

static void SpikeoutP1StateTick(UINT8 *ram)   // every frame: mark (bot) or unmark (human) our own link record
{
	if (!g_spikeofe8task || g_satClone.on) return;   // 11/20: the satellite clone keeps the followed node's mark
	bool bot = g_spikeoutAI && !g_streamHumanJoined.load();   // a streaming client that took this slot is a human
	*(UINT32 *) &ram[0x140100 + 0x2FC] = bot ? SPIKEOUT_BOT_MARK : 0;
}
static bool SpikeoutP1Started(const UINT8 *ram)   // a human on another linked cabinet has started
{
	unsigned total = ram[0x10065A ^ 3];
	if (total > 8) total = 8;
	for (unsigned n = 1; n < total; n++)
	{
		UINT32 rec = *(const UINT32 *) &ram[0x150060 + 4 * n];
		if (rec < 0x150000 || rec > 0x160000 - 0x300 || (rec & 3)) continue;   // the records live at 0x150400 + k*0x300
		unsigned st = ram[(rec + 4) ^ 3];
		UINT32 mark = *(const UINT32 *) &ram[rec + 0x2FC];
		if ((st == 2 || st == 4) && mark != SPIKEOUT_BOT_MARK) return true;
	}
	return false;
}
// 11/21 (owner): every cabinet a bot -> cabinet 01 starts on its own, the other bots join once it has started. "Every
// cabinet a bot" = every other cabinet's link record is there and carries the bot mark (a LOCAL cabinet, or a STREAM
// slot - AIPlayer 0, no mark - makes it false: then the bots wait for that human as before). Held 3 s in a row before
// 01 presses Start (the records of the others arrive a little after the link comes up).
static bool SpikeoutAllBots(const UINT8 *ram)
{
	unsigned total = ram[0x10065A ^ 3];
	if (total > 8) total = 8;
	for (unsigned n = 1; n < total; n++)
	{
		UINT32 rec = *(const UINT32 *) &ram[0x150060 + 4 * n];
		if (rec < 0x150000 || rec > 0x160000 - 0x300 || (rec & 3)) return false;   // not there yet
		if (*(const UINT32 *) &ram[rec + 0x2FC] != SPIKEOUT_BOT_MARK) return false;
	}
	return true;
}
static bool SpikeoutAnyStarted(const UINT8 *ram)   // another cabinet (bot or not) is at the character select / in a game
{
	unsigned total = ram[0x10065A ^ 3];
	if (total > 8) total = 8;
	for (unsigned n = 1; n < total; n++)
	{
		UINT32 rec = *(const UINT32 *) &ram[0x150060 + 4 * n];
		if (rec < 0x150000 || rec > 0x160000 - 0x300 || (rec & 3)) continue;
		unsigned st = ram[(rec + 4) ^ 3];
		if (st == 2 || st == 4) return true;
	}
	return false;
}
// 11/24 (owner): every LINKED cabinet a bot (a 4-link looks at 01-04 only: the ring's own count) -> each bot keeps
// watching (only while it is out of a game - this is asked from the attract / title only) whether a game is there:
// a game (another cabinet at the character select / in a game) -> join it; no game for a while -> start one. Each
// bot starts after 3 s of "no game" (the records of the others arrive a little after the link comes up), all at
// the same time - separate cabinets starting together make one game (owner, 11/24: no stagger). With any cabinet that is not a bot in the link, the bots only
// join once such a cabinet has started (SpikeoutP1Started, as before).
static bool SpikeoutAllBotStart(const UINT8 *ram, unsigned node)
{
	static UINT32 since = 0;
	if (!SpikeoutAllBots(ram)) { since = 0; return false; }
	if (SpikeoutAnyStarted(ram)) { since = 0; return true; }   // a game is there: join it
	if (!since) since = g_ppcTraceFrame ? g_ppcTraceFrame : 1;
	(void) node;
	return g_ppcTraceFrame - since >= 180u;   // no game for 3 s: start (every bot at once - they make one game)
}

static bool SpikeoutAIWantsStart(const UINT8 *ram)
{
	static bool played = false;
	static UINT32 deadSince = 0;
	unsigned node = ram[0x10065B ^ 3];
	if (node < 1 || node > 8) return false;   // 01 too when it is an AI (11/09)
	if (ram[0x10402D ^ 3] == SPIKEOUT_AREA_GATE_PASSAGE)   // gate passage (area 36): no continue either - and the
	{                                                      // 30s continue window is paused, not used up meanwhile
		static UINT32 lastFrozen = 0;
		if (deadSince && g_ppcTraceFrame != lastFrozen) deadSince++;
		lastFrozen = g_ppcTraceFrame;
		return false;
	}
	UINT32 mask = *(const UINT32 *) &ram[0x14002C];
	unsigned mode = ram[0x10402C ^ 3];
	bool hasSelf = (mask & (0x80000000u >> (node - 1))) != 0;
	if (SpikeoutAttract(ram))   // attract (stage id 6 outside a game; the real Shipyard is also 6)
	{
		played = false;
		return hasSelf && (SpikeoutP1Started(ram) || SpikeoutAllBotStart(ram, node));   // a human on any cabinet has started (11/11: was 01 only; 11/16: from the link records, no file); 11/21: all bots -> 01 starts
	}
	UINT32 rob = 0x1A0000 + (node - 1) * 0x4000;
	int energy = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2];
	if ((*(const UINT32 *) &ram[rob + 0x00] & 1) && energy > 0 && SpikeoutInStage(ram))   // alive AND in a live stage (ignore stale Rob during attract/join)
	{
		if (hasSelf) played = true;
		deadSince = 0;
		return false;
	}
	if (played)                                   // was in play
	{
		// 11/22 (user; ai.txt 03 17:45): TIME OVER - our own timer ran out with HP left (460): the own link state went back
		// to 1 (title) while the Rob still shows us alive, and we waited for ever. Own link state 1 after playing = our game
		// is over: continue (rejoin) as after a death, our own stale Rob not counted as "anyone".
		const bool timeOver = ram[0x140104 ^ 3] == 1;
		if (energy > 0 && !timeOver) { deadSince = 0; return false; }   // still has HP: an area-move cutscene / gate transition / brief invuln, NOT a death - just wait, don't mash continue
		if (!deadSince) deadSince = g_ppcTraceFrame;        // real death (HP gone) or time over
		// Continue while anyone (01-08) is still in the game session (11/09): a player alive in a stage. With
		// nobody left, the old 30s window from our death.
		bool anyone = false;
		if (!SpikeoutAttract(ram) && mode >= 1 && mode <= 7 && mode != 2)
		{
			unsigned pcnt = ram[0x10065A ^ 3]; if (pcnt > 8) pcnt = 8;
			for (unsigned i = 0; i < pcnt && !anyone; i++)
			{
				if (timeOver && i == node - 1) continue;   // 11/22: our own Rob is stale after a time over
				UINT32 r = 0x1A0000 + i * 0x4000;
				if ((*(const UINT32 *) &ram[r] & 1) && (short) *(const UINT16 *) &ram[(r + 0x3F02) ^ 2] > 0) anyone = true;
			}
		}
		if (anyone || g_ppcTraceFrame - deadSince < 1800) return true;
		played = false;
		return false;
	}
	return hasSelf && (SpikeoutP1Started(ram) || SpikeoutAllBotStart(ram, node));   // join only once a human has started (the mask alone is just "link up"); 11/21: or all bots
}

static UINT32 g_aiContT0 = 0;    // 11/15: start frame of the current continue attempt (0 = not continuing); the coin / Start timing
static int g_aiLogTarget = -1;   // for ai.txt
// 11/25 (owner): the attacks are gone - the bot walks in to 1.5 (engaged out to 2.5) and only turns to its target there.
// 11/17 (user, log 18:31): B-B mostly went out at 2.0-2.5 and missed (hit 6-18% there, up to 40% nearer) - walk in to
// 1.5 (was 2.5), the attack range held out to 2.5 (was 3.5).
// 11/25 (owner, 2nd): the attack range is 1 (in and out) - the bot walks in to 1, faces it, then Shift + b-b (below).
static int g_aiBotSt = 0; static UINT32 g_aiBotStF = 0;   // 11/25: the attack states 0 / 1 / 2 (back from before today)
static bool g_aiDnKeep = false;    // 11/25: our downed target is kept (no other enemy within 3) - see the target search
static int g_aiBossDownSlot = -1;   // 11/25: our boss target that is down (kept while it is down / getting up)
static bool g_aiBossDownWait = false;  // 11/25: waiting for that boss to get up (no other enemy within 2)
static bool g_aiForceC2 = false;   // 11/25: the evade asks for B+C - B now
static bool g_aiAfterEv = false;   // 11/25: the evade just ended - the next combo pick is 50% combo 2 (B+C - B)
static bool g_aiRiseHold = false;  // 11/25: C held while that target gets up (rise)
static UINT32 g_aiBossDashUntil = 0;   // 11/25: dashing away from a boss after us that turned MUT
static UINT32 g_aiSpecNext = 0;    // 11/13: next frame a special may be used again (3 s after the last); 11/25: grab specials only
static const float AI_ENG_IN2 = 4.0f, AI_ENG_OUT2 = 4.0f, AI_FACE30 = 0.5236f;   // 2^2, 2^2, 30 deg (11/25: the range by the target 2; was 3, 4, 2)
static bool g_aiTgtOnMe = false;  // 11/13: our target was picked because it is after us (enemy +0x32E4 = our Rob); steer.txt onme=
static bool g_aiItemSeek = false;   // 11/17: going for an item this frame (top priority: no back-off, no boss escape over it)
static bool g_aiWeaponPress = false;   // 11/17: standing on a weapon to pick up: B+C (pulsed) this frame
// 11/17 (user, log 23:02): chairs / tables are there from the start and can't be stood on, only picked up - the bots
// stopped 0.6-1.4 from them and never got the B+C in (it waited for 0.6 / 0.8). The human picked things from 0.6-1.3.
// Close enough to press B+C: within 1.3, or within 2.0 and no nearer for 18 frames (bumped into it). Per caller (k).
static bool SpikeoutPickReach(int k, int idx, float d2, float ox, float oz)
{
	// 4th (user: "it moves in jerks - just walk straight at it and press"): the press 24 / walk 18 turns and the
	// stalled / pushing checks are gone. Within 2.0 the caller keeps the lever on the thing and pulses B+C.
	// 5th (11/18, user): the pick-up input is lever DOWN + B+C standing on the thing, whatever the camera. Within 1.3
	// (the human took them from 0.6-1.3): the press frames send DOWN + B+C, the others walk at it.
	(void) k; (void) idx; (void) ox; (void) oz;
	return d2 < 1.69f;
}
// Height check in attack range: an enemy within 1.5 units on the ground plane but more than AI_HEIGHT_GAP
// above or below us (another floor, a ledge, the stairs' other end) can't be hit. Held for
// AI_HEIGHT_HOLD frames (so a jump doesn't count), it is dropped as a target for AI_HEIGHT_EXCL frames (10 s).
#define AI_HEIGHT_GAP   2.0f
#define AI_HEIGHT_HOLD  15u
#define AI_HEIGHT_EXCL  600u
static UINT32 g_aiHeightExclUntil[20];
// 11/11: only enemies on another level left (none we can reach). Then, in an area with a wait spot, go and wait
// there instead of standing under them, so they come down to us (area 47: tall room, enemies stay right above).
static bool g_aiOtherLevelOnly = false;
struct SpikeoutWaitSpot { unsigned area; float x, z; };
static const SpikeoutWaitSpot s_waitSpots[] = {
	{ 47, -49.557f, -66.624f },   // waypoint recorded 11/11 (y -5.87)
	{ 41, -49.557f, -66.624f },   // 11/15: area 41 = the same room as 47 (same scripts 0xBCD4C / 0xBCE20)
};
static float g_aiLogDy = 0.0f;
static float g_aiLogPy = 0.0f;      // ai.txt py=: own height
// steer.txt (AIPlayerLog=1, every frame in play, 11/08): camera forward, the world direction asked for, the
// stick bits sent and the move actually made - to see whether the lever goes the wrong way when the camera swings.
static bool g_stValid = false;
static float g_stFx = 0, g_stFz = 0, g_stDx = 0, g_stDz = 0, g_stTx = 0, g_stTz = 0;
static float g_stCamY = 0, g_stCamM = 0, g_stCamPitch = 0;   // 11/13 steer.txt: camera y, horizontal length of (interest - camera), pitch (deg, - = looking down)
static int g_stFstate = 0, g_stStucks = 0, g_stMode = 0, g_stGate = 0, g_stDash = 0;
static UINT32 g_stMyAct = 0; static float g_stFaceErr = -1.0f;   // 11/17 steer.txt: own action (Rob +0x38), target off our facing (deg, -1 unknown)
static UINT32 g_wpJumpUntil = 0;   // area 53 route: big jump (dash + Jump) at the last point (11/08)
// Jump-kick spots (11/08): every line of ../waypoints.txt (the file the coin-press recorder appends to) is a spot
// where the bots do the running big jump kick - recorded at doors / gates they miss or can't open. The file is
// re-read every second, so a spot recorded in play works straight away. Area 53 (its own climb route) is left out.
// A spot is used: (a) when the bot jump-kicks a gate / door within 6 of it - the kick goes at the spot instead;
// (b) with no enemy and nothing else to do in its area, on our level (within 4 in y) - the bot goes there and
// kicks. Done for this visit to the area once the area flags change during the kicks there.
// Gate passed = the camera changed (11/09): once a bot has jump-kicked a gate / door, the first change of its
// camera (camar or camsar) while it is within 8 of that gate means it went through - measured: area 75 Door#1,
// 86 Door#13, area 4 Door#9 / #45, area 6 Door#16, 150-500 frames after the kick. That gate is then done for this
// visit to the area (no more going back to it); reset on an area change. The game's own flags still count too.
static unsigned g_cpArea = ~0u, g_cpCam = ~0u; static float g_cpKx = 1e9f, g_cpKz = 1e9f; static bool g_cpKicked = false;
static float g_cpBx = 0.0f, g_cpBz = 0.0f;   // where we stood at the kick (11/10: passed = past the gate, see below)
static std::vector<std::pair<float, float> > g_cpPassed;
static bool SpikeoutGatePassedCam(float x, float z)
{
	for (const std::pair<float, float> &p : g_cpPassed)
		if ((p.first - x) * (p.first - x) + (p.second - z) * (p.second - z) < 25.0f) return true;
	return false;
}
struct SpikeoutWpKick { unsigned area; float x, y, z; };
static std::vector<SpikeoutWpKick> g_wpk;
// Mall area 44 hidden route (11/10): when the pick is the heavy door D#5 (the other way is the A+B gate wall at
// S#12), the bots follow the area 44 lines of ../waypoints.txt in file order: Beat Door#11/#12 open (it takes two
// hits) at the point nearest them, walk on through the auto doors #9/#10, and from the last point the usual jump kick
// at D#5. No area 44 points recorded: straight at D#5 as before.
// 11/11: hard-coded from the three area 44 points recorded on 11/11 (file area 44 lines are no longer used).
static const SpikeoutWpKick s_r44Fixed[] = {
	{ 44, -20.908f, -6.930f,  2.818f },   // in front of Door#11/#12 (Beat them open)
	{ 44, -12.048f, -6.838f,  8.582f },   // on through the auto doors #9/#10
	{ 44,  -6.347f, -7.079f, -8.510f },   // last point: jump kick at D#5
};
static std::vector<SpikeoutWpKick> g_r44(s_r44Fixed, s_r44Fixed + sizeof(s_r44Fixed) / sizeof(s_r44Fixed[0]));
static std::vector<bool> g_wpkDone;
static UINT32 g_wpkChkF = ~0u; static unsigned g_wpkArea = ~0u;
static std::vector<char> g_wpkRaw;
static void SpikeoutWpKickLoad()
{
	if (g_wpkChkF != ~0u && g_ppcTraceFrame - g_wpkChkF < 60) return;
	g_wpkChkF = g_ppcTraceFrame;
	std::vector<char> raw;
	if (FILE *f = fopen(SPIKEOUT_WAYPOINT_FILE, "rb"))
	{
		char buf[4096]; size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && raw.size() < (1u << 20)) raw.insert(raw.end(), buf, buf + n);
		fclose(f);
	}
	if (raw == g_wpkRaw) return;
	g_wpkRaw = raw;
	std::vector<SpikeoutWpKick> v;
	raw.push_back(0);
	for (const char *p = raw.data(); *p; )
	{
		const char *e = strchr(p, '\n');
		SpikeoutWpKick w;
		if (sscanf(p, "area=%u x=%f y=%f z=%f", &w.area, &w.x, &w.y, &w.z) == 4 && w.area != 53 && w.area != 44 && w.area != 47) v.push_back(w);
		if (!e) break;
		p = e + 1;
	}
	g_wpk = v; g_wpkDone.assign(v.size(), false);
	printf("[WPKICK] %u spot(s) loaded, area 44 route %u point(s)\n", (unsigned) v.size(), (unsigned) g_r44.size());
}

// 11/26 (owner): area 40 marathon mode - see the target search in SpikeoutAIInputBits.
static bool g_marOn = false; static float g_marX = 0.0f, g_marZ = 0.0f;
static int g_aiLogA75 = 0;   // for ai.txt: 0 none, 2..5 start-room Door#2..5, 9 going to the area's exit (ROM table), 10 at it
static int g_aiLogEhp = -1;
static UINT32 g_aiLogArflg = 0;   // for ai.txt: area flags (0x60 = gate open)   // for ai.txt: enemy slot assigned to this bot (-1 = none: shares its nearest)
// gatelog.txt (AIPlayerLog=1, 11/09): every change of a door's or shutter's state bits, the area flags and the
// camera (camar / camsar, with our position) on this cabinet, to see
// whether a door opened by one node shows as open on the others (thought: nodes 0-3 sync, 4-7 don't).
// Doors: *(0x10411C)+0x7EC+id*0x18 - 0x40000000 placed, 0x00800000 arrow lit, 0x04000000 gone through, 0x400 held open.
// Shutters: *(0x104514)+0x18+id*0x1C - 0x40000000 placed, 0x00020000 arrow lit, 0x00040000 attackable, 0x08000000 broken.
static void SpikeoutGateFlagLog(const UINT8 *ram, unsigned node)
{
	static UINT32 pd[130], ps[100]; static unsigned pArea = ~0u, pCamar = ~0u, pCamsar = ~0u; static UINT32 pArflg = 0; FILE *&f = g_gateLogF;   // 11/18: shared with SpikeoutGateNote
	unsigned area = ram[0x10402D ^ 3];
	UINT32 fs = *(const UINT32 *) &ram[0x10413C] & 0x7FFFFF;
	UINT32 arflg = (fs >= 0x10000 && fs < 0x7FF000) ? *(const UINT32 *) &ram[fs + 0x284] : 0;
	if (!f) { f = fopen("gatelog.txt", "w"); if (!f) return; }
	bool fresh = area != pArea;
	if (fresh || arflg != pArflg) fprintf(f, "f=%u id=%u area=%u arflg %08X -> %08X\n", g_ppcTraceFrame, node - 1, area, pArflg, arflg);
	UINT32 cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;   // camera: +0xC0 camar, +0xC1 camsar (11/09)
	if (cam >= 0x10000 && cam < 0x7FF000)
	{
		unsigned camar = ram[(cam + 0xC0) ^ 3], camsar = ram[(cam + 0xC1) ^ 3];
		if (fresh || camar != pCamar || camsar != pCamsar)
			fprintf(f, "f=%u id=%u area=%u camar %u -> %u camsar %u -> %u p=(%.2f,%.2f,%.2f)\n", g_ppcTraceFrame, node - 1, area,
			        pCamar, camar, pCamsar, camsar, *(const float *) &ram[0x1A0000 + (node - 1) * 0x4000 + 0x380],
			        *(const float *) &ram[0x1A0000 + (node - 1) * 0x4000 + 0x384], *(const float *) &ram[0x1A0000 + (node - 1) * 0x4000 + 0x388]);
		pCamar = camar; pCamsar = camsar;
	}
	UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
	if (dw >= 0x10000 && dw < 0x700000)
	{
		unsigned cnt = *(const UINT16 *) &ram[(dw + 0x800) ^ 2]; if (cnt > 128) cnt = 0;
		for (unsigned i = 1; i <= cnt; i++)
		{
			UINT32 v = *(const UINT32 *) &ram[dw + 0x7EC + i * 0x18];   // 11/10: every bit (was & 0x44800400)
			if (!fresh && v != pd[i]) fprintf(f, "f=%u id=%u area=%u Door#%u %08X -> %08X%s%s%s%s\n", g_ppcTraceFrame, node - 1, area, i, pd[i], v,
			                                  (v & ~pd[i] & 0x01000000u) ? " +SHINE" : "", (pd[i] & ~v & 0x01000000u) ? " -SHINE" : "",
			                                  (v & ~pd[i] & 0x00008000u) ? " +BROKEN" : "", (pd[i] & ~v & 0x00008000u) ? " -BROKEN" : "");
			pd[i] = v;
		}
	}
	UINT32 sw = *(const UINT32 *) &ram[0x104514] & 0x7FFFFF;
	if (sw >= 0x10000 && sw < 0x700000)
	{
		unsigned cnt = ram[(sw + 0x10) ^ 3]; if (cnt > 96) cnt = 0;
		for (unsigned i = 0; i < cnt; i++)
		{
			UINT32 v = *(const UINT32 *) &ram[sw + 0x18 + i * 0x1C];   // 11/10: every bit (was & 0x48060000)
			if (!fresh && v != ps[i]) fprintf(f, "f=%u id=%u area=%u Shutter#%u %08X -> %08X%s%s%s%s\n", g_ppcTraceFrame, node - 1, area, i, ps[i], v,
			                                  (v & ~ps[i] & 0x01000000u) ? " +SHINE" : "", (ps[i] & ~v & 0x01000000u) ? " -SHINE" : "",
			                                  (v & ~ps[i] & 0x00008000u) ? " +BROKEN" : "", (ps[i] & ~v & 0x00008000u) ? " -BROKEN" : "");
			ps[i] = v;
		}
	}
	// GW (11/10): gate walls (0x10000 with A 0x20000 / B 0x40000) whose flags change - to see a gate wall go off when
	// it is broken (SpikeoutGateWallOpen relies on it).
	{
		static UINT32 gwPrev[8192]; static UINT32 gwList = 0;
		UINT32 wt = WnWallList(ram);
		bool reinit = fresh || wt != gwList; gwList = wt;
		for (int i = 0; wt && i < 8192 && wt + (i + 1) * 4 <= 0x800000; i++)
		{
			UINT32 wp = *(const UINT32 *) &ram[wt + i * 4];
			if (wp == 0xFFFFFFFFu) break;
			wp &= 0x7FFFFF;
			if (wp < 0x10000 || wp + 0x2C > 0x800000) break;
			UINT32 fl = *(const UINT32 *) &ram[wp];
			if ((fl & 0x00010000u) && (fl & 0x00060000u) && !reinit && fl != gwPrev[i])
				fprintf(f, "f=%u id=%u area=%u GateWall#%d %08X -> %08X at (%.2f,%.2f)-(%.2f,%.2f)\n", g_ppcTraceFrame, node - 1, area, i, gwPrev[i], fl,
				        *(const float *) &ram[wp + 0x0C], *(const float *) &ram[wp + 0x14], *(const float *) &ram[wp + 0x18], *(const float *) &ram[wp + 0x20]);
			gwPrev[i] = fl;
		}
	}
	// SC (11/10): fa_scenario fields, to find "BREAK THE GATE", the screen arrow and "gate opened". Each change is
	// logged (new bits +, cleared bits -) with our position; the whole set once on entering an area (base).
	if (fs >= 0x10000 && fs < 0x7FF000)
	{
		static const struct { UINT16 off; const char *name; } scf[] =
		{
			{ 0x020, "flag0" }, { 0x024, "flag1" }, { 0x028, "flag2" }, { 0x02C, "flag3" },
			{ 0x030, "flag4" }, { 0x034, "flag5" }, { 0x038, "flag6" }, { 0x03C, "flag7" },
			{ 0x19C, "gflg" },
			{ 0x220, "G01-02" }, { 0x224, "G03-04" }, { 0x228, "G05-06" }, { 0x22C, "G07-08" },
			{ 0x230, "G09-10" }, { 0x234, "G11-12" }, { 0x238, "G13-14" }, { 0x23C, "G15-16" },
			{ 0x240, "gflgo" },
			{ 0x244, "a00-03" }, { 0x248, "a04-07" }, { 0x24C, "a08-11" }, { 0x250, "a12-15" },
			{ 0x288, "STflg" }, { 0x2A8, "gflgt" }, { 0x2D4, "DFSUM/ALara" }, { 0x39C, "DIcnd" },
		};
		const unsigned NSC = sizeof(scf) / sizeof(scf[0]);
		static UINT32 pv[32]; static bool scInit = false;
		const UINT32 me = 0x1A0000 + (node - 1) * 0x4000;
		float mx = *(const float *) &ram[me + 0x380], mz = *(const float *) &ram[me + 0x388];
		if (fresh || !scInit)
		{
			fprintf(f, "f=%u id=%u area=%u SC base", g_ppcTraceFrame, node - 1, area);
			for (unsigned k = 0; k < NSC; k++) { pv[k] = *(const UINT32 *) &ram[fs + scf[k].off]; fprintf(f, " %s=%08X", scf[k].name, pv[k]); }
			fprintf(f, "\n");
			scInit = true;
		}
		else for (unsigned k = 0; k < NSC; k++)
		{
			UINT32 v = *(const UINT32 *) &ram[fs + scf[k].off];
			if (v != pv[k])
				fprintf(f, "f=%u id=%u area=%u SC +%03X %s %08X -> %08X (+%08X -%08X) p=(%.2f,%.2f)\n", g_ppcTraceFrame, node - 1, area,
				        scf[k].off, scf[k].name, pv[k], v, v & ~pv[k], pv[k] & ~v, mx, mz);
			pv[k] = v;
		}
	}
	pArea = area; pArflg = arflg;
	fflush(f);
}
static UINT8 SpikeoutAIInputBits(const UINT8 *ram)   // returns active-high bits to press
{
	static unsigned fr = 0; fr++;
	g_aiItemSeek = false;   // 11/17: set again below by the item seek when it runs this call
	g_aiWeaponPress = false;
	unsigned node = ram[0x10065B ^ 3];
	if (g_spikeoutAILog && node >= 1 && node <= 8 && ram[0x10402D ^ 3]) SpikeoutGateFlagLog(ram, node);
	unsigned pcnt = ram[0x10065A ^ 3];            // player count: players are slots 0..pcnt-1,
	if (node < 1 || node > 8) return 0;           //   enemies start at slot pcnt (4 in 4-link, 8 in 8-link)
	{   // the area we came from (11/10, for area 74's gate): the last area before this one, the passage (36) skipped
		static unsigned curA = 0; unsigned a = ram[0x10402D ^ 3];
		if (a && a != SPIKEOUT_AREA_GATE_PASSAGE && a != curA) { g_spikeoutPrevArea = curA; curA = a; }
	}
	if (ram[0x10402D ^ 3] == SPIKEOUT_AREA_GATE_PASSAGE) return 0;   // gate passage (area 36): the AI stops thinking
	UINT32 rob = 0x1A0000 + (node - 1) * 0x4000;
	UINT32 pflags = *(const UINT32 *) &ram[rob + 0x00];
	float px = *(const float *) &ram[rob + 0x380];
	float pz = *(const float *) &ram[rob + 0x388];
	unsigned estart = (pcnt >= 1 && pcnt <= 8) ? pcnt : 4;
	float bestd = 1e30f, ex = 0, ez = 0; int found = 0, nact = 0, tSlot = -1;
	for (unsigned e = estart; e < estart + 12 && e < 20; e++)   // enemy Robs
	{
		UINT32 er = 0x1A0000 + e * 0x4000;
		if (!(*(const UINT32 *) &ram[er + 0x00] & 1)) continue;
		nact++;
		float cx = *(const float *) &ram[er + 0x380];
		float cz = *(const float *) &ram[er + 0x388];
		float dx = cx - px, dz = cz - pz, d = dx * dx + dz * dz;
		if (d < bestd) { bestd = d; ex = cx; ez = cz; found = 1; tSlot = (int) e; }
	}
	// 11/25 (owner, 3rd): the target is simply the nearest live enemy (Rob +0 bit 1, HP > 0). Left out only: one on another
	// level we could not get to (held out 10 s while it stays there, g_aiHeightExclUntil) and, in area 75's start room,
	// anything but a boss until we have been within 1 of a live boss (g_a75Near) - the area's own rule, kept.
	{
		const float myY = *(const float *) &ram[rob + 0x384];
		auto hExcl = [&](int sl) { return sl >= 0 && sl < 20 && g_ppcTraceFrame < g_aiHeightExclUntil[sl] &&
			fabsf(*(const float *) &ram[0x1A0000 + (unsigned) sl * 0x4000 + 0x384] - myY) > AI_HEIGHT_GAP; };   // 11/11: came down -> fair game again
		if (!SpikeoutA75BossOnly(ram)) g_a75Near = false;
		else if (!g_a75Near)
			for (unsigned e9 = estart; e9 < 20 && !g_a75Near; e9++)
			{
				const UINT32 er = 0x1A0000 + e9 * 0x4000;
				if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0 || ram[(er + 0x73) ^ 3] < 4) continue;
				const float qx = *(const float *) &ram[er + 0x380] - px, qz = *(const float *) &ram[er + 0x388] - pz;
				if (qx * qx + qz * qz < 1.0f)
				{
					g_a75Near = true;
					SpikeoutGateNote(g_ppcTraceFrame, "[A75] node %u: within 1 of boss E%u - small fry are targets from now on\n", node, e9);
				}
			}
		const bool a75BossOnly = SpikeoutA75BossOnly(ram) && !g_a75Near;
		// 11/25 (owner, 4th): the pairing from before today back. Every cabinet sees the same game state, so each works out the
		// same player <-> enemy pairs for EVERYONE: the closest free player and free enemy are paired, again and again (last
		// frame's pairs count at a quarter of the distance, so they don't flip); players left over take the enemy nearest to
		// them. Two players don't pile onto one enemy while another stands free. All live players and enemies are paired (the
		// same on every cabinet); our own rules come after it.
		static int prevAssign[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		int plist[8], np = 0, elist[12], ne = 0;
		float ppx[8], ppz[8], eex[12], eez[12];
		for (unsigned p = 0; p < estart && p < 8; p++)
		{
			const UINT32 pr = 0x1A0000 + p * 0x4000;
			if (!(*(const UINT32 *) &ram[pr] & 1) || (short) *(const UINT16 *) &ram[(pr + 0x3F02) ^ 2] <= 0) continue;
			ppx[np] = *(const float *) &ram[pr + 0x380]; ppz[np] = *(const float *) &ram[pr + 0x388]; plist[np++] = (int) p;
		}
		for (unsigned e = estart; e < estart + 12 && e < 20; e++)
		{
			const UINT32 er = 0x1A0000 + e * 0x4000;
			if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
			eex[ne] = *(const float *) &ram[er + 0x380]; eez[ne] = *(const float *) &ram[er + 0x388]; elist[ne++] = (int) e;
		}
		int assign[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };   // per player slot: enemy slot
		bool pUsed[8] = { false }, eUsed[12] = { false };
		// 11/25 (owner): no pair with an enemy 8 or more further than that player's nearest enemy (log, area 86: two bots were
		// paired with enemies 22-24 away while one stood 2-4 from them). Such a player is left over (-> its nearest enemy).
		float pNear[8];
		for (int i = 0; i < np; i++)
		{
			pNear[i] = 1e30f;
			for (int j = 0; j < ne; j++)
			{
				const float dx = eex[j] - ppx[i], dz = eez[j] - ppz[i], d = sqrtf(dx * dx + dz * dz);
				if (d < pNear[i]) pNear[i] = d;
			}
		}
		for (int k = 0; k < np && k < ne; k++)
		{
			float bd2 = 1e30f; int bp = -1, be = -1;
			for (int i = 0; i < np; i++)
			{
				if (pUsed[i]) continue;
				for (int j = 0; j < ne; j++)
				{
					if (eUsed[j]) continue;
					float dx = eex[j] - ppx[i], dz = eez[j] - ppz[i], d = dx * dx + dz * dz;
					if (sqrtf(d) >= pNear[i] + 8.0f) continue;   // 11/25: too far beyond its nearest

					if (prevAssign[plist[i]] == elist[j]) d *= 0.25f;
					if (d < bd2) { bd2 = d; bp = i; be = j; }
				}
			}
			if (bp < 0) break;
			pUsed[bp] = true; eUsed[be] = true; assign[plist[bp]] = elist[be];
		}
		for (int i = 0; i < np; i++)                          // left over: the nearest enemy
		{
			if (pUsed[i] || ne == 0) continue;
			float bd2 = 1e30f; int be = -1;
			for (int j = 0; j < ne; j++)
			{
				float dx = eex[j] - ppx[i], dz = eez[j] - ppz[i], d = dx * dx + dz * dz;
				if (prevAssign[plist[i]] == elist[j]) d *= 0.25f;
				if (d < bd2) { bd2 = d; be = j; }
			}
			if (be >= 0) assign[plist[i]] = elist[be];
		}
		for (int i = 0; i < 8; i++) prevAssign[i] = assign[i];
		// 11/25 (owner, 4th): the lock from before today (only this part): an enemy held by another player (its mot_kind2
		// tsukamare 0x00100000) is left to them - unless we hold someone and it is within 2 of us (then it is ours).
		const bool meHold = (*(const UINT32 *) &ram[rob + 0x48] & 0x00200000u) != 0;
		auto locked = [&](int sl) {
			const UINT32 er = 0x1A0000 + (unsigned) sl * 0x4000;
			if (!(*(const UINT32 *) &ram[er + 0x48] & 0x00100000u)) return false;
			const float qx = *(const float *) &ram[er + 0x380] - px, qz = *(const float *) &ram[er + 0x388] - pz;
			return !(meHold && qx * qx + qz * qz < 4.0f); };
		// ours: the paired one if we can go for it; else the nearest one we can (as before: not on another level we could not
		// get to, area 75's bosses-only rule; now also not locked)
		auto distOf = [&](int sl) { const UINT32 er = 0x1A0000 + (unsigned) sl * 0x4000;
			const float qx = *(const float *) &ram[er + 0x380] - px, qz = *(const float *) &ram[er + 0x388] - pz; return qx * qx + qz * qz; };
		auto usable = [&](int sl) { return !(a75BossOnly && ram[(0x1A0000 + (unsigned) sl * 0x4000 + 0x73) ^ 3] < 4) && !hExcl(sl) && !locked(sl); };
		int mine = -1; float bd = 1e30f; bool anyHigh = false;
		const int paired = assign[node - 1];
		if (paired >= 0 && usable(paired)) { mine = paired; bd = distOf(paired); }
		else
			for (int j = 0; j < ne; j++)
			{
				const int sl = elist[j];
				if (a75BossOnly && ram[(0x1A0000 + (unsigned) sl * 0x4000 + 0x73) ^ 3] < 4) continue;
				if (hExcl(sl)) { anyHigh = true; continue; }
				if (locked(sl)) continue;
				const float d = distOf(sl);
				if (d < bd) { bd = d; mine = sl; }
			}
		// 11/25 (owner): area 87 - with a live boss (class 4+) and 2 or fewer small fry left, the target is the boss (the
		// nearest one); but a small fry within 2 of us is fought first (the nearest such). Not held-by-another / other level.
		if (ram[0x10402D ^ 3] == 87)
		{
			int nFry = 0, boss = -1, fry2 = -1; float bossD = 1e30f, fryD = 4.0f;
			for (int j = 0; j < ne; j++)
			{
				const int sl = elist[j];
				const bool isBoss = ram[(0x1A0000 + (unsigned) sl * 0x4000 + 0x73) ^ 3] >= 4;
				const float d = distOf(sl);
				if (isBoss) { if (!hExcl(sl) && !locked(sl) && d < bossD) { bossD = d; boss = sl; } }
				else
				{
					nFry++;
					if (!hExcl(sl) && !locked(sl) && d < fryD) { fryD = d; fry2 = sl; }
				}
			}
			if (boss >= 0 && nFry <= 2)   // 11/25 (owner): 2 or fewer small fry left (back from "whatever the count")
			{
				if (fry2 >= 0) { mine = fry2; bd = fryD; }
				else { mine = boss; bd = bossD; }
			}
		}
		// 11/25 (owner): our target is down (mot_kind down 0x00020000 / down_pose 0x00008000) or getting up (rise 0x00010000)
		// and no other live enemy is within 3 of us -> it stays our target (no switch). Ends when it is up, dead or gone, or
		// another enemy comes within 3.
		{
			static int dnPrev = -1;
			g_aiDnKeep = false;
			if (dnPrev >= (int) estart && dnPrev < 20)
			{
				const UINT32 dr = 0x1A0000 + (unsigned) dnPrev * 0x4000;
				const bool alive = (*(const UINT32 *) &ram[dr] & 1) && (short) *(const UINT16 *) &ram[(dr + 0x3F02) ^ 2] > 0;
				const UINT32 dk = *(const UINT32 *) &ram[dr + 0x44];
				bool other3 = false;
				for (int j = 0; j < ne && !other3; j++) if (elist[j] != dnPrev && distOf(elist[j]) < 9.0f) other3 = true;
				if (alive && (dk & 0x00038000u) && !other3) { mine = dnPrev; bd = distOf(dnPrev); g_aiDnKeep = true; }
			}
			dnPrev = mine;
		}
		// 11/25 (owner): our target is a boss that is down (down 0x00020000 / down_pose 0x00008000 / rise 0x00010000): an
		// enemy other than a boss within 2 of us -> that one is the target (the nearest) and we fight it; none -> the boss
		// stays the target and we wait for it to get up (g_aiBossDownWait; the input part stands, then dashes away at its
		// rise). Remembered while the boss stays down (g_aiBossDownSlot), so a fight with the small fry does not lose it.
		{
			// 11/25 (owner, 2nd): "down" = (down or down_pose) and not air (0x08000000); kept on through its rise (0x00010000)
			auto bossDown = [&](int sl, bool keep) { if (sl < (int) estart || sl >= 20) return false; const UINT32 br = 0x1A0000 + (unsigned) sl * 0x4000;
				if (!(*(const UINT32 *) &ram[br] & 1) || (short) *(const UINT16 *) &ram[(br + 0x3F02) ^ 2] <= 0 || ram[(br + 0x73) ^ 3] < 4) return false;
				const UINT32 k = *(const UINT32 *) &ram[br + 0x44];
				return ((k & 0x00028000u) && !(k & 0x08000000u)) || (keep && (k & 0x00010000u)); };
			if (!bossDown(g_aiBossDownSlot, true)) g_aiBossDownSlot = -1;
			if (g_aiBossDownSlot < 0 && bossDown(mine, false)) g_aiBossDownSlot = mine;
			g_aiBossDownWait = false;
			if (g_aiBossDownSlot >= 0)
			{
				int fry = -1; float fd = 4.0f;
				for (int j = 0; j < ne; j++)
				{
					const int sl = elist[j];
					if (ram[(0x1A0000 + (unsigned) sl * 0x4000 + 0x73) ^ 3] >= 4 || hExcl(sl) || locked(sl)) continue;
					const float d = distOf(sl);
					if (d < fd) { fd = d; fry = sl; }
				}
				if (fry >= 0) { if (mine != fry) g_aiDnKeep = false; mine = fry; bd = fd; }
				else { if (mine != g_aiBossDownSlot) g_aiDnKeep = false; mine = g_aiBossDownSlot; bd = distOf(mine); g_aiBossDownWait = true; }
				if (mine == g_aiBossDownSlot) g_aiDnKeep = false;   // no C hold at a boss's get-up: we dash away instead
			}
		}
		g_aiOtherLevelOnly = mine < 0 && anyHigh;   // only enemies on another level left (the wait spots use it)
		g_aiTgtOnMe = false;
		found = 0;
		if (mine >= 0)
		{
			const UINT32 er = 0x1A0000 + (unsigned) mine * 0x4000;
			ex = *(const float *) &ram[er + 0x380]; ez = *(const float *) &ram[er + 0x388];
			bestd = bd; found = 1;
			g_aiTgtOnMe = (*(const UINT32 *) &ram[er + 0x32E4] & 0x7FFFFF) == (rob & 0x7FFFFF);
		}
		tSlot = mine;
		g_aiLogTarget = mine;
		// 11/26 (owner): area 40 only (2nd-4th: and areas 27 / 29 / 43, each its own route) - marathon mode. Neither our HP going down nor one of our attacks landing for 15 s while
		// we have a target: stop the enemy search and run the area 40 points (hard-coded below) - first the nearest point, then
		// the next one in file order on each arrival (within 1.5), and from the last one back the other way (and so on, to and
		// fro). Hit by an enemy (our HP goes down) on the way: marathon over, the search and the fight again (the 15 s count
		// starts over). Also over: out of area 40, dead, or no live enemy left (the gate handling takes over then).
		// "An attack landed" = a live enemy within 3 of us (height within 2) lost HP while we were attacking (mot_kind attack
		// 0x00800000 within the last 30 frames). a75 = 190.
		{
			static UINT32 mLastF = ~0u, mStillF = 0, mAtkF = 0; static int mPrevHp = -1, mEnHp[20], mIdx = -1, mDir = 1, mArrived = 0; static bool mEnInit = false, mResume = false;
			if (g_ppcTraceFrame != mLastF)
			{
				mLastF = g_ppcTraceFrame;
				// 11/26 (owner, 2nd): areas 27 and 40 (each its own route). An area change ends a marathon and forgets the route.
				static unsigned mArea = 0;
				const unsigned curAm = ram[0x10402D ^ 3];
				const bool areaChanged = curAm != mArea; mArea = curAm;
				const bool inA40 = (curAm == 40 || curAm == 27 || curAm == 29 || curAm == 43) && (pflags & 1) && !areaChanged;
				const int hpMe = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2];
				const float myYm = *(const float *) &ram[rob + 0x384];
				if (*(const UINT32 *) &ram[rob + 0x44] & 0x00800000u) mAtkF = g_ppcTraceFrame;
				bool landed = false;
				for (unsigned e = estart; e < 20; e++)
				{
					const UINT32 er = 0x1A0000 + e * 0x4000;
					const bool act = (*(const UINT32 *) &ram[er] & 1) != 0;
					const int h = act ? (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] : -1;
					if (mEnInit && act && mEnHp[e] > 0 && h < mEnHp[e] && g_ppcTraceFrame - mAtkF <= 30u)
					{
						const float qx = *(const float *) &ram[er + 0x380] - px, qz = *(const float *) &ram[er + 0x388] - pz;
						if (qx * qx + qz * qz < 9.0f && fabsf(*(const float *) &ram[er + 0x384] - myYm) < 2.0f) landed = true;
					}
					mEnHp[e] = h;
				}
				mEnInit = true;
				const bool hurt = mPrevHp > 0 && hpMe < mPrevHp;
				mPrevHp = hpMe;
				// 11/26 (owner): the route is hard-coded - the area 40 lines of bin64\PonMi\waypoints.txt (10:15), in that order
				// (all at y -12.2..-12.5). The file is no longer read for it.
				static const std::pair<float, float> wp40[] = {
					{ -45.987f, -68.199f }, { -40.067f, -63.224f }, { -24.450f, -62.765f }, {  -2.331f, -63.872f }, {  -1.237f, -38.291f },
					{ -14.530f, -43.011f }, { -12.744f, -21.818f }, { -58.968f, -24.974f }, { -53.977f, -42.138f }, { -15.597f, -42.982f },
				};
				// 11/26 (owner, 2nd): area 27 - the area 27 lines of bin64\PonMi\waypoints.txt (12:24), in that order (y -12.2..-12.5).
				static const std::pair<float, float> wp27[] = {
					{ -22.413f, -43.293f }, { -36.805f, -43.316f }, { -59.670f, -43.351f }, { -59.355f, -63.191f }, { -31.577f, -62.822f },
					{  -2.666f, -62.789f },
				};
				// 11/26 (owner, 3rd): area 29 - the area 29 lines of bin64\PonMi\waypoints.txt (13:05), in that order (y -4.8..-4.9).
				static const std::pair<float, float> wp29[] = {
					{ 103.099f, -38.508f }, { 115.604f, -37.572f }, { 115.519f,  -0.178f }, {  91.589f,   0.162f }, {  91.039f,  10.267f },
					{ 116.420f,  10.794f },
				};
				// 11/26 (owner, 4th): area 43 - the area 43 lines of bin64\PonMi\waypoints.txt (13:18), in that order (y 9.1..9.2).
				static const std::pair<float, float> wp43[] = { { -29.401f, -34.864f }, { -29.238f, -46.644f }, { -51.599f, -44.680f } };
				const std::pair<float, float> *wp = curAm == 27 ? wp27 : curAm == 29 ? wp29 : curAm == 43 ? wp43 : wp40;
				const int nWp = curAm == 27 ? (int) (sizeof(wp27) / sizeof(wp27[0])) : curAm == 29 ? (int) (sizeof(wp29) / sizeof(wp29[0])) : curAm == 43 ? 3
				                                                                                   : (int) (sizeof(wp40) / sizeof(wp40[0]));
				if (!inA40 || hpMe <= 0 || nWp == 0)
				{
					if (g_marOn) SpikeoutGateNote(g_ppcTraceFrame, "[MARATHON] node %u: over (%s)\n", node, !inA40 ? "left the area" : hpMe <= 0 ? "dead" : "no waypoints");
					g_marOn = false; mStillF = g_ppcTraceFrame; mResume = false;
				}
				else if (g_marOn)
				{
					if (hurt || ne == 0)
					{
						SpikeoutGateNote(g_ppcTraceFrame, "[MARATHON] node %u: over (%s) at (%.2f,%.2f)\n", node, hurt ? "hit by an enemy" : "no live enemy left", px, pz);
						g_marOn = false; mStillF = g_ppcTraceFrame; mResume = false;
					}
					else
					{
						if (mIdx >= nWp) mIdx = nWp - 1;
						const float dx = wp[mIdx].first - px, dz = wp[mIdx].second - pz;
						if (dx * dx + dz * dz < 2.25f)
						{
							// 11/26 (owner, 2nd/3rd): the enemy search again at a point - but not at the first point of this run:
							// from the 2nd point reached on (marathon over, the 15 s count starts over). The next marathon in this
							// area goes on to the next point (mResume), not the nearest one.
							int nx = mIdx;
							if (nWp > 1) { nx = mIdx + mDir; if (nx < 0 || nx >= nWp) { mDir = -mDir; nx = mIdx + mDir; } }
							mArrived++;
							if (mArrived >= 2)
							{
								SpikeoutGateNote(g_ppcTraceFrame, "[MARATHON] node %u: at point %d (%d reached) - searching again (next point %d)\n", node, mIdx + 1, mArrived, nx + 1);
								mIdx = nx; mResume = true;
								g_marOn = false; mStillF = g_ppcTraceFrame;
							}
							else
							{
								SpikeoutGateNote(g_ppcTraceFrame, "[MARATHON] node %u: at point %d (1st of this run) -> %d\n", node, mIdx + 1, nx + 1);
								mIdx = nx;
							}
						}
					}
				}
				else
				{
					if (hurt || landed || mine < 0) mStillF = g_ppcTraceFrame;
					else if (g_ppcTraceFrame - mStillF >= 863u)   // 15 s (57.5 fps)
					{
						float bd2 = 1e30f;
						if (!mResume || mIdx < 0 || mIdx >= nWp) { mResume = false; mIdx = 0; mDir = 1; }
						for (int k = 0; k < nWp && !mResume; k++)
						{
							const float dx = wp[k].first - px, dz = wp[k].second - pz;
							if (dx * dx + dz * dz < bd2) { bd2 = dx * dx + dz * dz; mIdx = k; }
						}
						mResume = false; mArrived = 0;
						g_marOn = true;
						SpikeoutGateNote(g_ppcTraceFrame, "[MARATHON] node %u: 15 s with no hit either way - running, point %d of %d first (me (%.2f,%.2f))\n", node, mIdx + 1, nWp, px, pz);
					}
				}
				if (g_marOn) { g_marX = wp[mIdx].first; g_marZ = wp[mIdx].second; }
			}
			if (g_marOn)
			{
				found = 0; tSlot = -1; g_aiLogTarget = -1;
				g_aiTgtOnMe = false; g_aiDnKeep = false; g_aiBossDownWait = false;
			}
		}
	}
	const UINT8 UP = 0x20, DOWN = 0x10, LEFT = 0x80, RIGHT = 0x40, BEAT = 0x01;
	UINT8 bits = 0;
	int mode = 0;   // 0 idle, 1 engage enemy, 2 follow P1, 3 calibration
	unsigned phase = 0;
	if ((pflags & 1) && SpikeoutInStage(ram))   // active AND actually in a live stage (any stage's scene id); ignore stale Rob during attract/join
	{
		if (g_spikeoutAICalib)                    // calibration: cycle directions, ignore enemies
		{
			mode = 3;
			phase = (g_ppcTraceFrame / 90) % 4;   // ~1.5 s each
			bits = phase == 0 ? UP : phase == 1 ? DOWN : phase == 2 ? LEFT : RIGHT;
		}
		else
		{
			// Enemy search, 2 stages (squared distances):
			//   stage 1 - an enemy within NEAR (1.5 units): go straight at it and attack (pre-aim, combos)
			//   stage 2 - farther: walk to it (wall avoidance below detours walls), until it is within NEAR
			const float dead = 0.5f;
			const UINT8 DIRS = UP | DOWN | LEFT | RIGHT;
			// Stuck handling: 0 straight at the target -> 1 turn along the wall -> 3 slide off along the wall for
			// 1.5s. "Hit a wall" = stick held 30f, moved < 0.3.
			static UINT32 lastF = 0, chkF = 0, stateF = 0, lastStuckF = 0;
			static float chkX = 0, chkZ = 0, lastP1x = 0, lastP1z = 0;
			static int prevPressed = 0, prevMode = 0, fstate = 0, stucks = 0, side = 0;
			static UINT32 escRng = 0x1234567u; static float escA = 0;   // fstate 3: random escape heading
			static UINT32 jumpUntil = 0; static bool stepJumpReq = false;
			static UINT32 floorJumpNext = 0; bool floorJumpReq = false;   // floor nav: a step up right ahead on the path
			UINT32 p1 = 0x1A0000;
			float p1x = *(const float *) &ram[p1 + 0x380], p1z = *(const float *) &ram[p1 + 0x388];
			int p1active = (*(const UINT32 *) &ram[p1 + 0x00] & 1);
			if (g_ppcTraceFrame != lastF)
			{
				lastF = g_ppcTraceFrame;
				if (p1active)
				{
					float jx = p1x - lastP1x, jz = p1z - lastP1z;
					if (jx * jx + jz * jz > 25.0f) { fstate = 0; stucks = 0; }   // P1 warped (gate): new area, start over
					lastP1x = p1x; lastP1z = p1z;
				}
				if (fstate == 1 && g_ppcTraceFrame - stateF >= 60) fstate = 0;   // turned for 1s: try straight again
				if (fstate == 3 && g_ppcTraceFrame - stateF >= 90) { fstate = 0; stucks = 0; }   // escaped for 1.5s: try the target again
				if (!prevPressed) { chkF = g_ppcTraceFrame; chkX = px; chkZ = pz; }
				else if (g_ppcTraceFrame - chkF >= 30)
				{
					float mx = px - chkX, mz = pz - chkZ;
					if (mx * mx + mz * mz < 0.09f)
					{
						if (prevMode == 2)
						{
							WallNavMarkBlocked(px, pz, g_aiLogTx, g_aiLogTz);   // the wall grid missed this: block the cells ahead
							FloorNavMarkBlocked(px, pz, g_aiLogTx, g_aiLogTz);
							if (g_ppcTraceFrame - lastStuckF > 300) stucks = 0;
							if (stucks == 0) side = side > 0 ? -1 : (side < 0 ? 1 : ((node & 1) ? 1 : -1));   // keep one side per wall
							lastStuckF = g_ppcTraceFrame; stucks++;
							if (SpikeoutStepAhead(ram, px, *(const float *) &ram[rob + 0x384] - FN_BODY, pz, 0.0f, 0.0f))   // the walls we are up against (feet height: Rob y - body)
								stepJumpReq = true;               // stuck at a step: jump it
							if (fstate == 3) { fstate = 0; stucks = 0; }
							else if (stucks >= 3)                                      // slide along the wall normal/tangent (or random if none) for 1.5s
							{
								float wnx = *(const float *) &ram[rob + 0x1D28], wnz = *(const float *) &ram[rob + 0x1D2C];
								float wnl2 = wnx * wnx + wnz * wnz;
								if ((*(const UINT32 *) &ram[rob + 0x1D30] & 1) && (wnx == wnx) && (wnz == wnz) && wnl2 > 1e-6f && wnl2 < 100.0f)
								{
									float wnl = sqrtf(wnl2); wnx /= wnl; wnz /= wnl;
									// Tangent along the wall toward (g_aiLogTx, g_aiLogTz), angled 30 deg away from the wall
									float tgx = -wnz * side, tgz = wnx * side;
									if ((stucks & 1) == 0 && tgx * (g_aiLogTx - px) + tgz * (g_aiLogTz - pz) < 0.0f) { tgx = -tgx; tgz = -tgz; }
									escA = atan2f(tgz * 0.866f + wnz * 0.5f, tgx * 0.866f + wnx * 0.5f);
								}
								else
								{
									escRng = escRng * 1103515245u + 12345u + node;
									escA = ((escRng >> 16) % 360u) * 0.0174533f;
								}
								fstate = 3; stateF = g_ppcTraceFrame;
							}
							else { fstate = 1; stateF = g_ppcTraceFrame; }
						}
					}
					chkF = g_ppcTraceFrame; chkX = px; chkZ = pz;
				}
			}
			float tx = px, tz = pz; int attack = 0, followMode = 0, gateMash = 0, gatePush = 0, dashMode = 0;
			bool r44Beat = false;   // area 44 hidden route: Beat Door#11/#12
			// Grabs (11/10): Rob +0x48 mot_kind2, names from the ROM's debug table (0x859D2, bit 0 = MSB): bit 10
			// tsukami (we hold someone) = 0x00200000, bit 11 tsukamare (someone holds us) = 0x00100000. Checked in
			// the 11/10 logs: 0x00200000 with an ally 0.77 away (k1 nage 0x2000), 0x00100000 with an enemy whose
			// own k2 had 0x00200000 (k1 nagerare 0x1000).
			UINT32 myK2 = *(const UINT32 *) &ram[rob + 0x48];
			bool grabbing = (myK2 & 0x00200000u) != 0, grabbed = (myK2 & 0x00100000u) != 0;
			bool grabEsc = false, grabSpecial = false, grabBossHold = false;
			int grabHp = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2];
			float grabOx = 0.0f, grabOz = 0.0f; bool grabAlly = false, grabHasPartner = false; int grabIdx = -1;
			if (grabbing || grabbed)
			{
				// The partner (11/13): the nearest other Rob within 2 whose own mot_kind2 shows the other side of the hold -
				// tsukamare 0x00100000 when we hold, tsukami 0x00200000 when we are held. Just the nearest Rob took an ally
				// standing by for the one we held: "holding an ally" -> lever up + Beat, mid-hold. No such Rob: the nearest.
				float bd2 = 4.0f, nd2 = 4.0f; int nIdx = -1; float nOx = 0.0f, nOz = 0.0f;
				UINT32 want = grabbing ? 0x00100000u : 0x00200000u;
				for (unsigned r = 0; r < 20; r++)
				{
					UINT32 rb = 0x1A0000 + r * 0x4000; if (rb == rob) continue;
					float ox = *(const float *) &ram[rb + 0x380], oz = *(const float *) &ram[rb + 0x388];
					float d2 = (ox - px) * (ox - px) + (oz - pz) * (oz - pz);
					if (d2 < nd2) { nd2 = d2; nIdx = (int) r; nOx = ox; nOz = oz; }
					if (d2 < bd2 && (*(const UINT32 *) &ram[rb + 0x48] & want)) { bd2 = d2; grabOx = ox; grabOz = oz; grabAlly = r < pcnt; grabHasPartner = true; grabIdx = (int) r; }
				}
				if (!grabHasPartner && nIdx >= 0) { grabOx = nOx; grabOz = nOz; grabAlly = (unsigned) nIdx < pcnt; grabHasPartner = true; grabIdx = nIdx; }
				// Special attack (11/13) only: low on life while holding an ENEMY, or held by a boss (class Rob +0x73 >= 4), and
				// at least 3s after the last one. Not ready yet: the normal hold / escape handling, so the bot never just stands.
				bool specReady = g_ppcTraceFrame >= g_aiSpecNext;
				bool bossHold = grabbed && grabIdx >= 0 && (unsigned) grabIdx >= (unsigned) pcnt && ram[(0x1A0000 + (unsigned) grabIdx * 0x4000 + 0x73) ^ 3] >= 4;
				// 11/15 (user): not Linda (Rob +0x3F00 = 2) - B+C+J while she holds one gives no special (her hold has its own
				// recorded combo instead, gkPh 6).
				// Live enemies around (within 3, height within 1.5, as the crowd special) and a live boss (class Rob +0x73 >= 4)
				// within 3 - for the ally holds below.
				int nNearG = 0; bool bossNearG = false;
				if (grabAlly)
				{
					const float myYg = *(const float *) &ram[rob + 0x384];
					for (unsigned e3 = estart; e3 < 20; e3++)
					{
						UINT32 er = 0x1A0000 + e3 * 0x4000;
						if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
						float ox = *(const float *) &ram[er + 0x380] - px, oz = *(const float *) &ram[er + 0x388] - pz;
						if (ox * ox + oz * oz < 9.0f && fabsf(*(const float *) &ram[er + 0x384] - myYg) < 1.5f) nNearG++;
						if (ox * ox + oz * oz < 4.0f && ram[(er + 0x73) ^ 3] >= 4) bossNearG = true;   // 11/22 (user): within 2 (was 3)
					}
				}
				// 11/21 (user, 3rd): holding an ally OR held by one - first of all, a boss within 3: the special B+C+J.
				// 11/21 (owner: "the special doesn't come out" in holds): the special is B+C+J held 30 frames, but from its 2nd
				// frame on specReady was false (g_aiSpecNext moved 3 s on), so the escape / ally throw / enemy hold handling
				// took the buttons back after one frame. While a special runs (its 30 frames: g_aiSpecNext - 180 + 30), keep it.
				const bool specRunning = g_ppcTraceFrame + 120u < g_aiSpecNext;   // 11/21: its 60 frames (mashed)
				// 11/21 (owner): the grab specials below run in the attack test too (they had !s_aiAttack0Test and never fired).
				// 11/22 (user): a BOSS held from the front (our motion 1118) with special stock left (Rob +0x3F25): half the
				// holds (drawn once, when the hold starts) the special, the other half the usual B, B, C hold handling.
				static UINT32 gbLastF = 0, gbRng = 0x6C8E9CF5u; static bool gbSpec = false;
				if (grabbing && g_ppcTraceFrame - gbLastF > 10u) { gbRng = gbRng * 1103515245u + 12345u + node; gbSpec = ((gbRng >> 16) % 10u) == 0u; }   // 11/25 (owner): 10% (was half)
				if (grabbing) gbLastF = g_ppcTraceFrame;
				const bool gbBossFront = grabbing && !grabAlly && grabIdx >= (int) pcnt && grabIdx < 20 &&
				                         ram[(0x1A0000 + (unsigned) grabIdx * 0x4000 + 0x73) ^ 3] >= 4 &&
				                         (short) *(const UINT16 *) &ram[(rob + 0x3C) ^ 2] == 1118 && ram[(rob + 0x3F25) ^ 3] > 0;
				if (specRunning) grabSpecial = true;
				// 11/22 (user): the ally hold with a boss near (within 2) only when we are low on life too (under 150, as the others)
				else if (specReady && grabAlly && grabHp > 0 && grabHp < 150 && bossNearG) grabSpecial = true;
				else if (specReady && gbBossFront && gbSpec && grabHp > 0) grabSpecial = true;
				// 11/21 (user): Linda too - everyone can.
				else if (specReady && grabbing && !grabAlly && grabHp > 0 && grabHp < 150) grabSpecial = true;   // holding an enemy, low on life
				// 11/21 (user): held by a boss - no more special: the lever round clockwise twice (grabEsc, inputs at the end).
				else if (bossHold) { grabEsc = true; grabBossHold = true; }
				// 11/21 (user): held by an enemy other than a boss, low on life (under 150, as the hold above): the special.
				else if (specReady && grabbed && !grabAlly && grabHp > 0 && grabHp < 150) grabSpecial = true;
				// 11/21 (user): holding an ALLY with 3+ live enemies around: the special instead of letting go (Linda too).
				else if (specReady && grabbing && grabAlly && grabHp > 0 && nNearG >= 3) grabSpecial = true;
				else if (grabbed || (grabbing && grabAlly)) grabEsc = true;               // held, or holding an ally: get out (inputs at the end)
			}
			bool grabEnemy = grabbing && !grabAlly && grabHasPartner && !grabSpecial;   // holding an enemy: knee x2, then a throw (end)
			bool jkWant = false, jkJump = false, jkBeat = false; float jkGx = 0, jkGz = 0, jkNx = 0, jkNz = 0;   // running big jump kick at a gate / door
			UINT32 navCam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
			UINT32 navKey = ((unsigned) ram[0x10402D ^ 3] << 8) | ram[(navCam + 0xC0) ^ 3];
			{
				unsigned camNow = ((unsigned) ram[(navCam + 0xC0) ^ 3] << 8) | ram[(navCam + 0xC1) ^ 3];
				if ((navKey >> 8) != g_cpArea) { g_cpArea = navKey >> 8; g_cpPassed.clear(); g_cpKicked = false; }
				if (g_cpKicked && (camNow >> 8) != (g_cpCam >> 8))   // 11/10: camar only - camsar flips just walking about (area 74: 3<->4 right in front of S#20/21)
				{
					if ((px - g_cpKx) * (px - g_cpKx) + (pz - g_cpKz) * (pz - g_cpKz) < 64.0f)
					{
						g_cpPassed.push_back(std::make_pair(g_cpKx, g_cpKz));
						SpikeoutGateNote(g_ppcTraceFrame, "[GATE] node %u passed the gate at (%.1f,%.1f) in area %u: camera %04X -> %04X (camar.camsar) me=(%.2f,%.2f) d=%.2f\n", node, g_cpKx, g_cpKz, navKey >> 8, g_cpCam, camNow, px, pz, sqrtf((px - g_cpKx) * (px - g_cpKx) + (pz - g_cpKz) * (pz - g_cpKz)));
					}
					g_cpKicked = false;
				}
			}
			g_aiLogNavKey = navKey;
			// Gate open = "BREAK THE GATE": the area flags at fa_scenario(+0x10413C)+0x284 gain 0x20|0x40 when the
			// area's last enemy falls (measured in area 75: 0x219 -> 0x279 at the final KO, cleared again in area 36).
			UINT32 scen = *(const UINT32 *) &ram[0x10413C] & 0x7FFFFF;
			UINT32 arflg = (scen >= 0x10000 && scen < 0x700000) ? *(const UINT32 *) &ram[scen + 0x284] : 0;
			bool gateFlag = (arflg & 0x60) != 0;
			g_aiLogArflg = arflg;
			SpikeoutWpKickLoad();
			if ((navKey >> 8) != g_wpkArea) { g_wpkArea = navKey >> 8; g_wpkDone.assign(g_wpk.size(), false); }
			int wpkIdx = -1;   // (b): nearest unused spot of this area on our level
			{
				float myY = *(const float *) &ram[rob + 0x384], bd = 1e30f;
				for (size_t k = 0; k < g_wpk.size(); k++)
				{
					if (g_wpk[k].area != (navKey >> 8) || g_wpkDone[k] || fabsf(g_wpk[k].y - myY) > 4.0f) continue;
					float d = (g_wpk[k].x - px) * (g_wpk[k].x - px) + (g_wpk[k].z - pz) * (g_wpk[k].z - pz);
					if (d < bd) { bd = d; wpkIdx = (int) k; }
				}
			}
			// 11/17 (user, test): attack range. Engaged once within 2.5 (AI_ENG_IN2), until it is further than 3.5 (AI_ENG_OUT2)
			// or the target changes; before 11/17 (s_aiAttack0Test false) 1.5 (near2).
			static bool engaged = false; static int engSlot = -1;
			if (!found || tSlot != engSlot) engaged = false;
			engSlot = found ? tSlot : -1;
			if (found) { if (bestd < AI_ENG_IN2) engaged = true; else if (bestd > AI_ENG_OUT2) engaged = false; }
			const float atkR2 = engaged ? AI_ENG_OUT2 : AI_ENG_IN2;
			// Height: in attack range but on another level. If the floor grid has a way up/down to it, go round there
			// (heightChase: no attacking from below); otherwise drop it as a target for 10 s.
			bool heightChase = false;
			{
				static int hSlot = -1; static UINT32 hFrames = 0;
				g_aiLogDy = 0.0f;
				if (found && tSlot >= 0 && tSlot < 20 && bestd < atkR2)   // 11/17: atkR2 (was near2)
				{
					float py = *(const float *) &ram[rob + 0x384];
					float ey = *(const float *) &ram[0x1A0000 + (unsigned) tSlot * 0x4000 + 0x384];
					g_aiLogDy = ey - py;
					// Another level? Compare the floors under us and under it (Rob heights above the floor differ between
					// players and enemies); no floor data: the Rob heights.
					int myN = SpikeoutFloorUnder(ram, px, pz, py - FN_BODY), enN = SpikeoutFloorUnder(ram, ex, ez, ey - FN_ENEMY_BODY);
					bool otherLevel = (myN >= 0 && enN >= 0) ? (myN != enN && fabsf(g_fn.y[enN] - g_fn.y[myN]) > FN_STEP) : fabsf(ey - py) > AI_HEIGHT_GAP;
					float hx = px, hz = pz; bool hj = false;
					if (otherLevel && SpikeoutFloorNav(ram, px, py, pz, ex, ey - FN_ENEMY_BODY, ez, &hx, &hz, &hj) == 1)
					{
						heightChase = true; hSlot = -1; hFrames = 0;           // there is a way up/down: go round, don't hit from below
					}
					else if (otherLevel || fabsf(ey - py) > AI_HEIGHT_GAP)
					{
						if (tSlot != hSlot) { hSlot = tSlot; hFrames = 0; }
						if (++hFrames >= AI_HEIGHT_HOLD) { g_aiHeightExclUntil[tSlot] = g_ppcTraceFrame + AI_HEIGHT_EXCL; hSlot = -1; hFrames = 0; found = 0; g_aiOtherLevelOnly = true; }
					}
					else hSlot = -1;
				}
				else hSlot = -1;
			}
			float py = *(const float *) &ram[rob + 0x384];
			float gtx = px, gtz = pz, gwx = px, gwz = pz; int mgKind = 0;
			bool mgOn = !found && !g_spikeoutWaypointRecord && SpikeoutMemGate(ram, node, navKey >> 8, gateFlag, px, pz, &gtx, &gtz, &gwx, &gwz, &mgKind);
			if (mgOn && SpikeoutGatePassedCam(gtx, gtz)) mgOn = false;   // went through it already (camera changed)
			if (mgOn && !SpikeoutNearFilteredExit(navKey >> 8, gtx, gtz, 6.0f)) mgOn = false;   // bot filter (11/09)
			if (mgOn && (navKey >> 8) == 15 && g_a15D1Open && (gtx - 9.0f) * (gtx - 9.0f) + (gtz - 23.8f) * (gtz - 23.8f) < 36.0f) mgOn = false;   // area 15: D#1 open -> not D#11/#12
			if (mgOn && (navKey >> 8) == 15 && (arflg & 0x0800u) && (gtx + 6.375f) * (gtx + 6.375f) + (gtz + 3.45f) * (gtz + 3.45f) < 36.0f) mgOn = false;   // area 15: D#11/#12 open -> not D#1
			g_aiLogMemGate = mgOn ? mgKind : 0; if (mgOn) { g_aiLogMgX = gtx; g_aiLogMgZ = gtz; }
			// Fully autonomous AI (no canwalk.bin or waypoints.txt):
			// 1. Prologue (area 75) start-room exit & sub-gate handling when no enemies are present
			// 2. Enemy present & close (bestd < near2): engage & attack
			// 3. Enemy present & far: approach enemy directly (wall avoidance below detours around walls/doors)
			// 4. No enemies: approach active door / exit gate from game memory (wall avoidance below detours around walls)
			bool a75 = (navKey >> 8) == 75 && !found;
			float a75x = 0.0f, a75z = 0.0f; bool a75On = false, a75Push = false;
			{
				static const float rd[4][2] = { { 55.2f, 38.1f }, { 42.8f, 38.1f }, { 60.2f, 51.9f }, { 42.8f, 51.9f } };   // Door#2..5 doorway centers (x, z)
				static int startDoor = -1; static bool latched = false, exited = false;
				if ((navKey >> 8) != 75) { latched = false; exited = false; startDoor = -1; }
				bool inRoom = py > 11.0f && (pz < 40.0f || pz > 50.0f);
				if ((navKey >> 8) == 75 && !latched)
				{
					latched = true; startDoor = -1;
					if (inRoom)
					{
						float bd = 1e30f;
						for (int k = 0; k < 4; k++) { float dx = rd[k][0] - px, dz = rd[k][1] - pz, d = dx * dx + dz * dz; if (d < bd) { bd = d; startDoor = k; } }
					}
				}
				if (latched && !inRoom) exited = true;
				g_aiLogA75 = 0;
				// 11/13 (B): an enemy targeted while still in the start room - leave the room first unless the enemy is in
				// this same room (in a start room, and nearest to the same door).
				bool a75EnemyOut = false;
				if ((navKey >> 8) == 75 && found && startDoor >= 0)
				{
					float ey = (tSlot >= 0 && tSlot < 20) ? *(const float *) &ram[0x1A0000 + (unsigned) tSlot * 0x4000 + 0x384] : py;
					bool eRoom = ey > 11.0f && (ez < 40.0f || ez > 50.0f);
					int eDoor = -1; float ebd = 1e30f;
					for (int k = 0; k < 4; k++) { float dx = rd[k][0] - ex, dz = rd[k][1] - ez, d = dx * dx + dz * dz; if (d < ebd) { ebd = d; eDoor = k; } }
					a75EnemyOut = !(eRoom && eDoor == startDoor);
				}
				if ((SPIKEOUT_HAND_TUNED || AI_A75_ROOM_EXIT) && (a75 || a75EnemyOut) && inRoom && !exited && startDoor >= 0)
				{
					float dx = rd[startDoor][0], dz = rd[startDoor][1];
					float ddx = dx - px, ddz = dz - pz;
					if (ddx * ddx + ddz * ddz > 2.25f) { a75x = dx; a75z = dz; }
					else { a75x = dx; a75z = dz + (dz < 45.0f ? 3.0f : -3.0f); a75Push = true; }
					a75On = true; g_aiLogA75 = 2 + startDoor;
					// 11/17 (user, log 19:57): the swing door leaf held them - 04 ~1070f, 01 ~350f at Door#2 (55.2, 38.1),
					// z ~39.5, the target flipping between the doorway and 3 past it (a75 2) and the push never getting
					// through. Within 3 of the doorway and under 0.3 moved in 60f: ask for the door pass (straight along
					// the door's normal out of the room, running big jump kick - wn 9 / a75 41-43). Its centre is put 1.6
					// out (where they stall) so that "through" (1.2 past it) means past the leaf.
					static UINT32 stF = 0; static float stX = 0.0f, stZ = 0.0f;
					const float nz = dz < 45.0f ? 1.0f : -1.0f;
					if (ddx * ddx + ddz * ddz < 9.0f)
					{
						if (!stF || (px - stX) * (px - stX) + (pz - stZ) * (pz - stZ) > 0.09f) { stF = g_ppcTraceFrame; stX = px; stZ = pz; }
						else if (g_ppcTraceFrame - stF >= 60) SpikeoutDoorPassReq(dx, dz + 1.6f * nz, 0.0f, nz);
					}
					else stF = 0;
				}
			}
			// The exit the area script has lit (ROM table above) comes first, enemies or not: once the gate can be
			// opened the leftover grunts run away, and some areas keep spawning them for ever, so waiting for "no
			// enemies" could mean never going. Walk to it (taking on only enemies that come within 1.5 on the way);
			// from within 3 units push into it and Beat (shutters must be broken; doors open as a player goes
			// through - Beat also hits whoever is next to us there).
			// Exits whose table cond is 0 (lit with no arflg bit set first - the script may be waiting on something
			// else, e.g. the wave count) are only taken once no enemy has been around for 1s.
			static UINT32 noEnemySince = 0; static bool hadEnemy = true;
			if (found) hadEnemy = true;
			else if (hadEnemy) { hadEnemy = false; noEnemySince = g_ppcTraceFrame; }
			float tex = 0.0f, tez = 0.0f; bool teShutter = false, tePush = false; int teCond = -1, teDoor = 0;
			SpikeoutDoorPassedTick(ram, navKey >> 8);
			UINT32 teFlags = arflg;   // area 74: the way in, as 0x4000 (from 91) / 0x8000 (from 93); both when unknown
			if ((navKey >> 8) == 74) teFlags |= g_spikeoutPrevArea == 91 ? 0x4000u : g_spikeoutPrevArea == 93 ? 0x8000u : 0xC000u;
			bool teOn = SpikeoutTableExit(ram, navKey >> 8, teFlags, node, &tex, &tez, &teShutter, &teCond, &teDoor);
			// 11/14 (user): Opera area 29 Door#5 is in the door table but is a GATE, broken by attacks (a running big jump
			// kick is fine; a cut-scene follows). As a door, the target went 3 past it once within 6-9, the floor path found
			// no way through the closed leaf and the 2D path ran off to z 40 (all 8 stuck there 3 hours, 11/14 log). As a
			// shutter: the target stays the gate itself, the kick from 3 (jump kick), no door pass.
			if (teOn && !teShutter && teDoor == 5 && (navKey >> 8) == 29) teShutter = true;
			// 11/14 (user): Diesel Town area 1 Door#15 is a gate too. Log (08:35): all 8 there at f 44531, opened at 47771
			// (54 s) - as a door the target was 3 past it (-27.2, -16.3) and they mostly stood pushing there (a75 10); it
			// opened only after a few running jump kicks.
			if (teOn && !teShutter && teDoor == 15 && (navKey >> 8) == 1) teShutter = true;
			// 11/14 (user): area 4 Door#45 is handled as a gate too (the target is the gate itself, jump kick from 3).
			if (teOn && !teShutter && teDoor == 45 && (navKey >> 8) == 4) teShutter = true;
			// 11/14 (user): area 6 Door#16 needs the gate broken - hard-coded as a gate. The game sets no "done" bit when
			// it is passed (0x40 comes only after the next room), so it alternates: 240 frames as a gate (target the
			// gate, running jump kicks from 3), then 150 frames walking through it (a6d16 below: the landing spot, then
			// straight down the steps). Within 1.5 of the passTune spot it counts as passed (passTune also runs for it).
			const bool a6gate = teOn && !teShutter && teDoor == 16 && (navKey >> 8) == 6;
			{
				static UINT32 a6T0 = 0;
				if (!a6gate) a6T0 = 0;
				else
				{
					// 11/18 (user): by the door's own flag instead of the 240 / 150 frame turns: a gate (kick it) until it is
					// broken, then the way through (landing spot, down the steps, passTune).
					(void) a6T0;
					if (!SpikeoutA6D16Broken(ram)) teShutter = true;
				}
			}
			// 11/18 (user): area 40 D#13/14 the same way - a gate (target it, running jump kicks from 3, the stall kick
			// anywhere within 8) until broken, then a door: through it along its normal (the door pass).
			if (teOn && !teShutter && (teDoor == 13 || teDoor == 14) && (navKey >> 8) == 40 && !SpikeoutA40Broken(ram)) teShutter = true;
			// Area 76 S#47 hard-coded (11/10): once broken (shutter 0x00040000) the game warps the players past it and
			// puts the shutter back (flags 20100000, no shine) and the gate wall back solid - and the area stays 76. The
			// table then fell back to S#47 (cond 0x0200 still in arflg) and the twin wall #946 never went off, so the
			// gate-wall test missed it: the bots went back to S#47 for ever. Seen broken once = passed for this visit.
			if ((navKey >> 8) == 76 && !SpikeoutGatePassedCam(74.0f, -19.0f))
			{
				UINT32 sw = *(const UINT32 *) &ram[0x104514] & 0x7FFFFF;
				if (sw >= 0x10000 && sw < 0x700000 && ram[(sw + 0x10) ^ 3] > 47 && ram[(sw + 0x10) ^ 3] <= 96 &&
				    (*(const UINT32 *) &ram[sw + 0x18 + 47 * 0x1C] & 0x00040000u))
				{
					g_cpPassed.push_back(std::make_pair(74.0f, -19.0f));
					SpikeoutGateNote(g_ppcTraceFrame, "[GATE] node %u: area 76 Shutter#47 broken -> passed (hard-coded), back to normal search\n", node);
				}
			}
			// Gate wall gone (11/10): a shutter row's target is a gate wall (0x10000 with A / B bits). When every such wall
			// within 2.5 of it is off (0x80000000 disabled, or not solid for players), the gate is broken -> passed, never
			// the target again in this area. For gates inside an area that change neither camar nor arflg (area 76's
			// S#47). (A plain "8 past it" test was tried first and dropped: the bots may come from the other side.)
			if (teOn && teShutter && !SpikeoutGatePassedCam(tex, tez) && SpikeoutGateWallOpen(ram, tex, tez))
			{
				g_cpPassed.push_back(std::make_pair(tex, tez));
				SpikeoutGateNote(g_ppcTraceFrame, "[GATE] node %u: the gate wall at (%.1f,%.1f) in area %u is gone\n", node, tex, tez, navKey >> 8);
			}
			if (teOn && SpikeoutGatePassedCam(tex, tez)) teOn = false;   // went through it already (camera changed)
			if (teOn && (teCond == 0 || !g_teLit) && (found || g_ppcTraceFrame - noEnemySince < 60)) teOn = false;
			// Doors are gone THROUGH, not into: aiming at the doorway centre turned the bot round as soon as it crossed
			// it (and dashed it back from 3 units on). The door's own normal (ROM def; else the way it came in, latched
			// within 6 units) gives the far side: the target is 3 units past the centre. The door is done only when the
			// game says so (door work passed bit or the arflg done bits) - no position-based "passed" any more.
			{
				static unsigned dArea = ~0u; static int nPassed = 0; static float passX[8], passZ[8];
				static float appX = 0.0f, appZ = 0.0f, appCX = 1e9f, appCZ = 1e9f, appSide = 0.0f, appHalf = 1.5f;
				unsigned curArea = navKey >> 8;
				g_teDoorCx = g_teDoorCz = 1e9f;
				if (curArea != dArea) { dArea = curArea; nPassed = 0; appCX = appCZ = 1e9f; g_passNextArea = ~0u; }
				g_passNextOn = false;
				// Hard-coded "gone through" spots (from 01's waypoints): a bot within r of the spot counts the door as
				// passed for this area visit. Area 6 Door#16: the game doesn't flag it and its table position is off -
				// the bots stood at the real way through for minutes, still aiming at the table spot (11/08).
				if (teOn && (!teShutter || a6gate))
				{
					// nx,nz: where to head on after it, until the next exit lights (the game sets the bit for it only
					// once someone gets further in).
					static const struct { UINT8 area; UINT8 id; float x, z, r, nx, nz; } passTune[] =
					{
						{ 6, 16, 18.797f, 14.994f, 1.5f, 9.0f, 23.8f },   // then on to Door#11/#12
					};
					for (unsigned k = 0; k < sizeof(passTune) / sizeof(passTune[0]); k++)
					{
						if (passTune[k].area != curArea || passTune[k].id != (UINT8) teDoor) continue;
						float qx = px - passTune[k].x, qz = pz - passTune[k].z;
						// 11/14 (user): past Door#16 is a one-way drop (landing Rob y ~5 -> floor below ~1): anywhere on the
						// floor below it counts as passed too - there is no way back up to the gate.
						const bool belowA6 = curArea == 6 && teDoor == 16 && py < 3.5f && px > 14.0f && px < 24.0f && pz > 12.5f && pz < 20.0f;
						if ((qx * qx + qz * qz < passTune[k].r * passTune[k].r || belowA6) && nPassed < 8)
						{
							passX[nPassed] = tex; passZ[nPassed] = tez; nPassed++;
							g_passNextArea = curArea; g_passNextX = passTune[k].nx; g_passNextZ = passTune[k].nz;
						}
					}
				}
				if (teOn && (!teShutter || a6gate))
				{
					for (int k = 0; k < nPassed; k++)
						if ((passX[k] - tex) * (passX[k] - tex) + (passZ[k] - tez) * (passZ[k] - tez) < 1.0f) teOn = false;
				}
				if (teOn && !teShutter)
				{
					float cdx = tex - px, cdz = tez - pz, cd = sqrtf(cdx * cdx + cdz * cdz);
					bool sameDoor = (appCX - tex) * (appCX - tex) + (appCZ - tez) * (appCZ - tez) < 1.0f;
					// (Opera area 29 Door#5 is a gate, not a door: handled as a shutter, above - 11/14.)
					if (!sameDoor && cd < 6.0f && cd > 0.3f)
					{
						appCX = tex; appCZ = tez; appX = cdx / cd; appZ = cdz / cd; sameDoor = true; appSide = -cd;
						float dwid = teDoor ? SpikeoutDoorWidth(ram, (unsigned) teDoor) : 0.0f;
						appHalf = (dwid > 0.5f ? dwid : 2.0f) + 0.8f;   // lateral tolerance: whole width (double doors) + body
						// Through the door means along its normal, not along the way we came (11/08: area 5 Door#43 is
						// approached side-on - walking along its face counted as "passed" and the bot never went through).
						float nx, nz;
						if (teDoor && SpikeoutDoorNormal(ram, (unsigned) teDoor, &nx, &nz))
						{
							if (nx * cdx + nz * cdz < 0.0f) { nx = -nx; nz = -nz; }   // pointing from our side to the far side
							appX = nx; appZ = nz;
						}
					}
					if (sameDoor)
					{
						// "Passed" is left to the game (11/08): the door work bit 0x04000000 / 0x400 (SpikeoutDoorPassed) and
						// the arflg done bits, both checked in SpikeoutTableExit. Going by our own position marked a door
						// done after walking along its face (area 5 Door#43). Until the game says so, keep aiming through.
						g_teDoorCx = appCX; g_teDoorCz = appCZ;
						tex += appX * 3.0f; tez += appZ * 3.0f;   // aim through it
						// 11/13: no door pass (straight through + running jump kick) for a door with no real leaf (width under
						// 0.5): its normal means nothing. Area 6 Door#16 (width 0): the pass pushed along x into the wall for
						// ~1.5 min; each time it timed out, the floor path took the bots through to +z within 2s.
						float lnx, lnz;   // readable def (SpikeoutDoorWidth also gives 0 for an unreadable one: keep the pass then)
						bool leafless = teDoor && SpikeoutDoorNormal(ram, (unsigned) teDoor, &lnx, &lnz) && SpikeoutDoorWidth(ram, (unsigned) teDoor) < 0.5f;
						if (!leafless && (appCX - px) * (appCX - px) + (appCZ - pz) * (appCZ - pz) < 16.0f) SpikeoutDoorPassReq(appCX, appCZ, appX, appZ);
					}
				}
			}
			if (!teOn && g_passNextArea == (navKey >> 8)) g_passNextOn = true;   // past a hard-coded spot: head on (below)
			// Stuck at the gate: pushed 10s and the area flags haven't moved (the gate won't open yet - the script still
			// wants a wave cleared): fight whoever is around for 15s, then try the gate again.
			static UINT32 gStallF = 0, gFightUntil = 0, gStallFlg = ~0u, gStallKey = ~0u;
			if (teOn)
			{
				float sdx = tex - px, sdz = tez - pz;
				bool atGate = sdx * sdx + sdz * sdz < 9.0f;
				if (g_ppcTraceFrame < gFightUntil) { gStallF = g_ppcTraceFrame; if (found) teOn = false; }   // fighting a while
				else if (!atGate || arflg != gStallFlg || navKey != gStallKey) { gStallF = g_ppcTraceFrame; gStallFlg = arflg; gStallKey = navKey; }
				else if (g_ppcTraceFrame - gStallF >= 600) { gFightUntil = g_ppcTraceFrame + 900; if (found) teOn = false; }
			}
			else gStallF = g_ppcTraceFrame;
			bool teStallKick = false;                                 // stuck short of the exit: kick it where we are
			// 11/14 (user: "a6 D#16 won't open again"): the door code aimed 3 past the table centre along the door's
			// +-x normal, (21.15, 12.0) - behind a wall. Stuck short of it the stall kick ran its 10 s of running jump
			// kicks there (a75 41-43, all 8, f 32010-32790), and only after it the floor path took them through on
			// the +z side in 2 s. Aim straight at the real way through (the passTune spot) and never stall-kick here.
			const bool a6d16 = teOn && !teShutter && teDoor == 16 && (navKey >> 8) == 6;
			// 11/14 (2nd): aiming at the passTune spot itself failed - it is down the steps from the landing (Rob y 5),
			// and an exit goal's height is "our level", so the floor grid found no way (wn 5, 30 s+ at 19-21, 10-11).
			// Now: the floor path only to the landing spot in front of it (19.35, 11.2; same level), then straight
			// (no path search) along the line that worked (19.35, 11.2) -> (18.2, 15.0); within 1.5 of the passTune
			// spot the door counts as passed and the bots head on to (9.0, 23.8) as before.
			bool a6d16Direct = false;
			if (a6d16)
			{
				const float lx = 19.35f, lz = 11.2f;
				const float dl = (px - lx) * (px - lx) + (pz - lz) * (pz - lz);
				if (dl < 16.0f && (pz > 12.5f || (pz > 11.0f && fabsf(px - 18.9f) < 1.2f))) { tex = 18.2f; tez = 15.0f; a6d16Direct = true; }   // in the doorway / past it: down the steps
				else if (dl < 1.0f) { tex = 18.2f; tez = 15.0f; a6d16Direct = true; }           // on the landing spot
				else { tex = lx; tez = lz; a6d16Direct = dl < 6.25f; }                           // walk up to it (straight in the last 2.5)
			}
			if (teOn)
			{
				float ddx = tex - px, ddz = tez - pz;
				tePush = ddx * ddx + ddz * ddz < (teShutter ? 9.0f : 30.25f);   // doors: within 5.5 of the far-side point = at the doorway
				// Can't get there: going for the gate, within 8 of it, no enemy near, but hardly moving (< 0.3 in 1s) -
				// its own wall, a step or someone in the way. Start the gate kick right where we are, for up to 10s or
				// until the area flags move (gate opened); then walk at it again.
				static UINT32 tsCheckF = 0, tsPushUntil = 0, tsFlg = ~0u; static float tsX = 0, tsZ = 0, tsGx = 1e9f, tsGz = 1e9f;
				bool stallPush = false;
				if (fabsf(tsGx - tex) > 1.0f || fabsf(tsGz - tez) > 1.0f) { tsGx = tex; tsGz = tez; tsPushUntil = 0; tsCheckF = g_ppcTraceFrame; tsX = px; tsZ = pz; }
				if (g_ppcTraceFrame < tsPushUntil)
				{
					if (arflg != tsFlg) tsPushUntil = 0;
					else stallPush = true;
				}
				else if ((!tePush || !teShutter) && ddx * ddx + ddz * ddz < 64.0f && !(found && bestd < 64.0f) &&
				         (teShutter || g_teDoorCx > 1e8f || (g_teDoorCx - px) * (g_teDoorCx - px) + (g_teDoorCz - pz) * (g_teDoorCz - pz) < 9.0f))   // doors: also while going through - but only at the door (within 3): stuck behind another wall, kicking there did nothing for minutes (area 6 Door#16, 11/08)
				{
					if (g_ppcTraceFrame - tsCheckF >= 60)
					{
						float mx = px - tsX, mz = pz - tsZ;
						if (mx * mx + mz * mz < 0.09f) { tsPushUntil = g_ppcTraceFrame + 600; tsFlg = arflg; stallPush = true; }
						tsCheckF = g_ppcTraceFrame; tsX = px; tsZ = pz;
					}
				}
				else { tsCheckF = g_ppcTraceFrame; tsX = px; tsZ = pz; }
				if (a6d16) stallPush = false;
				if (stallPush) tePush = true;
				teStallKick = stallPush;
				if (!(a75On && g_aiLogA75 >= 2)) g_aiLogA75 = stallPush ? 11 : (tePush ? 10 : 9);   // a75= in ai.txt: 9 going to the table exit, 10 at it, 11 stuck short of it: kicking there
			}
			// BotCanOpenGate=0 (11/21, owner): a gate to break (table shutter / door handled as a gate, a shutter from game
			// memory) is not taken - the bot fights as usual, and with no enemy waits 3.5 short of it (gateHold, below).
			// A broken gate is no longer teShutter / found by the memory search, so the way on is taken as before.
			bool gateHold = false; float ghX = 0.0f, ghZ = 0.0f;
			if (!g_spikeoutBotCanOpenGate)
			{
				if (teOn && teShutter) { gateHold = true; ghX = tex; ghZ = tez; teOn = false; }
				if (mgOn && (mgKind == 1 || mgKind == 3)) { if (!gateHold) { gateHold = true; ghX = gtx; ghZ = gtz; } mgOn = false; }
				wpkIdx = -1;   // recorded jump-kick spots = where gates are kicked
				teStallKick = false;   // a door stuck shut is not kicked open either (walking into it still opens it)
			}
			bool enemyWalk = false;                                  // walking up to an enemy (within 4): no random/stuck high jump
			g_aiLogPy = py;
			// 11/25 (owner, 2nd): the back grab. Our target is not after us (its Rob +0x32E4 is not ours) and we are not within 90
			// degrees either side of its front (its +0x2A against its angle to us, +0x3260 + 2 x our object) -> go round to within
			// 15 degrees of its back (a point on a circle round it, at most 0.6 rad a step toward its back, radius 1.3-2.5), and
			// once there walk straight into it (no Shift, no dash) - the grab. a75 = 129 going round, 128 walking in.
			// Its back in the world: the angle table is in yang units (65536 = 360 deg) with the same zero for every entry, so the
			// world angle of its facing = the world angle to us + (its +0x2A - its angle to us), turned the way the yang units
			// turn in the world (s_yangSign, learned once from two other Robs in our own table: +1 = the same way as atan2(z, x)).
			// Not learned yet: no going round, straight in as soon as we are behind it within 15 degrees.
			static int s_yangSign = 0;
			bool bhGrab = false, bhIn = false; float bhX = px, bhZ = pz;
			const unsigned myObj = ram[(rob + 0x72) ^ 3];
			if (!s_yangSign && myObj < 20)
			{
				int oa = -1, ob = -1; float ax = 0, az = 0, bx = 0, bz = 0;
				for (unsigned r2 = 0; r2 < 20 && ob < 0; r2++)
				{
					const UINT32 rr = 0x1A0000 + r2 * 0x4000;
					if (rr == rob || !(*(const UINT32 *) &ram[rr] & 1)) continue;
					const unsigned o2 = ram[(rr + 0x72) ^ 3];
					if (o2 >= 20) continue;
					const float vx = *(const float *) &ram[rr + 0x380] - px, vz = *(const float *) &ram[rr + 0x388] - pz;
					if (vx * vx + vz * vz < 0.25f) continue;
					if (oa < 0) { oa = (int) o2; ax = vx; az = vz; } else { ob = (int) o2; bx = vx; bz = vz; }
				}
				if (ob >= 0)
				{
					float dth = atan2f(bz, bx) - atan2f(az, ax);
					while (dth > 3.14159265f) dth -= 6.2831853f; while (dth < -3.14159265f) dth += 6.2831853f;
					const float dy = (float) (INT16) (UINT16) (*(const UINT16 *) &ram[(rob + 0x3260 + 2 * ob) ^ 2] - *(const UINT16 *) &ram[(rob + 0x3260 + 2 * oa) ^ 2]) * (6.2831853f / 65536.0f);
					if (fabsf(dth) > 0.3f && fabsf(dth) < 2.8f && fabsf(fabsf(dy) - fabsf(dth)) < 0.2f)
					{
						s_yangSign = (dy > 0.0f) == (dth > 0.0f) ? 1 : -1;
						printf("[BACK] node %u: yang turns %s atan2(z, x) in the world\n", node, s_yangSign > 0 ? "with" : "against");
					}
				}
			}
			if (found && tSlot >= (int) estart && tSlot < 20 && myObj < 20)
			{
				const UINT32 tr = 0x1A0000 + (unsigned) tSlot * 0x4000;
				const bool onMe = (*(const UINT32 *) &ram[tr + 0x32E4] & 0x7FFFFF) == (rob & 0x7FFFFF);
				const UINT16 itYang = *(const UINT16 *) &ram[(tr + 0x2A) ^ 2], itToMe = *(const UINT16 *) &ram[(tr + 0x3260 + 2 * myObj) ^ 2];
				const INT16 itErr = (INT16) (UINT16) (itYang - itToMe);   // 0 = it faces us, +-0x8000 = its back to us
				if (!onMe && abs((int) itErr) > 0x4000 && bestd < 16.0f) // not after us, and we are outside its front +-90 degrees; 11/25 (owner): within 4 only (log: rounded from 22 away)
				{
					bhGrab = true;
					bhIn = abs((int) itErr) >= 0x8000 - 0x0AAB;             // within 15 degrees of its back
					if (!bhIn && s_yangSign)
					{
						const float vx = px - ex, vz = pz - ez, d = sqrtf(vx * vx + vz * vz);
						const float aMe = atan2f(vz, vx);                     // world angle from it to us
						const float aBack = aMe + (float) s_yangSign * ((float) itErr * (6.2831853f / 65536.0f) + 3.14159265f);   // its facing + 180
						float df = aBack - aMe;
						while (df > 3.14159265f) df -= 6.2831853f; while (df < -3.14159265f) df += 6.2831853f;
						const float step = df > 0.6f ? 0.6f : df < -0.6f ? -0.6f : df;
						const float R = d < 1.3f ? 1.3f : d > 2.5f ? 2.5f : d;
						bhX = ex + R * cosf(aMe + step); bhZ = ez + R * sinf(aMe + step);
					}
					if (!bhIn && !s_yangSign) bhGrab = false;
				}
			}
			// 11/25 (owner, test): evade mode (no attacks). An enemy after us (its Rob +0x32E4 = ours; live, height within 2)
			// within 2 -> move away from every enemy after us within 4 (the sum of the directions away from them, nearer ones
			// counting more), Shift held, no dash, until none of them is within 4. Enemies not after us are ignored. a75 = 127.
			static bool evOn = false;
			bool evMove = false;                                     // 11/25: the evade is what we do this frame (the chain below)
			bool piyoGo = false;                                     // 11/25: walking into a dizzy boss (the grab) this frame
			float evX = 0.0f, evZ = 0.0f;
			{
				int n4 = 0, nOther = 0; float nd = 1e30f, nx = 0.0f, nz = 0.0f;   // 11/25 (owner): n4 = enemies after us within 5 (same level)
				for (unsigned e = estart; e < 20; e++)
				{
					const UINT32 er = 0x1A0000 + e * 0x4000;
					if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
					if ((*(const UINT32 *) &ram[er + 0x32E4] & 0x7FFFFF) != (rob & 0x7FFFFF)) continue;   // not after us: ignored
					if (bhGrab && (int) e == tSlot) continue;                                              // the target we are behind: grabbed, not run from
					if (fabsf(*(const float *) &ram[er + 0x384] - py) > 2.0f) continue;                       // another level
					const float ox = *(const float *) &ram[er + 0x380] - px, oz = *(const float *) &ram[er + 0x388] - pz, d2 = ox * ox + oz * oz;
					if (d2 >= 25.0f) continue;   // 11/25 (owner): within 5 (was 4)
					n4++;
					if ((int) e != tSlot && d2 < 4.0f) nOther++;        // 11/25: one after us other than our target, within 2
					if (d2 > 1e-6f && d2 < 4.0f) { evX -= ox / d2; evZ -= oz / d2; }   // away from it, 1/d weight (unit vector / d); 11/25 (owner): only those within 2
					if (d2 < nd) { nd = d2; nx = ox; nz = oz; }
				}
				// 11/25 (owner, 2nd): also by our target (within 2) with another one after us within 2: evade, no b-b
				{ const bool tgtNear = found && bestd < 4.0f && !heightChase;   // 11/25 (owner): our target within 2 - only another one within 2 makes us evade
				  const bool tgtIn3 = found && bestd < 9.0f && !heightChase;   // 11/25 (owner): out once the target is further than 3
				  evOn = evOn ? tgtIn3 : (tgtNear && nOther >= 1); (void) n4; }   // in: target within 2 + another after us within 2
				//   // 11/25 (owner): two or more after us within 5 - in; none / only one left - out
				if (grabbing || grabbed) evOn = false;
				// 11/25 (owner): the evade forces B+C - B (combo 2, g_aiForceC2) when (a) it has run 180 frames without ending (the
				// evade is ended then), or (b) it has ended 3 times within 180 frames. Ending (b) counts only the evade's own ends.
				{
					static bool evPrev = false; static UINT32 evStartF = 0, evEnds[3] = { 0, 0, 0 }; static int evEndN = 0;
					if (evOn && !evPrev) evStartF = g_ppcTraceFrame;
					if (evOn && g_ppcTraceFrame - evStartF >= 180u) { evOn = false; g_aiForceC2 = true; evPrev = false; evEndN = 0; g_aiAfterEv = false; }   // (a)
					else
					{
						if (evPrev && !evOn && !grabbing && !grabbed)
						{
							g_aiAfterEv = true;
							evEnds[evEndN % 3] = g_ppcTraceFrame; evEndN++;
							if (evEndN >= 3 && g_ppcTraceFrame - evEnds[evEndN % 3] <= 180u) { g_aiForceC2 = true; g_aiAfterEv = false; evEndN = 0; }   // (b): the 3rd end within 180 frames of the 1st
						}
						if (evOn) g_aiAfterEv = false;
						evPrev = evOn;
					}
				}
				float l = sqrtf(evX * evX + evZ * evZ);
				if (l < 1e-4f) { evX = -nx; evZ = -nz; l = sqrtf(evX * evX + evZ * evZ); }   // they cancel out: away from the nearest
				if (l > 1e-4f) { evX /= l; evZ /= l; } else { evX = 1.0f; evZ = 0.0f; }
			}
			if (g_marOn)                                             // 11/26 (owner): area 40 marathon - to the next waypoint
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				tx = g_marX; tz = g_marZ; followMode = 1;
				dashMode = (g_marX - px) * (g_marX - px) + (g_marZ - pz) * (g_marZ - pz) > 16.0f;
				g_aiLogA75 = 190;
			}
			else if (a75On && (!found || g_aiLogA75 >= 2))           // in Area 75 start room: exit room first; otherwise if no enemies, head to Area 75 gate
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				tx = a75x; tz = a75z; followMode = 1;
				if (a75Push) { fstate = 0; stucks = 0; gatePush = 1; if (g_aiLogA75 == 6 && g_spikeoutBotCanOpenGate) { jkWant = true; jkGx = a75x; jkGz = a75z; } }   // jump-kick only the exit shutter
			}
			// An exit is lit (going to / opening the gate): no enemy is focused on the way (11/08) - before, any enemy
			// within 8 pulled the bots off the gate. If the gate won't open because the script wants a wave cleared,
			// the gate stall above (10s) switches teOn off for 15s and they fight.
			else if (teOn)                                           // the script's exit: through it / break it
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				tx = tex; tz = tez; followMode = 1;
				// Shutters are broken: kick from 3 units. Doors are just gone through (most open as a player walks
				// in) - kick one only when stuck at it (it needs hitting, or something is in the way).
				bool kick = tePush && (teShutter || teStallKick);
				dashMode = !kick;                                 // dash there / through
				if (kick) { fstate = 0; stucks = 0; gatePush = 1; jkWant = true; jkGx = tex; jkGz = tez; }
			}
			// 11/25 (owner, 6th: "no b-b, it evades"): the target within 1 comes before the evade - right behind it (back 15
			// degrees): walk into it (the grab); else stand by it, face it, b-b. The evade runs only for what is further than 1.
			// 11/25 (owner): our target is a boss that is dizzy (mot_kind piyo 0x00000800): go to it and walk straight into it
			// (the grab) - no Shift, no attack; a dash only while it is more than 4 away. a75 = 145.
			else if (found && tSlot >= (int) estart && tSlot < 20 && !heightChase && !grabbing && !grabbed &&
			         ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x73) ^ 3] >= 4 &&
			         (*(const UINT32 *) &ram[0x1A0000 + (unsigned) tSlot * 0x4000 + 0x44] & 0x00000800u))
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				tx = ex; tz = ez; followMode = 1; dashMode = bestd > 16.0f; enemyWalk = !dashMode; g_aiLogA75 = 145; piyoGo = true;
			}
			else if (bhGrab && bhIn && bestd < atkR2 && !heightChase) // 11/25: within 1 and right behind it: straight into it (the grab)
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				tx = ex; tz = ez; followMode = 1; dashMode = 0; g_aiLogA75 = 128;
			}
			else if (found && bestd < atkR2 && !heightChase && !evOn) // 11/17: atkR2 (was near2). 1. Enemy is close: engage and attack (same level); 11/25: not when evading
			{
				tx = ex; tz = ez; mode = 1; attack = 1;
			}
			else if (evOn)                                           // 11/25 (owner, test): evade - away from the enemies after us
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				tx = px + evX * 3.0f; tz = pz + evZ * 3.0f; followMode = 1; dashMode = 0;
				evMove = true;
			}
			else if (bhGrab && !heightChase)                         // 11/25 (owner): the back grab - round to its back, then walk into it
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				followMode = 1; dashMode = 0;                            // no dash (and no Shift) all the way
				if (bhIn) { tx = ex; tz = ez; g_aiLogA75 = 128; }       // within 15 degrees of its back: straight into it (the grab)
				else { tx = bhX; tz = bhZ; g_aiLogA75 = 129; }          // round to its back first
			}
			else if (found)                                          // 2. Enemy is far: approach enemy (wall avoidance detours around obstacles)
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				tx = ex; tz = ez; followMode = 1; dashMode = bestd > 16.0f; enemyWalk = !dashMode;   // more than 4 away: dash at it
			}
			else if (!found && g_aiOtherLevelOnly && [&]() { for (const SpikeoutWaitSpot &w : s_waitSpots) if (w.area == (navKey >> 8)) return true; return false; }())
			{                                                        // 11/11: enemies only above/below: wait at the area's spot
				float wx = px, wz = pz;
				for (const SpikeoutWaitSpot &w : s_waitSpots) if (w.area == (navKey >> 8)) { wx = w.x; wz = w.z; }
				if ((wx - px) * (wx - px) + (wz - pz) * (wz - pz) > 1.0f) { mode = 2; if (prevMode != 2) fstate = 0; tx = wx; tz = wz; followMode = 1; }
				else { mode = 0; if (prevMode != 0) fstate = 0; }
				g_aiLogA75 = 60;
			}
			else if (gateHold && !found)                             // BotCanOpenGate=0: wait 3.5 short of the gate for a player to break it
			{
				float ax = px - ghX, az = pz - ghZ, al = sqrtf(ax * ax + az * az);
				if (al < 0.01f) { ax = 1.0f; az = 0.0f; al = 1.0f; }
				float wx = ghX + ax / al * 3.5f, wz = ghZ + az / al * 3.5f;
				if (al > 4.5f) { mode = 2; if (prevMode != 2) fstate = 0; tx = wx; tz = wz; followMode = 1; dashMode = al > 8.0f; }
				else { mode = 0; if (prevMode != 0) fstate = 0; }
				g_aiLogA75 = 70;
			}
			else if (mgOn)                                           // 3. No enemies: head to target door / shutter gate in game memory
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				float dgx = gtx - px, dgz = gtz - pz, dg = dgx * dgx + dgz * dgz;
				if (dg < 9.0f)                                       // within 3 units of gate/door: push through (doors), Beat only to break shutters
				{
					fstate = 0; stucks = 0; gatePush = 1;
					tx = gwx; tz = gwz; followMode = 1;
					if (mgKind == 1 || mgKind == 3) { jkWant = true; jkGx = gtx; jkGz = gtz; }
				}
				else
				{
					tx = gtx; tz = gtz; followMode = 1;
				}
			}
			else if (!found && wpkIdx >= 0)                          // a recorded jump-kick spot in this area: go there and kick
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				float wx = g_wpk[wpkIdx].x, wz = g_wpk[wpkIdx].z;
				tx = wx; tz = wz; followMode = 1; dashMode = 1; g_aiLogA75 = 50;
				if ((wx - px) * (wx - px) + (wz - pz) * (wz - pz) < 36.0f) { fstate = 0; stucks = 0; gatePush = 1; jkWant = true; jkGx = wx; jkGz = wz; }
			}
			else if (g_passNextOn && (g_passNextX - px) * (g_passNextX - px) + (g_passNextZ - pz) * (g_passNextZ - pz) > 4.0f)   // past a hard-coded spot: on to the next exit
			{
				mode = 2; if (prevMode != 2) fstate = 0;
				tx = g_passNextX; tz = g_passNextZ; followMode = 1;
			}
			else { mode = 0; if (prevMode != 0) fstate = 0; }
			// 11/13 (b): standing idle (mode 0) for 3s while an enemy is alive anywhere - e.g. the only ones left are on
			// another level (excluded above), and with every cabinet a bot nobody moved on: go for the nearest live
			// enemy anyway, the floor grid finding the way up / down (its feet height is the goal). a75=105.
			{
				static UINT32 idleSince = 0; static bool idleWas = false;
				if (mode != 0) idleWas = false;
				else
				{
					if (!idleWas) { idleWas = true; idleSince = g_ppcTraceFrame; }
					if (g_ppcTraceFrame - idleSince >= 180)
					{
						float bd = 1e30f; int be = -1;
						for (unsigned e2 = estart; e2 < 20; e2++)
						{
							UINT32 er = 0x1A0000 + e2 * 0x4000;
							if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
							if (SpikeoutA75BossOnly(ram) && !g_a75Near && ram[(er + 0x73) ^ 3] < 4) continue;   // 11/15: area 75 start room - bosses only (11/24: until within 1 of a boss)
							float ox = *(const float *) &ram[er + 0x380] - px, oz = *(const float *) &ram[er + 0x388] - pz;
							if (ox * ox + oz * oz < bd) { bd = ox * ox + oz * oz; be = (int) e2; }
						}
						if (be >= 0)
						{
							UINT32 er = 0x1A0000 + (unsigned) be * 0x4000;
							ex = *(const float *) &ram[er + 0x380]; ez = *(const float *) &ram[er + 0x388];
							found = 1; tSlot = be; bestd = bd;
							mode = 2; fstate = 0; tx = ex; tz = ez; followMode = 1; dashMode = bd > 16.0f;
							g_aiLogA75 = 105;
						}
					}
				}
			}
			int ehp = tSlot >= 0 ? (short) *(const UINT16 *) &ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x3F02) ^ 2] : 0;
			g_aiLogEhp = tSlot >= 0 ? ehp : -1;
			// 11/15 (user): area 27 (0x1B) - the enemies spawn on a ledge above the corridor behind a wall (no jumping up) and drop
			// down from it; bots standing there kept them up. Keep out of 5 around the spawn point (the middle of where the stuck
			// enemies paced on the ledge: x -41.1..-39.3, z -33.0..-31.4). Inside it: straight out to 5.7 (a75 = 111). A walk
			// target inside it: moved to the edge (5.3) on our side (a75 = 112) - so a bot chasing an enemy still up there waits
			// at the edge for it to drop.
			const unsigned koArea = ram[0x10402D ^ 3];
			if ((koArea == 6 || koArea == 27 || koArea == 40 || koArea == 41 || koArea == 47 || koArea == 87) && !teOn && !(*(const UINT32 *) &ram[rob + 0x48] & 0x00300000u))
			{
				// 11/15 (user): every spawn point of the area - the generator points the area's init script sets (group 2 command
				// 0x29 -> table 0xB74C4, 16 bytes each: x, y, z floats, direction; the generator 0x5AC7C picks one, 0x5AE98..). All at
				// y -12; #5-#8 in a row at z -34.25 (#7/#8 either side of where the bots stuck), #0-#4 at z -52..-54.
				// Inside one or more: away from all of them (sum of the outward directions, 3 ahead; a75 = 111). A walk target
				// inside one: moved to that circle's edge (5.3) on our side, up to 3 passes for neighbouring circles (a75 = 112).
				// 11/15 (user): + area 47, the last waypoint (8p\waypoints.txt 10:34: -38.973, -76.923).
				// 11/15 (user): a 4th value = the radius of that circle (left out = 5).
				static const float s_koPts[][4] = {
					{ 27, -24.25f, -53.75f }, { 27, -46.25f, -52.25f }, { 27, -29.75f, -52.25f }, { 27, -19.75f, -52.25f }, { 27, -29.75f, -53.75f },
					{ 27, -18.75f, -34.25f }, { 27, -27.75f, -34.25f }, { 27, -38.25f, -34.25f }, { 27, -43.75f, -34.25f },
					{ 47, -38.973f, -76.923f },
					{ 41, -38.973f, -76.923f },   // 11/15: area 41 = the same room as 47 (same scripts)
					// 11/15 (user): area 40 = the same room as 27 (other scripts; its generator table 0xBCB98 holds all 9 of 27's
					// points): all 16 of its points.
					{ 40, -24.25f, -53.75f }, { 40, -40.25f, -14.75f }, { 40, -48.25f, -14.75f }, { 40, -48.25f, -32.25f },
					{ 40, -29.75f, -32.25f }, { 40, -18.75f, -34.25f }, { 40, -27.75f, -34.25f }, { 40, -38.25f, -34.25f },
					{ 40, -43.75f, -34.25f }, { 40, -46.25f, -52.25f }, { 40, -29.75f, -52.25f }, { 40, -19.75f, -52.25f },
					{ 40, -29.75f, -53.75f }, { 40, -28.25f, -14.75f }, { 40, -22.75f, -14.75f }, { 40, -19.25f, -32.75f },
					// 11/15 (user): Diesel area 6, generator points #10 / #11 (init 0xAA6AC, y 1.0), radius 3.
					{ 6, 30.4f, 17.8f, 2.0f }, { 6, 30.4f, 29.8f, 2.0f },   // 11/15: radius 3 -> 2
					// 11/21 (user): area 87 (PHOENIX's map, AREA 4) - generators #8 / #9 (table 0xD6084) use spawn points #6 / #7
					// (table 0xD63A0, y 0.0). Their 2nd enemy (3-4 players only) cannot come out there (also on the real machine);
					// the bots walked into the wall at it and stuck. Radius 2, as area 6.
					{ 87, -45.0f, 50.0f, 2.0f }, { 87, -56.0f, 38.0f, 2.0f },
				};
				float s_a27Spawn[32][2], s_koR[32]; unsigned NSP = 0;
				for (unsigned c = 0; c < sizeof(s_koPts) / sizeof(s_koPts[0]) && NSP < 32; c++)
					if ((unsigned) s_koPts[c][0] == koArea) { s_a27Spawn[NSP][0] = s_koPts[c][1]; s_a27Spawn[NSP][1] = s_koPts[c][2]; s_koR[NSP] = s_koPts[c][3] > 0.0f ? s_koPts[c][3] : 2.0f; NSP++; }   // 11/15 (user): every circle radius 2 (was 5)
				float ox = 0.0f, oz = 0.0f; bool in = false;
				for (unsigned c = 0; c < NSP; c++)
				{
					const float SR = s_koR[c];
					float bx = px - s_a27Spawn[c][0], bz = pz - s_a27Spawn[c][1], bl = sqrtf(bx * bx + bz * bz);
					if (bl >= SR) continue;
					in = true;
					if (bl < 0.1f) { bx = 0.0f; bz = -1.0f; bl = 1.0f; }
					ox += bx / bl * (SR - bl + 0.5f); oz += bz / bl * (SR - bl + 0.5f);   // deeper in: stronger push
				}
				if (in)
				{
					float ol = sqrtf(ox * ox + oz * oz);
					if (ol < 0.01f) { ox = 0.0f; oz = -1.0f; ol = 1.0f; }
					mode = 2; if (prevMode != 2) fstate = 0;
					tx = px + ox / ol * 3.0f; tz = pz + oz / ol * 3.0f; followMode = 1; dashMode = 0;
					attack = 0; enemyWalk = false; g_aiLogA75 = 111;
				}
				else if (mode == 2)
					for (int pass = 0; pass < 3; pass++)
					{
						bool moved = false;
						for (unsigned c = 0; c < NSP; c++)
						{
							const float SCX = s_a27Spawn[c][0], SCZ = s_a27Spawn[c][1], SR = s_koR[c];
							float gx = tx - SCX, gz = tz - SCZ;
							if (gx * gx + gz * gz >= SR * SR) continue;
							float bx = px - SCX, bz = pz - SCZ, bl = sqrtf(bx * bx + bz * bz);
							if (bl < 0.1f) { bx = 0.0f; bz = -1.0f; bl = 1.0f; }
							tx = SCX + bx / bl * (SR + 0.3f); tz = SCZ + bz / bl * (SR + 0.3f);
							enemyWalk = false; g_aiLogA75 = 112; moved = true;
						}
						if (!moved) break;
					}
			}
			// 11/13: OFF (AI_ITEM_SEEK) - a bot headed for an item another cabinet took, and then stood there doing nothing.
			// 11/14 (user): back ON with new rules - HP items when own HP is below max (avoided when it is full), SP items
			// when no enemy is being fought, time / invincible items always; anything else on the floor (chairs etc. -
			// no pickup bits) is left alone. Standing within 1.2 of the item for 30 frames without getting it (taken by
			// someone else, or out of reach): leave it for 10 s.
			const bool AI_ITEM_SEEK = true;
			// 11/12 item seek: own HP under half -> walk over the nearest HP item on the floor (within 12, same level)
			// before anything else here (the area-53 climb and the area-44 route below still win). Area objects
			// 0x1F4000 + i*0xBC (count *(0x10411C)+0x10); pickable (0x3A1B0): flags 0x10000000, not 0x00020000
			// (held by +0x57), 0x8 or 0x00400000, timer +0x52 (signed) <= 0x3C0; picked within 1.0 across and 1.0
			// in Rob y. Type = def(+4)+0x21: 0x01 HP 1/4, 0x10 HP 1/2, 0x20 HP 1/8. Max HP: halfword 0x106280.
			// The same item for 6 s without getting it: leave it for 10 s.
			{
				static int seekIdx = -1; static UINT32 seekSince = 0; static int skipIdx = -1; static UINT32 skipUntil = 0;
				static unsigned seekArea = ~0u;
				unsigned curArea = ram[0x10402D ^ 3];
				if (curArea != seekArea) { seekArea = curArea; seekIdx = -1; skipIdx = -1; }
				int hpMe = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2];
				int hpMax = *(const UINT16 *) &ram[0x106280 ^ 2];
				int best = -1; float bestD = 144.0f, bix = 0.0f, biz = 0.0f;
				UINT32 mgr = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
				// which pickup bits we go for: 0x31 HP (own HP below max), 0x04 SP (no enemy being fought), 0x02 time and
				// 0x08 invincible (always)
				// 11/18 (user): heading for the open gate / lit exit (teOn): only HP / SP items (by the rules below) - the time
				// and invincibility items are left, and so are the things to hold (below).
				UINT8 wantBits = teOn ? 0x00 : 0x0A;
				// 11/17 (user, 2nd): HP items when own HP is under half of the max (was: under the max); SP items when own
				// SP stock (Rob +0x3F25, 5 at most - 0x5444C) is 4 or less. Both go before anything else; picked up (by us
				// or anyone) -> the item is gone and the target pick takes over again. Otherwise they are walked round (below).
				if (hpMe > 0 && hpMax > 0 && hpMe * 2 < hpMax) wantBits |= 0x31;
				if (ram[(rob + 0x3F25) ^ 3] <= 4) wantBits |= 0x04;
				static UINT32 nearSince = 0; static int nearIdx = -1;
				if (AI_ITEM_SEEK && !g_marOn && hpMe > 0 && mgr >= 0x100000 && mgr < 0x7FF000)   // 11/26: not in the marathon
				{
					int cnt = *(const UINT16 *) &ram[(mgr + 0x10) ^ 2];
					if (cnt > 0x106) cnt = 0x106;
					for (int i = 0; i < cnt; i++)
					{
						UINT32 o = 0x1F4000 + (UINT32) i * 0xBC;
						UINT32 fl = *(const UINT32 *) &ram[o];
						if (!(fl & 0x10000000) || (fl & 0x00020000) || !(fl & 0x00400008)) continue;
						if ((short) *(const UINT16 *) &ram[(o + 0x52) ^ 2] > 0x3C0) continue;
						if (i == skipIdx && g_ppcTraceFrame < skipUntil) continue;
						const UINT8 *dp = SpikeoutMapPtr((*(const UINT32 *) &ram[o + 4] + 0x21) & ~3u, 4);
						if (!dp || !(dp[((*(const UINT32 *) &ram[o + 4] + 0x21) & 3) ^ 3] & wantBits)) continue;
						float ix = *(const float *) &ram[o + 0x2C], iy = *(const float *) &ram[o + 0x30], iz = *(const float *) &ram[o + 0x34];
						if (fabsf(iy - py) > 1.5f) continue;
						float d = (ix - px) * (ix - px) + (iz - pz) * (iz - pz);
						if (d < bestD) { bestD = d; best = i; bix = ix; biz = iz; }
					}
				}
				g_aiItemSeek = false;
				if (best < 0) seekIdx = -1;
				else
				{
					if (best != seekIdx) { seekIdx = best; seekSince = g_ppcTraceFrame; }
					else if (g_ppcTraceFrame - seekSince > 360) { skipIdx = best; skipUntil = g_ppcTraceFrame + 600; seekIdx = -1; }
					// 11/14: at it (within 1.2) for 30 frames and still not ours: give it up for 10 s
					if (seekIdx >= 0 && bestD < 1.44f)
					{
						if (nearIdx != seekIdx) { nearIdx = seekIdx; nearSince = g_ppcTraceFrame; }
						else if (g_ppcTraceFrame - nearSince > 30) { skipIdx = seekIdx; skipUntil = g_ppcTraceFrame + 600; seekIdx = -1; nearIdx = -1; }
					}
					else nearIdx = -1;
					if (seekIdx >= 0)
					{
						mode = 2; if (prevMode != 2) fstate = 0;
						tx = bix; tz = biz; followMode = 1; dashMode = bestD > 16.0f;
						attack = 0; gatePush = 0; gateMash = 0; jkWant = false;
						enemyWalk = false; g_aiLogA75 = 90;
						g_aiItemSeek = true;
					}
				}
				// 11/17 (user): weapons / things to hold - the 9 defs a human was seen to take (01 itemlog 22:15; #6 =
				// the katana, area 17). On the floor = flags 0x00400000 with none of 0x00020000 (held) / 0x00040000 /
				// 0x00080000 / 0x00100000, no owner (+0x57 = FF). Picked up by B+C standing on it (the human took them
				// from 0.6-1.3). Go for the nearest within 6 (same level) when not holding one, no HP/SP item is wanted
				// and no target within 2.5; within 0.8: B+C (4 on / 8 off, below via g_aiWeaponPress). 4 s without
				// getting it: leave it for 10 s. a75 = 92.
				g_aiWeaponPress = false;
				if (AI_ITEM_SEEK && seekIdx < 0 && !teOn && !g_marOn && hpMe > 0 && mgr >= 0x100000 && mgr < 0x7FF000)   // 11/18: not on the way to the gate; 11/23: nor while guarding
				{
					static const UINT32 s_holdDefs[] = { 0xFFF6F3EC, 0xFFF6F574, 0xFFF6F880, 0xFFF6F904, 0xFFF6FB24,
					                                     0xFFF701C8, 0xFFF7026C, 0xFFF7068C, 0xFFF6FCB4 };
					static int wSeek = -1, wSkip = -1; static UINT32 wSince = 0, wSkipUntil = 0;
					const unsigned mySlotW = (rob - 0x1A0000) / 0x4000;
					int cntW = *(const UINT16 *) &ram[(mgr + 0x10) ^ 2]; if (cntW > 0x106) cntW = 0x106;
					bool holding = false; int bw = -1; float bwd = 36.0f, wx = 0.0f, wz = 0.0f;
					// 11/17 (user, 2nd): everything an enemy dropped too - an object seen held by an enemy (owner +0x57 =
					// slot 8-19) counts as holdable from then on in this area (besides the 9 defs). And the floor test is
					// only "0x00400000 on, held 0x00020000 off, no owner": the dropped katanas carry 0x00100000 / 0x00080000
					// (42508400, 42488400 - 01 itemlog 22:15) and the old test (none of 0x001E0000) never went for them.
					static UINT8 s_enemyHeld[0x106]; static unsigned s_ehArea = ~0u;
					{ const unsigned ar = ram[0x10402D ^ 3]; if (ar != s_ehArea) { s_ehArea = ar; memset(s_enemyHeld, 0, sizeof(s_enemyHeld)); } }
					for (int i = 0; i < cntW; i++)
					{
						UINT32 o = 0x1F4000 + (UINT32) i * 0xBC, fl = *(const UINT32 *) &ram[o], def = *(const UINT32 *) &ram[o + 4];
						const unsigned own = ram[(o + 0x57) ^ 3];
						if (own >= 8 && own < 20) s_enemyHeld[i] = 1;
						bool hold = s_enemyHeld[i] != 0;
						for (UINT32 hd : s_holdDefs) if (!hold && def == hd) { hold = true; break; }
						if (!hold) continue;
						if (own == mySlotW && (fl & 0x00020000u)) { holding = true; break; }
						if (own != 0xFF || !(fl & 0x00400000u) || (fl & 0x00020000u)) continue;
						if (i == wSkip && g_ppcTraceFrame < wSkipUntil) continue;
						float ix = *(const float *) &ram[o + 0x2C], iy = *(const float *) &ram[o + 0x30], iz = *(const float *) &ram[o + 0x34];
						if (fabsf(iy - (py - 1.15f)) > 1.5f) continue;
						float d = (ix - px) * (ix - px) + (iz - pz) * (iz - pz);
						if (d < bwd) { bwd = d; bw = i; wx = ix; wz = iz; }
					}
					if (holding || bw < 0 || (found && bestd < 6.25f)) wSeek = -1;
					else
					{
						if (bw != wSeek) { wSeek = bw; wSince = g_ppcTraceFrame; }
						else if (g_ppcTraceFrame - wSince > 240) { wSkip = bw; wSkipUntil = g_ppcTraceFrame + 600; wSeek = -1; }
						if (wSeek >= 0)
						{
							mode = 2; if (prevMode != 2) fstate = 0;
							tx = wx; tz = wz; followMode = 1; dashMode = bwd > 16.0f;
							attack = 0; gatePush = 0; gateMash = 0; jkWant = false;
							enemyWalk = false; g_aiLogA75 = 92;
							if (SpikeoutPickReach(0, wSeek, bwd, wx, wz)) g_aiWeaponPress = true;   // 11/17: was within 0.8
						}
					}
				}
			}
			// Final area (Michael's rooftop, area 53): the enemies never come down, and the floor grid can't find the
			// way back up. Fallen off (Rob y < 5.0; the roof is at ~7.9): follow the recorded route up (waypoints
			// 11/08) and ignore everything else until back on the roof (y > 7.4) or at the last point.
			bool wpClimb = false;
			{
				static const float wp53[][3] = {   // x, Rob y, z
					// 11/25 (owner): the area 53 lines of ../waypoints.txt (was -63.179 1.180 -15.377 / -82.385 6.347 -14.525 /
					// -83.036 6.326 0.382 / -72.050 6.330 -0.097).
					{ -59.244f, 1.160f, -11.685f },
					{ -62.569f, 1.160f, -16.037f },
					{ -80.198f, 6.238f, -13.098f },
					{ -82.583f, 6.219f,   0.116f },
					{ -65.256f, 7.862f,   0.074f },
				};
				const int nWp = (int) (sizeof(wp53) / sizeof(wp53[0]));
				static int wpIdx = -1;
				static float jdx = 0.0f, jdz = 0.0f;
				if (ram[0x10402D ^ 3] != 53 || (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2] <= 0) { wpIdx = -1; g_wpJumpUntil = 0; }
				else if (g_ppcTraceFrame < g_wpJumpUntil)
				{
					// The big jump from the last point: keep dashing the way we came in (#2 -> #3) with Jump held.
					mode = 2; tx = px + jdx * 3.0f; tz = pz + jdz * 3.0f; followMode = 1; dashMode = 1;
					attack = 0; gatePush = 0; gateMash = 0;
					wpClimb = true; enemyWalk = false; g_aiLogA75 = 30;
				}
				else
				{
					if (wpIdx < 0 && py < 5.0f)
					{
						wpIdx = 0;                                    // on the ground: from the foot of the route
					}
					if (wpIdx >= 0 && py > 7.4f) wpIdx = -1;          // back on the roof
					// Fell off the route again (more than 1.5 below the point we last reached): start over from #0.
					if (wpIdx > 0 && py < wp53[wpIdx - 1][1] - 1.5f) wpIdx = 0;
					if (wpIdx >= 0)
					{
						// Reached = within 1.2 across AND at its height (within 1.5): from the ground, standing under a
						// point on the upper level (the last one is right above where it falls) is not reaching it (11/08).
						float dx = wp53[wpIdx][0] - px, dz = wp53[wpIdx][2] - pz;
						if (wpIdx == nWp - 1)
						{
							// The last point is reached dashing, at whatever height: big jump from there (dash + Jump,
							// 20 frames) up onto the roof. Still below afterwards: the route starts over from #0.
							// 11/10: jump anywhere inside the box of 6 points recorded around the last point (x -72.88..-71.05,
							// z -2.15..2.02) instead of within 1.2 of it.
							if (px >= -72.88f && px <= -71.05f && pz >= -2.15f && pz <= 2.02f)
							{
								float ix = wp53[wpIdx][0] - wp53[wpIdx - 1][0], iz = wp53[wpIdx][2] - wp53[wpIdx - 1][2], il = sqrtf(ix * ix + iz * iz);
								jdx = il > 0.01f ? ix / il : 0.0f; jdz = il > 0.01f ? iz / il : 0.0f;
								g_wpJumpUntil = g_ppcTraceFrame + 20; wpIdx = -1;
								mode = 2; tx = px + jdx * 3.0f; tz = pz + jdz * 3.0f; followMode = 1; dashMode = 1;
								attack = 0; gatePush = 0; gateMash = 0;
								wpClimb = true; enemyWalk = false; g_aiLogA75 = 30;
							}
						}
						else if (dx * dx + dz * dz < 1.44f && fabsf(py - wp53[wpIdx][1]) < 1.5f) wpIdx++;
					}
					if (wpIdx >= 0)
					{
						mode = 2; if (prevMode != 2) fstate = 0;
						tx = wp53[wpIdx][0]; tz = wp53[wpIdx][2]; followMode = 1; dashMode = 1;
						attack = 0; gatePush = 0; gateMash = 0;
						wpClimb = true; enemyWalk = false; g_aiLogA75 = 20 + wpIdx;
					}
				}
			}
			// Mall area 44 hidden route to the heavy door D#5 (see g_r44). Only while the exit pick is D#5.
			{
				static int rIdx = -1; static UINT32 rBeatF = 0; static unsigned rArea = ~0u;
				unsigned curA = navKey >> 8;
				if (curA != rArea) { rArea = curA; rIdx = -1; }
				if (curA != 44 || !teOn || teDoor != 5 || g_r44.empty() || wpClimb) rIdx = -1;
				else
				{
					const float dcx = -19.99f, dcz = 3.09f;   // Door#11/#12 doorway centre
					int nR = (int) g_r44.size(), beatAt = -1; float bb = 9.0f;
					for (int k = 0; k < nR; k++)
					{
						float d = (g_r44[k].x - dcx) * (g_r44[k].x - dcx) + (g_r44[k].z - dcz) * (g_r44[k].z - dcz);
						if (d < bb) { bb = d; beatAt = k; }
					}
					if (rIdx < 0)
					{
						// start from the nearest point (a bot that is already past part of the route does not go back)
						float bd = 1e30f; rIdx = 0;
						for (int k = 0; k < nR; k++)
						{
							float d = (g_r44[k].x - px) * (g_r44[k].x - px) + (g_r44[k].z - pz) * (g_r44[k].z - pz);
							if (d < bd) { bd = d; rIdx = k; }
						}
						rBeatF = 0;
					}
					if (rIdx < nR)
					{
						float dx = g_r44[rIdx].x - px, dz = g_r44[rIdx].z - pz;
						bool at = dx * dx + dz * dz < 1.44f;
						mode = 2; if (prevMode != 2) fstate = 0;
						tx = g_r44[rIdx].x; tz = g_r44[rIdx].z; followMode = 1; dashMode = 1;
						attack = 0; gatePush = 0; gateMash = 0;
						wpClimb = true; enemyWalk = false; g_aiLogA75 = 60 + rIdx;
						if (rIdx == beatAt && (at || rBeatF))   // once beating, stay on it (pushing at the doors moves us off the point)
						{
							// at the doors: face them and Beat (4 on / 20 off) until one of them is held open, or 4s
							if (!rBeatF) rBeatF = g_ppcTraceFrame ? g_ppcTraceFrame : 1;
							bool open = false;
							UINT32 dw = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
							if (dw >= 0x10000 && dw < 0x700000)
								for (unsigned di = 11; di <= 12; di++)
									if (*(const UINT32 *) &ram[dw + 0x7EC + di * 0x18] & 0x00000400) open = true;
							unsigned be = g_ppcTraceFrame - rBeatF;
							tx = dcx; tz = dcz; dashMode = 0; gatePush = 1; fstate = 0; stucks = 0;
							r44Beat = (be % 24) < 4;
							g_aiLogA75 = 59;
							if ((open && be >= 30) || be > 240) { rIdx++; rBeatF = 0; r44Beat = false; }
						}
						else if (at) rIdx++;
					}
					// past the last point: the normal exit code takes over (jump kick at D#5)
				}
			}
			bool doorPass = false;
			{
				// Door pass, kept simple (11/08): go straight through the doorway (centre + 2 along its normal, dashing);
				// stuck (under 0.3 further through in 1s) -> kick / punch for 72f (one Charge+Jump / Beat round), then
				// push on again. Through = 1.2 past the centre; the door being done is still the game's flags.
				// 6s without getting through -> give it up for 3s (the path search takes us round to it again).
				static bool dpOn = false; static float dCx = 0, dCz = 0, dNx = 0, dNz = 0; static UINT32 dStart = 0, dBlockUntil = 0;
				if ((g_dpReqF == g_ppcTraceFrame || g_dpReqF + 1 == g_ppcTraceFrame) && g_ppcTraceFrame >= dBlockUntil)
				{
					float ddx = g_dpReqCx - dCx, ddz = g_dpReqCz - dCz;
					if (!dpOn || ddx * ddx + ddz * ddz > 1.0f) { dpOn = true; dStart = g_ppcTraceFrame; }
					dCx = g_dpReqCx; dCz = g_dpReqCz; dNx = g_dpReqNx; dNz = g_dpReqNz;
				}
				if (dpOn)
				{
					float side = (px - dCx) * dNx + (pz - dCz) * dNz;              // < 0: still on our side
					if (side > 1.2f || mode != 2 || wpClimb) dpOn = false;          // through / other business
					else if (g_ppcTraceFrame - dStart > 600) { dpOn = false; dBlockUntil = g_ppcTraceFrame + 180; }
					if (dpOn)
					{
						tx = dCx + dNx * 2.0f; tz = dCz + dNz * 2.0f; dashMode = 1;
						jkWant = true; jkGx = dCx; jkGz = dCz; jkNx = dNx; jkNz = dNz;   // every door: running big jump kick through it
						followMode = 1; gatePush = 1; attack = 0; fstate = 0;   // no escape turns in front of the door
						doorPass = true; g_aiLogWn = 9;
					}
				}
			}
			// Running big jump kick (11/08, every gate and door): back off to 3.2 in front of it (walk), dash at it, Jump
			// once the dash is going and we are within 2.2 (or after 1.5s), Beat in the air (jump frames 8-11), land,
			// again while still wanted. Direction: the door's normal, else bot -> gate latched at the start of each try.
			// ai.txt / steer.txt a75 = 41 back off, 42 run, 43 in the air.
			{
				static int jkSt = 0; static UINT32 jkF = 0; static float gX = 0, gZ = 0, nX = 0, nZ = 0;
				static int jkSpot = -1; static UINT32 jkSpotFlg = 0;
				if (jkWant)
				{
					// (a) a recorded spot within 6 of the gate / door being kicked: kick at the spot instead
					int sp = -1; float bd = 36.0f;
					for (size_t k = 0; k < g_wpk.size(); k++)
					{
						if (g_wpk[k].area != (navKey >> 8) || g_wpkDone[k]) continue;
						float d = (g_wpk[k].x - jkGx) * (g_wpk[k].x - jkGx) + (g_wpk[k].z - jkGz) * (g_wpk[k].z - jkGz);
						if (d < bd) { bd = d; sp = (int) k; }
					}
					if (sp >= 0)
					{
						jkGx = g_wpk[sp].x; jkGz = g_wpk[sp].z;
						if (sp != jkSpot) { jkSpot = sp; jkSpotFlg = arflg; }
						else if (arflg != jkSpotFlg) { g_wpkDone[sp] = true; jkSpot = -1; }   // something happened: this spot is done
					}
					else jkSpot = -1;
				}
				else jkSpot = -1;
				if (!jkWant || mode != 2 || wpClimb) jkSt = 0;
				else
				{
					if (jkSt == 0 || (jkGx - gX) * (jkGx - gX) + (jkGz - gZ) * (jkGz - gZ) > 1.0f)
					{
						gX = jkGx; gZ = jkGz; nX = jkNx; nZ = jkNz;
						if (nX * nX + nZ * nZ < 0.25f) { nX = gX - px; nZ = gZ - pz; }
						float nl = sqrtf(nX * nX + nZ * nZ); if (nl < 0.01f) { nX = 1.0f; nZ = 0.0f; nl = 1.0f; }
						nX /= nl; nZ /= nl;
						float along = (gX - px) * nX + (gZ - pz) * nZ;
						jkSt = along < 2.5f ? 1 : 2; jkF = g_ppcTraceFrame;
					}
					unsigned je = g_ppcTraceFrame - jkF;
					if (jkSt == 1)
					{
						float bx = gX - nX * 3.2f, bz = gZ - nZ * 3.2f;
						tx = bx; tz = bz; dashMode = 0;
						if ((bx - px) * (bx - px) + (bz - pz) * (bz - pz) < 0.36f || je > 90) { jkSt = 2; jkF = g_ppcTraceFrame; }
					}
					else if (jkSt == 2)
					{
						tx = gX + nX * 2.0f; tz = gZ + nZ * 2.0f; dashMode = 1;
						float along = (gX - px) * nX + (gZ - pz) * nZ;
						if ((je >= 12 && along < 2.2f) || je > 90)
						{
							jkSt = 3; jkF = g_ppcTraceFrame;
							// the kick: remember the gate and the camera, for "passed = the camera changed"
							UINT32 kc = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
							g_cpKx = gX; g_cpKz = gZ; g_cpKicked = true; g_cpBx = px; g_cpBz = pz;
							g_cpCam = ((unsigned) ram[(kc + 0xC0) ^ 3] << 8) | ram[(kc + 0xC1) ^ 3];
						}
					}
					else
					{
						tx = gX + nX * 2.0f; tz = gZ + nZ * 2.0f; dashMode = 1;
						jkJump = je < 20; jkBeat = je >= 8 && je < 12;
						if (je >= 45) jkSt = 0;
					}
					followMode = 1; gatePush = 1; gateMash = 0; attack = 0; fstate = 0; stucks = 0;
					g_aiLogA75 = 40 + jkSt;
				}
			}
			bool navPoint = false;   // 11/13 (a): (tx,tz) is a point on the way (floor / wall path), not the goal itself
			float navGoalX = 0.0f, navGoalZ = 0.0f;   // 11/13: the goal behind that point (for the dash heading)
			// If a closed non-gate door (e.g. a start-room door) is right in front of us while walking, steer to its
			// center and push/Beat through it; otherwise, if an active wall segment in RAM (0x106168) blocks the straight
			// line to (tx,tz), steer around the nearer wall endpoint so the bot doesn't get stuck on walls when heading to gates.
			// 11/13 swing door between us and the goal, doorway within 2.5: always the door pass (straight through along its
			// normal) - picked up next frame by the door pass block above, which then holds until 1.2 through.
			if (AI_SWING_DOOR_PASS && followMode && mode == 2 && !gatePush && !wpClimb && !doorPass)
			{
				float sdx, sdz, sdnx, sdnz;
				if (SpikeoutSwingDoorAhead(ram, px, py, pz, tx, tz, 2.5f, &sdx, &sdz, &sdnx, &sdnz))
					SpikeoutDoorPassReq(sdx, sdz, sdnx, sdnz);
			}
			if (followMode && !gatePush && !wpClimb && !doorPass && !(a6d16Direct && tx == tex && tz == tez))
			{
				float bdx = tx, bdz = tz; bool bPush = false;
				if (SpikeoutBlockingDoor(ram, px, py, pz, tx, tz, &bdx, &bdz, &bPush))
				{
					tx = bdx; tz = bdz;
					if (bPush) { fstate = 0; stucks = 0; gatePush = 1; }   // doors open by walking into them: no Beat
				}
				else
				{
					float nwx = tx, nwz = tz, awx = tx, awz = tz;
					// goal height: the enemy's / P1's Rob y when heading for them, else unknown (= our level)
					float ty = NAN;
					if (found && tSlot >= 0 && tSlot < 20 && tx == ex && tz == ez) ty = *(const float *) &ram[0x1A0000 + (unsigned) tSlot * 0x4000 + 0x384] - FN_ENEMY_BODY;
					else if (p1active && tx == p1x && tz == p1z) ty = *(const float *) &ram[p1 + 0x384] - FN_BODY;   // (ty = the goal's feet)
					bool fnJump = false;
					int fn = SpikeoutFloorNav(ram, px, py, pz, tx, ty, tz, &nwx, &nwz, &fnJump);
					// Path method held for 0.5s once picked (11/08): the floor grid finding / not finding a way flipped
					// from frame to frame, and each method sent the bot a different way (back and forth).
					float wnvx = tx, wnvz = tz;
					bool okF = (fn == 1);
					bool okW = SpikeoutWallNav(ram, px, py, pz, tx, tz, &wnvx, &wnvz);
					bool doorP = !okF && !okW && fn == 2 && [&]() -> bool {
						// No way round on the floor grid and a door within 4: it's that door in the way (closed) -
						// go to its doorway and through it along its normal; walking into most doors opens them,
						// and the stall kick takes care of the rest (11/08).
						float dcx, dcz, dnx, dnz; UINT32 dflg;
						int did = SpikeoutNearestDoor(ram, px, py, pz, tx, tz, 4.0f, &dcx, &dcz, &dnx, &dnz, &dflg);
						g_stDoorId = did; g_stDoorFlg = did ? dflg : 0;
						if (!did) { g_stDoorD = -1.0f; return false; }
						float side = (px - dcx) * dnx + (pz - dcz) * dnz;       // < 0: still on our side of it
						g_stDoorD = side;
						if (side > 0.8f) return false;                          // already through
						float lat = fabsf((px - dcx) * dnz - (pz - dcz) * dnx); // off the doorway centre line
						if (lat > 0.6f && side < -0.5f) { tx = dcx - dnx * 0.8f; tz = dcz - dnz * 0.8f; }   // line up in front of it
						else { tx = dcx + dnx * 2.5f; tz = dcz + dnz * 2.5f; fstate = 0; stucks = 0; gatePush = 1; }   // push through
						SpikeoutDoorPassReq(dcx, dcz, dnx, dnz);
						g_aiLogWn = 7;
						return true; }();
					if (!doorP)
					{
						static int held = 0; static UINT32 heldUntil = 0;
						static UINT32 awCommitUntil = 0; static float awCgx = 1e9f, awCgz = 1e9f;
						// Wall-slide commitment: kept up to 3s for the same goal; dropped when stuck (the side didn't work).
						if (g_ppcTraceFrame >= awCommitUntil || (awCgx - tx) * (awCgx - tx) + (awCgz - tz) * (awCgz - tz) > 4.0f || stucks >= 2)
						{ g_awCommitWp = 0; g_awCommitEnd = -1; }
						bool okA = false;
						if ((!okF && !okW) || (held == 5 && g_ppcTraceFrame < heldUntil))
						{
							okA = SpikeoutAvoidWall(ram, px, py, pz, tx, tz, &awx, &awz);
							if (okA && (g_awCommitWp == 0 || g_awCommitWp != g_awLastWp))
							{ g_awCommitWp = g_awLastWp; g_awCommitEnd = g_awLastEnd; awCommitUntil = g_ppcTraceFrame + 180; awCgx = tx; awCgz = tz; }
						}
						int pick = okF ? 4 : (okW ? 6 : (okA ? 5 : 0));
						if (g_ppcTraceFrame < heldUntil && pick != held)
						{
							if (held == 4 && okF) pick = 4;
							else if (held == 6 && okW) pick = 6;
							else if (held == 5 && okA) pick = 5;
						}
						if (pick != held) { held = pick; heldUntil = g_ppcTraceFrame + 30; }   // 0.5s
						float goalX = tx, goalZ = tz;
						if (pick == 4) { tx = nwx; tz = nwz; g_aiLogWn = 4; if (fnJump) floorJumpReq = true; }   // 3D path on the floor grid
						else if (pick == 6) { tx = wnvx; tz = wnvz; if (fn == 2) g_aiLogWn = 6; }               // the 2D path round the walls
						else if (pick == 5) { tx = awx; tz = awz; if (fn == 2) g_aiLogWn = 5; if (g_awLastPush) { g_aiLogWn = 8; g_awPushFrame = g_ppcTraceFrame; SpikeoutDoorPassReq(g_awPushCx, g_awPushCz, g_awPushNx, g_awPushNz); } }   // slide round the nearest wall (committed end) / 8: into a door wall
						navPoint = (tx != goalX || tz != goalZ) && (goalX - px) * (goalX - px) + (goalZ - pz) * (goalZ - pz) > 1.0f;
						// 11/13: hold the point on the way for 30 frames while the goal stays put (within 2) and the target is the
						// same. The next point flipped between two places (the farthest point in sight coming and going, the path
						// rebuilt round the other side when the enemy moved a cell, the wall end swapping) and every flip re-did the
						// dash - the left / right dashing. A new point at once when: reached (0.75), stuck since, the goal moved
						// 2+, the target changed, or no point on the way now.
						{
							static float nhX = 0.0f, nhZ = 0.0f, nhGx = 1e9f, nhGz = 1e9f; static UINT32 nhUntil = 0, nhF = 0; static int nhTgt = -2;
							if (navPoint)
							{
								navGoalX = goalX; navGoalZ = goalZ;
								float gmx = goalX - nhGx, gmz = goalZ - nhGz, hdx = nhX - px, hdz = nhZ - pz;
								bool keep = g_ppcTraceFrame < nhUntil && gmx * gmx + gmz * gmz < 4.0f && tSlot == nhTgt &&
								            hdx * hdx + hdz * hdz > 0.5625f && lastStuckF < nhF;
								if (keep) { tx = nhX; tz = nhZ; }
								else { nhX = tx; nhZ = tz; nhGx = goalX; nhGz = goalZ; nhTgt = tSlot; nhF = g_ppcTraceFrame; nhUntil = g_ppcTraceFrame + 30; }
							}
							else nhUntil = 0;
						}
					}
				}
			}
			else if (!doorPass) g_aiLogWn = 0;
			if (followMode && !doorPass && !evMove && !bhGrab) dashMode = 1;          // every move is a dash (11/08), except a door pass / evading / walking into a grab (set there)
			// An ally right in front (within 0.8, less than 45 degrees off our way): veer 45 degrees to the side away
			// from it, so we don't run into it (bots running into each other kept grabbing each other).
			if (followMode && !gatePush)
			{
				float mdx = tx - px, mdz = tz - pz, ml = sqrtf(mdx * mdx + mdz * mdz);
				if (ml > 0.05f)
				{
					float ux = mdx / ml, uz = mdz / ml, side = 0.0f;
					for (unsigned q = 0; q < estart && q < 8; q++)
					{
						if (q == node - 1) continue;
						UINT32 qr = 0x1A0000 + q * 0x4000;
						if (!(*(const UINT32 *) &ram[qr] & 1) || (short) *(const UINT16 *) &ram[(qr + 0x3F02) ^ 2] <= 0) continue;
						float ax = *(const float *) &ram[qr + 0x380] - px, az = *(const float *) &ram[qr + 0x388] - pz, ad = sqrtf(ax * ax + az * az);
						if (ad < 0.05f || ad > 0.8f) continue;
						if ((ax * ux + az * uz) / ad < 0.7071f) continue;   // not in front
						side = (ux * az - uz * ax) > 0 ? -1.0f : 1.0f;     // ally left of our way: veer right, and the other way round
						break;
					}
					if (side != 0.0f)
					{
						float c = 0.7071f, sn = 0.7071f * side;
						float rx = mdx * c - mdz * sn, rz = mdx * sn + mdz * c;
						tx = px + rx; tz = pz + rz;
					}
				}
			}
			// 11/14 (user): own HP full - keep off HP items on the floor (picked up within 1.0 across / 1.0 in height):
			// one within 1.6 ahead (less than 60 degrees off our way), same level -> veer 60 degrees away from it.
			if (followMode && !gatePush)
			{
				int hpF = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2], hpFM = *(const UINT16 *) &ram[0x106280 ^ 2];
				UINT32 mg = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
				float mdx = tx - px, mdz = tz - pz, ml = sqrtf(mdx * mdx + mdz * mdz);
				// 11/17 (user): walk round HP items when own HP is half or more (was: full), and SP items when the SP stock is
				// full (5).
				const UINT8 avoidBits = (UINT8) ((hpF > 0 && hpFM > 0 && hpF * 2 >= hpFM ? 0x31 : 0) | (ram[(rob + 0x3F25) ^ 3] >= 5 ? 0x04 : 0));
				if (avoidBits && !g_aiItemSeek && ml > 0.05f && mg >= 0x100000 && mg < 0x7FF000)
				{
					float ux = mdx / ml, uz = mdz / ml, side = 0.0f, myYf = *(const float *) &ram[rob + 0x384];
					int cnt = *(const UINT16 *) &ram[(mg + 0x10) ^ 2]; if (cnt > 0x106) cnt = 0x106;
					for (int i = 0; i < cnt && side == 0.0f; i++)
					{
						UINT32 o = 0x1F4000 + (UINT32) i * 0xBC, fl = *(const UINT32 *) &ram[o];
						if (!(fl & 0x10000000) || (fl & 0x00020000) || !(fl & 0x00400008)) continue;
						const UINT8 *dp = SpikeoutMapPtr((*(const UINT32 *) &ram[o + 4] + 0x21) & ~3u, 4);
						if (!dp || !(dp[((*(const UINT32 *) &ram[o + 4] + 0x21) & 3) ^ 3] & avoidBits)) continue;
						float ax = *(const float *) &ram[o + 0x2C] - px, az = *(const float *) &ram[o + 0x34] - pz, ad = sqrtf(ax * ax + az * az);
						if (ad < 0.05f || ad > 1.6f || fabsf(*(const float *) &ram[o + 0x30] - myYf) > 1.5f) continue;
						if ((ax * ux + az * uz) / ad < 0.5f) continue;     // not ahead
						side = (ux * az - uz * ax) > 0 ? -1.0f : 1.0f;    // item left of our way: veer right, and the other way round
					}
					if (side != 0.0f)
					{
						float c = 0.5f, sn = 0.8660f * side;
						float rx = mdx * c - mdz * sn, rz = mdx * sn + mdz * c;
						tx = px + rx; tz = pz + rz;
					}
				}
			}
			g_aiLogTx = tx; g_aiLogTz = tz;
			float dx = tx - px, dz = tz - pz;
			if (fstate == 3 && followMode) { dx = 5.0f * cosf(escA); dz = 5.0f * sinf(escA); }   // random escape heading
			else if (fstate == 1 && followMode)               // along the wall: turn 45, then 90 degrees
			{
				float c = stucks <= 1 ? 0.7071f : 0.0f, s = (stucks <= 1 ? 0.7071f : 1.0f) * side;
				float rx = dx * c - dz * s, rz = dx * s + dz * c;
				dx = rx; dz = rz;
			}
			// Camera-relative steering: the stick is screen/camera-relative, so project the world
			// direction onto the camera's forward/right axes (derived from interest - position, which
			// rotates with the camera). fa_camera: pos +0x10/+0x18 (x/z), interest +0x404/+0x40C (x/z). Read
			// its base from the pointer at 0x104150: the 8-player patch grows fa_player, moving it by 0x80.
			UINT32 cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
			float camx = *(const float *) &ram[cam + 0x10], camz = *(const float *) &ram[cam + 0x18];
			float itrx = *(const float *) &ram[cam + 0x404], itrz = *(const float *) &ram[cam + 0x40C];
			float fx = itrx - camx, fz = itrz - camz;        // camera forward (world, horizontal)
			float m = fx * fx + fz * fz;
			{
				float camy = *(const float *) &ram[cam + 0x14], itry = *(const float *) &ram[cam + 0x408];
				g_stCamY = camy; g_stCamM = sqrtf(m); g_stCamPitch = atan2f(itry - camy, g_stCamM) * 57.29578f;
			}
			if (m > 1e-3f) { float inv = 1.0f / sqrtf(m); fx *= inv; fz *= inv; }
			else { fx = 0.0f; fz = 1.0f; }                   // fallback: calibration had forward = +z
			float fwd = dx * fx + dz * fz;                   // component along camera forward
			float rgt = dx * (-fz) + dz * fx;                // along camera right (= UP x90; calib: RIGHT=-x)
			g_stValid = true; g_stFx = fx; g_stFz = fz; g_stDx = dx; g_stDz = dz; g_stTx = tx; g_stTz = tz;
			g_stFstate = fstate; g_stStucks = stucks; g_stMode = mode; g_stGate = gatePush | (gateMash << 1); g_stDash = dashMode;
			const UINT8 CHG = 0x02, SHIFT = 0x04, JUMP = 0x08;
			const float TWO_PI = 6.2831853f;
			float faceErr = 0.0f;   // how far the enemy is off our facing (radians, 0..pi); only meaningful when fcKnown
			bool fcKnown = false;
			// 11/17 (user): from the game's own table, not learned. 02-04 logs: fcOk never came true (fe -1 all game,
			// a75 122 never) - the bot never turned before a combo since 11/10. Every Rob, players too, keeps the
			// direction to each other Rob at +0x3260 + 2 x obj (obj = that Rob's +0x72), in yang units (dump: = the
			// angle from positions + 180 deg); the game compares its own +0x2A with it (0x247C0, +-0x3000). Facing the
			// target: the signed 16-bit difference is 0 (dump: enemies facing their player 0-18 deg).
			if (found && tSlot >= 0 && tSlot < 20)
			{
				const UINT32 tRob = 0x1A0000 + (unsigned) tSlot * 0x4000;
				const unsigned tObj = ram[(tRob + 0x72) ^ 3];
				if (tObj < 20 && tRob != rob)
				{
					const UINT16 tAng = *(const UINT16 *) &ram[(rob + 0x3260 + 2 * tObj) ^ 2];
					const UINT16 myYang = *(const UINT16 *) &ram[(rob + 0x2A) ^ 2];
					faceErr = fabsf((float) (INT16) (UINT16) (myYang - tAng)) * (TWO_PI / 65536.0f);
					fcKnown = true;
				}
			}
			// 11/25 (owner): the attacks (combos, specials, throws, grabs) are gone. In attack range the bot only turns to
			// its target: the lever toward it while it is 30+ degrees off our facing and we can act (testAim); else it stands.
			// Own action (Rob +0x38), high byte: 00 a move playing, 04 jump, 06 down / getting up, 07 lying, 08-0A hit = busy.
			const UINT32 myAct = *(const UINT32 *) &ram[rob + 0x38];
			const unsigned myCat = myAct >> 24;
			const bool myBusy = (myAct != 0 && myCat == 0x00) || myCat == 0x04 || (myCat >= 0x06 && myCat <= 0x0A);
			g_stMyAct = myAct; g_stFaceErr = (fcKnown && found) ? faceErr * 57.29578f : -1.0f;
			const bool testAim = attack && found && !myBusy && fcKnown && faceErr >= AI_FACE30;
			// 11/25 (owner, 12th): the attack of before today (11/17-11/24, the attack-0 test) back in place of the b-b:
			// state 0 - a combo (0 1 2 5 6 8 at random; a boss: half of them 6; holding a thing: 50% 20 b-b-b, 50% 21 B+C throw). From
			//   its first B, 20 frames: the target's HP down / reeling (yarare 0x00004000) / guarding (0x00040000) = it landed -> state 2,
			//   nothing -> state 1.
			// state 1 - Shift + the lever away from the target, until it is 2.6+ away or 60 frames (at least 8); no target: back to 0.
			// state 2 - another one after us within 2.0: combo 2 first; then one of 0 10%, 1 20%, 19 50%, 16 10%, 17 10%; then state 0.
			// A combo runs to its end, but is dropped when an attack (a B press / a C let-go) has not landed in 20 frames (not 16-18),
			// or when the target is 60+ degrees off our facing (not 5-7's first 10 frames). Within 1.5 of the target no Shift (not
			// 16-18). It starts only when we face the target (30 deg), can act, the target is not down / invincible, and not in a hold,
			// the grab-in, a grab special or the evade.
			static int cbPat = -1; static UINT32 cbStart = 0, cbRng = 0x2545F491u;
			static UINT32 cbForcedUntil = 0;   // 11/25: the forced B+C - B (evade) is not dropped for facing
			bool cbMove = false; UINT8 cbButtons = 0;
			const bool specRun = g_ppcTraceFrame + 120u < g_aiSpecNext;            // a grab special running (its 60 frames)
			bool tgtInvul = false;
			if (found && tSlot >= 0 && tSlot < 20)
			{
				const UINT32 tr2 = 0x1A0000 + (unsigned) tSlot * 0x4000;
				const UINT32 tk = *(const UINT32 *) &ram[tr2 + 0x44];
				// 11/25 (owner): invincible = mot_kind muteki (0x1, EnemyHud MUT) - was the +0x3F22 timer
				// 11/25 (owner): a downed target (down / down_pose) no longer counts - only MUT stops the attacks
				tgtInvul = (tk & 0x00000001u) != 0;
			}
			if (grabbing || grabbed || specRun) cbPat = -1;
			// state 1: out after 2.6 / 60 frames (at least 8), an item walk, or no target
			if (g_aiBotSt == 1 && g_aiItemSeek) { g_aiBotSt = 0; g_aiBotStF = g_ppcTraceFrame; }
			if (g_aiBotSt == 1)
			{
				const unsigned be = g_ppcTraceFrame - g_aiBotStF;
				if (!found || (be >= 8 && bestd >= 6.76f) || be >= 60) { g_aiBotSt = 0; g_aiBotStF = g_ppcTraceFrame; }
			}
			// the target came round us (60+ degrees off) - drop the combo (not 5-7 in their first 10 frames)
			if (cbPat >= 0 && cbPat <= 8 && g_ppcTraceFrame >= cbForcedUntil && fcKnown && found && faceErr > 1.05f && !(cbPat >= 5 && cbPat <= 7 && g_ppcTraceFrame - cbStart < 10u)) cbPat = -1;
			// 11/25 (owner, 13th): the dash stop / the dash attack (from before today) - the first frame in range (attack) while
			// moving at dash speed (0.095+ a frame; a walk is 0.05-0.09, a dash 0.10-0.12): 20% B+C 4 frames (lever kept), else
			// Shift alone 4 frames. No combo starts in those 4 frames.
			static float dsX = 0.0f, dsZ = 0.0f, dsV = 0.0f; static UINT32 dsF = 0, dsStopUntil = 0, dsAtkUntil = 0, dsRng = 0x2545F491u; static bool dsPrevAtk = false;
			if (g_ppcTraceFrame != dsF)                       // called several times a frame: once
			{
				const float mx = px - dsX, mz = pz - dsZ;
				dsV = (dsF + 1 == g_ppcTraceFrame) ? sqrtf(mx * mx + mz * mz) : 0.0f;
				dsX = px; dsZ = pz; dsF = g_ppcTraceFrame;
				if (attack && !dsPrevAtk && dsV >= 0.095f)
				{
					dsRng = dsRng * 1103515245u + 12345u + node;
					if (((dsRng >> 16) % 100u) < 20u && !tgtInvul && !SpikeoutHolding(ram, rob)) dsAtkUntil = g_ppcTraceFrame + 4;
					else dsStopUntil = g_ppcTraceFrame + 4;
					cbPat = -1;
				}
				dsPrevAtk = attack != 0;
			}
			const bool dashStop = attack && g_ppcTraceFrame < dsStopUntil && !grabbing && !grabbed;
			const bool dashAtk = attack && g_ppcTraceFrame < dsAtkUntil && !grabbing && !grabbed;
			// 11/25 (owner): B+C - B forced by the evade (g_aiForceC2) - started at once, in or out of range, facing or not.
			if (g_aiForceC2 && found && !grabbing && !grabbed && !specRun) { g_aiForceC2 = false; cbPat = 2; cbStart = g_ppcTraceFrame; cbForcedUntil = g_ppcTraceFrame + 28u; g_aiBotSt = 0; g_aiBotStF = g_ppcTraceFrame; }
			else if (g_aiForceC2 && !found) g_aiForceC2 = false;
			// pick a combo
			if (cbPat < 0 && attack && found && !evOn && !tgtInvul && !dashStop && !dashAtk && !myBusy && !testAim && g_aiBotSt != 1 && !grabbing && !grabbed && myCat != 0x08 && !specRun)
			{
				cbRng = cbRng * 1103515245u + 12345u + node;
				static UINT32 st2For = 0; static int st2Step = 0;
				if (g_aiBotSt == 2)
				{
					if (st2For != g_aiBotStF) { st2For = g_aiBotStF; st2Step = 0; }   // a new state 2
					if (st2Step == 0)
					{
						bool onMe2 = false;
						for (unsigned e2 = estart; e2 < 20 && !onMe2; e2++)
						{
							if ((int) e2 == tSlot) continue;
							const UINT32 er = 0x1A0000 + e2 * 0x4000;
							if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
							if ((*(const UINT32 *) &ram[er + 0x32E4] & 0x7FFFFF) != (rob & 0x7FFFFF)) continue;
							const float ox = *(const float *) &ram[er + 0x380] - px, oz = *(const float *) &ram[er + 0x388] - pz;
							if (ox * ox + oz * oz < 4.0f) onMe2 = true;
						}
						st2Step = 1;
						if (onMe2) cbPat = 2;
					}
					if (cbPat < 0 && st2Step == 1)
					{
						const unsigned r = (cbRng >> 16) % 100u;
						cbPat = r < 10 ? 0 : r < 30 ? 1 : r < 80 ? 19 : r < 90 ? 16 : 17;
						st2Step = 2;
					}
					else if (cbPat < 0 && st2Step == 2) { g_aiBotSt = 0; g_aiBotStF = g_ppcTraceFrame; st2Step = 0; }
				}
				if (cbPat < 0 && g_aiBotSt == 0)                  // state 0
				{
					static const int s_st0[] = { 0, 1, 2, 5, 6, 8 };
					cbPat = s_st0[(cbRng >> 16) % 6u];
					if (tSlot >= 0 && tSlot < 20 && ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x73) ^ 3] >= 4 && ((cbRng >> 8) & 1)) cbPat = 6;   // a boss: half 6
				}
				if (cbPat >= 0 && g_aiAfterEv) { if (((cbRng >> 20) & 1u) != 0) cbPat = 2; g_aiAfterEv = false; }   // 11/25 (owner): after the evade, 50% B+C - B
				if (cbPat >= 0 && SpikeoutHolding(ram, rob)) cbPat = ((cbRng >> 12) % 10u) < 5u ? 21 : 20;   // holding a thing: b-b-b / throw
				if (cbPat >= 0) cbStart = g_ppcTraceFrame;
			}
			// play it
			if (cbPat >= 0)
			{
				const unsigned e = g_ppcTraceFrame - cbStart, tap = e / 8, ph = e % 8;
				const unsigned wait = 10u, after = cbPat == 6 ? 30u : 10u;
				const unsigned len = cbPat == 20 ? 24u : cbPat == 21 ? 6u : cbPat <= 1 ? 48u : cbPat == 19 ? 40u : cbPat == 2 ? 28u :
				                     (cbPat == 5 || cbPat == 6) ? wait + 24 + after + (cbPat == 6 ? 0u : 10u) : cbPat == 8 ? 16u :
				                     (cbPat >= 16 && cbPat <= 18) ? 92u + 24u * (unsigned) (cbPat - 16) : 30u;
				if (e >= len) cbPat = -1;
				else if (cbPat <= 1) { if (ph < 4) cbButtons = (cbPat == 1 && tap >= 3) ? CHG : BEAT; }
				else if (cbPat == 20) { if (ph < 4 && tap < 3) cbButtons = BEAT; }
				else if (cbPat == 21) { if (e < 6) cbButtons = BEAT | CHG; }
				else if (cbPat == 19) { if (ph < 4) cbButtons = tap >= 3 ? CHG : BEAT; }
				else if (cbPat == 2) { if (e < 6) cbButtons = BEAT | CHG; else if (e >= 14 && e < 18) cbButtons = BEAT; }
				else if (cbPat == 5 || cbPat == 6)
				{
					if (e < wait + 24 + after) cbButtons = CHG;
					if (e < wait) cbButtons |= SHIFT;
					if (e >= wait && e < wait + 24 && ((e - wait) % 8) < 4) cbButtons |= BEAT;
				}
				else if (cbPat == 8) { if (ph < 4 && tap <= 1) cbButtons = BEAT; }
				else if (cbPat >= 16 && cbPat <= 18)
				{
					// Shift + C held, the lever toward the target; b-b pairs (b 4f, 8f apart) 1 / 2 times from frame 10, 24f a pair; C
					// let go once we can act again after the last b (at most 60f), then 10f of nothing.
					static UINT32 st2RelE = 0xFFFFFFFFu;
					const unsigned pairs = (unsigned) (cbPat - 15);
					const unsigned lastB = 22u + 24u * (pairs - 1);
					if (e == 0) st2RelE = 0xFFFFFFFFu;
					if (st2RelE == 0xFFFFFFFFu && e >= lastB && (!myBusy || e >= lastB + 60)) st2RelE = e;
					cbMove = st2RelE == 0xFFFFFFFFu;
					if (st2RelE == 0xFFFFFFFFu) cbButtons = CHG | SHIFT;
					else if (e >= st2RelE + 10) cbPat = -1;
					if (e >= 10 && e < 10 + 24 * pairs) { const unsigned pe2 = (e - 10) % 24; if (pe2 < 4 || (pe2 >= 8 && pe2 < 12)) cbButtons |= BEAT; }
				}
				if (cbPat == 0 || cbPat == 1 || cbPat == 2 || cbPat == 5 || cbPat == 6 || cbPat == 8) cbMove = true;   // these move toward it
			}
			// an attack that does not land: dropped 20 frames after it (a B press / a C let-go), not 16-18
			{
				static UINT8 nhPrev = 0; static bool nhPending = false; static UINT32 nhF = 0; static int nhHp = 0;
				if (cbPat >= 0 && !(cbPat >= 16 && cbPat <= 18) && found && tSlot >= 0 && tSlot < 20)
				{
					const UINT32 nr = 0x1A0000 + (unsigned) tSlot * 0x4000;
					const int nhNow = (short) *(const UINT16 *) &ram[(nr + 0x3F02) ^ 2];
					const UINT32 nk = *(const UINT32 *) &ram[nr + 0x44];
					const bool atk = ((cbButtons & BEAT) && !(nhPrev & BEAT)) || ((nhPrev & CHG) && !(cbButtons & CHG));
					if (atk && !nhPending) { nhPending = true; nhF = g_ppcTraceFrame; nhHp = nhNow; }
					if (nhPending)
					{
						if (nhNow < nhHp || (nk & 0x00044000u)) nhPending = false;
						else if (g_ppcTraceFrame - nhF >= 20) { cbPat = -1; cbButtons = 0; nhPending = false; g_aiLogA75 = 106; }
					}
				}
				else nhPending = false;
				nhPrev = cbButtons;
			}
			// state 0: did the first B land? -> state 2 / state 1
			{
				static UINT8 bsPrev = 0; static bool bsPend = false; static UINT32 bsF = 0; static int bsHp = 0, bsSlot = -1;
				if (g_aiBotSt != 1)
				{
					const bool st0Pat = cbPat == 0 || cbPat == 1 || cbPat == 2 || cbPat == 5 || cbPat == 6 || cbPat == 8;
					if ((cbButtons & BEAT) && !(bsPrev & BEAT) && g_aiBotSt == 0 && st0Pat && found && tSlot >= 0 && tSlot < 20 && !bsPend)
					{
						bsPend = true; bsF = g_ppcTraceFrame; bsSlot = tSlot;
						bsHp = (short) *(const UINT16 *) &ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x3F02) ^ 2];
					}
					if (bsPend)
					{
						const UINT32 br = 0x1A0000 + (unsigned) bsSlot * 0x4000;
						const int bHp = (short) *(const UINT16 *) &ram[(br + 0x3F02) ^ 2];
						const UINT32 bk = *(const UINT32 *) &ram[br + 0x44];
						if (bHp < bsHp || (bk & 0x00044000u)) { bsPend = false; g_aiBotSt = 2; g_aiBotStF = g_ppcTraceFrame; }
						else if (g_ppcTraceFrame - bsF >= 20) { bsPend = false; g_aiBotSt = 1; g_aiBotStF = g_ppcTraceFrame; cbPat = -1; cbButtons = 0; }
					}
				}
				else bsPend = false;
				bsPrev = cbButtons;
			}
			UINT8 buttons = 0;
			// At a gate, every 72 frames: big jump kick = Charge + Jump held 30 frames, then Beat (frames 30-34, still
			// holding); then a kick (Beat). Some gates sit a little high for standing hits. The stick keeps pushing.
			if (jkBeat) buttons |= BEAT;                     // running big jump kick: the kick in the air
			if (r44Beat && g_spikeoutBotCanOpenGate) buttons |= BEAT;   // area 44 hidden route: Beat Door#11/#12 (not with BotCanOpenGate=0)
			if (gateMash)
			{
				const unsigned GATE_HOLD = 30, GATE_CYCLE = 72;
				unsigned gph = g_ppcTraceFrame % GATE_CYCLE;
				if (gph < GATE_HOLD + 4) buttons |= CHG | JUMP;
				if (gph >= GATE_HOLD && gph < GATE_HOLD + 4) buttons |= BEAT;
				else if (gph >= GATE_HOLD + 22 && gph < GATE_HOLD + 26) buttons |= BEAT;
			}
			float th = followMode ? 0.38f * sqrtf(dx * dx + dz * dz) : dead;   // follow: nearest of the 8 directions
			if (th < dead) th = dead;
			// 11/13 (a): a point on the way within 0.5 (the floor path's next cell right by us) fell inside the dead zone -
			// no lever at all, and the bot stood there for 400+ frames (steer.txt: mode 2, wn 4, bits 00). Keep pushing.
			if (navPoint && followMode) { th = 0.38f * sqrtf(dx * dx + dz * dz); if (th < 0.05f) th = 0.05f; }
			if (gatePush) th = 0.1f;                         // gate: keep pushing right up to it
			if (testAim) { th = 0.38f * sqrtf(dx * dx + dz * dz); if (th < 0.05f) th = 0.05f; }   // nearest of the 8 directions to the enemy
			// 11/25 (owner): no attacks - in attack range the lever is used only to turn to it (testAim), no walking up to it.
			if (!attack || testAim)
			{
				if (fwd >  th) bits |= UP; else if (fwd < -th) bits |= DOWN;
				if (rgt >  th) bits |= RIGHT; else if (rgt < -th) bits |= LEFT;
			}
			// Dash (far enemy, the exit): lever toward it, neutral, then toward it held (tap - release - hold).
			// The tap/release is redone when the lever turns away (no direction in common with the held one).
			bool dashEarly = false;
			const unsigned DASH_JUMP_F = 45;                   // jump once the dash has run this long (0.75s)
			{
				// release 3f - tap 3f - release 3f - hold. The first release makes the tap a fresh press even when the
				// stick was already held that way (walking): without it the game saw no tap and we only walked.
				// Still walking (0.05-0.09 per frame; a dash is 0.10-0.12, under 0.05 = blocked) 0.5s into the hold:
				// the dash didn't take - do it again (at most every 0.5s).
				static UINT8 dashDir = 0; static UINT32 dashF = 0, dashChkF = 0; static float dashX = 0, dashZ = 0;
				static float dashHx = 0.0f, dashHz = 0.0f;   // world heading of the running dash (unit)
				static UINT32 rsF = 0; static UINT8 rsDir = 0;   // 11/21: long-dash reset (below), 0 = not running
				static UINT32 rs2F = 0;                          // 11/22: the dash after the 1st reset started here; 15 frames on, the 2nd reset (0 = none)
				static UINT32 rsChgUntil = 0;                    // 11/22 (3rd): C held through the reset until the new dash's start is done
				static UINT32 rsDashF = 0;                       // 11/22 (7th): dashF of a dash started by a reset (its own release 2 - tap 3 - release 4)
				UINT8 dir = bits & DIRS;
				if (dashMode && dir && fstate == 0)
				{
					// A real turn is judged on the WORLD heading, not on the lever bits (11/11): the lever is camera-relative,
					// so when the camera switches (camar / camsar flipping as the bot runs) the same world way becomes another
					// lever direction - that used to restart the dash (release - tap - release) every switch and the bot
					// dashed left / right over and over. Within 60 degrees of the dash heading: same dash, new lever only.
					// 11/13: on a path, judge the dash (turn / distance) by the GOAL, not by the point on the way - those points
					// shift sideways round corners; the lever still goes for the point.
					float hdx0 = navPoint ? navGoalX - px : dx, hdz0 = navPoint ? navGoalZ - pz : dz;
					float hl = sqrtf(hdx0 * hdx0 + hdz0 * hdz0), hx = hl > 1e-4f ? hdx0 / hl : 0.0f, hz = hl > 1e-4f ? hdz0 / hl : 0.0f;
					bool sameWay = dashDir && (hx * dashHx + hz * dashHz) > 0.5f;
					// No new dash start (11/11): within 3 of the target (a dash there overshoots, the bot turns back and dashes
					// again - most of the left/right dashing in steer.txt: the target had not changed, it was just close), or
					// less than 20 frames after the last start. The lever is still held, so a running dash goes on and a
					// walk stays a walk; the "still walking after 0.5s" check below dashes again later if needed.
					static UINT32 dashStartF = 0;               // last real dash start (the 20-frame spacing)
					bool canStart = hl >= 3.0f && g_ppcTraceFrame - dashStartF >= 20;
					bool turned = !(dir & dashDir) && !sameWay;
					if (turned && canStart) { dashDir = dir; dashF = dashStartF = g_ppcTraceFrame; dashChkF = 0; }   // a real turn: dash again
					else
					{
						// A turn we don't dash on: just hold the lever the new way. Skip whatever is left of the last start's
						// release - tap - release (it would tap the new way = a dash after all) by jumping to its hold phase.
						if (turned && g_ppcTraceFrame - dashF < 9) dashF = g_ppcTraceFrame - 9;
						dashDir = dir;                          // same way (diagonal step, or the camera turned), or no new start: keep going
					}
					if (hl > 1e-4f) { dashHx = hx; dashHz = hz; }   // follow gradual turns
					unsigned de = g_ppcTraceFrame - dashF;
					// 11/21 (user): a far enemy (target further than 4) and the dash held 120 frames or more (5th: was 180): Shift
					// alone 6 frames, then the lever the other way 6 frames, then a new dash straight at the enemy (the usual release - tap -
					// release - hold). a75 = 181. (3rd: was a dash the other way, held, then Shift.)
					// 11/22 (user, 2nd): the same thing twice - Shift 6, the other way 6, the dash at it again; 15 frames into
					// that dash: Shift 6, the other way 6, the dash at it again (was: Shift / other way twice in a row).
					// 11/22 (user, 4th-6th): C held from the 1st reset's Shift on, all through both resets and dashes. Twice only.
					// 11/22 (user, 7th - the human's timing, inputlog 00 16:26): Shift + C 4, the other way 4 (C held), the new dash
					// release 2 - tap 3 - release 4, run 26 (the 2nd reset on the new dash's frame 35), C let go on the 2nd new
					// dash's frame 50.
					const bool rsSecond = !rsF && rs2F && g_ppcTraceFrame - rs2F >= 35u;
					if (rsSecond || (!rsF && !rs2F && found && bestd > 16.0f && de >= 9u + 120u))
					{
						rsF = g_ppcTraceFrame ? g_ppcTraceFrame : 1;
						rsDir = (UINT8) (((dashDir & UP) ? DOWN : 0) | ((dashDir & DOWN) ? UP : 0) | ((dashDir & LEFT) ? RIGHT : 0) | ((dashDir & RIGHT) ? LEFT : 0));
						if (rsSecond) rs2F = 0xFFFFFFFFu;            // the 2nd one runs: no 3rd
					}
					if (rsF)
					{
						const unsigned rf = g_ppcTraceFrame - rsF;
						bits &= ~DIRS;
						bits |= CHG;                                   // 11/22 (3rd/4th): C held with Shift and the other-way lever
						if (rf < 4u) bits |= SHIFT;                    // Shift (and C) 4 frames (7th: was 6)
						else bits |= rsDir;                            // the lever the other way 4 frames (7th: was 6)
						g_aiLogA75 = 181;
						if (rf >= 7u)                                  // then the dash at it again
						{
							rsF = 0; dashF = dashStartF = g_ppcTraceFrame; dashChkF = 0; rsDashF = dashF;
							// 11/22 (6th/7th): C never let go in between - after the 1st reset it stays held into the 2nd (which starts
							// on the new dash's frame 35); after the 2nd it is let go on the new dash's frame 50.
							rsChgUntil = g_ppcTraceFrame + (rs2F ? 50u : 35u);
							rs2F = rs2F ? 0u : (g_ppcTraceFrame ? g_ppcTraceFrame : 1u);   // after the 1st: the 2nd in 15 frames; after the 2nd: done
						}
						de = 0; dashEarly = true;
					}
					else
					{
						if (dashF == rsDashF ? (de < 2 || (de >= 5 && de < 9)) : (de < 3 || (de >= 6 && de < 9))) bits &= ~DIRS;   // the releases around the tap (after a reset: 2 / tap 3 / 4)
						if (g_ppcTraceFrame < rsChgUntil) { bits |= CHG; g_aiLogA75 = 181; }   // 11/22 (3rd): C let go once the new dash is going
					}
					if (de < DASH_JUMP_F) dashEarly = true;     // dash still getting going: no jump yet
					if (de == 9) { dashChkF = g_ppcTraceFrame; dashX = px; dashZ = pz; }   // hold starts: measure from here
					else if (dashChkF && g_ppcTraceFrame - dashChkF >= 30)
					{
						float mx = px - dashX, mz = pz - dashZ, v = sqrtf(mx * mx + mz * mz) / (float) (g_ppcTraceFrame - dashChkF);
						if (v >= 0.05f && v < 0.09f && hl >= 3.0f) { dashF = dashStartF = g_ppcTraceFrame; dashChkF = 0; }   // walking (not close to it): dash again
						else { dashChkF = g_ppcTraceFrame; dashX = px; dashZ = pz; }
					}
				}
				else { dashDir = 0; rsF = 0; rs2F = 0; rsChgUntil = 0; rsDashF = 0; }
			}
			prevPressed = ((bits & DIRS) && !attack);
			prevMode = mode;
			bits |= buttons;
			// Step check every 12 frames while the lever is held (11/08): moved less than 0.2 and a step (a wall whose top is
			// 0.25..3.0 above our feet) right ahead the way we want to go -> high jump. The 30-frame stuck window above
			// kept restarting whenever the dash released the lever, so steps were rarely jumped.
			bool stepNow = false;
			{
				static float sjX = 0.0f, sjZ = 0.0f; static UINT32 sjF = 0;
				if (followMode && !gatePush && !attack && (bits & DIRS))
				{
					if (sjF == 0) { sjF = g_ppcTraceFrame; sjX = px; sjZ = pz; }
					else if (g_ppcTraceFrame - sjF >= 12)
					{
						float mx = px - sjX, mz = pz - sjZ;
						if (mx * mx + mz * mz < 0.04f && SpikeoutStepAhead(ram, px, *(const float *) &ram[rob + 0x384] - FN_BODY, pz, dx, dz)) stepNow = true;
						sjF = g_ppcTraceFrame; sjX = px; sjZ = pz;
					}
				}
				else sjF = 0;
			}
			// High jump (held 20f): once a dash gets going, or when stuck at a step (wall data says jumpable)
			if (followMode && !gatePush && (bits & DIRS) && g_ppcTraceFrame >= jumpUntil &&
			    ((stepJumpReq && !dashEarly) || stepNow || (floorJumpReq && g_ppcTraceFrame >= floorJumpNext)))   // no random / after-dash jumps
			{
				jumpUntil = g_ppcTraceFrame + 20;
				stepJumpReq = false;
				if (floorJumpReq) floorJumpNext = g_ppcTraceFrame + 40;
			}
			if (!followMode) stepJumpReq = false;
			// Steps the bots can't walk up: a high jump (held 20f) while within 2 of one, again every 40f while there.
			// Places from 01's waypoints. Only while moving (lever held).
			{
				struct JumpSpot { UINT8 area; float x, z, r; };
				static const JumpSpot spots[] =
				{
					{ 51, -44.305f, 0.090f, 2.0f },   // OPERA area 51: the step before Shutter#36/#37 (11/07)
				};
				static UINT32 spotNext = 0;
				unsigned curArea = ram[0x10402D ^ 3];
				for (const JumpSpot &js : spots)
				{
					float jdx = js.x - px, jdz = js.z - pz;
					if (SPIKEOUT_HAND_TUNED && js.area == curArea && followMode && (bits & DIRS) && jdx * jdx + jdz * jdz < js.r * js.r && g_ppcTraceFrame >= spotNext)
					{
						jumpUntil = g_ppcTraceFrame + 20; spotNext = g_ppcTraceFrame + 40;
					}
				}
			}
			if (gatePush) jumpUntil = 0;
			if (jkJump) bits |= JUMP;                         // running big jump kick: the jump
			if (g_ppcTraceFrame < jumpUntil) bits |= JUMP;
			if (g_ppcTraceFrame < g_wpJumpUntil) bits |= JUMP;   // area 53: big jump at the route's last point
			if (g_awPushFrame == g_ppcTraceFrame) bits |= 0x04;   // pushing into a door wall: hold Shift (11/08)
			if (testAim) g_aiLogA75 = 122;                     // 11/25: in attack range, turning to it (no attacks)
			// 11/17 (user, log 21:44): HP items were still picked up at half HP or more (20 of 37) - the walk-round above only
			// works while heading somewhere and the item is ahead; in a fight the bot stood or stepped onto them (picked
			// within 1.0). Now: an item we don't want (HP items at half HP or more, SP items at SP 5) within 1.3 of us, same
			// level -> step straight away from it (lever only, the combo dropped) until it is 1.3+ away. Not while going for
			// an item, grabbing or held. a75 = 91.
			if (g_aiWeaponPress && !grabbing && !grabbed)   // 11/17: on a weapon - pick it up: B+C, 4 on / 8 off, lever kept
			{
				bits &= ~0x0F;   // 4th: keep the lever (walking at it), only the buttons go
				if ((g_ppcTraceFrame % 12) < 4) { bits &= ~DIRS; bits |= DOWN | BEAT | CHG; }   // 11/18: on it: lever down + B+C
				g_aiLogA75 = 93;
			}
			else if (!g_aiItemSeek && !grabbing && !grabbed)
			{
				const int hpA = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2], hpAM = *(const UINT16 *) &ram[0x106280 ^ 2];
				const UINT8 awayBits = (UINT8) ((hpA > 0 && hpAM > 0 && hpA * 2 >= hpAM ? 0x31 : 0) | (ram[(rob + 0x3F25) ^ 3] >= 5 ? 0x04 : 0));
				const UINT32 mgA = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
				if (awayBits && hpA > 0 && mgA >= 0x100000 && mgA < 0x7FF000)
				{
					const float myYa = *(const float *) &ram[rob + 0x384];
					int cntA = *(const UINT16 *) &ram[(mgA + 0x10) ^ 2]; if (cntA > 0x106) cntA = 0x106;
					float bd = 1.69f, ax = 0.0f, az = 0.0f; bool itNear = false;
					for (int i = 0; i < cntA; i++)
					{
						UINT32 o = 0x1F4000 + (UINT32) i * 0xBC, fl = *(const UINT32 *) &ram[o];
						if (!(fl & 0x10000000) || (fl & 0x00020000) || !(fl & 0x00400008)) continue;
						const UINT8 *dp = SpikeoutMapPtr((*(const UINT32 *) &ram[o + 4] + 0x21) & ~3u, 4);
						if (!dp || !(dp[((*(const UINT32 *) &ram[o + 4] + 0x21) & 3) ^ 3] & awayBits)) continue;
						if (fabsf(*(const float *) &ram[o + 0x30] - myYa) > 1.5f) continue;
						float ix = *(const float *) &ram[o + 0x2C] - px, iz = *(const float *) &ram[o + 0x34] - pz, d = ix * ix + iz * iz;
						if (d < bd) { bd = d; ax = ix; az = iz; itNear = true; }
					}
					if (itNear)
					{
						float vx = -ax, vz = -az, vl = sqrtf(vx * vx + vz * vz);
						if (vl < 0.05f) { vx = -g_stFx; vz = -g_stFz; vl = 1.0f; }   // right on it: back off along the camera
						const float cfx = g_stFx, cfz = g_stFz, f2 = vx * cfx + vz * cfz, r2 = vx * (-cfz) + vz * cfx;
						const float th = 0.38f * vl;
						bits &= ~(DIRS | 0x0F);
						if (f2 > th) bits |= UP; else if (f2 < -th) bits |= DOWN;
						if (r2 > th) bits |= RIGHT; else if (r2 < -th) bits |= LEFT;
						if (!(bits & DIRS)) bits |= DOWN;
						g_aiLogA75 = 91;
					}
				}
			}
			// 11/17 (user): area 83 is a bonus stage (only area 83, though its map is used elsewhere too) - no attack works
			// there. Things lie on the floor: B+C on one picks it up, B+C again throws it at whoever we face. So here:
			// no normal attack at all; empty-handed -> to the nearest thing on the floor (flags 0x00400000, not held
			// 0x00020000, no owner, no pickup-item bits) and B+C on it (4 on / 8 off, a75 94); holding one -> turn to the
			// nearest live enemy (+-30 degrees, the game's own direction table +0x3260 vs our +0x2A), walk up to 4 of it,
			// then B+C once (a75 95), at most every 30 frames. Straight lines (open arena), lever camera-relative.
			if (ram[0x10402D ^ 3] == 83 && !grabbing && !grabbed)
			{
				const unsigned mySlot83 = (rob - 0x1A0000) / 0x4000;
				UINT32 mg83 = *(const UINT32 *) &ram[0x10411C] & 0x7FFFFF;
				bool holding83 = false; int bo = -1; float bod = 1e30f, ox83 = 0.0f, oz83 = 0.0f;
				if (mg83 >= 0x100000 && mg83 < 0x7FF000)
				{
					int c83 = *(const UINT16 *) &ram[(mg83 + 0x10) ^ 2]; if (c83 > 0x106) c83 = 0x106;
					for (int i = 0; i < c83; i++)
					{
						UINT32 o = 0x1F4000 + (UINT32) i * 0xBC, fl = *(const UINT32 *) &ram[o];
						const unsigned own = ram[(o + 0x57) ^ 3];
						if (own == mySlot83 && (fl & 0x00020000u)) { holding83 = true; break; }
						if (own != 0xFF || !(fl & 0x00400000u) || (fl & 0x00020000u)) continue;
						const UINT8 *dp = SpikeoutMapPtr((*(const UINT32 *) &ram[o + 4] + 0x21) & ~3u, 4);
						if (dp && dp[((*(const UINT32 *) &ram[o + 4] + 0x21) & 3) ^ 3]) continue;   // HP / SP / ... items: the item seek's
						float ix = *(const float *) &ram[o + 0x2C] - px, iz = *(const float *) &ram[o + 0x34] - pz, d = ix * ix + iz * iz;
						if (fabsf(*(const float *) &ram[o + 0x30] - (py - 1.15f)) > 1.5f) continue;
						if (d < bod) { bod = d; bo = i; ox83 = ix; oz83 = iz; }
					}
				}
				auto lever83 = [&](float vx, float vz) {
					const float vl = sqrtf(vx * vx + vz * vz); if (vl < 1e-3f) return;
					const float fx2 = g_stFx, fz2 = g_stFz, f2 = vx * fx2 + vz * fz2, r2 = vx * (-fz2) + vz * fx2, th = 0.38f * vl;
					if (f2 > th) bits |= UP; else if (f2 < -th) bits |= DOWN;
					if (r2 > th) bits |= RIGHT; else if (r2 < -th) bits |= LEFT;
				};
				if (!g_aiItemSeek)
				{
					bits &= ~(DIRS | 0x0F);
					if (!holding83)
					{
						if (bo >= 0)
						{
							// 4th: lever on it all the way in; 11/18: on it (1.3) the press frames are lever down + B+C
							if (SpikeoutPickReach(1, bo, bod, px + ox83, pz + oz83) && !myBusy && (g_ppcTraceFrame % 12) < 4) bits |= DOWN | BEAT | CHG;
							else lever83(ox83, oz83);
							g_aiLogA75 = 94;
						}
					}
					else
					{
						int be = -1; float bed = 1e30f, exx = 0.0f, ezz = 0.0f;
						for (unsigned e2 = estart; e2 < 20; e2++)
						{
							UINT32 er = 0x1A0000 + e2 * 0x4000;
							if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
							float vx = *(const float *) &ram[er + 0x380] - px, vz = *(const float *) &ram[er + 0x388] - pz, d = vx * vx + vz * vz;
							if (d < bed) { bed = d; be = (int) e2; exx = vx; ezz = vz; }
						}
						if (be >= 0)
						{
							const unsigned tObj83 = ram[(0x1A0000 + (unsigned) be * 0x4000 + 0x72) ^ 3];
							float fe83 = 0.0f;
							if (tObj83 < 20)
							{
								const UINT16 tA = *(const UINT16 *) &ram[(rob + 0x3260 + 2 * tObj83) ^ 2], myY = *(const UINT16 *) &ram[(rob + 0x2A) ^ 2];
								fe83 = fabsf((float) (INT16) (UINT16) (myY - tA)) * (6.2831853f / 65536.0f);
							}
							static UINT32 thrF = 0;
							if (bed > 16.0f || fe83 >= 0.5236f) lever83(exx, ezz);   // walk up / turn to it
							else if (!myBusy && g_ppcTraceFrame - thrF >= 30) thrF = g_ppcTraceFrame;
							if (g_ppcTraceFrame - thrF < 4) bits |= BEAT | CHG;
							g_aiLogA75 = 95;
						}
					}
				}
			}
			if (evMove)                                         // 11/25 (owner, test): evade mode - Shift held, no buttons
			{
				bits &= ~0x0F;
				bits |= SHIFT;
				g_aiLogA75 = 127;
			}
			// 11/25 (owner, 12th): the combos of state 0 / 2 and the back-off of state 1 (applied over the moves above).
			if (piyoGo) { cbPat = -1; if (g_aiBotSt == 1) { g_aiBotSt = 0; g_aiBotStF = g_ppcTraceFrame; } }   // 11/25: a dizzy boss - no combo / back-off
			if (cbPat >= 0 || g_aiBotSt == 1)
			{
				bits &= ~(DIRS | 0x0F);
				const float vx = ex - px, vz = ez - pz, tf = vx * fx + vz * fz, tr = vx * (-fz) + vz * fx;
				float th2 = 0.38f * sqrtf(vx * vx + vz * vz); if (th2 < 0.05f) th2 = 0.05f;
				if (g_aiBotSt == 1 && found)                     // state 1: Shift + the lever away from the target
				{
					if (-tf > th2) bits |= UP; else if (-tf < -th2) bits |= DOWN;
					if (-tr > th2) bits |= RIGHT; else if (-tr < -th2) bits |= LEFT;
					if (!(bits & DIRS)) bits |= DOWN;
					bits |= SHIFT;
					g_aiLogA75 = 124;
				}
				else
				{
					if (cbMove && found)                          // the patterns that move: the lever toward the target
					{
						if (tf > th2) bits |= UP; else if (tf < -th2) bits |= DOWN;
						if (tr > th2) bits |= RIGHT; else if (tr < -th2) bits |= LEFT;
					}
					UINT8 b = cbButtons;
					if (found && bestd < 2.25f && !(cbPat >= 16 && cbPat <= 18)) b &= ~SHIFT;   // within 1.5: no Shift (not 16-18)
					bits |= b;
					g_aiLogA75 = g_aiBotSt == 2 ? 125 : 123;
				}
			}
			// 11/25 (owner, 13th, from before today): the first frame in range while moving at dash speed: 80% Shift alone 4 frames
			// (the dash stopped, a75 = 120), 20% B+C 4 frames with the lever kept (the dash attack, a75 = 132; not holding a thing
			// or with the target down / invincible).
			if (dashAtk) { bits &= ~0x0F; bits |= BEAT | CHG; g_aiLogA75 = 132; }
			else if (dashStop) { bits &= ~(DIRS | 0x0F); bits |= SHIFT; g_aiLogA75 = 120; }
			// 11/25 (owner): the target down and in the air (mot_kind down 0x00020000 + air 0x08000000, not muteki) within 2:
			// b-b repeated (B 3 frames on, 3 off) for as long as it stays so, no combo. Any height. Not in a hold / grab special.
			// The lever is left as it is. a75 = 137.
			{
				bool jug = false;
				if (found && tSlot >= (int) estart && tSlot < 20 && bestd < AI_ENG_IN2 && !grabbing && !grabbed && !specRun)
				{
					const UINT32 jk = *(const UINT32 *) &ram[0x1A0000 + (unsigned) tSlot * 0x4000 + 0x44];
					jug = (jk & 0x00020000u) && (jk & 0x08000000u) && !(jk & 0x00000001u);
				}
				static UINT32 jugF = 0; static bool jugPrev = false;
				if (jug && !jugPrev) jugF = g_ppcTraceFrame;
				jugPrev = jug;
				if (jug)
				{
					bits &= ~0x0F; cbPat = -1;
					{ const unsigned jp = (g_ppcTraceFrame - jugF) % 35u; if (jp < 5u || (jp >= 15u && jp < 20u)) bits |= BEAT; }   // 11/25: b(5)-(10)-b(5)-(15), 35 frames a round
					g_aiLogA75 = 137;
				}
			}
			// 11/25 (owner): the downed target we kept (g_aiDnKeep) gets up (rise 0x00010000): C held (a75 = 138) until it is no
			// longer rising, then C let go that frame (the charge attack, a75 = 139). Once held, it goes on until the rise ends
			// (or the target is lost / dead, or we are grabbed / grabbing / in a grab special). The lever is left as it is.
			{
				bool rise = false, riseOnMe = false;
				if (found && tSlot >= (int) estart && tSlot < 20)
				{
					const UINT32 rr = 0x1A0000 + (unsigned) tSlot * 0x4000;
					rise = (*(const UINT32 *) &ram[rr + 0x44] & 0x00010000u) && (short) *(const UINT16 *) &ram[(rr + 0x3F02) ^ 2] > 0;
					// 11/25 (owner, 3rd): only one within 2 of us that is after us (its +0x32E4 is our Rob) starts the C hold
					riseOnMe = bestd < 4.0f && (*(const UINT32 *) &ram[rr + 0x32E4] & 0x7FFFFF) == (rob & 0x7FFFFF);
				}
				// 11/25 (owner, 2nd): the rise over -> b-b-b with Shift + C still held (B 4 on / 4 off, 3 times = 24 frames,
				// a75 = 146), then everything let go 2 frames (a75 = 139) -> the usual search. Grabbed / grabbing / a special:
				// let go at once.
				static UINT32 rhRelUntil = 0, rhBbbF = 0; static bool rhBbb = false;
				const bool can = !grabbing && !grabbed && !specRun;
				if (!g_aiRiseHold && !rhBbb && rise && riseOnMe && g_aiDnKeep && can) g_aiRiseHold = true;
				if (g_aiRiseHold && !(rise && can))
				{
					g_aiRiseHold = false;
					if (can) { rhBbb = true; rhBbbF = g_ppcTraceFrame; }
					else rhRelUntil = g_ppcTraceFrame + 2;
				}
				if (rhBbb && (!can || g_ppcTraceFrame - rhBbbF >= 24u)) { rhBbb = false; rhRelUntil = g_ppcTraceFrame + 2; }
				if (g_aiRiseHold) { bits &= ~0x0F; cbPat = -1; bits |= CHG | SHIFT; g_aiLogA75 = 138; }   // 11/25 (owner): Shift held too
				else if (rhBbb)
				{
					bits &= ~0x0F; cbPat = -1; bits |= CHG | SHIFT;
					if (((g_ppcTraceFrame - rhBbbF) % 8u) < 4u) bits |= BEAT;
					g_aiLogA75 = 146;
				}
				else if (g_ppcTraceFrame < rhRelUntil) { bits &= ~0x0F; cbPat = -1; g_aiLogA75 = 139; }
			}
			// 11/25 (owner, from before today): holding a thing and our target within 10: B+C (6 frames) = throw it, facing it
			// (within 30 degrees, when the facing is known). At most every 30 frames. Not in a grab special, a hold, or the bonus
			// stage (area 83 has its own throws). a75 = 95.
			{
				static UINT32 thrUntil = 0, thrNext = 0;
				const int hpT = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2];
				if (found && bestd < 100.0f && hpT > 0 && !grabbing && !grabbed && !specRun && ram[0x10402D ^ 3] != 83 &&
				    g_ppcTraceFrame >= thrNext && (!fcKnown || faceErr < AI_FACE30) && SpikeoutHolding(ram, rob))
				{ thrUntil = g_ppcTraceFrame + 6; thrNext = g_ppcTraceFrame + 30; cbPat = -1; }
				if (g_ppcTraceFrame < thrUntil) { bits &= ~0x0F; bits |= BEAT | CHG; g_aiLogA75 = 95; }
			}
			// 11/25 (owner, 12 boss): the charge from behind a boss (from before today, 11/21-11/24). Our target is a boss
			// (class 4+), alive, within 2.5, same level (1.5 in y); we face it within 15 degrees (own angle Rob +0x2A vs the angle
			// to its object, +0x3260 + 2 * obj) and are outside its front 90 degrees each side (its angle +0x2A vs its angle to us);
			// it is not down / invincible (mot_kind 0x00028001, +0x3F22) / dizzy (0x00000800). Shift + C held 60 frames,
			// the lever toward it while further than 1 (a75 = 170), then let go (10 frames, a75 = 172). 30 frames between.
			// It turns to us (we are in its front 90 degrees) while C is held: C let go that frame, done (a75 = 173).
			// Not grabbing / held / in a grab special.
			{
				static UINT32 bcF = 0, bcEnd = 0; static int bcSlot = -1; static bool bcOn = false;
				const int hpB = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2];
				const unsigned BC_HOLD = 60u;
				if (!bcOn && !g_aiRiseHold && found && tSlot >= (int) estart && tSlot < 20 && !grabbing && !grabbed && !specRun && hpB > 0 &&
				    bestd < 6.25f && g_ppcTraceFrame - bcEnd >= 30u)
				{
					const UINT32 br = 0x1A0000 + (unsigned) tSlot * 0x4000;
					const UINT32 bk = *(const UINT32 *) &ram[br + 0x44];
					const bool bBoss = ram[(br + 0x73) ^ 3] >= 4 && (short) *(const UINT16 *) &ram[(br + 0x3F02) ^ 2] > 0;
					const bool level = fabsf(*(const float *) &ram[br + 0x384] - *(const float *) &ram[rob + 0x384]) < 1.5f;
					float myErr = 3.2f, backErr = 0.0f;
					const unsigned bo = ram[(br + 0x72) ^ 3], mo = ram[(rob + 0x72) ^ 3];
					if (bo < 20) { const UINT16 tA = *(const UINT16 *) &ram[(rob + 0x3260 + 2 * bo) ^ 2], myA = *(const UINT16 *) &ram[(rob + 0x2A) ^ 2];
					               myErr = fabsf((float) (INT16) (UINT16) (myA - tA)) * (6.2831853f / 65536.0f); }
					if (mo < 20) { const UINT16 uA = *(const UINT16 *) &ram[(br + 0x3260 + 2 * mo) ^ 2], bA = *(const UINT16 *) &ram[(br + 0x2A) ^ 2];
					               backErr = fabsf((float) (INT16) (UINT16) (bA - uA)) * (6.2831853f / 65536.0f); }
					if (bBoss && level && myErr < 0.2618f && backErr > 1.5708f &&
					    !(bk & 0x00028801u))   // 11/25 (owner): invincible = MUT (mot_kind 0x1) only, the +0x3F22 timer not looked at
					{ bcOn = true; bcF = g_ppcTraceFrame; bcSlot = tSlot; }
				}
				if (bcOn && (g_ppcTraceFrame < g_aiBossDashUntil || grabbing || grabbed || specRun || hpB <= 0 || tSlot != bcSlot)) { bcOn = false; bcEnd = g_ppcTraceFrame; }
				if (bcOn && bcSlot >= 0 && g_ppcTraceFrame - bcF < BC_HOLD)   // it turned to us: let go now
				{
					const UINT32 tr = 0x1A0000 + (unsigned) bcSlot * 0x4000;
					const unsigned mo = ram[(rob + 0x72) ^ 3];
					if (mo < 20)
					{
						const UINT16 uA = *(const UINT16 *) &ram[(tr + 0x3260 + 2 * mo) ^ 2], bA = *(const UINT16 *) &ram[(tr + 0x2A) ^ 2];
						if (fabsf((float) (INT16) (UINT16) (bA - uA)) * (6.2831853f / 65536.0f) <= 1.5708f)
						{
							bcOn = false; bcEnd = g_ppcTraceFrame;
							bits &= ~(DIRS | 0x0F); cbPat = -1; g_aiLogA75 = 173;
						}
					}
				}
				if (bcOn)
				{
					const unsigned be = g_ppcTraceFrame - bcF;
					bits &= ~(DIRS | 0x0F); cbPat = -1;
					if (be < BC_HOLD)
					{
						bits |= CHG | SHIFT; g_aiLogA75 = 170;
						if (bestd > 1.0f && bcSlot >= 0)
						{
							const UINT32 tr = 0x1A0000 + (unsigned) bcSlot * 0x4000;
							const float vx = *(const float *) &ram[tr + 0x380] - px, vz = *(const float *) &ram[tr + 0x388] - pz;
							const float fw = vx * fx + vz * fz, rg = vx * (-fz) + vz * fx, th = 0.38f * sqrtf(vx * vx + vz * vz);
							UINT8 tw = 0;
							if (fw > th) tw |= UP; else if (fw < -th) tw |= DOWN;
							if (rg > th) tw |= RIGHT; else if (rg < -th) tw |= LEFT;
							bits |= tw ? tw : UP;
						}
					}
					else if (be < BC_HOLD + 10u) g_aiLogA75 = 172;   // let go: the charge attack comes out
					else { bcOn = false; bcEnd = g_ppcTraceFrame; }
				}
			}
			// 11/25 (owner): a boss after us (its +0x32E4 is our Rob; any distance) turns MUT (muteki 0x1) or ATK (attack 0x00800000):
			// go round it, dashing (no Shift, no buttons; it replaces the dash straight away). The side is picked once at the start:
			//   1. the side we are on - the shorter way round to its back (its facing from +0x2A against its angle to us,
			//      +0x3260 + 2 * our object, turned into the world with s_yangSign as for the back grab);
			//   2. right in front of it (within 15 degrees): the side with fewer other enemies within 3 of us;
			//   3. still even (or s_yangSign not learned): by node (odd + / even -, atan2 turns);
			//   4. a wall one step ahead on that side (SpikeoutAvoidWall) and none on the other: the other side.
			// Every frame the lever goes to the point 0.6 rad further round on a circle round it, radius max(distance, 3); the
			// dash taps first (3 off, 3 on, 3 off, then held). Ends after 30 frames, at its back (150+ degrees off its front), once
			// its ATK and MUT are both off (from frame 9), or in a hold / held. a75 = 140 (+), 141 (-). Stops the charge from behind.
			{
				static UINT32 bdF = 0, bdUntil = 0; static int bdSlot = -1, bdDir = 1; static bool bdPrevAtk[20] = { false };
				bool startD = false; int atkBoss = -1;
				for (unsigned e = estart; e < 20; e++)
				{
					const UINT32 er = 0x1A0000 + e * 0x4000;
					bool atkNow = false;
					if ((*(const UINT32 *) &ram[er] & 1) && (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] > 0 && ram[(er + 0x73) ^ 3] >= 4 &&
					    (*(const UINT32 *) &ram[er + 0x32E4] & 0x7FFFFF) == (rob & 0x7FFFFF))
						atkNow = (*(const UINT32 *) &ram[er + 0x44] & 0x00800001u) != 0;   // MUT or ATK (11/25)
					if (atkNow && !bdPrevAtk[e] && atkBoss < 0) atkBoss = (int) e;
					bdPrevAtk[e] = atkNow;
				}
				if (atkBoss >= 0 && !grabbing && !grabbed && g_ppcTraceFrame >= bdUntil) startD = true;
				const unsigned myObjB = ram[(rob + 0x72) ^ 3];
				if (startD)
				{
					bdF = g_ppcTraceFrame; bdUntil = g_ppcTraceFrame + 30; bdSlot = atkBoss;
					const UINT32 br = 0x1A0000 + (unsigned) bdSlot * 0x4000;
					const float bx = *(const float *) &ram[br + 0x380], bz = *(const float *) &ram[br + 0x388];
					const float aMe = atan2f(pz - bz, px - bx);
					int dir = 0;
					if (s_yangSign && myObjB < 20)
					{
						const INT16 itErr = (INT16) (UINT16) (*(const UINT16 *) &ram[(br + 0x2A) ^ 2] - *(const UINT16 *) &ram[(br + 0x3260 + 2 * myObjB) ^ 2]);
						if (abs((int) itErr) >= 0x0AAB)                       // 1. the shorter way to its back
						{
							float df = (float) s_yangSign * ((float) itErr * (6.2831853f / 65536.0f) + 3.14159265f);
							while (df > 3.14159265f) df -= 6.2831853f; while (df < -3.14159265f) df += 6.2831853f;
							dir = df > 0.0f ? 1 : -1;
						}
					}
					if (dir == 0)                                           // 2. in front: fewer enemies within 3 of us
					{
						int nPos = 0, nNeg = 0;
						for (unsigned e = estart; e < 20; e++)
						{
							if ((int) e == bdSlot) continue;
							const UINT32 er = 0x1A0000 + e * 0x4000;
							if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
							const float ox = *(const float *) &ram[er + 0x380], oz = *(const float *) &ram[er + 0x388];
							if ((ox - px) * (ox - px) + (oz - pz) * (oz - pz) >= 9.0f) continue;
							float da = atan2f(oz - bz, ox - bx) - aMe;
							while (da > 3.14159265f) da -= 6.2831853f; while (da < -3.14159265f) da += 6.2831853f;
							if (da > 0.0f) nPos++; else nNeg++;
						}
						if (nPos != nNeg) dir = nPos < nNeg ? 1 : -1;
						else dir = (node & 1) ? 1 : -1;                     // 3. by node
					}
					{                                                       // 4. a wall one step ahead on that side only: the other
						const float dd = sqrtf((px - bx) * (px - bx) + (pz - bz) * (pz - bz)), R = dd > 3.0f ? dd : 3.0f;
						float awx = 0.0f, awz = 0.0f;
						const bool wA = SpikeoutAvoidWall(ram, px, py, pz, bx + R * cosf(aMe + 0.6f * dir), bz + R * sinf(aMe + 0.6f * dir), &awx, &awz);
						const bool wB = SpikeoutAvoidWall(ram, px, py, pz, bx + R * cosf(aMe - 0.6f * dir), bz + R * sinf(aMe - 0.6f * dir), &awx, &awz);
						if (wA && !wB) dir = -dir;
					}
					bdDir = dir;
				}
				if (g_ppcTraceFrame < bdUntil && (grabbing || grabbed)) bdUntil = 0;
				if (g_ppcTraceFrame < bdUntil && bdSlot >= 0)
				{
					const UINT32 br = 0x1A0000 + (unsigned) bdSlot * 0x4000;
					const unsigned de = g_ppcTraceFrame - bdF;
					const UINT32 bk = *(const UINT32 *) &ram[br + 0x44];
					bool atBack = false;
					if (myObjB < 20)
					{
						const INT16 itErr = (INT16) (UINT16) (*(const UINT16 *) &ram[(br + 0x2A) ^ 2] - *(const UINT16 *) &ram[(br + 0x3260 + 2 * myObjB) ^ 2]);
						atBack = abs((int) itErr) >= 0x8000 - 0x1555;      // within 30 degrees of its back
					}
					const bool alive = (*(const UINT32 *) &ram[br] & 1) && (short) *(const UINT16 *) &ram[(br + 0x3F02) ^ 2] > 0;
					if (!alive || atBack || (de >= 9 && !(bk & 0x00800001u))) bdUntil = 0;
				}
				g_aiBossDashUntil = bdUntil;
				if (g_ppcTraceFrame < bdUntil && bdSlot >= 0)
				{
					const UINT32 br = 0x1A0000 + (unsigned) bdSlot * 0x4000;
					const float bx = *(const float *) &ram[br + 0x380], bz = *(const float *) &ram[br + 0x388];
					const float dd = sqrtf((px - bx) * (px - bx) + (pz - bz) * (pz - bz)), R = dd > 3.0f ? dd : 3.0f;
					const float a = atan2f(pz - bz, px - bx) + 0.6f * (float) bdDir;
					const float vx = bx + R * cosf(a) - px, vz = bz + R * sinf(a) - pz;
					const float fw = vx * fx + vz * fz, rg = vx * (-fz) + vz * fx, th = 0.38f * sqrtf(vx * vx + vz * vz);
					UINT8 aw = 0;
					if (fw > th) aw |= UP; else if (fw < -th) aw |= DOWN;
					if (rg > th) aw |= RIGHT; else if (rg < -th) aw |= LEFT;
					if (!aw) aw = DOWN;
					const unsigned de = g_ppcTraceFrame - bdF;
					bits &= ~(DIRS | 0x0F); cbPat = -1;
					if ((de >= 3 && de < 6) || de >= 9) bits |= aw;
					g_aiLogA75 = bdDir > 0 ? 140 : 141;
				}
			}
			// 11/25 (owner): our downed boss (g_aiBossDownSlot, see the target search). Waiting for it (no other enemy within 2):
			// stand - no lever, no buttons (a75 = 142). It starts getting up (rise 0x00010000) while we were waiting: dash straight
			// away from it 30 frames, no Shift, no buttons - the lever away: 3 off, 3 on, 3 off, then held (a75 = 143); its get-up
			// is nearly always MUT. Not in a hold / held. It wins over the going round (above) and stops the charge from behind.
			{
				static UINT32 rdF = 0, rdUntil = 0; static int rdSlot = -1; static bool rdPrevRise = false, rdWasWait = false;
				bool riseNow = false;
				if (g_aiBossDownSlot >= 0)
					riseNow = (*(const UINT32 *) &ram[0x1A0000 + (unsigned) g_aiBossDownSlot * 0x4000 + 0x44] & 0x00010000u) != 0;
				if (riseNow && !rdPrevRise && rdWasWait && !grabbing && !grabbed) { rdF = g_ppcTraceFrame; rdUntil = g_ppcTraceFrame + 30; rdSlot = g_aiBossDownSlot; }
				rdPrevRise = riseNow;
				rdWasWait = g_aiBossDownWait;
				if (g_ppcTraceFrame < rdUntil && (grabbing || grabbed)) rdUntil = 0;
				if (g_ppcTraceFrame < rdUntil && rdSlot >= 0)
				{
					if (rdUntil > g_aiBossDashUntil) g_aiBossDashUntil = rdUntil;
					const UINT32 br = 0x1A0000 + (unsigned) rdSlot * 0x4000;
					const float vx = px - *(const float *) &ram[br + 0x380], vz = pz - *(const float *) &ram[br + 0x388];   // away from it
					const float fw = vx * fx + vz * fz, rg = vx * (-fz) + vz * fx, th = 0.38f * sqrtf(vx * vx + vz * vz);
					UINT8 aw = 0;
					if (fw > th) aw |= UP; else if (fw < -th) aw |= DOWN;
					if (rg > th) aw |= RIGHT; else if (rg < -th) aw |= LEFT;
					if (!aw) aw = DOWN;
					const unsigned de = g_ppcTraceFrame - rdF;
					bits &= ~(DIRS | 0x0F); cbPat = -1;
					if ((de >= 3 && de < 6) || de >= 9) bits |= aw;
					g_aiLogA75 = 143;
				}
				else if (g_aiBossDownWait && found && tSlot == g_aiBossDownSlot && !grabbing && !grabbed)
				{
					// 11/25 (owner, 2nd): once each time we start waiting on it: Shift held, the lever once round clockwise (right,
					// down, left, up, 6 frames each, after 2 frames of Shift alone) + B on the up (its frames 2-5), then Shift let
					// go (a75 = 144); after that stand (a75 = 142).
					static int wtSlot = -1; static UINT32 wtF = 0, wtLastF = 0;
					if (wtSlot != g_aiBossDownSlot || g_ppcTraceFrame - wtLastF > 2u) { wtSlot = g_aiBossDownSlot; wtF = g_ppcTraceFrame; }
					wtLastF = g_ppcTraceFrame;
					const unsigned TN = 2u, TD = 6u, TR = TN + 4u * TD;
					static const UINT8 RW[4] = { 0x40 /*RIGHT*/, 0x10 /*DOWN*/, 0x80 /*LEFT*/, 0x20 /*UP*/ };
					const unsigned e = g_ppcTraceFrame - wtF;
					bits &= ~(DIRS | 0x0F); cbPat = -1;
					if (e < TR)
					{
						bits |= SHIFT;
						if (e >= TN)
						{
							const unsigned r = e - TN;
							bits |= RW[r / TD];
							if (r >= 3u * TD + 2u && r < 3u * TD + 6u) bits |= BEAT;
						}
						g_aiLogA75 = 144;
					}
					else g_aiLogA75 = 142;
				}
			}
			// 11/25 (owner): the old grab handling brought back (from before the 11/25 reset) - its specials (grabSpecial, above):
			// B+C+J mashed 60 frames (4 on / 4 off), again 3 s after the start at the earliest.
			{
				static UINT32 specUntil = 0;
				const int hpNow = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2];
				if (grabSpecial && hpNow > 0 && g_ppcTraceFrame >= g_aiSpecNext) { specUntil = g_ppcTraceFrame + 60; g_aiSpecNext = g_ppcTraceFrame + 180; }
				if (g_ppcTraceFrame < specUntil) { bits &= ~0x0F; if (((specUntil - g_ppcTraceFrame) % 8u) >= 4u) bits |= BEAT | CHG | JUMP; }
			}
			// Grabs (11/10, 2nd try): dashing away did nothing - a bot holding an ally (04) kept 02 for good, both
			// "dashing". Held: mash out - the lever round the 4 ways (4 frames each) and Beat 4 on / 4 off. Holding an
			// ally: let go with a throw - lever forward + Beat 4 on / 4 off.
			static UINT32 bhF = 0; static bool bhPrev = false;
			if (grabBossHold && !bhPrev) bhF = g_ppcTraceFrame;   // 11/21: a boss's hold on us just started
			bhPrev = grabBossHold;
			if (grabEsc)
			{
				const UINT8 DIRS4[4] = { UP, RIGHT, DOWN, LEFT };
				bits &= ~(DIRS | 0x0F);
				if (grabBossHold)
				{
					// 11/21 (user): held by a boss - the lever round clockwise (right, down, left, up), 6 frames a way (the
					// timing of the other lever turns), after 2 frames neutral, on and on until the hold ends. No buttons.
					static const UINT8 CWB[4] = { RIGHT, DOWN, LEFT, UP };
					const unsigned be = g_ppcTraceFrame - bhF;
					if (be >= 2u) bits |= CWB[((be - 2u) / 6u) & 3u];
				}
				else if (grabbed)
				{
					bits |= DIRS4[(g_ppcTraceFrame >> 2) & 3];
					if ((g_ppcTraceFrame & 7) < 4) bits |= BEAT;
				}
				else
				{
					// 11/17 (user): holding an ally - lever left, down, right, up (4 frames each) with Beat on the up, round
					// and round until we let go (was: lever forward + Beat 4 on / 4 off).
					const UINT8 ROT4[4] = { LEFT, DOWN, RIGHT, UP };
					const unsigned st = (g_ppcTraceFrame >> 2) & 3;
					bits |= ROT4[st];
					if (st == 3) bits |= BEAT;
				}
				g_aiLogA75 = grabbed ? 71 : 72;
			}
			// Holding an enemy (11/10): Beat twice (knees, 12 frames apart), then from frame 28 a throw - the lever toward
			// another live enemy (the nearest to us, to bowl it over), or forward if there is none - with Beat 4 on / 8
			// off until the hold ends.
			// 11/12 back grab: the grab motion (Rob +0x3C at the hold) tells the side - 1118 front, 749 from behind
			// (mot_kind2 usiro_nage 0x2000), 1287/1289 the sides (grablog.txt). Not from behind: Charge 20 frames (goes
			// round to its back); now from behind: Jump 20 frames, then Beat (4 on / 8 off). The back-hold moves after
			// the jump keep usiro_nage but may drop tsukami, so the sequence runs on while usiro_nage stays (up to 90 f).
			// Still not from behind after the Charge: the old knees + throw.
			{
				static UINT32 gkStart = 0, gkLast = 0; static UINT8 gkDir = 0;
				static int gkPh = 0; static UINT32 gkPhF = 0;   // 1 charging, 2 back: jump, 3 back: beat, 4 knees + throw
				static bool gkSide = false;                     // 11/17: this hold started from a side (C instead of B-B-C)
				int gkMot = (short) *(const UINT16 *) &ram[(rob + 0x3C) ^ 2];
				bool gkBack = gkMot == 749 || (myK2 & 0x00002000u) != 0;
				// 11/13: the lever must stay neutral from the grab until the jump is done. Going round to its back can drop
				// the hold flag for a moment (or put an ally nearer than the enemy): the normal steering then pushed the
				// lever at the enemy and spoilt it. So once a hold has started, keep it (all neutral here) while we hold
				// anyone / usiro_nage is up, or for 20 frames after that; the jump phase always runs its full 40 frames.
				static UINT32 gkSeenF = 0;
				if (grabEnemy || gkBack) gkSeenF = g_ppcTraceFrame;
				// 11/17 (log 15:01): the hold flag drops while the knee plays (own action 0800045E -> 080007B2 / 07B4, the
				// whole hold is action group 08), so the hold was given up 20 frames later - before the C of B-B-C at 24 -
				// and the normal steering (Shift + lever away) ran on while we still held it. A hold we are in keeps going
				// while our own action stays in group 08 (up to the 240-frame cap below).
				if ((*(const UINT32 *) &ram[rob + 0x38] >> 24) == 0x08 && g_ppcTraceFrame - gkLast <= 1 && gkPh >= 1 && gkPh <= 3) gkSeenF = g_ppcTraceFrame;
				bool gkCont = ((gkPh >= 1 && gkPh <= 3) || gkPh == 5) && g_ppcTraceFrame - gkLast <= 1 && g_ppcTraceFrame - gkPhF < 240;
				// 11/14 (user): Linda's special for a BOSS held from the FRONT (mot 1118) - a human's input recorded with InputLog
				// on 00 (inputlog.txt, the 07:42 session, f=278-503, 226 frames, chr 2, area 4), buttons only, lever neutral.
				// Played to the end even when the hold is over (the recording goes on into the moves after it).
				struct GkRun { UINT8 n; UINT8 btn; };
				// 11/14 re-recorded (user: the timing is tight - same frames): inputlog.txt 09:42, grab (mot 1118) at f=2708,
				// played from THAT frame (frame 0 = the first frame of mot 1118, tracked below) to f=2841 (134 frames):
				// B B B (the 3rd with C held), C, C+J (with B), release C. (The 07:42 recording started 12 frames late.)
				static const GkRun s_lindaBoss[] = {
					{ 10, 0 }, { 5, BEAT }, { 9, 0 }, { 9, BEAT }, { 6, 0 }, { 16, CHG }, { 8, BEAT | CHG },
					{ 37, CHG }, { 25, BEAT | CHG | JUMP }, { 5, CHG }, { 3, 0 },   // 11/15 (user): B and J held together 100-124 (released at 125), C released at 130
				};
				const unsigned LINDA_BOSS_LEN = 133;   // 11/15: B+J 100-124, C off at 130 (sum of the runs above)
				static int gkPrevMot = -1; static UINT32 gk1118F = 0;   // the frame our motion turned 1118 (the grab)
				if (gkMot == 1118 && gkPrevMot != 1118) gk1118F = g_ppcTraceFrame;
				gkPrevMot = gkMot;
				bool gkLinda = gkPh == 6 && g_ppcTraceFrame - gkLast <= 1 && g_ppcTraceFrame - gkPhF < LINDA_BOSS_LEN;
				bool gkOn = !grabSpecial && (grabEnemy || gkLinda || (gkCont && (gkPh == 2 || grabbing || gkBack || (gkPh != 3 && g_ppcTraceFrame - gkSeenF < 20) ||
				                                               (gkPh == 3 && g_ppcTraceFrame - gkPhF < 80))));   // after the jump: over as soon as the hold is (11/17: but the jump - up to its top and the C there, 80 frames - always)
				if (gkOn)
				{
					// 11/17 (user, log 17:09): the throw failed, we landed, took it from behind again (0800020D -> 02ED,
					// motion 749) and stood there for 250-650 frames - still "after the jump" (76), lever only, no J. Back
					// at a back hold more than 10 frames after the jump (the jump itself is motion 2416): a new hold.
					if (gkPh == 3 && g_ppcTraceFrame - gkPhF > 10 && gkMot == 749) gkLast = 0;
					if (g_ppcTraceFrame - gkLast > 1)   // a new hold (11/13: the input is read several times a frame - the same frame is the same hold, not a new one)
					{
						gkStart = g_ppcTraceFrame; gkDir = 0; gkPh = gkMot == 749 ? 2 : 1; gkPhF = g_ppcTraceFrame;
						gkSide = gkMot == 1287 || gkMot == 1289;   // 11/17: held from a side (grablog.txt motions)
						// Linda (Rob +0x3F00 = 2) holding a boss (class Rob +0x73 >= 4) from the front: her special instead
						bool bossHeld = grabEnemy && grabIdx >= 0 && (unsigned) grabIdx >= (unsigned) pcnt && ram[(0x1A0000 + (unsigned) grabIdx * 0x4000 + 0x73) ^ 3] >= 4;
						(void) bossHeld;   // 11/14 (user): not only bosses any more - any enemy held from the front
						if (false && gkMot == 1118 && grabEnemy && ram[(rob + 0x3F00) ^ 3] == 2)   // 11/17: not in the test (B-B-C like the rest)
						{
							gkPh = 6;
							if (g_ppcTraceFrame - gk1118F < 30) gkPhF = gk1118F;   // count from the frame the grab motion started
						}
					}
					gkLast = g_ppcTraceFrame;
					bits &= ~(DIRS | 0x0F);
					unsigned pe = g_ppcTraceFrame - gkPhF;
					// 11/13 (new logic): not 749 -> mash Charge (3 on / 3 off, lever neutral) to go round to its back;
					// 749 -> mash Jump (3 on / 3 off) until we jump (own mot_kind jump 0x00080000, or the motion leaves 749),
					// at most 60 frames; jumped -> Beat (4 on / 8 off). Lever neutral all the way (DIRS cleared above).
					UINT32 gkMyK1 = *(const UINT32 *) &ram[rob + 0x44];
					if (gkPh == 1)
					{
						// 11/17 (user): held from the front (or a side): B, B, C - 12 frames apart, 4 frames each (the old knee
						// timing) - then wait; once it is a back hold (749) -> Jump (phase 2), then C (phase 3). Not round to
						// its back 20 frames after the C: B, B, C again. (Was: Charge x3, then Beat with the lever in - phase 5.)
						// 11/17 (user, 2nd): held from a side: C alone (4 frames) to go round to its back, then Jump, C. Not round
						// 20 frames after the C: C again.
						// 11/17 (log 16:30): going round to its back takes ~50 frames (own action 0800046B from the front,
						// 08000215 / 08000211 from a side). B-B-C / C started again in it (48 / 24 frames) and that C, read at
						// the back hold (749), made a different move (0800 02FB, motion 763) - the J never came. While going
						// round: nothing pressed, and the count held just after the C so it does not start again.
						const UINT32 gkAct = *(const UINT32 *) &ram[rob + 0x38];
						const bool gkRound = gkAct == 0x0800046Bu || gkAct == 0x08000215u || gkAct == 0x08000211u;
						if (gkMot == 749) { gkPh = 2; gkPhF = g_ppcTraceFrame; pe = 0; }
						else if (gkRound) { pe = gkSide ? 4 : 28; gkPhF = g_ppcTraceFrame - pe; }   // hands off (a75 74 / 80 stays)
						else if (gkSide) { if (pe < 4) bits |= CHG; else if (pe >= 24) { gkPhF = g_ppcTraceFrame; pe = 0; } }
						else if (pe < 4 || (pe >= 12 && pe < 16)) bits |= BEAT;
						else if (pe >= 24 && pe < 28) bits |= CHG;
						else if (pe >= 48) { gkPhF = g_ppcTraceFrame; pe = 0; }   // B, B, C again
					}
					if (gkPh == 2)
					{
						// 11/17 (user): the J came too early (back hold from a side, and from the start) - wait 8 frames after the
						// back hold (749), then hold J down until we jump (was: J 3 on / 3 off from the first frame). At most 60
						// frames of J (68 in all).
						const unsigned GK_J_WAIT = 8;
						if ((gkMyK1 & 0x00080000u) || (gkMot != 749 && pe > 0) || pe >= GK_J_WAIT + 60) { gkPh = 3; gkPhF = g_ppcTraceFrame; pe = 0; }
						else if (pe >= GK_J_WAIT) bits |= JUMP;
					}
					// 11/17 (user): after the jump, C once - 4 frames. 3rd (user): the move comes out only near the top of the
					// jump, not once on the way down. Log 16:42: the jump (0800 0970, motion 2416) sinks for 8 frames, rises
					// from ~10, tops out at ~36-40 (+1.0), lands ~70; C at 4 (on the ground) and at 29-32 (still rising) were
					// read (chg 4) but did nothing. So: our height (Rob +0x384) - once it has risen 0.5+ and stops rising
					// (not above the highest so far), C for 4 frames. No top found: no C.
					static float gkY0 = 0.0f, gkYmax = 0.0f; static UINT32 gkCF = 0, gkYF = 0;
					if (gkPh == 3)
					{
						const float gkY = *(const float *) &ram[rob + 0x384];
						if (pe == 0) { gkY0 = gkY; gkYmax = gkY; gkCF = 0; gkYF = g_ppcTraceFrame; }
						else if (gkYF != g_ppcTraceFrame)          // once a frame (read several times a frame)
						{
							gkYF = g_ppcTraceFrame;
							if (!gkCF && gkYmax > gkY0 + 0.5f && gkY <= gkYmax) gkCF = g_ppcTraceFrame;   // stopped rising: the top
							if (gkY > gkYmax) gkYmax = gkY;
						}
						// 11/17 (user, 5th; human InputLog on 00, 17:18, 7 throws): after J the jump is motion 2416; the human
						// started B or C 9-17 frames into it, every 8-9 frames, and the throw (motion 2564) came at 22-26 -
						// one C at 29+ (or at the top, 38+) was too late. So: C 4 on / 4 off from frame 8 until the throw
						// (2564) or frame 40. The top found above (gkCF) is no longer used for the C.
						(void) gkCF;
						static bool gkThrown = false;
						if (pe == 0) gkThrown = false;
						if (gkMot == 2564) gkThrown = true;
						if (!gkThrown && pe >= 8 && pe < 40 && ((pe - 8) % 8) < 4) bits |= CHG;
						// 11/17 (user, 4th): the C at the top throws the enemy we hold - the lever may have to say which way.
						// Lever held all through the jump: away from the nearest ally within 3 (11/24), else toward the nearest other enemy (11/23);
						// none: a random one of the 8 ways (world direction, picked once per jump; camera-relative each frame).
						static UINT32 gkTwF = 0; static float gkTwX = 0.0f, gkTwZ = 1.0f; static UINT32 gkTwRng = 0x9E3779B9u;
						if (gkTwF != gkPhF)
						{
							gkTwF = gkPhF;
							// 11/24 (owner): an ally near (players 0-7, alive, not us, within 3) -> away from the nearest one;
							// else toward the nearest other live enemy (11/23); none of either: a random way, as before.
							float ad2 = 9.0f, alx = 0.0f, alz = 0.0f; bool ally = false;
							for (unsigned s2 = 0; s2 < 8; s2++)
							{
								UINT32 ar = 0x1A0000 + s2 * 0x4000;
								if (ar == rob || !(*(const UINT32 *) &ram[ar] & 1) || (short) *(const UINT16 *) &ram[(ar + 0x3F02) ^ 2] <= 0) continue;
								float ox = *(const float *) &ram[ar + 0x380] - px, oz = *(const float *) &ram[ar + 0x388] - pz;
								if (ox * ox + oz * oz < ad2) { ad2 = ox * ox + oz * oz; alx = ox; alz = oz; ally = true; }
							}
							if (ally && ad2 > 1e-4f) { float inv = 1.0f / sqrtf(ad2); gkTwX = -alx * inv; gkTwZ = -alz * inv; }
							else
							{
								// 11/23 (owner): toward the nearest other live enemy instead (was: away from the nearest ally within 3).
								// The one we hold (mot_kind2 Rob +0x48 held 0x00100000) is skipped. None: a random way, as before.
								float bd2 = 1e30f, ax = 0.0f, az = 0.0f; bool foe = false;
								for (unsigned s2 = estart; s2 < 20; s2++)
								{
									UINT32 ar = 0x1A0000 + s2 * 0x4000;
									if (!(*(const UINT32 *) &ram[ar] & 1) || (short) *(const UINT16 *) &ram[(ar + 0x3F02) ^ 2] <= 0) continue;
									if (*(const UINT32 *) &ram[ar + 0x48] & 0x00100000u) continue;   // held (the one we throw)
									float ox = *(const float *) &ram[ar + 0x380] - px, oz = *(const float *) &ram[ar + 0x388] - pz;
									if (ox * ox + oz * oz < bd2) { bd2 = ox * ox + oz * oz; ax = ox; az = oz; foe = true; }
								}
								if (foe && bd2 > 1e-4f) { float inv = 1.0f / sqrtf(bd2); gkTwX = ax * inv; gkTwZ = az * inv; }
								else
								{
									gkTwRng = gkTwRng * 1103515245u + 12345u + node;
									float a = (float) ((gkTwRng >> 16) & 7) * 0.7853982f;
									gkTwX = sinf(a); gkTwZ = cosf(a);
								}
							}
						}
						if (*(const UINT32 *) &ram[rob + 0x38] != 0x0800020Du)   // not while taking it from behind again (grab-in)
						{
							float fx = g_stFx, fz = g_stFz, fwd = gkTwX * fx + gkTwZ * fz, rgt = gkTwX * (-fz) + gkTwZ * fx;
							const float th = 0.38f;
							if (fwd > th) bits |= UP; else if (fwd < -th) bits |= DOWN;
							if (rgt > th) bits |= RIGHT; else if (rgt < -th) bits |= LEFT;
						}
					}
					static int gkEnemy = -1;                       // the enemy we hold (Rob index), for the lever in the Beat phase
					if (grabEnemy && grabIdx >= 0) gkEnemy = grabIdx;
					// 11/13: after the jump (phase 3) nothing - the game lets go of it by itself on landing; all neutral until
					// the hold is over. Phase 5 (Charge x3 did not get round to its back): Beat with the lever in - toward the
					// enemy we hold (camera-relative 8 ways), forward if we lost track of it.
					if (gkPh == 5)
					{
						UINT8 lv = 0;
						if (gkEnemy >= 0)
						{
							UINT32 er = 0x1A0000 + (unsigned) gkEnemy * 0x4000;
							float vx = *(const float *) &ram[er + 0x380] - px, vz = *(const float *) &ram[er + 0x388] - pz;
							float fx = g_stFx, fz = g_stFz, fwd = vx * fx + vz * fz, rgt = vx * (-fz) + vz * fx;
							float th = 0.38f * sqrtf(vx * vx + vz * vz);
							if (fwd > th) lv |= UP; else if (fwd < -th) lv |= DOWN;
							if (rgt > th) lv |= RIGHT; else if (rgt < -th) lv |= LEFT;
						}
						bits |= lv ? lv : UP;
						if ((pe % 6) < 3) bits |= BEAT;                   // 11/13: Beat mashed (3 on / 3 off), lever in
					}
					if (gkPh == 6)                                    // Linda, boss held from the front: the recorded input
					{
						unsigned le = pe;
						for (const GkRun &r : s_lindaBoss) { if (le < r.n) { bits |= r.btn; break; } le -= r.n; }
					}
					g_aiLogA75 = (gkPh == 1 && gkSide) ? 80 : 73 + gkPh;   // 80 side: C (11/17), 74 B-B-C (11/17; was charging), 75 back: jump, 76 after the jump (11/17: C once), 77 knees + throw (unused), 78 Beat + lever (11/17: no longer entered), 79 Linda's boss special
				}
				if (gkOn && gkPh == 4)
				{
					unsigned ge = g_ppcTraceFrame - gkStart;
					if (ge < 4 || (ge >= 12 && ge < 16)) bits |= BEAT;                 // knee, knee
					else if (ge >= 28)
					{
						if (!gkDir)
						{
							float bd2 = 1e30f, vx = 0.0f, vz = 0.0f;
							for (unsigned e2 = pcnt; e2 < 20; e2++)
							{
								if ((int) e2 == grabIdx) continue;
								UINT32 er = 0x1A0000 + e2 * 0x4000;
								if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
								float ox = *(const float *) &ram[er + 0x380] - px, oz = *(const float *) &ram[er + 0x388] - pz;
								if (ox * ox + oz * oz < bd2) { bd2 = ox * ox + oz * oz; vx = ox; vz = oz; }
							}
							if (bd2 < 400.0f)   // another enemy within 20: throw that way
							{
								float fx = g_stFx, fz = g_stFz, fwd = vx * fx + vz * fz, rgt = vx * (-fz) + vz * fx;
								float th = 0.38f * sqrtf(vx * vx + vz * vz);
								if (fwd > th) gkDir |= UP; else if (fwd < -th) gkDir |= DOWN;
								if (rgt > th) gkDir |= RIGHT; else if (rgt < -th) gkDir |= LEFT;
							}
							if (!gkDir) gkDir = UP;
						}
						bits |= gkDir;
						if (((ge - 28) % 12) < 4) bits |= BEAT;
					}
				}
				// 11/17 (user, test): Linda's recorded hold and the knees + throw stay off; the B-B-C / Jump / C hold above is on.
				if (true && gkOn && (gkPh == 4 || gkPh == 6)) bits &= ~(DIRS | 0x0F);
				// 11/17 (user; log 15:14): the grab-in (own action group 08 - 0800045A front, 02C8 / 02C6 side, 020D back,
				// 0977 ally - about 15 frames before the hold flag comes up): nothing pressed and the combo dropped. A B of
				// the b b b b b b still running there started the knees one early, and the C of B-B-C landed on the 3rd knee
				// (07B6, which sends the enemy flying) instead of going round. The boss escape (101) also pushed the lever
				// and Jump there. Not while the grab handling (gkOn) or a grab escape / ally throw (grabEsc) is running.
				if (!gkOn && !grabEsc && !grabSpecial && (*(const UINT32 *) &ram[rob + 0x38] >> 24) == 0x08)   // 11/21: not over a special
				{
					bits &= ~(DIRS | 0x0F);
					g_aiLogA75 = 81;   // grab-in: hands off
				}
			}
			// 11/18 (user): an HP item taken (going for one within the last 30 frames) and our HP went up: stop searching -
			// lever right, down, left, up (4 frames each) with B on the up, then 60 frames with nothing pressed. a75 = 133
			// (the lever round) / 134 (standing).
			{
				// 11/18 (user, 2nd): also after letting go of an ally we held: lever left, down, right, up with B on the up, then
				// 60 frames still (a75 = 135 / 136).
				// 11/18 (user, 5th): the HP-item turn is gone. Let go of an ally WE held: left, down, right, up + B; an ally
				// that held US let go: right, down, left, up + B (heldAlly 1 / 2).
				static int hpPrevT = -1; static UINT32 hpSeekF = 0, tauntF = 0; static bool tauntOn = false, tauntAlly = false; static int heldAlly = 0, tauntKind = 0;
				static int heldAllySlot = -1, tauntAllySlot = -1;   // 11/22: the ally of the hold (the lever away from it after)
				const int hpN = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2];
				(void) hpSeekF;
				if (heldAlly && !grabbing && !grabbed && hpN > 0) { tauntOn = true; tauntAlly = true; tauntKind = heldAlly; tauntF = g_ppcTraceFrame; tauntAllySlot = heldAllySlot; }
				heldAlly = (grabbing && grabAlly) ? 1 : (grabbed && grabAlly) ? 2 : 0;
				if (heldAlly && grabIdx >= 0 && grabIdx < 8) heldAllySlot = grabIdx;
				hpPrevT = hpN;
				if (tauntOn)
				{
					// 11/18 (user, 2nd): the move did not come out - the lever went round too fast (4 frames a way). Now 2 frames
					// neutral, 6 frames each way, B for 4 frames from the 3rd frame of the up; then 10 frames still (was 60).
					// After letting go of an ally: 10 frames' wait first.
					// 11/18 (3rd): a 10-frame wait first for both (the HP item too).
					// 11/18 (4th, user): an enemy within 2.5 (live, same level) during the wait -> no turn, fight (it ends); after
					// the turn: one within 2.5 -> fight at once, none -> 6 frames still (was 10).
					// 11/22 (user, 2nd): after the round + B (the "thank you" emote) and its 6 frames still, the lever away from the
					// ally of the hold for 60 frames - no Shift (in the emote the lever only turns us). An enemy within 2.5 cuts only
					// the wait.
					const unsigned TW = 10u, TN = 2u, TD = 6u, TR = TN + 4u * TD, TSTILL = 6u, TS = TSTILL + 60u;
					const unsigned e = g_ppcTraceFrame - tauntF;
					bool near25 = false;
					{
						const float myYt = *(const float *) &ram[rob + 0x384];
						for (unsigned e5 = estart; e5 < 20 && !near25; e5++)
						{
							UINT32 er = 0x1A0000 + e5 * 0x4000;
							if (!(*(const UINT32 *) &ram[er] & 1) || (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2] <= 0) continue;
							float ox = *(const float *) &ram[er + 0x380] - px, oz = *(const float *) &ram[er + 0x388] - pz;
							if (ox * ox + oz * oz < 6.25f && fabsf(*(const float *) &ram[er + 0x384] - myYt) < 1.5f) near25 = true;
						}
					}
					if (e >= TW + TR + TS || hpN <= 0 || grabbed || grabbing || (near25 && e < TW)) tauntOn = false;
					else
					{
						static const UINT8 R4[4] = { 0x40 /*RIGHT*/, 0x10 /*DOWN*/, 0x80 /*LEFT*/, 0x20 /*UP*/ };
						static const UINT8 L4[4] = { 0x80 /*LEFT*/, 0x10 /*DOWN*/, 0x40 /*RIGHT*/, 0x20 /*UP*/ };
						bits &= ~(DIRS | 0x0F);
						const bool turning = e >= TW + TN && e < TW + TR;
						if (turning)
						{
							const unsigned r = e - TW - TN;
							bits |= (tauntKind == 1 ? L4 : R4)[r / TD];
							if (r >= 3u * TD + 2u && r < 3u * TD + 6u) bits |= BEAT;
						}
						else if (e >= TW + TR + TSTILL && tauntAllySlot >= 0)   // 11/22: 6 frames still, then the lever away from the ally, 60 frames
						{
							const UINT32 ar = 0x1A0000 + (unsigned) tauntAllySlot * 0x4000;
							const float ax = px - *(const float *) &ram[ar + 0x380], az = pz - *(const float *) &ram[ar + 0x388];
							const float fw = ax * g_stFx + az * g_stFz, rg = ax * (-g_stFz) + az * g_stFx, th = 0.38f * sqrtf(ax * ax + az * az);
							UINT8 aw = 0;
							if (fw > th) aw |= UP; else if (fw < -th) aw |= DOWN;
							if (rg > th) aw |= RIGHT; else if (rg < -th) aw |= LEFT;
							bits |= aw ? aw : DOWN;
						}
						g_aiLogA75 = tauntKind == 1 ? (e < TW + TR ? 135 : 136) : (e < TW + TR ? 133 : 134);   // 133/134 now: let go BY an ally
					}
				}
			}
		}
	}
	if (g_spikeoutAILog && g_stValid)
	{
		static FILE *sf = NULL; static long sfBytes = 0; static float lpx = 0, lpz = 0; static UINT32 lastF = ~0u;
		if (!sf) sf = fopen("steer.txt", "w");
		float cx = *(const float *) &ram[rob + 0x380], cz = *(const float *) &ram[rob + 0x388];
		if (sf && sfBytes < 80L * 1024 * 1024 && g_ppcTraceFrame != lastF)   // called several times a frame: once
		{
			// cam=(camera forward x,z) want=(world dir x,z) tgt=(steer point) bits=lever/buttons mv=(move this frame)
			int n = fprintf(sf, "f=%u p=(%.2f,%.2f) y=%.2f cam=(%.3f,%.3f) want=(%.2f,%.2f) tgt=(%.2f,%.2f) bits=%02X mv=(%.3f,%.3f) mode=%d fs=%d st=%d gate=%d dash=%d a75=%d wn=%d key=%04X arflg=%03X door=%d dflg=%08X dside=%.2f wflg=%08X wlen=%.2f cy=%.2f m=%.3f pit=%.1f onme=%d mya=%08X fe=%.0f chg=%u cdec=%u ck=%d\n",
			                g_ppcTraceFrame, cx, cz, g_aiLogPy, g_stFx, g_stFz, g_stDx, g_stDz, g_stTx, g_stTz, bits, cx - lpx, cz - lpz,
			                g_stMode, g_stFstate, g_stStucks, g_stGate, g_stDash, g_aiLogA75, g_aiLogWn, g_aiLogNavKey, g_aiLogArflg, g_stDoorId, g_stDoorFlg, g_stDoorD, g_awLastFlg, g_awLastLen, g_stCamY, g_stCamM, g_stCamPitch, g_aiTgtOnMe ? 1 : 0,
			                g_stMyAct, g_stFaceErr,
			                // 11/17 charge (ROM debug table 0x98DC0 / 0x24B24): chg = frames Charge held (Rob +0x224E, 0 when not),
			                // cdec = how long it was held at the last release (+0x226A), ck = mot_kind2 charge (+0x48 bit 0x20000000).
			                // Levels per character: +0x1E74 1st 15 (Linda 20), +0x1E72 2nd 45 (50), +0x1E70 peak 80 (90), +0x1E84 cancel 170 (180).
			                (unsigned) *(const UINT16 *) &ram[(rob + 0x224E) ^ 2], (unsigned) *(const UINT16 *) &ram[(rob + 0x226A) ^ 2],
			                (*(const UINT32 *) &ram[rob + 0x48] & 0x20000000u) ? 1 : 0);
			g_stDoorId = 0; g_stDoorFlg = 0; g_stDoorD = -1.0f;
			if (n > 0) sfBytes += n;
			if ((g_ppcTraceFrame & 63) == 0) fflush(sf);
		}
		if (g_ppcTraceFrame != lastF) { lpx = cx; lpz = cz; lastF = g_ppcTraceFrame; }
		g_stValid = false;
	}
	bool wantsStart = SpikeoutAIWantsStart(ram);
	// 11/14 (user): starting - no lever (on a continue's select: LEFT 0-3 times at random, below). On the
	// character select of a NEW session (started from the attract; own link state 0x140104 = 2), AIChar picks the
	// character: L L U U always lands on Spike (top left); Spike -> right Linda, down Tenshin; Linda -> down White.
	// Taps 4 on / 4 off from 30 frames into the screen, then Beat; Beat again once a second if it did not take.
	bool selDone = true;
	// 11/14 fix: run on the select screen itself (own link state 2) too - with all 8 cabinets bots nobody "wants to
	// start" (no linked human cabinet has started), the session is started by hand, and the lever never moved.
	const bool onSelect = ram[0x140104 ^ 3] == 2;
	{
		static bool newSession = false; static UINT32 selT0 = 0;
		if (SpikeoutAttract(ram) || ram[0x140104 ^ 3] == 1) newSession = true;   // seen the title / attract: the next select is a new session's
		if (wantsStart || onSelect)
		{
			bits &= (UINT8) ~0xF0;
			if (ram[0x140104 ^ 3] == 2 && newSession && g_spikeoutAIChar >= 0)
			{
				if (!selT0) selT0 = g_ppcTraceFrame ? g_ppcTraceFrame : 1;
				UINT8 seq[6]; int n = 0;
				seq[n++] = LEFT; seq[n++] = LEFT; seq[n++] = UP; seq[n++] = UP;   // -> Spike
				if (g_spikeoutAIChar == 2) seq[n++] = RIGHT;                      // Linda
				else if (g_spikeoutAIChar == 3) seq[n++] = DOWN;                  // Tenshin
				else if (g_spikeoutAIChar == 1) { seq[n++] = RIGHT; seq[n++] = DOWN; }   // White (Linda, then down)
				unsigned t = g_ppcTraceFrame - selT0;
				if (t < 30u + 8u * (unsigned) n)
				{
					selDone = false;
					if (t >= 30 && ((t - 30) % 8) < 4) bits |= seq[(t - 30) / 8];
				}
				else
				{
					unsigned c = t - (30u + 8u * (unsigned) n);         // first Beat 10 frames after the last tap's slot
					selDone = false;
					if (c >= 10 && ((c - 10) % 60) < 4) bits |= BEAT;
				}
			}
			else if (ram[0x140104 ^ 3] == 2 && !newSession)
			{
				// 11/14 (user): a continue's select: LEFT 0-3 times at random (picked once per screen, per cabinet - the
				// pick itself reaches the others through the game like a player's), 4 on / 4 off from 30 frames into
				// the screen, then Beat 10 frames after the last tap's slot, again once a second if it did not take.
				static unsigned nLeft = 0;
				if (!selT0)
				{
					selT0 = g_ppcTraceFrame ? g_ppcTraceFrame : 1;
					// rand() is never seeded here (the same on all 8 processes): mix the clock, node and frame instead
					uint32_t x = (uint32_t) time(NULL) ^ ((uint32_t) ram[0x10065B ^ 3] * 2654435761u) ^ (g_ppcTraceFrame * 40503u);
					x ^= x << 13; x ^= x >> 17; x ^= x << 5; x ^= x >> 11;
					nLeft = x % 4;
					if (g_spikeoutAIChar >= 0 || !g_spikeoutAIRandSel) nLeft = 0;   // 11/17: AIChar set / AIrandSel=0: same character
				}
				unsigned t = g_ppcTraceFrame - selT0;
				selDone = false;
				if (t < 30u + 8u * nLeft) { if (t >= 30 && ((t - 30) % 8) < 4) bits |= LEFT; }
				else
				{
					unsigned c = t - (30u + 8u * nLeft);
					if (c >= 10 && ((c - 10) % 60) < 4) bits |= BEAT;
				}
			}
			else selT0 = 0;
		}
		else
		{
			selT0 = 0;
			if (SpikeoutInStage(ram)) newSession = false;
		}
	}
	// 11/15 (user: still fixed before the lever): on a continue the own link state stays 4 (charsel.txt) - the select
	// screen can't be told from the state. So a continue runs on time, one attempt per 300 frames: coin at 0-3 and
	// Start at 20-23 (input code, g_aiContT0), LEFT 0-3 times at random from 80 (4 on / 4 off), Beat 10 frames after
	// the last tap's slot - and no once-a-second Beat meanwhile (it confirmed the character ~20 frames after Start).
	{
		static UINT32 contT0 = 0; static unsigned contN = 0;
		const bool contMode = wantsStart && !SpikeoutAttract(ram) && !onSelect;
		if (contMode)
		{
			if (!contT0 || g_ppcTraceFrame - contT0 >= 300)
			{
				contT0 = g_ppcTraceFrame ? g_ppcTraceFrame : 1;
				uint32_t x = (uint32_t) time(NULL) ^ ((uint32_t) ram[0x10065B ^ 3] * 2654435761u) ^ (g_ppcTraceFrame * 40503u);
				x ^= x << 13; x ^= x >> 17; x ^= x << 5; x ^= x >> 11;
				contN = x % 4;
				if (g_spikeoutAIChar >= 0 || !g_spikeoutAIRandSel) contN = 0;    // 11/15 (user): AIChar set - 11/17: or AIrandSel=0 - no random pick on a continue
			}
			const unsigned t = g_ppcTraceFrame - contT0, tb = 80u + 8u * contN + 10u;
			bits &= (UINT8) ~0xF0;
			if (t >= 80 && t < 80u + 8u * contN && ((t - 80) % 8) < 4) bits |= LEFT;
			if (t >= tb && t < tb + 4) bits |= BEAT;
			selDone = false;
		}
		else contT0 = 0;
		g_aiContT0 = contT0;
	}
	if ((wantsStart || onSelect) && selDone && (g_ppcTraceFrame % 60) >= 40 && (g_ppcTraceFrame % 60) < 44) bits |= BEAT;   // confirm character select
	// 11/15: charsel.txt (AIPlayerLog=1) - the own link state, wantsStart and the lever / Beat bits on every change
	// while starting / continuing / selecting: to see what the continue's select screen really looks like.
	if (g_spikeoutAILog)
	{
		static FILE *cf = NULL; static unsigned lastKey = ~0u;
		const unsigned ls = ram[0x140104 ^ 3];
		const unsigned key = ls | (wantsStart ? 0x100u : 0u) | (onSelect ? 0x200u : 0u) | ((unsigned) (bits & (0xF0 | BEAT)) << 16);
		if ((wantsStart || onSelect || ls != (lastKey & 0xFF)) && key != lastKey)
		{
			if (!cf) cf = fopen("charsel.txt", "w");
			if (cf) { fprintf(cf, "f=%u ls=%u want=%d sel=%d gmode=%u hp=%d bits=%02X\n", g_ppcTraceFrame, ls, wantsStart ? 1 : 0, onSelect ? 1 : 0,
			                   (unsigned) ram[0x10402C ^ 3], (int) (short) *(const UINT16 *) &ram[(0x1A0000 + (ram[0x10065B ^ 3] - 1u) * 0x4000 + 0x3F02) ^ 2], bits); fflush(cf); }
			lastKey = key;
		}
	}
	if (g_spikeoutWallView)
	{
		// 11/17 (user): walls round the bot (within 15), can't pass = thick, can be got over = thin (Main.cpp).
		std::vector<float> wb;
		float cam6[6] = { 0 };
		UINT32 cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
		UINT32 wt = WnWallList(ram);
		if (cam >= 0x100000 && cam < 0x7FF000 && wt)
		{
			for (int k = 0; k < 3; k++) { cam6[k] = *(const float *) &ram[cam + 0x10 + 4 * k]; cam6[3 + k] = *(const float *) &ram[cam + 0x404 + 4 * k]; }
			const float mx = *(const float *) &ram[rob + 0x380], my = *(const float *) &ram[rob + 0x384], mz = *(const float *) &ram[rob + 0x388];
			const float feet = my - 1.15f;
			for (unsigned wid = 0; wid < 4096 && wt + (wid + 1) * 4 <= 0x800000; wid++)
			{
				UINT32 wp = *(const UINT32 *) &ram[wt + wid * 4];
				if (wp == 0xFFFFFFFFu) break;
				wp &= 0x7FFFFF;
				if (wp < 0x10000 || wp + 0x2C > 0x800000) break;
				const UINT32 wflg = *(const UINT32 *) &ram[wp];
				if (wflg & 0x80000000u) continue;                             // disabled
				float x0 = *(const float *) &ram[wp + 0x0C], y0 = *(const float *) &ram[wp + 0x10], z0 = *(const float *) &ram[wp + 0x14];
				float x1 = *(const float *) &ram[wp + 0x18], y1 = *(const float *) &ram[wp + 0x1C], z1 = *(const float *) &ram[wp + 0x20];
				float h0 = *(const float *) &ram[wp + 0x24], h1 = *(const float *) &ram[wp + 0x28];
				if (!(x0 == x0) || !(z0 == z0) || !(x1 == x1) || !(z1 == z1) || !(y0 == y0) || !(y1 == y1) || !(h0 == h0) || !(h1 == h1)) continue;
				if (fabsf(x0) > 2000.0f || fabsf(z0) > 2000.0f || fabsf(x1) > 2000.0f || fabsf(z1) > 2000.0f) continue;
				// near us: the closest point of the segment within 15
				float sx = x1 - x0, sz = z1 - z0, sl2 = sx * sx + sz * sz, t = 0.0f;
				if (sl2 > 1e-6f) { t = ((mx - x0) * sx + (mz - z0) * sz) / sl2; if (t < 0) t = 0; if (t > 1) t = 1; }
				float qx = x0 + sx * t - mx, qz = z0 + sz * t - mz;
				if (qx * qx + qz * qz > 225.0f) continue;
				const float top = (y0 + h0 > y1 + h1) ? y0 + h0 : y1 + h1, bot = y0 < y1 ? y0 : y1;
				if (top < feet - 1.0f || bot > feet + 3.0f) continue;          // another level
				const bool pass = (wflg & (0x01000000u | 0x00800000u)) != 0 || top - feet <= 2.3f;
				wb.insert(wb.end(), { x0, y0, z0, x1, y1, z1, y0 + h0, y1 + h1, pass ? 1.0f : 0.0f });
			}
		}
		std::lock_guard<std::mutex> lk(g_spkWallMx);
		g_spkWallBuf.swap(wb);
		for (int k = 0; k < 6; k++) g_spkWallCam[k] = cam6[k];
		g_spkWallF = g_ppcTraceFrame;
	}
	if (g_spikeoutAITargetMarker)
	{
		SpikeoutMarker mk = g_spkMarker;
		mk.f = g_ppcTraceFrame; mk.on = 0;
		if (found && tSlot >= (int) estart && tSlot < 20)
		{
			UINT32 tr = 0x1A0000 + (unsigned) tSlot * 0x4000;
			UINT32 cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
			UINT32 fp = *(const UINT32 *) &ram[0x104114] & 0x7FFFFF;
			unsigned mySlot = (rob - 0x1A0000) / 0x4000;
			for (int k = 0; k < 3; k++)
			{
				mk.cam[k] = *(const float *) &ram[cam + 0x10 + 4 * k];
				mk.itr[k] = *(const float *) &ram[cam + 0x404 + 4 * k];
				mk.me[k] = *(const float *) &ram[rob + 0x380 + 4 * k];
				mk.tg[k] = *(const float *) &ram[tr + 0x380 + 4 * k];
			}
			mk.me[1] += 0.3f; mk.tg[1] += 0.3f;   // 11/15: 1.2 -> 0.9 -> 0.3 (user: lower, much lower)
			mk.colour = (fp >= 0x100000 && fp < 0x7FF000 && mySlot < 8) ? (ram[(fp + 0xA0 + mySlot) ^ 3] & 7u) : mySlot & 7u;
			mk.on = (cam >= 0x100000 && cam < 0x7FF000) ? 1 : 0;
		}
		g_spkMarker = mk;
	}
	if (g_spikeoutAILog && (fr % (g_spikeoutAICalib ? 6u : 30u)) == 0)
	{
		static FILE *af = NULL; if (!af) af = fopen("ai.txt", "w");
		float npd = -1.0f; int nps = -1;                       // nearest other player (pd=, ps= in ai.txt)
		{
			float mx = *(const float *) &ram[rob + 0x380], mz = *(const float *) &ram[rob + 0x388];
			for (unsigned q = 0; q < 8; q++)
			{
				if (q == node - 1) continue;
				UINT32 qr = 0x1A0000 + q * 0x4000;
				if (!(*(const UINT32 *) &ram[qr] & 1)) continue;
				float dx = *(const float *) &ram[qr + 0x380] - mx, dz = *(const float *) &ram[qr + 0x388] - mz, d = sqrtf(dx * dx + dz * dz);
				if (npd < 0 || d < npd) { npd = d; nps = (int) q; }
			}
		}
		if (af) { fprintf(af, "f=%u mode=%d ph=%u node=%u cnt=%u px=%.2f pz=%.2f nact=%d near=(%.2f,%.2f) d=%.2f bits=%02X start=%d mask=%08X gmode=%u hp=%d key=%04X tgt=(%.2f,%.2f) enemy=%d ehp=%d arflg=%03X dy=%.2f a75=%d wn=%d mg=%d(%.1f,%.1f) act=%08X mot=%d enf=%08X step=%u cls=%u name=%d csc=%06X waza=%u ingame=%08X own=%08X tok1=%08X py=%.2f myact=%08X mymot=%d mykind=%u pd=%.2f ps=%d\n",
		          g_ppcTraceFrame, mode, phase, node, pcnt, px, pz, nact, ex, ez, found ? bestd : -1.0f, bits, wantsStart ? 1 : 0,
		          *(const UINT32 *) &ram[0x14002C], (unsigned) ram[0x10402C ^ 3], (int) (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2],
		          g_aiLogNavKey, g_aiLogTx, g_aiLogTz, g_aiLogTarget, g_aiLogEhp, g_aiLogArflg, g_aiLogDy, g_aiLogA75, g_aiLogWn, g_aiLogMemGate, g_aiLogMgX, g_aiLogMgZ,
		          tSlot >= 0 ? *(const UINT32 *) &ram[0x1A0000 + (unsigned) tSlot * 0x4000 + 0x38] : 0u,                     // target Rob: action
		          tSlot >= 0 ? (int) (short) *(const UINT16 *) &ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x3C) ^ 2] : -1,    // mot_num
		          tSlot >= 0 ? *(const UINT32 *) &ram[0x1A0000 + (unsigned) tSlot * 0x4000 + 0x4C] : 0u,                     // en_flag
		          tSlot >= 0 ? (unsigned) ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x6C) ^ 3] : 0u,                       // step
		          tSlot >= 0 ? (unsigned) ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x73) ^ 3] : 0u,                       // class
		          tSlot >= 0 ? (int) (short) *(const UINT16 *) &ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x15C) ^ 2] : -1,   // name (enemy kind)
		          tSlot >= 0 ? *(const UINT32 *) &ram[0x1A0000 + (unsigned) tSlot * 0x4000 + 0x2280] & 0xFFFFFF : 0u,        // CPU AI script
		          tSlot >= 0 ? (unsigned) *(const UINT16 *) &ram[(0x1A0000 + (unsigned) tSlot * 0x4000 + 0x227A) ^ 2] : 0u,  // AI technique
		          *(const UINT32 *) &ram[0x140128],
		          *(const UINT32 *) &ram[(*(const UINT32 *) &ram[0x140004] & 0x7FFFFF) + 0x14],     // link struct group id: own, node 1
		          *(const UINT32 *) &ram[(*(const UINT32 *) &ram[0x14000C] & 0x7FFFFF) + 0x14],
		          g_aiLogPy, *(const UINT32 *) &ram[rob + 0x38], (int) *(const UINT32 *) &ram[rob + 0x3C], (unsigned) *(const UINT32 *) &ram[rob + 0x44],
		          npd, nps);
			// 11/15 (user): who each live enemy is after and its flags. et= the slot of the Rob at its +0x32E4 (written by
			// 0x53F18; -1 = not a player Rob), etp= +0x325F (player number byte), ey= Rob y, ef= +0x00 flags, ek= +0x44
			// mot_kind, ek2= +0x48 mot_kind2, inv= +0x3F22 invincible timer. First the target (tSlot), then every live
			// enemy as E[slot](x,z,y,et,ef,ek,hp).
			auto etSlot = [&](UINT32 er) -> int {
				UINT32 t = *(const UINT32 *) &ram[er + 0x32E4] & 0x7FFFFF;
				return (t >= 0x1A0000 && t < 0x1A0000 + 20 * 0x4000 && (t & 0x3FFF) == 0) ? (int) ((t - 0x1A0000) / 0x4000) : -1;
			};
			if (tSlot >= 0 && tSlot < 20)
			{
				UINT32 er = 0x1A0000 + (unsigned) tSlot * 0x4000;
				fprintf(af, "    T et=%d etp=%u ey=%.2f ef=%08X ek=%08X ek2=%08X inv=%u myslot=%u\n", etSlot(er), (unsigned) ram[(er + 0x325F) ^ 3],
				        *(const float *) &ram[er + 0x384], *(const UINT32 *) &ram[er], *(const UINT32 *) &ram[er + 0x44], *(const UINT32 *) &ram[er + 0x48],
				        (unsigned) *(const UINT16 *) &ram[(er + 0x3F22) ^ 2], node - 1);
			}
			fprintf(af, "    E");
			for (unsigned e2 = estart; e2 < 20; e2++)
			{
				UINT32 er = 0x1A0000 + e2 * 0x4000;
				int hp = (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2];
				if (!(*(const UINT32 *) &ram[er] & 1) || hp <= 0) continue;
				fprintf(af, " E%u(%.1f,%.1f,%.2f,%d,%08X,%08X,%d)", e2, *(const float *) &ram[er + 0x380], *(const float *) &ram[er + 0x388],
				        *(const float *) &ram[er + 0x384], etSlot(er), *(const UINT32 *) &ram[er], *(const UINT32 *) &ram[er + 0x44], hp);
			}
			fprintf(af, "\n");
			fflush(af); }
	}
	// 11/17 (user): enemyact.txt (AIPlayerLog=1) - what each live enemy's own AI decided, to check it against what it does.
	// Read from ROM 11/17: the enemy think rules (0x70C00..0x72300) compare Rob +0x64 (distance to its target, sqrt of
	// the smallest +0x3288[obj] squared distance, written by 0x53FC8; +0x16C = the previous one) and start an action
	// (0x65370): the action number at +0x22C2, its input steps copied from the ROM table 0x8C08C to +0x227C.. (pad bits as
	// a player's: B 01 C 02 S 04 J 08 down 10 up/forward 20 right 40 left 80; FF ends) and +0x227A = the step now (FF done).
	// One line per change of an enemy's action / step / attack bit (mot_kind 0x00800000), at most once per frame.
	if (g_spikeoutAILog)
	{
		static FILE *eaf = NULL; static UINT32 eaLastF = 0xFFFFFFFFu;
		static UINT16 eaAct[20]; static UINT8 eaStep[20], eaAtk[20], eaLive[20];
		if (g_ppcTraceFrame != eaLastF)
		{
			eaLastF = g_ppcTraceFrame;
			if (!eaf) eaf = fopen("enemyact.txt", "w");
			for (unsigned e2 = estart; e2 < 20 && eaf; e2++)
			{
				UINT32 er = 0x1A0000 + e2 * 0x4000;
				int hp = (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2];
				if (!(*(const UINT32 *) &ram[er] & 1) || hp <= 0) { eaLive[e2] = 0; continue; }
				UINT16 act = *(const UINT16 *) &ram[(er + 0x22C2) ^ 2];
				UINT8 step = ram[(er + 0x227A) ^ 3];
				UINT32 ek = *(const UINT32 *) &ram[er + 0x44];
				UINT8 atk = (ek & 0x00800000u) ? 1 : 0;
				if (eaLive[e2] && act == eaAct[e2] && step == eaStep[e2] && atk == eaAtk[e2]) continue;
				eaLive[e2] = 1; eaAct[e2] = act; eaStep[e2] = step; eaAtk[e2] = atk;
				UINT32 t = *(const UINT32 *) &ram[er + 0x32E4] & 0x7FFFFF;
				int et = (t >= 0x1A0000 && t < 0x1A0000 + 20 * 0x4000 && (t & 0x3FFF) == 0) ? (int) ((t - 0x1A0000) / 0x4000) : -1;
				fprintf(eaf, "f=%u E%u act=%02X step=%02X seq=", g_ppcTraceFrame, e2, (unsigned) act, (unsigned) step);
				for (unsigned k = 0; k < 16; k++)
				{
					UINT8 v = ram[(er + 0x227C + k) ^ 3];
					if (v == 0xFF) break;
					fprintf(eaf, "%s%02X", k ? "," : "", (unsigned) v);
				}
				fprintf(eaf, " d=%.2f dprev=%.2f et=%d atk=%u mot=%d ek=%08X cls=%u hp=%d me=%u\n",
				        *(const float *) &ram[er + 0x64], *(const float *) &ram[er + 0x16C], et, (unsigned) atk,
				        (int) (short) *(const UINT16 *) &ram[(er + 0x3C) ^ 2], ek, (unsigned) ram[(er + 0x73) ^ 3], hp, node - 1);
			}
			if (eaf) fflush(eaf);
		}
	}
	return bits;
}

// InputLog=1 (11/14, human cabinets only - AIPlayer=0): this cabinet's lever and S/B/C/J every frame, with the own
// character id and motion, to measure combo timings. Kept in memory and appended to inputlog.txt every 60 frames (the
// rest at exit). One line per emulated frame (the first read of the port in that frame).
static bool g_spikeoutInputLog = false;
static std::string g_ilBuf; static unsigned g_ilN = 0; static UINT32 g_ilLastF = ~0u;
// 11/23 AIProfile=1: is the bot weak because the AI costs too much? Per 600 frames, aiprof.txt gets the frame rate
// (wall clock), the AI calls per frame, the AI's time per call (average / max, in µs) and the path searches.
struct SpikeoutAIProf
{
	unsigned frames = 0, calls = 0, callsThisF = 0, maxCallsF = 0, multiF = 0, over1 = 0, over4 = 0;
	unsigned kills = 0, dmgTaken = 0, specials = 0;   // strength: enemies gone, own HP lost, own SP spent (this period)
	double sumUs = 0, maxUs = 0, maxFrameMs = 0;
	bool started = false;
	std::chrono::steady_clock::time_point t0, lastF;
};
static SpikeoutAIProf g_aip;
static void SpikeoutAIProfCall(double us)
{
	g_aip.calls++; g_aip.callsThisF++; g_aip.sumUs += us;
	if (us > g_aip.maxUs) g_aip.maxUs = us;
	if (us > 1000.0) g_aip.over1++;
	if (us > 4000.0) g_aip.over4++;
}
static void SpikeoutAIProfFrame(const UINT8 *ram)   // once per emulated frame
{
	auto now = std::chrono::steady_clock::now();
	if (!g_aip.started) { g_aip = SpikeoutAIProf(); g_aip.started = true; g_aip.t0 = g_aip.lastF = now; g_aipFnSearch = g_aipWnSearch = g_aipFnPops = 0; return; }
	double fms = std::chrono::duration<double, std::milli>(now - g_aip.lastF).count();
	g_aip.lastF = now;
	if (fms > g_aip.maxFrameMs) g_aip.maxFrameMs = fms;
	if (g_aip.callsThisF > g_aip.maxCallsF) g_aip.maxCallsF = g_aip.callsThisF;
	if (g_aip.callsThisF > 1) g_aip.multiF++;
	g_aip.callsThisF = 0;
	{   // strength counters (own slot = node - 1; enemies = slots from the player count on)
		static UINT32 prevAlive = 0; static int prevHp = -1, prevSp = -1;
		unsigned node = ram[0x10065B ^ 3], pc = ram[0x10065A ^ 3]; if (pc < 1 || pc > 8) pc = 4;
		UINT32 alive = 0;
		for (unsigned e = pc; e < 20; e++) if (*(const UINT32 *) &ram[0x1A0000 + e * 0x4000] & 1u) alive |= 1u << e;
		g_aip.kills += (unsigned) __builtin_popcount(prevAlive & ~alive);
		prevAlive = alive;
		if (node >= 1 && node <= 8 && ram[0x10402D ^ 3])
		{
			UINT32 rob = 0x1A0000 + (node - 1) * 0x4000;
			int hp = (short) *(const UINT16 *) &ram[(rob + 0x3F02) ^ 2], sp = ram[(rob + 0x3F25) ^ 3];
			if (prevHp >= 0 && hp < prevHp) g_aip.dmgTaken += (unsigned) (prevHp - hp);
			if (prevSp >= 0 && sp < prevSp) g_aip.specials++;
			prevHp = hp; prevSp = sp;
		}
		else prevHp = prevSp = -1;
	}
	if (++g_aip.frames < 600) return;
	double sec = std::chrono::duration<double>(now - g_aip.t0).count();
	if (FILE *f = fopen("aiprof.txt", "a"))
	{
		fprintf(f, "f=%u node=%u area=%u fps=%.1f frameMax=%.1fms calls=%u callsPerF(max)=%u multiF=%u ai_avg=%.1fus ai_max=%.1fus >1ms=%u >4ms=%u fnSearch=%u fnPops=%u wnSearch=%u kills=%u dmg=%u sp=%u\n",
			g_ppcTraceFrame, ram[0x10065B ^ 3], ram[0x10402D ^ 3], sec > 0 ? g_aip.frames / sec : 0.0, g_aip.maxFrameMs,
			g_aip.calls, g_aip.maxCallsF, g_aip.multiF, g_aip.calls ? g_aip.sumUs / g_aip.calls : 0.0, g_aip.maxUs,
			g_aip.over1, g_aip.over4, g_aipFnSearch, g_aipFnPops, g_aipWnSearch, g_aip.kills, g_aip.dmgTaken, g_aip.specials);
		fclose(f);
	}
	g_aip = SpikeoutAIProf(); g_aip.started = true; g_aip.t0 = g_aip.lastF = now;
	g_aipFnSearch = g_aipWnSearch = g_aipFnPops = 0;
}

static void SpikeoutInputLogFlush()
{
	if (g_ilBuf.empty()) return;
	if (FILE *f = fopen("inputlog.txt", "a")) { fwrite(g_ilBuf.data(), 1, g_ilBuf.size(), f); fclose(f); }
	g_ilBuf.clear(); g_ilN = 0;
}
static void SpikeoutInputLogFrame(const UINT8 *ram, UINT8 pressed)   // pressed: active-high (UP 0x20 DOWN 0x10 LEFT 0x80 RIGHT 0x40 B 1 C 2 S 4 J 8)
{
	if (g_ppcTraceFrame == g_ilLastF) return;
	g_ilLastF = g_ppcTraceFrame;
	unsigned node = ram[0x10065B ^ 3]; if (node < 1 || node > 8) node = 1;
	UINT32 rob = 0x1A0000 + (node - 1) * 0x4000;
	// character id: Rob +0x3F00 (byte; = the node packet's chr_id +0x25 on all 8 in a linked dump, 11/14). The packet
	// table *(0x150060 + 4 x node) is empty when playing alone.
	int chr = (int) ram[(rob + 0x3F00) ^ 3];
	char lv[5]; int k = 0;
	if (pressed & 0x20) lv[k++] = 'U';
	if (pressed & 0x10) lv[k++] = 'D';
	if (pressed & 0x80) lv[k++] = 'L';
	if (pressed & 0x40) lv[k++] = 'R';
	if (!k) lv[k++] = '-';
	lv[k] = 0;
	char line[200];
	snprintf(line, sizeof(line), "f=%u lv=%-2s S=%d B=%d C=%d J=%d chr=%d mot=%d kind=%08X area=%u\n", g_ppcTraceFrame, lv,
	         (pressed >> 2) & 1, pressed & 1, (pressed >> 1) & 1, (pressed >> 3) & 1, chr,
	         (int) (short) *(const UINT16 *) &ram[(rob + 0x3C) ^ 2], *(const UINT32 *) &ram[rob + 0x44], (unsigned) ram[0x10402D ^ 3]);
	g_ilBuf += line;
	if (++g_ilN >= 60) SpikeoutInputLogFlush();
}
static unsigned char g_taskShadow[0x40];     // 4 arrays x 4 overflow entries (idx 4-7) x 4 bytes
// 11/14: every patch applied at boot (all tables, in order). The patches go into the CROM image, which the game copies
// to RAM at the same address; loading a save state puts back the RAM of whatever exe saved it, so after a load they are
// put into RAM again (only where RAM still holds the original word: a patch missing from an older exe's state).
static std::vector<Spikeout8LinkPatch> g_spk8Applied;
static unsigned char g_taskPcBase[0x20000];  // PC>>2 -> array base (0x10/0x20/0x30/0x40), 0 = not a task accessor
static const struct { UINT32 pc; UINT8 base; } s_spikeofeTaskPc[] = {
  {0x1E29C,0x40},{0x22C90,0x30},{0x22CB0,0x30},{0x27278,0x10},{0x272AC,0x10},{0x28A9C,0x10},{0x28AC4,0x10},
  {0x28B54,0x10},{0x3EF30,0x10},{0x4A844,0x40},{0x4A888,0x40},{0x4A9AC,0x40},{0x4A9B0,0x40},{0x4A9B4,0x30},
  {0x4AB10,0x10},{0x4AB40,0x10},{0x513A8,0x10},{0x513AC,0x30},{0x513B0,0x40},{0x51424,0x20},{0x51428,0x30},
  {0x51430,0x40},{0x51454,0x40},{0x5145C,0x10},{0x51468,0x20},{0x5146C,0x30},{0x51478,0x10},{0x51494,0x10},
  {0x514FC,0x30},{0x5150C,0x20},{0x515EC,0x30},{0x51628,0x10},{0x516C8,0x10},{0x516D8,0x10},{0x51794,0x10},
  {0x517A4,0x10},{0x517F4,0x10},{0x517FC,0x40},{0x51878,0x10},{0x51910,0x10},{0x53CA0,0x40},{0x53F3C,0x10},
  {0x540F4,0x10},{0x56B50,0x10},{0x5ADE4,0x10},{0x5ADE8,0x10},{0x5ADEC,0x10},{0x5ADF0,0x10},{0x5B5E8,0x10},{0x5BA20,0x10},
  {0x5BB24,0x10},{0x5BBA8,0x10},{0x5BC20,0x10},{0x5BD08,0x10},{0x5BD74,0x10},{0x5D5FC,0x30},{0x68194,0x30},
  {0x6946C,0x40},{0x6A2AC,0x40},{0x72BA8,0x30},{0x72C30,0x30},
};
// If the current access (RAM addr) is an index-4..7 task-array slot for this PC, return its shadow
// byte offset (0..0x3F); otherwise return -1. ram[] is the main RAM array (member of CModel3).
#define TASK_SHADOW_OFF(A) ({ int _r = -1; if (g_spikeofe8task && (A) < 0x00800000) { \
    unsigned _b = g_taskPcBase[(ppc_get_pc()>>2) & 0x1FFFF]; \
    if (_b) { UINT32 _fp = (*(UINT32 *) &ram[0x104114]) & 0x7FFFFF; int _so = (int)(A) - (int)_fp - (int)_b; \
      if (_so >= 0x10 && _so < 0x20) _r = (((int)_b - 0x10) & 0x30) + (_so - 0x10); } } _r; })

/******************************************************************************
 Model 3 Inputs

 Game controls. The EEPROM is mapped here as well.
******************************************************************************/

/*
 * Test input script (config option TestInputScript). Each line is
 *   <start frame> <end frame> <input>
 * and holds that input down from start to end (inclusive). Inputs: coin1,
 * start1, service, test, up, down, left, right, beat, charge, jump, shift.
 * Lines starting with '#' are ignored. Used for automated link tests.
 * "<start> <end> poke <hex address> <hex byte>" writes a RAM byte every frame
 * in the range instead.
 */
void CModel3::LoadTestInputScript(const std::string &path)
{
  static const struct { const char *name; unsigned reg; UINT8 mask; } names[] =
  {
    { "coin1", 0x04, 0x01 }, { "test", 0x04, 0x04 }, { "service", 0x04, 0x08 }, { "start1", 0x04, 0x10 },
    { "up", 0x08, 0x20 }, { "down", 0x08, 0x10 }, { "left", 0x08, 0x80 }, { "right", 0x08, 0x40 },
    { "beat", 0x08, 0x01 }, { "charge", 0x08, 0x02 }, { "shift", 0x08, 0x04 }, { "jump", 0x08, 0x08 },
  };

  m_testInputs.clear();
  m_testPokes.clear();
  FILE *fp = fopen(path.c_str(), "r");
  if (fp == NULL)
  {
    ErrorLog("TestInputScript: unable to open '%s'.", path.c_str());
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), fp))
  {
    unsigned start, end;
    char input[64];
    if (line[0] == '#' || sscanf(line, "%u %u %63s", &start, &end, input) != 3)
      continue;
    unsigned pokeAddr, pokeData;
    if (strcmp(input, "poke") == 0 && sscanf(line, "%*u %*u %*s %x %x", &pokeAddr, &pokeData) == 2)
      m_testPokes.push_back({ start, end, pokeAddr & 0x7FFFFF, (UINT8) pokeData });
    for (const auto &n : names)
    {
      if (strcmp(n.name, input) == 0)
        m_testInputs.push_back({ start, end, n.reg, n.mask });
    }
  }
  fclose(fp);
  InfoLog("TestInputScript: loaded %zu events from '%s'.", m_testInputs.size(), path.c_str());
}

UINT8 CModel3::ApplyTestInputs(unsigned reg, UINT8 data) const
{
  for (const auto &e : m_testInputs)
  {
    if (e.reg == reg && m_testFrame >= e.startFrame && m_testFrame <= e.endFrame)
      data &= ~e.mask;
  }
  return data;
}

UINT8 CModel3::ReadInputs(unsigned reg)
{
  UINT8 adc[8];
  UINT8 data;

  // 11/20 satellite clone (Stream/Satellite.h): every linked cabinet records what the game read from port
  // 0x04 (bank 0), 0x08 and 0x0C this frame; the clone replays the followed cabinet's record instead.
  static bool s_satInner = false;
  if (g_spikeofe8task && !s_satInner)
  {
    int p = SatInputPort(reg & 0x3F);
    if (p == 0 && (inputBank & 1))
      p = -1;
    if (p >= 0)
    {
      if (g_satClone.on)
      {
        SatCloneState &c = g_satClone;
        if (!c.haveInputs || !c.inpCnt[p])
          return c.inpLast[p];
        unsigned i = c.inpIdx[p] < c.inpCnt[p] ? c.inpIdx[p] : c.inpCnt[p] - 1u;
        c.inpIdx[p]++;
        return c.inp[p][i];
      }
      s_satInner = true;
      UINT8 d = ReadInputs(reg);
      s_satInner = false;
      SatInputRecord &r = g_satInRec;
      if (r.cnt[p] < 4)
        r.val[p][r.cnt[p]++] = d;
      else
        r.val[p][3] = d;
      return d;
    }
  }
  reg &= 0x3F;
  switch (reg)
  {
  case 0x00:  // input bank
    return inputBank;

  case 0x04:  // current input bank

    data = 0xFF;

    if ((inputBank&1) == 0)
    {
      data &= ~(Inputs->coin[0]->value);        // Coin 1
      data &= ~(Inputs->coin[1]->value<<1);     // Coin 2
      data &= ~(Inputs->test[0]->value<<2);     // Test A
      data &= ~(Inputs->service[0]->value<<3);  // Service A
      data &= ~(Inputs->start[0]->value<<4);    // Start 1
      data &= ~(Inputs->start[1]->value<<5);    // Start 2
      // 11/16 (owner): Shift + Beat + Charge + Jump held with Start opens the 3-minute mode (outside the link session).
      // While all four are held, Start does not reach the game. Fixed rule, no ini switch. Shift + Start (ally stats)
      // and every other use of Start are untouched.
      // 11/27 (owner): not on a single cabinet (Network = 0, LINKS 1 - no net board running): there the 3-minute mode is
      // allowed again.
      if ((m_game.inputs & Game::INPUT_SPIKEOUT) && m_runNetBoard && Inputs->shift->value && Inputs->beat->value &&
          Inputs->charge->value && Inputs->jump->value)
        data |= 0x10;
      data = ApplyTestInputs(0x04, data);
      if (g_floorDump)   // 11/13 FloorDump: coin on this cabinet -> ask all cabinets to dump (see SpikeoutFloorDumpTick)
      {
        static bool prevCoinD = false;
        bool coin = Inputs->coin[0]->value != 0;
        if (coin && !prevCoinD) SpikeoutFloorDumpRequest(ram);
        prevCoinD = coin;
      }
      if (g_spikeoutWaypointRecord)
      {
        static bool prevCoin = false;
        bool coin = Inputs->coin[0]->value != 0;
        // 11/15 (user): not while an AI cabinet is starting / continuing (01 as a bot logged a waypoint on every continue)
        if (coin && !prevCoin && !(g_spikeoutAI && SpikeoutAIWantsStart(ram))) SpikeoutRecordWaypoint(ram);
        prevCoin = coin;
      }
      // 11/15 (user: on a continue the character was fixed at once, the lever taps never counted): Start confirms the
      // character on the select screen - no coin / Start there (own link state 0x140104 = 2); Beat after the taps does it.
      if (g_spikeoutAI && !g_streamHumanJoined.load() && SpikeoutAIWantsStart(ram) && ram[0x140104 ^ 3] != 2)   // AI player: one coin, then start, once a second (P4: not once a streaming client took the slot)
      {
        unsigned t = g_aiContT0 ? (g_ppcTraceFrame - g_aiContT0) : g_ppcTraceFrame % 60;   // 11/15: a continue: once per attempt
        // 11/15 (user): no coin any more - the cabinets are on free play; Start only.
        if (t >= 20 && t < 24) data &= ~0x10;
      }
    }
    else
    {
      data &= ~(Inputs->service[1]->value<<6);  // Service B
      data &= ~(Inputs->test[1]->value<<7);     // Test B
      data = (data&0xDF)|(EEPROM.Read()<<5);    // bank 1 contains EEPROM data bit
    }

    if ((m_game.inputs & Game::INPUT_SKI))
    {
      if ((inputBank&1) == 0)
      {
        data &= ~(Inputs->skiPollLeft->value<<5);
        data &= ~(Inputs->skiSelect1->value<<6);
        data &= ~(Inputs->skiSelect2->value<<7);
        data &= ~(Inputs->skiSelect3->value<<4);
      }
    }

    return data;

  case 0x08:  // game-specific inputs

    data = 0xFF;

    if ((m_game.inputs & Game::INPUT_SKI))
    {
      data &= ~(Inputs->skiPollRight->value<<0);
    }

    if ((m_game.inputs & Game::INPUT_JOYSTICK1))
    {
      data &= ~(Inputs->up[0]->value<<5);     // P1 Up
      data &= ~(Inputs->down[0]->value<<4);   // P1 Down
      data &= ~(Inputs->left[0]->value<<7);   // P1 Left
      data &= ~(Inputs->right[0]->value<<6);  // P1 Right
    }

    if ((m_game.inputs & Game::INPUT_FIGHTING))
    {
      data &= ~(Inputs->escape[0]->value<<3); // P1 Escape
      data &= ~(Inputs->guard[0]->value<<2);  // P1 Guard
      data &= ~(Inputs->kick[0]->value<<1);   // P1 Kick
      data &= ~(Inputs->punch[0]->value<<0);  // P1 Punch
    }

    if ((m_game.inputs & Game::INPUT_SPIKEOUT))
    {
      data &= ~(Inputs->shift->value<<2);     // Shift
      data &= ~(Inputs->beat->value<<0);      // Beat
      data &= ~(Inputs->charge->value<<1);    // Charge
      data &= ~(Inputs->jump->value<<3);      // Jump

      if (g_spikeoutInputLog && !g_spikeoutAI) SpikeoutInputLogFrame(ram, (UINT8) ~data);   // 11/14 InputLog

      if (g_spikeoutAI && !g_streamHumanJoined.load())   // AI player: override stick/beat for this cabinet (P4: stops for good once a streaming client took the slot)
      {
        g_spikeoutCrom = crom; g_spikeoutCromBank = cromBank; g_spikeoutRam = ram;
        if (g_spikeoutAIProfile)   // 11/23 AIProfile
        {
          auto t0 = std::chrono::steady_clock::now();
          data &= ~SpikeoutAIInputBits(ram);
          SpikeoutAIProfCall(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        }
        else
        data &= ~SpikeoutAIInputBits(ram);     // (active-low: clearing a bit = pressed)
      }
    }

    if ((m_game.inputs & Game::INPUT_SOCCER))
    {
      data &= ~(Inputs->shortPass[0]->value<<2);  // P1 Short Pass
      data &= ~(Inputs->longPass[0]->value<<0);   // P1 Long Pass
      data &= ~(Inputs->shoot[0]->value<<1);      // P1 Shoot
    }

    if ((m_game.inputs & Game::INPUT_VR4))
    {
      data &= ~(Inputs->vr[0]->value<<0); // VR1 Red
      data &= ~(Inputs->vr[1]->value<<1); // VR2 Blue
      data &= ~(Inputs->vr[2]->value<<2); // VR3 Yellow
      data &= ~(Inputs->vr[3]->value<<3); // VR4 Green
    }

    if ((m_game.inputs & Game::INPUT_VIEWCHANGE))
    {
      // Harley is wired slightly differently
      if ((m_game.inputs & Game::INPUT_HARLEY))
        data &= ~(Inputs->viewChange->value<<1);  // View change
      else
        data &= ~(Inputs->viewChange->value<<0);  // View change
    }

    if ((m_game.inputs & Game::INPUT_SHIFT4))
    {
      if (Inputs->gearShift4->value == 2)       // Shift 2
        data &= ~0x60;
      else if (Inputs->gearShift4->value == 4)  // Shift 4
        data &= ~0x20;
      if (Inputs->gearShift4->value == 1)       // Shift 1
        data &= ~0x50;
      else if (Inputs->gearShift4->value == 3)  // Shift 3
        data &= ~0x10;
    }

    if ((m_game.inputs & Game::INPUT_SHIFTUPDOWN))
    {
      // Harley is wired slightly differently
      if ((m_game.inputs & Game::INPUT_HARLEY))
      {
        if (Inputs->gearShiftUp->value)         // Shift up
          data &= ~0x20;
        else if (Inputs->gearShiftDown->value)  // Shift down
          data &= ~0x10;
      }
      else
      {
        if (Inputs->gearShiftUp->value)         // Shift up
          data &= ~0x50;
        else if (Inputs->gearShiftDown->value)  // Shift down
          data &= ~0x60;
      }
    }

    if ((m_game.inputs & Game::INPUT_HANDBRAKE))
      data &= ~(Inputs->handBrake->value<<1);   // Hand brake

    if ((m_game.inputs & Game::INPUT_HARLEY))
      data &= ~(Inputs->musicSelect->value<<0); // Music select

    if ((m_game.inputs & Game::INPUT_GUN1))
      data &= ~(Inputs->trigger[0]->value<<0);  // P1 Trigger

    if ((m_game.inputs & Game::INPUT_ANALOG_JOYSTICK))
    {
      data &= ~(Inputs->analogJoyTrigger1->value<<5); // Trigger 1
      data &= ~(Inputs->analogJoyTrigger2->value<<4); // Trigger 2
      data &= ~(Inputs->analogJoyEvent1->value<<0);   // Event Button 1
      data &= ~(Inputs->analogJoyEvent2->value<<1);   // Event Button 2
    }

    if ((m_game.inputs & Game::INPUT_TWIN_JOYSTICKS)) // First twin joystick
    {
      /*
       * Process left joystick inputs first
       */

      // Shot trigger and Turbo
      data &= ~(Inputs->twinJoyShot1->value<<0);
      data &= ~(Inputs->twinJoyTurbo1->value<<1);

      // Stick
      data &= ~(Inputs->twinJoyLeft1->value<<7);
      data &= ~(Inputs->twinJoyRight1->value<<6);
      data &= ~(Inputs->twinJoyUp1->value<<5);
      data &= ~(Inputs->twinJoyDown1->value<<4);

      /*
       * Next, process twin joystick macro inputs (higher level inputs
       * that map to actions on both joysticks simultaneously).
       */

      /*
       * Forward/reverse/turn are mutually exclusive.
       *
       * Turn Left:   1D 2U
       * Turn Right:  1U 2D
       * Forward:     1U 2U
       * Reverse:     1D 2D
       */
      if (Inputs->twinJoyTurnLeft->value)
        data &= ~0x10;
      else if (Inputs->twinJoyTurnRight->value)
        data &= ~0x20;
      else if (Inputs->twinJoyForward->value)
        data &= ~0x20;
      else if (Inputs->twinJoyReverse->value)
        data &= ~0x10;

      /*
       * Strafe/crouch/jump are mutually exclusive.
       *
       * Strafe Left:   1L 2L
       * Strafe Right:  1R 2R
       * Jump:          1L 2R
       * Crouch:        1R 2L
       */
      if (Inputs->twinJoyStrafeLeft->value)
        data &= ~0x80;
      else if (Inputs->twinJoyStrafeRight->value)
        data &= ~0x40;
      else if (Inputs->twinJoyJump->value)
        data &= ~0x80;
      else if (Inputs->twinJoyCrouch->value)
        data &= ~0x40;
    }

    if ((m_game.inputs & Game::INPUT_ANALOG_GUN1))
    {
      data &= ~(Inputs->analogTriggerLeft[0]->value<<0);
      data &= ~(Inputs->analogTriggerRight[0]->value<<1);
    }

    if ((m_game.inputs & Game::INPUT_MAGTRUCK))
      data &= ~(Inputs->magicalPedal1->value << 0);

    if ((m_game.inputs & Game::INPUT_FISHING))
    {
      if (m_game.name == "getbassur")
      {
        // bass fishing
        data &= ~(Inputs->fishingCast->value << 0);
        data &= ~(Inputs->fishingSelect->value << 1);
      }
      else
      {
        // get bass fishing
        data &= ~(!Inputs->fishingCast->value << 4);
        data &= ~(!Inputs->fishingSelect->value << 5);
      }
    }
    return ApplyTestInputs(0x08, data);

  case 0x10: // Drive board
      return OutputRegister[0];
  case 0x14: // Lamps
      return OutputRegister[1];

  case 0x0C:  // game-specific inputs

    data = 0xFF;

    if (DriveBoard->IsAttached() && DriveBoard->GetType() != Game::DRIVE_BOARD_BILLBOARD)
    {
      // If driveboard is set as billboard, don't read BillBoard reg (no inputs)
      data = DriveBoard->Read();
    }

    if ((m_game.inputs & Game::INPUT_JOYSTICK2))
    {
      data &= ~(Inputs->up[1]->value<<5);     // P2 Up
      data &= ~(Inputs->down[1]->value<<4);   // P2 Down
      data &= ~(Inputs->left[1]->value<<7);   // P2 Left
      data &= ~(Inputs->right[1]->value<<6);  // P2 Right
    }

    if ((m_game.inputs & Game::INPUT_FIGHTING))
    {
      data &= ~(Inputs->escape[1]->value<<3); // P2 Escape
      data &= ~(Inputs->guard[1]->value<<2);  // P2 Guard
      data &= ~(Inputs->kick[1]->value<<1);   // P2 Kick
      data &= ~(Inputs->punch[1]->value<<0);  // P2 Punch
    }

    if ((m_game.inputs & Game::INPUT_SOCCER))
    {
      data &= ~(Inputs->shortPass[1]->value<<2);  // P2 Short Pass
      data &= ~(Inputs->longPass[1]->value<<0);   // P2 Long Pass
      data &= ~(Inputs->shoot[1]->value<<1);      // P2 Shoot
    }

    if ((m_game.inputs & Game::INPUT_TWIN_JOYSTICKS)) // Second twin joystick (see register 0x08 for comments)
    {

      data &= ~(Inputs->twinJoyShot2->value<<0);
      data &= ~(Inputs->twinJoyTurbo2->value<<1);

      data &= ~(Inputs->twinJoyLeft2->value<<7);
      data &= ~(Inputs->twinJoyRight2->value<<6);
      data &= ~(Inputs->twinJoyUp2->value<<5);
      data &= ~(Inputs->twinJoyDown2->value<<4);

      if (Inputs->twinJoyTurnLeft->value)
        data &= ~0x20;
      else if (Inputs->twinJoyTurnRight->value)
        data &= ~0x10;
      else if (Inputs->twinJoyForward->value)
        data &= ~0x20;
      else if (Inputs->twinJoyReverse->value)
        data &= ~0x10;

      if (Inputs->twinJoyStrafeLeft->value)
        data &= ~0x80;
      else if (Inputs->twinJoyStrafeRight->value)
        data &= ~0x40;
      else if (Inputs->twinJoyJump->value)
        data &= ~0x40;
      else if (Inputs->twinJoyCrouch->value)
        data &= ~0x80;
    }

    if ((m_game.inputs & Game::INPUT_GUN2))
      data &= ~(Inputs->trigger[1]->value<<0);  // P2 Trigger

    if ((m_game.inputs & Game::INPUT_ANALOG_GUN2))
    {
      data &= ~(Inputs->analogTriggerLeft[1]->value<<0);
      data &= ~(Inputs->analogTriggerRight[1]->value<<1);
    }

    if ((m_game.inputs & Game::INPUT_MAGTRUCK))
      data &= ~(Inputs->magicalPedal2->value << 0);

    return data;

  case 0x18:         // swtrilgy and getbass. Remove IO board error on getbass. Not sure, but may be related to device feedback ?
      data = 0x7f;   // Note : when this returned value is wrong, there is a side effect on Ocean Hunter game, a sort of 3d interlaced effect
      if (m_game.name == "bassdx" || m_game.name == "getbassdx" || m_game.name == "getbass")  // Prevent I/O erreur after a while (related to tension)
      {
          data = 0x01;
      }
      return data;

  case 0x2C:  // Serial FIFO 1
    return serialFIFO1;

  case 0x30:  // Serial FIFO 2
    return serialFIFO2;

  case 0x34:  // Serial FIFO full/empty flags
    if (m_game.inputs & (Game::INPUT_GUN1 | Game::INPUT_GUN2)) {
      return 0x0C;
    }
    else {
      return 0;
    }

  case 0x3C:  // ADC

    // Load ADC channels with input data
    memset(adc, 0, sizeof(adc));
    if ((m_game.inputs & Game::INPUT_VEHICLE))
    {
      adc[0] = (UINT8)Inputs->steering->value;
      adc[1] = (UINT8)Inputs->accelerator->value;
      adc[2] = (UINT8)Inputs->brake->value;
      if ((m_game.inputs & Game::INPUT_HARLEY))
        adc[3] = (UINT8)Inputs->rearBrake->value;
    }

    if ((m_game.inputs & Game::INPUT_ANALOG_JOYSTICK))
    {
      adc[0] = (UINT8)Inputs->analogJoyY->value;
      adc[1] = (UINT8)Inputs->analogJoyX->value;
    }

    if (m_game.inputs & (Game::INPUT_ANALOG_GUN1 | Game::INPUT_ANALOG_GUN2))
    {
      adc[0] = (UINT8)Inputs->analogGunX[0]->value;
      adc[2] = (UINT8)Inputs->analogGunY[0]->value;
      adc[1] = (UINT8)Inputs->analogGunX[1]->value;
      adc[3] = (UINT8)Inputs->analogGunY[1]->value;

  	  // Unclear why this is necessary or how to cleanly fix it, so I'm
  	  // disabling it but leaving it here for future reference. The proper fix is
  	  // probably to allow users to define inverted controls for this game only,
  	  // which means the input system must support loading per-game config (not
  	  // all analog_gun games require axis inversion to be playable).
	  if (m_game.name == "lostwsga" || m_game.name == "lostwsgo")
	  { // to do, not a string compare
        adc[0] =       (UINT8)Inputs->analogGunX[0]->value; // order is different for some reason in lost world
        adc[1] = 255 - (UINT8)Inputs->analogGunY[0]->value; // why are values inverted? is this the wrong place to fix this
        adc[2] =       (UINT8)Inputs->analogGunX[1]->value;
        adc[3] = 255 - (UINT8)Inputs->analogGunY[1]->value;
      }
    }

    if ((m_game.inputs & Game::INPUT_SKI))
    {
      adc[0] = (UINT8)Inputs->skiY->value;
      adc[1] = (UINT8)Inputs->skiX->value;
    }

    if ((m_game.inputs & Game::INPUT_MAGTRUCK))
    {
      adc[0] = uint8_t(Inputs->magicalLever1->value);
      adc[1] = uint8_t(Inputs->magicalLever2->value);
    }

    if ((m_game.inputs & Game::INPUT_FISHING))
    {
      adc[0] = uint8_t(Inputs->fishingRodY->value);
      adc[1] = uint8_t(Inputs->fishingRodX->value);
      adc[2] = uint8_t(Inputs->fishingTension->value); // get bass fishing only : Tension Sensor ?
      adc[3] = uint8_t(Inputs->fishingReel->value);
      adc[5] = uint8_t(Inputs->fishingStickX->value);
      adc[4] = uint8_t(Inputs->fishingStickY->value);
    }

    // Read out appropriate channel
    data = adc[adcChannel&7];
    ++adcChannel;
    return data;

  default:
    break;
  }

  return 0xFF;  // controls are active low
}

void CModel3::WriteInputs(unsigned reg, UINT8 data)
{
  switch (reg&0x3F)
  {
  case 0:
    EEPROM.Write((data>>6)&1,(data>>7)&1,(data>>5)&1);
    inputBank = data;
    break;

  case 0x10:  // Drive board
    if (DriveBoard->IsAttached())
      DriveBoard->Write(data);
    if (NULL != Outputs) // TODO - check gameInputs
      Outputs->SetValue(OutputRawDrive, data);
    OutputRegister[0] = data;
    break;

  case 0x14:  // Lamp outputs (Daytona/Scud Race/Sega Rally/Le Mans 24)
    if (NULL != Outputs) // TODO - check gameInputs
    {
      Outputs->SetValue(OutputLampStart, !!(data&0x04));
      Outputs->SetValue(OutputLampView1, !!(data&0x08));
      Outputs->SetValue(OutputLampView2, !!(data&0x10));
      Outputs->SetValue(OutputLampView3, !!(data&0x20));
      Outputs->SetValue(OutputLampView4, !!(data&0x40));
      Outputs->SetValue(OutputLampLeader, !!(data&0x80));
      Outputs->SetValue(OutputRawLamps, data);
    }
    OutputRegister[1] = data;
    break;

  case 0x24:  // Serial FIFO 1
    switch (data) // Command
    {
    case 0x00:    // Light gun register select
      gunReg = serialFIFO2;
      break;
    case 0x87:    // Read light gun register
      serialFIFO1 = 0;  // clear serial FIFO 1
      serialFIFO2 = 0;
      if ((m_game.inputs & Game::INPUT_GUN1)||(m_game.inputs & Game::INPUT_GUN2))
      {
        switch (gunReg)
        {
        case 0: // Player 1 gun Y (low 8 bits)
          serialFIFO2 = Inputs->gunY[0]->value&0xFF;
          break;
        case 1: // Player 1 gun Y (high 2 bits)
          serialFIFO2 = (Inputs->gunY[0]->value>>8)&3;
          break;
        case 2: // Player 1 gun X (low 8 bits)
          serialFIFO2 = Inputs->gunX[0]->value&0xFF;
          break;
        case 3: // Player 1 gun X (high 2 bits)
          serialFIFO2 = (Inputs->gunX[0]->value>>8)&3;
          break;
        case 4: // Player 2 gun Y (low 8 bits)
          serialFIFO2 = Inputs->gunY[1]->value&0xFF;
          break;
        case 5: // Player 2 gun Y (high 2 bits)
          serialFIFO2 = (Inputs->gunY[1]->value>>8)&3;
          break;
        case 6: // Player 2 gun X (low 8 bits)
          serialFIFO2 = Inputs->gunX[1]->value&0xFF;
          break;
        case 7: // Player 2 gun X (high 2 bits)
          serialFIFO2 = (Inputs->gunX[1]->value>>8)&3;
          break;
        case 8: // Off-screen indicator (bit 0 = player 1, bit 1 = player 2, set indicates off screen)
          serialFIFO2 = (Inputs->trigger[1]->offscreenValue<<1)|Inputs->trigger[0]->offscreenValue;
          break;
        default:
          DebugLog("Unknown gun register: %X\n", gunReg);
          break;
        }
      }
      break;
    default:
      DebugLog("Uknown command to serial FIFO: %02X\n", data);
      break;
    }
    break;
  case 0x28:  // Serial FIFO 2
    serialFIFO2 = data;
    break;
  case 0x3C:
    adcChannel = data&7;
    break;
  default:
    break;
  }
  //printf("Controls: %X=%02X\n", reg, data);
}


/******************************************************************************
 Model 3 Security Device

 The security device is present in some games. Virtual On and Dirt Devils read
 tile pattern data from it. Spikeout calls a routine at PC=0x6FAC8 that writes/
 reads the security device and, if the return value in R3 is 0, prints "ILLEGAL
 ROM" and locks the game. Our habit of returning all 1's for unknown reads
 seems to help avoid this.
******************************************************************************/

uint16_t CModel3::ReadSecurityRAM(uint32_t addr)
{
  if (addr < 0x8000)
    return (*(uint32_t *) &securityRAM[addr * 4]) >> 16;
  return 0;
}

UINT32 CModel3::ReadSecurity(unsigned reg)
{
  switch (reg)
  {
  case 0x00:  // Status
    return 0;
  case 0x1C:  // Data
    if (m_securityFirstRead)
    {
      m_securityFirstRead = false;
      return 0xFFFF0000;
    }
    else
    {
      uint8_t *base_ptr;
      return m_cryptoDevice.Decrypt(&base_ptr) << 16;
    }
  default:
    DebugLog("Security read: reg=%X\n", reg);
    break;
  }

  return 0xFFFFFFFF;
}

void CModel3::WriteSecurity(unsigned reg, UINT32 data)
{
  switch (reg)
  {
  case 0x10:
  case 0x14:
    m_cryptoDevice.SetAddressLow(0);
    m_cryptoDevice.SetAddressHigh(0);
    m_securityFirstRead = true;
    break;
  case 0x18:
  {
    uint16_t subKey = data >> 16;
    subKey = ((subKey & 0xFF00) >> 8) | ((subKey & 0x00FF) << 8);
    m_cryptoDevice.SetSubKey(subKey);
    break;
  }
  default:
    DebugLog("Security write: reg=%X, data=%08X (PC=%08X, LR=%08X)\n", reg, data, ppc_get_pc(), ppc_get_lr());
    break;
  }
}


/******************************************************************************
 PCI Devices

 Unknown PCI devices are handled here.
******************************************************************************/

UINT32 CModel3::ReadPCIConfigSpace(unsigned device, unsigned reg, unsigned bits, unsigned offset) const
{
  if ((bits==8) || (bits==16))
  {
    DebugLog("Model 3: %d-bit PCI read request for reg=%02X\n", bits, reg);
    return 0;
  }

  switch (device)
  {
  case 16:  // Used by Daytona 2
    switch (reg)
    {
    case 0: // PCI vendor and device ID
      return 0x182711DB;
    default:
      break;
    }
    break;
  default:
    break;
  }

  DebugLog("Model 3: PCI %d-bit write request for device=%d, reg=%02X\n", bits, device, reg);
  return 0;
}

void CModel3::WritePCIConfigSpace(unsigned device, unsigned reg, unsigned bits, unsigned offset, UINT32 data)
{
  DebugLog("Model 3: PCI %d-bit write request for device=%d, reg=%02X, data=%08X\n", bits, device, reg, data);
}


/******************************************************************************
 Model 3 System Registers

 NOTE: Different modules that generate IRQs, like the tilegen, Real3D, and
 SCSP, should each call IRQ.Assert() on their own.
******************************************************************************/

// Set the CROM bank index (active low logic)
void CModel3::SetCROMBank(unsigned idx)
{
  cromBankReg = idx;
  idx = (~idx) & 0xF;
  cromBank = &crom[0x800000 + (idx*0x800000)];
  DebugLog("CROM bank setting: %d (%02X), PC=%08X, LR=%08X\n", idx, cromBankReg, ppc_get_pc(), ppc_get_lr());
}

UINT8 CModel3::ReadSystemRegister(unsigned reg) const
{
  switch (reg&0x3F)
  {
  case 0x08:  // CROM bank
    return cromBankReg;
  case 0x14:  // IRQ enable
    return IRQ.ReadIRQEnable();
  case 0x18:  // IRQ pending
    return IRQ.ReadIRQState();
  case 0x1C:  // unknown (apparently expects some or all bits set)
    //DebugLog("System register %02X read\n", reg);
    return 0xFF;
  case 0x10:  // JTAG Test Access Port
    return m_jtag.Read() << 5;
  default:
    //DebugLog("System register %02X read\n", reg);
    break;
  }

  return 0xFF;
}

void CModel3::WriteSystemRegister(unsigned reg, UINT8 data)
{
  switch (reg&0x3F)
  {
  case 0x08:  // CROM bank
    SetCROMBank(data);
    break;
  case 0x14:  // IRQ enable
    IRQ.WriteIRQEnable(data);
    DebugLog("IRQ ENABLE=%02X\n", data);
    break;
  case 0x18:  // IRQ acknowledge
    IRQ.Deassert(data);
    DebugLog("IRQ ACK? %02X=%02X\n", reg, data);
    break;
  case 0x0C:  // JTAG Test Access Port
  {
    uint8_t tck = (data >> 6) & 1;
    uint8_t tms = (data >> 2) & 1;
    uint8_t tdi = (data >> 5) & 1;
    uint8_t trst = (data >> 7) & 1; // not sure about this one (trst not required to exist by JTAG spec)
    m_jtag.Write(tck, tms, tdi, trst);
    break;
  }
  case 0x0D:
  case 0x0E:
  case 0x0F:
  case 0x1C:  // LED control
    break;
  default:
    //DebugLog("System register %02X=%02X\n", reg, data);
    break;
  }
}


/******************************************************************************
 Address Space Access Handlers

 NOTE: Testing of some of the address ranges is not strict enough, especially
 for the MPC10x. Write32() handles the MPC10x most correctly.
******************************************************************************/

/*
 * CModel3::Read8(addr):
 * CModel3::Read16(addr):
 * CModel3::Read32(addr):
 * CModel3::Read64(addr):
 *
 * Read handlers.
 */
UINT8 CModel3::Read8(UINT32 addr)
{
  // RAM (most frequently accessed)
  if (addr<0x00800000)
  {
    TASKLOG(addr, 0, 8);
    { int _s = TASK_SHADOW_OFF(addr); if (_s >= 0) return g_taskShadow[_s^3]; }
    return ram[addr^3];
  }

  // Other
  switch ((addr >> 24))
  {
  // CROM
  case 0xFF:
    if (addr < 0xFF800000)
      return cromBank[(addr & 0x7FFFFF) ^ 3];
    else
      return crom[(addr & 0x7FFFFF) ^ 3];

  // Real3D DMA
  case 0xC2:
    return GPU.ReadDMARegister8(addr & 0xFF);

  // Various
  case 0xF0:
  case 0xFE:  // mirror

    switch ((addr >> 16) & 0xFF)
    {
    // Inputs
    case 0x04:
      return ReadInputs(addr & 0x3F);

    // Sound Board
    case 0x08:
      switch (addr & 0xf)
      {
      case 0x0:         // MIDI data port
        return 0x00;    // Something to do with region locked in magtruck (0=locked, 1=unlocked). /!\ no effect if rom patch is activated!
      case 0x4:         // MIDI control port
        return 0x83;    // magtruck country check
      default:
        return 0;
      }
      break;

    // System registers
    case 0x10:
      return ReadSystemRegister(addr & 0x3F);

    // RTC
    case 0x14:
      if ((addr & 3) == 1)  // battery voltage test
        return 0x03;
      else if ((addr & 3) == 0)
        return RTC.ReadRegister((addr >> 2) & 0xF);
      return 0;

    // Unknown
    default:
      //printf("CMODEL3 : unknown R8 mirror : %x\n", addr >> 16);
      break;
    }

    break;

  // Tile generator
  case 0xF1:
    if (addr < 0xF1120000)
    {
      // Tile generator accesses its RAM as little endian, no adjustment needed here
      return TileGen.ReadRAM8(addr & 0x1FFFFF);
    }
    break;

  // 53C810 SCSI
  case 0xC0:  // only on Step 1.x
    if (m_runNetBoard)
    {
      switch ((addr & 0x3ffff) >> 16)
      {
      case 0:
        return NetBoard->ReadCommRAM8((addr & 0xFFFF) ^ 2);

      case 1: // ioreg 32bits access in 16bits environment
        if (addr > 0xc00101ff)
        {
          printf("R8 ATTENTION OUT OF RANGE\n");
          SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Info", "Out of Range", NULL);
        }
        return (UINT8)NetBoard->ReadIORegister((addr & 0x1FF) / 2);

      case 2:
      case 3:
        return netRAM[((addr & 0x1FFFF) / 2)];

      default:
        printf("R8 ATTENTION OUT OF RANGE\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Info", "Out of Range", NULL);
        break;
      }
    }
  else if (m_stepping > 0x15 || SCSI.GetBaseAddress() != 0xC0) break;
  case 0xF9:
  case 0xC1:
    return SCSI.ReadRegister(addr&0xFF);

  // Unknown
  default:
    printf("CMODEL3 : unknown R8 : %x addr=%08X PC=%08X LR=%08X\n", addr >> 24, addr, ppc_get_pc(), ppc_get_lr());
    break;
  }

  DebugLog("PC=%08X\tread8 : %08X\n", ppc_get_pc(), addr);
  return 0xFF;
}

UINT16 CModel3::Read16(UINT32 addr)
{
  UINT16  data;

  if ((addr&1))
  {
    data =  Read8(addr+0)<<8;
    data |= Read8(addr+1);
    return data;
  }

  // RAM (most frequently accessed)
  if (addr<0x00800000)
  {
    TASKLOG(addr, 0, 16);
    { int _s = TASK_SHADOW_OFF(addr); if (_s >= 0) return *(UINT16 *) &g_taskShadow[_s^2]; }
    return *(UINT16 *) &ram[addr^2];
  }

  // Other
  switch ((addr>>24))
  {
  // CROM
  case 0xFF:
    if (addr < 0xFF800000)
      return *(UINT16 *) &cromBank[(addr&0x7FFFFF)^2];
    else
      return *(UINT16 *) &crom[(addr&0x7FFFFF)^2];

  // Various
  case 0xF0:
  case 0xFE:  // mirror

    switch ((addr>>16)&0xFF)
    {
    // Backup RAM
    case 0x0C:
    case 0x0D:
      return *(UINT16 *) &backupRAM[(addr&0x1FFFF)^2];

    // Sound Board
    case 0x08:
      //printf("PPC: Read16 %08X\n", addr);
      break;

    // MPC105
    case 0xC0:  // F0C00CF8
      if (PCIBridge.GetModel() == 0x105)
        return PCIBridge.ReadPCIConfigData(16,addr&3);
      break;

    // MPC106
    case 0xE0:
    case 0xE1:
    case 0xE2:
    case 0xE3:
    case 0xE4:
    case 0xE5:
    case 0xE6:
    case 0xE7:
    case 0xE8:
    case 0xE9:
    case 0xEA:
    case 0xEB:
    case 0xEC:
    case 0xED:
    case 0xEE:
    case 0xEF:
      if (PCIBridge.GetModel() == 0x106)
        return PCIBridge.ReadPCIConfigData(16,addr&3);
      break;

    // Unknown
    default:
      //printf("CMODEL3 : unknown R16 mirror : %x\n", addr >> 16);
      break;
    }

    break;

  // Tile generator
  case 0xF1:
    if (addr < 0xF1120000)
    {
      // Tile generator accesses its RAM as little endian, no adjustment needed here
      data = TileGen.ReadRAM16(addr&0x1FFFFF);
      return FLIPENDIAN16(data);
    }
    break;

  case 0xc0: // spikeout calls this
  // interesting : poking @4 master to same value as slave (0x100) or simply !=0 -> connected and go in game, but freeze (prints comm error) as soon as players appear after the gate
  // sort of sync ack ? who writes this 16b value ?
  {
    UINT16 result;
    switch ((addr & 0x3ffff) >> 16)
    {
    case 0:
      result = NetBoard->ReadCommRAM16((addr & 0xFFFF) ^ 2);
      return FLIPENDIAN16(result); // result
    default:
      printf("CMODEL3 : unknown R16 : %x (C0)\n", addr);
      break;
    }
  }
  break;
  // Unknown
  default:
    printf("CMODEL3 : unknown R16 : %x (%x)\n", addr, addr >> 24);
    {
      char _msg[128];
      snprintf(_msg, sizeof(_msg), "CMODEL3 : Unknown R16 : 0x%08X (PC=0x%08X)", addr, ppc_get_pc());
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Info", _msg, NULL);
    }
    break;
  }

  DebugLog("PC=%08X\tread16: %08X\n", ppc_get_pc(), addr);
  return 0xFFFF;
}

UINT32 CModel3::Read32(UINT32 addr)
{
  UINT32  data;

  if ((addr&3))
  {
    data =  Read16(addr+0)<<16;
    data |= Read16(addr+2);
    return data;
  }

  // RAM (most frequently accessed)
  if (addr < 0x00800000)
  {
    /* Player pointer table probe: log reads of the 4-entry table at 0x140014+(slot+1)*4
       (0x140018..0x140034) with PC, to find every accessor before relocating it to 8 entries.
       Node-mask/link reads in the same range are distinguished later by disassembling each PC. */
    if (g_ppcStateTrace && addr >= 0x00140018 && addr < 0x00140038)
    {
      static FILE *ptf = NULL;
      if (!ptf) ptf = fopen("ptbl.txt", "w");
      if (ptf) { fprintf(ptf, "f=%u addr=%08X PC=%08X LR=%08X\n", m_testFrame, addr, ppc_get_pc(), ppc_get_lr()); fflush(ptf); }
    }
    TASKLOG(addr, 0, 32);
    { int _s = TASK_SHADOW_OFF(addr); if (_s >= 0) return *(UINT32 *) &g_taskShadow[_s]; }
    return *(UINT32 *) &ram[addr];
  }

  // Other
  switch ((addr>>24))
  {
  // CROM
  case 0xFF:
    if (addr < 0xFF800000)
      return *(UINT32 *) &cromBank[(addr&0x7FFFFF)];
    else
      return *(UINT32 *) &crom[(addr&0x7FFFFF)];

  // Real3D registers
  case 0x84:
    data = GPU.ReadRegister(addr&0x3F);
    return FLIPENDIAN32(data);

  // Real3D DMA
  case 0xC2:
    data = GPU.ReadDMARegister32(addr&0xFF);
    return FLIPENDIAN32(data);

  // Various
  case 0xF0:
  case 0xFE:  // mirror

    switch ((addr>>16)&0xFF)
    {
    // Inputs
    case 0x04:
      data =  ReadInputs((addr&0x3F)+0) << 24;
      data |= ReadInputs((addr&0x3F)+1) << 16;
      data |= ReadInputs((addr&0x3F)+2) << 8;
      data |= ReadInputs((addr&0x3F)+3) << 0;
      return data;

    // Sound Board
    case 0x08:
      //printf("PPC: Read32 %08X\n", addr);
      break;

    // Backup RAM
    case 0x0C:
    case 0x0D:
      return *(UINT32 *) &backupRAM[(addr&0x1FFFF)];

    // System registers
    case 0x10:
      data =  ReadSystemRegister((addr&0x3F)+0) << 24;
      data |= ReadSystemRegister((addr&0x3F)+1) << 16;
      data |= ReadSystemRegister((addr&0x3F)+2) << 8;
      data |= ReadSystemRegister((addr&0x3F)+3) << 0;
      return data;

    // MPC105
    case 0xC0:  // F0C00CF8
      if (PCIBridge.GetModel() == 0x105)
        return PCIBridge.ReadPCIConfigData(32,0);
      break;

    // MPC106
    case 0xE0:
    case 0xE1:
    case 0xE2:
    case 0xE3:
    case 0xE4:
    case 0xE5:
    case 0xE6:
    case 0xE7:
    case 0xE8:
    case 0xE9:
    case 0xEA:
    case 0xEB:
    case 0xEC:
    case 0xED:
    case 0xEE:
    case 0xEF:
      if (PCIBridge.GetModel() == 0x106)
        return PCIBridge.ReadPCIConfigData(32,0);
      break;

    // RTC
    case 0x14:
      data = (RTC.ReadRegister((addr>>2)&0xF) << 24);
      data |= 0x00030000; // set these bits to pass battery voltage test
      return data;

    // Security board RAM
    case 0x18:
    case 0x19:
      return *(UINT32 *) &securityRAM[(addr&0x1FFFF)];  // so far, only 32-bit access observed, so we use little endian access

    // Security board registers
    case 0x1A:
      return ReadSecurity(addr&0x3F);

    // Unknown
    default:
      //printf("CModel 3 unknown R32 mirror %x", (addr >> 16) & 0xFF);
      break;
    }

    break;

  // Tile generator
  case 0xF1:
    // Tile generator accesses its RAM as little endian, must flip for big endian PowerPC
    if (addr < 0xF1120000)
    {
      data = TileGen.ReadRAM32(addr&0x1FFFFF);
      return FLIPENDIAN32(data);
    }
    else if ((addr>=0xF1180000) && (addr<0xF1180100))
    {
      data = TileGen.ReadRegister(addr & 0xFF);
      return FLIPENDIAN32(data);
    }

    break;

  // 53C810 SCSI
  case 0xC0:  // only on Step 1.x
    if (m_runNetBoard)
    {
      UINT32 result;

      switch ((addr & 0x3ffff) >> 16)
      {
      case 0:
        result = NetBoard->ReadCommRAM32(addr & 0xFFFF);
        result = FLIPENDIAN32(result);
        return ((result << 16) | (result >> 16));

      case 1: // ioreg 32bits access to 16bits range
        if (addr > 0xc00101ff)
        {
          printf("R32 ATTENTION OUT OF RANGE\n");
        }

        result = NetBoard->ReadIORegister((addr & 0x1FF) / 2);
        return FLIPENDIAN32(result);

      case 2:
      case 3:
        result = (*(UINT32 *)&netRAM[((addr & 0x1FFFF) / 2)]) & 0x0000ffff;
        return FLIPENDIAN32(result); // result

      default:
        printf("R32 ATTENTION OUT OF RANGE\n");
        break;
      }

    }
    else if (m_stepping > 0x15 || SCSI.GetBaseAddress() != 0xC0) break;
  case 0xF9:
  case 0xC1:
    data =  (SCSI.ReadRegister((addr+0)&0xFF) << 24);
    data |= (SCSI.ReadRegister((addr+1)&0xFF) << 16);
    data |= (SCSI.ReadRegister((addr+2)&0xFF) << 8);
    data |= (SCSI.ReadRegister((addr+3)&0xFF) << 0);
    return data;

  // Unknown
  default:
    printf("CMODEL3 : unknown R32 : %x addr=%08X PC=%08X LR=%08X\n", addr >> 24, addr, ppc_get_pc(), ppc_get_lr());
    break;
  }

  DebugLog("PC=%08X\tread32: %08X\n", ppc_get_pc(), addr);
  return 0xFFFFFFFF;
}

UINT64 CModel3::Read64(UINT32 addr)
{
  UINT64  data;

  data = Read32(addr+0);
  data <<= 32;
  data |= Read32(addr+4);
  //printf("read64 %x = %x\n",addr,data);
  return data;
}

/*
 * CModel3::Write8(addr, data):
 * CModel3::Write16(addr, data):
 * CModel3::Write32(addr, data):
 * CModel3::Write64(addr, data):
 *
 * Write handlers.
 */
void CModel3::Write8(UINT32 addr, UINT8 data)
{
  // RAM (most frequently accessed)
  if (addr < 0x00800000)
  {
    // spikeofe: log writes to the fa_enemy type table (one byte per enemy index), i.e. enemy creation
    if (m_traceEnemyCreate && addr >= 0x1113FC && addr < 0x11140C)
      printf("[ENEMY] frame=%u index=%u type=%u PC=%08X LR=%08X\n", m_testFrame, addr - 0x1113FC, data, ppc_get_pc(), ppc_get_lr());
    if (addr >= m_watchStart && addr < m_watchEnd)
      WLOG("[WATCH] frame=%u sync=%u w8 %08X=%02X PC=%08X LR=%08X\n", m_testFrame, *(UINT32 *) &ram[0x1000A8], addr, data, ppc_get_pc(), ppc_get_lr());
    TASKLOG(addr, 1, 8);
    { int _s = TASK_SHADOW_OFF(addr); if (_s >= 0) { g_taskShadow[_s^3] = data; return; } }
    ram[addr^3] = data;
    return;
  }

  // Other
  switch ((addr>>24))
  {
  // Real3D DMA
  case 0xC2:
    GPU.WriteDMARegister8(addr&0xFF,data);
    break;

  // Various
  case 0xF0:
  case 0xFE:  // mirror

    switch ((addr>>16)&0xFF)
    {
    // Inputs
    case 0x04:
      WriteInputs(addr&0x3F,data);
      break;

    // Sound Board
    case 0x08:
      //printf("PPC: %08X=%02X * (PC=%08X, LR=%08X)\n", addr, data, ppc_get_pc(), ppc_get_lr());
      if ((addr & 0xF) == 0)      // MIDI data port
      {
        SoundBoard.WriteMIDIPort(data);
        IRQ.Deassert(0x40);
      }
      else if ((addr & 0xF) == 4) // MIDI control port
      {
        midiCtrlPort = data;
        if ((data & 0x20) == 0)
          IRQ.Deassert(0x40);
      }
      break;

    // Backup RAM
    case 0x0C:
    case 0x0D:
      backupRAM[(addr&0x1FFFF)^3] = data;
      break;

    // System registers
    case 0x10:
      WriteSystemRegister(addr&0x3F,data);
      break;

    // RTC
    case 0x14:
      if ((addr&3)==0)
        RTC.WriteRegister((addr>>2)&0xF,data);
      break;

    // Unknown
    default:
      //printf("CMODEL3 : unknown W8 mirror : %x\n", addr >> 16);
      break;
    }

    DebugLog("PC=%08X\twrite8 : %08X=%02X\n", ppc_get_pc(), addr, data);
    break;

  // Tile generator
  case 0xF1:
    if (addr < 0xF1120000)
    {
      // Tile generator accesses its RAM as little endian, no adjustment needed here
      TileGen.WriteRAM8(addr&0x1FFFFF, data);
      break;
    }
    goto Unknown8;

  // MPC105
  case 0xF8:
    if (PCIBridge.GetModel() == 0x105)
      PCIBridge.WriteRegister(addr&0xFF,data);
    break;

  // 53C810 SCSI
  case 0xC0:  // only on Step 1.x
    if (m_runNetBoard)
    {
      //printf("CModel 3 : write8 %x<-%x\n", addr, data);

      switch ((addr & 0x3ffff) >> 16)
      {
      case 0:
        NetBoard->WriteCommRAM8((addr & 0xFFFF) ^ 2, data);
        break;

      case 1: // ioreg 32bits access to 16bits range
        if (addr > 0xc00101ff)
        {
          printf("W8 ATTENTION OUT OF RANGE\n");
        }

        NetBoard->WriteIORegister((addr & 0x1FF) / 2, data);
        break;

      case 2:
      case 3:
        *(UINT8 *)&netRAM[(addr & 0x1FFFF)/2] = data;
        break;

      default:
        printf("W8 ATTENTION OUT OF RANGE\n");
        break;
      }

      break;
    }
    else if (m_stepping > 0x15 || SCSI.GetBaseAddress() != 0xC0) break;
  case 0xF9:
  case 0xC1:
    SCSI.WriteRegister(addr&0xFF,data);
    break;

  // Unknown:
  default:
  Unknown8:
    //printf("CMODEL3 : unknown W8 : %x\n", addr >> 24); // harleyb unknown 0xF1
    DebugLog("PC=%08X\twrite8 : %08X=%02X\n", ppc_get_pc(), addr, data);
    break;
  }
}

void CModel3::Write16(UINT32 addr, UINT16 data)
{
  if ((addr&1))
  {
    Write8(addr+0,data>>8);
    Write8(addr+1,data&0xFF);
    return;
  }

  // RAM (most frequently accessed)
  if (addr < 0x00800000)
  {
    if (addr + 2 > m_watchStart && addr < m_watchEnd)
      WLOG("[WATCH] frame=%u w16 %08X=%04X PC=%08X LR=%08X call=%08X(%08X,%08X,%08X,%08X)\n", m_testFrame, addr, data, ppc_get_pc(), ppc_get_lr(),
        g_ppcTraceCallInfo[0], g_ppcTraceCallInfo[1], g_ppcTraceCallInfo[2], g_ppcTraceCallInfo[3], g_ppcTraceCallInfo[4]);
    /* Live player-HP probe (StateTrace=1): log every change of a player's HP (rob+0x3F02, slots 0-7),
       so we can see which players enemies actually damage (exit dumps are unreliable). */
    if (g_ppcStateTrace && (addr & 0x3FFF) == 0x3F02 && addr >= 0x1A0000 && addr < 0x1C0000)
    {
      static FILE *hf = NULL;
      UINT16 old = *(UINT16 *) &ram[addr^2];
      if (old != (UINT16) data)
      {
        if (!hf) hf = fopen("hp.txt", "w");
        if (hf)
        {
          fprintf(hf, "f=%u slot=%d hp=%d->%d PC=%08X LR=%08X\n", m_testFrame, (int)((addr - 0x1A0000) / 0x4000),
                  (int)(INT16) old, (int)(INT16) data, ppc_get_pc(), ppc_get_lr());
          fflush(hf);
        }
      }
    }
    TASKLOG(addr, 1, 16);
    { int _s = TASK_SHADOW_OFF(addr); if (_s >= 0) { *(UINT16 *) &g_taskShadow[_s^2] = data; return; } }
    *(UINT16 *) &ram[addr^2] = data;
    return;
  }

  // Other
  switch ((addr>>24))
  {
  // Various
  case 0xF0:
  case 0xFE:  // mirror

    switch ((addr>>16)&0xFF)
    {
    // Sound Board
    case 0x08:
      //printf("%08X=%04X\n", addr, data);
      break;

    // Backup RAM
    case 0x0C:
    case 0x0D:
      *(UINT16 *) &backupRAM[(addr&0x1FFFF)^2] = data;
      break;

    // MPC105
    case 0xC0:  // F0C00CF8
      if (PCIBridge.GetModel() == 0x105)
        PCIBridge.WritePCIConfigData(16,addr&2,data);
      break;

    // Unknown
    default:
      //printf("CMODEL3 : unknown W16 mirror : %x\n", addr >> 16);
      break;
    }

    DebugLog("PC=%08X\twrite16 : %08X=%04X\n", ppc_get_pc(), addr, data);
    break;

  // Tile generator
  case 0xF1:
    if (addr < 0xF1120000)
    {
      // Tile generator accesses its RAM as little endian, no adjustment needed here
      TileGen.WriteRAM16(addr&0x1FFFFF, FLIPENDIAN16(data));
    }
    goto Unknown16;

  // MPC105
  case 0xF8:
    if (PCIBridge.GetModel() == 0x105)
    {
      // Write in big endian order, like a real PowerPC
      PCIBridge.WriteRegister((addr&0xFF)+0,data>>8);
      PCIBridge.WriteRegister((addr&0xFF)+1,data&0xFF);
    }
    break;

  case 0xC0: // skichamp only
    //printf("CModel 3 : write16 %x<-%x\n", addr, data);

    switch ((addr & 0x3ffff) >> 16)
    {
    case 0:
      NetBoard->WriteCommRAM16((addr & 0xFFFF) ^ 2, FLIPENDIAN16(data));
      break;

    default:
      //printf("CMODEL3 : unknown W16 : %x\n", addr >> 24);
      break;
    }

    break;

  // Unknown
  default:
  Unknown16:
    DebugLog("PC=%08X\twrite16: %08X=%04X\n", ppc_get_pc(), addr, data);
    break;
  }
}

void CModel3::Write32(UINT32 addr, UINT32 data)
{
  if ((addr&3))
  {
    Write16(addr+0,data>>16);
    Write16(addr+2,data);
    return;
  }

  // RAM (most frequently accessed)
  if (addr<0x00800000)
  {
    if (addr + 4 > m_watchStart && addr < m_watchEnd)
      WLOG("[WATCH] frame=%u sync=%u w32 %08X=%08X PC=%08X LR=%08X call=%08X(%08X,%08X,%08X,%08X)\n", m_testFrame, *(UINT32 *) &ram[0x1000A8], addr, data, ppc_get_pc(), ppc_get_lr(),
        g_ppcTraceCallInfo[0], g_ppcTraceCallInfo[1], g_ppcTraceCallInfo[2], g_ppcTraceCallInfo[3], g_ppcTraceCallInfo[4]);
    /* Live enemy-target probe: log every write of an enemy's target field (rob+0x32E4), so we can
       see which players enemies actually lock onto during play (exit dumps are unreliable). */
    if (g_ppcStateTrace && (addr & 0x3FFF) == 0x32E4 && addr >= 0x1A0000 && addr < 0x00200000)
    {
      static FILE *tf = NULL;
      if (!tf) tf = fopen("target.txt", "w");
      if (tf)
      {
        UINT32 tgt = data & 0x7FFFFF;
        int eslot = (int)((addr - 0x1A0000) / 0x4000);
        int tslot = (tgt >= 0x1A0000 && tgt < 0x00200000) ? (int)((tgt - 0x1A0000) / 0x4000) : -1;
        fprintf(tf, "f=%u enemy_slot=%d target=%08X tslot=%d\n", m_testFrame, eslot, data, tslot);
        fflush(tf);
      }
    }
    if (g_ppcStateTrace && (addr & 0x3FFF) == 0x3F00 && addr >= 0x1A0000 && addr < 0x1C0000 &&
        ((*(UINT32 *) &ram[addr]) & 0xFFFF) != (data & 0xFFFF))
    {
      static FILE *hf32 = NULL;
      if (!hf32) hf32 = fopen("hp32.txt", "w");
      if (hf32)
      {
        fprintf(hf32, "f=%u slot=%d hp=%d->%d PC=%08X\n", m_testFrame, (int)((addr - 0x1A0000) / 0x4000),
                (int)(INT16)((*(UINT32 *) &ram[addr]) & 0xFFFF), (int)(INT16)(data & 0xFFFF), ppc_get_pc());
        fflush(hf32);
      }
    }
    TASKLOG(addr, 1, 32);
    { int _s = TASK_SHADOW_OFF(addr); if (_s >= 0) { *(UINT32 *) &g_taskShadow[_s] = data; return; } }
    *(UINT32 *) &ram[addr] = data;
    return;
  }

  // Other
  switch ((addr>>24))
  {
  // Real3D trigger
  case 0x88:  // 88000000
    GPU.Flush();
    break;

  // Real3D low culling RAM
  case 0x8C:  // 8C000000-8C400000
    GPU.WriteLowCullingRAM(addr&0x3FFFFF,FLIPENDIAN32(data));
    break;

  // Real3D high culling RAM
  case 0x8E:  // 8E000000-8E100000
    GPU.WriteHighCullingRAM(addr&0xFFFFF,FLIPENDIAN32(data));
    break;

  // Real3D texture port
  case 0x90:  // 90000000-90??????
    GPU.WriteTexturePort(addr&0xFF,FLIPENDIAN32(data));
    break;

  // Real3D texture FIFO
  case 0x94:  // 94000000-94100000
    GPU.WriteTextureFIFO(FLIPENDIAN32(data));
    break;

  // Real3D polygon RAM
  case 0x98:  // 98000000-98400000
    GPU.WritePolygonRAM(addr&0x3FFFFF,FLIPENDIAN32(data));
    break;

  // Real3D configuration registers
  case 0x9C:  // 9Cxxxxxx
    GPU.WriteConfigurationRegister(addr, FLIPENDIAN32(data));
    break;

  // Real3D DMA
  case 0xC2:  // C2000000-C2000100
    GPU.WriteDMARegister32(addr&0xFF,FLIPENDIAN32(data));
    break;

  // Various
  case 0xF0:
  case 0xFE:  // mirror

    switch ((addr>>16)&0xFF)
    {
    // Inputs
    case 0x04:
      WriteInputs((addr&0x3F)+0,(data>>24)&0xFF);
      WriteInputs((addr&0x3F)+1,(data>>16)&0xFF);
      WriteInputs((addr&0x3F)+2,(data>>8)&0xFF);
      WriteInputs((addr&0x3F)+3,(data>>0)&0xFF);
      break;

    // Sound Board
    case 0x08:
      //printf("PPC: %08X=%08X\n", addr, data);
      break;

    // Backup RAM
    case 0x0C:
    case 0x0D:
      *(UINT32 *) &backupRAM[(addr&0x1FFFF)] = data;
      break;

    // MPC105
    case 0x80:  // F0800CF8
      if (PCIBridge.GetModel() == 0x105)
        PCIBridge.WritePCIConfigAddress(data);
      break;

    // MPC105/106
    case 0xC0: case 0xD0: case 0xE0:
    case 0xC1: case 0xD1: case 0xE1:
    case 0xC2: case 0xD2: case 0xE2:
    case 0xC3: case 0xD3: case 0xE3:
    case 0xC4: case 0xD4: case 0xE4:
    case 0xC5: case 0xD5: case 0xE5:
    case 0xC6: case 0xD6: case 0xE6:
    case 0xC7: case 0xD7: case 0xE7:
    case 0xC8: case 0xD8: case 0xE8:
    case 0xC9: case 0xD9: case 0xE9:
    case 0xCA: case 0xDA: case 0xEA:
    case 0xCB: case 0xDB: case 0xEB:
    case 0xCC: case 0xDC: case 0xEC:
    case 0xCD: case 0xDD: case 0xED:
    case 0xCE: case 0xDE: case 0xEE:
    case 0xCF: case 0xDF: case 0xEF:
      if ((PCIBridge.GetModel() == 0x105) && (addr>=0xF0C00CF8) && (addr<0xF0C00D00))    // MPC105
        PCIBridge.WritePCIConfigData(32,0,data);
      else if ((PCIBridge.GetModel() == 0x106) && (addr>=0xFEC00000) && (addr<0xFEE00000)) // MPC106
        PCIBridge.WritePCIConfigAddress(data);
      else if ((PCIBridge.GetModel() == 0x106) && (addr>=0xFEE00000) && (addr<0xFEF00000)) // MPC106
        PCIBridge.WritePCIConfigData(32,0,data);
      break;

    // System registers
    case 0x10:
      WriteSystemRegister((addr&0x3F)+0,(data>>24)&0xFF);
      WriteSystemRegister((addr&0x3F)+1,(data>>16)&0xFF);
      WriteSystemRegister((addr&0x3F)+2,(data>>8)&0xFF);
      WriteSystemRegister((addr&0x3F)+3,(data>>0)&0xFF);
      break;

    // RTC
    case 0x14:
      RTC.WriteRegister((addr>>2)&0xF,data);
      break;

    // Security board RAM
    case 0x18:
    case 0x19:
      *(UINT32 *) &securityRAM[(addr&0x1FFFF)] = data;
      break;

    // Security board registers
    case 0x1A:
      WriteSecurity(addr&0x3F,data);
      break;

    // Unknown
    default:
      //printf("CMODEL3 : unknown W32 mirror : %x\n", addr >> 16);
      break;
    }

    DebugLog("PC=%08X\twrite32: %08X=%08X\n", ppc_get_pc(), addr, data);
    break;

  // Tile generator
  case 0xF1:
    if (addr < 0xF1120000)
    {
      // Tile generator accesses its RAM as little endian, must flip for big endian PowerPC
      data = FLIPENDIAN32(data);
      TileGen.WriteRAM32(addr&0x1FFFFF,data);
      break;
    }
    else if ((addr>=0xF1180000) && (addr<0xF1180100))
    {
      TileGen.WriteRegister(addr&0xFF,FLIPENDIAN32(data));
      if (addr == 0xf118000c) {
        GPU.TilegenDrawFrame(FLIPENDIAN32(data));
      }
      break;
    }

    goto Unknown32;

  // MPC105
  case 0xF8:  // F8FFF000-F8FFF0FF
    if (PCIBridge.GetModel() == 0x105)
    {
      // Write in big endian order, like a real PowerPC
      PCIBridge.WriteRegister((addr&0xFF)+0,(data>>24)&0xFF);
      PCIBridge.WriteRegister((addr&0xFF)+1,(data>>16)&0xFF);
      PCIBridge.WriteRegister((addr&0xFF)+2,(data>>8)&0xFF);
      PCIBridge.WriteRegister((addr&0xFF)+3,data&0xFF);
    }
    break;

  // 53C810 SCSI
  case 0xC0:  // step 1.x only
    if (m_runNetBoard)
    {
      UINT32 temp;
      switch ((addr & 0x3ffff) >> 16)
      {
      case 0:
        temp = FLIPENDIAN32(data);
        NetBoard->WriteCommRAM32(addr & 0xFFFF, (temp << 16) | (temp >> 16));
        break;

      case 1: // ioreg 32bits access to 16bits range
        if (addr > 0xc00101ff)
        {
          printf("W32 ATTENTION OUT OF RANGE\n");
        }

        NetBoard->WriteIORegister((addr & 0x1FF) / 2, FLIPENDIAN16(data >> 16));
        break;

      case 2:
      case 3:
        *(UINT16 *)&netRAM[((addr & 0x1FFFF) / 2)] = FLIPENDIAN16(data >> 16);
        break;

      default:
        printf("W32 ATTENTION OUT OF RANGE\n");
        break;
      }

      break;
    }
    else if (m_stepping > 0x15 || SCSI.GetBaseAddress() != 0xC0) break;
  case 0xF9:
  case 0xC1:
    SCSI.WriteRegister((addr&0xFF)+0,(data>>24)&0xFF);
    SCSI.WriteRegister((addr&0xFF)+1,(data>>16)&0xFF);
    SCSI.WriteRegister((addr&0xFF)+2,(data>>8)&0xFF);
    SCSI.WriteRegister((addr&0xFF)+3,data&0xFF);
    break;

  // Unknown
  default:
  Unknown32:
    if (m_runNetBoard) printf("CMODEL3 : unknown W32 : %x (%x) data=%d\n", addr,addr >> 24,data);
    //printf("PC=%08X\twrite32: %08X=%08X\n", ppc_get_pc(), addr, data);
    DebugLog("PC=%08X\twrite32: %08X=%08X\n", ppc_get_pc(), addr, data);
    break;
  }
}

void CModel3::Write64(UINT32 addr, UINT64 data)
{
    //printf("write64 %x <- %x\n", addr, data);
    Write32(addr+0, (UINT32) (data>>32));
    Write32(addr+4, (UINT32) data);
}


/******************************************************************************
 Emulation and Interface Functions
******************************************************************************/

void CModel3::SaveState(CBlockFile *SaveState)
{
  // Write Model 3 state
  SaveState->NewBlock("Model 3", __FILE__);
  SaveState->Write(&inputBank, sizeof(inputBank));
  SaveState->Write(&serialFIFO1, sizeof(serialFIFO1));
  SaveState->Write(&serialFIFO2, sizeof(serialFIFO2));
  SaveState->Write(&gunReg, sizeof(gunReg));
  SaveState->Write(&adcChannel, sizeof(adcChannel));
  SaveState->Write(&cromBankReg, sizeof(cromBankReg));
  SaveState->Write(&securityPtr, sizeof(securityPtr));
  SaveState->Write(ram, 0x800000);
  SaveState->Write(backupRAM, 0x20000);
  SaveState->Write(securityRAM, 0x20000);
  SaveState->Write(&midiCtrlPort, sizeof(midiCtrlPort));
  int32_t securityFirstRead = m_securityFirstRead;
  SaveState->Write(&securityFirstRead, sizeof(securityFirstRead));

  // All devices...
  ppc_save_state(SaveState);
  IRQ.SaveState(SaveState);
  PCIBridge.SaveState(SaveState);
  SCSI.SaveState(SaveState);
  EEPROM.SaveState(SaveState);
  TileGen.SaveState(SaveState);
  GPU.SaveState(SaveState);
  SoundBoard.SaveState(SaveState);  // also saves DSB state
  DriveBoard->SaveState(SaveState);
  m_cryptoDevice.SaveState(SaveState);
  m_jtag.SaveState(SaveState);
  // 11/23 main6: the link (comm RAM, counter, bank) - own block, SimNetBoard.cpp
  if (NetBoard) NetBoard->SaveState(SaveState);

  // 11/14 Spikeout 8 players: data the emulator keeps outside RAM (the task arrays' slots 4-7). Own block, so
  // states without it still load.
  if (g_spikeofe8task)
  {
    SaveState->NewBlock("Spikeout8", __FILE__);
    UINT32 ver = 1;
    SaveState->Write(&ver, sizeof(ver));
    SaveState->Write(g_taskShadow, sizeof(g_taskShadow));
  }
}

void CModel3::LoadState(CBlockFile *SaveState)
{
  // Load Model 3 state
  if (Result::OKAY != SaveState->FindBlock("Model 3"))
  {
    ErrorLog("Unable to load Model 3 core state. Save state file is corrupt.");
    return;
  }

  SaveState->Read(&inputBank, sizeof(inputBank));
  SaveState->Read(&serialFIFO1, sizeof(serialFIFO1));
  SaveState->Read(&serialFIFO2, sizeof(serialFIFO2));
  SaveState->Read(&gunReg, sizeof(gunReg));
  SaveState->Read(&adcChannel, sizeof(adcChannel));
  SaveState->Read(&cromBankReg, sizeof(cromBankReg));
  SetCROMBank(cromBankReg); // update CROM bank
  SaveState->Read(&securityPtr, sizeof(securityPtr));
  SaveState->Read(ram, 0x800000);
  SaveState->Read(backupRAM, 0x20000);
  SaveState->Read(securityRAM, 0x20000);
  SaveState->Read(&midiCtrlPort, sizeof(midiCtrlPort));
  int32_t securityFirstRead;
  SaveState->Read(&securityFirstRead, sizeof(securityFirstRead));
  m_securityFirstRead = securityFirstRead != 0;

  // All devices...
  GPU.LoadState(SaveState);
  TileGen.LoadState(SaveState);
  EEPROM.LoadState(SaveState);
  SCSI.LoadState(SaveState);
  PCIBridge.LoadState(SaveState);
  IRQ.LoadState(SaveState);
  ppc_load_state(SaveState);
  SoundBoard.LoadState(SaveState);
  DriveBoard->LoadState(SaveState);
  m_cryptoDevice.LoadState(SaveState);
  m_jtag.LoadState(SaveState);
  // 11/23 main6: the link (only when the save is of the same link as now; SimNetBoard.cpp)
  if (NetBoard) NetBoard->LoadState(SaveState);

  // 11/14 Spikeout 8 players: the task arrays' slots 4-7 (kept outside RAM) - cleared when the state has none.
  if (g_spikeofe8task)
  {
    UINT32 ver = 0;
    if (Result::OKAY == SaveState->FindBlock("Spikeout8"))
    {
      SaveState->Read(&ver, sizeof(ver));
      if (ver == 1) SaveState->Read(g_taskShadow, sizeof(g_taskShadow));
    }
    if (ver != 1)
    {
      memset(g_taskShadow, 0, sizeof(g_taskShadow));
      InfoLog("Spikeout8: this save state has no 8-player data (older exe); slots 4-7 task data cleared.");
    }
  }
  // ...and this exe's patches back into RAM: where RAM holds the original word (an older exe without that patch),
  // the new one goes in; where it holds neither (an older exe's other version of it, or data the game has changed
  // since), it is left alone and counted.
  if (!g_spk8Applied.empty())
  {
    unsigned put = 0, other = 0;
    for (const Spikeout8LinkPatch &p : g_spk8Applied)
    {
      if (p.addr + 4 > 0x800000) continue;
      UINT32 *w = (UINT32 *) &ram[p.addr];
      if (*w == p.newValue) continue;
      if (*w == p.oldValue) { *w = p.newValue; put++; }
      else other++;
    }
    if (put || other)
    {
      InfoLog("Spikeout8: after the state load, %u patch(es) put back into RAM, %u word(s) left as they were.", put, other);
    }
  }
}

void CModel3::SaveNVRAM(CBlockFile *NVRAM)
{
  // Load EEPROM
  EEPROM.SaveState(NVRAM);

  // Save backup RAM
  NVRAM->NewBlock("Backup RAM", __FILE__);
  NVRAM->Write(backupRAM, 0x20000);
}

void CModel3::LoadNVRAM(CBlockFile *NVRAM)
{
  // Load EEPROM
  EEPROM.LoadState(NVRAM);

  // Load backup RAM
  if (Result::OKAY != NVRAM->FindBlock("Backup RAM"))
  {
    ErrorLog("Unable to load Model 3 backup RAM. NVRAM file is corrupt.");
    return;
  }
  NVRAM->Read(backupRAM, 0x20000);
}

void CModel3::ClearNVRAM(void)
{
  memset(backupRAM, 0, 0x20000);
  EEPROM.Clear();
}

void CModel3::RunFrame(void)
{
  UINT32 start = CThread::GetTicks();

  // See if currently running multi-threaded
  if (m_multiThreaded)
  {
    // If so, check all threads are up and running
    if (!StartThreads())
      goto ThreadError;

    // Wake threads for PPC main board (if multi-threading GPU), sound board (if sync'd) and drive board (if attached) so they can process a frame
    if ((m_gpuMultiThreaded       && !ppcBrdThreadSync->Post()) ||
        (syncSndBrdThread         && !sndBrdThreadSync->Post()) ||
        (DriveBoard->IsAttached()  && !drvBrdThreadSync->Post()))
      goto ThreadError;

    // If not multi-threading GPU, then run PPC main board for a frame and sync GPUs now in this thread
    if (!m_gpuMultiThreaded)
    {
      RunMainBoardFrame();
      SyncGPUs();
    }

    // Render frame
    RenderFrame();

    // Enter notify wait critical section
    if (!notifyLock->Lock())
      goto ThreadError;

    // Wait for PPC main board, sound board and drive board threads to finish their work (if they are running and haven't finished already)
    while ((m_gpuMultiThreaded      && !ppcBrdThreadDone) ||
           (syncSndBrdThread        && !sndBrdThreadDone) ||
           (DriveBoard->IsAttached() && !drvBrdThreadDone))
    {
      if (!notifySync->Wait(notifyLock))
        goto ThreadError;
    }
    ppcBrdThreadDone = false;
    sndBrdThreadDone = false;
    drvBrdThreadDone = false;

    // Leave notify wait critical section
    if (!notifyLock->Unlock())
      goto ThreadError;

    // If multi-threading GPU, then sync GPUs last while PPC main board thread is waiting
    if (m_gpuMultiThreaded)
      SyncGPUs();

    if (NetBoard->IsRunning() && m_config["SimulateNet"].ValueAs<bool>())
        RunNetBoardFrame();
  }
  else
  {
    // If not multi-threaded, then just process and render a single frame for PPC main board, sound board and drive board in turn in this thread
    RunMainBoardFrame();
    SyncGPUs();
    RenderFrame();
    RunSoundBoardFrame();
    if (DriveBoard->IsAttached())
      RunDriveBoardFrame();
    if (NetBoard->IsRunning())
      RunNetBoardFrame();
  }

  timings.frameTicks = CThread::GetTicks() - start;
  // Frame counter
  timings.frameId++;
  return;

ThreadError:
  ErrorLog("Threading error in CModel3::RunFrame: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
}

static unsigned GetCPUClockFrequencyInHz(const Game &game, Util::Config::Node &config)
{
  unsigned mhz = config["PowerPCFrequency"].ValueAsDefault<unsigned>(0);
  if (!mhz)
  {
    if (game.stepping == "1.0")
    {
      mhz = 66;
    }
    else if (game.stepping == "1.5")
    {
      mhz = 100;
    }
    else  // 2.x
    {
      mhz = 166;
    }
  }
  return mhz * 1000000;
}

// 11/20 satellite clone (Stream/Satellite.h): before every frame, make our link records match the node
// we follow. On a switch: our node number and the record pointers the link code set up at link time
// (0x6D3EC / 0x6D6E4); the received records are refreshed by sequence (+0x58), so clear those to take the
// next ones whatever they are. With SatelliteMirror, our own link block 0x140100 is the followed node's.
static void SatelliteCloneApply(UINT8 *ram)
{
	SatCloneState &c = g_satClone;
	const int n = c.numMachines, me = c.node;
	if (me < 1 || n < 2 || me > n)
		return;
	{
		// satclone.txt: our own link block vs the followed node's (state +4, flags +8, mask +0x28, seq +0x58),
		// every change - the clone must stay in step with the node it follows
		static FILE *f = fopen("satclone.txt", "w");
		static UINT32 prev[8] = {};
		const UINT32 *own = (const UINT32 *) &ram[0x140100];
		UINT32 cur[8] = { own[1] >> 24, own[2] & 0xFFFF, own[0x28 / 4], 0,
		                  c.havePacket ? c.packet[1] >> 24 : 0, c.havePacket ? c.packet[2] & 0xFFFF : 0, c.havePacket ? c.packet[0x28 / 4] : 0, (UINT32) me };
		if (f && memcmp(cur, prev, sizeof(cur)) != 0)
		{
			fprintf(f, "f=%u node=%u area=%u c0=%08X c1C=%08X/%08X %08X | ours st%u f%04X m%08X seq%08X | node st%u f%04X m%08X seq%08X\n", g_ppcTraceFrame, cur[7],
			        (unsigned) ram[0x10402D ^ 3], own[0], own[0x1C / 4], c.havePacket ? c.packet[0] : 0, c.havePacket ? c.packet[0x1C / 4] : 0, cur[0], cur[1], cur[2], own[0x58 / 4], cur[4], cur[5], cur[6], c.havePacket ? c.packet[0x58 / 4] : 0);
			fflush(f);
			memcpy(prev, cur, sizeof(cur));
		}
	}
	if (c.switched && ram[0x10065A ^ 3] == n)
	{
		c.switched = false;
		auto rec = [&](int node) { return 0x150400u + (UINT32) (((n - 1 + me - node) % n + n) % n) * 0x300u; };
		ram[0x10065B ^ 3] = (UINT8) me;
		if (c.havePacket)                                                      // our link sequence (0x140048, +1 a frame into
			*(UINT32 *) &ram[0x140048] = c.packet[0x58 / 4];                   // block +0x58, 0x6D9DC) is the followed node's
		*(UINT32 *) &ram[0x104024] = 0x1A0000u + (UINT32) (me - 1) * 0x4000u;   // my Rob (SDA -0x3FDC, 0x11C3C / 0x513D4)
		*(UINT32 *) &ram[0x106008] = 0x106DFEu + (UINT32) me * 2u;             // SDA -0x1FF8 (0x11C64)
		UINT32 cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;              // the camera follows player cam+0x4A (set from 0x10065B - 1
		if (cam >= 0x100000 && cam < 0x7FF000)                                // only at an area's start, 0x4B428)
			ram[(cam + 0x4A) ^ 3] = (UINT8) (me - 1);
		*(UINT32 *) &ram[0x140004] = rec(me);
		*(UINT32 *) &ram[0x140008] = rec(me);
		*(UINT32 *) &ram[0x14000C] = rec(1);
		for (int node = 1; node <= n; node++)
			*(UINT32 *) &ram[0x150060 + 4 * node] = rec(node);
		for (int i = 0; i < n; i++)
			*(UINT32 *) &ram[0x152000 + i * 0x900 + 0x58] = 0;
	}
	for (int k = 1; k <= 8; k++)
	{
		c.hp[k] = (INT16) *(const UINT16 *) &ram[(0x1A0000 + (k - 1) * 0x4000 + 0x3F02) ^ 2];
		c.robOn[k] = (*(const UINT32 *) &ram[0x1A0000 + (k - 1) * 0x4000] & 1) != 0;
	}
	// 11/21 (owner): an area / stage change (LOADING...: no player on the field, or the passage area 36) - CloneChoose
	// then follows the lowest-numbered node of the game.
	{
		bool anyOn = false;
		for (int k = 1; k <= 8; k++) anyOn = anyOn || c.robOn[k];
		c.loadingNow = ram[0x140104 ^ 3] == 4 && (!anyOn || ram[0x10402D ^ 3] == SPIKEOUT_AREA_GATE_PASSAGE);
	}
	if ((c.mirror || c.mirrorOnce) && c.havePacket)
	{
		c.mirrorOnce = false;
		memcpy(&ram[0x140100], c.packet, 0x900);
	}
	c.inpIdx[0] = c.inpIdx[1] = c.inpIdx[2] = 0;
}

void CModel3::RunMainBoardFrame(void)
{
	UINT32 start = CThread::GetTicks();
	if (g_satClone.on) SatelliteCloneApply(ram);
	if (g_satView.on) g_satView.inGame = ram[0x140104 ^ 3] == 4;
	if (g_satView.on && g_satView.inGame && m_config["SatelliteFontDump"].ValueAsDefault<bool>(false))
	{
		// 11/20 debug: the tile generator's VRAM / registers once, 20 s into a game (tilegen_dump.bin)
		static unsigned inGameFrames = 0;
		if (++inGameFrames == 1200)
		{
			FILE *f = fopen("tilegen_dump.bin", "wb");
			if (f)
			{
				fwrite(TileGen.SatVRAM(), 1, 0x120000, f);
				fwrite(TileGen.SatRegs(), 4, 64, f);
				fclose(f);
			}
		}
	}
	g_satInRec = SatInputRecord();   // 11/20: this frame's input reads, for the satellite clone
	m_testFrame++;
	g_ppcTraceFrame = m_testFrame;
	if (g_spikeoutAIProfile) SpikeoutAIProfFrame(ram);   // 11/23 AIProfile
	if (g_spikeoutScenarioLog) SpikeoutScenarioLog(ram);
	if (g_spikeoutItemLog) SpikeoutItemLog(ram);
	SpikeoutP1StateTick(ram);   // bot cabinets mark their link record, so the bots can tell a started human (bots must not start a game on their own)
	SpikeoutSessionTick(ram);   // 11/17: the games in the link, for the lobby / Discord
	if (g_floorDump && SpikeoutFloorDumpDue(ram))
	{
		// 11/13 FloorDump: every cabinet writes the same link-synced moment (sync counter 0x1000A8)
		UINT32 sync = *(const UINT32 *) &ram[0x1000A8];
		char path[64]; snprintf(path, sizeof(path), "floordump_%u.bin", g_floorDumpTarget);
		FILE *f = fopen(path, "wb");
		if (f)
		{
			auto sec = [f](const char *name, const void *p, uint32_t len) {
				char n[8] = {}; strncpy(n, name, 8); fwrite(n, 1, 8, f); fwrite(&len, 4, 1, f); fwrite(p, 1, len, f);
			};
			char meta[256];
			int ml = snprintf(meta, sizeof(meta), "node=%u frame=%u sync=%u target=%u localRob=%08X mask=%08X\n",
				(unsigned) ram[0x10065B ^ 3], m_testFrame, sync, g_floorDumpTarget, *(const UINT32 *) &ram[0x104024], *(const UINT32 *) &ram[0x14002C]);
			fwrite("FDMP0001", 1, 8, f);
			sec("META", meta, (uint32_t) ml);
			sec("RAM", ram, 0x800000);				// host order: word access as is, bytes at a^3
			sec("TASKSHD", g_taskShadow, sizeof(g_taskShadow));
			GPU.DumpRaw(f);
			fclose(f);
		}
	}
	g_spikeoutCrom = crom; g_spikeoutCromBank = cromBank; g_spikeoutRam = ram;   // for the gate readers (01 runs no AI)
	if (g_spikeoutGateLog) { SpikeoutGateLog(ram); SpikeoutGateDump(ram); SpikeoutGateWorkDump(ram); }   // diagnostic: area-clear flags + the whole gate work area
	if (g_spikeoutCpicLog) SpikeoutCpicLog(ram, m_testFrame);   // 11/15: texture cache use / "[CPM]: No idle cpic." -> cpiclog.txt
	for (const auto &e : m_testPokes)
	{
		if (m_testFrame >= e.startFrame && m_testFrame <= e.endFrame)
			ram[e.reg ^ 3] = e.mask;
	}
	if (g_spikeoutP1Invincible && !SpikeoutAttract(ram) && (*(const UINT32 *) &ram[0x1A0000] & 1))
		*(UINT16 *) &ram[(0x1A0000 + 0x3F22) ^ 2] = 0xD8;   // the game's own spawn invincibility (0x1E85C), ticks down at 0x1E384
	// 11/23 (owner): SpikeJr=1 - a player who picked Spike becomes Spike Jr. Same as the owner's cheat (Rob +0x3F00
	// word 0x000001C2 Spike -> 0x040001C2 Jr), but only the character byte is changed, not the HP. Every live player
	// slot 0-7 (Rob 0x1A0000 + slot x 0x4000) with character 0 gets 4, once a frame, outside the demo. Every cabinet
	// sets all 8 slots in its own RAM, so it MUST be on on all cabinets (Jr moves differently: inputs would desync).
	if (g_spikeoutSpikeJr && !SpikeoutAttract(ram))
	{
		for (unsigned s = 0; s < 8; s++)
		{
			const UINT32 r = 0x1A0000 + s * 0x4000;
			if ((*(const UINT32 *) &ram[r] & 1) && ram[(r + 0x3F00) ^ 3] == 0)
				ram[(r + 0x3F00) ^ 3] = 4;
		}
	}
	// 11/26 (owner): PlayerJumpMul=X (1.0-6.0, 11/26 owner: was 1.0-2.0) - the players jump X times as high. A jump is velocity-driven (DownLog,
	// motion 622: Vy +0x20 0.131 on the first frame, - 0.005 a frame, y up 1.77), so on the frame a player's mot_kind
	// has jump 0x00080000 and Vy turns > 0 (the take-off), Vy is multiplied by sqrt(X) once (height ~ Vy^2). "Was going up" is kept in
	// the game's RAM (0xDC0 + slot, in the unused trace vector), so a save state / netlink state carries it and every
	// cabinet does it on the same frame. All player slots (the same on every cabinet): MUST be the same everywhere.
	if (g_spikeoutJumpMul != 1.0f && !SpikeoutAttract(ram))
	{
		const unsigned pc = ram[0x10065A ^ 3];
		const unsigned np = (pc >= 1 && pc <= 8) ? pc : 4;
		for (unsigned s = 0; s < np; s++)
		{
			const UINT32 r = 0x1A0000 + s * 0x4000;
			// the take-off: in a jump and moving up, the first such frame (the jump bit is on already in the crouch before it,
			// motion 1063 with Vy 0, so its own edge is too early)
			float *vy = (float *) &ram[r + 0x20];
			const bool up = (*(const UINT32 *) &ram[r] & 1) && (*(const UINT32 *) &ram[r + 0x44] & 0x00080000u) && *vy > 0.0f;
			UINT8 &was = ram[(0xDC0 + s) ^ 3];
			if (up && !was) *vy *= sqrtf(g_spikeoutJumpMul);
			was = up ? 1 : 0;
		}
	}
	// Per-player timers (time-out = game over): 0x1062B0 (4 players) / 0x106E00 with the 8-player patch (T8 in
	// phase2.py), ticked once a frame at 0x5B604, shown as value>>6 seconds, 0x18FF = 99 (0x5CB64), link-synced.
	// The FixTimer ini switch was removed on 11/16. 11/16 later (owner): with 5 or more players in the game the timers
	// are held at 99; with 4 or fewer they count down as in the original. The count is the in-game mask 0x140128 (the
	// same word the 5-8 player heal / damage caves count; the same on every node, so all nodes hold or run together).
	// Zero means "not started", so it is left alone.
	// 11/17 (owner): TimerStop=1 in the ini holds them with 4 or fewer players as well (someone in the game). The timers
	// are link-synced, so it must be the same on every cabinet.
	if (g_spikeofe8task)
	{
		UINT32 inGame = *(const UINT32 *) &ram[0x140128];
		// 11/21 (owner): the satellite clone always holds them (it timed out to a game over; it sends nothing that
		// counts for the others, so this does not touch the link). Timers that are 0 (not started) stay 0.
		if (__builtin_popcount(inGame) >= 5 || (g_spikeoutTimerStop && inGame != 0) || g_satClone.on)
		{
			for (unsigned i = 0; i < 8; i++)
			{
				UINT16 *t = (UINT16 *) &ram[(0x106E00 + i * 2) ^ 2];
				if (*t) *t = 0x087F;   // 11/25 (owner): held at 33 (33 << 6 | 0x3F; was 0x18FF = 99)
			}
		}
	}

	/* Per-player stat dump (StateTrace=1): every 60 frames, dump each slot's Rob combat-stat
	   region (0x1A0000+slot*0x4000 + 0x3F00..0x3F80) and relocated save record
	   (0x160008+slot*0x28). Diffing slots 0-3 vs 4-7 after an all-8 session reveals which field
	   holds the special-attack stock, defeated-enemy count and score so slots 4-7 can be fixed. */
	if (g_ppcStateTrace)
	{
		static FILE *sdf = NULL;
		if (!sdf) sdf = fopen("statdump.txt", "w");
		if (sdf && (m_testFrame % 60) == 0)
		{
			for (int s = 0; s < 8; s++)
			{
				UINT32 rob = 0x1A0000 + s * 0x4000;
				UINT32 rec = 0x160008 + s * 0x28;
				fprintf(sdf, "f=%u slot=%d rob3F00=", m_testFrame, s);
				for (int o = 0x3F00; o < 0x3F80; o++) fprintf(sdf, "%02X", ram[(rob + o) ^ 3]);
				fprintf(sdf, " rec=");
				for (int o = 0; o < 0x28; o++) fprintf(sdf, "%02X", ram[(rec + o) ^ 3]);
				fprintf(sdf, "\n");
			}
			fflush(sdf);
		}
	}


	/*
   * Compute display timings. Refresh rate is 57.524160 Hz and we assume frame timing is the same as System 24:
   *
   * - 25 scanlines from /VSYNC high to /BLANK high (top border)
   * - 384 scanlines from /BLANK high to /BLANK low (active display)
   * - 11 scanlines from /BLANK low to /VSYNC low (bottom border)
   * - 4 scanlines from /VSYNC low to /VSYNC high (vertical sync. pulse)
	 *
   * 424 lines total: 384 display and 40 blanking/vsync.
	 */ 
	unsigned ppcCycles		= GetCPUClockFrequencyInHz(m_game, m_config);
	unsigned frameCycles	= (unsigned)((float)ppcCycles / 57.524160f);
	unsigned lineCycles     = frameCycles / 424;
    unsigned vBlankCycles   = lineCycles * 40;

	// Games will start writing a new frame after the ping-pong buffers have been flipped, which is indicated by the
	// ping-pong status bit. The timing of ping-pong flip is determined by the value of tilegen register 0x08, which
	// is the number of active video lines to display before ping-pong flip occurs. Most games set it to 238 or 239
	// so that ping-pong flip occurs 66% of the frame time after IRQ2, though a few games set it to a higher value.

	// Scale PPC timer ratio according to speed at which the PowerPC is being emulated so that the observed running frequency of the PPC timer
	// registers is more or less correct.  This is needed to get the Virtua Striker 2 series of games running at the right speed (they are
	// too slow otherwise).  Other games appear to not be affected by this ratio so much as their running speed depends more on the timing of
	// the Real3D status bit below.
	ppc_set_timer_ratio(ppc_get_bus_freq_multipler() * 2 * ppcCycles / ppc_get_cycles_per_sec());

	// VBlank
	if (gpusReady)
	{
        TileGen.BeginVBlank();
		GPU.BeginVBlank();
        
		// keep running cycles until IRQ2 is acknowledged
		// Ski Champ can hang if we check the MIDI control port too early
		// and miss MIDI interrupts pending before the next IRQ2
		while (IRQ.ReadIRQEnable() & 0x2 && IRQ.ReadIRQState() & 0x2 && vBlankCycles > 1000)
		{
			ppc_execute(1000);
            vBlankCycles -= 1000;
		}

		/*
		* Sound:
		*
		* Bit 0x20 of the MIDI control port appears to enable periodic interrupts,
		* which are used to send MIDI commands. Often games will write 0x27, send
		* a series of commands, and write 0x06 to stop. Other games, like Star
		* Wars Trilogy and Sega Rally 2, will enable interrupts at the beginning
		* by writing 0x37 and will disable/enable interrupts to control command
		* output.
		*/
		//printf("\t-- BEGIN (Ctrl=%02X, IRQEn=%02X, IRQPend=%02X) --\n", midiCtrlPort, IRQ.ReadIRQEnable()&0x40, IRQ.ReadIRQState());
		int irqCount = 0;
		while ((midiCtrlPort & 0x20))
			//while (midiCtrlPort == 0x27)  // 27 triggers IRQ sequence, 06 stops it
		{
			// Don't waste time firing MIDI interrupts if game has disabled them
			if ((IRQ.ReadIRQEnable() & 0x40) == 0)
				break;

			// Process MIDI interrupt
			IRQ.Assert(0x40);
			ppc_execute(1000); // give PowerPC time to acknowledge IR
            vBlankCycles -= 1000;

			++irqCount;
			if (irqCount > 128)
			{
				break;
			}
		}

        ppc_execute(vBlankCycles);

		IRQ.Assert(0x0D);

		// End VBlank
		GPU.EndVBlank();
		TileGen.EndVBlank();
	}

    auto pingPongFlipLine = TileGen.ReadRegister(0x08);

	// Run the PowerPC for the active display part of the frame
    for (unsigned int i = 0; i < 384; i++)
    {
        if (i == pingPongFlipLine) {
            GPU.FlipPingPongBit();
        }

        if (i == 383) {
            IRQ.Assert(0x02);       // irq2 is asserted at the start of the last line on system24 (as apposed to the end). Lost world won't work without this, the game soft locks. We assume the same here
        }

        if (!g_skipTileDraw)
          TileGen.DrawLine(i);
        ppc_execute(lineCycles);
    }

	// 11/13 IdleSkipLog=1: every 60 frames, how much of the PPC time was idle (skipped) and how many
	// frames the game itself counted as late (byte SDA -0x7ff3 = 0x10000D, +1 in 0x36F58 when the
	// frame-wait saw the counter move by 2 or more = the game's work did not fit in one frame).
	if (g_spikeoutIdleLog)
	{
		static FILE *ilf = nullptr;
		static UINT64 lastSkip = 0;
		static unsigned frames = 0, lateFrames = 0, worstPct = 100;
		static UINT8 lastLate = 0;
		static UINT64 frameStartSkip = 0;
		if (!ilf) { ilf = fopen("idlelog.txt", "w"); lastSkip = frameStartSkip = ppc_get_idle_skipped(); lastLate = ram[0x10000D ^ 3]; }
		UINT64 nowSkip = ppc_get_idle_skipped();
		unsigned pct = (unsigned)((nowSkip - frameStartSkip) * 100 / (frameCycles ? frameCycles : 1));
		if (pct < worstPct) worstPct = pct;				// least idle frame in this window
		frameStartSkip = nowSkip;
		UINT8 late = ram[0x10000D ^ 3];
		lateFrames += (UINT8)(late - lastLate);
		lastLate = late;
		if (++frames >= 60 && ilf)
		{
			fprintf(ilf, "f=%u mhz=%u idle_avg=%u%% idle_min=%u%% late=%u\n", m_testFrame, ppcCycles / 1000000,
				(unsigned)((nowSkip - lastSkip) * 100 / ((UINT64)frameCycles * frames)), worstPct, lateFrames);
			fflush(ilf);
			lastSkip = nowSkip; frames = 0; lateFrames = 0; worstPct = 100;
		}
	}

	timings.ppcTicks = CThread::GetTicks() - start;
}

// 11/20 satellite clone: our camera (Stream/Satellite.h SatViewState). The game's view is rebuilt from its camera
// (*(0x104150): eye +0x10, looks at +0x404), ours chases the followed player from behind (Rob +0x35C 3x3 rows,
// +0x380 position); New3D puts "ours x game's^-1" in front of the 3D world. Real3D eye axes: forward, right, down.
// Called where the display list is taken over (SyncGPUs), so camera and picture are of the same frame.
// 11/22 (owner): while a boss is out, the satellite shows the boss - the first one of the boss list (the top row of the
// overlay's boss bars: enemy slots 8-19 in order, live, class Rob +0x73 >= 4, +0x3F00 != 0x26, HP > 0). -1: none.
// 11/22 (owner, 2nd): with several bosses, the next one of the list every 10 s (600 frames), from the top one; the one
// shown is gone (dead / left) -> the top one of the rest at once. Worked out once a frame (camera and HUD agree).
static int SatBossTarget(const UINT8 *ram)
{
	static int cur = -1; static UINT32 since = 0, lastF = 0xFFFFFFFFu;
	if (g_ppcTraceFrame == lastF) return cur;
	lastF = g_ppcTraceFrame;
	int list[12], n = 0;
	for (int slot = 8; slot < 20; slot++)
	{
		const UINT32 r = 0x1A0000 + (UINT32) slot * 0x4000;
		if (!(*(const UINT32 *) &ram[r] & 1) || ram[(r + 0x73) ^ 3] < 4 || ram[(r + 0x3F00) ^ 3] == 0x26) continue;
		if ((INT16) *(const UINT16 *) &ram[(r + 0x3F02) ^ 2] <= 0) continue;
		list[n++] = slot;
	}
	if (n == 0) { cur = -1; return -1; }
	int at = -1;
	for (int i = 0; i < n; i++) if (list[i] == cur) at = i;
	if (at < 0) { cur = list[0]; since = g_ppcTraceFrame; }
	else if (n > 1 && g_ppcTraceFrame - since >= 600u) { cur = list[(at + 1) % n]; since = g_ppcTraceFrame; }
	return cur;
}
float g_satCamDist = 3.5f, g_satCamHeight = 1.5f, g_satCamLook = 0.6f, g_satCamDown = 0.8f;   // 11/25: SatelliteCamDown (1.0 -> 0.8, owner: a little higher)
int g_satCamAxis = 1;   // the character faces -(row 2) (11/20: with +row 2 we saw the face)
// 11/21 (owner): keep the chase camera out of walls - "a wall in the way: go up". The game's wall list (the one the bots
// steer by: *(0x106168)+0x08, per wall +0x0C/+0x18 the ends x y z, +0x24/+0x28 the heights; +0 bit 0x80000000 = off).
// For every wall that the line from the player (Rob y + 0.6) to the wanted camera crosses on the map and that stands
// in that line's height there: raise the camera until the line passes 0.4 over the wall's top. A raise of more than
// 6 (a wall right behind the player, or a tall one): pull the camera in to just in front of that wall instead, at the
// usual height. The glide from one player to another may still go through walls (owner: fine).
static bool SatCamClearWalls(const UINT8 *ram, const float p[3], float de[3])
{
	const UINT32 wh = *(const UINT32 *) &ram[0x106168] & 0x7FFFFF;
	if (wh < 0x10000 || wh + 0x18 > 0x800000) return false;
	const UINT32 wt = *(const UINT32 *) &ram[wh + 0x08] & 0x7FFFFF;
	if (wt < 0x10000 || wt >= 0x800000) return false;
	const float ex = p[1] + 0.6f;                                  // eye of the line: the player's chest
	const float baseY = de[1];
	float needY = de[1], pullT = 2.0f;
	const float dx = de[0] - p[0], dz = de[2] - p[2];
	for (unsigned wid = 0; wid < 2048 && wt + (wid + 1) * 4 <= 0x800000; wid++)
	{
		UINT32 wp = *(const UINT32 *) &ram[wt + wid * 4];
		if (wp == 0xFFFFFFFFu) break;
		wp &= 0x7FFFFF;
		if (wp < 0x10000 || wp + 0x2C > 0x800000) break;
		if (*(const UINT32 *) &ram[wp] & 0x80000000u) continue;   // off
		const float x0 = *(const float *) &ram[wp + 0x0C], y0 = *(const float *) &ram[wp + 0x10], z0 = *(const float *) &ram[wp + 0x14];
		const float x1 = *(const float *) &ram[wp + 0x18], y1 = *(const float *) &ram[wp + 0x1C], z1 = *(const float *) &ram[wp + 0x20];
		const float h0 = *(const float *) &ram[wp + 0x24], h1 = *(const float *) &ram[wp + 0x28];
		if (!(x0 == x0) || !(z0 == z0) || !(x1 == x1) || !(z1 == z1) || !(h0 == h0) || !(h1 == h1)) continue;
		const float wx = x1 - x0, wz = z1 - z0;
		const float den = dx * wz - dz * wx;
		if (std::fabs(den) < 1e-6f) continue;
		const float rx = x0 - p[0], rz = z0 - p[2];
		const float t = (rx * wz - rz * wx) / den;                 // along player -> camera, 0..1
		const float u = (rx * dz - rz * dx) / den;                 // along the wall, 0..1
		if (t <= 0.02f || t > 1.0f || u < 0.0f || u > 1.0f) continue;
		const float bot = y0 + (y1 - y0) * u, top = bot + h0 + (h1 - h0) * u;
		const float lineY = ex + (baseY - ex) * t;
		if (lineY < bot - 0.2f || lineY > top + 0.4f) continue;   // under it (a beam overhead) or already over it
		const float want = ex + (top + 0.4f - ex) / t;            // the camera height that clears this wall
		if (want - baseY > 6.0f) { if (t < pullT) pullT = t; }
		else if (want > needY) needY = want;
	}
	if (pullT <= 1.0f)
	{
		const float k = pullT * 0.85f;                             // just in front of the nearest such wall
		de[0] = p[0] + dx * k; de[2] = p[2] + dz * k;
		return true;
	}
	de[1] = needY;
	return needY > baseY;
}
static void SatelliteCamUpdate(const UINT8 *ram)
{
	SatViewState &v = g_satView;
	v.camValid = false;
	if (!v.inGame || !v.cam)
		return;
	auto rf = [&](UINT32 a) { return *(const float *) &ram[a]; };
	auto view = [](const float pos[3], const float fwd0[3], float R[9]) -> bool
	{
		float f[3] = { fwd0[0], fwd0[1], fwd0[2] };
		float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
		if (l < 1e-4f) return false;
		for (float &x : f) x /= l;
		float r[3] = { -f[2], 0.0f, f[0] };            // right = (-fz, 0, fx) (as measured for the bots' lever)
		float lr = std::sqrt(r[0] * r[0] + r[2] * r[2]);
		if (lr < 1e-4f) return false;
		r[0] /= lr; r[2] /= lr;
		float d[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };   // down = fwd x right
		R[0] = f[0]; R[1] = f[1]; R[2] = f[2];
		R[3] = r[0]; R[4] = r[1]; R[5] = r[2];
		R[6] = d[0]; R[7] = d[1]; R[8] = d[2];
		return true;
	};
	UINT32 cam = *(const UINT32 *) &ram[0x104150] & 0x7FFFFF;
	if (cam < 0x100000 || cam > 0x7FF000)
		return;
	float ge[3] = { rf(cam + 0x10), rf(cam + 0x14), rf(cam + 0x18) };
	float gt[3] = { rf(cam + 0x404), rf(cam + 0x408), rf(cam + 0x40C) };
	float gf[3] = { gt[0] - ge[0], gt[1] - ge[1], gt[2] - ge[2] };
	float Rg[9];
	if (!view(ge, gf, Rg))
		return;

	// ours
	static float cp[3], ct[3];
	static int lastNode = -1;
	float ne[3], nt[3];
	if (v.cam == 2)
	{
		memcpy(ne, ge, sizeof(ne));
		memcpy(nt, gt, sizeof(nt));
	}
	else
	{
		int node = g_satClone.view >= 1 && g_satClone.view <= 8 ? g_satClone.view : g_satClone.node;   // 11/21: the shown player, not the clone's node
		if (node < 1 || node > 8)
			return;
		UINT32 rob = 0x1A0000 + (UINT32) (node - 1) * 0x4000;
		const int bossSlot = SatBossTarget(ram);   // 11/22 (owner): a boss out -> the camera on the (top) boss
		if (bossSlot >= 0) { rob = 0x1A0000 + (UINT32) bossSlot * 0x4000; node = 100 + bossSlot; }   // node: the glide's "who" key
		float p[3] = { rf(rob + 0x380), rf(rob + 0x384), rf(rob + 0x388) };
		float de[3], dt[3];
		if (v.cam == 3)
		{
			// 11/21 (owner): the game's own camera, moved over to the shown player - the game's camera (eye ge, looking at gt)
			// frames the clone's own player; the same eye / look-at offset is put on the shown player. Same angle and distance
			// as the game's (that of the clone's player's spot - another camera region of the map is not looked up).
			const int me = g_satClone.node;
			if (me < 1 || me > 8) return;
			const UINT32 mr = 0x1A0000 + (UINT32) (me - 1) * 0x4000;
			const float off[3] = { p[0] - rf(mr + 0x380), p[1] - rf(mr + 0x384), p[2] - rf(mr + 0x388) };
			for (int i = 0; i < 3; i++) { de[i] = ge[i] + off[i]; dt[i] = gt[i] + off[i]; }
			// 11/22 (owner): a little further back than the game's. 11/25 (owner): back along the ground only (it used to go
			// back along the look-at -> eye line, which also raised it) and no more lift (was up 0.05) - the game's height.
			const float SAT_CAM_BACK = 0.2f;   // 11/22: back 0.2 (was 0.5, 1.5)
			float bx = de[0] - dt[0], bz = de[2] - dt[2], bl = std::sqrt(bx * bx + bz * bz);
			if (bl > 1e-3f) { de[0] += bx / bl * SAT_CAM_BACK; de[2] += bz / bl * SAT_CAM_BACK; }
			de[1] -= g_satCamDown;   // 11/25 (owner, "still well above - lower"): the eye lower (ini SatelliteCamDown, 1.0); the look-at stays
		}
		else
		{
			int row = (g_satCamAxis & 2) ? 0 : 2;          // 0: +row 2, 1: -row 2, 2: +row 0, 3: -row 0
			float sg = (g_satCamAxis & 1) ? -1.0f : 1.0f;
			float h[3] = { sg * rf(rob + 0x35C + row * 12), 0.0f, sg * rf(rob + 0x35C + row * 12 + 8) };
			float lh = std::sqrt(h[0] * h[0] + h[2] * h[2]);
			if (lh < 1e-3f) return;
			h[0] /= lh; h[2] /= lh;
			de[0] = p[0] - h[0] * g_satCamDist; de[1] = p[1] + g_satCamHeight; de[2] = p[2] - h[2] * g_satCamDist;
			dt[0] = p[0] + h[0] * 1.0f; dt[1] = p[1] + g_satCamLook; dt[2] = p[2] + h[2] * 1.0f;
		}
		const bool wallAdj = SatCamClearWalls(ram, p, de);   // 11/21 (owner): not into walls - over them
		// 11/21 (owner): smoother - the camera follows slowly, and a change of the shown player is not a cut: the camera
		// glides over to the new one (eases in, then slows down as it gets near). A snap only for a jump of the same
		// player (area change / warp) or the very first frame.
		static bool glide = false, haveCam = false;
		float jump = std::fabs(de[0] - cp[0]) + std::fabs(de[1] - cp[1]) + std::fabs(de[2] - cp[2]);
		if (!haveCam || (node == lastNode && !glide && jump > 20.0f))
		{
			memcpy(cp, de, sizeof(cp));
			memcpy(ct, dt, sizeof(ct));
			haveCam = true;
		}
		static unsigned glideF = 0;
		if (node != lastNode) { glide = true; glideF = 0; lastNode = node; }
		float kp = 0.015f, kt = 0.035f;               // following (11/22 owner: half; was 0.03 / 0.07)
		if (wallAdj) kp = 0.2f;                      // a wall moved the camera: get there quickly (not left inside it)
		if (glide)
		{
			glideF++;
			const float s = glideF < 40 ? glideF / 40.0f : 1.0f;   // ease in over the first 40 frames
			kp = 0.0025f + 0.0175f * s; kt = 0.005f + 0.03f * s;   // 11/22: half (was 0.005-0.04 / 0.01-0.07)
			if (jump < 1.0f) glide = false;
		}
		for (int i = 0; i < 3; i++)
		{
			cp[i] += (de[i] - cp[i]) * kp;
			ct[i] += (dt[i] - ct[i]) * kt;
		}
		memcpy(ne, cp, sizeof(ne));
		memcpy(nt, ct, sizeof(nt));
	}
	float nf[3] = { nt[0] - ne[0], nt[1] - ne[1], nt[2] - ne[2] };
	float Rn[9];
	if (!view(ne, nf, Rn))
		return;

	// C = [Rn Rg^T, Rn (ge - ne)]
	float M[9];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			M[i * 3 + j] = Rn[i * 3 + 0] * Rg[j * 3 + 0] + Rn[i * 3 + 1] * Rg[j * 3 + 1] + Rn[i * 3 + 2] * Rg[j * 3 + 2];
	float d[3] = { ge[0] - ne[0], ge[1] - ne[1], ge[2] - ne[2] };
	float t[3];
	for (int i = 0; i < 3; i++)
		t[i] = Rn[i * 3 + 0] * d[0] + Rn[i * 3 + 1] * d[1] + Rn[i * 3 + 2] * d[2];
	float *c = v.camFix;
	for (int col = 0; col < 4; col++)
		for (int row = 0; row < 4; row++)
			c[col * 4 + row] = (row < 3 && col < 3) ? M[row * 3 + col] : (row < 3 ? t[row] : (col == 3 ? 1.0f : 0.0f));
	v.camValid = true;
}

// 11/20 (owner) satellite overlay: stage / area, the followed player's life, the bosses (Stream/Satellite.h SatHud).
// Names: the game's name table, CROM 0xFFF80A60 + (Rob +0x15C) * 4 -> a string (the HUD's enemy name, 0x1B4A0).
static void SatelliteHudUpdate(const UINT8 *ram, const UINT8 *crom)
{
	SatHud &h = g_satView.hud;
	h.valid = false;
	h.nBoss = 0;
	h.nAlly = 0;
	h.self = g_satClone.view >= 1 && g_satClone.view <= 8 ? g_satClone.view : g_satClone.node;   // 11/21: the shown player
	// 11/21 (owner): LOADING... while someone joins (a node's join flags), in the area-change passage (area 36), or while no
	// player is on the field (the stage / area change: every player's Rob +0 bit 1 goes off together).
	{
		bool anyOn = false;
		for (int s = 0; s < 8; s++) if (*(const UINT32 *) &ram[0x1A0000 + (UINT32) s * 0x4000] & 1) anyOn = true;
		h.loading = g_satClone.joining || ram[0x10402D ^ 3] == SPIKEOUT_AREA_GATE_PASSAGE || !anyOn;
	}
	int node = h.self;   // 11/21: the chase camera's player (g_satClone.view)
	if (!g_satView.inGame || node < 1 || node > 8)
		return;
	auto r16 = [&](UINT32 a) { return (int) (INT16) *(const UINT16 *) &ram[a ^ 2]; };
	h.stage = ram[0x10402C ^ 3];
	h.area = ram[0x10402D ^ 3];
	UINT32 me = 0x1A0000 + (UINT32) (node - 1) * 0x4000;
	h.hp = r16(me + 0x3F02);
	h.hpMax = r16(me + 0x3F04);
	// a name from the game's name table (CROM 0xFFF80A60, index n: 0 SPIKE 1 WHITE 2 LINDA 3 TENSHIN 4 JUNIOR ...)
	auto nameOf = [&](unsigned idx, char out[16])
	{
		out[0] = 0;
		if (!crom || idx >= 2000)
			return;
		UINT32 p = *(const UINT32 *) &crom[0x780A60 + idx * 4];
		if (p < 0xFF800000u || p >= 0xFFFFFFF0u)
			return;
		UINT32 o = p - 0xFF800000u;
		int n = 0;
		for (; n < 15; n++)
		{
			UINT8 ch = crom[(o + n) ^ 3];
			if (ch < 0x20 || ch >= 0x7F)
				break;
			out[n] = (char) ch;
		}
		out[n] = 0;
	};
	{
		unsigned ch = ram[(me + 0x3F00) ^ 3];
		nameOf(ch <= 4 ? ch : 0, h.name);
		if (ch > 4) snprintf(h.name, sizeof(h.name), "P%d", node);
	}
	for (int slot = 8; slot < 20 && h.nBoss < 4; slot++)
	{
		UINT32 r = 0x1A0000 + (UINT32) slot * 0x4000;
		if (!(*(const UINT32 *) &ram[r] & 1) || ram[(r + 0x73) ^ 3] < 4 || ram[(r + 0x3F00) ^ 3] == 0x26)
			continue;
		int hp = r16(r + 0x3F02);
		if (hp <= 0)
			continue;
		const int bi = h.nBoss;
		SatHudBoss &b = h.boss[h.nBoss++];
		b.hp = hp;
		b.hpMax = r16(r + 0x3F04);
		nameOf(*(const UINT16 *) &ram[(r + 0x15C) ^ 2], b.name);
		// 11/22 (owner): its move under the bar. A move other than 00 (none) / 01 (let go) is latched with its steps when it
		// starts (the game puts 01 there as soon as a B move's input is fed); shown until the boss is back in its stance
		// (mot_kind kamae 0x00400000 with attack 0x00800000 off), then 20 frames more.
		{
			struct Mv { int act = -1, n = 0, step = -1; uint8_t seq[16] = {}; bool on = false; UINT32 idleF = 0; };
			static Mv mv[20];
			Mv &m = mv[slot];
			const int act = r16(r + 0x22C2) & 0xFF, step = ram[(r + 0x227A) ^ 3];
			const UINT32 ek = *(const UINT32 *) &ram[r + 0x44];
			if (act > 0x01 && (act != m.act || (step != 0xFF && step < m.step) || !m.on))
			{
				if (act != m.act || !m.on || step < m.step)
				{
					m.act = act; m.n = 0; m.on = true; m.idleF = 0;
					for (int k = 0; k < 16; k++) { const uint8_t v = ram[(r + 0x227C + k) ^ 3]; if (v == 0xFF) break; m.seq[m.n++] = v; }
				}
			}
			if (m.on)
			{
				m.step = (act == m.act && step != 0xFF) ? step : -1;
				const bool idle = !(ek & 0x00800000u) && (ek & 0x00400000u) && m.step < 0;
				if (!idle) m.idleF = 0;
				else if (!m.idleF) m.idleF = g_ppcTraceFrame ? g_ppcTraceFrame : 1;
				else if (g_ppcTraceFrame - m.idleF >= 20u) { m.on = false; m.act = -1; }
			}
			h.bossMvN[bi] = m.on ? m.n : 0;
			h.bossMvAct[bi] = m.act;
			h.bossMvStep[bi] = m.step;
			h.bossMvAtk[bi] = (ek & 0x00800000u) != 0;
			for (int k = 0; k < 16; k++) h.bossMvSeq[bi][k] = m.seq[k];
		}
	}
	// 11/21 (owner): name colours and bots for the list. A player's name colour number 0-7 (fa_player +0xA0 + slot; same
	// character colours in pick order) picks the name image 0x9A-0x9D / 0xF6-0xF9 (HudColors); its palette (xBGR555 after
	// the count, pattern table of the copies at bank 13 + 0x3E0000) gives the colour: the most vivid entry (the brightest
	// for the grey / white ones). A bot cabinet carries "BOT!" at +0x2FC of its link record (copies at 0x150400 + k * 0x300).
	static uint32_t nameCol[8] = {}; static bool colDone = false;
	if (!colDone && crom)
	{
		colDone = true;
		static const UINT16 img[8] = { 0x9A, 0x9B, 0x9C, 0x9D, 0xF6, 0xF7, 0xF8, 0xF9 };
		const UINT32 BANK13 = 0x800000 + 13 * 0x800000;
		for (int k = 0; k < 8; k++)
		{
			UINT32 p = *(const UINT32 *) &crom[BANK13 + 0x3E0000 + 4 * img[k]];
			if ((p & 0xFF000000u) != 0xFF000000u || p == 0xFFFFFFFFu) continue;   // (HudColors not applied: unused table)
			const UINT32 base = BANK13 + (p & 0x7FFFFF);
			auto H = [&](UINT32 off) { return *(const UINT16 *) &crom[base + (off ^ 2)]; };
			unsigned cnt = H(0); if (cnt > 64) cnt = 0;
			float bestS = -1.0f; UINT16 best = 0;
			for (unsigned j = 0; j < cnt; j++)
			{
				UINT16 c = H(2 + 2 * j);
				if ((c & 0x7FFF) == 0) continue;
				float r = (c & 31) / 31.0f, g = ((c >> 5) & 31) / 31.0f, b = ((c >> 10) & 31) / 31.0f;
				float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
				float score = (mx - mn) * mx * 4.0f + mx;          // vivid first; among greys the brightest
				if (score > bestS) { bestS = score; best = c; }
			}
			if (bestS >= 0.0f)
				nameCol[k] = 0xFF000000u | ((UINT32) (((best >> 10) & 31) * 255 / 31) << 16) | ((UINT32) (((best >> 5) & 31) * 255 / 31) << 8) | (UINT32) ((best & 31) * 255 / 31);
		}
	}
	const UINT32 faP = *(const UINT32 *) &ram[0x104114] & 0x7FFFFF;
	auto colOf = [&](int slot) -> uint32_t { return (faP >= 0x100000 && faP < 0x7FF000) ? nameCol[ram[(faP + 0xA0 + slot) ^ 3] & 7] : 0u; };
	const int nm_ = g_satClone.numMachines;
	auto isBot = [&](int nd) -> bool {
		if (nm_ < 1 || nd < 1 || nd > nm_) return false;
		const int me_ = g_satClone.node;   // 11/22: the record layout is the clone's own node's, not the shown player's
		const UINT32 rec = 0x150400u + (UINT32) (((nm_ - 1 + me_ - nd) % nm_ + nm_) % nm_) * 0x300u;
		return *(const UINT32 *) &ram[rec + 0x2FC] == SPIKEOUT_BOT_MARK;
	};
	h.selfBot = isBot(node);
	h.selfCol = colOf(node - 1);   // 11/22 (owner): the big name in the same colour as the ally list (the game's name images)
	// 11/21 (owner): the other players (slots 0-7 but the followed one) that are in this world: Rob +0 bit 1, max life set.
	// Name: their character (Rob +0x3F00: 0 Spike, 1 White, 2 Linda, 3 Tenshin, 4 Junior = the name table's first rows).
	const int hudBoss = SatBossTarget(ram);   // 11/22: a boss shown -> the big name / bar are the boss's, every player listed
	for (int slot = 0; slot < 8 && h.nAlly < 8; slot++)
	{
		// 11/24 (owner): every player in the list, the followed one too (was: all but the followed one unless a boss was shown)
		UINT32 r = 0x1A0000 + (UINT32) slot * 0x4000;
		if (!(*(const UINT32 *) &ram[r] & 1) || r16(r + 0x3F04) <= 0)
			continue;
		h.allySlot[h.nAlly] = slot;
		h.allyCol[h.nAlly] = colOf(slot);
		h.allyBot[h.nAlly] = isBot(slot + 1);
		h.allySp[h.nAlly] = ram[(r + 0x3F25) ^ 3];   // 11/21 (owner): special stock (0-5)
		SatHudBoss &a = h.ally[h.nAlly++];
		a.hp = r16(r + 0x3F02);
		a.hpMax = r16(r + 0x3F04);
		unsigned ch = ram[(r + 0x3F00) ^ 3];
		nameOf(ch <= 4 ? ch : 0, a.name);
		if (ch > 4) snprintf(a.name, sizeof(a.name), "P%d", slot + 1);
	}
	if (hudBoss >= 0)
	{
		// 11/22 (owner): the boss is shown - its name (the game's name table, Rob +0x15C) and life in the big row, in the
		// colour of the boss bars
		const UINT32 br = 0x1A0000 + (UINT32) hudBoss * 0x4000;
		nameOf(*(const UINT16 *) &ram[(br + 0x15C) ^ 2], h.name);
		h.hp = r16(br + 0x3F02);
		h.hpMax = r16(br + 0x3F04);
		h.selfBot = false;
		h.selfCol = 0xFF2058F0u;
	}
	h.valid = true;
}

// 11/25 (owner): the enemy list (ini EnemyHud = 1): every enemy Rob on the field (Rob +0 bit 1), from the first enemy slot
// (the player count) to 19, read once a frame here (display only). Shown in RenderFrame by Render2D::RenderEnemyOverlay.
EnemyHud g_enemyHud;
// 11/26 (owner: "they bounce off the back only, not when they land on the chest / head" - to find out why): DownLog=1
// (default 0) - downlog.txt, every frame, every live enemy that is flying / hit / down / getting up (mot_kind air
// 0x08000000, yarare 0x00004000, down 0x00020000, down_pose 0x00008000, rise 0x00010000): action +0x38, motion +0x3C,
// mot_kind +0x44 / +0x48, y +0x384, Vy +0x20, the motion's root height +0x2408 (what EnemyBounceMul scales).
static bool g_downLogOn = false;
static void DownLogUpdate(const UINT8 *ram)
{
  static FILE *f = NULL; static UINT32 n = 0; static bool was[20];
  if (!f) { f = fopen("downlog.txt", "w"); if (!f) return; }
  n++;
  const unsigned pc = ram[0x10065A ^ 3];
  const unsigned es = (pc >= 1 && pc <= 8) ? pc : 4;
  for (unsigned e = es; e < 20; e++)
  {
    const UINT32 er = 0x1A0000 + e * 0x4000;
    const UINT32 k = *(const UINT32 *) &ram[er + 0x44];
    const bool on = (*(const UINT32 *) &ram[er] & 1) && (k & 0x0803C000u);
    if (on || was[e])
      fprintf(f, "n=%u E%u act=%u mot=%d mk=%08X mk2=%08X y=%.3f vy=%.3f root=%.3f hp=%d%s\n", n, e, (unsigned) ram[(er + 0x38) ^ 3],
              (int) (short) *(const UINT16 *) &ram[(er + 0x3C) ^ 2], k, *(const UINT32 *) &ram[er + 0x48],
              *(const float *) &ram[er + 0x384], *(const float *) &ram[er + 0x20], *(const float *) &ram[er + 0x2408],
              (int) (short) *(const UINT16 *) &ram[(er + 0x3F02) ^ 2], on ? "" : " (end)");
    was[e] = on;
  }
  // 11/26 (owner: player jump x1.0-2.0 - how a jump is made): the players in a jump (mot_kind jump 0x00080000), with the
  // other velocity set too (+0x84 / +0x88 / +0x8C, the one the hit physics writes)
  static bool pwas[8];
  for (unsigned p = 0; p < es && p < 8; p++)
  {
    const UINT32 pr = 0x1A0000 + p * 0x4000;
    const UINT32 k = *(const UINT32 *) &ram[pr + 0x44];
    const bool on = (*(const UINT32 *) &ram[pr] & 1) && (k & 0x08080000u);
    if (on || pwas[p])
      fprintf(f, "n=%u P%u act=%u mot=%d mk=%08X y=%.3f v=(%.3f,%.3f,%.3f) v2=(%.3f,%.3f,%.3f) root=%.3f%s\n", n, p, (unsigned) ram[(pr + 0x38) ^ 3],
              (int) (short) *(const UINT16 *) &ram[(pr + 0x3C) ^ 2], k, *(const float *) &ram[pr + 0x384],
              *(const float *) &ram[pr + 0x1C], *(const float *) &ram[pr + 0x20], *(const float *) &ram[pr + 0x24],
              *(const float *) &ram[pr + 0x84], *(const float *) &ram[pr + 0x88], *(const float *) &ram[pr + 0x8C],
              *(const float *) &ram[pr + 0x2408], on ? "" : " (end)");
    pwas[p] = on;
  }
  if ((n & 63) == 0) fflush(f);
}
bool g_enemyHudOn = false;
static void EnemyHudUpdate(const UINT8 *ram, const UINT8 *crom)
{
	EnemyHud &h = g_enemyHud;
	h.valid = false;
	h.n = 0;
	if (!SpikeoutInStage(ram))
		return;
	auto r16 = [&](UINT32 a) { return (int) (INT16) *(const UINT16 *) &ram[a ^ 2]; };
	const unsigned pcnt = ram[0x10065A ^ 3];
	const unsigned estart = (pcnt >= 1 && pcnt <= 8) ? pcnt : 4;
	// our own player: the clone shows the followed one, a cabinet its own node
	int self = g_satView.on ? (g_satClone.view >= 1 && g_satClone.view <= 8 ? g_satClone.view : g_satClone.node) : ram[0x10065B ^ 3];
	if (self < 1 || self > 8) self = 1;
	h.self = self;
	const UINT32 me = 0x1A0000 + (UINT32) (self - 1) * 0x4000;
	const float mx = *(const float *) &ram[me + 0x380], my = *(const float *) &ram[me + 0x384], mz = *(const float *) &ram[me + 0x388];
	for (unsigned slot = estart; slot < 20 && h.n < 12; slot++)
	{
		const UINT32 r = 0x1A0000 + slot * 0x4000;
		if (!(*(const UINT32 *) &ram[r] & 1))
			continue;
		EnemyHudRow &e = h.e[h.n++];
		e.slot = (int) slot;
		e.nameIdx = *(const UINT16 *) &ram[(r + 0x15C) ^ 2];
		e.name[0] = 0;
		if (crom && e.nameIdx < 2000)   // the game's name table (CROM 0xFFF80A60), as the satellite's boss names
		{
			const UINT32 p = *(const UINT32 *) &crom[0x780A60 + (UINT32) e.nameIdx * 4];
			if (p >= 0xFF800000u && p < 0xFFFFFFF0u)
			{
				const UINT32 o = p - 0xFF800000u;
				int k = 0;
				for (; k < 15; k++) { const UINT8 ch = crom[(o + k) ^ 3]; if (ch < 0x20 || ch >= 0x7F) break; e.name[k] = (char) ch; }
				e.name[k] = 0;
			}
		}
		e.hp = r16(r + 0x3F02);
		e.hpMax = r16(r + 0x3F04);
		e.obj = ram[(r + 0x72) ^ 3];
		e.cls = ram[(r + 0x73) ^ 3];
		e.chr = ram[(r + 0x3F00) ^ 3];
		e.act = *(const UINT32 *) &ram[r + 0x38];
		e.mot = r16(r + 0x3C);
		e.k1 = *(const UINT32 *) &ram[r + 0x44];
		e.k2 = *(const UINT32 *) &ram[r + 0x48];
		e.aiAct = r16(r + 0x22C2) & 0xFF;
		e.aiStep = ram[(r + 0x227A) ^ 3];
		e.inv = *(const UINT16 *) &ram[(r + 0x3F22) ^ 2];
		e.target = -1;
		const UINT32 tr = *(const UINT32 *) &ram[r + 0x32E4] & 0x7FFFFF;   // the player's Rob it goes for
		if (tr >= 0x1A0000 && tr < 0x1A0000 + 8 * 0x4000 && (tr & 0x3FFF) == 0) e.target = (int) ((tr - 0x1A0000) / 0x4000);
		const float dx = *(const float *) &ram[r + 0x380] - mx, dz = *(const float *) &ram[r + 0x388] - mz;
		e.dist = sqrtf(dx * dx + dz * dz);
		e.dy = *(const float *) &ram[r + 0x384] - my;
		// 11/25 (owner): its move, latched as the satellite's boss line (SatelliteHudUpdate): a move other than 00 (none) / 01
		// (let go) is taken with its steps when it starts; shown until it is back in its stance (kamae 0x00400000 with attack
		// 0x00800000 off and all steps fed), then 20 frames more.
		{
			struct Mv { int act = -1, n = 0, step = -1; uint8_t seq[16] = {}; bool on = false; UINT32 idleF = 0, lastF = 0; };
			static Mv mv[20];
			Mv &m = mv[slot];
			if (g_ppcTraceFrame - m.lastF > 60u) m = Mv();   // a new Rob in this slot (it was gone a while)
			m.lastF = g_ppcTraceFrame;
			const int act = r16(r + 0x22C2) & 0xFF, step = ram[(r + 0x227A) ^ 3];
			if (act > 0x01 && (act != m.act || (step != 0xFF && step < m.step) || !m.on))
			{
				m.act = act; m.n = 0; m.on = true; m.idleF = 0;
				for (int k = 0; k < 16; k++) { const uint8_t v = ram[(r + 0x227C + k) ^ 3]; if (v == 0xFF) break; m.seq[m.n++] = v; }
			}
			if (m.on)
			{
				m.step = (act == m.act && step != 0xFF) ? step : -1;
				const bool idle = !(e.k1 & 0x00800000u) && (e.k1 & 0x00400000u) && m.step < 0;
				if (!idle) m.idleF = 0;
				else if (!m.idleF) m.idleF = g_ppcTraceFrame ? g_ppcTraceFrame : 1;
				else if (g_ppcTraceFrame - m.idleF >= 20u) { m.on = false; m.act = -1; }
			}
			e.mvN = m.on ? m.n : 0;
			e.mvAct = m.act;
			e.mvStep = m.step;
			e.mvAtk = (e.k1 & 0x00800000u) != 0;
			for (int k = 0; k < 16; k++) e.mvSeq[k] = m.seq[k];
		}
	}
	h.stage = ram[0x10402C ^ 3];   // 11/25 (owner): the stage and area on the top line
	h.area = ram[0x10402D ^ 3];
	h.valid = true;
}

// 11/25 (owner): the launcher (PonLuncher) and the settings / lobby (PonUI) draw with the game's 8x8 font, taken from a
// save state's "Tile Generator" block - a PC that never saved a state had none. Once a run, about 30 s in (the font is
// in the tile VRAM by then), spikeofe writes Saves/font.st: a save state file with only that block (the player's own
// ROM data, made on their PC - nothing of the game is shipped).
static bool g_spikeoutFontDump = false;
void CModel3::SyncGPUs(void)
{
  UINT32 start = CThread::GetTicks();
  {
    static bool fontDone = false;
    if (g_spikeoutFontDump && !fontDone && g_ppcTraceFrame >= 1800)
    {
      fontDone = true;
      CBlockFile f;
      if (Result::OKAY == f.Create(std::string("Saves/font.st"), "Supermodel Save State", "PonMi font (Tile Generator only)"))
      {
        TileGen.SaveState(&f);
        f.Close();
      }
    }
  }
  if (g_satView.on) SatelliteCamUpdate(ram);
  if (g_satView.on) SatelliteHudUpdate(ram, crom);
  if (g_enemyHudOn) EnemyHudUpdate(ram, crom);
  if (g_downLogOn) DownLogUpdate(ram);

  timings.syncSize = GPU.SyncSnapshots() + TileGen.SyncSnapshots();
  gpusReady = true;

  timings.syncTicks = CThread::GetTicks() - start;
}

// 11/24 (owner): the game difficulty (0 easiest .. 4 hardest) from a Supermodel NVRAM file: its block "93C46" (the
// EEPROM, 64 little-endian 16-bit words) holds the settings ("M3SEGA" at 0, difficulty at 0x21; bytes swapped in pairs).
// -1 when it cannot be read.
static int SpikeoutNvDifficulty(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  int result = -1;
  for (;;)
  {
    UINT32 hdr[3];
    const long at = ftell(f);
    if (fread(hdr, 4, 3, f) != 3 || hdr[0] < 12 || hdr[1] < 1 || hdr[1] > 1025 || hdr[2] > 1025) break;
    char name[1026] = {};
    if (fread(name, 1, hdr[1], f) != hdr[1]) break;
    if (strcmp(name, "93C46") == 0)
    {
      fseek(f, (long) hdr[2], SEEK_CUR);
      UINT8 e[128];
      if (fread(e, 1, sizeof(e), f) == sizeof(e) && e[0 ^ 1] == 'M' && e[1 ^ 1] == '3' && e[2 ^ 1] == 'S' && e[0x21 ^ 1] <= 4)
        result = e[0x21 ^ 1];
      break;
    }
    if (fseek(f, at + (long) hdr[0], SEEK_SET) != 0) break;
  }
  fclose(f);
  return result;
}

void CModel3::RenderFrame(void)
{
  UINT32 start = CThread::GetTicks();

  // Call OSD video callbacks
  if (BeginFrameVideo() && gpusReady)
  {
    // Render frame
    TileGen.BeginFrame();
    GPU.BeginFrame();
    TileGen.PreRenderFrame();
    TileGen.RenderFrameBottom();
    GPU.RenderFrame();
    if (!(g_satView.on && g_satView.hideUI && g_satView.inGame))   // 11/20 satellite clone: no HUD
      TileGen.RenderFrameTop();
    if (g_satView.on && g_satView.overlay)                        // 11/20 satellite clone: title / name / ticker
      {
        // 11/21 (owner): title and ticker hard-coded. A slot is free when its node is not at the character select / in a
        // game (link state not 2 / 4: a STREAM slot nobody took, or a player whose game is over).
        bool freeSlot = false;
        for (int m = 1; m <= g_satClone.numMachines && m <= 8; m++)
          if (g_satClone.linkState[m] != 2 && g_satClone.linkState[m] != 4) freeSlot = true;
        static const std::string satTitle = "SPIKEOUT FE SATELLITE";
        // 11/24 (owner): "Welcome to Ponmi's *Spikeout* online system. Current status {Stage: Area} - {Participants/Max
        // capacity} ---" (the game font is ASCII: ' and - for the typographic marks). Stage / area of the shown game
        // ("-" outside a game), participants = cabinets at the character select / in a game, max = the cabinets of the link.
        (void) freeSlot;
        int players = 0;
        for (int m = 1; m <= g_satClone.numMachines && m <= 8; m++)
          if (g_satClone.linkState[m] == 2 || g_satClone.linkState[m] == 4) players++;
        char where[48];
        if (g_satView.inGame && g_satView.hud.valid) snprintf(where, sizeof(where), "Stage %d: Area %d", g_satView.hud.stage, g_satView.hud.area);
        else snprintf(where, sizeof(where), "Stage -: Area -");
        char tick[192];
        // 11/24 (owner, 3rd): + the difficulty - the game's settings copy in RAM ("M3SEGA" at 0x100190, difficulty +0x21 =
        // 0x1001B1: 0 easiest .. 4 hardest; checked against the 93C46 EEPROM in a save state). The followed cabinet's value.
        static const char *const kDiff[5] = { "easiest", "easy", "normal", "hard", "hardest" };
        // 11/24 (owner: 'normal but easiest shown'): read 01's NVRAM file (../01/NVRAM/spikeofe.nv, block 93C46, byte
        // 0x21, the 16-bit words byte-swapped) every 10 s - the RAM copy is 0 until the game has booted, and the first
        // ticker pass kept that. The RAM copy (0x1001B1, M3SEGA at 0x100190) only when the file cannot be read.
        static int s_diffNv = -1; static UINT32 s_diffAt = 0; static bool s_diffFirst = true;
        if (s_diffFirst || g_ppcTraceFrame - s_diffAt >= 600) { s_diffFirst = false; s_diffAt = g_ppcTraceFrame; s_diffNv = SpikeoutNvDifficulty("../01/NVRAM/spikeofe.nv"); }
        const bool ramOk = ram[0x100190 ^ 3] == 'M' && ram[0x100191 ^ 3] == '3' && ram[0x100192 ^ 3] == 'S';
        const unsigned diff = s_diffNv >= 0 ? (unsigned) s_diffNv : ramOk ? ram[0x1001B1 ^ 3] : 99u;
        snprintf(tick, sizeof(tick), "Welcome to PonMi's SPIKEOUT FE 8-Link ONLINE SYSTEM. ::: Current status Difficulty %s ::: %s ::: %d/%d Player(s) ...", diff < 5 ? kDiff[diff] : "-", where, players, g_satClone.numMachines);
        TileGen.SatOverlay(g_satView.inGame, satTitle, std::string(tick), &g_satView.hud);
      }
    if (g_enemyHudOn && g_enemyHud.valid)                          // 11/25 (owner): the enemy list, left (satellite: right of its ally list)
      TileGen.EnemyOverlay(&g_enemyHud, (g_satView.on && g_satView.overlay) ? 248 : 4);
    GPU.EndFrame();
    TileGen.EndFrame();
    m_superAA->Draw();
  }

  EndFrameVideo();

  timings.renderTicks = CThread::GetTicks() - start;
}

bool CModel3::RunSoundBoardFrame(void)
{
  UINT32 start = CThread::GetTicks();
  bool bufferFull = SoundBoard.RunFrame();
  timings.sndTicks = CThread::GetTicks() - start;
  return bufferFull;
}

void CModel3::RunDriveBoardFrame(void)
{
  UINT32 start = CThread::GetTicks();
  DriveBoard->RunFrame();
  timings.drvTicks = CThread::GetTicks() - start;
}

void CModel3::RunNetBoardFrame(void)
{
  NetBoard->RunFrame();
}

bool CModel3::StartThreads(void)
{
  if (startedThreads)
    return true;

  // Create synchronization objects
  if (m_gpuMultiThreaded)
  {
    ppcBrdThreadSync = CThread::CreateSemaphore(0);
    if (ppcBrdThreadSync == NULL)
      goto ThreadError;
  }
  sndBrdThreadSync = CThread::CreateSemaphore(0);
  if (sndBrdThreadSync == NULL)
    goto ThreadError;
  sndBrdNotifyLock = CThread::CreateMutex();
  if (sndBrdNotifyLock == NULL)
    goto ThreadError;
  sndBrdNotifySync = CThread::CreateCondVar();
  if (sndBrdNotifySync == NULL)
    goto ThreadError;
  if (DriveBoard->IsAttached())
  {
    drvBrdThreadSync = CThread::CreateSemaphore(0);
    if (drvBrdThreadSync == NULL)
      goto ThreadError;
  }
  notifyLock = CThread::CreateMutex();
  if (notifyLock == NULL)
    goto ThreadError;
  notifySync = CThread::CreateCondVar();
  if (notifySync == NULL)
    goto ThreadError;

  // Reset thread flags
  pauseThreads = false;
  stopThreads = false;

  // Create PPC main board thread, if multi-threading GPU
  if (m_gpuMultiThreaded)
  {
    ppcBrdThread = CThread::CreateThread("MainBoard", StartMainBoardThread, this);
    if (ppcBrdThread == NULL)
      goto ThreadError;
  }

  // Create sound board thread (sync'd or unsync'd)
  if (syncSndBrdThread)
    sndBrdThread = CThread::CreateThread("SoundBoardSync", StartSoundBoardThreadSyncd, this);
  else
    sndBrdThread = CThread::CreateThread("SoundBoardNoSync", StartSoundBoardThread, this);
  if (sndBrdThread == NULL)
    goto ThreadError;

  // Create drive board thread, if drive board is attached
  if (DriveBoard->IsAttached())
  {
    drvBrdThread = CThread::CreateThread("DriveBoard", StartDriveBoardThread, this);
    if (drvBrdThread == NULL)
      goto ThreadError;
  }

  // Set audio callback if sound board thread is unsync'd
  if (!syncSndBrdThread)
  {
    SetAudioCallback(AudioCallback, this);
  }

  startedThreads = true;
  return true;

ThreadError:
  ErrorLog("Unable to create threads and/or synchronization objects: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  DeleteThreadObjects();
  m_multiThreaded = false;
  return false;
}

bool CModel3::PauseThreads(void)
{
  if (!startedThreads)
    return true;

  // Enter notify critical section
  if (!notifyLock->Lock())
    goto ThreadError;

  // Let threads know that they should pause and wait for all of them to do so
  pauseThreads = true;
  while (ppcBrdThreadRunning || sndBrdThreadRunning || drvBrdThreadRunning)
  {
    if (!notifySync->Wait(notifyLock))
      goto ThreadError;
  }

  // Leave notify critical section
  if (!notifyLock->Unlock())
    goto ThreadError;
  return true;

ThreadError:
  ErrorLog("Threading error in CModel3::PauseThreads: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
  return false;
}

bool CModel3::ResumeThreads(void)
{
  if (!startedThreads)
    return true;

  // Enter notify critical section
  if (!notifyLock->Lock())
    goto ThreadError;

  // Let all threads know that they can continue running
  pauseThreads = false;

  // Leave notify critical section
  if (!notifyLock->Unlock())
    goto ThreadError;
  return true;

ThreadError:
  ErrorLog("Threading error in CModel3::ResumeThreads: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
  return false;
}

bool CModel3::StopThreads(void)
{
  if (!startedThreads)
    return true;

  // If sound board thread is unsync'd then remove audio callback
  if (!syncSndBrdThread)
    SetAudioCallback(NULL, NULL);

  // Enter notify critical section
  if (!notifyLock->Lock())
    goto ThreadError;

  // Let threads know that they should pause and wait for all of them to do so
  pauseThreads = true;
  while (ppcBrdThreadRunning || sndBrdThreadRunning || drvBrdThreadRunning)
  {
    if (!notifySync->Wait(notifyLock))
      goto ThreadError;
  }

  // Now let threads know that they should exit
  stopThreads = true;

  // Leave notify critical section
  if (!notifyLock->Unlock())
    goto ThreadError;

  // Resume each thread in turn and wait for them to exit
  if (ppcBrdThread != NULL)
  {
    if (ppcBrdThreadSync->Post())
      ppcBrdThread->Wait();
  }
  if (sndBrdThread != NULL)
  {
    if (syncSndBrdThread)
    {
      if (sndBrdThreadSync->Post())
        sndBrdThread->Wait();
    }
    else
    {
      if (WakeSoundBoardThread())
        sndBrdThread->Wait();
    }
  }
  if (drvBrdThread != NULL)
  {
    if (drvBrdThreadSync->Post())
      drvBrdThread->Wait();
  }

  // Delete all thread and synchronization objects
  DeleteThreadObjects();
  startedThreads = false;
  return true;

ThreadError:
  ErrorLog("Threading error in CModel3::StopThreads: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
  return false;
}

void CModel3::DeleteThreadObjects(void)
{
  // Delete PPC main board, sound board and drive board threads
  if (ppcBrdThread != NULL)
  {
    delete ppcBrdThread;
    ppcBrdThread = NULL;
  }
  if (sndBrdThread != NULL)
  {
    delete sndBrdThread;
    sndBrdThread = NULL;
  }
  if (drvBrdThread != NULL)
  {
    delete drvBrdThread;
    drvBrdThread = NULL;
  }


  // Delete synchronization objects
  if (ppcBrdThreadSync != NULL)
  {
    delete ppcBrdThreadSync;
    ppcBrdThreadSync = NULL;
  }
  if (sndBrdThreadSync != NULL)
  {
    delete sndBrdThreadSync;
    sndBrdThreadSync = NULL;
  }
  if (drvBrdThreadSync != NULL)
  {
    delete drvBrdThreadSync;
    drvBrdThreadSync = NULL;
  }


  if (sndBrdNotifyLock != NULL)
  {
    delete sndBrdNotifyLock;
    sndBrdNotifyLock = NULL;
  }
  if (sndBrdNotifySync != NULL)
  {
    delete sndBrdNotifySync;
    sndBrdNotifySync = NULL;
  }
  if (notifyLock != NULL)
  {
    delete notifyLock;
    notifyLock = NULL;
  }
  if (notifySync != NULL)
  {
    delete notifySync;
    notifySync = NULL;
  }
}

void CModel3::DumpTimings(void)
{
  printf("PPC:%3ums%c render:%3ums%c sync:%4uK%c%3ums%c snd:%3ums%c drv:%3ums%c frame:%3ums%c\n",
    timings.ppcTicks, (timings.ppcTicks > timings.renderTicks ? '!' : ','),
    timings.renderTicks, (timings.renderTicks > timings.ppcTicks ? '!' : ','),
    timings.syncSize / 1024, (timings.syncSize / 1024 > 128 ? '!' : ','),
    timings.syncTicks, (timings.syncTicks > 1 ? '!' : ','),
    timings.sndTicks, (timings.sndTicks > 10 ? '!' : ','),
    timings.drvTicks, (timings.drvTicks > 10 ? '!' : ','),
    timings.frameTicks, (timings.frameTicks > 16 ? '!' : ' '));
}

FrameTimings CModel3::GetTimings(void)
{
  return timings;
}

int CModel3::StartMainBoardThread(void *data)
{
  // Call method on CModel3 to run PPC main board thread
  CModel3 *model3 = (CModel3*)data;
  return model3->RunMainBoardThread();
}

int CModel3::StartSoundBoardThread(void *data)
{
  // Call method on CModel3 to run sound board thread (unsync'd)
  CModel3 *model3 = (CModel3*)data;
  return model3->RunSoundBoardThread();
}

int CModel3::StartSoundBoardThreadSyncd(void *data)
{
  // Call method on CModel3 to run sound board thread (sync'd)
  CModel3 *model3 = (CModel3*)data;
  return model3->RunSoundBoardThreadSyncd();
}

int CModel3::StartDriveBoardThread(void *data)
{
  // Call method on CModel3 to run drive board thread
  CModel3 *model3 = (CModel3*)data;
  return model3->RunDriveBoardThread();
}

int CModel3::RunMainBoardThread(void)
{
  for (;;)
  {
    bool wait = true;
    bool exit = false;
    while (wait && !exit)
    {
      // Wait on PPC main board thread semaphore
      if (!ppcBrdThreadSync->Wait())
        goto ThreadError;

      // Enter notify critical section
      if (!notifyLock->Lock())
        goto ThreadError;

      // Check threads are not being stopped or paused
      if (stopThreads)
        exit = true;
      else if (!pauseThreads)
      {
        wait = false;
        ppcBrdThreadRunning = true;
      }

      // Leave notify critical section
      if (!notifyLock->Unlock())
        goto ThreadError;
    }
    if (exit)
      return 0;

    // Process a single frame for PPC main board
    RunMainBoardFrame();

    // Enter notify critical section
    if (!notifyLock->Lock())
      goto ThreadError;

    // Let other threads know processing has finished
    ppcBrdThreadRunning = false;
    ppcBrdThreadDone = true;
    if (!notifySync->SignalAll())
      goto ThreadError;

    // Leave notify critical section
    if (!notifyLock->Unlock())
      goto ThreadError;
  }

ThreadError:
  ErrorLog("Threading error in RunMainBoardThread: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
  return 1;
}

void CModel3::AudioCallback(void *data)
{
  // Call method on CModel3 to wake sound board thread
  CModel3 *model3 = (CModel3*)data;
  model3->WakeSoundBoardThread();
}

bool CModel3::WakeSoundBoardThread(void)
{
  // Enter sound board notify critical section
  bool wake;
  if (!sndBrdNotifyLock->Lock())
    goto ThreadError;

  // Enter main notify critical section
  if (!notifyLock->Lock())
    goto ThreadError;

  // See if sound board thread is currently running
  wake = !sndBrdThreadRunning;

  // Leave main notify critical section
  if (!notifyLock->Unlock())
    goto ThreadError;

  // Only send wake notification to sound board thread if it was not running
  if (wake)
  {
    // Signal to sound board thread that it should start processing again
    sndBrdWakeNotify = true;
    if (!sndBrdNotifySync->Signal())
      goto ThreadError;
  }

  // Leave sound board notify critical section
  if (!sndBrdNotifyLock->Unlock())
    goto ThreadError;
  return wake;

ThreadError:
  ErrorLog("Threading error in WakeSoundBoardThread: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
  return false;
}

int CModel3::RunSoundBoardThread(void)
{
  for (;;)
  {
    bool wait = true;
    bool exit = false;
    while (wait && !exit)
    {
      // Enter sound board notify critical section
      if (!sndBrdNotifyLock->Lock())
        goto ThreadError;

      // Wait for notification from audio callback
      while (!sndBrdWakeNotify)
      {
        if (!sndBrdNotifySync->Wait(sndBrdNotifyLock))
          goto ThreadError;
      }
      sndBrdWakeNotify = false;

      // Enter main notify critical section
      if (!notifyLock->Lock())
        goto ThreadError;

      // Check threads are not being stopped or paused
      if (stopThreads)
        exit = true;
      else if (!pauseThreads)
      {
        wait = false;
        sndBrdThreadRunning = true;
      }

      // Leave main notify critical section
      if (!notifyLock->Unlock())
        goto ThreadError;

      // Leave sound board notify critical section
      if (!sndBrdNotifyLock->Unlock())
        goto ThreadError;
    }
    if (exit)
      return 0;

    // Keep processing frames until pausing or audio buffer is full
    while (true)
    {
      // Enter main notify critical section
      bool paused;
      if (!notifyLock->Lock())
        goto ThreadError;

      paused = pauseThreads;

      // Leave main notify critical section
      if (!notifyLock->Unlock())
        goto ThreadError;

      if (paused || RunSoundBoardFrame())
        break;
      //printf("Rerunning sound board\n");
    }

    // Enter main notify critical section
    if (!notifyLock->Lock())
      goto ThreadError;

    // Let other threads know processing has finished
    sndBrdThreadRunning = false;
    sndBrdThreadDone = true;
    if (!notifySync->SignalAll())
      goto ThreadError;

    // Leave main notify critical section
    if (!notifyLock->Unlock())
      goto ThreadError;
  }

ThreadError:
  ErrorLog("Threading error in RunSoundBoardThread: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
  return 1;
}

int CModel3::RunSoundBoardThreadSyncd(void)
{
  for (;;)
  {
    bool wait = true;
    bool exit = false;
    while (wait && !exit)
    {
      // Wait on sound board thread semaphore
      if (!sndBrdThreadSync->Wait())
        goto ThreadError;

      // Enter notify critical section
      if (!notifyLock->Lock())
        goto ThreadError;

      // Check threads are not being stopped or paused
      if (stopThreads)
        exit = true;
      else if (!pauseThreads)
      {
        wait = false;
        sndBrdThreadRunning = true;
      }

      // Leave notify critical section
      if (!notifyLock->Unlock())
        goto ThreadError;
    }
    if (exit)
      return 0;

    // Process a single frame for sound board
    RunSoundBoardFrame();

    // Enter notify critical section
    if (!notifyLock->Lock())
      goto ThreadError;

    // Let other threads know processing has finished
    sndBrdThreadRunning = false;
    sndBrdThreadDone = true;
    if (!notifySync->SignalAll())
      goto ThreadError;

    // Leave notify critical section
    if (!notifyLock->Unlock())
      goto ThreadError;
  }

ThreadError:
  ErrorLog("Threading error in RunSoundBoardThreadSyncd: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
  return 1;
}

int CModel3::RunDriveBoardThread(void)
{
  for (;;)
  {
    bool wait = true;
    bool exit = false;
    while (wait && !exit)
    {
      // Wait on drive board thread semaphore
      if (!drvBrdThreadSync->Wait())
        goto ThreadError;

      // Enter notify critical section
      if (!notifyLock->Lock())
        goto ThreadError;

      // Check threads are not being stopped or paused
      if (stopThreads)
        exit = true;
      else if (!pauseThreads)
      {
        wait = false;
        drvBrdThreadRunning = true;
      }

      // Leave notify critical section
      if (!notifyLock->Unlock())
        goto ThreadError;
    }
    if (exit)
      return 0;

    // Process a single frame for drive board
    RunDriveBoardFrame();

    // Enter notify critical section
    if (!notifyLock->Lock())
      goto ThreadError;

    // Let other threads know processing has finished
    drvBrdThreadRunning = false;
    drvBrdThreadDone = true;
    if (!notifySync->SignalAll())
      goto ThreadError;

    // Leave notify critical section
    if (!notifyLock->Unlock())
      goto ThreadError;
  }

ThreadError:
  ErrorLog("Threading error in RunDriveBoardThread: %s\nSwitching back to single-threaded mode.\n", CThread::GetLastError());
  m_multiThreaded = false;
  return 1;
}

void CModel3::Reset(void)
{
  // Clear memory (but do not modify backup RAM!)
  memset(ram, 0, 0x800000);

  // Initial bank is bank 0
  SetCROMBank(0xFF);

  // Reset security device
  securityPtr = 0;
  m_securityFirstRead = true;

  // Reset inputs
  inputBank = 0;
  serialFIFO1 = 0;
  serialFIFO2 = 0;
  adcChannel = 0;

  // MIDI
  midiCtrlPort = 0;

  // Reset all devices
  ppc_reset();
  IRQ.Reset();
  PCIBridge.Reset();
  PCIBus.Reset();
  SCSI.Reset();
  RTC.Reset();
  EEPROM.Reset();
  TileGen.Reset();
  GPU.Reset();
  SoundBoard.Reset();
  m_jtag.Reset();

  if (DriveBoard->IsAttached())
    DriveBoard->Reset();

  m_cryptoDevice.Reset();

  gpusReady = false;

  timings.ppcTicks = 0;
  timings.syncSize = 0;
  timings.syncTicks = 0;
  timings.renderTicks = 0;
  timings.sndTicks = 0;
  timings.drvTicks = 0;
  timings.netTicks = 0;
  NetBoard->Reset();
  timings.frameTicks = 0;
  timings.frameId = 0;
  
  DebugLog("Model 3 reset\n");
}


/******************************************************************************
 Initialization, Shutdown, and ROM Management
******************************************************************************/

const Game &CModel3::GetGame() const
{
  return m_game;
}

// Stepping-dependent parameters (MPC10x type, etc.) are initialized here
Result CModel3::LoadGame(const Game &game, const ROMSet &rom_set)
{
  m_game = Game();

  /*
   * Copy in ROM data with mirroring as necessary for the following cases:
   *
   *  - VROM: 64MB. If <= 32MB, mirror to high 32MB.
   *  - Banked CROM: 128MB. If <= 64MB, mirror to high 64MB.
   *  - Fixed CROM: 8MB. If < 8MB, loaded only in high part of space and low
   *    part is a mirror of (banked) CROM0.
   *  - Sample ROM: 16MB. If <= 8MB, mirror to high 8MB.
   */
  if (rom_set.get_rom("vrom").size <= 32*0x100000)
  {
    rom_set.get_rom("vrom").CopyTo(&vrom[0], 32*100000);
    rom_set.get_rom("vrom").CopyTo(&vrom[32*0x100000], 32*0x100000);
  }
  else
    rom_set.get_rom("vrom").CopyTo(vrom, 64*0x100000);
  if (rom_set.get_rom("banked_crom").size <= 64*0x100000)
  {
    rom_set.get_rom("banked_crom").CopyTo(&crom[8*0x100000 + 0], 64*0x100000);
    rom_set.get_rom("banked_crom").CopyTo(&crom[8*0x100000 + 64*0x100000], 64*0x100000);
  }
  else
    rom_set.get_rom("banked_crom").CopyTo(&crom[8*0x100000 + 0], 128*0x100000);
  size_t crom_size = rom_set.get_rom("crom").size;
  rom_set.get_rom("crom").CopyTo(&crom[8*0x100000 - crom_size], crom_size);
  if (crom_size < 8*0x100000)
    rom_set.get_rom("banked_crom").CopyTo(&crom[0], 8*0x100000 - crom_size);
  if (rom_set.get_rom("sound_samples").size <= 8*0x100000)
  {
    rom_set.get_rom("sound_samples").CopyTo(&sampleROM[0], 8*0x100000);
    rom_set.get_rom("sound_samples").CopyTo(&sampleROM[8*0x100000], 8*0x100000);
  }
  else
    rom_set.get_rom("sound_samples").CopyTo(sampleROM, 16*0x100000);
  rom_set.get_rom("sound_program").CopyTo(soundROM, 512*1024);
  rom_set.get_rom("mpeg_program").CopyTo(dsbROM, 128*1024);
  rom_set.get_rom("mpeg_music").CopyTo(mpegROM, 16*0x100000);
  rom_set.get_rom("driveboard_program").CopyTo(driveROM, 64*1024);

  // Convert PowerPC and 68K ROMs to little endian words
  Util::FlipEndian32(crom, 8*0x100000 + 128*0x100000);
  Util::FlipEndian16(soundROM, 512*1024);
  Util::FlipEndian16(sampleROM, 16*0x100000);

  // Scripted test inputs
  m_testFrame = 0;
  std::string testInputScript = m_config["TestInputScript"].ValueAsDefault<std::string>("");
  if (!testInputScript.empty())
    LoadTestInputScript(testInputScript);
  m_traceEnemyCreate = game.name == "spikeofe" && m_config["TraceEnemyCreate"].ValueAsDefault<bool>(false);
  unsigned traceCall = 0;
  if (sscanf(m_config["TraceCall"].ValueAsDefault<std::string>("").c_str(), "%x", &traceCall) == 1)
    g_ppcTraceCallTarget = traceCall;
  unsigned watchStart = 0, watchLen = 0;
  g_ppcStateTrace = (game.name == "spikeofe" && m_config["StateTrace"].ValueAsDefault<bool>(false));
  g_spikeoutP1Invincible = (game.name == "spikeofe" && m_config["P1Invincible"].ValueAsDefault<bool>(false));
  g_spikeoutTimerStop = (game.name == "spikeofe" && m_config["TimerStop"].ValueAsDefault<bool>(false));
  g_spikeoutSpikeJr = (game.name == "spikeofe" && m_config["SpikeJr"].ValueAsDefault<bool>(false));
  g_spikeoutJumpMul = game.name == "spikeofe" ? m_config["PlayerJumpMul"].ValueAsDefault<float>(1.0f) : 1.0f;   // 11/26
  if (!(g_spikeoutJumpMul >= 1.0f && g_spikeoutJumpMul <= 6.0f)) g_spikeoutJumpMul = 1.0f;
  if (g_spikeoutJumpMul != 1.0f) InfoLog("PlayerJumpMul: players jump x%.2f as high.", g_spikeoutJumpMul);
  g_spikeoutFontDump = game.name == "spikeofe";   // 11/25: Saves/font.st for the launcher / PonUI font
  { extern bool g_costumeSpikeJr; g_costumeSpikeJr = g_spikeoutSpikeJr; }   // 11/23: Real3D CostumeSwap leaves Spike (Jr) in colour 0
  if (g_spikeoutSpikeJr) InfoLog("SpikeJr: players who pick Spike play Spike Jr (character 4).");
  if (g_spikeoutTimerStop) InfoLog("TimerStop: countdown timers held at 33 with 1-4 players too (5+ always).");
  g_spikeoutAI = (game.name == "spikeofe" && m_config["AIPlayer"].ValueAsDefault<bool>(false));
  g_spikeoutInputLog = (game.name == "spikeofe" && !g_spikeoutAI && m_config["InputLog"].ValueAsDefault<bool>(false));
  {
    std::string ch = m_config["AIChar"].ValueAsDefault<std::string>("");
    for (char &c : ch) c = (char) tolower((unsigned char) c);
    g_spikeoutAIChar = -1;
    if (ch == "spike" || ch == "0") g_spikeoutAIChar = 0;
    else if (ch == "white" || ch == "1") g_spikeoutAIChar = 1;
    else if (ch == "linda" || ch == "2") g_spikeoutAIChar = 2;
    else if (ch == "tenshin" || ch == "3") g_spikeoutAIChar = 3;
    g_spikeoutAIRandSel = m_config["AIrandSel"].ValueAsDefault<bool>(true);   // 11/17 (owner); default = as before (random)
  }
  g_spikeoutWaypointRecord = (game.name == "spikeofe" && m_config["WaypointRecord"].ValueAsDefault<bool>(false));
  g_spikeoutScenarioLog = (game.name == "spikeofe" && m_config["ScenarioLog"].ValueAsDefault<bool>(false));
  g_texGhost = (game.name == "spikeofe" && m_config["New3DEngine"].ValueAsDefault<bool>(true));   // 11/12: costumes 4-7 to ghost texture pages
  g_spikeoutItemLog = (game.name == "spikeofe" && m_config["ItemLog"].ValueAsDefault<bool>(false));
  // 11/13: skip the frame-wait polling loops (ppc.cpp ppc_idle_check). IdleSkip=0 turns it off.
  ppc_set_idle_skip(game.name == "spikeofe" && m_config["IdleSkip"].ValueAsDefault<bool>(true));
  g_spikeoutIdleLog = (game.name == "spikeofe" && m_config["IdleSkipLog"].ValueAsDefault<bool>(false));
  g_floorDump = (game.name == "spikeofe" && m_config["FloorDump"].ValueAsDefault<bool>(false));
  g_spikeoutAIShift =(game.name == "spikeofe" && m_config["AIShift"].ValueAsDefault<bool>(false));
  g_spikeoutAIRoute = m_config["AIRoute"].ValueAsDefault<int>(-1);
  if (g_spikeoutAIRoute < 0 || g_spikeoutAIRoute > 3) g_spikeoutAIRoute = -1;
  g_spikeoutAIRoute2 = m_config["AIRoute2"].ValueAsDefault<int>(-1);
  g_spikeoutAIRoute3 = m_config["AIRoute3"].ValueAsDefault<int>(-1);
  if (g_spikeoutAIRoute3 < 0 || g_spikeoutAIRoute3 > 1) g_spikeoutAIRoute3 = -1;
  if (g_spikeoutAIRoute2 < 0 || g_spikeoutAIRoute2 > 1) g_spikeoutAIRoute2 = -1;
  g_spikeoutGateLog = (game.name == "spikeofe" && m_config["GateLog"].ValueAsDefault<bool>(false));
  g_spikeoutAILog = (game.name == "spikeofe" && m_config["AIPlayerLog"].ValueAsDefault<bool>(false));
  g_spikeoutAIProfile = (game.name == "spikeofe" && m_config["AIProfile"].ValueAsDefault<bool>(false));   // 11/23
  g_spikeoutCpicLog = (game.name == "spikeofe" && m_config["CpicLog"].ValueAsDefault<bool>(false));
  g_spikeoutAITargetMarker = (game.name == "spikeofe" && m_config["AITargetMarker"].ValueAsDefault<bool>(false));
  g_downLogOn = (game.name == "spikeofe" && m_config["DownLog"].ValueAsDefault<bool>(false));   // 11/26: downlog.txt
  g_enemyHudOn = (game.name == "spikeofe" && m_config["EnemyHud"].ValueAsDefault<bool>(false));   // 11/25: the enemy list on screen
  g_spikeoutBotCanOpenGate = m_config["BotCanOpenGate"].ValueAsDefault<bool>(true);
  if (game.name == "spikeofe" && !g_spikeoutBotCanOpenGate) InfoLog("BotCanOpenGate = 0: the bots do not break gates.");
  g_spikeoutWallView = (game.name == "spikeofe" && m_config["AIWallView"].ValueAsDefault<bool>(false));   // 11/17: walls on screen (bot cabinets)
  {
    extern bool g_texLog;   // Graphics/New3D/R3DShader.cpp (11/15): textures the meshes use in the costume areas -> texlog.txt
    g_texLog = (game.name == "spikeofe" && m_config["TexLog"].ValueAsDefault<bool>(false));
    extern bool g_modelColorLog;   // Graphics/New3D/New3D.cpp (11/26): models and their polygon colours -> modellog.txt
    g_modelColorLog = (game.name == "spikeofe" && m_config["ModelColorLog"].ValueAsDefault<bool>(false));
    // 11/26 (owner): the floating life-up cross (VROM model 0x5A510A: 15 polygons, front FD0000, sides 99/66/4C 0000)
    // drawn green instead of red - its red polygon colours get red and green swapped as the ROM is loaded (ini
    // LifeUpGreen, default on). Only the picture: the CPU never reads VROM. Also the first-aid box on the ground
    // (0x63DA89: 67 polygons, white / grey / blue and the cross FF0000 x10).
    if (game.name == "spikeofe" && m_config["LifeUpGreen"].ValueAsDefault<bool>(true))
    {
      // word addresses in VROM: the box, and the bigger box (0x5AAEE8, the same 67 polygons - 01's save state 11/26 12:43)
      // (11/26 owner: the floating cross 0x5A510A back to red)
      static const uint32_t kModels[] = { 0x63DA89, 0x5AAEE8 };
      for (uint32_t kModel : kModels)
      {
      uint32_t *w = reinterpret_cast<uint32_t *>(vrom) + kModel;
      const uint32_t *end = reinterpret_cast<uint32_t *>(vrom) + 0x4000000 / 4;
      int polys = 0, changed = 0;
      while (w + 7 <= end && w[6] != 0 && polys < 256)
      {
        polys++;
        if (w[1] & 2)   // colour in the header (not the colour table)
        {
          const uint32_t r = w[4] >> 24, g = (w[4] >> 16) & 0xFF, b = (w[4] >> 8) & 0xFF;
          if (r >= 0x30 && g <= r / 4 && b <= r / 4)
          {
            w[4] = (g << 24) | (r << 16) | (b << 8) | (w[4] & 0xFF);
            changed++;
          }
        }
        if (w[1] & 4) break;   // last polygon
        const int verts = (w[0] & 0x40) ? 4 : 3;
        int shared = 0;
        for (int i = 0; i < 4; i++) shared += (w[0] >> i) & 1;
        w += 7 + (verts - shared) * 4;
      }
      InfoLog("LifeUpGreen: model %06X: %d of %d polygons made green.", kModel, changed, polys);
      }
    }
  }
  g_spikeoutAICalib = (game.name == "spikeofe" && m_config["AICalib"].ValueAsDefault<bool>(false));
  g_taskArrayLog = m_config["TaskArrayLog"].ValueAsDefault<bool>(false);
  g_spikeofe8task = (game.name == "spikeofe" && m_config["Spikeout8Link"].ValueAsDefault<bool>(false)
                     && m_config["Spikeout8Players"].ValueAsDefault<bool>(false));
  if (g_spikeofe8task) {
    memset(g_taskShadow, 0, sizeof(g_taskShadow));
    memset(g_taskPcBase, 0, sizeof(g_taskPcBase));
    for (size_t i = 0; i < sizeof(s_spikeofeTaskPc)/sizeof(s_spikeofeTaskPc[0]); i++)
      g_taskPcBase[(s_spikeofeTaskPc[i].pc >> 2) & 0x1FFFF] = s_spikeofeTaskPc[i].base;
  }
  if (sscanf(m_config["WatchWrite"].ValueAsDefault<std::string>("").c_str(), "%x,%x", &watchStart, &watchLen) == 2)
  {
    m_watchStart = watchStart;
    m_watchEnd = watchStart + watchLen;
  }
  // 11/27 fast RAM path in the PPC core (ppc.cpp): only when no RAM hook above is on (they need every access)
  {
    extern UINT8 *g_ppcFastRam;
    extern const unsigned char *g_ppcFastTaskPc;
    bool ramHooks = g_taskArrayLog || g_ppcStateTrace || m_traceEnemyCreate || m_watchEnd > m_watchStart;
    g_ppcFastRam = (ramHooks || !m_config["FastRam"].ValueAsDefault<bool>(true)) ? nullptr : ram;
    g_ppcFastTaskPc = g_spikeofe8task ? g_taskPcBase : nullptr;
    printf("FastRam: %s\n", g_ppcFastRam ? "on" : "off (a RAM hook is on, or FastRam = 0)");
  }

  // Spikeout Final Edition 8-cabinet link patches (see Spikeout8Link.h)
  if (game.name == "spikeofe" && m_config["Spikeout8Link"].ValueAsDefault<bool>(false))
  {
    // Verify every site first so a different ROM revision is never half-patched
    auto applyPatches = [this](const char *name, const Spikeout8LinkPatch *patches, size_t count) -> bool
    {
      for (size_t i = 0; i < count; i++)
      {
        UINT32 value = *(UINT32 *) &crom[0x600000 + patches[i].addr];
        if (value != patches[i].oldValue)
        {
          ErrorLog("%s: unexpected value at %05X (%08X). Patch not applied.", name, patches[i].addr, value);
          return false;
        }
      }
      for (size_t i = 0; i < count; i++)
        *(UINT32 *) &crom[0x600000 + patches[i].addr] = patches[i].newValue;
      g_spk8Applied.insert(g_spk8Applied.end(), patches, patches + count);   // for save-state loads (11/14)
      InfoLog("%s: applied %zu patches.", name, count);
      return true;
    };
    bool linkApplied = applyPatches("Spikeout8Link", s_spikeout8LinkPatches, sizeof(s_spikeout8LinkPatches) / sizeof(s_spikeout8LinkPatches[0]));
    if (linkApplied && m_config["Spikeout8Players"].ValueAsDefault<bool>(false))
      applyPatches("Spikeout8Players", s_spikeout8PlayersPatches, sizeof(s_spikeout8PlayersPatches) / sizeof(s_spikeout8PlayersPatches[0]));
    // CostumeSwap (11/14, default on with 8 players): slots 4-7 get new costume colours instead of wrapping round to the
    // 4 ROM colours. Real3D.cpp replaces the upload data. 11/17 (owner): the new colours are made at start-up from the
    // ROM costumes below (no pictures in the exe or in a folder any more; CostumeDir is gone).
    if (linkApplied && m_config["Spikeout8Players"].ValueAsDefault<bool>(false) && m_config["CostumeSwap"].ValueAsDefault<bool>(true))
    {
      extern void Real3DCostumeSwapSetup(const uint32_t hdrSlot0[10], const uint32_t hdr[8][10], const uint16_t *const orig[4][4], const uint8_t *ram);
      auto C32 = [this](UINT32 off) { return *(const UINT32 *) &crom[off]; };   // host-order word of the CROM image
      uint32_t h0[10], h47[8][10]; const uint16_t *orig[4][4] = {};   // 11/14 col8: all 8 slots
      for (int i = 0; i < 10; i++) h0[i] = C32(0x600000 + 0xA1924 + 4 * i);   // slot 0's piece headers (the BMP layout)
      bool ok = true;
      for (int s = 0; s < 8; s++)
      {
        UINT32 lst = C32(0x600000 + 0x3548 + 4 * s);                          // 8-slot area table (patched): slots 0-7
        if (lst >= 0x200000) { ok = false; break; }
        for (int i = 0; i < 10; i++) h47[s][i] = C32(0x600000 + lst + 4 * i);
      }
      for (int c = 0; c < 4 && ok; c++)                                        // costume table 0xFFF6C878: [character] -> [colour]
      {
        UINT32 p = C32(0xFFF6C878u - 0xFF800000u + 4 * c);
        if (p < 0xFF800000u) { ok = false; break; }
        for (int k = 0; k < 4; k++)
        {
          UINT32 v = C32(p - 0xFF800000u + 4 * k);
          orig[c][k] = (const uint16_t *) &crom[0x800000 + 7 * 0x800000 + (v & 0x7FFFFC)];   // bank byte 0x0D = banked CROM bank 7 (11/14)
        }
      }
      if (ok) Real3DCostumeSwapSetup(h0, h47, orig, ram);
      else ErrorLog("CostumeSwap: unexpected CROM tables, not used.");
    }
    // HudColors (11/14, default on with 8 players): the ally names of slots 4-7 in pink / purple / grey / white. A
    // player's name is drawn from 2D image 0x9A-0x9D by colour number (table 0x8F9CC; the 4 images are the same picture,
    // each with its own palette: count, 0x8000, then the colours, xBGR555). The image tables are full (0xFFD00004:
    // 246 ids, then the images), so both are copied to free CROM space with 4 more ids 0xF6-0xF9 = copies of the red
    // one (0x9A) with the palette recoloured, the 9 places that load the table bases point at the copies, and the 3
    // name sites add 4 to the colour number for slots 4-7 (table of 8 in the cave at 0x3D64). Only the CROM image in
    // memory and the program are changed, as with the other patches. MUST be the same on all 8 cabinets.
    if (linkApplied && m_config["Spikeout8Players"].ValueAsDefault<bool>(false) && m_config["HudColors"].ValueAsDefault<bool>(true))
    {
      auto CW = [this](UINT32 off) -> UINT32 & { return *(UINT32 *) &crom[off]; };   // host-order CROM word
      const UINT32 FIX = 0xFF800000u, BANK13 = 0x800000 + 13 * 0x800000;            // CPU 0xFF8xxxxx -> crom; bank 13 of the 0xFF0xxxxx window
      const UINT32 DESC_OLD = 0xFFD00004u, DESC_NEW = 0xFFD50004u, PAT_NEW = 0x3E0000, IMG_NEW = 0x3E1000, NIDS = 0xF6;
      bool ok = true;
      for (UINT32 i = 0; i < NIDS + 4 && ok; i++)                                   // destinations must be unused (0xFF fill)
        ok = CW(DESC_NEW - FIX + 4 * i) == 0xFFFFFFFFu && CW(BANK13 + PAT_NEW + 4 * i) == 0xFFFFFFFFu;
      UINT32 src = CW(BANK13 + 4 * 0x9A) & 0x7FFFFF, srcEnd = CW(BANK13 + 4 * 0x9B) & 0x7FFFFF;
      UINT32 size = (srcEnd - src + 3) & ~3u;
      ok = ok && CW(DESC_OLD - FIX + 4 * 0x9A) >= 0xFFD00000u && src < srcEnd && size <= 0x800;
      for (UINT32 i = 0; i < 4 * 0x800 / 4 && ok; i++) ok = CW(BANK13 + IMG_NEW + 4 * i) == 0xFFFFFFFFu;
      const UINT32 sites[8] = { 0x9078, 0x9230, 0x9344, 0x9558, 0x99D4, 0x9C04, 0x45280, 0x50A54 };
      for (UINT32 a : sites) ok = ok && (CW(0x600000 + a) & 0xFC1FFFFFu) == 0x3C00FFD0u && CW(0x600000 + a + 4) == (0x38000004u | ((CW(0x600000 + a) & 0x03E00000u) >> 5) | (CW(0x600000 + a) & 0x03E00000u));
      if (!ok) ErrorLog("HudColors: unexpected CROM contents, not used.");
      else
      {
        // descriptor table copy + ids 0xF6-0xF9 -> the same picture as 0x9A-0x9D
        for (UINT32 i = 0; i < NIDS; i++) CW(DESC_NEW - FIX + 4 * i) = CW(DESC_OLD - FIX + 4 * i);
        for (UINT32 k = 0; k < 4; k++) CW(DESC_NEW - FIX + 4 * (NIDS + k)) = CW(DESC_OLD - FIX + 4 * (0x9A + k));
        // pattern pointer table copy (bank 13) + the 4 recoloured copies of 0x9A
        for (UINT32 i = 0; i < NIDS; i++) CW(BANK13 + PAT_NEW + 4 * i) = CW(BANK13 + 4 * i);
        static const struct { float hue, sat, va, vb; } tint[4] = {
          { 330.0f / 360.0f, 0.55f, 0.20f, 0.85f },   // pink
          { 275.0f / 360.0f, 0.85f, 0.00f, 0.95f },   // purple
          { 0.0f,            0.00f, 0.03f, 0.27f },   // grey (almost black)
          { 0.0f,            0.00f, 0.45f, 0.60f },   // white
        };
        for (UINT32 k = 0; k < 4; k++)
        {
          const UINT32 dst = IMG_NEW + 0x800 * k;
          for (UINT32 i = 0; i < size / 4; i++) CW(BANK13 + dst + 4 * i) = CW(BANK13 + src + 4 * i);
          CW(BANK13 + PAT_NEW + 4 * (NIDS + k)) = 0xFF000000u | dst;
          auto H = [&](UINT32 off) -> UINT16 & { return *(UINT16 *) &crom[BANK13 + ((dst + off) ^ 2)]; };   // big-endian halfword in host words
          unsigned n = H(0); if (n > 64) n = 0;
          for (unsigned j = 0; j < n; j++)
          {
            // 11/14 fix: the colours start right after the count (halfword 1: 0x8000 is colour 0). Starting one later
            // recoloured the 0x8039 that follows the palette and broke the image - every cabinet hung when it was drawn.
            UINT16 &p = H(2 + 2 * j);
            float r = (p & 31) / 31.0f, g = ((p >> 5) & 31) / 31.0f, b = ((p >> 10) & 31) / 31.0f;   // xBGR555
            float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), v = mx, s = mx > 0 ? (mx - mn) / mx : 0.0f;
            float v2 = std::min(1.0f, tint[k].va + tint[k].vb * v), s2 = std::min(1.0f, s * tint[k].sat), h = tint[k].hue * 6.0f;
            int hi = (int) h % 6; float f = h - (int) h, P = v2 * (1 - s2), Q = v2 * (1 - s2 * f), T = v2 * (1 - s2 * (1 - f));
            float rr = hi == 0 ? v2 : hi == 1 ? Q : hi == 2 ? P : hi == 3 ? P : hi == 4 ? T : v2;
            float gg = hi == 0 ? T : hi == 1 ? v2 : hi == 2 ? v2 : hi == 3 ? Q : hi == 4 ? P : P;
            float bb = hi == 0 ? P : hi == 1 ? P : hi == 2 ? T : hi == 3 ? v2 : hi == 4 ? v2 : Q;
            if ((p & 0x7FFF) == 0) continue;   // black / transparent entries (0x8000, 0x0000) stay
            p = (UINT16) ((p & 0x8000) | (((int) (bb * 31 + 0.5f)) << 10) | (((int) (gg * 31 + 0.5f)) << 5) | (int) (rr * 31 + 0.5f));
          }
        }
        // the program: table bases -> the copies; colour 0-7 -> image via the 8-entry table in the cave (11/14 col8)
        std::vector<Spikeout8LinkPatch> hp;
        for (UINT32 a : sites) hp.push_back({ a, CW(0x600000 + a), (CW(0x600000 + a) & 0xFFFF0000u) | 0xFFD5u });
        hp.push_back({ 0x9080, 0x3D20FF00u, 0x3D20FF3Eu });                       // lis r9,-0x100 -> 0xFF3E (pattern table copy)
        const UINT32 TBL = 0x3D64, CAVE = 0x3D74;
        hp.push_back({ TBL + 0, 0, 0x009A009Bu }); hp.push_back({ TBL + 4, 0, 0x009C009Du });
        hp.push_back({ TBL + 8, 0, 0x00F600F7u }); hp.push_back({ TBL + 12, 0, 0x00F800F9u });
        // 0x2A888: the own name at the top left (slot = byte 0x10065B - 1, in r4; 11/14)
        const struct { UINT32 at; UINT32 slotReg; UINT32 cave; } ns[4] = { { 0x1AC0C, 23, CAVE }, { 0x1C558, 17, CAVE + 20 },
                                                                             { 0x1D20C, 26, CAVE + 40 }, { 0x2A888, 4, 0x3DB4 } };
        for (int s = 0; s < 4; s++)
        {
          const UINT32 c = ns[s].cave, at = ns[s].at;
          hp.push_back({ at, 0x5463083Cu, 0x48000000u | ((c - at) & 0x03FFFFFCu) });            // slwi r3,r3,1 -> b cave
          hp.push_back({ at + 4, 0x3C800009u, 0x3C800000u });                                  // lis r4,9 -> lis r4,0
          hp.push_back({ at + 8, 0x3884F9CCu, 0x38840000u | TBL });                             // addi r4,r4,-0x634 -> TBL
          // 11/14 col8: the colour byte itself is 0-7 now (same-character colours in pick order, any slot), so no
          // slot test any more (was: cmpwi rSlot,4 / blt +8 / addi r3,r3,4)
          (void) ns[s].slotReg;
          hp.push_back({ c + 0, 0, 0x60000000u });                                             // nop
          hp.push_back({ c + 4, 0, 0x60000000u });                                             // nop
          hp.push_back({ c + 8, 0, 0x60000000u });                                             // nop
          hp.push_back({ c + 12, 0, 0x5463083Cu });                                            // slwi r3,r3,1
          hp.push_back({ c + 16, 0, 0x48000000u | ((at + 4 - (c + 16)) & 0x03FFFFFCu) });      // b back
        }
        if (applyPatches("SpikeoutHudColors", hp.data(), hp.size()))
          InfoLog("HudColors: ally names of slots 4-7 in new colours (ids 0xF6-0xF9, image %u bytes).", size);
      }
    }
    // SameStart (default on): every character starts the prologue in the same room. The prologue intro
    // (area 95) picks its spawn spots per character from a 4-pointer table at 0xFFED4A1C (0284 command, one
    // 160-byte block of 4 spots - x, y, z, facing - per character, each block in its own room). All four
    // pointers -> character 1's block, whose spots are moved to one chosen point: everyone appears there.
    // Done in ROM so every cabinet's game places them itself; moving players at run time split the link
    // session. MUST be the same on all 8 cabinets (the exe default keeps it so).
    static const Spikeout8LinkPatch s_spikeoutSameStartPatches[] =
    {
      { 0xD4A20, 0x000D4ACC, 0x000D4A2C },  // character 2 spawn block -> character 1's (55.3, 13, 31.5 ...)
      { 0xD4A24, 0x000D4B6C, 0x000D4A2C },  // character 3 spawn block -> character 1's
      { 0xD4A28, 0x000D4C0C, 0x000D4A2C },  // character 4 spawn block -> character 1's
      // ...and character 1's 4 spots moved onto the last waypoint recorded in area 75 (54.627, 12.646,
      // 47.928; camar 1), 0.6 apart so the four don't stack exactly (facing words left as they were)
      { 0xD4A30, 0x425D3333, 0x425A820C },  // spot 0 x 55.300 -> 54.627
      { 0xD4A34, 0x41500000, 0x414A5604 },  // spot 0 y 13.000 -> 12.646
      { 0xD4A38, 0x41FC0000, 0x423FB646 },  // spot 0 z 31.500 -> 47.928
      { 0xD4A58, 0x42560000, 0x42581BA6 },  // spot 1 x 53.500 -> 54.027
      { 0xD4A5C, 0x41500000, 0x414A5604 },  // spot 1 y 13.000 -> 12.646
      { 0xD4A60, 0x41F80000, 0x423FB646 },  // spot 1 z 31.000 -> 47.928
      { 0xD4A80, 0x425D3333, 0x425A820C },  // spot 2 x 55.300 -> 54.627
      { 0xD4A84, 0x41500000, 0x414A5604 },  // spot 2 y 13.000 -> 12.646
      { 0xD4A88, 0x41F40000, 0x423D4FDF },  // spot 2 z 30.500 -> 47.328
      { 0xD4AA8, 0x42560000, 0x42581BA6 },  // spot 3 x 53.500 -> 54.027
      { 0xD4AAC, 0x41500000, 0x414A5604 },  // spot 3 y 13.000 -> 12.646
      { 0xD4AB0, 0x41F00000, 0x423D4FDF },  // spot 3 z 30.000 -> 47.328
    };
    if (false && linkApplied && m_config["SameStart"].ValueAsDefault<bool>(true))   // kept but disabled (11/07): each character starts in its own room again
      applyPatches("SpikeoutSameStart", s_spikeoutSameStartPatches, sizeof(s_spikeoutSameStartPatches) / sizeof(s_spikeoutSameStartPatches[0]));
    // BossHPx1=1 (11/13, temporary, default off): boss HP x1.0 whatever the number of players. The game raises a
    // boss's HP at spawn (list 0x5D03C / generator 0x5AFF0: class >= 4) by the player count capped at 4 (2p x1.25,
    // 3p x1.375, 4p x1.5), and Spikeout8Link.h's caves 0x3CA0 / 0x3CF0 go on for 5-8 (up to x2.0). Both scaling
    // entries jump straight to the store-less exit instead. MUST be the same on all 8 cabinets (link sync).
    if (linkApplied && m_config["BossHPx1"].ValueAsDefault<bool>(false))
    {
      UINT32 vList = *(UINT32 *) &crom[0x600000 + 0x5D048], vGen = *(UINT32 *) &crom[0x600000 + 0x5AFFC];
      bool okList = (vList == 0x88CDE180 || vList == 0x4BFA6C58);   // lbz r6,-0x1e80(r13) / b 0x3CA0 (8 players)
      bool okGen  = (vGen  == 0x886DE180 || vGen  == 0x4BFA8CF4);   // lbz r3,-0x1e80(r13) / b 0x3CF0 (8 players)
      if (okList && okGen)
      {
        const Spikeout8LinkPatch bossHpPatches[] =
        {
          { 0x5D048, vList, 0x48000048 },   // b 0x5D090: list spawn, HP as listed
          { 0x5AFFC, vGen,  0x48000048 },   // b 0x5B044: generator spawn, HP as listed
        };
        applyPatches("SpikeoutBossHPx1", bossHpPatches, sizeof(bossHpPatches) / sizeof(bossHpPatches[0]));
      }
      else ErrorLog("SpikeoutBossHPx1: unexpected code at 5D048 (%08X) / 5AFFC (%08X). Not applied.", vList, vGen);
      // 11/22 (owner): with BossHPx1 the damage a PLAYER takes is x1 too, whatever the number of players. Its scaling:
      // the callers 0x261A8 / 0x26250 (r4 = r16 = the damage) call 0x22834, which picks a routine from the table
      // 0x8B684 by the player count (0x106180, capped at 4: x1 / x1.25 / x1.5 / x1.75), then a second table (0x8B694,
      // by SDA -0x3FBC - left as it is). Spikeout8Link.h sends 5-8 players to the cave 0x3F44 (up to x2.75) instead.
      // Here: both callers back to 0x22834, and its count read (lbz r3,-0x1e80(r13)) -> li r3,1 = the x1 entry.
      UINT32 vC1 = *(UINT32 *) &crom[0x600000 + 0x261A8], vC2 = *(UINT32 *) &crom[0x600000 + 0x26250], vCnt = *(UINT32 *) &crom[0x600000 + 0x22848];
      if ((vC1 == 0x4BFFC68D || vC1 == 0x4BFDDD9D) && (vC2 == 0x4BFFC5E5 || vC2 == 0x4BFDDCF5) && vCnt == 0x886DE180)
      {
        const Spikeout8LinkPatch dmgPatches[] =
        {
          { 0x261A8, vC1, 0x4BFFC68D },   // bl 0x22834 (not the 5-8 cave)
          { 0x26250, vC2, 0x4BFFC5E5 },   // bl 0x22834
          { 0x22848, vCnt, 0x38600001 },  // li r3, 1: the 1-player scale (x1)
        };
        applyPatches("SpikeoutPlayerDamageX1", dmgPatches, sizeof(dmgPatches) / sizeof(dmgPatches[0]));
      }
      else ErrorLog("SpikeoutPlayerDamageX1: unexpected code at 261A8 (%08X) / 26250 (%08X) / 22848 (%08X). Not applied.", vC1, vC2, vCnt);
    }
    // PlayerAttackMul=N (2..32767, default 1 = off): players' damage to enemies x N. The game's damage routine
    // 0x225B8 (r14 = victim Rob, r4 = damage; HP at +0x3F02 -= r4) is entered with the attacker's slot already
    // in victim+0x1BD0 (stored at hit time, 0x203AC / 0x26118 / 0x2865C). Its first compare (0x225E0) jumps to a
    // cave that multiplies r4 when the victim is an enemy (slot +0x72 >= player count 0x10065A) and the
    // attacker a player (0 <= +0x1BD0 < player count). Done in ROM so every cabinet computes the same damage:
    // MUST be the same on all 8 cabinets.
    // Not in the final area (Michael's rooftop, area 53): the boss fight stays at normal damage (11/08).
    #define SPIKEOUT_ATKMUL_OFF_AREA 53u
    int atkMul = m_config["PlayerAttackMul"].ValueAsDefault<int>(1);
    if (atkMul > 32767) atkMul = 32767;
    if (linkApplied && atkMul >= 2)
    {
      const Spikeout8LinkPatch atkMulPatches[] =
      {
        { 0x225E0, 0x2C840014, 0x4BFE1820 },  // cmpwi cr1,r4,0x14 -> b 0x3E00
        { 0x03E00, 0x00000000, 0x3C600010 },  // lis   r3,0x10
        { 0x03E04, 0x00000000, 0x8863065A },  // lbz   r3,0x65A(r3)      player count
        { 0x03E08, 0x00000000, 0x880E0072 },  // lbz   r0,0x72(r14)      victim slot
        { 0x03E0C, 0x00000000, 0x7C001800 },  // cmpw  r0,r3
        { 0x03E10, 0x00000000, 0x41800030 },  // blt   0x3E40            victim is a player: as is
        { 0x03E14, 0x00000000, 0x880E1BD0 },  // lbz   r0,0x1BD0(r14)    attacker slot
        { 0x03E18, 0x00000000, 0x7C000774 },  // extsb r0,r0
        { 0x03E1C, 0x00000000, 0x2C000000 },  // cmpwi r0,0
        { 0x03E20, 0x00000000, 0x41800020 },  // blt   0x3E40            no attacker
        { 0x03E24, 0x00000000, 0x7C001800 },  // cmpw  r0,r3
        { 0x03E28, 0x00000000, 0x40800018 },  // bge   0x3E40            attacker is an enemy
        { 0x03E2C, 0x00000000, 0x3C600010 },  // lis   r3,0x10
        { 0x03E30, 0x00000000, 0x8863402D },  // lbz   r3,0x402D(r3)     area id (0x10402D)
        { 0x03E34, 0x00000000, 0x2C030000u | SPIKEOUT_ATKMUL_OFF_AREA },   // cmpwi r3,53   Michael's rooftop (final area)
        { 0x03E38, 0x00000000, 0x41820008 },  // beq   0x3E40            there: x1 (11/08)
        { 0x03E3C, 0x00000000, 0x1C840000u | (UINT32) atkMul },   // mulli r4,r4,N
        { 0x03E40, 0x00000000, 0x2C840014 },  // cmpwi cr1,r4,0x14       (the replaced instruction)
        { 0x03E44, 0x00000000, 0x4801E7A0 },  // b     0x225E4
      };
      applyPatches("SpikeoutPlayerAttackMul", atkMulPatches, sizeof(atkMulPatches) / sizeof(atkMulPatches[0]));
    }
    // EnemyKnockMul=X (float, default 1.0 = off, 11/10): enemies fly X times as far / high when hit. The hit physics
    // at 0x266AC (victim Rob r15): mass f3 = weight(+0x58) x 0.15 + 20.25, f13 = power f12 / f3, v = sqrt(f13)
    // (0xC494), then Vy = v sin(t) -> +0x88, Vxz = v cos(t) -> +0x84 / +0x8C. Right after the sqrt (0x266D0, fmr
    // f13,f1) a cave multiplies v by X (a float kept at 0x3EF0) when the victim is an enemy (slot +0x72 >= player
    // count 0x10065A; players 5-8 sit at 0x1B0000 in 8-link, so the slot is compared, not the address). f0 is free
    // there (between two calls). Players are hit as before. MUST be the same on all 8 cabinets.
    // 11/23 (owner): EnemyKnockMulY / EnemyKnockMulXZ split it: Y = up (Vy), XZ = sideways (Vxz). Each defaults to
    // EnemyKnockMul. v is multiplied by XZ (the cave below), and Vy is multiplied again by Y / XZ right at its store
    // (0x266F4 stfs f5,0x88(r15) -> cave 0x3E90; r3 / r0 / f0 are free there: r3 is set at 0x266F8, r0 at 0x26710).
    float knockMul = m_config["EnemyKnockMul"].ValueAsDefault<float>(1.0f);
    if (!(knockMul > 0.0f && knockMul <= 100.0f)) knockMul = 1.0f;
    float knockMulY = m_config["EnemyKnockMulY"].ValueAsDefault<float>(knockMul);
    float knockMulXZ = m_config["EnemyKnockMulXZ"].ValueAsDefault<float>(knockMul);
    if (!(knockMulY > 0.0f && knockMulY <= 100.0f)) knockMulY = knockMul;
    if (!(knockMulXZ > 0.0f && knockMulXZ <= 100.0f)) knockMulXZ = knockMul;
    if (linkApplied && knockMulY != knockMulXZ)
    {
      float ratio = knockMulY / knockMulXZ;
      UINT32 ratioBits; memcpy(&ratioBits, &ratio, 4);
      const Spikeout8LinkPatch knockYPatches[] =
      {
        { 0x266F4, 0xD0AF0088, 0x4BFDD79C },  // stfs f5,0x88(r15) -> b 0x3E90
        { 0x03E90, 0x00000000, 0x3C600010 },  // lis   r3,0x10
        { 0x03E94, 0x00000000, 0x8863065A },  // lbz   r3,0x65A(r3)      player count
        { 0x03E98, 0x00000000, 0x880F0072 },  // lbz   r0,0x72(r15)      victim slot
        { 0x03E9C, 0x00000000, 0x7C001800 },  // cmpw  r0,r3
        { 0x03EA0, 0x00000000, 0x4180000C },  // blt   0x3EAC            a player: as is
        { 0x03EA4, 0x00000000, 0xC0003EB8 },  // lfs   f0,0x3EB8(0)      Y / XZ
        { 0x03EA8, 0x00000000, 0xECA50032 },  // fmuls f5,f5,f0          an enemy: Vy x Y / XZ
        { 0x03EAC, 0x00000000, 0xD0AF0088 },  // stfs  f5,0x88(r15)      (the replaced instruction)
        { 0x03EB0, 0x00000000, 0x48022848 },  // b     0x266F8
        { 0x03EB8, 0x00000000, ratioBits },   // .float Y / XZ
      };
      applyPatches("SpikeoutEnemyKnockMulY", knockYPatches, sizeof(knockYPatches) / sizeof(knockYPatches[0]));
    }
    if (linkApplied && knockMulY != 1.0f && knockMulXZ == 1.0f)
      InfoLog("SpikeoutEnemyKnockMul: enemies knocked up x%.2f, sideways x1.00.", knockMulY);
    knockMul = knockMulXZ;
    if (linkApplied && knockMul != 1.0f)
    {
      UINT32 knockBits; memcpy(&knockBits, &knockMul, 4);
      const Spikeout8LinkPatch knockPatches[] =
      {
        { 0x266D0, 0xFDA00890, 0x4BFDD7F0 },  // fmr f13,f1 -> b 0x3EC0
        { 0x03EC0, 0x00000000, 0x3C600010 },  // lis   r3,0x10
        { 0x03EC4, 0x00000000, 0x8863065A },  // lbz   r3,0x65A(r3)      player count
        { 0x03EC8, 0x00000000, 0x880F0072 },  // lbz   r0,0x72(r15)      victim slot
        { 0x03ECC, 0x00000000, 0x7C001800 },  // cmpw  r0,r3
        { 0x03ED0, 0x00000000, 0x4180000C },  // blt   0x3EDC            a player: as is
        { 0x03ED4, 0x00000000, 0xC0003EF0 },  // lfs   f0,0x3EF0(0)      X
        { 0x03ED8, 0x00000000, 0xEC210032 },  // fmuls f1,f1,f0          an enemy: v x X
        { 0x03EDC, 0x00000000, 0xFDA00890 },  // fmr   f13,f1            (the replaced instruction)
        { 0x03EE0, 0x00000000, 0x480227F4 },  // b     0x266D4
        { 0x03EF0, 0x00000000, knockBits },   // .float X
      };
      if (applyPatches("SpikeoutEnemyKnockMul", knockPatches, sizeof(knockPatches) / sizeof(knockPatches[0])))
        InfoLog("SpikeoutEnemyKnockMul: enemies knocked up x%.2f, sideways x%.2f.", knockMulY, knockMulXZ);
    }
    // 11/26 (owner: "enemies bounce when they hit the ground - make them bounce higher"): EnemyBounceMul=X (float,
    // default 1.0 = off). The bounce is not velocity (Vy is 0 on the ground): it is the down motion's own root height
    // (Rob +0x2408, local, e.g. motion 0946 lies at 0.12 and bounces to 0.90 / 0.65 / 0.30; the Rob's y is the ground
    // + this). It is stored at 0x63570 and 0x65CC8 (stfs f16,0x2408(r14), right after the root is worked out by
    // 0x6392C); both become bl 0xA04. For an enemy (slot +0x72 >= player count) in a motion-driven action (+0x38 = 7)
    // with down (mot_kind 0x00020000), the lowest root height of this motion so far (kept per slot at 0xB04 + slot x 8:
    // +0 float, +4 motion number; -1 = none) is the floor, and the height above it is multiplied by X:
    // y = min + (y - min) x X. So it never pops at the start (a fall from standing starts high) and lands where it
    // did. r0 / r3 / r4 / f0-f2 are free at both sites (written before read after them; LR is kept in r2 there).
    // The code and the table sit in the unused reserved exception vectors 0xA00 / 0xB00. MUST be the same on all
    // cabinets.
    float bounceMul = m_config["EnemyBounceMul"].ValueAsDefault<float>(1.0f);
    if (!(bounceMul > 0.0f && bounceMul <= 20.0f)) bounceMul = 1.0f;
    // 11/26 (owner): EnemyBounceMulChest=Xs (float) - the chest / head landings' multiplier (see below); left out (or out of
    // 0..40): 1 + (X - 1) x 5. It works on its own too (EnemyBounceMul 1.0: only the chest / head landings, walls as before).
    float smallX = m_config["EnemyBounceMulChest"].ValueAsDefault<float>(-1.0f);
    if (!(smallX > 0.0f && smallX <= 40.0f)) smallX = bounceMul > 1.0f ? 1.0f + (bounceMul - 1.0f) * 5.0f : bounceMul;
    if (linkApplied && (bounceMul != 1.0f || smallX != 1.0f))
    {
      UINT32 bounceBits; memcpy(&bounceBits, &bounceMul, 4);
      const float wallE = 1.0f + (bounceMul > 1.0f ? bounceMul - 1.0f : 0.0f);   // 1 + e
      UINT32 wallBits; memcpy(&wallBits, &wallE, 4);
      // 11/26 (owner: "they bounce off the back, not off the chest / head" - DownLog, run00 area 75): two other landings.
      // (a) still in the flying motion (action 6: 411, 150) the enemy hits the floor and bounces a little (0.13 -> 0.30);
      // only action 7 was scaled. Now these two motions (action 6, down) too, but only from the frame their root height is
      // between 0.05 and 0.3 (on the floor; the flight's own arc, 0.76 -> 1.22 in 411, is left alone). By motion number:
      // other action 6 motions are launches from the floor (1076 / 1092 / 1662 start at 0.12 and fly to 4-13 when scaled).
      // (b) the landing motions 407 / 415 bounce only ~0.1 (0.14 -> 0.24), so x X hardly shows. These two kinds get
      // Xs = EnemyBounceMulChest, else 1 + (X - 1) x 5 (X 2.0 -> 6.0: 0.1 -> 0.6, the back landing 2374's 0.78 -> 1.56). Marked in the upper half
      // of the record's +4 (the motion is compared on the lower half now). Code at 0xD04 (the trace vector, unused).
      const float touchHi = 0.3f, touchLo = 0.05f;
      UINT32 smallBits, touchHiBits, touchLoBits;
      memcpy(&smallBits, &smallX, 4); memcpy(&touchHiBits, &touchHi, 4); memcpy(&touchLoBits, &touchLo, 4);
      const Spikeout8LinkPatch bouncePatches[] =
      {
        { 0x63570, 0xD20E2408, 0x4BF9D495 },  // stfs f16,0x2408(r14) -> bl 0xA04
        { 0x65CC8, 0xD20E2408, 0x4BF9AD3D },  // stfs f16,0x2408(r14) -> bl 0xA04
        { 0x00A04, 0x00000000, 0x3C600010 },  // lis   r3,0x10
        { 0x00A08, 0x00000000, 0x8863065A },  // lbz   r3,0x65A(r3)      player count
        { 0x00A0C, 0x00000000, 0x880E0072 },  // lbz   r0,0x72(r14)      slot
        { 0x00A10, 0x00000000, 0x7C001800 },  // cmpw  r0,r3
        { 0x00A14, 0x00000000, 0x41800064 },  // blt   0xA78             a player: as is
        { 0x00A18, 0x00000000, 0x54031838 },  // slwi  r3,r0,3
        { 0x00A1C, 0x00000000, 0x38630B04 },  // addi  r3,r3,0xB04       this slot's record
        { 0x00A20, 0x00000000, 0x880E0038 },  // lbz   r0,0x38(r14)      action
        { 0x00A24, 0x00000000, 0x2C000007 },  // cmpwi r0,7              motion-driven?
        { 0x00A28, 0x00000000, 0x408202DC },  // bne   0xD04             (11/26: not 7 -> the action 6 check, was bne 0xA70)
        { 0x00A2C, 0x00000000, 0x800E0044 },  // lwz   r0,0x44(r14)      mot_kind
        { 0x00A30, 0x00000000, 0x74000002 },  // andis. r0,r0,2          down?
        { 0x00A34, 0x00000000, 0x4182003C },  // beq   0xA70
        { 0x00A38, 0x00000000, 0xA00E003C },  // lhz   r0,0x3C(r14)      motion
        { 0x00A3C, 0x00000000, 0xA0830006 },  // lhz   r4,6(r3)          (11/26: the motion, lower half - was lwz r4,4(r3))
        { 0x00A40, 0x00000000, 0x7C002000 },  // cmpw  r0,r4
        { 0x00A44, 0x00000000, 0x4182000C },  // beq   0xA50
        { 0x00A48, 0x00000000, 0x48000314 },  // b     0xD5C             a new motion (11/26: was stw r0,4(r3); 0xD5C stores it, min, mark)
        { 0x00A4C, 0x00000000, 0xD2030000 },  // stfs  f16,0(r3)         min = y
        { 0x00A50, 0x00000000, 0xC0030000 },  // lfs   f0,0(r3)
        { 0x00A54, 0x00000000, 0xEC300028 },  // fsubs f1,f16,f0
        { 0x00A58, 0x00000000, 0xFC01802E },  // fsel  f0,f1,f0,f16      min = min(min, y)
        { 0x00A5C, 0x00000000, 0xD0030000 },  // stfs  f0,0(r3)
        { 0x00A60, 0x00000000, 0xEC300028 },  // fsubs f1,f16,f0         y - min
        { 0x00A64, 0x00000000, 0x4800031C },  // b     0xD80             X or Xs (11/26: was lfs f2,0xA80(0))
        { 0x00A68, 0x00000000, 0xEE0100BA },  // fmadds f16,f1,f2,f0     y = min + (y - min) x X
        { 0x00A6C, 0x00000000, 0x4800000C },  // b     0xA78
        { 0x00A70, 0x00000000, 0x3800FFFF },  // li    r0,-1             not bouncing: forget the motion
        { 0x00A74, 0x00000000, 0x90030004 },  // stw   r0,4(r3)
        { 0x00A78, 0x00000000, 0xD20E2408 },  // stfs  f16,0x2408(r14)   (the replaced instruction)
        { 0x00A7C, 0x00000000, 0x4E800020 },  // blr
        { 0x00A80, 0x00000000, bounceBits },  // .float X
        // 11/26: chest / head landings (see smallX above)
        { 0x00D04, 0x00000000, 0x2C000006 },  // cmpwi r0,6              flying (action 6)?
        { 0x00D08, 0x00000000, 0x4082FD68 },  // bne   0xA70             no: forget the motion
        { 0x00D0C, 0x00000000, 0x800E0044 },  // lwz   r0,0x44(r14)      mot_kind
        { 0x00D10, 0x00000000, 0x74000002 },  // andis. r0,r0,2          down?
        { 0x00D14, 0x00000000, 0x4182FD5C },  // beq   0xA70
        { 0x00D18, 0x00000000, 0xA00E003C },  // lhz   r0,0x3C(r14)      motion
        { 0x00D1C, 0x00000000, 0x2C00019B },  // cmpwi r0,411            only the flights that land and bounce in themselves: 411 / 150
        { 0x00D20, 0x00000000, 0x4182000C },  // beq   0xD2C
        { 0x00D24, 0x00000000, 0x2C000096 },  // cmpwi r0,150
        { 0x00D28, 0x00000000, 0x4082FD48 },  // bne   0xA70             (a launch from the floor, 1076 / 1092 / 1662, must not be scaled)
        { 0x00D2C, 0x00000000, 0xA0830006 },  // lhz   r4,6(r3)
        { 0x00D30, 0x00000000, 0x7C002000 },  // cmpw  r0,r4
        { 0x00D34, 0x00000000, 0x4182FD1C },  // beq   0xA50             on the floor already in this motion: scale
        { 0x00D38, 0x00000000, 0xC0000DA4 },  // lfs   f0,0xDA4(0)       0.3
        { 0x00D3C, 0x00000000, 0xFC100000 },  // fcmpu cr0,f16,f0
        { 0x00D40, 0x00000000, 0x4080FD30 },  // bge   0xA70             still in the air
        { 0x00D44, 0x00000000, 0xC0000DA8 },  // lfs   f0,0xDA8(0)       0.05
        { 0x00D48, 0x00000000, 0xFC100000 },  // fcmpu cr0,f16,f0
        { 0x00D4C, 0x00000000, 0x4180FD24 },  // blt   0xA70             below the floor line (a one-frame dip): not yet
        { 0x00D50, 0x00000000, 0x90030004 },  // stw   r0,4(r3)          touched down: min = y, small-bounce mark
        { 0x00D54, 0x00000000, 0xD2030000 },  // stfs  f16,0(r3)
        { 0x00D58, 0x00000000, 0x4800001C },  // b     0xD74
        { 0x00D5C, 0x00000000, 0x90030004 },  // stw   r0,4(r3)          (from 0xA48) a new landing motion: min = y
        { 0x00D60, 0x00000000, 0xD2030000 },  // stfs  f16,0(r3)
        { 0x00D64, 0x00000000, 0x2C000197 },  // cmpwi r0,407            the small-bounce landings 407 / 415
        { 0x00D68, 0x00000000, 0x4182000C },  // beq   0xD74
        { 0x00D6C, 0x00000000, 0x2C00019F },  // cmpwi r0,415
        { 0x00D70, 0x00000000, 0x4082FCE0 },  // bne   0xA50
        { 0x00D74, 0x00000000, 0x38000001 },  // li    r0,1
        { 0x00D78, 0x00000000, 0xB0030004 },  // sth   r0,4(r3)          small-bounce mark (upper half of +4)
        { 0x00D7C, 0x00000000, 0x4BFFFCD4 },  // b     0xA50
        { 0x00D80, 0x00000000, 0xA0830004 },  // lhz   r4,4(r3)          (from 0xA64) the mark
        { 0x00D84, 0x00000000, 0xC0400A80 },  // lfs   f2,0xA80(0)       X
        { 0x00D88, 0x00000000, 0x2C040000 },  // cmpwi r4,0
        { 0x00D8C, 0x00000000, 0x4182FCDC },  // beq   0xA68
        { 0x00D90, 0x00000000, 0xC0400DA0 },  // lfs   f2,0xDA0(0)       small bounce: Xs
        { 0x00D94, 0x00000000, 0x4BFFFCD4 },  // b     0xA68
        { 0x00DA0, 0x00000000, smallBits },   // .float Xs
        { 0x00DA4, 0x00000000, touchHiBits }, // .float 0.3
        { 0x00DA8, 0x00000000, touchLoBits }, // .float 0.05
      };
      if (applyPatches("SpikeoutEnemyBounceMul", bouncePatches, sizeof(bouncePatches) / sizeof(bouncePatches[0])))
        InfoLog("SpikeoutEnemyBounceMul: enemies bounce x%.2f (chest / head landings x%.2f).", bounceMul, smallX);
      // the walls only with EnemyBounceMul (11/26: split out so EnemyBounceMulChest alone leaves the walls as they were)
      const Spikeout8LinkPatch wallPatches[] =
      {
        // 11/26 (owner: "they bounce on the floor but not when the head hits a wall - bounce off the wall too"): a flying
        // Rob that touches a wall goes through 0x50594 once per flight (+0x48 bit 31): head or back to the wall -> a
        // wall-hit motion (0x404F / 0x4096 / 0x4097, it sticks and slides down), else Vx / Vz x 0.4. For an enemy moving
        // into the wall, the horizontal velocity is reflected instead, v -= (1 + e) (v.p / p.p) p with p the push-out
        // from the wall (+0x1D28 / +0x1D2C, worked out at 0x51150) and e = X - 1 (2.0: as fast as it came, 1.5: half),
        // and the wall-hit motion is skipped (on to 0x5070C, r31 stays 0, so the flight goes on; also when it is not
        // moving into the wall, e.g. sliding along it: no reflection then, but no sticking either). r0 / r3 / f0-f7 are
        // free there (the callers 0x200D0 / 0x20104 / 0x20758 use no FP register after it).
        { 0x50608, 0x56009801, 0x4BFB047C },  // rlwinm. r0,r16,19,0,0 -> b 0xA84   (11/26 wall)
        { 0x00A84, 0x00000000, 0x3C600010 },  // lis r3, 0x10
        { 0x00A88, 0x00000000, 0x8863065A },  // lbz r3, 0x65a(r3)
        { 0x00A8C, 0x00000000, 0x880E0072 },  // lbz r0, 0x72(r14)
        { 0x00A90, 0x00000000, 0x7C001800 },  // cmpw r0, r3
        { 0x00A94, 0x00000000, 0x41800058 },  // blt 0xaec               a player: as is
        { 0x00A98, 0x00000000, 0xC02E1D28 },  // lfs f1, 0x1d28(r14)
        { 0x00A9C, 0x00000000, 0xC04E1D2C },  // lfs f2, 0x1d2c(r14)
        { 0x00AA0, 0x00000000, 0xEC610072 },  // fmuls f3, f1, f1        p.p (p = the push-out from the wall, +0x1D28 / +0x1D2C)
        { 0x00AA4, 0x00000000, 0xEC6218BA },  // fmadds f3, f2, f2, f3
        { 0x00AA8, 0x00000000, 0xC0001500 },  // lfs f0, 0x1500(0)
        { 0x00AAC, 0x00000000, 0xFC030000 },  // fcmpu cr0, f3, f0
        { 0x00AB0, 0x00000000, 0x4081003C },  // ble 0xaec               no push-out: as is
        { 0x00AB4, 0x00000000, 0xC08E001C },  // lfs f4, 0x1c(r14)
        { 0x00AB8, 0x00000000, 0xC0AE0024 },  // lfs f5, 0x24(r14)
        { 0x00ABC, 0x00000000, 0xECC40072 },  // fmuls f6, f4, f1        v.p (v = Vx / Vz, +0x1C / +0x24)
        { 0x00AC0, 0x00000000, 0xECC530BA },  // fmadds f6, f5, f2, f6
        { 0x00AC4, 0x00000000, 0xFC060000 },  // fcmpu cr0, f6, f0
        { 0x00AC8, 0x00000000, 0x40800020 },  // bge 0xae8               not moving into the wall: no reflection, but no wall-hit motion either
        { 0x00ACC, 0x00000000, 0xECC61824 },  // fdivs f6, f6, f3
        { 0x00AD0, 0x00000000, 0xC0E00AF4 },  // lfs f7, 0xaf4(0)        1 + e
        { 0x00AD4, 0x00000000, 0xECC601F2 },  // fmuls f6, f6, f7
        { 0x00AD8, 0x00000000, 0xEC86207C },  // fnmsubs f4, f6, f1, f4  v -= (1 + e) (v.p / p.p) p
        { 0x00ADC, 0x00000000, 0xECA628BC },  // fnmsubs f5, f6, f2, f5
        { 0x00AE0, 0x00000000, 0xD08E001C },  // stfs f4, 0x1c(r14)
        { 0x00AE4, 0x00000000, 0xD0AE0024 },  // stfs f5, 0x24(r14)
        { 0x00AE8, 0x00000000, 0x4804FC24 },  // b 0x5070c               past the wall-hit motion (r31 stays 0)
        { 0x00AEC, 0x00000000, 0x56009801 },  // rlwinm. r0, r16, 0x13, 0, 0(the replaced instruction)
        { 0x00AF0, 0x00000000, 0x4804FB1C },  // b 0x5060c
        { 0x00AF4, 0x00000000, wallBits },  // .float 1 + e
      };
      if (bounceMul != 1.0f && applyPatches("SpikeoutEnemyBounceWall", wallPatches, sizeof(wallPatches) / sizeof(wallPatches[0])))
        InfoLog("SpikeoutEnemyBounceWall: enemies bounce off walls, e = %.2f.", wallE - 1.0f);
    }
    // 11/23 (owner): InfiniteSpecial=1 - the special attack does not use up the lightning (SP, Rob +0x3F25). The only
    // place that takes one away is 0x21FBC..0x21FC8 in the motion-command loop (SP - 1, unless SDA -0x4000 & 0x3000
    // or SP is 0); its store is made a nop. SP is kept per Rob on each cabinet, so it MUST be the same on all cabinets.
    if (m_config["InfiniteSpecial"].ValueAsDefault<bool>(false))
    {
      const Spikeout8LinkPatch infSpPatches[] =
      {
        { 0x21FC8, 0x988E3F25, 0x60000000 },  // stb r4,0x3f25(r14) (SP - 1) -> nop
      };
      if (applyPatches("SpikeoutInfiniteSpecial", infSpPatches, sizeof(infSpPatches) / sizeof(infSpPatches[0])))
        InfoLog("SpikeoutInfiniteSpecial: the special attack does not use up SP.");
    }
    // 11/23 (owner): SpikeJr=1 crashed at the stage start ("CMODEL3 : Unknown R16 (PC=0x5F2A8)", the map was the
    // wrong stage). The stage-start script commands index tables of 4 characters (Spike, White, Linda, Tenshin) with
    // Rob +0x3F00: 0x5E6F0 (start positions, the table at script arg +4 = r17; 0x5E83C / 0x5E878) and 0x5E8F4 (map
    // icon 0xA / 3 + character, 0x5E90C). Jr (4) read past them. These three loads now read the character & 3
    // (4 -> 0 = Spike, 0-3 as is) through tiny caves (bl: both functions keep LR on the stack; cr0 is not used after).
    if (g_spikeoutSpikeJr)
    {
      // (11/23: the costume table 0xFFF6C878 has entries for characters 0-3 only; [4] = 0. Jr has no costume of his own.)
      const Spikeout8LinkPatch jrPatches[] =
      {
        { 0x5E83C, 0x88A53F00, 0x4BFA560D },  // lbz r5,0x3f00(r5) -> bl 0x3E48
        { 0x5E878, 0x88A53F00, 0x4BFA55D1 },  // lbz r5,0x3f00(r5) -> bl 0x3E48
        { 0x5E90C, 0x88833F00, 0x4BFA55D9 },  // lbz r4,0x3f00(r3) -> bl 0x3EE4
        { 0x03E48, 0x00000000, 0x88A53F00 },  // lbz   r5,0x3f00(r5)
        { 0x03E4C, 0x00000000, 0x70A50003 },  // andi. r5,r5,3
        { 0x03E50, 0x00000000, 0x4E800020 },  // blr
        { 0x03EE4, 0x00000000, 0x88833F00 },  // lbz   r4,0x3f00(r3)
        { 0x03EE8, 0x00000000, 0x70840003 },  // andi. r4,r4,3
        { 0x03EEC, 0x00000000, 0x4E800020 },  // blr
        // 11/23 (owner): every Jr in the 1P colour. The costume loader 0x733DC gets the colour from 0x7347C (r3,
        // 0-7 by pick order, kept at fa_player +0xA0 + slot) and loads table[character][colour] (bl 0x3C44). Right
        // after it, the colour is made 0 when the link character (+0x25 of the node record r18) is Spike (0):
        // r3 &= (char == 0 ? 0 : -1). fa_player +0xA0 is left alone, so the name colours stay different.
        { 0x73430, 0x88920025, 0x4BF902F1 },  // lbz r4,0x25(r18) -> bl 0x3720
        { 0x03720, 0x00000000, 0x88920025 },  // lbz   r4,0x25(r18)    (the replaced instruction)
        { 0x03724, 0x00000000, 0x7C0400D0 },  // neg   r0,r4
        { 0x03728, 0x00000000, 0x7C00FE70 },  // srawi r0,r0,31         Spike: 0, others: -1
        { 0x0372C, 0x00000000, 0x7C630038 },  // and   r3,r3,r0
        { 0x03730, 0x00000000, 0x4E800020 },  // blr
        // The same for the one-player costume load at a (re)spawn (0x5B3E0 bl 0x7347C, character r24).
        { 0x5B3E4, 0x38980000, 0x4BFA8351 },  // addi r4,r24,0 -> bl 0x3734
        { 0x03734, 0x00000000, 0x38980000 },  // addi  r4,r24,0         (the replaced instruction)
        { 0x03738, 0x00000000, 0x7C0400D0 },  // neg   r0,r4
        { 0x0373C, 0x00000000, 0x7C00FE70 },  // srawi r0,r0,31
        { 0x03740, 0x00000000, 0x7C630038 },  // and   r3,r3,r0
        { 0x03744, 0x00000000, 0x4E800020 },  // blr
        // 11/23 (owner, 8 links): right after a join everyone was Spike (the model is made when the Rob is set up;
        // the once-a-frame 0 -> 4 came too late) until a gate scene made the models again. The two places that set
        // a player's character from the link (+0x25) now give 4 for Spike:
        // game start / join 0x7338C (stb r4,0x3f00(r5)), and a (re)spawn 0x5B3AC (the set-up record +0x14; r24
        // itself stays 0, it picks the costume and is compared with the old character just after).
        { 0x7338C, 0x98853F00, 0x4BF903BC },  // stb r4,0x3f00(r5) -> b 0x3748
        { 0x03748, 0x00000000, 0x2C040000 },  // cmpwi r4,0
        { 0x0374C, 0x00000000, 0x40820008 },  // bne   +8
        { 0x03750, 0x00000000, 0x38800004 },  // li    r4,4               Spike -> Jr
        { 0x03754, 0x00000000, 0x98853F00 },  // stb   r4,0x3f00(r5)      (the replaced instruction)
        { 0x03758, 0x00000000, 0x4806FBEC },  // b     0x73344            (the original next instruction)
        { 0x5B3AC, 0x9B030014, 0x4BFA826C },  // stb r24,0x14(r3) -> b 0x3618
        { 0x03618, 0x00000000, 0x2C180000 },  // cmpwi r24,0
        { 0x0361C, 0x00000000, 0x38000004 },  // li    r0,4
        { 0x03620, 0x00000000, 0x41820008 },  // beq   +8
        { 0x03624, 0x00000000, 0x7F00C378 },  // mr    r0,r24
        { 0x03628, 0x00000000, 0x98030014 },  // stb   r0,0x14(r3)
        { 0x0362C, 0x00000000, 0x48057D84 },  // b     0x5B3B0
        // 11/23 (8 links): players who joined later were still Spike. The join set-up record (0x73670, r18 = the
        // record, +0x14 = character from the link) is the third place: Spike -> 4 here too.
        { 0x73718, 0x98920014, 0x4BF8FB24 },  // stb r4,0x14(r18) -> b 0x323C
        { 0x0323C, 0x00000000, 0x2C040000 },  // cmpwi r4,0
        { 0x03240, 0x00000000, 0x40820008 },  // bne   +8
        { 0x03244, 0x00000000, 0x38800004 },  // li    r4,4
        { 0x03248, 0x00000000, 0x98920014 },  // stb   r4,0x14(r18)     (the replaced instruction)
        { 0x0324C, 0x00000000, 0x480704D0 },  // b     0x7371C
        // 11/23 (owner, 8 links): only 01's Jr looked right. Jr has one texture of his own (one colour) at slot 0's
        // place; a player model is drawn with a per-slot texture offset (the 8-slot table 0x3608 by Rob +4 = slot,
        // 0x34EF4 / 0x36130, and slots 6-7 on page 0 in caves A / B 0x3964 / 0x3994), so slots 1-7 read other
        // costume texels. For a Jr (Rob +0x3F00 = 4) both now take slot 0: offset (0,0), page 1.
        { 0x34EF4, 0x886E0004, 0x4BFCECC9 },  // lbz r3,4(r14) -> bl 0x3BBC
        { 0x36130, 0x886E0004, 0x4BFCDA8D },  // lbz r3,4(r14) -> bl 0x3BBC
        { 0x03964, 0x880E0004, 0x48000271 },  // cave A: lbz r0,4(r14) -> bl 0x3BD4
        { 0x03994, 0x880E0004, 0x48000241 },  // cave B: lbz r0,4(r14) -> bl 0x3BD4
        { 0x03BBC, 0x00000000, 0x886E0004 },  // lbz   r3,4(r14)         slot
        { 0x03BC0, 0x00000000, 0x880E3F00 },  // lbz   r0,0x3f00(r14)    character
        { 0x03BC4, 0x00000000, 0x2C000004 },  // cmpwi r0,4
        { 0x03BC8, 0x00000000, 0x4C820020 },  // bnelr
        { 0x03BCC, 0x00000000, 0x38600000 },  // li    r3,0              Jr: slot 0's offset
        { 0x03BD0, 0x00000000, 0x4E800020 },  // blr
        { 0x03BD4, 0x00000000, 0x880E0004 },  // lbz   r0,4(r14)         slot
        { 0x03BD8, 0x00000000, 0x88AE3F00 },  // lbz   r5,0x3f00(r14)    character (r5 is free here)
        { 0x03BDC, 0x00000000, 0x2C050004 },  // cmpwi r5,4
        { 0x03BE0, 0x00000000, 0x4C820020 },  // bnelr
        { 0x03BE4, 0x00000000, 0x38000000 },  // li    r0,0              Jr: page 1 as slot 0
        { 0x03BE8, 0x00000000, 0x4E800020 },  // blr
      };
      applyPatches("SpikeoutSpikeJr", jrPatches, sizeof(jrPatches) / sizeof(jrPatches[0]));
    }
    // StartStage=S / StartArea=A (both 1..255, default 0 = off): when the prologue (stage 7 SPKHOME) ends, go to
    // stage S, area A instead of Diesel Town. The stage-change script command (group 1 op 0x17, 0x5CEC8: arg byte 3
    // = stage, byte 4 = area -> pending stage / area 0x10404B / 0x104077) is redirected while the current stage
    // (0x10402C) is 7, so the stage loads through the game's own stage-to-stage path. Starting a new game straight
    // in another stage (0x732B8) broke the screen and the AI (11/08). MUST be the same on all 8 cabinets.
    // Stages: 3 Diesel Town, 4 Roy's Mall, 5 Opera, 6 Shipyard.
    int startStage = m_config["StartStage"].ValueAsDefault<int>(0);
    int startArea = m_config["StartArea"].ValueAsDefault<int>(0);
    if (linkApplied && startStage >= 1 && startStage <= 255 && startArea >= 1 && startArea <= 255)
    {
      const Spikeout8LinkPatch startPatches[] =
      {
        { 0x5CEC8, 0x8A3F0003, 0x4BFA6F98 },  // lbz r17,3(r31) -> b 0x3E60
        { 0x5CEE8, 0x8A5F0004, 0x4BFA6F90 },  // lbz r18,4(r31) -> b 0x3E78
        { 0x03E60, 0x00000000, 0x8A3F0003 },  // lbz   r17,3(r31)        stage from the script
        { 0x03E64, 0x00000000, 0x880DC02C },  // lbz   r0,-0x3FD4(r13)   current stage (0x10402C)
        { 0x03E68, 0x00000000, 0x2C000007 },  // cmpwi r0,7              prologue?
        { 0x03E6C, 0x00000000, 0x40820008 },  // bne   0x3E74
        { 0x03E70, 0x00000000, 0x3A200000u | (UINT32) startStage },   // li r17,S
        { 0x03E74, 0x00000000, 0x48059058 },  // b     0x5CECC
        { 0x03E78, 0x00000000, 0x8A5F0004 },  // lbz   r18,4(r31)        area from the script
        { 0x03E7C, 0x00000000, 0x880DC02C },  // lbz   r0,-0x3FD4(r13)
        { 0x03E80, 0x00000000, 0x2C000007 },  // cmpwi r0,7
        { 0x03E84, 0x00000000, 0x40820008 },  // bne   0x3E8C
        { 0x03E88, 0x00000000, 0x3A400000u | (UINT32) startArea },    // li r18,A
        { 0x03E8C, 0x00000000, 0x48059060 },  // b     0x5CEEC
      };
      applyPatches("SpikeoutStartArea", startPatches, sizeof(startPatches) / sizeof(startPatches[0]));
    }
    // StageTestMenu=1 (default 0, 11/20, owner): the Test button opens the developers' test menu, at STAGE TEST.
    // The game runs on a major state (0x104008, next one in 0x10400A; names at 0x7EEF0, functions at 0xA0458 + n x 4):
    // 16/17 TEST_INT/DSP is the operator test menu, 18/19 DBG_TEST_INT (0x4C38C) / DBG_TEST_DSP (0x4C4E8) the
    // developers' one. DBG_TEST_DSP runs step 0x10400B through 0xB0DF0 (15 tests x {entry, every frame}); STAGE TEST
    // is steps 8/9 (0x68834 / 0x68884: mode 0x31/0x32, screen 0x183E0 / 0x18478). The Test handler (0x12064; with a
    // link it marks the own link record +0xC: bit 1 = DBG_TEST, 4 = TEST, and every cabinet acts on every record at
    // 0x1215C; without a link 0x12220) picks DBG_TEST when bit 0x00800000 of 0x104400 is set and no Service button
    // is held, but nothing in the retail ROM sets that bit (17 other places read it as "debug build"), so only these
    // two branches are opened:
    //   Test -> DBG_TEST at STAGE TEST. Service + Test -> the normal test menu.
    // In a link game all linked cabinets follow (as with the normal test menu).
    // On STAGE TEST: P1 lever right/up = STAGE REQ + 1, left/down = - 1 (0..15, names at 0x8B960), P1 Jump = load
    // that stage (0x10402C = REQ, 0x4F4E8: music + model data; the camera stays where the attract demo left it, so
    // open it on a 3D demo scene - on the "TEAM SPIKEOUT" logo the logo stays in front). MONO AREA (0x104070, names
    // 0xFFF74DD4): P1 Beat + 1, Charge - 1, Shift = load (moved from P2, see below). Start 2 held + P1 down / up:
    // the next / previous test (0x4C530). Checked 11/20 on bin64\host: STAGE TEST text, Jump on BASKET2 over a sea
    // demo scene shows the Diesel Town geometry, SUNSET (1) only turns the background orange.
    // One cabinet only: not on a streaming server (Streaming = 1).
    if (m_config["StageTestMenu"].ValueAsDefault<bool>(false))
    {
      if (m_config["Streaming"].ValueAsDefault<bool>(false))
        InfoLog("SpikeoutStageTest: not on a streaming server (Streaming = 1). Not applied.");
      else
      {
        const Spikeout8LinkPatch dbgTestPatches[] =
        {
          { 0x120B8, 0x4182005C, 0x60000000 },  // link: beq 0x12114 (debug bit clear -> record +0xC bit 4, TEST) -> nop
          { 0x12248, 0x41820034, 0x60000000 },  // no link: beq 0x1227C (debug bit clear -> TEST_INT) -> nop
          { 0x4C4CC, 0x38000000, 0x38000008 },  // DBG_TEST_INT (0x4C38C): first step 0 (unlabelled character test) -> 8 (STAGE TEST)
          // STAGE TEST's MONO AREA keys were on P2, which Spikeout does not wire (no joystick2): moved to P1 buttons
          { 0x6891C, 0x57C04801, 0x57C0B801 },  // MONO + 1: P2 right (0x400000) -> P1 Beat (0x100)
          { 0x68924, 0x57C05001, 0x57C0B801 },  // MONO + 1: P2 up (0x200000) -> P1 Beat
          { 0x68930, 0x57C04001, 0x57C0B001 },  // MONO - 1: P2 left (0x800000) -> P1 Charge (0x200)
          { 0x68938, 0x57C05801, 0x57C0B001 },  // MONO - 1: P2 down (0x100000) -> P1 Charge
          { 0x68968, 0x77C00008, 0x73C00400 },  // MONO load: andis. 0x80000 (P2 Jump) -> andi. 0x400 (P1 Shift)
        };
        applyPatches("SpikeoutStageTest", dbgTestPatches, sizeof(dbgTestPatches) / sizeof(dbgTestPatches[0]));
      }
    }
  }

  // Configure CPU and PCI bridge
  PPC_CONFIG  ppc_config;
  if (game.stepping == "2.0" || game.stepping == "2.1")
  {
    ppc_config.pvr = PPC_MODEL_603R;  // 166 MHz
    ppc_config.bus_frequency = BUS_FREQUENCY_66MHZ;
    ppc_config.bus_frequency_multiplier = 0x25; // 2.5X multiplier
    PCIBridge.SetModel(0x106);        // MPC106
  }
  else if (game.stepping == "1.5")
  {
    ppc_config.pvr = PPC_MODEL_603E;  // 100 MHz
    ppc_config.bus_frequency = BUS_FREQUENCY_66MHZ;
    ppc_config.bus_frequency_multiplier = 0x15; // 1.5X multiplier
    PCIBridge.SetModel(0x105);        // MPC105
  }
  else if (game.stepping == "1.0")
  {
    ppc_config.pvr = PPC_MODEL_603R;  // 66 MHz
    ppc_config.bus_frequency = BUS_FREQUENCY_66MHZ;
    ppc_config.bus_frequency_multiplier = 0x10; // 1X multiplier
    PCIBridge.SetModel(0x105);        // MPC105
  }
  else
  {
    ErrorLog("Cannot configure Model 3 because game uses unrecognized stepping (%s).", game.stepping.c_str());
    return Result::FAIL;
  }

  // Initialize CPU
  ppc_init(&ppc_config);
  ppc_attach_bus(this);
  PPCFetchRegions[0].start = 0;
  PPCFetchRegions[0].end = 0x007FFFFF;
  PPCFetchRegions[0].ptr = (UINT32 *) ram;
  PPCFetchRegions[1].start = 0xFF800000;
  PPCFetchRegions[1].end = 0xFFFFFFFF;
  PPCFetchRegions[1].ptr = (UINT32 *) crom;
  PPCFetchRegions[2].start = 0;
  PPCFetchRegions[2].end = 0;
  PPCFetchRegions[2].ptr = NULL;
  ppc_set_fetch(PPCFetchRegions);

  // Initialize Real3D
  m_stepping = ((game.stepping[0] - '0') << 4) | (game.stepping[2] - '0');
  GPU.SetStepping(m_stepping);
  m_jtag.SetStepping(m_stepping);

  // MPEG board (if present)
  if (rom_set.get_rom("mpeg_program").size)
  {
    if (game.mpeg_board == "DSB1")
    {
      DSB = new(std::nothrow) CDSB1(m_config);
      if (NULL == DSB)
        return ErrorLog("Insufficient memory for Digital Sound Board object.");
    }
    else if (game.mpeg_board == "DSB2")
    {
      Util::FlipEndian16(dsbROM, 128*1024); // 68K program needs to be byte swapped
      DSB = new(std::nothrow) CDSB2(m_config);
      if (NULL == DSB)
        return ErrorLog("Insufficient memory for Digital Sound Board object.");
    }
    else if (game.mpeg_board.empty())
      ErrorLog("No MPEG board type defined in game XML for MPEG ROMs.");
    else
      ErrorLog("Unknown MPEG board type '%s'. Only 'DSB1' and 'DSB2' are supported.", game.mpeg_board.c_str());
    if (DSB && Result::OKAY != DSB->Init(dsbROM, mpegROM))
      return Result::FAIL;
  }
  SoundBoard.AttachDSB(DSB);

  // Drive board (if present)
  if (game.driveboard_type == Game::DRIVE_BOARD_WHEEL && rom_set.get_rom("driveboard_program").size)
  {
    DriveBoard = new CWheelBoard(m_config);
    if (DriveBoard->Init(driveROM) != Result::OKAY)
      return Result::FAIL;
  }
  else if (game.driveboard_type == Game::DRIVE_BOARD_JOYSTICK && rom_set.get_rom("driveboard_program").size)
  {
    DriveBoard = new CJoyBoard(m_config);
    if (DriveBoard->Init(driveROM) != Result::OKAY)
      return Result::FAIL;
  }
  else if (game.driveboard_type == Game::DRIVE_BOARD_BILLBOARD && rom_set.get_rom("driveboard_program").size)
  {
    DriveBoard = new CBillBoard(m_config);
    if (DriveBoard->Init(driveROM) != Result::OKAY)
      return Result::FAIL;
  }
  else if (game.driveboard_type == Game::DRIVE_BOARD_SKI)
  {
    DriveBoard = new CSkiBoard(m_config);
    if (DriveBoard->Init(driveROM) != Result::OKAY) // no actual ROM data loaded (ski feedback is simulated)
      return Result::FAIL;
  }
  else
  {
    // Dummy drive board (presents itself as not attached)
    DriveBoard = new CDriveBoard(m_config);
    if (DriveBoard->Init() != Result::OKAY)
      return Result::FAIL;
  }

  // Security board encryption device
  m_cryptoDevice.Init(game.encryption_key, std::bind(&CModel3::ReadSecurityRAM, this, std::placeholders::_1));

  // Print game information
  std::set<std::string> extra_hw;

  if (DSB)
    extra_hw.insert(Util::Format() << "Digital Sound Board (Type " << game.mpeg_board << ")");
  if (DriveBoard->IsAttached())
  {
    if (DriveBoard->GetType() == Game::DRIVE_BOARD_BILLBOARD)
      extra_hw.insert("Billboard");
    else
      extra_hw.insert("Drive Board");
  }
  if (game.encryption_key)
    extra_hw.insert("Security Board");
  if (game.netboard_present)
    extra_hw.insert("Net Board");
  if (!game.version.empty())
    std::cout << "    Title:          " << game.title << " (" << game.version << ")" << std::endl;
  else
    std::cout << "    Title:          " << game.title << std::endl;
  std::cout << "    ROM Set:        " << game.name << std::endl;
  std::cout << "    Developer:      " << game.manufacturer << std::endl;
  std::cout << "    Year:           " << game.year << std::endl;
  std::cout << "    Stepping:       " << game.stepping << std::endl;
  if (!extra_hw.empty())
    std::cout << "    Extra Hardware: " << Util::Format(", ").Join(extra_hw) << std::endl;
  std::cout << std::endl;

  m_game = game;
  SoundBoard.SetGameName(m_game.name);
  NetBoard->GetGame(m_game);
  if (Result::OKAY != NetBoard->Init(netRAM, netBuffer))
  {
    return Result::FAIL;
  }

  m_runNetBoard = m_game.stepping != "1.0" && NetBoard->IsAttached();

  return Result::OKAY;
}

void CModel3::AttachRenderers(CRender2D *Render2DPtr, IRender3D *Render3DPtr, SuperAA *superAA)
{
  TileGen.AttachRenderer(Render2DPtr);
  GPU.AttachRenderer(Render3DPtr);
  m_superAA = superAA;
}

void CModel3::AttachInputs(CInputs *InputsPtr)
{
  Inputs = InputsPtr;

  if (DriveBoard->IsAttached())
    DriveBoard->AttachInputs(Inputs, m_game.inputs);

  DebugLog("Model 3 attached inputs\n");
}

void CModel3::AttachOutputs(COutputs *OutputsPtr)
{
  Outputs = OutputsPtr;
  Outputs->SetGame(m_game);
  Outputs->Attached();

  if (DriveBoard->IsAttached())
    DriveBoard->AttachOutputs(Outputs);

  DebugLog("Model 3 attached outputs\n");
}

constexpr static int RAM_SIZE			= 0x800000;		//8MB
constexpr static int CROM_SIZE			= 0x800000;		//8MB
constexpr static int CROMxx_SIZE		= 0x8000000;	//128MB
constexpr static int VROM_SIZE			= 0x4000000;	//64MB
constexpr static int BACKUPRAM_SIZE		= 0x20000;		//128KB
constexpr static int SECURITYRAM_SIZE	= 0x20000;		//128KB
constexpr static int SOUNDROM_SIZE		= 0x80000;		//512KB
constexpr static int SAMPLEROM_SIZE		= 0x1000000;	//16MB
constexpr static int DSBPROGROM_SIZE	= 0x20000;		//128KB
constexpr static int DSBMPEGROM_SIZE	= 0x1000000;	//16MB
constexpr static int DRIVEROM_SIZE		= 0x10000;		//64KB
constexpr static int NETBUFFER_SIZE		= 0x20000;		//128KB
constexpr static int NETRAM_SIZE		= 0x10000;		//64KB

constexpr static int MEM_POOL_SIZE		= RAM_SIZE + CROM_SIZE +
                                        CROMxx_SIZE + VROM_SIZE +
                                        BACKUPRAM_SIZE + SECURITYRAM_SIZE +
                                        SOUNDROM_SIZE + SAMPLEROM_SIZE +
                                        DSBPROGROM_SIZE + DSBMPEGROM_SIZE +
                                        DRIVEROM_SIZE + NETBUFFER_SIZE +
                                        NETRAM_SIZE;

constexpr static int RAM_OFFSET			= 0;
constexpr static int CROM_OFFSET		= RAM_OFFSET + RAM_SIZE;
constexpr static int CROMxx_OFFSET		= CROM_OFFSET + CROM_SIZE;
constexpr static int VROM_OFFSET		= CROMxx_OFFSET + CROMxx_SIZE;
constexpr static int BACKUPRAM_OFFSET	= VROM_OFFSET + VROM_SIZE;
constexpr static int SECURITYRAM_OFFSET	= BACKUPRAM_OFFSET + BACKUPRAM_SIZE;
constexpr static int SOUNDROM_OFFSET	= SECURITYRAM_OFFSET + SECURITYRAM_SIZE;
constexpr static int SAMPLEROM_OFFSET	= SOUNDROM_OFFSET + SOUNDROM_SIZE;
constexpr static int DSBPROGROM_OFFSET	= SAMPLEROM_OFFSET + SAMPLEROM_SIZE;
constexpr static int DSBMPEGROM_OFFSET	= DSBPROGROM_OFFSET + DSBPROGROM_SIZE;
constexpr static int DRIVEROM_OFFSET	= DSBMPEGROM_OFFSET + DSBMPEGROM_SIZE;
constexpr static int NETBUFFER_OFFSET	= DRIVEROM_OFFSET + DRIVEROM_SIZE;
constexpr static int NETRAM_OFFSET		= NETBUFFER_OFFSET + NETBUFFER_SIZE;

// Model 3 initialization. Some initialization is deferred until ROMs are loaded in LoadROMSet()
Result CModel3::Init(void)
{
  constexpr float memSizeMB = (float)MEM_POOL_SIZE / (float)0x100000;

  // Allocate all memory for ROMs and PPC RAM
  memoryPool = new(std::nothrow) UINT8[MEM_POOL_SIZE];
  if (NULL == memoryPool)
    return ErrorLog("Insufficient memory for Model 3 object (needs %1.1f MB).", memSizeMB);
  memset(memoryPool, 0, MEM_POOL_SIZE);

  // Set up pointers
  ram = &memoryPool[RAM_OFFSET];
  crom = &memoryPool[CROM_OFFSET];
  vrom = &memoryPool[VROM_OFFSET];
  soundROM = &memoryPool[SOUNDROM_OFFSET];
  sampleROM = &memoryPool[SAMPLEROM_OFFSET];
  dsbROM = &memoryPool[DSBPROGROM_OFFSET];
  mpegROM = &memoryPool[DSBMPEGROM_OFFSET];
  backupRAM = &memoryPool[BACKUPRAM_OFFSET];
  securityRAM = &memoryPool[SECURITYRAM_OFFSET];
  driveROM = &memoryPool[DRIVEROM_OFFSET];
  netRAM = &memoryPool[NETRAM_OFFSET];
  netBuffer = &memoryPool[NETBUFFER_OFFSET];

  SetCROMBank(0xFF);

  // Initialize other devices (PowerPC, DSB, and security board initialized after ROMs loaded)
  IRQ.Init();
  PCIBridge.Init();
  PCIBus.Init();
  SCSI.Init(this,&IRQ,0x100); // SCSI is actually a non-maskable interrupt, so we give it a bit number outside of 8-bit range
  RTC.Init();
  EEPROM.Init();
  if (Result::OKAY != TileGen.Init(&IRQ))
    return Result::FAIL;
  if (Result::OKAY != GPU.Init(vrom,this,&IRQ,0x100)) // same for Real3D DMA interrupt
    return Result::FAIL;
  if (Result::OKAY != SoundBoard.Init(soundROM,sampleROM))
    return Result::FAIL;

  PCIBridge.AttachPCIBus(&PCIBus);
  PCIBus.AttachDevice(13,&GPU);
  PCIBus.AttachDevice(14,&SCSI);
  PCIBus.AttachDevice(16,this);

  if (m_config["SimulateNet"].ValueAs<bool>())
      NetBoard = new CSimNetBoard(m_config);
  else
      NetBoard = new CNetBoard(m_config);

  DebugLog("Initialized Model 3 (allocated %1.1f MB)\n", memSizeMB);

  return Result::OKAY;
}

CSoundBoard *CModel3::GetSoundBoard(void)
{
  return &SoundBoard;
}

CDriveBoard *CModel3::GetDriveBoard(void)
{
  return DriveBoard;
}

INetBoard *CModel3::GetNetBoard(void)
{
  return NetBoard;
}

CModel3::CModel3(Util::Config::Node &config)
  : m_config(config),
    m_multiThreaded(config["MultiThreaded"].ValueAs<bool>()),
    m_gpuMultiThreaded(config["GPUMultiThreaded"].ValueAs<bool>()),
    sndBrdWakeNotify(false),
    TileGen(config),
    GPU(config),
    SoundBoard(config),
    m_jtag(GPU),
    m_superAA(nullptr)
{
  // Initialize pointers so dtor can know whether to free them
  memoryPool = NULL;

  // Various uninitialized pointers
  Inputs = NULL;
  Outputs = NULL;
  ram = NULL;
  crom = NULL;
  vrom = NULL;
  soundROM = NULL;
  sampleROM = NULL;
  dsbROM = NULL;
  mpegROM = NULL;
  cromBank = NULL;
  backupRAM = NULL;
  securityRAM = NULL;
  netRAM = NULL;
  netBuffer = NULL;

  DSB = NULL;
  DriveBoard = NULL;

  NetBoard = NULL;
  m_runNetBoard = false;

  securityPtr = 0;

  startedThreads = false;
  pauseThreads = false;
  stopThreads = false;
  ppcBrdThread = NULL;
  sndBrdThread = NULL;
  drvBrdThread = NULL;

  ppcBrdThreadRunning = false;
  ppcBrdThreadDone = false;
  sndBrdThreadRunning = false;
  sndBrdThreadDone = false;
  drvBrdThreadRunning = false;
  drvBrdThreadDone = false;

  syncSndBrdThread = false;
  ppcBrdThreadSync = NULL;
  sndBrdThreadSync = NULL;
  drvBrdThreadSync = NULL;

  notifyLock = NULL;
  notifySync = NULL;

  m_stepping = 0;
  inputBank = 0;
  serialFIFO1 = 0;
  serialFIFO2 = 0;
  gunReg = 0;
  adcChannel = 0;

  midiCtrlPort = 0;
  driveROM = nullptr;
  OutputRegister[0] = OutputRegister[1] = 0;
  cromBankReg = 0;
  memset(PPCFetchRegions, 0, sizeof(PPCFetchRegions));
  gpusReady = false;
  sndBrdNotifyLock = nullptr;
  sndBrdNotifySync = nullptr;
  memset(&timings, 0, sizeof(FrameTimings));

  DebugLog("Built Model 3\n");
}

// Dumps a memory region to a file for debugging purposes
static void Dump(const char *name, const char *file, uint8_t *buf, size_t size, bool reverse32, bool reverse16)
{
  FILE *fp = fopen(file, "wb");
  if (NULL != fp)
  {
    if (reverse32)
      Util::FlipEndian32(buf, size);
    else if (reverse16)
      Util::FlipEndian16(buf, size);
    fwrite(buf, sizeof(UINT8), size, fp);
    fclose(fp);
    printf("Wrote %s to '%s'\n", name, file);
  }
  else
    ErrorLog("Unable to open '%s' for writing.", file);
}

CModel3::~CModel3(void)
{
  SpikeoutInputLogFlush();   // InputLog: the last < 60 frames
  // Dump memory if requested
  if (m_config["DumpMemory"].ValueAsDefault<bool>(false))
  {
    printf("[EXIT] frame=%u PC=%08X LR=%08X\n", m_testFrame, ppc_get_pc(), ppc_get_lr());
    Dump("RAM", "ram.bin", ram, 0x800000, true, false);
    extern uint32_t g_texUploadMap[64 * 64];
    if (FILE *tf = fopen("texmap.txt", "w"))
    {
      for (int j = 0; j < 64; j++)
      {
        for (int i = 0; i < 64; i++)
        {
          uint32_t n = g_texUploadMap[j * 64 + i];
          fputc(n == 0 ? '.' : n < 10 ? '0' + n : '#', tf);
        }
        fputc(10, tf);
      }
      fclose(tf);
    }
    Dump("VROM", "vrom.bin", vrom, 0x4000000, true, false);
    Dump("CROM", "crom.bin", crom, 0x800000, true, false);
    Dump("banked CROM", "banked_crom.bin", &crom[0x800000], 0x7000000, true, false);
    Dump("sound ROM", "sound_rom.bin", soundROM, 0x80000, false, true);
    Dump("sample ROM", "sample_rom.bin", sampleROM, 0x800000, false, true);
  }

  // Stop all threads
  StopThreads();

  // Free memory
  if (memoryPool != NULL)
  {
    delete [] memoryPool;
    memoryPool = NULL;
  }

  if (DSB != NULL)
  {
    delete DSB;
    DSB = NULL;
  }

  if (DriveBoard != NULL)
  {
      delete DriveBoard;
      DriveBoard = NULL;
  }

  if (NetBoard != NULL)
  {
      delete NetBoard;
      NetBoard = NULL;
  }

  Inputs = NULL;
  Outputs = NULL;
  ram = NULL;
  crom = NULL;
  vrom = NULL;
  soundROM = NULL;
  sampleROM = NULL;
  dsbROM = NULL;
  mpegROM = NULL;
  cromBank = NULL;
  backupRAM = NULL;
  securityRAM = NULL;
  netRAM = NULL;
  netBuffer = NULL;

  DebugLog("Destroyed Model 3\n");
}
