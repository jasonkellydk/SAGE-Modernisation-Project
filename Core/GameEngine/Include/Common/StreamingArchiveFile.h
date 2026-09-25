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

//----------------------------------------------------------------------------=
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright(C) 2001 - All Rights Reserved
//
//----------------------------------------------------------------------------
//
// Project:    RTS 3
//
// Module:     IO
//
// File name:  Common/StreamingArchiveFile.h
//
// Created:    11/08/01
//
//----------------------------------------------------------------------------

#pragma once

//----------------------------------------------------------------------------
//           Includes
//----------------------------------------------------------------------------

#include "Common/RAMFile.h"

//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------

//===============================
// StreamingArchiveFile
//===============================
/**
  *	File abstraction for standard C file operators: open, close, lseek, read, write
	*/
//===============================

class StreamingArchiveFile : public RAMFile
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(StreamingArchiveFile, "StreamingArchiveFile")
	protected:

		File					*m_file;											///< The archive file that I came from
		Int						m_startingPos;								///< My starting position in the archive
		Int						m_size;												///< My length
		Int						m_curPos;											///< My current position.

	public:

		StreamingArchiveFile();
		//virtual				~StreamingArchiveFile();


		virtual Bool	open( const Char *filename, Int access = NONE, size_t bufferSize = BUFFERSIZE ) override; ///< Open a file for access
		virtual void	close() override;																			///< Close the file
		virtual Int		read( void *buffer, Int bytes ) override;										///< Read the specified number of bytes in to buffer: See File::read
		virtual Int		write( const void *buffer, Int bytes ) override;							///< Write the specified number of bytes from the buffer: See File::write
		virtual Int		seek( Int new_pos, seekMode mode = CURRENT ) override;				///< Set file position: See File::seek

		// Ini's should not be parsed with streaming files, that's just dumb.
		virtual void	nextLine(Char *buf = nullptr, Int bufSize = 0) override;
		virtual Bool	scanInt(Int &newInt) override;
		virtual Bool	scanReal(Real &newReal) override;
		virtual Bool	scanString(AsciiString &newString) override;

		virtual Bool	open( File *file ) override;																	///< Open file for fast RAM access
		virtual Bool	openFromArchive(File *archiveFile, const AsciiString& filename, Int offset, Int size) override; ///< copy file data from the given file at the given offset for the given size.
		virtual Bool	copyDataToFile(File *localFile) override;

		virtual char* readEntireAndClose() override;
		virtual File* convertToRAMFile() override;
};




//----------------------------------------------------------------------------
//           Inlining
//----------------------------------------------------------------------------
