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

//----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright(C) 2001 - All Rights Reserved
//
//----------------------------------------------------------------------------
//
// Project:   RTS
//
// Module:    IO
//
// File name: StreamingArchiveFile.cpp
//
// Created:   12/06/02
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes
//----------------------------------------------------------------------------

#include "PreRTS.h"
import engine.profiling;
import engine.debug;

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include "Common/AsciiString.h"
#include "Common/FileSystem.h"
#include "Common/StreamingArchiveFile.h"

void StreamingArchiveFile::nextLine(Char *, Int)
{
	engine::debug::invariant(false, "streaming file line access", __FILE__, __LINE__,
		"Should not call nextLine on a streaming file.");
}

Bool StreamingArchiveFile::scanInt(Int &)
{
	engine::debug::invariant(false, "streaming file integer scan", __FILE__, __LINE__,
		"Should not call scanInt on a streaming file.");
	return FALSE;
}

Bool StreamingArchiveFile::scanReal(Real &)
{
	engine::debug::invariant(false, "streaming file real scan", __FILE__, __LINE__,
		"Should not call scanReal on a streaming file.");
	return FALSE;
}

Bool StreamingArchiveFile::scanString(AsciiString &)
{
	engine::debug::invariant(false, "streaming file string scan", __FILE__, __LINE__,
		"Should not call scanString on a streaming file.");
	return FALSE;
}

Bool StreamingArchiveFile::copyDataToFile(File *)
{
	engine::debug::invariant(false, "streaming file copy", __FILE__, __LINE__,
		"Are you sure you meant to copyDataToFile on a streaming file?");
	return FALSE;
}

char *StreamingArchiveFile::readEntireAndClose()
{
	engine::debug::invariant(false, "streaming file read entire", __FILE__, __LINE__,
		"Are you sure you meant to readEntireAndClose on a streaming file?");
	return nullptr;
}

File *StreamingArchiveFile::convertToRAMFile()
{
	engine::debug::invariant(false, "streaming file convert", __FILE__, __LINE__,
		"Are you sure you meant to readEntireAndClose on a streaming file?");
	return this;
}



//----------------------------------------------------------------------------
//         Externals
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Data
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Prototypes
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions
//----------------------------------------------------------------------------

//=================================================================
// StreamingArchiveFile::StreamingArchiveFile
//=================================================================

StreamingArchiveFile::StreamingArchiveFile()
: m_file(nullptr),
	m_startingPos(0),
	m_size(0),
	m_curPos(0)
{

}


//----------------------------------------------------------------------------
//         Public Functions
//----------------------------------------------------------------------------


//=================================================================
// StreamingArchiveFile::~StreamingArchiveFile
//=================================================================

StreamingArchiveFile::~StreamingArchiveFile()
{
}

//=================================================================
// StreamingArchiveFile::open
//=================================================================
/**
	* This function opens a file using the file system. Access flags
	* are mapped to the appropriate open flags. Returns true if file
	* was opened successfully.
	*/
//=================================================================

Bool StreamingArchiveFile::open( const Char *filename, Int access, size_t bufferSize )
{
	//engine::profiling::Scope profile_scope_139("StreamingArchiveFile")
	File *file = TheFileSystem->openFile( filename, access, bufferSize );

	if ( file == nullptr )
	{
		return FALSE;
	}

	return open( file );
}

//============================================================================
// StreamingArchiveFile::open
//============================================================================

Bool StreamingArchiveFile::open( File *file )
{
	return TRUE;
}

//============================================================================
// StreamingArchiveFile::openFromArchive
//============================================================================
Bool StreamingArchiveFile::openFromArchive(File *archiveFile, const AsciiString& filename, Int offset, Int size)
{
	//engine::profiling::Scope profile_scope_164("StreamingArchiveFile")
	if (archiveFile == nullptr) {
		return FALSE;
	}

	if (File::open(filename.str(), File::READ | File::BINARY | File::STREAMING) == FALSE) {
		return FALSE;
	}

	m_file = archiveFile;
	m_startingPos = offset;
	m_size = size;
	m_curPos = 0;

	if (m_file->seek(offset, File::START) != offset) {
		return FALSE;
	}

	if (m_file->seek(size) != m_startingPos + size) {
		return FALSE;
	}

	// We know this will succeed.
	m_file->seek(offset, File::START);

	m_nameStr = filename;

	return TRUE;
}

//=================================================================
// StreamingArchiveFile::close
//=================================================================
/**
	* Closes the current file if it is open.
  * Must call StreamingArchiveFile::close() for each successful StreamingArchiveFile::open() call.
	*/
//=================================================================

void StreamingArchiveFile::close()
{
	File::close();
}

//=================================================================
// StreamingArchiveFile::read
//=================================================================
// if buffer is null, just advance the current position by 'bytes'
Int StreamingArchiveFile::read( void *buffer, Int bytes )
{
	if (!m_file) {
		return 0;
	}

	// There shouldn't be a way that this can fail, because we've already verified that the file
	// contains at least this many bits.
	m_file->seek(m_startingPos + m_curPos, File::START);

	if (bytes + m_curPos > m_size)
		bytes = m_size - m_curPos;

	Int bytesRead = m_file->read(buffer, bytes);

	m_curPos += bytesRead;

	return bytesRead;
}

//=================================================================
// StreamingArchiveFile::write
//=================================================================

Int StreamingArchiveFile::write( const void *buffer, Int bytes )
{
	engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "Cannot write to streaming files.");
	return -1;
}

//=================================================================
// StreamingArchiveFile::seek
//=================================================================

Int StreamingArchiveFile::seek( Int pos, seekMode mode)
{
	Int newPos;

	switch( mode )
	{
		case START:
			newPos = pos;
			break;
		case CURRENT:
			newPos = m_curPos + pos;
			break;
		case END:
			engine::debug::invariant((pos <= 0), "pos <= 0", __FILE__, __LINE__, "StreamingArchiveFile::seek - position should be <= 0 for a seek starting from the end.");
			newPos = m_size + pos;
			break;
		default:
			// bad seek mode
			return -1;
	}

	if ( newPos < 0 )
	{
		newPos = 0;
	}
	else if ( newPos > m_size )
	{
		newPos = m_size;
	}

	m_curPos = newPos;

	return m_curPos;

}
