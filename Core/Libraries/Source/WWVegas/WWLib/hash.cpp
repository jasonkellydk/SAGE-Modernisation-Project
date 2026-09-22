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

/* $Header: /VSS_Sync/wwlib/hash.cpp 3     10/17/00 4:48p Vss_sync $ */
/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando / G 3D Library                                      *
 *                                                                                             *
 *                     $Archive:: /VSS_Sync/wwlib/hash.cpp                                    $*
 *                                                                                             *
 *                       Author:: Greg_h                                                       *
 *                                                                                             *
 *                     $Modtime:: 10/16/00 11:42a                                             $*
 *                                                                                             *
 *                    $Revision:: 3                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "hash.h"

#include "realcrc.h"
import engine.debug;

/*
** HashTableClass
*/
HashTableClass::HashTableClass( int size ) :
	HashTableSize( size )
{
	// Assert HashTableSize is a power of 2
	engine::debug::assert_condition(((HashTableSize & (HashTableSize-1)) == 0), "(HashTableSize & (HashTableSize-1)) == 0", __FILE__, __LINE__, "assertion failed");

	// Allocate and clear the table
	HashTable = W3DNEWARRAY HashableClass * [ HashTableSize ];
	Reset();
}

HashTableClass::~HashTableClass()
{
	// If we need to, free the hash table
	delete [] HashTable;
	HashTable = nullptr;
}

void	HashTableClass::Reset()
{
	for ( int i = 0; i < HashTableSize; i++ ) {
		HashTable[i] = nullptr;
	}
}

void	HashTableClass::Add( HashableClass * entry )
{
	engine::debug::assert_condition((entry != nullptr), "entry != nullptr", __FILE__, __LINE__, "assertion failed");

	int index = Hash( entry->Get_Key() );
	engine::debug::assert_condition((entry->NextHash == nullptr), "entry->NextHash == nullptr", __FILE__, __LINE__, "assertion failed");
	entry->NextHash = HashTable[ index ];
	HashTable[ index ] = entry;
}

bool	HashTableClass::Remove( HashableClass * entry )
{
	engine::debug::assert_condition((entry != nullptr), "entry != nullptr", __FILE__, __LINE__, "assertion failed");

	// Find in the hash table.
	const char *key = entry->Get_Key();
	int index = Hash( key );

	if ( HashTable[ index ] != nullptr ) {

		// Special check for first entry
		if ( HashTable[ index ] == entry ) {
			HashTable[ index ] = entry->NextHash;
			return true;
		}

		// Search the list for the entry, and remove it
		HashableClass * node = HashTable[ index ];
		while ( node->NextHash != nullptr ) {
			if ( node->NextHash == entry ) {
				node->NextHash = entry->NextHash;
				return true;
			}
			node = node->NextHash;
		}
	}

	return false;
}

HashableClass * HashTableClass::Find( const char * key )
{
	// Find in the hash table.
	int index = Hash( key );
	for ( HashableClass * node = HashTable[ index ]; node != nullptr; node = node->NextHash ) {
		if ( ::stricmp( node->Get_Key(), key ) == 0 ) {
			return node;
		}
	}
	return nullptr;
}

int	HashTableClass::Hash( const char * key )
{
	return CRC_Stringi( key ) & (HashTableSize-1);
}


/*
**
*/
void	HashTableIteratorClass::First()
{
	Index = 0;
	NextEntry = Table.HashTable[ Index ];
	Advance_Next();
	Next();		// Accept the next we found, and go to the next next
}

void	HashTableIteratorClass::Next()
{
	CurrentEntry = NextEntry;
	if ( NextEntry != nullptr ) {
		NextEntry = NextEntry->NextHash;
		Advance_Next();
	}
}

void	HashTableIteratorClass::Advance_Next()
{
	while ( NextEntry == nullptr ) {
		Index++;
		if ( Index >= Table.HashTableSize ) {
			return;	// Done!
		}
		NextEntry = Table.HashTable[ Index ];
	}
}


