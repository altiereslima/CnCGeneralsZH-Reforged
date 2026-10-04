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
// Modified 2026 by İlyas Akın for the macOS/Linux port: moved here from GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp; see NOTICE.md and the git history.

// FILE: WorldHeightMapData.cpp ///////////////////////////////////////////////////////////////////
// Desc:   WorldHeightMap's data half: heights, cells and cliff bits, and the map chunks that hold them.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* Moved out of GameEngineDevice's WorldHeightMap.cpp (T1): see WorldHeightMapData.h for why and for
	 what is new.  Every function below that WorldHeightMap had is its text there, with only the class
	 name in front changed; the destructor's frees are the data members' frees from ~WorldHeightMap, in
	 the order they were there. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <string.h>

#include "Common/DataChunk.h"
#include "Common/Dict.h"
#include "Common/Errors.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/ThingFactory.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/WorldHeightMapData.h"

#define PATHFIND_CLIFF_SLOPE_LIMIT_F	9.8f	

/** Optimized version of method to get triangle flip state of a terrain cell.  Use this
*	instead of getAlphaUVData() whenever possible.
*/
Bool WorldHeightMapData::getFlipState(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_cellFlipState) return false;
	return m_cellFlipState[yIndex*m_flipStateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}

/** Sets the value of the flip state bit.
*/
void WorldHeightMapData::setFlipState(Int xIndex, Int yIndex, Bool value) 
{
	if (xIndex<0 || yIndex<0) return ;
	if (yIndex>=m_height) return ;
	if (xIndex>=m_width) return ;
	if (!m_cellFlipState) return ;
	UnsignedByte *curVal = &m_cellFlipState[yIndex*m_flipStateWidth + (xIndex >> 3)];
	if (value) {
		*curVal |= (1<<(xIndex&0x7));
	}	else {
		*curVal &= ~(1<<(xIndex&0x7));
	}
}

/** Clears all flip state bits.
*/
void WorldHeightMapData::clearFlipStates(void) {
	if (m_cellFlipState) {
		memset(m_cellFlipState,0,m_flipStateWidth*m_height);	//clear all flags
	}
}

//////////////////////////////////////////////////////////////////////////////m_SeismicUpdateFlag
Bool WorldHeightMapData::getSeismicUpdateFlag(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_seismicUpdateFlag) return false;
	return m_seismicUpdateFlag[yIndex*m_seismicUpdateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}

void WorldHeightMapData::setSeismicUpdateFlag(Int xIndex, Int yIndex, Bool value) 
{
	if (xIndex<0 || yIndex<0) return ;
	if (yIndex>=m_height) return ;
	if (xIndex>=m_width) return ;
	if (!m_seismicUpdateFlag) return ;
	UnsignedByte *curVal = &m_seismicUpdateFlag[yIndex*m_seismicUpdateWidth + (xIndex >> 3)];
	if (value) {
		*curVal |= (1<<(xIndex&0x7));
	}	else {
		*curVal &= ~(1<<(xIndex&0x7));
	}
}

void WorldHeightMapData::clearSeismicUpdateFlags(void) 
{
	if (m_seismicUpdateFlag) {
		memset(m_seismicUpdateFlag,0,m_seismicUpdateWidth*m_height);	//clear all flags
	}
}

void WorldHeightMapData::fillSeismicZVelocities( Real value ) 
{
	if (!m_seismicZVelocities) return ;
  for (Int idx = 0; idx < m_width*m_height; ++idx)
    m_seismicZVelocities[idx] = value;
}

/** Get whether the cell is a cliff cell (impassable to ground vehicles).
*/
Bool WorldHeightMapData::getCliffState(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_cellCliffState) return false;
	return m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}

//=============================================================================
// setCliffState
//=============================================================================
/** Sets the cliff state for a given cell. */
//=============================================================================
void WorldHeightMapData::setCliffState(Int xIndex, Int yIndex, Bool state) 
{
	if (xIndex<0 || yIndex<0) return;
	if (yIndex>=m_height) return;
	if (xIndex>=m_width) return;
	if (!m_cellCliffState) return;
	UnsignedByte	flagByte = m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)];
	UnsignedByte flagMask = (1<<(xIndex&0x7));
	if (state) {
		flagByte |= flagMask;
	} else {
		flagByte &= (~flagMask);
	}
	m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)] = flagByte;
}

Bool WorldHeightMapData::ParseWorldDictDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	Dict d = file.readDict();
	*MapObject::getWorldDict() = d;
	Bool exists;
	Int theWeather = MapObject::getWorldDict()->getInt(TheKey_weather, &exists);
	if (exists) {
		TheWritableGlobalData->m_weather = (Weather) theWeather;
	}
	return true;
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMapData::ParseHeightMapData(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	m_width = file.readInt();
	m_height = file.readInt();
	if (info->version >= K_HEIGHT_MAP_VERSION_3) {
		m_borderSize = file.readInt();
	} else {
		m_borderSize = 0;
	}

	if (info->version >= K_HEIGHT_MAP_VERSION_4) {
		Int numBorders = file.readInt();
		m_boundaries.resize(numBorders);
		for (int i = 0; i < numBorders; ++i) {
			m_boundaries[i].x = file.readInt();
			m_boundaries[i].y = file.readInt();
		}
	} else {
		m_boundaries.resize(1);
		m_boundaries[0].x = m_width - 2 * m_borderSize;
		m_boundaries[0].y = m_height - 2 * m_borderSize;
	}

	m_dataSize = file.readInt();
	m_data = MSGNEW("WorldHeightMap_ParseHeightMapData") UnsignedByte[m_dataSize];
	if (m_dataSize <= 0 || (m_dataSize != (m_width*m_height))) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}

	Int numBytesX = (m_width+7)/8;	//how many bytes to fit all bitflags
	Int numBytesY = m_height;	
	m_seismicUpdateWidth=numBytesX;
	m_seismicUpdateFlag	= MSGNEW("WorldHeightMap::ParseHeightMapData _ m_seismicUpdateFlag allocated") UnsignedByte[numBytesX*numBytesY];
  clearSeismicUpdateFlags();
  m_seismicZVelocities = MSGNEW("WorldHeightMap_ParseHeightMapData _ zvelocities allocated") Real[m_dataSize];
  fillSeismicZVelocities( 0 );


	file.readArrayOfBytes((char *)m_data, m_dataSize);
	// Resize me. 
	if (info->version == K_HEIGHT_MAP_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i, j;
		for (i=0; i<newHeight; i++) {
			for (j=0; j<newWidth; j++) {
				m_data[i*newWidth+j] = m_data[2*i*m_width+2*j];
			}
		}
	}
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	return true;
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMapData::ParseSizeOnly(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	m_width = file.readInt();
	m_height = file.readInt();
	if (info->version >= K_HEIGHT_MAP_VERSION_3) {
		m_borderSize = file.readInt();
	} else {
		m_borderSize = 0;
	}

	if (info->version >= K_HEIGHT_MAP_VERSION_4) {
		Int numBorders = file.readInt();
		m_boundaries.resize(numBorders);
		for (int i = 0; i < numBorders; ++i) {
			m_boundaries[i].x = file.readInt();
			m_boundaries[i].y = file.readInt();
		}
	} else {
		m_boundaries.resize(1);
		m_boundaries[0].x = m_width - 2 * m_borderSize;
		m_boundaries[0].y = m_height - 2 * m_borderSize;
	}

	m_dataSize = file.readInt();
	m_data = MSGNEW("WorldHeightMap_ParseSizeOnly") UnsignedByte[m_dataSize];
	if (m_dataSize <= 0 || (m_dataSize != (m_width*m_height))) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}
	file.readArrayOfBytes((char *)m_data, m_dataSize);
	// Resize me. 
	if (info->version == K_HEIGHT_MAP_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i, j;
		for (i=0; i<newHeight; i++) {
			for (j=0; j<newWidth; j++) {
				m_data[i*newWidth+j] = m_data[2*i*m_width+2*j];
			}
		}
		m_width = newWidth;
		m_height = newHeight;
	}
	return true;
}

/**
* WorldHeightMap::ParseObjectData - read a object info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMapData::ParseObjectData(DataChunkInput &file, DataChunkInfo *info, void *userData, Bool readDict)
{
	MapObject *pPrevious = (MapObject *)file.m_currentObject;

	Coord3D loc;
	loc.x = file.readReal();
	loc.y = file.readReal();
	loc.z = file.readReal();

	Real minZ = -100*MAP_XY_FACTOR;
	Real maxZ = (255*10)*MAP_HEIGHT_SCALE;

	if (info->version <= K_OBJECTS_VERSION_2) {
		loc.z = 0;
	}

	Real angle = file.readReal();
	Int flags = file.readInt(); 
	AsciiString name = file.readAsciiString();
	Dict d;
	if (readDict)
	{
		d = file.readDict();
	}		 

	if (loc.z<minZ || loc.z>maxZ) {
		DEBUG_LOG(("Removing object at z height %f\n", loc.z));
		return true;
	}

	MapObject *pThisOne;
	
	// create the map object
	pThisOne = newInstance( MapObject )( loc, name, angle, flags, &d, 
														TheThingFactory->findTemplate( name, FALSE ) );

//DEBUG_LOG(("obj %s owner %s\n",name.str(),d.getAsciiString(TheKey_originalOwner).str()));

	if (pThisOne->getProperties()->getType(TheKey_waypointID) == Dict::DICT_INT)
		pThisOne->setIsWaypoint();

	if (pThisOne->getProperties()->getType(TheKey_lightHeightAboveTerrain) == Dict::DICT_REAL)
		pThisOne->setIsLight();

	if (pThisOne->getProperties()->getType(TheKey_scorchType) == Dict::DICT_INT)
		pThisOne->setIsScorch();
	

	if (pPrevious) {
		DEBUG_ASSERTCRASH(MapObject::TheMapObjectListPtr != NULL && pPrevious->getNext() == NULL, ("Bad linkage."));
		pPrevious->setNextMap(pThisOne);
	}	else {
		DEBUG_ASSERTCRASH(MapObject::TheMapObjectListPtr == NULL, ("Bad linkage."));
		MapObject::TheMapObjectListPtr = pThisOne;
	}
	file.m_currentObject = pThisOne;
	return true;
}

/** Sets all the cliff flags in map based on height. */
void WorldHeightMapData::initCliffFlagsFromHeights()
{
	Int xIndex, yIndex;

	for (xIndex=0; xIndex<m_width-1; xIndex++) {
		for (yIndex=0; yIndex<m_height-1; yIndex++) {
			setCellCliffFlagFromHeights(xIndex, yIndex);
		}
	}
}

/** Sets the cliff flag for a cell based on height. */
void WorldHeightMapData::setCellCliffFlagFromHeights(Int xIndex, Int yIndex)
{
	Real height1 = getHeight(xIndex, yIndex)*MAP_HEIGHT_SCALE;
	Real height2 = getHeight(xIndex+1, yIndex)*MAP_HEIGHT_SCALE;
	Real height3 = getHeight(xIndex, yIndex+1)*MAP_HEIGHT_SCALE;
	Real height4 = getHeight(xIndex+1, yIndex+1)*MAP_HEIGHT_SCALE;
	Real minZ = height1;
	if (minZ > height2) minZ = height2;
	if (minZ > height3) minZ = height3;
	if (minZ > height4) minZ = height4;
	Real maxZ = height1;
	if (maxZ < height2) maxZ = height2;
	if (maxZ < height3) maxZ = height3;
	if (maxZ < height4) maxZ = height4;
	const Real cliffRange = PATHFIND_CLIFF_SLOPE_LIMIT_F;	
	Bool isCliff = (maxZ-minZ > cliffRange);
	setCliffState(xIndex, yIndex, isCliff);

}

void WorldHeightMapData::freeListOfMapObjects(void)
{
	if (MapObject::TheMapObjectListPtr) 
	{
		MapObject::TheMapObjectListPtr->deleteInstance();
		MapObject::TheMapObjectListPtr = NULL;
	}
	MapObject::getWorldDict()->clear();
}

/** The first half of WorldHeightMap::ParseBlendTileData, as it was there: the cells (tile indexes,
	flip and cliff bits), up to where the terrain textures start. */
Bool WorldHeightMapData::parseBlendTileCells(DataChunkInput &file, DataChunkInfo *info)
{
	int i, j;
	Int len = file.readInt();
	if (m_dataSize != len) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}
	m_tileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	m_cliffInfoNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize]; 
	m_blendTileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	m_extraBlendTileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	// Note - we have one less cell than the width & height. But for paranoia, allocate
	// extra row. jba.
	// 
	Int numBytesX = (m_width+7)/8;	//how many bytes to fit all bitflags
	Int numBytesY = m_height;	

	m_flipStateWidth=numBytesX;

	m_cellFlipState	= MSGNEW("WorldHeightMap_getTerrainTexture") UnsignedByte[numBytesX*numBytesY];
	m_cellCliffState	= MSGNEW("WorldHeightMap_getTerrainTexture") UnsignedByte[numBytesX*numBytesY];
	memset(m_cellFlipState,0,numBytesX*numBytesY);	//clear all flags
	memset(m_cellCliffState,0,numBytesX*numBytesY);	//clear all flags

	file.readArrayOfBytes((char*)m_tileNdxes, m_dataSize*sizeof(Short));
	file.readArrayOfBytes((char*)m_blendTileNdxes, m_dataSize*sizeof(Short));
	if (info->version >= K_BLEND_TILE_VERSION_6) {
		file.readArrayOfBytes((char*)m_extraBlendTileNdxes, m_dataSize*sizeof(Short));
		//Allow clearing of extra blend tiles via ini and resaving of map.
		//Useful for flushing out initial maps made with buggy 3-way blending.
		if (!TheGlobalData->m_use3WayTerrainBlends)
			memset(m_extraBlendTileNdxes,0,m_dataSize*sizeof(Short));		
	} 
	if (info->version >= K_BLEND_TILE_VERSION_5) {
		file.readArrayOfBytes((char*)m_cliffInfoNdxes, m_dataSize*sizeof(Short));
	} 
	if (info->version >= K_BLEND_TILE_VERSION_7) {
		if (info->version==K_BLEND_TILE_VERSION_7) {
			Int byteWidth = (m_width+1)/8; // previous incorrect length that got used to save the file.  jba. [4/3/2003]
			UnsignedByte *data = new UnsignedByte[m_height*byteWidth];
			file.readArrayOfBytes((char*)data, m_height*byteWidth);
			for (j=0; j<m_height; j++) {
				for (i=0; i<byteWidth; i++) {
					m_cellCliffState[j*m_flipStateWidth + i] = data[j*byteWidth + i];
				}
			}
		} else {
			file.readArrayOfBytes((char*)m_cellCliffState, m_height*m_flipStateWidth);
		}
	} else {
		initCliffFlagsFromHeights();
	}
	return true;
}


//-------------------------------------------------------------------------------------------------
/** The members the WorldHeightMap constructors initialised, as they did; the rest are set by the
	parsers, as before. */
WorldHeightMapData::WorldHeightMapData(void) :
	m_width(0), m_height(0), m_dataSize(0), m_data(NULL),
	m_seismicUpdateFlag(NULL), m_seismicZVelocities(NULL),
	m_cellFlipState(NULL), m_cellCliffState(NULL),
	m_tileNdxes(NULL), m_blendTileNdxes(NULL), m_cliffInfoNdxes(NULL), m_extraBlendTileNdxes(NULL)
{
}

WorldHeightMapData::~WorldHeightMapData(void)
{
	if (m_data) {
		delete(m_data);
		m_data = NULL;
	}
	if (m_tileNdxes) {
		delete(m_tileNdxes);
		m_tileNdxes = NULL;
	}
	if (m_blendTileNdxes) {
		delete(m_blendTileNdxes);
		m_blendTileNdxes = NULL;
	}
	if (m_extraBlendTileNdxes) {
		delete(m_extraBlendTileNdxes);
		m_extraBlendTileNdxes = NULL;
	}
	if (m_cliffInfoNdxes) {
		delete(m_cliffInfoNdxes);
		m_cliffInfoNdxes = NULL;
	}
	if (m_cellFlipState)
	{	delete (m_cellFlipState);
		m_cellFlipState = NULL;
	}
	if (m_seismicUpdateFlag)
	{	delete (m_seismicUpdateFlag);
		m_seismicUpdateFlag = NULL;
	}
	if (m_seismicZVelocities)
	{	delete (m_seismicZVelocities);
		m_seismicZVelocities = NULL;
	}
	if (m_cellCliffState)
	{	delete (m_cellCliffState);
		m_cellCliffState = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
// This class's own chunk callbacks.  userData is the WorldHeightMapData* parse() was given here.
//-------------------------------------------------------------------------------------------------
Bool WorldHeightMapData::parseHeightMapDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMapData *pThis = (WorldHeightMapData *)userData;
	return pThis->ParseHeightMapData(file, info, userData);
}

Bool WorldHeightMapData::parseBlendTileCellsChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMapData *pThis = (WorldHeightMapData *)userData;
	return pThis->parseBlendTileCells(file, info);
}

/** WorldHeightMap::ParseObjectsDataChunk's text, registering this class's object callback. */
Bool WorldHeightMapData::parseObjectsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	file.m_currentObject = NULL;
	file.registerParser( AsciiString("Object"), info->label, parseObjectDataChunk );
	return (file.parse(userData));
}

/** WorldHeightMap::ParseObjectDataChunk's text, casting to this class. */
Bool WorldHeightMapData::parseObjectDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMapData *pThis = (WorldHeightMapData *)file.m_userData;
	return pThis->ParseObjectData(file, info, userData, info->version >= K_OBJECTS_VERSION_2);
}

//-------------------------------------------------------------------------------------------------
/** The heights (HeightMapData, read as W3DTerrainVisual's map reads it, by ParseHeightMapData) and the
	cells of BlendTileData.  Chunks with no parser here are skipped by DataChunkInput. */
Bool WorldHeightMapData::parseHeightsAndCells(ChunkInputStream *pStrm)
{
	DataChunkInput file( pStrm );
	file.registerParser( AsciiString("HeightMapData"), AsciiString::TheEmptyString, parseHeightMapDataChunk );
	file.registerParser( AsciiString("BlendTileData"), AsciiString::TheEmptyString, parseBlendTileCellsChunk );
	return file.parse(this);
}

/** parseHeightsAndCells, and the logical-data-only branch of WorldHeightMap's constructor: the world
	dictionary, the map objects, the polygon triggers and the sides, with the same clearing before and
	validateSides after.  The heights are read by ParseHeightMapData, as the terrain visual's map reads
	them, where the logical-only map used ParseSizeOnly: the two read the same bytes for every map from
	K_HEIGHT_MAP_VERSION_2 on, and they differ only in how version 1 is halved. */
Bool WorldHeightMapData::parseLogicalMap(ChunkInputStream *pStrm)
{
	DataChunkInput file( pStrm );
	file.registerParser( AsciiString("HeightMapData"), AsciiString::TheEmptyString, parseHeightMapDataChunk );
	file.registerParser( AsciiString("BlendTileData"), AsciiString::TheEmptyString, parseBlendTileCellsChunk );
	file.registerParser( AsciiString("WorldInfo"), AsciiString::TheEmptyString, ParseWorldDictDataChunk );
	file.registerParser( AsciiString("ObjectsList"), AsciiString::TheEmptyString, parseObjectsDataChunk );
	freeListOfMapObjects(); // just in case.
	file.registerParser( AsciiString("PolygonTriggers"), AsciiString::TheEmptyString, PolygonTrigger::ParsePolygonTriggersDataChunk );
	PolygonTrigger::deleteTriggers(); // just in case.
	TheSidesList->emptySides();
	file.registerParser(AsciiString("SidesList"), AsciiString::TheEmptyString,	SidesList::ParseSidesDataChunk );
	if (!file.parse(this))
		return FALSE;
	TheSidesList->validateSides();
	return TRUE;
}
