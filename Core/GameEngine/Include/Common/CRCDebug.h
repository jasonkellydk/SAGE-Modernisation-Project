/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// CRCDebug.h ///////////////////////////////////////////////////////////////
// Macros/functions/etc to help logging values for tracking down sync errors
// Author: Matthew D. Campbell, June 2002

#pragma once



#ifndef NO_DEBUG_CRC
		#define DEBUG_CRC
#endif

#ifdef DEBUG_CRC

#include "Common/AsciiString.h"
#include "GameLogic/GameLogic.h"
#include "Lib/BaseType.h"
import Engine.Core.Math.AffineTransform3;

	#define AS_INT(x) (*(Int *)(&x))
	#define DUMPVEL DUMPCOORD3DNAMED(&m_vel, "m_vel")
	#define DUMPACCEL DUMPCOORD3DNAMED(&m_accel, "m_accel")
	#define DUMPCOORD3D(x) DUMPCOORD3DNAMED(x, #x)
	#define DUMPCOORD3DNAMED(x, y) dumpCoord3D(x, y, __FILE__, __LINE__)
	#define DUMPTRANSFORM(x) DUMPTRANSFORMNAMED(x, #x)
	#define DUMPTRANSFORMNAMED(x, y) dumpTransform(x, y, __FILE__, __LINE__)
	#define DUMPREAL(x) DUMPREALNAMED(x, #x)
	#define DUMPREALNAMED(x, y) dumpReal(x, y, __FILE__, __LINE__)

	extern Int TheCRCFirstFrameToLog;
	extern UnsignedInt TheCRCLastFrameToLog;

	void dumpCoord3D(const Coord3D *c, AsciiString name, AsciiString fname, Int line);
	void dumpTransform(const Engine::Math::AffineTransform3 &transform, AsciiString name, AsciiString fname, Int line);
	void dumpReal(Real r, AsciiString name, AsciiString fname, Int line);

	void outputCRCDebugLines();
	void CRCDebugStartNewGame();
	void outputCRCDumpLines();

	void addCRCDebugLine(const char *fmt, ...);
	void addCRCDebugLineNoCounter(const char *fmt, ...);
	void addCRCDumpLine(const char *fmt, ...);
	void addCRCGenLine(const char *fmt, ...);
	#define CRCDEBUG_LOG(x) addCRCDebugLine x
	#define CRCDUMP_LOG(x) addCRCDumpLine x
	#define CRCGEN_LOG(x) addCRCGenLine x

	class CRCVerification
	{
	public:
		CRCVerification();
		~CRCVerification();
	protected:
		UnsignedInt m_startCRC;
	};
	#define VERIFY_CRC CRCVerification crcVerification;

	extern Int lastCRCDebugFrame;
	extern Int lastCRCDebugIndex;

	extern Bool g_verifyClientCRC;
	extern Bool g_clientDeepCRC;

	extern Bool g_crcModuleDataFromClient;
	extern Bool g_crcModuleDataFromLogic;

	extern Bool g_keepCRCSaves;
	extern Bool g_saveDebugCRCPerFrame;
	extern AsciiString g_saveDebugCRCPerFrameDir;

	extern Bool g_logObjectCRCs;

#else // DEBUG_CRC

	#define DUMPVEL
	#define DUMPACCEL
	#define DUMPCOORD3D(x)
	#define DUMPCOORD3DNAMED(x, y)
	#define DUMPTRANSFORM(x)
	#define DUMPTRANSFORMNAMED(x, y)

	#define DUMPREAL(x)
	#define DUMPREALNAMED(x, y)

	#define CRCDEBUG_LOG(x)
	#define CRCDUMP_LOG(x)
	#define CRCGEN_LOG(x)

	#define VERIFY_CRC

#endif

extern Int NET_CRC_INTERVAL;
extern Int REPLAY_CRC_INTERVAL;
extern Bool TheDebugIgnoreSyncErrors;
