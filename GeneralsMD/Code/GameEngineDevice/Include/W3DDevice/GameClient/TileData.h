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

// TileData.h
// Class to hold 1 tile's data.
// Author: John Ahlquist, April 2001

#pragma once

#ifndef TileData_H
#define TileData_H

#include <stdio.h>

#include "Lib/BaseType.h"
#include "WWLib/RefCount.h"
#include "Common/AsciiString.h"

typedef struct {
	Int blendNdx;
	UnsignedByte horiz;
	UnsignedByte vert;
	UnsignedByte rightDiagonal;
	UnsignedByte leftDiagonal;
	UnsignedByte inverted;
	UnsignedByte longDiagonal;
	Int customBlendEdgeClass; // Class of texture for a blend edge.  -1 means use alpha. 
} TBlendTileInfo;

#define INVERTED_MASK	0x1		//AND this with TBlendTileInfo.inverted to get actual inverted state
#define FLIPPED_MASK	0x2		//AND this with TBlendTileInfo.inverted to get forced flip state (for horizontal/vertical flips).
// A terrain tile's side in the atlas.  EA fixed it at 64; a run that draws doubles it, set once
// before any map loads (W3DDisplay::init).  The shipped images and every map are laid out in 64 pixel tiles,
// which is SOURCE_TILE_PIXEL_EXTENT, and a source image is read in those and scaled to this one.
extern Int TheTilePixelExtent;
#define TILE_PIXEL_EXTENT TheTilePixelExtent
#define SOURCE_TILE_PIXEL_EXTENT 64
#define MAX_TILE_PIXEL_EXTENT 128
#define TILE_BYTES_PER_PIXEL 4
#define DATA_LEN_BYTES MAX_TILE_PIXEL_EXTENT*MAX_TILE_PIXEL_EXTENT*TILE_BYTES_PER_PIXEL
// Every mip below the largest, down to one pixel: a third of the largest level, rounded up.
#define MIP_LEN_BYTES (DATA_LEN_BYTES/3 + TILE_BYTES_PER_PIXEL)
// The atlas is 32 tiles wide however big a tile is, so a map's layout and the cliff coordinates
// it stores, which are fractions of this width, come out the same at either extent.
#define TEXTURE_WIDTH (32*TILE_PIXEL_EXTENT) // was 1024 jba

/** This class holds the bitmap data from the .tga texture files.  It is used to 
create the D3D texture in the game and 3d windows, and to create DIB data for the 
2d window. */
class TileData : public RefCountClass
{
protected: 

	// data is bgrabgrabgra to be compatible with windows blt. jba.
	// Also, first byte is lower left pixel, not upper left pixel.
	// so 0,0 is lower left, not upper left.
	UnsignedByte m_tileData[DATA_LEN_BYTES];
	/// Mipped down copies of the tile data, each half the side of the one before, packed in order.
	UnsignedByte m_tileDataMips[MIP_LEN_BYTES];

public:
	ICoord2D	m_tileLocationInTexture;


protected:
	/** doMip - generates the next mip level mipping pHiRes down to pLoRes.
				pLoRes is 1/2 the width of pHiRes, and both are square. */
	static void doMip(UnsignedByte *pHiRes, Int hiRow, UnsignedByte *pLoRes);



public:
	TileData(void);

public:
	UnsignedByte *getDataPtr(void) {return(m_tileData);};
	static Int dataLen(void) {return(TILE_PIXEL_EXTENT*TILE_PIXEL_EXTENT*TILE_BYTES_PER_PIXEL);};

	void updateMips(void);

	/** The tile was filled at sourceExtent, which is smaller than TILE_PIXEL_EXTENT: stretch it to
			fill the tile, bilinearly, wrapping at its own edges.  That is right for a texture class of
			one tile; in a bigger one the neighbour is another tile, so the edge can show a faint seam,
			which a source image already at the full extent does not have. */
	void scaleUpFrom(Int sourceExtent);

	Bool hasRGBDataForWidth(Int width);
	UnsignedByte *getRGBDataForWidth(Int width);
};

#endif