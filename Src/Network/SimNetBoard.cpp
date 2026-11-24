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

#include <thread>
#include "Supermodel.h"
#include "SimNetBoard.h"
#include <OSD/Thread.h>
#include "Stream/Satellite.h"
#include "Stream/Protocol.h"
#include "Netlink.h"

 // these make 16-bit read/writes much neater
#define RAM16 *(uint16_t*)&RAM
#define CommRAM16 *(uint16_t*)&CommRAM

static const uint64_t netGUID = 0x5bf177da34872;

inline bool CSimNetBoard::IsGame(const char* gameName)
{
	return (m_gameInfo.name == gameName) || (m_gameInfo.parent == gameName);
}

CSimNetBoard::CSimNetBoard(const Util::Config::Node& config) : m_config(config)
{
}

CSimNetBoard::~CSimNetBoard(void)
{
	m_quit = true;
	if (m_netlinkClient)
		NetlinkClientStop();
	if (m_clone)
		SatelliteTapStop();
	if (m_netlinkServe || m_netlinkWatchServe)
		NetlinkHostStop();
	if (m_segDump)
	{
		fclose(m_segDump);
		m_segDump = nullptr;
	}

	if (m_connectThread.joinable())
		m_connectThread.join();
}

// 11/23 main6: the link in the save state. Before, nothing of the net board was saved: after a load the game RAM
// was from the save, but the comm RAM (what the other nodes sent last frame, both banks), the frame counter in it
// and which bank is which were from before the load - half the time the game read the other bank. Own block, so
// states without it still load and older exes skip it.
// On load the link itself is kept as it is (state, number of machines, our index, status1, running): the ring is
// live and cannot go back to the link test. Only when the save is of the same link (ready, same number of
// machines, same segment size) do the comm RAM, the counter and the bank come back.
namespace
{
	const char* kNetStateBlock = "SimNetBoard";
	const uint32_t kNetStateVer = 1;
}

void CSimNetBoard::SaveState(CBlockFile* SaveState)
{
	if (!m_attached)
		return;
	SaveState->NewBlock(kNetStateBlock, __FILE__);
	uint32_t ver = kNetStateVer;
	uint8_t state = (uint8_t)m_state, running = m_running ? 1 : 0, bank = m_commbank ? 1 : 0;
	SaveState->Write(&ver, sizeof(ver));
	SaveState->Write(&state, sizeof(state));
	SaveState->Write(&running, sizeof(running));
	SaveState->Write(&m_numMachines, sizeof(m_numMachines));
	SaveState->Write(&bank, sizeof(bank));
	SaveState->Write(&m_segmentSize, sizeof(m_segmentSize));
	SaveState->Write(&m_counter, sizeof(m_counter));
	SaveState->Write(&m_IRQ2ack, sizeof(m_IRQ2ack));
	SaveState->Write(&m_status0, sizeof(m_status0));
	SaveState->Write(&m_status1, sizeof(m_status1));
	SaveState->Write(RAM, 0x10000);
	SaveState->Write(Buffer, 0x20000);
}

void CSimNetBoard::LoadState(CBlockFile* SaveState)
{
	if (!m_attached)
		return;
	// a synced save / load still to come is dropped (every node loads on the same frame, so all drop it)
	m_nsReqOn = m_nsPlanOn = false;
	g_netStateDo = 0;
	if (Result::OKAY != SaveState->FindBlock(kNetStateBlock))
	{
		InfoLog("NetState: this save state has no link data (older exe): the link is left as it is.");
		return;
	}
	uint32_t ver = 0;
	SaveState->Read(&ver, sizeof(ver));
	if (ver != kNetStateVer)
	{
		InfoLog("NetState: link data version %u, this exe reads %u: the link is left as it is.", ver, kNetStateVer);
		return;
	}
	uint8_t state = 0, running = 0, numMachines = 0, bank = 0;
	uint16_t segmentSize = 0, counter = 0, irq2ack = 0, status0 = 0, status1 = 0;
	SaveState->Read(&state, sizeof(state));
	SaveState->Read(&running, sizeof(running));
	SaveState->Read(&numMachines, sizeof(numMachines));
	SaveState->Read(&bank, sizeof(bank));
	SaveState->Read(&segmentSize, sizeof(segmentSize));
	SaveState->Read(&counter, sizeof(counter));
	SaveState->Read(&irq2ack, sizeof(irq2ack));
	SaveState->Read(&status0, sizeof(status0));
	SaveState->Read(&status1, sizeof(status1));

	if (m_netlinkClient)
	{
		// 11/23 netlink: the host's node k saved this at the end of frame F; we become that node, linked, and the
		// host's frames F+1.. follow. Everything comes back, the comm RAM header and status1 included (they are
		// node k's, which we play).
		if ((State)state != State::ready || numMachines < 2 || numMachines > 8 || segmentSize == 0)
		{
			InfoLog("NetState: netlink client: the host's state is not of a running link (state %u, %u machines).", state, numMachines);
			return;
		}
		SaveState->Read(RAM, 0x10000);
		SaveState->Read(Buffer, 0x20000);
		m_numMachines = numMachines;
		m_segmentSize = segmentSize;
		m_counter = counter;
		m_IRQ2ack = irq2ack;
		m_status0 = status0;
		m_status1 = status1;
		m_running = running != 0;
		m_connected = true;
		m_commbank = bank != 0;
		CommRAM = m_commbank ? Buffer + 0x10000 : Buffer;
		externalCommRAM = m_commbank ? Buffer : Buffer + 0x10000;
		m_state = State::ready;
		// our node: status1 = 0x2021 + (machines - 1) * 0x20 + index (the link test above)
		int k = ((status1 - 0x2021) & 0x1F) + 1;
		if (k < 1 || k > numMachines)
			k = 1;
		m_machineIndex = (uint8_t)(k - 1);
		m_cloneFixed = m_netlinkWatch ? m_cloneFixedIni : k;	// 11/23: the satellite keeps switching (SatelliteNode 0)
		g_satClone.numMachines = numMachines;
		g_satClone.node = k;
		g_satClone.view = k;
		g_satClone.switched = true;
		g_satClone.havePacket = false;
		g_satClone.haveInputs = false;
		m_cloneHaveNext = false;
		m_cloneTimer = 0;
		InfoLog("NetState: netlink client: node %d of %u, link frame %u.", k, numMachines, counter);
		return;
	}
	if (m_clone)
	{
		InfoLog("NetState: satellite clone: the link comes from the relay, not from the save.");
		return;
	}
	if (m_state != State::ready || (State)state != State::ready || numMachines != m_numMachines || segmentSize != m_segmentSize)
	{
		InfoLog("NetState: save: state %u, %u machines, segment 0x%X / now: state %u, %u machines, segment 0x%X - not the same link, left as it is.",
			state, numMachines, segmentSize, (unsigned)m_state, m_numMachines, m_segmentSize);
		return;
	}

	// the comm RAM header (+0x0 master / slave, +0x2 machines, +0x4 our index, +0x8.. buffer offsets) stays ours,
	// in case the state file came from another node's folder; +0x6 is the frame counter and comes from the save
	uint8_t head[2][0x10];
	memcpy(head[0], Buffer, 0x10);
	memcpy(head[1], Buffer + 0x10000, 0x10);
	SaveState->Read(RAM, 0x10000);
	SaveState->Read(Buffer, 0x20000);
	for (int b = 0; b < 2; b++)
	{
		memcpy(Buffer + b * 0x10000, head[b], 6);
		memcpy(Buffer + b * 0x10000 + 8, head[b] + 8, 8);
	}
	m_counter = counter;
	m_IRQ2ack = irq2ack;
	m_status0 = status0;
	m_commbank = bank != 0;
	CommRAM = m_commbank ? Buffer + 0x10000 : Buffer;
	externalCommRAM = m_commbank ? Buffer : Buffer + 0x10000;
	InfoLog("NetState: link restored: %u machines, counter %u, bank %u.", m_numMachines, m_counter, bank);
}

Result CSimNetBoard::Init(uint8_t* netRAMPtr, uint8_t* netBufferPtr)
{
	RAM = netRAMPtr;
	Buffer = netBufferPtr;

	CommRAM = Buffer;
	externalCommRAM = Buffer + 0x10000;

	m_attached = m_gameInfo.netboard_present && m_config["Network"].ValueAs<bool>();

	if (!m_attached)
		return Result::OKAY;

	if (IsGame("daytona2") || IsGame("harley") || IsGame("scud") || IsGame("srally2") ||
		IsGame("skichamp") || IsGame("spikeout") || IsGame("spikeofe"))
		m_gameType = GameType::one;
	else if (IsGame("lemans24") || IsGame("von2") || IsGame("dirtdvls"))
		m_gameType = GameType::two;
	else
		return ErrorLog("Game not recognized or supported");

	m_state = State::start;

	//netsocks
	port_in = m_config["PortIn"].ValueAs<unsigned>();
	port_out = m_config["PortOut"].ValueAs<unsigned>();
	addr_out = m_config["AddressOut"].ValueAs<std::string>();

	// 11/20 satellite clone (Stream/Satellite.h): spikeofe only
	m_spikeofe = IsGame("spikeofe");
	m_clone = m_spikeofe && m_config["SatelliteClone"].ValueAsDefault<bool>(false);
	if (m_clone)
	{
		m_cloneFixed = m_config["SatelliteNode"].ValueAsDefault<int>(0);
		if (m_cloneFixed < 0 || m_cloneFixed > 8)
			m_cloneFixed = 0;
		m_cloneFixedIni = m_cloneFixed;
		int sec = m_config["SatelliteInterval"].ValueAsDefault<int>(10);
		m_cloneInterval = sec <= 0 ? 0 : (sec < 2 ? 2 : sec) * 60;	// 0 = stay with one node
		g_satClone.on = true;
		g_satView.on = true;
		g_satView.hideUI = m_config["SatelliteHideUI"].ValueAsDefault<int>(1);
		g_satView.cam = 3;	// 11/21 (owner): always our camera - the game's own camera moved over to the shown player (g_satClone.view)
		                  	// while the clone keeps its node (1 = the chase camera from behind)
		g_satView.vpLog = m_config["SatelliteVpLog"].ValueAsDefault<int>(0);
		g_satView.overlay = m_config["SatelliteOverlay"].ValueAsDefault<int>(1);
		g_satView.title = m_config["SatelliteTitle"].ValueAsDefault<std::string>(g_satView.title);
		g_satView.ticker = m_config["SatelliteTicker"].ValueAsDefault<std::string>(g_satView.ticker);
		extern float g_satCamDist, g_satCamHeight, g_satCamLook;
		extern int g_satCamAxis;
		g_satCamDist = m_config["SatelliteCamDist"].ValueAsDefault<float>(3.5f);
		g_satCamHeight = m_config["SatelliteCamHeight"].ValueAsDefault<float>(1.5f);
		g_satCamLook = m_config["SatelliteCamLook"].ValueAsDefault<float>(0.6f);
		g_satCamAxis = m_config["SatelliteCamAxis"].ValueAsDefault<int>(1);
		g_satClone.mirror = m_config["SatelliteMirror"].ValueAsDefault<int>(0);
		if (m_config["SatelliteSegDump"].ValueAsDefault<int>(0))
		{
			m_segDump = fopen("segdump.bin", "wb");
			if (m_segDump)
			{
				setvbuf(m_segDump, nullptr, _IOFBF, 1 << 20);
				printf("[Satellite] SegDump: segdump.bin\n");
			}
		}
		// 11/23 main6 netlink phase 1: NetlinkConnect = host:port -> the link data from the host's node k, which we
		// follow with the game's own camera and HUD (no satellite view)
		// 11/23 (owner): SatelliteConnect = host:port -> the satellite outside the ring: the same data from a node's
		// NetlinkWatchPort, with the satellite view and switching kept, no inputs sent
		std::string sat = m_config["SatelliteConnect"].ValueAsDefault<std::string>("");
		size_t satColon = sat.rfind(':');
		if (!sat.empty() && satColon != std::string::npos && satColon > 0)
		{
			m_netlinkClient = true;
			m_netlinkWatch = true;
			NetlinkClientSetWatch(true);
			NetlinkClientStart(sat.substr(0, satColon), atoi(sat.c_str() + satColon + 1));
			printf("[Satellite] Clone: outside the ring, link data from %s\n", sat.c_str());
			return Result::OKAY;
		}
		std::string nl = m_config["NetlinkConnect"].ValueAsDefault<std::string>("");
		size_t colon = nl.rfind(':');
		if (!nl.empty() && colon != std::string::npos && colon > 0)
		{
			m_netlinkClient = true;
			g_satView.on = false;
			NetlinkClientStart(nl.substr(0, colon), atoi(nl.c_str() + colon + 1));
			printf("[Netlink] Client: the clone follows the host's node\n");
			return Result::OKAY;
		}
		SatelliteTapStart(port_in, addr_out, port_out);
		printf("[Satellite] Clone: following %s\n", m_cloneFixed ? "a fixed node" : "the games in the link");
		return Result::OKAY;
	}

	// 11/23 main6 netlink phase 1: serve our link data to one client on TCP (slot base port + 2, or NetlinkPort)
	// 11/23 (owner): + NetlinkWatchPort > 0 -> also serve the satellite (09) outside the ring on that port
	const int watchPort = m_spikeofe ? m_config["NetlinkWatchPort"].ValueAsDefault<int>(0) : 0;
	if (m_spikeofe && (m_config["NetlinkServe"].ValueAsDefault<int>(0) || watchPort > 0))
	{
		std::string settings;
		for (int i = 0; i < kNetlinkGameKeyCount; i++)
		{
			std::string v = m_config[kNetlinkGameKeys[i]].ValueAsDefault<std::string>("");
			if (!v.empty())
				settings += std::string(kNetlinkGameKeys[i]) + "=" + v + "\n";
		}
		if (m_config["NetlinkServe"].ValueAsDefault<int>(0))
		{
			int port = m_config["NetlinkPort"].ValueAsDefault<int>(0);
			int slot = m_config["LinkPlay"].ValueAsDefault<int>(0);
			if (port <= 0)
				port = StreamProto::SlotBasePort(slot >= 1 && slot <= 8 ? slot : 1) + StreamProto::PORT_VIDEO;
			m_netlinkServe = true;
			NetlinkHostStart(port, settings);
		}
		if (watchPort > 0 && watchPort < 65536)
		{
			m_netlinkWatchServe = true;
			NetlinkWatchStart(watchPort, settings);
		}
	}

	nets = std::make_unique<TCPSend>(addr_out, port_out);
	netr = std::make_unique<TCPReceive>(port_in);

	return Result::OKAY;
}

void CSimNetBoard::RunFrame(void)
{
	g_netStateSyncOn = false;
	if (!IsRunning())
		return;

	switch (m_state)
	{
	case State::start:
		if (m_clone)
			m_connected = true;		// the relay connects on its own
		else if (!m_connected && !m_connectThread.joinable())
			m_connectThread = std::thread(&CSimNetBoard::ConnectProc, this);
		m_status0 = 0;
		m_status1 = IsGame("dirtdvls") ? 0x4004 : 0xe000;
		m_state = State::init;
		break;

	case State::init:
		memset(CommRAM, 0, 0x20000);
		if (m_gameType == GameType::one)
		{
			if (m_status0 & 0x8000)				// has main board changed this register?
			{
				m_IRQ2ack |= 0x01;				// simulate IRQ 2 ack
				if (m_status0 == 0xf000)
				{
					// initialization complete
					m_status1 = 0;
					CommRAM16[0x72] = FLIPENDIAN16(0x1); // is this necessary?
					m_state = State::testing;
				}
				m_status0 = 0;					// 0 should work for all init subroutines
			}
		}
		else
		{
			// type 2 performs initialization on its own
			m_status1 = 0;
			m_state = State::testing;
			m_counter = 0;
		}
		break;

	case State::testing:
		if (m_gameType == GameType::one)
		{
			m_status0 += 1; // type 1 games require this to be incremented every frame

			if (!m_connected)
				break;

			uint8_t numMachines, machineIndex;

			if (m_clone)
			{
				if (!CloneTesting(numMachines, machineIndex))
					break;
			}
			else if (RAM16[0x400] == 0)	// master
			{
				// flush receive buffer
				while (netr->CheckDataAvailable())
				{
					netr->Receive();
				}

				// check all linked instances have the same GUID
				nets->Send(&netGUID, sizeof(netGUID));
				auto& recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;

				// send the GUID for one more loop
				nets->Send(&testGUID, sizeof(testGUID));
				netr->Receive();
				
				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// master has an index of zero
				machineIndex = 0;
				nets->Send(&machineIndex, sizeof(machineIndex));

				// receive back the number of other linked machines
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				numMachines = recv_data[0];

				// send the number of other linked machines
				nets->Send(&numMachines, sizeof(numMachines));
				netr->Receive();
			}
			else
			{
				// receive GUID from the previous machine and check it matches
				auto& recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				nets->Send(&testGUID, sizeof(testGUID));

				// one more time, in case a later machine has a GUID mismatch
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				nets->Send(&testGUID, sizeof(testGUID));

				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// receive the previous machine's index, increment it, send it to the next machine
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				machineIndex = recv_data[0] + 1;
				nets->Send(&machineIndex, sizeof(machineIndex));

				// receive the number of other linked machines and forward it on
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				numMachines = recv_data[0];
				nets->Send(&numMachines, sizeof(numMachines));
			}

			// if there are no other linked machines, only continue if Supermodel is linked to itself
			// there might be more than one machine set to master which would cause glitches
			if ((numMachines == 0) && ((port_in != port_out) || (addr_out != "127.0.0.1")))
			{
				ErrorLog("no slave machines detected. Make sure only one machine is set to master!");
				m_state = State::error;
				break;
			}

			m_numMachines = numMachines + 1;
			m_machineIndex = machineIndex;

			m_status0 = 0;		// supposed to cycle between 0 and 1 (also 2 for Daytona 2); doesn't seem to matter
			m_status1 = 0x2021 + (numMachines * 0x20) + machineIndex;

			CommRAM16[0x0] = RAM16[0x400];	// 0 if master, 1 if slave
			CommRAM16[0x2] = numMachines;
			CommRAM16[0x4] = machineIndex;

			m_counter = 0;
			CommRAM16[0x6] = 0;

			m_segmentSize = RAM16[0x404];

			// don't know if these are actually required, but it never hurts to include them
			CommRAM16[0x8] = FLIPENDIAN16(0x100 + m_segmentSize);
			CommRAM16[0xa] = FLIPENDIAN16(RAM16[0x402] - m_segmentSize - 1);
			CommRAM16[0xc] = FLIPENDIAN16(0x100);
			CommRAM16[0xe] = FLIPENDIAN16(RAM16[0x402] - m_segmentSize + 0x200);

			m_state = State::ready;
		}
		else
		{
			if (!m_connected)
				break;

			// we have to track both playable and non-playable machines for type 2
			struct
			{
				uint8_t total;
				uint8_t playable;
			} numMachines, machineIndex;

			if (RAM16[0x200] == 0)	// master
			{
				// flush receive buffer
				while (netr->CheckDataAvailable())
					netr->Receive();

				// check all linked instances have the same GUID
				nets->Send(&netGUID, sizeof(netGUID));
				auto& recv_data = netr->Receive();
				if (recv_data.empty())
					break;

				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;

				// send the GUID for one more loop
				nets->Send(&testGUID, sizeof(testGUID));
				netr->Receive();

				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// master has indices set to zero
				machineIndex.total = 0; machineIndex.playable = 0;
				nets->Send(&machineIndex, sizeof(machineIndex));

				// receive back the number of other linked machines
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				memcpy(&numMachines, recv_data.data(), recv_data.size());

				// send the number of other linked machines
				nets->Send(&numMachines, sizeof(numMachines));
				netr->Receive();
			}
			else if (RAM16[0x200] < 0x8000)	// slave
			{
				// receive GUID from the previous machine and check it matches
				auto& recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				nets->Send(&testGUID, sizeof(testGUID));

				// one more time, in case a later machine has a GUID mismatch
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				nets->Send(&testGUID, sizeof(testGUID));

				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// receive the indices of the previous machine and increment them
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				memcpy(&machineIndex, recv_data.data(), recv_data.size());
				machineIndex.total++; machineIndex.playable++;

				// send our indices to the next machine
				nets->Send(&machineIndex, sizeof(machineIndex));

				// receive the number of machines
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				memcpy(&numMachines, recv_data.data(), recv_data.size());

				// forward the number of machines
				nets->Send(&numMachines, sizeof(numMachines));
			}
			else
			{
				// relay/satellite
				
				// receive GUID from the previous machine and check it matches
				auto& recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				nets->Send(&testGUID, sizeof(testGUID));

				// one more time, in case a later machine has a GUID mismatch
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				nets->Send(&testGUID, sizeof(testGUID));

				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// receive the indices of the previous machine; don't increment the playable index
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				memcpy(&machineIndex, recv_data.data(), recv_data.size());
				machineIndex.total++;

				// send our indices to the next machine
				nets->Send(&machineIndex, sizeof(machineIndex));

				// receive the number of machines
				recv_data = netr->Receive();
				if (recv_data.empty())
					break;
				memcpy(&numMachines, recv_data.data(), recv_data.size());

				// forward the number of machines
				nets->Send(&numMachines, sizeof(numMachines));

				// indicate that this machine is a relay/satellite
				if (!IsGame("dirtdvls"))
					machineIndex.playable |= 0x80;
			}

			// if there are no other linked machines, only continue if Supermodel is linked to itself
			// there might be more than one machine set to master which would cause glitches
			if ((numMachines.total == 0) && ((port_in != port_out) || (addr_out != "127.0.0.1")))
			{
				ErrorLog("no slave machines detected. Make sure only one machine is set to master!");
				if (IsGame("dirtdvls"))
					m_status1 = 0x8085;	// seems like the netboard code writers really liked their CPU model numbers
				m_state = State::error;
				break;
			}

			m_numMachines = numMachines.total + 1;

			m_status0 = 5;			// probably not necessary
			if (IsGame("dirtdvls"))
				m_status1 = (numMachines.playable << 4) | machineIndex.playable | 0x7400;
			else
				m_status1 = (numMachines.playable << 8) | machineIndex.playable;

			CommRAM16[0x0] = RAM16[0x200];	// master/slave/relay status
			CommRAM16[0x2] = (numMachines.playable << 8) | numMachines.total;
			CommRAM16[0x4] = (machineIndex.playable << 8) | machineIndex.total;

			m_counter = 0;
			CommRAM16[0x6] = 0;

			m_segmentSize = RAM16[0x204];

			// don't know if these are actually required, but it never hurts to include them
			CommRAM16[0x8] = FLIPENDIAN16(0x100 + m_segmentSize);
			CommRAM16[0xa] = FLIPENDIAN16(RAM16[0x206]);
			CommRAM16[0xc] = FLIPENDIAN16(0x100);
			CommRAM16[0xe] = FLIPENDIAN16(RAM16[0x206] + 0x80);

			m_state = State::ready;
		}
		break;

	case State::ready:
		m_counter++;
		CommRAM16[0x6] = FLIPENDIAN16(m_counter);
		
		// we only send what we need to; helps cut down on bandwidth
		// each machine has to receive back its own data (TODO: copy this data manually?)
		if (m_spikeofe && !m_clone)
		{
			PutInputRecord();
			NetStateBegin();
		}
		if (m_clone)
			CloneReady();
		else
		for (int i = 0; i < m_numMachines; i++)
		{
			nets->Send(CommRAM + 0x100 + i * m_segmentSize, m_segmentSize);
			auto& recv_data = netr->Receive();
			if (recv_data.empty())
			{
				// link broken - send an "empty" packet to alert other machines
				nets->Send(nullptr, 0);
				m_state = State::error;
				if (m_gameType == GameType::one)
					m_status1 = 0x40;			// send "link broken" message to mainboard
				if (m_netlinkServe || m_netlinkWatchServe)
					NetlinkHostBroken();
				break;
			}
			memcpy(CommRAM + 0x100 + (i + 1) * m_segmentSize, recv_data.data(), recv_data.size());
			if (m_spikeofe)
				NetStateRead(recv_data);
		}
		if ((m_netlinkServe || m_netlinkWatchServe) && m_state == State::ready)
			NetlinkHostFrame(CommRAM, m_numMachines, m_machineIndex + 1, m_segmentSize);
		if (m_spikeofe && !m_clone && m_state == State::ready)
			NetStateEnd();

		// swap CommRAM banks
		if (m_commbank)
		{
			m_commbank = false;
			CommRAM = Buffer;
			externalCommRAM = Buffer + 0x10000;
		}
		else
		{
			m_commbank = true;
			CommRAM = Buffer + 0x10000;
			externalCommRAM = Buffer;
		}
		
		break;

	case State::error:
		// do nothing
		break;
	}
}

void CSimNetBoard::Reset(void)
{
	// if netboard was active, send an "empty" packet so the other machines don't get stuck waiting for data
	if (m_state == State::ready && !m_clone)
	{
		nets->Send(nullptr, 0);
		netr->Receive();
	}

	m_running = false;
	m_state = State::start;
	m_nsReqOn = m_nsPlanOn = false;
	g_netStateSyncOn = false;
}

bool CSimNetBoard::IsAttached(void)
{
	return m_attached;
}

bool CSimNetBoard::IsRunning(void)
{
	return m_attached && m_running;
}

void CSimNetBoard::GetGame(const Game& gameInfo)
{
	m_gameInfo = gameInfo;
}

void CSimNetBoard::ConnectProc(void)
{
	if (m_connected)
		return;

	printf("Connecting to %s:%i ..\n", addr_out.c_str(), port_out);

	// wait until TCPSend has connected to the next machine
	while (!nets->Connect())
	{
		if (m_quit)
			return;
	}

	// wait until TCPReceive has accepted a connection from the previous machine
	while (!netr->Connected())
	{
		if (m_quit)
			return;
		CThread::Sleep(1);
	}

	printf("Successfully connected.\n");

	m_connected = true;
}

uint8_t CSimNetBoard::ReadCommRAM8(unsigned addr)
{
	return externalCommRAM[addr];
}

uint16_t CSimNetBoard::ReadCommRAM16(unsigned addr)
{
	return *(uint16_t*)&externalCommRAM[addr];
}

uint32_t CSimNetBoard::ReadCommRAM32(unsigned addr)
{
	return *(uint32_t*)&externalCommRAM[addr];
}

void CSimNetBoard::WriteCommRAM8(unsigned addr, uint8_t data)
{
	externalCommRAM[addr] = data;
}

void CSimNetBoard::WriteCommRAM16(unsigned addr, uint16_t data)
{
	*(uint16_t*)&externalCommRAM[addr] = data;
}

void CSimNetBoard::WriteCommRAM32(unsigned addr, uint32_t data)
{
	*(uint32_t*)&externalCommRAM[addr] = data;
}

uint16_t CSimNetBoard::ReadIORegister(unsigned reg)
{
	if (!IsRunning())
		return 0;

	switch (reg)
	{
	case 0x00:
		return m_IRQ2ack;
	case 0x88:
		return m_status0;
	case 0x8a:
		return m_status1;
	default:
		ErrorLog("read from unknown IO register 0x%02x", reg);
		return 0;
	}
}

void CSimNetBoard::WriteIORegister(unsigned reg, uint16_t data)
{
	switch (reg)
	{
	case 0x00:
		m_IRQ2ack = data;
		break;
	case 0x88:
		m_status0 = data;
		break;
	case 0x8a:
		m_status1 = data;
		break;
	case 0xc0:
		if (data == 0)
			Reset();
		m_running = (data != 0);
		break;
	default:
		ErrorLog("write to unknown IO register 0x%02x", reg);
	}
}

// ---------------------------------------------------------------------------------------------------------
// 11/20 satellite clone (Stream/Satellite.h)
// ---------------------------------------------------------------------------------------------------------

// The link test is over when 08 has passed on the number of machines. We take the same number, and the
// index of the node we follow, as if we were that node.
bool CSimNetBoard::CloneTesting(uint8_t& numMachines, uint8_t& machineIndex)
{
	SatLinkEvent e;
	while (SatelliteTapPop(e, 0))
	{
		if (e.type != SatLinkEvent::Link)
			continue;			// frames from before the link test
		int n = e.numMachines;
		if (n < 2 || n > 8)
			continue;
		int k = (m_cloneFixed >= 1 && m_cloneFixed <= n) ? m_cloneFixed : 1;
		if (m_netlinkClient && e.node >= 1 && e.node <= n)
			k = e.node;		// 11/23 netlink: the host's node (its header), whatever the ini says
		if (m_netlinkClient && !m_netlinkWatch)
			m_cloneFixed = k;	// and stay with it (no switches)
		numMachines = (uint8_t)(n - 1);
		machineIndex = (uint8_t)(k - 1);
		g_satClone.numMachines = n;
		g_satClone.node = k;
		g_satClone.switched = true;
		g_satClone.havePacket = false;
		g_satClone.haveInputs = false;
		m_cloneHaveNext = false;
		m_cloneTimer = 0;
		printf("[Satellite] Clone: link of %d machines, following node %d\n", n, k);
		return true;
	}
	return false;
}

// Which node to follow. The clone has the world of a game only if it was that node when the game began, so:
// - while our node is in a session (link state 2 select / 4 game): stay; every m_cloneInterval frames move
//   on to the next node playing in the same group (same world, another point of view);
// - otherwise (title / attract): when a node at the title presses Start in the coming frame, become that
//   node before the frame is played, so we start the game with it.
void CSimNetBoard::CloneChoose(const std::vector<std::vector<char>>& segs, const std::vector<std::vector<char>>* next, int n)
{
	for (int m = 1; m <= n; m++)
	{
		const auto& s = segs[n - m];
		// PPC byte b of a segment is s[b ^ 2] (Model3.cpp swaps half words on the comm RAM)
		g_satClone.linkState[m] = s.size() > 0x40 ? (uint8_t)s[4 ^ 2] : 0;
		g_satClone.group[m] = s.size() > 0x40 ? (uint8_t)s[0x3C ^ 2] : 0;	// game number = its host node (+0x3C; +0x3E/+0x3F are stage / area)
	}
	// satellite.txt (in the cabinet's folder): each node's link state / flags (+8) / game mask (+0x28) once a
	// second, and every switch
	static FILE* slog = fopen("satellite.txt", "w");
	static unsigned frame = 0;
	auto word = [&](const std::vector<char>& s, unsigned b) -> uint32_t
	{
		if (s.size() < b + 4)
			return 0;
		return ((uint32_t)(uint8_t)s[b ^ 2] << 24) | ((uint32_t)(uint8_t)s[(b + 1) ^ 2] << 16) | ((uint32_t)(uint8_t)s[(b + 2) ^ 2] << 8) | (uint8_t)s[(b + 3) ^ 2];
	};
	if (slog && (++frame % 60) == 0)
	{
		fprintf(slog, "f=%u node=%d", frame, g_satClone.node);
		for (int m = 1; m <= n; m++)
		{
			const auto& s = segs[n - m];
			fprintf(slog, " | %d:st%u f%04X m%08X g%u", m, g_satClone.linkState[m], word(s, 8) & 0xFFFF, word(s, 0x28), g_satClone.group[m]);
		}
		fprintf(slog, "\n");
		fflush(slog);
	}
	// Moving to another node is safe only in plain play: the link of every node unchanged (state, the join
	// flags +8, the game mask +0x28) for 5 seconds and no join going on. (11/20 owner: with a switch in the
	// middle of 02 joining, 09 stayed on the loading screen at 99%.)
	{
		uint32_t sig = 2166136261u;
		bool joining = false;
		for (int m = 1; m <= n; m++)
		{
			const auto& s = segs[n - m];
			uint32_t v[3] = { g_satClone.linkState[m], word(s, 8) & 0xFFFF, word(s, 0x28) };
			for (uint32_t x : v)
				sig = (sig ^ x) * 16777619u;
			if (v[1] != 0)
				joining = true;
		}
		g_satClone.joining = joining;   // 11/21: LOADING... on the satellite
		if (sig != m_cloneSig || joining)
			m_cloneStable = 0;
		else if (m_cloneStable < 1000000)
			m_cloneStable++;
		m_cloneSig = sig;
	}
	if (m_cloneFixed)
		return;

	auto playing = [&](int m) { return g_satClone.linkState[m] == 4; };
	auto session = [&](int m) { return g_satClone.linkState[m] == 2 || g_satClone.linkState[m] == 4; };
	auto startPressed = [&](int m)
	{
		if (!next)
			return false;
		const auto& s = (*next)[n - m];
		if (s.size() < kSatInputOffset + 0x20 || (uint8_t)s[kSatInputOffset ^ 2] != kSatInputMagic)
			return false;
		unsigned cnt = (uint8_t)s[(kSatInputOffset + 1) ^ 2];
		for (unsigned i = 0; i < cnt && i < 4; i++)
			if (!((uint8_t)s[(kSatInputOffset + 2 + i) ^ 2] & 0x10))
				return true;
		return false;
	};
	auto alive = [&](int m) { return g_satClone.hp[m] > 0; };	// not dead / on the continue screen (a local countdown)
	auto nextState = [&](int m) -> int { return next ? (int)(uint8_t)(*next)[n - m][4 ^ 2] : -1; };
	int cur = g_satClone.node, next_ = 0;
	// The node we follow leaves the game in the coming frame (no continue) while others of its game go on:
	// become one of them first, so we stay in that game (11/20 owner: all idle, no continue).
	if (playing(cur) && next && nextState(cur) != 4)
	{
		for (int d = 1; d < n && !next_; d++)
		{
			int m = (cur - 1 + d) % n + 1;
			if (playing(m) && nextState(m) == 4 && g_satClone.group[m] == g_satClone.group[cur] && alive(m))
				next_ = m;
		}
		if (next_ && slog) { fprintf(slog, "f=%u LEAVE %d (state %d next) -> %d\n", frame, cur, nextState(cur), next_); fflush(slog); }
	}
	else if (session(cur))
	{
		// 11/21 (owner): who is shown is the chase camera / HUD's player (g_satClone.view); 11/22 the node follows it below
		// when the link is quiet (the view alone otherwise, e.g. at LOADING):
		// at the start of an area / stage change (LOADING...) the lowest-numbered player of this game, then the next one
		// every SatelliteInterval - players on the field (Rob +0 bit 1) with HP. One that has left the field (dead,
		// continue screen, gone) is moved off at once.
		auto shown = [&](int m) { return m >= 1 && m <= n && playing(m) && g_satClone.group[m] == g_satClone.group[cur] && alive(m) && g_satClone.robOn[m]; };
		static bool wasLoading = false;
		int v = g_satClone.view >= 1 && g_satClone.view <= n ? g_satClone.view : cur, nv = 0;
		if (g_satClone.loadingNow && !wasLoading)
		{
			for (int m = 1; m <= n && !nv; m++)
				if (playing(m) && g_satClone.group[m] == g_satClone.group[cur] && alive(m)) nv = m;
			if (nv && slog) { fprintf(slog, "f=%u LOADING view %d -> %d (lowest)\n", frame, v, nv); fflush(slog); }
		}
		else if (!g_satClone.loadingNow && (!shown(v) || (m_cloneInterval > 0 && ++m_cloneTimer >= m_cloneInterval)))
		{
			for (int d = 1; d <= n && !nv; d++)
			{
				int m = (v - 1 + d) % n + 1;
				if (shown(m)) nv = m;
			}
			if (nv || shown(v)) m_cloneTimer = 0;
			if (nv && nv != v && slog) { fprintf(slog, "f=%u VIEW %d -> %d\n", frame, v, nv); fflush(slog); }
			// 11/22 (owner): the node itself goes too, in plain play only (the link unchanged for 5 s, no join, our node
			// alive, the next one alive on the field). Tested with satsync.txt: 285 switches, 09 kept every player's and
			// enemy's position / HP equal to the node it followed.
			if (nv && nv != cur && m_cloneStable >= 300 && alive(cur))
				next_ = nv;
		}
		wasLoading = g_satClone.loadingNow;
		if (nv) g_satClone.view = nv;
	}
	else if (!startPressed(cur))
	{
		for (int m = 1; m <= n && !next_; m++)
			if (g_satClone.linkState[m] == 1 && startPressed(m))
				next_ = m;
	}
	if (next_ && next_ != cur)
	{
		printf("[Satellite] Clone: node %d -> %d (state %d, group %d)\n", cur, next_, g_satClone.linkState[next_], g_satClone.group[next_]);
		if (slog) { fprintf(slog, "f=%u SWITCH %d -> %d\n", frame, cur, next_); fflush(slog); }
		g_satClone.node = next_;
		g_satClone.view = next_;	// 11/21: the camera goes with it
		g_satClone.switched = true;
		g_satClone.mirrorOnce = true;
		m_cloneTimer = 0;
	}
}

bool CSimNetBoard::CloneWait(SatLinkEvent& e)
{
	for (;;)
	{
		if (m_quit)
			return false;
		if (SatelliteTapPop(e, 200))
		{
			if (m_netlinkClient && e.type == SatLinkEvent::Link)
				continue;	// 11/23 netlink: the host's link header after its state; we are linked already
			return true;
		}
	}
}

// One frame: the N segments 08 sent (like TCPReceive, we block), laid out the way the node we follow
// received them: its slot j holds node ((k - 1 - j) mod N) + 1, slot N being its own. We also take the
// next frame (the clone runs behind the ring anyway): the input records in it are what the nodes play
// the coming frame with.
bool CSimNetBoard::CloneReady(void)
{
	const int n = m_numMachines;
	auto bad = [&](const SatLinkEvent& e) { return e.type != SatLinkEvent::Frame || e.numMachines != n || (int)e.segs.size() != n; };
	SatLinkEvent cur;
	if (m_cloneHaveNext)
	{
		cur = std::move(m_cloneNext);
		m_cloneHaveNext = false;
	}
	else if (!CloneWait(cur))
		return false;
	if (bad(cur))
	{
		printf("[Satellite] Clone: link broken\n");
		g_satClone.havePacket = false;
		g_satClone.haveInputs = false;
		m_state = State::error;
		m_status1 = 0x40;
		return false;
	}
	if (m_segDump)
		SegDump(cur);
	if (!CloneWait(m_cloneNext))
		return false;
	m_cloneHaveNext = true;
	const bool nextOk = !bad(m_cloneNext);

	CloneChoose(cur.segs, nextOk ? &m_cloneNext.segs : nullptr, n);
	const int k = g_satClone.node;
	for (int j = 1; j <= n; j++)
	{
		int m = ((k - 1 - j) % n + n) % n + 1;
		const auto& s = cur.segs[n - m];
		size_t len = s.size() < m_segmentSize ? s.size() : m_segmentSize;
		memcpy(CommRAM + 0x100 + j * m_segmentSize, s.data(), len);
	}

	const auto& own = cur.segs[n - k];
	if (own.size() >= 0x900)
	{
		for (int o = 0; o < 0x900; o += 4)
		{
			uint32_t b0 = (uint8_t)own[o ^ 2], b1 = (uint8_t)own[(o + 1) ^ 2], b2 = (uint8_t)own[(o + 2) ^ 2], b3 = (uint8_t)own[(o + 3) ^ 2];
			g_satClone.packet[o / 4] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
		}
		g_satClone.havePacket = true;
	}

	g_satClone.haveInputs = false;
	if (nextOk)
		CloneTakeInputs(m_cloneNext, k);
	return true;
}

// The input record (kSatInputOffset) node k put in its segment of frame e: what its game read that frame.
bool CSimNetBoard::CloneTakeInputs(const SatLinkEvent& e, int k)
{
	const int n = (int)e.segs.size();
	if (k < 1 || k > n)
		return false;
	const auto& s = e.segs[n - k];
	auto at = [&](unsigned b) { return (uint8_t)s[(kSatInputOffset + b) ^ 2]; };
	if (s.size() < kSatInputOffset + 0x20 || at(0) != kSatInputMagic)
		return false;
	for (int p = 0; p < 3; p++)
	{
		unsigned c = at(1 + p * 5);
		g_satClone.inpCnt[p] = (uint8_t)(c > 4 ? 4 : c);
		for (unsigned i = 0; i < 4; i++)
			g_satClone.inp[p][i] = at(2 + p * 5 + i);
		if (g_satClone.inpCnt[p])
			g_satClone.inpLast[p] = g_satClone.inp[p][g_satClone.inpCnt[p] - 1];
	}
	g_satClone.haveInputs = true;
	// 11/23 netlink: the latency display - what the node read changes when our pad's change has gone round
	if (m_netlinkClient && !m_netlinkWatch)
		NetlinkLatencyInputPlayed((uint32_t)g_satClone.inpLast[0] | ((uint32_t)g_satClone.inpLast[1] << 8) | ((uint32_t)g_satClone.inpLast[2] << 16));
	return true;
}

void CSimNetBoard::ClonePrime(void)
{
	if (!m_netlinkClient || m_state != State::ready)
		return;
	SatLinkEvent e;
	if (!CloneWait(e))
		return;
	if (e.type != SatLinkEvent::Frame || e.numMachines != m_numMachines || (int)e.segs.size() != m_numMachines)
	{
		m_cloneNext = std::move(e);		// CloneReady will see it and report the link broken
		m_cloneHaveNext = true;
		return;
	}
	g_satClone.haveInputs = false;
	bool got = CloneTakeInputs(e, g_satClone.node);
	m_cloneNext = std::move(e);			// frame F+1: CloneReady takes it as "cur" at the end of our frame F+1
	m_cloneHaveNext = true;
	InfoLog("Netlink: first frame primed (node %d inputs %s).", g_satClone.node, got ? "taken" : "missing");
	NetlinkTime("client: first frame primed");   // 11/24
}

// 11/23 main6 (netlink): one frame of link data as the clone got it (see SimNetBoard.h).
void CSimNetBoard::SegDump(const SatLinkEvent& e)
{
	uint32_t f = m_segDumpFrame++;
	uint8_t n = (uint8_t)e.segs.size();
	fwrite(&f, 4, 1, m_segDump);
	fwrite(&n, 1, 1, m_segDump);
	for (const auto& s : e.segs)
	{
		uint16_t len = (uint16_t)s.size();
		fwrite(&len, 2, 1, m_segDump);
		if (len)
			fwrite(s.data(), 1, len, m_segDump);
	}
	if ((f % 600) == 0)
		fflush(m_segDump);
}

// ---------------------------------------------------------------------------------------------------------
// 11/23 main6: save / load on the same frame on every linked node
// ---------------------------------------------------------------------------------------------------------
// Before, each node saved / loaded when its own process saw the key (all nodes on one XInput button), so the
// states could be a frame apart (seen 11/23: node 1 at link frame 5350, nodes 2-8 at 5351) and the ring went on
// with one node a frame out of step.
// Record in our own segment, PPC bytes 0x8F0-0x8F7 (free: the input record is 0x8E0-0x8EF, the bot mark 0x8FC):
// +0 magic 0x5A, +1 action (1 save, 2 load), +2 slot, +3 request number, +4 target frame, +6 our frame (u16, big).
// All nodes see all records in the same ring loop; the earliest target wins (then save before load).

bool g_netStateSyncOn = false;
int g_netStateReq = 0, g_netStateReqSlot = 0;
int g_netStateDo = 0, g_netStateDoSlot = 0;

namespace
{
	const unsigned kNsOffset = 0x8F0;
	const uint8_t kNsMagic = 0x5A;
	const int kNsLead = 6;		// frames from the key to the save / load (~0.1 s)
}

void CSimNetBoard::NetStateOffer(int action, int slot, uint16_t target)
{
	if ((int16_t)(target - m_counter) < 0)
		return;		// already gone by
	if (!m_nsPlanOn || (int16_t)(target - m_nsPlanTarget) < 0 || (target == m_nsPlanTarget && action < m_nsPlanAction))
	{
		bool changed = !m_nsPlanOn || target != m_nsPlanTarget || action != m_nsPlanAction || slot != m_nsPlanSlot;
		m_nsPlanOn = true;
		m_nsPlanAction = action;
		m_nsPlanSlot = slot;
		m_nsPlanTarget = target;
		if (changed)
			InfoLog("NetState: %s slot %d at link frame %u (now %u).", action == 1 ? "save" : "load", slot, target, m_counter);
	}
}

void CSimNetBoard::NetStateBegin(void)
{
	g_netStateSyncOn = true;
	if (g_netStateReq)
	{
		if (!m_nsPlanOn && !m_nsReqOn)
		{
			m_nsReqOn = true;
			m_nsReqAction = g_netStateReq;
			m_nsReqSlot = g_netStateReqSlot;
			m_nsReqTarget = (uint16_t)(m_counter + kNsLead);
			m_nsSeq++;
		}
		else
			InfoLog("NetState: key ignored, a %s is already coming.", m_nsPlanAction == 1 ? "save" : "load");
		g_netStateReq = 0;
	}
	uint8_t* seg = CommRAM + 0x100;
	auto put = [&](unsigned b, uint8_t v) { seg[(kNsOffset + b) ^ 2] = v; };
	if (m_nsReqOn)
	{
		put(0, kNsMagic);
		put(1, (uint8_t)m_nsReqAction);
		put(2, (uint8_t)m_nsReqSlot);
		put(3, m_nsSeq);
		put(4, (uint8_t)(m_nsReqTarget >> 8));
		put(5, (uint8_t)m_nsReqTarget);
		put(6, (uint8_t)(m_counter >> 8));
		put(7, (uint8_t)m_counter);
		NetStateOffer(m_nsReqAction, m_nsReqSlot, m_nsReqTarget);
	}
	else
		for (unsigned b = 0; b < 8; b++)
			put(b, 0);
}

void CSimNetBoard::NetStateRead(const std::vector<char>& s)
{
	if (s.size() < kNsOffset + 8)
		return;
	auto at = [&](unsigned b) { return (uint8_t)s[(kNsOffset + b) ^ 2]; };
	if (at(0) != kNsMagic)
		return;
	int action = at(1);
	if (action != 1 && action != 2)
		return;
	uint16_t target = (uint16_t)((at(4) << 8) | at(5));
	uint16_t theirs = (uint16_t)((at(6) << 8) | at(7));
	// their frame numbers -> ours (the same if the link is in step, as it should be)
	NetStateOffer(action, at(2), (uint16_t)(m_counter + (int16_t)(target - theirs)));
}

void CSimNetBoard::NetStateEnd(void)
{
	if (!m_nsPlanOn)
		return;
	int16_t left = (int16_t)(m_nsPlanTarget - m_counter);
	if (left > 0)
		return;
	if (left == 0)
	{
		g_netStateDo = m_nsPlanAction;
		g_netStateDoSlot = m_nsPlanSlot;
		InfoLog("NetState: %s slot %d now, link frame %u.", m_nsPlanAction == 1 ? "save" : "load", m_nsPlanSlot, m_counter);
	}
	m_nsPlanOn = false;
	m_nsReqOn = false;
}

// Every linked spikeofe cabinet: what our game read from the inputs this frame, into our own segment.
void CSimNetBoard::PutInputRecord(void)
{
	uint8_t* seg = CommRAM + 0x100;
	auto put = [&](unsigned b, uint8_t v) { seg[(kSatInputOffset + b) ^ 2] = v; };
	put(0, (uint8_t)kSatInputMagic);
	for (int p = 0; p < 3; p++)
	{
		put(1 + p * 5, g_satInRec.cnt[p]);
		for (int i = 0; i < 4; i++)
			put(2 + p * 5 + i, g_satInRec.val[p][i]);
	}
}
