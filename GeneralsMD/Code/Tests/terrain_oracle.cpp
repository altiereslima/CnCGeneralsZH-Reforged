/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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
/*
 * terrain_oracle - samples a map's simulation terrain with the ORIGINAL W3D code, for T1's golden.
 *
 * T1 moves the height map's data and the height maths out of GameEngineDevice.  This program is the
 * "before": the functions the simulation's terrain queries run - WorldHeightMap's height and cliff
 * parsing, and BaseHeightMapRenderObjClass's getHeightMapHeight, getClipHeight, isCliffCell,
 * isClearLineOfSight and getMaxCellHeight - taken by Tools/terrain_oracle_extract.py from a pinned
 * commit, unchanged, and compiled here inside stand-ins for the two classes that hold only what those
 * functions touch.  The moved code, run by the native test, must print exactly what this prints.
 *
 * Built two ways:
 *   - mingw-w64, run under Wine: the Windows oracle.  Wine runs the x86-64 code GCC made from the
 *     original text on the real CPU.  So it is "the original C++ under GCC's x86-64 code generation",
 *     not MSVC's: the arithmetic is IEEE single precision either way and nothing here asks for
 *     contraction (-ffp-contract=off, no FMA), but MSVC's own code generation is not what ran.  The
 *     Wine caveats of C1's fs_oracle apply: no Microsoft CRT is involved, and none is used here beyond
 *     file reads.
 *   - natively (arm64, and x86_64 under Rosetta): the same original text on the Mac's compilers, which
 *     says whether the original code is itself platform-independent before anything moves.
 *
 * Input is one map file, already decompressed (a CkMp file).  The chunk framing is read by a small
 * reader of its own below - the format's table of contents, then id, version, size, data - which is
 * test infrastructure, not code under test; the native test reads the same file through the engine's
 * real DataChunkInput, so the two framings are checked against each other too.
 *
 * Output: the map's dimensions, then one FNV-1a hash per kind of sample and the number of samples:
 *   height   getHeightMapHeight(x, y, &normal): the height and the three normal components, as bits
 *   bare     getHeightMapHeight(x, y, NULL): the height alone (the no-normal path)
 *   cliff    isCliffCell(x, y)
 *   maxcell  getMaxCellHeight(x, y)
 *   sight    isClearLineOfSight(a, b) for a fixed spread of pairs
 * over a grid 7.25 world units apart from 60 units outside the map to 60 past its far edge, so the
 * border and off-map clamps are sampled too.  With --dump every sample is printed, for finding the
 * first difference.
 *
 *   terrain_oracle <map.CkMp> [--dump]
 */

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"	// MapReaderWriterInfo.h names it
#include "Common/Errors.h"
#include "Common/MapReaderWriterInfo.h"
#include "vector3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---- The few engine names the extracted text uses that are not the code under test --------------
#define MSGNEW(MSG) new
#define DEBUG_ASSERTCRASH(c, m) ((void)0)
#define DEBUG_LOG(m) ((void)0)
#define MAP_XY_FACTOR			(10.0f)	 // Common/MapObject.h's, as it is there
#define MAP_HEIGHT_SCALE	(MAP_XY_FACTOR/16.0f)
#define K_MAX_HEIGHT  255					 // WorldHeightMap.h's
#ifndef __max
#define __max(a,b) (((a) > (b)) ? (a) : (b))	// MSVC's; mingw's stdlib.h has it, clang's does not
#endif

struct OracleGlobalData { Bool m_use3WayTerrainBlends; };
static OracleGlobalData s_globalData = { TRUE };	// affects only the extra blend tiles, which nothing samples
static OracleGlobalData *TheGlobalData = &s_globalData;

typedef std::vector<ICoord2D> VecICoord2D;

struct DataChunkInfo { unsigned short version; };

/* The chunk reader the extracted parsers call: readInt, readArrayOfBytes and atEndOfChunk over one
	 chunk's bytes, as DataChunkInput provides them. */
class DataChunkInput
{
public:
	DataChunkInput( const unsigned char *data, Int size ) : m_data( data ), m_left( size ) {}
	Int readInt( void )
	{
		Int value = 0;
		readArrayOfBytes( (char *)&value, sizeof( value ) );
		return value;
	}
	void readArrayOfBytes( char *out, Int length )
	{
		if (length > m_left)
		{
			printf( "terrain_oracle: FAIL - a parser read past the end of its chunk\n" );
			exit( 1 );
		}
		memcpy( out, m_data, length );
		m_data += length;
		m_left -= length;
	}
	Bool atEndOfChunk( void ) { return m_left <= 0; }
private:
	const unsigned char *m_data;
	Int m_left;
};

// ---- WorldHeightMap, holding what the extracted functions touch --------------------------------
class WorldHeightMap
{
public:
	WorldHeightMap() : m_width( 0 ), m_height( 0 ), m_borderSize( 0 ), m_dataSize( 0 ), m_data( NULL ),
		m_seismicUpdateFlag( NULL ), m_seismicUpdateWidth( 0 ), m_seismicZVelocities( NULL ),
		m_cellFlipState( NULL ), m_flipStateWidth( 0 ), m_cellCliffState( NULL ), m_tileNdxes( NULL ),
		m_blendTileNdxes( NULL ), m_cliffInfoNdxes( NULL ), m_extraBlendTileNdxes( NULL ) {}

	Bool ParseHeightMapData( DataChunkInput &file, DataChunkInfo *info, void *userData );
	Bool ParseBlendTileData( DataChunkInput &file, DataChunkInfo *info, void *userData );
	Bool getCliffState( Int xIndex, Int yIndex ) const;
	void setCliffState( Int xIndex, Int yIndex, Bool state );
	void initCliffFlagsFromHeights( void );
	void setCellCliffFlagFromHeights( Int xIndex, Int yIndex );

	// The seismic arrays ParseHeightMapData allocates and clears; nothing here reads them.
	void clearSeismicUpdateFlags( void ) {}
	void fillSeismicZVelocities( Real ) {}

	UnsignedByte *getDataPtr( void ) { return m_data; }
	Int getXExtent( void ) { return m_width; }
	Int getYExtent( void ) { return m_height; }
	Int getBorderSizeInline( void ) const { return m_borderSize; }
	static Int getMaxHeightValue( void ) { return K_MAX_HEIGHT; }

#define TERRAIN_ORACLE_WORLDHEIGHTMAP_MEMBERS
#include "terrain_oracle_original.inc"
#undef TERRAIN_ORACLE_WORLDHEIGHTMAP_MEMBERS

	Int m_width, m_height, m_borderSize;
	VecICoord2D m_boundaries;
	Int m_dataSize;
	UnsignedByte *m_data;
	UnsignedByte *m_seismicUpdateFlag;
	UnsignedInt m_seismicUpdateWidth;
	Real *m_seismicZVelocities;
	UnsignedByte *m_cellFlipState;
	Int m_flipStateWidth;
	UnsignedByte *m_cellCliffState;
	Short *m_tileNdxes, *m_blendTileNdxes, *m_cliffInfoNdxes, *m_extraBlendTileNdxes;
};

// TheTerrainVisual->getLogicHeightMap() is the map the height maths samples.
struct OracleTerrainVisual
{
	WorldHeightMap *m_logicHeightMap;
	WorldHeightMap *getLogicHeightMap( void ) { return m_logicHeightMap; }
};
static OracleTerrainVisual s_terrainVisual = { NULL };
static OracleTerrainVisual *TheTerrainVisual = &s_terrainVisual;

// ---- BaseHeightMapRenderObjClass, holding m_map ------------------------------------------------
class BaseHeightMapRenderObjClass
{
public:
	BaseHeightMapRenderObjClass() : m_map( NULL ), m_minHeight( 0 ), m_maxHeight( 0 ) {}
	Real getMaxHeight( void ) const { return m_maxHeight; }	// BaseHeightMap.h's, as it is there

	// initHeightData's min/max pass, which loading the map runs (updateExtraPassTiles is TRUE there).
	void findMinMaxHeights( WorldHeightMap *pMap )
	{
#define TERRAIN_ORACLE_BASEHEIGHTMAP_MINMAX
#include "terrain_oracle_original.inc"
#undef TERRAIN_ORACLE_BASEHEIGHTMAP_MINMAX
	}
	Real getHeightMapHeight( Real x, Real y, Coord3D *normal ) const;
	Bool isClearLineOfSight( const Coord3D &pos, const Coord3D &posOther ) const;
	Real getMaxCellHeight( Real x, Real y ) const;
	Bool isCliffCell( Real x, Real y );

#define TERRAIN_ORACLE_BASEHEIGHTMAP_MEMBERS
#include "terrain_oracle_original.inc"
#undef TERRAIN_ORACLE_BASEHEIGHTMAP_MEMBERS

	WorldHeightMap *m_map;
	Real m_minHeight, m_maxHeight;
};

#define TERRAIN_ORACLE_DEFINITIONS
#include "terrain_oracle_original.inc"
#undef TERRAIN_ORACLE_DEFINITIONS

// ---- Driving it -----------------------------------------------------------------------------------
#include "terrain_grid.h"

namespace {

struct OriginalSampler
{
	BaseHeightMapRenderObjClass *terrain;
	Real height( Real x, Real y, Coord3D *normal ) { return terrain->getHeightMapHeight( x, y, normal ); }
	Bool cliff( Real x, Real y ) { return terrain->isCliffCell( x, y ); }
	Real maxcell( Real x, Real y ) { return terrain->getMaxCellHeight( x, y ); }
	Bool sight( const Coord3D &a, const Coord3D &b ) { return terrain->isClearLineOfSight( a, b ); }
};

}  // namespace

int main( int argc, char **argv )
{
	if (argc < 2)
	{
		printf( "usage: terrain_oracle <map.CkMp> [--dump]\n" );
		return 2;
	}
	const Bool dump = argc > 2 && strcmp( argv[2], "--dump" ) == 0;

	FILE *file = fopen( argv[1], "rb" );
	if (file == NULL)
	{
		printf( "terrain_oracle: FAIL - cannot open %s\n", argv[1] );
		return 1;
	}
	std::vector<unsigned char> bytes;
	unsigned char chunk[ 65536 ];
	size_t got;
	while ((got = fread( chunk, 1, sizeof( chunk ), file )) > 0)
		bytes.insert( bytes.end(), chunk, chunk + got );
	fclose( file );

	// The table of contents: "CkMp", a count, then (length byte, name, id) for each label.
	if (bytes.size() < 8 || memcmp( &bytes[0], "CkMp", 4 ) != 0)
	{
		printf( "terrain_oracle: FAIL - %s is not a decompressed map (no CkMp)\n", argv[1] );
		return 1;
	}
	size_t at = 4;
	Int labels;
	memcpy( &labels, &bytes[at], 4 );
	at += 4;
	UnsignedInt heightId = 0, blendId = 0;
	for (Int i = 0; i < labels; ++i)
	{
		const unsigned length = bytes[at++];
		const std::string name( (const char *)&bytes[at], length );
		at += length;
		UnsignedInt id;
		memcpy( &id, &bytes[at], 4 );
		at += 4;
		if (name == "HeightMapData") heightId = id;
		if (name == "BlendTileData") blendId = id;
	}

	// Top-level chunks: id, version (16 bits), size, data.  HeightMapData comes before BlendTileData.
	WorldHeightMap map;
	Bool haveHeights = FALSE, haveBlend = FALSE;
	while (at + 10 <= bytes.size())
	{
		UnsignedInt id;
		unsigned short version;
		Int size;
		memcpy( &id, &bytes[at], 4 );
		memcpy( &version, &bytes[at + 4], 2 );
		memcpy( &size, &bytes[at + 6], 4 );
		at += 10;
		DataChunkInfo info = { version };
		DataChunkInput input( &bytes[at], size );
		if (id == heightId)
			haveHeights = map.ParseHeightMapData( input, &info, &map );
		else if (id == blendId && haveHeights)
			haveBlend = map.ParseBlendTileData( input, &info, &map );
		at += size;
	}
	if (!haveHeights || !haveBlend)
	{
		printf( "terrain_oracle: FAIL - the map has no %s chunk\n", haveHeights ? "BlendTileData" : "HeightMapData" );
		return 1;
	}

	BaseHeightMapRenderObjClass terrain;
	terrain.m_map = &map;
	terrain.findMinMaxHeights( &map );
	s_terrainVisual.m_logicHeightMap = &map;

	OriginalSampler sampler = { &terrain };
	printf( "%s", terrain_grid::run( sampler, (int)map.m_width, (int)map.m_height, (int)map.m_borderSize,
		(int)map.m_boundaries.size(), terrain.m_maxHeight, MAP_XY_FACTOR, dump != FALSE ).c_str() );
	return 0;
}
