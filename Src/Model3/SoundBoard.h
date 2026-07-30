/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011 Bart Trzynadlowski, Nik Henson 
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
 * SoundBoard.h
 * 
 * Model 3 sound board. Header file for the CSoundBoard class.
 */

#ifndef _SOUNDBOARD_H_
#define _SOUNDBOARD_H_

#include "Supermodel.h"
#include "CPU/Bus.h"
#include "CPU/68K/68K.h"
#include "Model3/DSB.h"
#include "OSD/Audio.h"
#include "Util/NewConfig.h"
#include <string>

class CSoundBoard : public IBus
{
public:

	void AttachDSB(CDSB *DSBPtr);

	/**************************************************************************
	 68K Address Space Handlers
	**************************************************************************/

	void UpdateROMBanks(void);

	UINT8 Read8(UINT32 a);
	UINT16 Read16(UINT32 a);
	UINT32 Read32(UINT32 a);
	void Write8(unsigned int a,unsigned char d);
	void Write16(unsigned int a,unsigned short d);
	void Write32(unsigned int a,unsigned int d);

	/**************************************************************************
	 Sound Board Interface
	**************************************************************************/

	void WriteMIDIPort(UINT8 data);
	bool RunFrame(void);

	void Reset(void);

	void SaveState(CBlockFile *SaveState);
	void LoadState(CBlockFile *SaveState);

	/**************************************************************************
	 Configuration, Initialization, and Shutdown
	**************************************************************************/

	void SetGameName(const std::string &gameName) { m_gameName = gameName; }
	Result Init(const UINT8 *soundROMPtr, const UINT8 *sampleROMPtr);
	M68KCtx *GetM68K(void);
	CDSB *GetDSB(void);

	CSoundBoard(const Util::Config::Node &config);
	~CSoundBoard(void);

private:

	const Util::Config::Node &m_config;
	std::string m_gameName;

	UINT8	*memoryPool;
	UINT8	*ram1, *ram2;
	float	*audioFL, *audioFR, *audioRL, *audioRR;
	
	const UINT8	*soundROM, *sampleROM;
	const UINT8	*sampleBank;
	UINT8		ctrlReg;
	
	M68KCtx	M68K;

	CDSB	*DSB;
};

#endif // _SOUNDBOARD_H_
