/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2020 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
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

#ifndef INCLUDED_SIMNETBOARD_H
#define INCLUDED_SIMNETBOARD_H

#include <cstdint>
#include <cstdio>
#include "TCPSend.h"
#include "TCPReceive.h"
#include "INetBoard.h"
#include "Stream/Satellite.h"

enum class State
{
	start,
	init,
	testing,
	ready,
	error
};

enum class GameType
{
	unknown,
	one,		// all games except those listed below
	two			// Le Mans 24, Virtual-On, Dirt Devils
};

class CSimNetBoard : public INetBoard
{
public:
	CSimNetBoard(const Util::Config::Node& config);
	~CSimNetBoard(void);

	void SaveState(CBlockFile* SaveState);
	void LoadState(CBlockFile* SaveState);

	Result Init(uint8_t* netRAMPtr, uint8_t* netBufferPtr);
	void RunFrame(void);
	void Reset(void);

	bool IsAttached(void);
	bool IsRunning(void);

	void GetGame(const Game& gameInfo);

	uint8_t ReadCommRAM8(unsigned addr);
	uint16_t ReadCommRAM16(unsigned addr);
	uint32_t ReadCommRAM32(unsigned addr);

	void WriteCommRAM8(unsigned addr, uint8_t data);
	void WriteCommRAM16(unsigned addr, uint16_t data);
	void WriteCommRAM32(unsigned addr, uint32_t data);

	uint16_t ReadIORegister(unsigned reg);
	void WriteIORegister(unsigned reg, uint16_t data);

	// 11/23 netlink phase 2: right after the host's state is loaded (end of the host's frame F), wait for frame F+1
	// and take node k's inputs from it, so our first frame plays what the node read (Main.cpp)
	void ClonePrime(void);

private:
	// Config
	const Util::Config::Node& m_config;

	uint8_t* RAM = nullptr;
	uint8_t* Buffer = nullptr;
	uint8_t* CommRAM = nullptr;
	uint8_t* externalCommRAM = nullptr;

	// netsock
	uint16_t port_in = 0;
	uint16_t port_out = 0;
	std::string addr_out;
	std::thread m_connectThread;
	std::atomic_bool m_quit = false;
	std::atomic_bool m_connected = false;

	std::unique_ptr<TCPSend> nets = nullptr;
	std::unique_ptr<TCPReceive> netr = nullptr;

	Game m_gameInfo;
	GameType m_gameType = GameType::unknown;
	State m_state = State::start;

	uint8_t m_numMachines = 0;
	uint16_t m_counter = 0;

	uint16_t m_segmentSize = 0;

	bool m_attached = false;
	bool m_running = false;

	uint16_t m_IRQ2ack = 0;
	uint16_t m_status0 = 0;	// ioreg 0x88
	uint16_t m_status1 = 0;	// ioreg 0x8a
	bool m_commbank = false;

	inline bool IsGame(const char* gameName);
	void ConnectProc(void);

	// 11/20 satellite clone (Stream/Satellite.h): no sockets of our own, the relay's copy of 08 -> 01 feeds us
	bool m_clone = false;
	int m_cloneFixed = 0;		// ini SatelliteNode: 1..8 = always follow that node, 0 = the games in the link
	int m_cloneInterval = 600;	// frames before moving on to the next node of the same game
	int m_cloneTimer = 0;
	uint32_t m_cloneSig = 0;	// the link of all nodes last frame (state / join flags / game mask)
	int m_cloneStable = 0;		// frames it has not changed, without a join going on
	bool m_spikeofe = false;
	SatLinkEvent m_cloneNext;
	bool m_cloneHaveNext = false;
	bool CloneTesting(uint8_t& numMachines, uint8_t& machineIndex);
	bool CloneWait(SatLinkEvent& e);
	bool CloneReady(void);
	void CloneChoose(const std::vector<std::vector<char>>& segs, const std::vector<std::vector<char>>* next, int n);
	void PutInputRecord(void);
	bool CloneTakeInputs(const SatLinkEvent& e, int k);

	// 11/23 main6 (netlink): ini SatelliteSegDump = 1 -> segdump.bin, every frame the clone takes, to measure how
	// small the link data gets (raw / delta / compressed) before sending it over the internet. Per frame:
	// u32 frame, u8 n, then n x (u16 len, len bytes) in the order 08 sent them (segs[i] = node N - i), comm RAM byte order.
	FILE* m_segDump = nullptr;
	uint32_t m_segDumpFrame = 0;
	void SegDump(const SatLinkEvent& e);

	// 11/23 main6: save / load on the same frame on every linked node (spikeofe). A node whose save / load key
	// was pressed puts "do it at frame T" in its own segment (PPC bytes 0x8F0.., the free tail before the bot
	// mark 0x8FC); every node sees it in the same ring loop and acts at the end of frame T.
	bool m_nsReqOn = false;		// our own request, sent every frame until T
	int m_nsReqAction = 0, m_nsReqSlot = 0;
	uint16_t m_nsReqTarget = 0;
	uint8_t m_nsSeq = 0;
	bool m_nsPlanOn = false;	// what every node will do (the earliest request seen)
	int m_nsPlanAction = 0, m_nsPlanSlot = 0;
	uint16_t m_nsPlanTarget = 0;
	void NetStateBegin(void);
	void NetStateRead(const std::vector<char>& seg);
	void NetStateEnd(void);
	void NetStateOffer(int action, int slot, uint16_t target);

	// 11/23 main6 netlink phase 1 (Netlink.h): host = serve our link data (ini NetlinkServe), client = the clone
	// fed from the host's socket (ini NetlinkConnect = host:port) instead of the 09 relay
	bool m_netlinkServe = false;
	bool m_netlinkClient = false;
	uint8_t m_machineIndex = 0;
};

// 11/23 main6 (Main.cpp <-> SimNetBoard, both on the main thread)
extern bool g_netStateSyncOn;			// the net board is in a running link that syncs save / load
extern int g_netStateReq, g_netStateReqSlot;	// Main: 1 save / 2 load asked on this node
extern int g_netStateDo, g_netStateDoSlot;	// net board: do it now, after this frame

#endif