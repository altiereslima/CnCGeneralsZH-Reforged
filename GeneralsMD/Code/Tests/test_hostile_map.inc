// A map file, and the map.ini beside it, come from whoever made the map.  These hand the readers
// numbers no editor writes and check each one is refused, rather than allocated, looped over or
// read past.  A refused map is ERROR_CORRUPT_FILE_FORMAT out of the chunk reader, which
// MapCache::addMap catches and leaves the map out of the list, or INI_INVALID_DATA out of a
// map.ini field.

#include "GameLogic/WorldHeightMapData.h"
#include "GameLogic/Module/CaveContain.h"

// A CkMp stream written by hand: a table of contents naming each label (label i gets id i + 1),
// then chunk headers and data in whatever order the test puts them.
struct HostileChunkBytes
{
	std::vector<char> bytes;

	void put( const void *p, size_t n ) { bytes.insert( bytes.end(), (const char *)p, (const char *)p + n ); }
	void putInt( Int v ) { put( &v, sizeof( v ) ); }
	void contents( const char *const *labels, Int count )
	{
		put( "CkMp", 4 );
		putInt( count );
		for( Int i = 0; i < count; ++i )
		{
			unsigned char len = (unsigned char)strlen( labels[ i ] );
			put( &len, 1 );
			put( labels[ i ], len );
			UnsignedInt id = i + 1;
			put( &id, sizeof( id ) );
		}
	}
	void chunk( UnsignedInt id, DataChunkVersionType version, Int dataSize )
	{
		put( &id, sizeof( id ) );
		put( &version, sizeof( version ) );
		putInt( dataSize );
	}
};

struct HostileProbe
{
	Int first;
	Int second;
	Bool threw;
};

static Bool hostileReadTwoInts( DataChunkInput &file, DataChunkInfo *, void *userData )
{
	HostileProbe *probe = (HostileProbe *)userData;
	probe->first = file.readInt();
	try
	{
		probe->second = file.readInt();
	}
	catch( ErrorCode ec )
	{
		probe->threw = ( ec == ERROR_CORRUPT_FILE_FORMAT );
	}
	return TRUE;
}

static Bool hostileReadCount( DataChunkInput &file, DataChunkInfo *, void *userData )
{
	HostileProbe *probe = (HostileProbe *)userData;
	try
	{
		probe->first = file.readCount( sizeof( Int ) );
	}
	catch( ErrorCode ec )
	{
		probe->threw = ( ec == ERROR_CORRUPT_FILE_FORMAT );
	}
	return TRUE;
}

static Bool hostileParseInner( DataChunkInput &file, DataChunkInfo *info, void *userData )
{
	file.registerParser( AsciiString( "Inner" ), info->label, hostileReadTwoInts );
	return file.parse( userData );
}

/* EA only asserted on a read past the end of a chunk, so a release build read the next chunk's bytes,
	 or past the end of the file, where nothing is copied and the field keeps whatever the stack held. */
TEST(data_chunk_read_past_the_end_of_its_chunk_throws_in_every_build)
{
	CHECK( bootOnce() );
	const char *labels[] = { "Probe" };

	// the chunk holds one Int and the parser asks for two
	{
		HostileChunkBytes f;
		f.contents( labels, 1 );
		f.chunk( 1, 1, sizeof( Int ) );
		f.putInt( 7 );
		f.putInt( 8 );	// the bytes are there, but past the chunk

		HostileProbe probe = { 0, 0, FALSE };
		MemoryChunkInputStream stream( &f.bytes[0], (Int)f.bytes.size() );
		DataChunkInput input( &stream );
		input.registerParser( AsciiString( "Probe" ), AsciiString::TheEmptyString, hostileReadTwoInts );
		CHECK( input.parse( &probe ) );
		CHECK_EQ( probe.first, 7 );
		CHECK( probe.threw );
		CHECK_EQ( probe.second, 0 );
	}

	// the chunk claims two Ints and the file ends after one
	{
		HostileChunkBytes f;
		f.contents( labels, 1 );
		f.chunk( 1, 1, 2 * sizeof( Int ) );
		f.putInt( 7 );

		HostileProbe probe = { 0, 0, FALSE };
		MemoryChunkInputStream stream( &f.bytes[0], (Int)f.bytes.size() );
		DataChunkInput input( &stream );
		input.registerParser( AsciiString( "Probe" ), AsciiString::TheEmptyString, hostileReadTwoInts );
		input.parse( &probe );
		CHECK_EQ( probe.first, 7 );
		CHECK( probe.threw );
	}

	// and a chunk that does hold both reads both, as every real map does
	{
		HostileChunkBytes f;
		f.contents( labels, 1 );
		f.chunk( 1, 1, 2 * sizeof( Int ) );
		f.putInt( 7 );
		f.putInt( 8 );

		HostileProbe probe = { 0, 0, FALSE };
		MemoryChunkInputStream stream( &f.bytes[0], (Int)f.bytes.size() );
		DataChunkInput input( &stream );
		input.registerParser( AsciiString( "Probe" ), AsciiString::TheEmptyString, hostileReadTwoInts );
		CHECK( input.parse( &probe ) );
		CHECK_EQ( probe.second, 8 );
		CHECK( !probe.threw );
	}
}

/* A chunk is nested whole inside its parent; one that claims more than the parent has left is
	 refused when it is opened, before any of its reads land in the parent's next chunk. */
TEST(data_chunk_larger_than_its_parent_is_corrupt)
{
	CHECK( bootOnce() );
	const char *labels[] = { "Outer", "Inner" };
	const Int header = sizeof( UnsignedInt ) + sizeof( DataChunkVersionType ) + sizeof( Int );

	HostileChunkBytes f;
	f.contents( labels, 2 );
	f.chunk( 1, 1, header + 2 * sizeof( Int ) );
	f.chunk( 2, 1, 1000 );
	f.putInt( 7 );
	f.putInt( 8 );

	HostileProbe probe = { 0, 0, FALSE };
	MemoryChunkInputStream stream( &f.bytes[0], (Int)f.bytes.size() );
	DataChunkInput input( &stream );
	input.registerParser( AsciiString( "Outer" ), AsciiString::TheEmptyString, hostileParseInner );
	Bool corrupt = FALSE;
	try
	{
		input.parse( &probe );
	}
	catch( ErrorCode ec )
	{
		corrupt = ( ec == ERROR_CORRUPT_FILE_FORMAT );
	}
	CHECK( corrupt );
	CHECK_EQ( probe.first, 0 );
}

/* A count that sizes an allocation is checked against the bytes its items would need. */
TEST(data_chunk_count_the_chunk_cannot_hold_is_refused)
{
	CHECK( bootOnce() );
	const char *labels[] = { "Probe" };

	HostileChunkBytes f;
	f.contents( labels, 1 );
	f.chunk( 1, 1, 3 * sizeof( Int ) );
	f.putInt( 1000000 );
	f.putInt( 0 );
	f.putInt( 0 );

	HostileProbe probe = { -1, 0, FALSE };
	MemoryChunkInputStream stream( &f.bytes[0], (Int)f.bytes.size() );
	DataChunkInput input( &stream );
	input.registerParser( AsciiString( "Probe" ), AsciiString::TheEmptyString, hostileReadCount );
	input.parse( &probe );
	CHECK( probe.threw );

	// two Ints left after the count: a count of two fits, three does not
	HostileChunkBytes g;
	g.contents( labels, 1 );
	g.chunk( 1, 1, 3 * sizeof( Int ) );
	g.putInt( 2 );
	g.putInt( 0 );
	g.putInt( 0 );

	HostileProbe fits = { -1, 0, FALSE };
	MemoryChunkInputStream fitsStream( &g.bytes[0], (Int)g.bytes.size() );
	DataChunkInput fitsInput( &fitsStream );
	fitsInput.registerParser( AsciiString( "Probe" ), AsciiString::TheEmptyString, hostileReadCount );
	fitsInput.parse( &fits );
	CHECK( !fits.threw );
	CHECK_EQ( fits.first, 2 );
}

/* The table of contents' own count used to loop and allocate up to two billion names off the
	 end of the file. */
TEST(data_chunk_table_of_contents_longer_than_the_file_stops)
{
	CHECK( bootOnce() );

	HostileChunkBytes f;
	f.put( "CkMp", 4 );
	f.putInt( 0x7fffffff );

	MemoryChunkInputStream stream( &f.bytes[0], (Int)f.bytes.size() );
	DataChunkInput input( &stream );
	CHECK( !input.isValidFileType() );
}

// HeightMapData, version 4: width, height, border, the boundaries, then the heights
static void hostileHeightMap( HostileChunkBytes &f, Int width, Int height, Int numBorders, Int dataSize, Int heightBytes )
{
	const char *labels[] = { "HeightMapData" };
	f.contents( labels, 1 );
	const Int borders = numBorders > 0 && numBorders < 16 ? numBorders : 0;
	f.chunk( 1, K_HEIGHT_MAP_VERSION_4, 5 * sizeof( Int ) + borders * 2 * sizeof( Int ) + heightBytes );
	f.putInt( width );
	f.putInt( height );
	f.putInt( 0 );
	f.putInt( numBorders );
	for( Int i = 0; i < borders; ++i )
	{
		f.putInt( width );
		f.putInt( height );
	}
	f.putInt( dataSize );
	for( Int i = 0; i < heightBytes; ++i )
		f.bytes.push_back( (char)i );
}

static Bool hostileHeightMapRefused( HostileChunkBytes &f )
{
	WorldHeightMapData map;
	MemoryChunkInputStream stream( &f.bytes[0], (Int)f.bytes.size() );
	try
	{
		map.parseHeightsAndCells( &stream );
	}
	catch( ErrorCode ec )
	{
		return ec == ERROR_CORRUPT_FILE_FORMAT;
	}
	return FALSE;
}

/* The heights were allocated from the file's dataSize before it was compared with width times
	 height, and the boundary list resized from a count nobody checked. */
TEST(height_map_sizes_a_map_file_cannot_hold_are_refused)
{
	CHECK( bootOnce() );

	// a real one, two by two, reads
	{
		HostileChunkBytes f;
		hostileHeightMap( f, 2, 2, 1, 4, 4 );
		WorldHeightMapData map;
		MemoryChunkInputStream stream( &f.bytes[0], (Int)f.bytes.size() );
		CHECK( map.parseHeightsAndCells( &stream ) );
		CHECK_EQ( map.getXExtent(), 2 );
		CHECK_EQ( map.getYExtent(), 2 );
		CHECK_EQ( (Int)map.getAllBoundaries().size(), 1 );
	}
	{
		HostileChunkBytes f;
		hostileHeightMap( f, K_MAX_HEIGHT_MAP_SIDE + 1, 2, 1, 2 * ( K_MAX_HEIGHT_MAP_SIDE + 1 ), 0 );
		CHECK( hostileHeightMapRefused( f ) );
	}
	{
		// 65537 squared wraps to 131073 in an Int, so this dataSize passed the old check and the
		// heights were then indexed as 65537 a side
		HostileChunkBytes f;
		hostileHeightMap( f, 65537, 65537, 1, 131073, 0 );
		CHECK( hostileHeightMapRefused( f ) );
	}
	{
		HostileChunkBytes f;
		hostileHeightMap( f, 2, 2, 0x10000000, 4, 4 );
		CHECK( hostileHeightMapRefused( f ) );
	}
	{
		HostileChunkBytes f;
		hostileHeightMap( f, 2, 2, -1, 4, 4 );
		CHECK( hostileHeightMapRefused( f ) );
	}
	{
		// a dataSize that is not width times height, refused before the heights are allocated
		HostileChunkBytes f;
		hostileHeightMap( f, 2, 2, 1, 0x7fffffff, 4 );
		CHECK( hostileHeightMapRefused( f ) );
	}
}

/* A map.ini can redefine a cave, and the index is a slot registerNewCave pads a vector up to. */
TEST(cave_index_outside_what_the_cave_system_holds_is_refused)
{
	CHECK( bootOnce() );

	GlobalData *savedGlobals = TheWritableGlobalData;
	if( savedGlobals == NULL )
		TheWritableGlobalData = NEW GlobalData;
	if( TheModuleFactory == NULL )
	{
		TheModuleFactory = NEW ModuleFactory;
		TheModuleFactory->init();
	}
	if( TheThingFactory == NULL )
		TheThingFactory = NEW ThingFactory;

	writeFile( TEST_INI,
		"Object CaveIndexProbe\r\n"
		"  Behavior = CaveContain ModuleTag_01\r\n"
		"    CaveIndex = 3\r\n"
		"  End\r\n"
		"End\r\n" );
	CHECK( loadIni( TEST_INI ) );
	const ThingTemplate *probe = TheThingFactory->findTemplate( "CaveIndexProbe" );
	CHECK( probe != NULL );
	if( probe != NULL )
	{
		const ModuleInfo &modules = probe->getBehaviorModuleInfo();
		CHECK_EQ( modules.getCount(), 1 );
		if( modules.getCount() == 1 )
			CHECK_EQ( ((const CaveContainModuleData *)modules.getNthData( 0 ))->m_caveIndexData, 3 );
	}

	writeFile( TEST_INI,
		"Object CaveIndexProbeHuge\r\n"
		"  Behavior = CaveContain ModuleTag_01\r\n"
		"    CaveIndex = 1024\r\n"
		"  End\r\n"
		"End\r\n" );
	CHECK( loadIni( TEST_INI ) == FALSE );

	writeFile( TEST_INI,
		"Object CaveIndexProbeNegative\r\n"
		"  Behavior = CaveContain ModuleTag_01\r\n"
		"    CaveIndex = -1\r\n"
		"  End\r\n"
		"End\r\n" );
	CHECK( loadIni( TEST_INI ) == FALSE );

	remove( TEST_INI );
	if( savedGlobals == NULL )
	{
		delete TheWritableGlobalData;
		TheWritableGlobalData = NULL;
	}
}

/* PartitionCellSize sizes the partition grid; a map.ini's GameData block asking for a tiny cell
	 made it millions of cells.  The refusal comes before the field is stored. */
TEST(partition_cell_size_outside_its_range_is_refused)
{
	CHECK( bootOnce() );

	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;
	const Real before = TheWritableGlobalData->m_partitionCellSize;

	writeFile( TEST_INI,
		"GameData\r\n"
		"  PartitionCellSize = 0.01\r\n"
		"End\r\n" );
	CHECK( loadIni( TEST_INI ) == FALSE );
	CHECK( TheWritableGlobalData->m_partitionCellSize == before );

	writeFile( TEST_INI,
		"GameData\r\n"
		"  PartitionCellSize = 100000\r\n"
		"End\r\n" );
	CHECK( loadIni( TEST_INI ) == FALSE );
	CHECK( TheWritableGlobalData->m_partitionCellSize == before );

	remove( TEST_INI );
	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}
