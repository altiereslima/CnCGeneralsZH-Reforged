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
// Modified 2026 by İlyas Akın for the macOS/Linux port: moved here from GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/WorldHeightMap.h; see NOTICE.md and the git history.

// FILE: WorldHeightMapData.h /////////////////////////////////////////////////////////////////////
// Desc:   A map's terrain as the simulation reads it: heights, cells, cliff bits, and the chunks the
//         map file holds them in.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* WorldHeightMap's data half (T1).  The simulation's ground - every getGroundHeight, cliff test and
	 line of sight - is sampled from a WorldHeightMap's heights and cliff bits, and until T1 all of it
	 lived in GameEngineDevice's W3D code.  This class is that data and its parsing, moved out verbatim;
	 WorldHeightMap derives from it and keeps everything that draws (tiles, textures, blends).

	 Every member is declared here in the order it had in WorldHeightMap, and every function body is the
	 text it had there, only the class name in front changed.  Two things are new:

	 - parseBlendTileCells is the first half of WorldHeightMap::ParseBlendTileData, cut where the terrain
		 textures start (everything the simulation reads from that chunk comes before);
		 WorldHeightMap::ParseBlendTileData calls it and goes on to the textures.
	 - parseHeightsAndCells and parseLogicalMap read a map file into this class alone, for an engine
		 with no W3D terrain.  Their chunk callbacks are this class's own: DataChunkInput hands each
		 callback back the pointer parse() was given, and with WorldHeightMap deriving from RefCountClass
		 first, a WorldHeightMap* and this class's part of it are not the same address, so each class casts
		 back only what it passed in.

	 No virtual functions: this is a data base, and a WorldHeightMap is never deleted through it. */

#pragma once

#ifndef WORLDHEIGHTMAPDATA_H
#define WORLDHEIGHTMAPDATA_H

#include "Lib/BaseType.h"

#include <vector>

typedef std::vector<ICoord2D> VecICoord2D;

#define K_MIN_HEIGHT  0
#define K_MAX_HEIGHT  255

class ChunkInputStream;
class DataChunkInput;
struct DataChunkInfo;

class WorldHeightMapData
{
public:
	WorldHeightMapData(void);
	~WorldHeightMapData(void);

	/// Read a map file's HeightMapData and BlendTileData cells into this object alone.
	Bool parseHeightsAndCells(ChunkInputStream *pStrm);
	/// parseHeightsAndCells, and the chunks W3DTerrainLogic's logical-data-only map read: the world
	/// dictionary, the map objects, the polygon triggers and the sides, into their globals.
	Bool parseLogicalMap(ChunkInputStream *pStrm);

protected:
	Int m_width;				///< Height map width.
	Int m_height;				///< Height map height (y size of array).
	Int m_borderSize;		///< Non-playable border area.
	VecICoord2D m_boundaries;	///< the in-game boundaries
	Int m_dataSize;			///< size of m_data.
	UnsignedByte *m_data;	///< array of z(height) values in the height map.

  UnsignedByte *m_seismicUpdateFlag;  ///< array of bits to prevent ovelapping physics-update regions from doubling effects on shared cells
  UnsignedInt   m_seismicUpdateWidth; ///< width of the array holding SeismicUpdateFlags
  Real         *m_seismicZVelocities; ///< how fast is the dirt rising/falling at this location

  UnsignedByte *m_cellFlipState;	///< array of bits to indicate the flip state of each cell.
	Int m_flipStateWidth;			///< with of the array holding cellFlipState
	UnsignedByte *m_cellCliffState;	///< array of bits to indicate the cliff state of each cell.

	/// Texture indices.
	Short  *m_tileNdxes;  ///< matches m_Data, indexes into m_SourceTiles.
	Short  *m_blendTileNdxes;  ///< matches m_Data, indexes into m_blendedTiles.  0 means no blend info.
	Short  *m_cliffInfoNdxes;  ///< matches m_Data, indexes into m_cliffInfo.	 0 means no cliff info.
	Short  *m_extraBlendTileNdxes;  ///< matches m_Data, indexes into m_extraBlendedTiles.  0 means no blend info.

protected:
	void initCliffFlagsFromHeights(void);
	void setCellCliffFlagFromHeights(Int xIndex, Int yIndex);
	void setCliffState(Int xIndex, Int yIndex, Bool state);

protected:	 // file readers, WorldHeightMap's as they were
	Bool ParseHeightMapData(DataChunkInput &file, DataChunkInfo *info, void *userData);
	Bool ParseSizeOnly(DataChunkInput &file, DataChunkInfo *info, void *userData);
	Bool parseBlendTileCells(DataChunkInput &file, DataChunkInfo *info);
	static Bool ParseWorldDictDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	Bool ParseObjectData(DataChunkInput &file, DataChunkInfo *info, void *userData, Bool readDict);

private:	 // this class's own chunk callbacks, for parseHeightsAndCells and parseLogicalMap
	static Bool parseHeightMapDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool parseBlendTileCellsChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool parseObjectsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool parseObjectDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);

public:  // Boundary info
	const VecICoord2D& getAllBoundaries(void) const { return m_boundaries; }

public:  // height map info.
	static Int getMinHeightValue(void) {return K_MIN_HEIGHT;}
	static Int getMaxHeightValue(void) {return K_MAX_HEIGHT;}

	UnsignedByte *getDataPtr(void) {return m_data;}

	Int getXExtent(void) {return m_width;}	///<number of vertices in x
	Int getYExtent(void) {return m_height;}	///<number of vertices in y

  inline Int getBorderSizeInline(void) const { return m_borderSize; }

	/// Get height in normal coordinates.
	inline UnsignedByte getHeight(Int xIndex, Int yIndex)
	{
		Int ndx = (yIndex*m_width)+xIndex;
		if ((ndx>=0) && (ndx<m_dataSize) && m_data)
			return(m_data[ndx]);
		else
			return(0);
	};

	static void freeListOfMapObjects(void);

public:  // cell bits
	Bool getFlipState(Int xIndex, Int yIndex) const;
	///Faster version of above function without all the safety checks - For people that do checks externally.
	inline Bool getQuickFlipState(Int xIndex, Int yIndex) const
	{
		return m_cellFlipState[yIndex*m_flipStateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
	}
	void setFlipState(Int xIndex, Int yIndex, Bool value);
	void clearFlipStates(void);

	Bool getCliffState(Int xIndex, Int yIndex) const;

  Bool getSeismicUpdateFlag(Int xIndex, Int yIndex) const;
  void setSeismicUpdateFlag(Int xIndex, Int yIndex, Bool value);
  void clearSeismicUpdateFlags(void) ;
  void fillSeismicZVelocities( Real value );

public:  // modify height value
	void setRawHeight(Int xIndex, Int yIndex, UnsignedByte height) {
		Int ndx = (yIndex*m_width)+xIndex;
		if ((ndx>=0) && (ndx<m_dataSize) && m_data) m_data[ndx]=height;
	};
};

#endif // WORLDHEIGHTMAPDATA_H
