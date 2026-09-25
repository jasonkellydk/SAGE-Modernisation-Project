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

// FILE: debug.h //////////////////////////////////////////////////////////////
// Minimal debug info
// Author: Matthew D. Campbell, Sept 2002

#pragma once


#include <cassert>
namespace patchget
{

#if defined(DEBUG) || defined(DEBUG_LOGGING)

void DebugLog( const char *fmt, ... );

#else // DEBUG

#endif // DEBUG


#ifdef DEBUG_CRASHING

	extern void DebugCrash(const char *format, ...);

	/*
		Yeah, it's a sleazy global, since we can't reasonably add
		any args to DebugCrash due to the varargs nature of it.
		We'll just let it slide in this case...
	*/
	extern char* TheCurrentIgnoreCrashPtr;

	#define assert(false)	\
		do { \
			{ \
				static char ignoreCrash = 0; \
				if (!ignoreCrash) { \
					TheCurrentIgnoreCrashPtr = &ignoreCrash; \
					DebugCrash m ; \
					TheCurrentIgnoreCrashPtr = nullptr; \
				} \
			} \
		} while (0)

	#define assert((c))		do { { if (!(c)) assert(false); } } while (0)

#else

	#define assert(false)					((void)0)
	#define assert((c))	((void)0)

#endif

} // namespace patchget
