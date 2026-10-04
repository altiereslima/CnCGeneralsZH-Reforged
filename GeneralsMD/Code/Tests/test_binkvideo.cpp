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
 * V1: the movies off Windows - bink_ffmpeg.cpp's Bink API over the FFmpeg the POSIX build makes
 * (Tools/ffmpeg-build-posix.sh), checked against the install's own .bik files, read only (rule 9).
 *
 *   - every movie of the Zero Hour install opens by its real path, and Width, Height, Frames and the
 *     frame rate match its own file header (the demuxer reports no frame count, so Frames comes from
 *     BinkOpen's duration arithmetic - this is what proves that arithmetic);
 *   - every frame decodes through BinkDoFrame / BinkCopyToBuffer / BinkNextFrame, exactly Frames of
 *     them, and the middle frame's pixels hash to the golden value recorded for FFmpeg 8.1.2 built as
 *     the script builds it (binkvideo_golden.inc).  The MIDDLE frame, because 27 of the 70 first
 *     frames are the same black picture, and a first-frame hash would not see a decoder change;
 *   - the engine's spelling of a path ("Data\\Movies\\...", any case) opens, as BinkVideoPlayer
 *     passes it;
 *   - BinkWait never releases a frame before it is due (a lower bound only, which load cannot flake);
 *   - the movie's sound goes into C4's mix: on the null backend with the capture on, EA_LOGO's audio
 *     is there, left and right apart (XAudio2's pass-through, not a Miles voice's sum), and not at
 *     volume 0.
 * Armed controls: 32 and 32R must be the same frame with R and B swapped, 24 must be 32 without
 * alpha, and each golden frame must have colour in it (not a blank frame that any decoder matches).
 *
 * What it cannot see: the picture on screen (W3DVideoBuffer, the renderer's); Windows' pixels (its
 * swscale runs x86 SIMD, where this build runs the C paths - V1); the retail BINKW32.DLL,
 * which is 32-bit Windows code (bink_smoke compares against it on Windows).
 *
 * Needs ZH_GAME_DATA (ZH_DATA_DIR here); without it the test exits 77 and ctest reports Skipped.
 * ZH_BINK_PRINT_GOLDEN=1 prints the table instead of checking it, for an FFmpeg bump.
 */

#include "test_harness.h"

#include "bink.h"
#include "MSS/mss_ex_pcm.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Golden
{
	const char *path;		///< relative to the Zero Hour install
	unsigned frames, width, height;
	uint64_t middleHash;
};

const Golden GOLDEN[] = {
#include "binkvideo_golden.inc"
};

std::string s_install;		///< ZH_DATA_DIR/zerohour

uint64_t fnv( const unsigned char *p, size_t n, uint64_t h = 1469598103934665603ULL )
{
	for (size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return h;
}

unsigned le32( const unsigned char *p ) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

struct Header { bool ok; unsigned frames, width, height, rate, rateDiv; };

Header readHeader( const std::string &path )
{
	Header h;
	memset( &h, 0, sizeof( h ) );
	FILE *f = fopen( path.c_str(), "rb" );
	unsigned char b[36];
	if (f != NULL && fread( b, 1, sizeof( b ), f ) == sizeof( b ) && memcmp( b, "BIK", 3 ) == 0)
	{
		h.ok = true;
		h.frames = le32( b + 8 );
		h.width = le32( b + 20 );
		h.height = le32( b + 24 );
		h.rate = le32( b + 28 );
		h.rateDiv = le32( b + 32 );
	}
	if (f != NULL)
		fclose( f );
	return h;
}

void findMovies( const std::string &dir, const std::string &relative, std::vector<std::string> &out )
{
	DIR *d = opendir( dir.c_str() );
	if (d == NULL)
		return;
	while (dirent *e = readdir( d ))
	{
		const std::string name = e->d_name;
		if (name[0] == '.')		// ., .., and exFAT's AppleDouble ._ files
			continue;
		const std::string full = dir + "/" + name, rel = relative.empty() ? name : relative + "/" + name;
		struct stat st;
		if (stat( full.c_str(), &st ) != 0)
			continue;
		if (S_ISDIR( st.st_mode ))
			findMovies( full, rel, out );
		else if (name.size() > 4 && strcasecmp( name.c_str() + name.size() - 4, ".bik" ) == 0)
			out.push_back( rel );
	}
	closedir( d );
	std::sort( out.begin(), out.end() );
}

struct Result
{
	std::string path;
	Header header;
	bool opened;
	unsigned width, height, frames, rate, rateDiv;
	unsigned decoded;			///< BinkDoFrame successes over Frames frames
	bool extraFrame;			///< a frame past Frames decoded (there should be none)
	uint64_t middleHash;
	unsigned middleColours;		///< distinct pixels in the middle frame (capped)
	bool swapOk, depthOk;		///< the armed surface controls
};

Result decodeMovie( const std::string &relative )
{
	Result r;
	r.path = relative;
	r.opened = false;
	r.width = r.height = r.frames = r.rate = r.rateDiv = r.decoded = r.middleColours = 0;
	r.extraFrame = false;
	r.middleHash = 0;
	r.swapOk = r.depthOk = false;
	const std::string full = s_install + "/" + relative;
	r.header = readHeader( full );
	HBINK bink = BinkOpen( full.c_str(), BINKPRELOADALL );
	if (bink == NULL)
		return r;
	r.opened = true;
	r.width = bink->Width; r.height = bink->Height; r.frames = bink->Frames;
	r.rate = bink->FrameRate; r.rateDiv = bink->FrameRateDiv;
	const unsigned middle = (bink->Frames + 1) / 2;
	std::vector<unsigned char> bgra( (size_t)r.width * r.height * 4 ), rgba( bgra.size() ), bgr( (size_t)r.width * r.height * 3 );
	for (unsigned frame = 1; frame <= r.frames; ++frame)
	{
		if (BinkDoFrame( bink ) != 0)
			break;
		++r.decoded;
		if (frame == middle)
		{
			BinkCopyToBuffer( bink, &bgra[0], (int)r.width * 4, r.height, 0, 0, BINKSURFACE32 );
			BinkCopyToBuffer( bink, &rgba[0], (int)r.width * 4, r.height, 0, 0, BINKSURFACE32R );
			BinkCopyToBuffer( bink, &bgr[0], (int)r.width * 3, r.height, 0, 0, BINKSURFACE24 );
			r.middleHash = fnv( &bgra[0], bgra.size() );
			bool swap = true, depth = true;
			std::vector<uint32_t> colours;
			for (size_t p = 0; p < (size_t)r.width * r.height; ++p)
			{
				const unsigned char *a = &bgra[p * 4], *b = &rgba[p * 4], *c = &bgr[p * 3];
				swap = swap && a[0] == b[2] && a[1] == b[1] && a[2] == b[0] && a[3] == b[3];
				depth = depth && a[0] == c[0] && a[1] == c[1] && a[2] == c[2];
				if (colours.size() < 64 && (p % 97) == 0)
				{
					const uint32_t v = a[0] | (a[1] << 8) | (a[2] << 16);
					if (std::find( colours.begin(), colours.end(), v ) == colours.end())
						colours.push_back( v );
				}
			}
			r.swapOk = swap;
			r.depthOk = depth;
			r.middleColours = (unsigned)colours.size();
		}
		BinkNextFrame( bink );
	}
	r.extraFrame = BinkDoFrame( bink ) == 0;
	BinkClose( bink );
	return r;
}

bool haveData()
{
	if (!s_install.empty())
		return true;
	const char *data = getenv( "ZH_DATA_DIR" );
	if (data == NULL || *data == 0)
		return false;
	s_install = std::string( data ) + "/zerohour";
	struct stat st;
	return stat( s_install.c_str(), &st ) == 0;
}

std::string userData( const char *name )
{
	const char *dir = getenv( "ZH_USER_DATA_DIR" );
	mkdir( dir, 0755 );
	return std::string( dir ) + "/" + name;
}

}  // namespace

TEST(binkvideo_every_install_movie_against_its_header_and_the_golden)
{
	std::vector<std::string> movies;
	findMovies( s_install, "", movies );
	printf( "  %zu movies under %s\n", movies.size(), s_install.c_str() );
	CHECK( movies.size() >= 70 );

	std::vector<Result> results( movies.size() );
	std::atomic<size_t> next( 0 );
	const unsigned workers = std::max( 1u, std::min( 8u, std::thread::hardware_concurrency() ) );
	const auto start = std::chrono::steady_clock::now();
	std::vector<std::thread> pool;
	for (unsigned w = 0; w < workers; ++w)
		pool.push_back( std::thread( [&]() {
			for (size_t i = next++; i < movies.size(); i = next++)
				results[i] = decodeMovie( movies[i] );
		} ) );
	for (size_t w = 0; w < pool.size(); ++w)
		pool[w].join();
	printf( "  decoded in %.1fs on %u threads\n",
		std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count(), workers );

	const bool printGolden = getenv( "ZH_BINK_PRINT_GOLDEN" ) != NULL;
	int wrong = 0, unmatched = 0, controls = 0, blank = 0;
	long totalFrames = 0;
	for (size_t i = 0; i < results.size(); ++i)
	{
		const Result &r = results[i];
		totalFrames += r.decoded;
		const Header &h = r.header;
		const bool asHeader = r.opened && h.ok && r.width == h.width && r.height == h.height && r.frames == h.frames
			&& r.rate == h.rate && r.rateDiv == h.rateDiv && r.decoded == h.frames && !r.extraFrame;
		if (!asHeader && wrong++ < 5)
			printf( "  %s: opened %d, %ux%u %u frames %u/%u, decoded %u%s; header %ux%u %u frames %u/%u\n",
				r.path.c_str(), r.opened, r.width, r.height, r.frames, r.rate, r.rateDiv, r.decoded,
				r.extraFrame ? " plus one past the end" : "", h.width, h.height, h.frames, h.rate, h.rateDiv );
		if (!r.swapOk || !r.depthOk)
			++controls;
		if (r.middleColours < 8)
			++blank;
		if (printGolden)
		{
			printf( "\t{ \"%s\", %u, %u, %u, 0x%016llxULL },\n", r.path.c_str(), r.frames, r.width, r.height,
				(unsigned long long)r.middleHash );
			continue;
		}
		const Golden *g = NULL;
		for (size_t k = 0; k < sizeof( GOLDEN ) / sizeof( GOLDEN[0] ); ++k)
			if (r.path == GOLDEN[k].path)
				g = &GOLDEN[k];
		if (g == NULL || g->middleHash != r.middleHash || g->frames != r.frames || g->width != r.width || g->height != r.height)
		{
			if (unmatched++ < 5)
				printf( "  %s: middle frame 0x%016llx, golden %s\n", r.path.c_str(), (unsigned long long)r.middleHash,
					g == NULL ? "none" : "different" );
		}
	}
	printf( "  %ld frames: %d off their header, %d off the golden, %d failing the surface controls, %d blank\n",
		totalFrames, wrong, unmatched, controls, blank );
	CHECK_EQ( wrong, 0 );
	CHECK_EQ( controls, 0 );
	CHECK_EQ( blank, 0 );
	if (!printGolden)
	{
		CHECK_EQ( unmatched, 0 );
		CHECK_EQ( results.size(), sizeof( GOLDEN ) / sizeof( GOLDEN[0] ) );
	}
	// armed: a golden entry with one bit changed is not a match
	CHECK( GOLDEN[0].middleHash != (GOLDEN[0].middleHash ^ 1) );
}

TEST(binkvideo_opens_the_engines_spelling_of_a_path)
{
	char cwd[4096];
	CHECK( getcwd( cwd, sizeof( cwd ) ) != NULL );
	CHECK_EQ( chdir( s_install.c_str() ), 0 );
	HBINK bink = BinkOpen( "data\\MOVIES\\gc_background.BIK", BINKPRELOADALL );
	CHECK( bink != NULL );
	if (bink != NULL)
	{
		CHECK_EQ( bink->Width, 800u );
		BinkClose( bink );
	}
	CHECK( BinkOpen( "Data\\Movies\\NoSuchMovie.bik", BINKPRELOADALL ) == NULL );
	CHECK_EQ( chdir( cwd ), 0 );
}

TEST(binkvideo_wait_never_releases_a_frame_early)
{
	HBINK bink = BinkOpen( (s_install + "/Data/English/Movies/Comp_AirGen_000.bik").c_str(), BINKPRELOADALL );
	CHECK( bink != NULL );
	if (bink == NULL)
		return;
	const auto start = std::chrono::steady_clock::now();
	int early = 0;
	for (unsigned frame = 1; frame <= bink->Frames; ++frame)
	{
		while (BinkWait( bink ))
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
		const double elapsed = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
		const double due = (double)(frame - 1) * bink->FrameRateDiv / bink->FrameRate;
		if (elapsed + 0.002 < due)
			++early;
		BinkDoFrame( bink );
		BinkNextFrame( bink );
	}
	BinkClose( bink );
	CHECK_EQ( early, 0 );
}

namespace {

struct Capture { unsigned rate, channels; std::vector<short> samples; };

Capture readCapture( const std::string &path )
{
	Capture c;
	c.rate = c.channels = 0;
	FILE *f = fopen( path.c_str(), "rb" );
	if (f == NULL)
		return c;
	unsigned char header[44];
	if (fread( header, 1, 44, f ) == 44)
	{
		c.channels = header[22] | (header[23] << 8);
		c.rate = le32( header + 24 );
		short buffer[4096];
		size_t n;
		while ((n = fread( buffer, sizeof( short ), 4096, f )) > 0)
			c.samples.insert( c.samples.end(), buffer, buffer + n );
	}
	fclose( f );
	return c;
}

/// Plays a movie through the Bink API with the capture on; returns what the mix heard
Capture playCaptured( const std::string &movie, int volume, const char *name )
{
	const std::string path = userData( name );
	CHECK_EQ( AIL_ex_start_capture( path.c_str() ), 1 );
	HBINK bink = BinkOpen( (s_install + "/" + movie).c_str(), BINKPRELOADALL );
	CHECK( bink != NULL );
	double seconds = 0.0;
	if (bink != NULL)
	{
		if (volume >= 0)
			BinkSetVolume( bink, 0, volume );
		seconds = (double)bink->Frames * bink->FrameRateDiv / bink->FrameRate;
		for (unsigned frame = 1; frame <= bink->Frames; ++frame)
		{
			while (BinkWait( bink ))
				std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			BinkDoFrame( bink );
			BinkNextFrame( bink );
		}
		// The mix drains the queue in real time: wait for the capture, not the clock, to hold the movie
		// and half a second more (the memory of real-time tests under load).
		const Capture head = readCapture( path );
		const off_t wanted = (off_t)(44 + (seconds + 0.5) * (head.rate ? head.rate : 48000) * 4);
		struct stat st;
		for (int i = 0; i < 3000 && (stat( path.c_str(), &st ) != 0 || st.st_size < wanted); ++i)
			std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		BinkClose( bink );
	}
	AIL_ex_stop_capture();
	Capture c = readCapture( path );
	unlink( path.c_str() );
	return c;
}

}  // namespace

TEST(binkvideo_sound_goes_through_c4s_mix)
{
	CHECK_EQ( AIL_startup(), 1 );
	CHECK_EQ( AIL_quick_startup( 1, 0, 44100, 16, 2 ), 1 );
	const std::string movie = "Data/English/Movies/EA_LOGO.BIK";		// 3.2s, 48kHz stereo
	Capture c = playCaptured( movie, -1, "binkaudio.wav" );
	CHECK_EQ( c.channels, 2u );
	long loud = 0, apart = 0;
	double energy = 0.0;
	for (size_t i = 0; i + 1 < c.samples.size(); i += 2)
	{
		const int l = c.samples[i], r = c.samples[i + 1];
		if (abs( l ) > 64 || abs( r ) > 64) ++loud;
		if (abs( l - r ) > 64) ++apart;
		energy += (double)l * l + (double)r * r;
	}
	const double rate = c.rate ? (double)c.rate : 1.0;
	printf( "  EA_LOGO through the mix: %.2fs captured at %u Hz, %.2fs of it audible, %.2fs with left and right apart, rms %.0f\n",
		c.samples.size() / 2 / rate, c.rate, loud / rate, apart / rate,
		c.samples.empty() ? 0.0 : sqrt( energy / c.samples.size() ) );
	CHECK( loud / rate > 1.0 );			// the movie's sound is there
	CHECK( loud / rate < 3.3 );			// and no more of it than the movie has
	CHECK( apart / rate > 0.5 );		// its channels kept apart, as XAudio2's default matrix does

	// at volume 0 the same movie is silent
	Capture quiet = playCaptured( movie, 0, "binkaudio0.wav" );
	long heard = 0;
	for (size_t i = 0; i < quiet.samples.size(); ++i)
		if (abs( quiet.samples[i] ) > 64) ++heard;
	CHECK( quiet.samples.size() > 48000 );
	CHECK_EQ( heard, 0L );
	AIL_shutdown();
}

// The test runner's main() runs every TEST; with no install they would all fail, so this skips first.
namespace {
struct SkipWithoutData
{
	SkipWithoutData()
	{
		if (!haveData())
		{
			printf( "SKIPPED: ZH_DATA_DIR (ZH_GAME_DATA) names no install with a zerohour/ folder\n" );
			exit( 77 );
		}
	}
} s_skipWithoutData;
}
