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

// CRC.h ///////////////////////////////////////////////////////////////
// A class encapsulating CRC calculation
// Author: Matthew D. Campbell, October 2001

#pragma once

#ifndef _CRC_H_
#define _CRC_H_

#include "Lib/BaseType.h"
#include "winsock2.h" // for htonl

#ifdef _DEBUG

class CRC
{
public:
	CRC() { crc = 0; }

	void computeCRC( const void *buf, Int len );		///< Compute the CRC for a buffer, added into current CRC
	void clear( void ) { crc = 0; }									///< Clears the CRC to 0
//	UnsignedInt get( void ) { return htonl(crc); }	///< Get the combined CRC
	UnsignedInt get( void );

private:
	void addCRC( UnsignedByte val );									///< CRC a 4-byte block

	UnsignedInt crc;
};

#else

// optimized inline only version
class CRC
{
public:
	CRC(void) { crc=0; }

  /// Compute the CRC for a buffer, added into current CRC
	__forceinline void computeCRC( const void *buf, Int len )
  {
    if (!buf||len<1)
      return;
    
    // EA wrote this in assembly and kept the C++ it was verified against in a comment; the
    // assembly is gone and this is that C++.  shl sets the carry from the top bit and adc adds it
    // back with the data byte, which is exactly the two lines below.  Every multiplayer and replay
    // CRC in the game goes through here, so it has to stay bit identical: 32-bit wraparound
    // arithmetic on UnsignedInt is what does that.
    for (const UnsignedByte *bytePtr=(const UnsignedByte *)buf; len>0; --len, ++bytePtr)
    {
      const UnsignedInt hibit = (crc & 0x80000000) ? 1 : 0;
      crc <<= 1;
      crc += *bytePtr;
      crc += hibit;
    }
  }

  /// Clears the CRC to 0
	void clear( void ) 
  { 
    crc = 0; 
  }									

  ///< Get the combined CRC
	UnsignedInt get( void ) const
  {
    return crc;
  }

private:
	UnsignedInt crc;
};

#endif

#endif // _CRC_H_
