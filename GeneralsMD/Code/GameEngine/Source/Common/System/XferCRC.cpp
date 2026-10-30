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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: XferCRC.cpp //////////////////////////////////////////////////////////////////////////////
// Author: Matt Campbell, February 2002
// Desc:   Xfer CRC implementation
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "zhio.h"

#include "Common/XferCRC.h"
#include "Common/XferDeepCRC.h"
#include "Common/crc.h"
#include "Common/Snapshot.h"
#include <stdlib.h>

/* The CRC is defined on words in network byte order, which every target this builds for (x64 and
	 ARM64, little-endian all) reaches with one byte swap.  It used to be Winsock's htonl, an imported
	 call per word that also made the compiler store m_crc back after each one; a whole game CRC late
	 in a 4-player Twilight Flame match took 1.26 ms that way and takes 0.87 ms now. */
static inline UnsignedInt crcSwap( UnsignedInt v )
{
#if defined(_MSC_VER)
	return _byteswap_ulong( v );
#else
	return __builtin_bswap32( v );
#endif
}

// One 4-byte step: the running CRC rotated left a bit, plus the word in network byte order.
static inline UnsignedInt crcFold( UnsignedInt crc, UnsignedInt word )
{
	return (crc << 1) + crcSwap( word ) + (crc >> 31);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
XferCRC::XferCRC( void )
{

	m_xferMode = XFER_CRC;
	//Added By Sadullah Nader
	//Initialization(s) inserted
	m_crc = 0;
	//
}  // end XferCRC

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
XferCRC::~XferCRC( void )
{

}  // end ~XferCRC

//-------------------------------------------------------------------------------------------------
/** Open file 'identifier' for writing */
//-------------------------------------------------------------------------------------------------
void XferCRC::open( AsciiString identifier )
{

	// call base class
	Xfer::open( identifier );

	// initialize CRC to brand new one at zero
	m_crc = 0;

}  // end open

//-------------------------------------------------------------------------------------------------
/** Close our current file */
//-------------------------------------------------------------------------------------------------
void XferCRC::close( void )
{

}  // end close

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Int XferCRC::beginBlock( void )
{

	return 0;

}  // end beginBlock

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void XferCRC::endBlock( void )
{

}  // end endBlock

// ------------------------------------------------------------------------------------------------
/** Entry point for xfering a snapshot */
// ------------------------------------------------------------------------------------------------
void XferCRC::xferSnapshot( Snapshot *snapshot )
{

	if( snapshot == NULL )
	{

		return;

	}  // end if

	// run the crc function of the snapshot
	snapshot->crc( this );

}  // end xferSnapshot

//-------------------------------------------------------------------------------------------------
/** Perform a single CRC operation on the data passed in */
//-------------------------------------------------------------------------------------------------
void XferCRC::xferImplementation( void *data, Int dataSize )
{

	if (!data || dataSize < 1)
	{
		return;
	}

	const UnsignedInt *uintPtr = (const UnsignedInt *) (data);

	// a local, so the compiler keeps it in a register instead of storing m_crc after every word
	UnsignedInt crc = m_crc;
	for (Int i=0 ; i<dataSize/4 ; i++)
	{
		crc = crcFold( crc, *uintPtr++ );
	}

	int leftover = dataSize & 3;
	if (leftover)
	{
		UnsignedInt val = 0;
		const unsigned char *c = (const unsigned char *)uintPtr;
		for (Int i=0; i<leftover; i++)
		{
			val += (c[i] << (i*8));
		}
		// swapped here and again in the fold, as EA did, so the tail goes in unswapped
		crc = crcFold( crc, crcSwap( val ) );
	}
	m_crc = crc;

}  // end xferImplementation

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void XferCRC::skip( Int dataSize )
{

}  // end skip

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UnsignedInt XferCRC::getCRC( void )
{

	return crcSwap( m_crc );

}  // end skip


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
XferDeepCRC::XferDeepCRC( void )
{

	m_xferMode = XFER_SAVE;
	m_fileFP = NULL;

}  // end XferCRC

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
XferDeepCRC::~XferDeepCRC( void )
{

	// warn the user if a file was left open
	if( m_fileFP != NULL )
	{

		DEBUG_CRASH(( "Warning: Xfer file '%s' was left open\n", m_identifier.str() ));
		close();

	}  // end if

}  // end ~XferCRC

//-------------------------------------------------------------------------------------------------
/** Open file 'identifier' for writing */
//-------------------------------------------------------------------------------------------------
void XferDeepCRC::open( AsciiString identifier )
{

	m_xferMode = XFER_SAVE;

	// sanity, check to see if we're already open
	if( m_fileFP != NULL )
	{

		DEBUG_CRASH(( "Cannot open file '%s' cause we've already got '%s' open\n",
									identifier.str(), m_identifier.str() ));
		throw XFER_FILE_ALREADY_OPEN;

	}  // end if

	// call base class
	Xfer::open( identifier );

	// open the file
	m_fileFP = zh_fopen( identifier.str(), "w+b" );
	if( m_fileFP == NULL )
	{
		
		DEBUG_CRASH(( "File '%s' not found\n", identifier.str() ));
		throw XFER_FILE_NOT_FOUND;

	}  // end if

	// initialize CRC to brand new one at zero
	m_crc = 0;

}  // end open

//-------------------------------------------------------------------------------------------------
/** Close our current file */
//-------------------------------------------------------------------------------------------------
void XferDeepCRC::close( void )
{

	// sanity, if we don't have an open file we can do nothing
	if( m_fileFP == NULL )
	{

		DEBUG_CRASH(( "Xfer close called, but no file was open\n" ));
		throw XFER_FILE_NOT_OPEN;

	}  // end if

	// close the file
	fclose( m_fileFP );
	m_fileFP = NULL;

	// erase the filename
	m_identifier.clear();

}  // end close

//-------------------------------------------------------------------------------------------------
/** Perform a single CRC operation on the data passed in */
//-------------------------------------------------------------------------------------------------
void XferDeepCRC::xferImplementation( void *data, Int dataSize )
{

	if (!data || dataSize < 1)
	{
		return;
	}

	// sanity
	DEBUG_ASSERTCRASH( m_fileFP != NULL, ("XferSave - file pointer for '%s' is NULL\n",
										 m_identifier.str()) );

	// write data to file
	if( fwrite( data, dataSize, 1, m_fileFP ) != 1 )
	{

		DEBUG_CRASH(( "XferSave - Error writing to file '%s'\n", m_identifier.str() ));
		throw XFER_WRITE_ERROR;

	}  // end if

	XferCRC::xferImplementation( data, dataSize );

}  // end xferImplementation

// ------------------------------------------------------------------------------------------------
/** Save ascii string */
// ------------------------------------------------------------------------------------------------
void XferDeepCRC::xferMarkerLabel( AsciiString asciiStringData )
{

}  // end xferAsciiString

// ------------------------------------------------------------------------------------------------
/** Save ascii string */
// ------------------------------------------------------------------------------------------------
void XferDeepCRC::xferAsciiString( AsciiString *asciiStringData )
{

	// sanity
	if( asciiStringData->getLength() > 16385 )
	{

		DEBUG_CRASH(( "XferSave cannot save this ascii string because it's too long.  Change the size of the length header (but be sure to preserve save file compatability\n" ));
		throw XFER_STRING_ERROR;

	}  // end if

	// save length of string to follow
	UnsignedShort len = asciiStringData->getLength();
	xferUnsignedShort( &len );

	// save string data
	if( len > 0 )
		xferUser( (void *)asciiStringData->str(), sizeof( Byte ) * len );

}  // end xferAsciiString

// ------------------------------------------------------------------------------------------------
/** Save unicodee string */
// ------------------------------------------------------------------------------------------------
void XferDeepCRC::xferUnicodeString( UnicodeString *unicodeStringData )
{
	
	// sanity
	if( unicodeStringData->getLength() > 255 )
	{

		DEBUG_CRASH(( "XferSave cannot save this unicode string because it's too long.  Change the size of the length header (but be sure to preserve save file compatability\n" ));
		throw XFER_STRING_ERROR;

	}  // end if

	// save length of string to follow
	Byte len = unicodeStringData->getLength();
	xferByte( &len );

	// save string data
	if( len > 0 )
		xferUser( (void *)unicodeStringData->str(), sizeof( WideChar ) * len );

}  // end xferUnicodeString
