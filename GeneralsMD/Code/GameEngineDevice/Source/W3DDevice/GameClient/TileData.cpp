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

// TileData.cpp
// Class to handle tile data.
// Author: John Ahlquist, April 2001

#include "W3DDevice/GameClient/TileData.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"



//
// TileData - no destructor.
//

//
// TileData - create a new texture tile .
//
TileData::TileData()
{

}

Int TheTilePixelExtent = SOURCE_TILE_PIXEL_EXTENT;

Bool TileData::hasRGBDataForWidth(Int width)
{
	for (Int side = TILE_PIXEL_EXTENT; side >= 1; side /= 2) {
		if (width == side) return(true);
	}
	return(false);
}

// The mips sit one after another, largest first, so a level starts where the sizes of the levels
// above it end.  The full size level is m_tileData itself; a width that is no level gets that too.
UnsignedByte * TileData::getRGBDataForWidth(Int width)
{
	Int offset = 0;
	for (Int side = TILE_PIXEL_EXTENT/2; side >= 1; side /= 2) {
		if (width == side) return(m_tileDataMips + offset);
		offset += side*side*TILE_BYTES_PER_PIXEL;
	}
	return(m_tileData);
}

void TileData::updateMips(void)
{
	UnsignedByte *higher = m_tileData;
	for (Int side = TILE_PIXEL_EXTENT; side > 1; side /= 2) {
		UnsignedByte *lower = getRGBDataForWidth(side/2);
		doMip(higher, side, lower);
		higher = lower;
	}
}

void TileData::scaleUpFrom(Int sourceExtent)
{
	const Int extent = TILE_PIXEL_EXTENT;
	if (sourceExtent >= extent) return;

	UnsignedByte source[SOURCE_TILE_PIXEL_EXTENT*SOURCE_TILE_PIXEL_EXTENT*TILE_BYTES_PER_PIXEL];
	memcpy(source, m_tileData, sourceExtent*sourceExtent*TILE_BYTES_PER_PIXEL);
	const Real step = (Real)sourceExtent / (Real)extent;
	for (Int row = 0; row < extent; ++row) {
		// Sample at the centre of each destination pixel, so the stretched tile is not shifted
		// half a source pixel towards its corner.
		const Real y = (row + 0.5f)*step - 0.5f;
		const Int y0 = (Int)floorf(y);
		const Real fy = y - y0;
		const Int rowA = (y0 + sourceExtent) % sourceExtent;
		const Int rowB = (y0 + 1) % sourceExtent;
		for (Int column = 0; column < extent; ++column) {
			const Real x = (column + 0.5f)*step - 0.5f;
			const Int x0 = (Int)floorf(x);
			const Real fx = x - x0;
			const Int columnA = (x0 + sourceExtent) % sourceExtent;
			const Int columnB = (x0 + 1) % sourceExtent;
			for (Int p = 0; p < TILE_BYTES_PER_PIXEL; ++p) {
				const Real top = source[(rowA*sourceExtent + columnA)*TILE_BYTES_PER_PIXEL + p]*(1 - fx)
					+ source[(rowA*sourceExtent + columnB)*TILE_BYTES_PER_PIXEL + p]*fx;
				const Real bottom = source[(rowB*sourceExtent + columnA)*TILE_BYTES_PER_PIXEL + p]*(1 - fx)
					+ source[(rowB*sourceExtent + columnB)*TILE_BYTES_PER_PIXEL + p]*fx;
				m_tileData[(row*extent + column)*TILE_BYTES_PER_PIXEL + p] = (UnsignedByte)(top*(1 - fy) + bottom*fy + 0.5f);
			}
		}
	}
}


void TileData::doMip(UnsignedByte *pHiRes, Int hiRow, UnsignedByte *pLoRes) 
{
	Int i, j;
	for (i=0; i<hiRow; i+=2) {
		for (j=0; j<hiRow; j+=2) {
			Int pxl;
			Int ndx = (j*hiRow+i)*TILE_BYTES_PER_PIXEL;
			Int loNdx = (j/2)*(hiRow/2) + (i/2);
			loNdx *= TILE_BYTES_PER_PIXEL;
			Int p;
			for (p=0; p<TILE_BYTES_PER_PIXEL; p++,ndx++,loNdx++) {
				pxl = pHiRes[ndx] + pHiRes[ndx+TILE_BYTES_PER_PIXEL] + pHiRes[ndx+TILE_BYTES_PER_PIXEL*hiRow] + pHiRes[ndx+TILE_BYTES_PER_PIXEL*hiRow+TILE_BYTES_PER_PIXEL] +2;
				pxl /= 4;
				pLoRes[loNdx] = pxl;
			}
		}
	}

}

