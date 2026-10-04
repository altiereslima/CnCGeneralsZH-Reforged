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

// The renderer's hooks gameengine calls, as the game links them off Windows: W3DDevice's own, now that
// w3ddevice builds there (A1), where until C2's wiring PosixDevice had stand-ins.  Above all
// W3DShaderManager's testMinimumRequirements, which is where decision 2 (B19, option (c)) lives: a CPU
// cpudetect cannot time is treated as fast when GameLOD chooses the default detail preset, and (the
// decision extended to the GPU) so is a device the chipset table cannot place.
//
// What it checks:
//   1. testMinimumRequirements on this machine: the top chipset (no device has been made, so getChipset
//      cannot place one), an unknown CPU type (XX, so a first launch still runs the benchmark), a speed
//      that is cpudetect's own when cpudetect measured one and the named fast figure when it did not,
//      memory, and the benchmark library's indices.
//   2. With ZH_DATA_DIR, against the game's own Data\INI\GameLODPresets.ini out of INIZH.big: that the
//      speed reported for an unmeasured CPU meets every BenchProfile and every LODPreset the file names
//      (GameLOD matches at 94% of a preset's MHz) and is above its ReallyLowMHz and GameLOD's default
//      400 - so a later launch, which takes this speed, no longer turns the shell map off where the
//      first launch, which took a benchmark profile's, left it on.  And that the chipset reported for
//      an unplaced device meets every LODPreset's: before it, every Mac's first launch chose LOW.  Without ZH_DATA_DIR that half says
//      "skip" and the test still runs the first half.
//   3. The small hooks: doSkyBoxSet and oversizeTheTerrain run with nothing to act on (no GlobalData,
//      no terrain render object), and the two DX8Wrapper globals start where dx8wrapper.cpp starts them.
//
// WHAT THIS DOES NOT PROVE: that GameLODManager, run for real, picks the top preset.  That needs the
// engine; Tools/lod-first-run-check.sh (ctest lod_first_run_check) runs the game's first launch and
// reads Options.ini.

#include "PreRTS.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "cpudetect.h"
#include "Platform/RenderTypes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>

extern Bool testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, Int *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex);
extern ChipsetType chipsetForPresets(ChipsetType detected, Bool overridden);
extern void doSkyBoxSet(Bool startDraw);
extern void oversizeTheTerrain(Int amount);
extern int DX8Wrapper_PreserveFPU;
extern bool DX8Wrapper_IsWindowed;

// The exe's names: Main/PosixMain.cpp's, which the test cannot link (it holds main()).  The window is
// null, as under -headless.
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
static char s_noAppPrefix[] = "";
char *gAppPrefix = s_noAppPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

static int failures = 0;
#define CHECK( COND, ... ) \
	do { if (!(COND)) { printf( "FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); ++failures; } } while (0)

static const float PROFILE_ERROR_LIMIT = 0.94f;	// GameLOD.cpp's
static const int DEFAULT_REALLY_LOW_MHZ = 400;	// GameLODManager's constructor

static unsigned be32( const unsigned char *p ) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

static std::string readBigEntry( const std::string &path, const char *entryName )
{
	std::string data;
	FILE *file = fopen( path.c_str(), "rb" );
	if (file == NULL)
		return data;
	unsigned char header[ 16 ];
	if (fread( header, 1, 16, file ) == 16 && memcmp( header, "BIGF", 4 ) == 0)
	{
		const unsigned count = be32( header + 8 );
		for (unsigned i = 0; i < count; ++i)
		{
			unsigned char pair[ 8 ];
			if (fread( pair, 1, 8, file ) != 8)
				break;
			std::string name;
			int c;
			while ((c = fgetc( file )) > 0)
				name += (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
			if (name == entryName)
			{
				data.resize( be32( pair + 4 ) );
				if (fseek( file, (long)be32( pair ), SEEK_SET ) != 0 || fread( &data[0], 1, data.size(), file ) != data.size())
					data.clear();
				break;
			}
		}
	}
	fclose( file );
	return data;
}

/** A preset's chipset name as GameLOD.cpp's VideoNames numbers it (ChipsetType), or -1. */
static int videoIndex( const char *name )
{
	static const char *const names[] = { "XX","V2","V3","V4","V5","TNT","TNT2","GF2","R100","PS11","GF3","GF4","PS14","R200","PS20","R300" };
	static_assert( sizeof( names ) / sizeof( names[0] ) == DC_MAX, "one name per ChipsetType, as VideoNames has" );
	for (int i = 0; i < (int)( sizeof( names ) / sizeof( names[0] ) ); ++i)
		if (strcasecmp( name, names[i] ) == 0)
			return i;
	return -1;
}

int main( void )
{
	CPUDetectClass::Get_Processor_Speed();	// cpudetect initialises itself on first use

	// 1. The function itself.
	ChipsetType chip = DC_TNT;
	CpuType cpu = P4;
	Int mhz = -1, ram = -1;
	Real intIndex = 0, floatIndex = 0, memIndex = 0;
	CHECK( testMinimumRequirements( &chip, &cpu, &mhz, &ram, &intIndex, &floatIndex, &memIndex ), "returned FALSE" );
	const Int measured = CPUDetectClass::Get_Processor_Speed();
	printf( "cpudetect: %s, %d MHz measured; reported: cpu %d, %d MHz, %d bytes, chip %d, bench %g/%g/%g\n",
		CPUDetectClass::Get_Processor_Manufacturer_Name(), (int)measured, (int)cpu, (int)mhz, (int)ram, (int)chip,
		(double)intIndex, (double)floatIndex, (double)memIndex );
	CHECK( chip == DC_MAX - 1, "chip %d, want %d, the top of the table: no device has been made, so getChipset cannot place one", (int)chip, (int)(DC_MAX - 1) );

	// The mapping behind it, which a device with a GPU reaches (A3e: the POSIX device claims pixel shader
	// 1.1, so getChipset places it only generically).  Unplaced or placed only by caps: the top.  A named
	// chip, or any override: as it is.
	{
		const struct { ChipsetType detected; Bool overridden; ChipsetType want; } TABLE[] = {
			{ DC_UNKNOWN, FALSE, (ChipsetType)(DC_MAX - 1) },
			{ DC_GENERIC_PIXEL_SHADER_1_1, FALSE, (ChipsetType)(DC_MAX - 1) },
			{ DC_GENERIC_PIXEL_SHADER_1_4, FALSE, (ChipsetType)(DC_MAX - 1) },
			{ DC_GENERIC_PIXEL_SHADER_2_0, FALSE, (ChipsetType)(DC_MAX - 1) },
			{ DC_GEFORCE3, FALSE, DC_GEFORCE3 },
			{ DC_TNT2, FALSE, DC_TNT2 },
			{ DC_GENERIC_PIXEL_SHADER_1_1, TRUE, DC_GENERIC_PIXEL_SHADER_1_1 },
			{ DC_VOODOO2, TRUE, DC_VOODOO2 },
		};
		for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); ++i)
		{
			const ChipsetType got = chipsetForPresets( TABLE[i].detected, TABLE[i].overridden );
			CHECK( got == TABLE[i].want, "chipsetForPresets(%d, %s) is %d, want %d", (int)TABLE[i].detected,
				TABLE[i].overridden ? "overridden" : "detected", (int)got, (int)TABLE[i].want );
		}
	}
	CHECK( cpu == XX, "cpu type %d, want XX: CPUID is x86 under MSVC only, and XX is what sends a first launch to the benchmark", (int)cpu );
	if (measured == CPUDETECT_UNMEASURED_PROCESSOR_MHZ)
		CHECK( mhz > 0, "an unmeasured CPU reported %d MHz: decision 2 says treat it as fast", (int)mhz );
	else
		CHECK( mhz == measured, "a measured CPU's %d MHz was reported as %d", (int)measured, (int)mhz );
	CHECK( ram == (Int)CPUDetectClass::Get_Total_Physical_Memory(), "memory %d, cpudetect says %d", (int)ram, (int)CPUDetectClass::Get_Total_Physical_Memory() );
	CHECK( intIndex > 0 && floatIndex > 0 && memIndex > 0, "the benchmark indices were not filled in" );

	// Asking for one output leaves the others alone, as GameLOD's separate calls expect.
	{
		Int onlyMhz = -1;
		CHECK( testMinimumRequirements( NULL, NULL, &onlyMhz, NULL, NULL, NULL, NULL ) && onlyMhz == mhz, "a speed-only call gave %d", (int)onlyMhz );
	}

	// 2. Against the shipped presets.  The speed tested is the one an unmeasured CPU is given, so this
	// half means the same whether or not this machine's CPU could be timed - on every POSIX build it
	// cannot, so the two are the same number here.
	const char *dir = getenv( "ZH_DATA_DIR" );
	if (measured != CPUDETECT_UNMEASURED_PROCESSOR_MHZ)
		printf( "skip: this CPU was timed (%d MHz), so the unmeasured speed is not what was reported\n", (int)measured );
	else if (dir == NULL || *dir == 0)
		printf( "skip: no game data (ZH_DATA_DIR) to read GameLODPresets.ini from\n" );
	else
	{
		const std::string ini = readBigEntry( std::string( dir ) + "/zerohour/INIZH.big", "data\\ini\\gamelodpresets.ini" );
		CHECK( !ini.empty(), "ZH_DATA_DIR is set but INIZH.big holds no Data\\INI\\GameLODPresets.ini" );
		int presets = 0, profiles = 0, reallyLow = DEFAULT_REALLY_LOW_MHZ, fastest = 0, toughestChip = 0;
		size_t at = 0;
		while (at < ini.size())
		{
			size_t end = ini.find( '\n', at );
			if (end == std::string::npos)
				end = ini.size();
			const std::string line = ini.substr( at, end - at );
			at = end + 1;
			char level[ 16 ], type[ 8 ], video[ 8 ];
			int value = 0;
			if (sscanf( line.c_str(), " LODPreset = %15s %7s %d %7s", level, type, &value, video ) == 4)
			{
				const int wanted = videoIndex( video );
				CHECK( wanted > 0 && wanted <= (int)chip, "chipset %d does not meet \"%s\" (%s is %d)", (int)chip, line.c_str(), video, wanted );
				if (wanted > toughestChip)
					toughestChip = wanted;
			}
			if (sscanf( line.c_str(), " LODPreset = %15s %7s %d", level, type, &value ) == 3 ||
					sscanf( line.c_str(), " BenchProfile = %7s %d", type, &value ) == 2)
			{
				line.find( "BenchProfile" ) != std::string::npos ? ++profiles : ++presets;
				if (value > fastest)
					fastest = value;
				CHECK( (Real)mhz / (Real)value >= PROFILE_ERROR_LIMIT, "%d MHz does not meet \"%s\"", (int)mhz, line.c_str() );
			}
			else if (sscanf( line.c_str(), " ReallyLowMHz = %d", &value ) == 1)
				reallyLow = value;
		}
		CHECK( presets > 0 && profiles > 0, "read %d presets and %d profiles: the file was not what this test expects", presets, profiles );
		CHECK( mhz >= reallyLow && mhz >= DEFAULT_REALLY_LOW_MHZ, "%d MHz is below ReallyLowMHz %d: a later launch would turn the shell map off", (int)mhz, reallyLow );
		CHECK( toughestChip > DC_TNT2, "no preset asks for more than a TNT2 (%d): the chipset half proves nothing on this file", toughestChip );
		printf( "GameLODPresets.ini: %d LODPresets and %d BenchProfiles, the fastest %d MHz, ReallyLowMHz %d; %d MHz meets all of them\n",
			presets, profiles, fastest, reallyLow, (int)mhz );
		printf( "GameLODPresets.ini: the toughest chipset asked for is %d; chipset %d meets every preset\n", toughestChip, (int)chip );
	}

	// 3. The small hooks.
	doSkyBoxSet( TRUE );
	oversizeTheTerrain( 4 );
	CHECK( DX8Wrapper_PreserveFPU == 0, "DX8Wrapper_PreserveFPU starts at %d; dx8wrapper.cpp starts it at 0", DX8Wrapper_PreserveFPU );
	CHECK( DX8Wrapper_IsWindowed, "DX8Wrapper_IsWindowed starts false; dx8wrapper.cpp starts it true" );

	if (failures != 0)
	{
		printf( "render_hooks: %d failure(s)\n", failures );
		return 1;
	}
	printf( "render_hooks: an unmeasured CPU and an unplaced device are reported good enough for every shipped preset\n" );
	return 0;
}
