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
 * ffref_judge: FFReference's own replay of a capture set (L2's oracle side, and A3's on any backend).
 *
 * Every draw_NNNNN.cap in the folder is read by ffcapture.h (written from the capture side's description of the
 * format, not from the device or its harness), its textures decoded there, its programs decoded by
 * ffprogram.h, and drawn by FFReference into a target of the capture's size.  With --gpu, the GPU's own
 * pixels for the same capture (the device harness's dump, read as a black box) are compared with it,
 * and each capture gets a verdict:
 *   agree        every pixel exact or inside the envelope's documented freedoms
 *   DISAGREE     pixels outside it: counted, with the worst one and the zones
 *   refused      FFReference's documented refusals (it says which); nothing is judged
 *   unreadable   the file disagrees with the format's description (it says where)
 * Without --gpu it draws only, and reports what the reference did: a check of the reader itself.
 *
 * THE STARTING POINT, as the capture side describes the harness's replay: before each draw the whole target is cleared
 * to 0xFF3F2F1F, depth to 1.0 and stencil to 0 (a D24S8 depth-stencil), then the captured viewport is set;
 * a draw with no depth-stencil bound has depth and stencil off.  The GPU dump is <capture>.cap.bgra: the
 * back buffer read back after the draw, B, G, R, A bytes, rows top first, TargetWidth x TargetHeight.
 *
 * ANISOTROPIC is replayed as LINEAR, as FFReference's ruling on the harness accepted it: D3D9 defines no
 * anisotropic footprint, so an envelope around one would be no oracle.  Every run counts and prints the
 * draws it did this to ("N draws: ANISOTROPIC replayed as LINEAR"), so a pass never hides it; the GPU
 * side of the comparison must replay them the same way (the harness does, by that ruling).
 *
 *   ffref_judge <capture dir> [--gpu <dump dir>] [--only draw_NNNNN] [--mutate <bits>] [--shift-gpu] [--detail]
 *               [--lod-delta <levels>] [--lod-sweep]
 *
 * For a disagreement it lists the outside pixels (up to 12): the GPU's colour, the reference's nominal
 * colour and its envelope; with --detail (best with --only) also FFReference's record of the pixel: the
 * triangle that wrote it, the coordinates and LOD it sampled at, and the triangle's corners on screen.
 *
 * Armed controls, so an "agree" is a comparison that can fail: --mutate draws with FFReference's own
 * deliberate departures (ffreference.h's Mutation bits; D3D10's pixel centres, swapped blend factors, ...),
 * and --shift-gpu compares each capture with the NEXT capture's GPU picture.  Each line also says how
 * many pixels the reference wrote and how many the GPU changed from the clear colour, so a draw that
 * wrote nothing on either side is counted as such ("empty"), never as evidence.
 * --lod-delta is a diagnostic, never a verdict: it widens one freedom (the LOD's, 0.6 by default), so
 * a disagreement that goes away under it is one of LOD alone.  The run says so on its first line.
 * --lod-sweep, also a diagnostic: for each disagreeing capture, redraws it with every stage's
 * MIPMAPLODBIAS at -4..+4 in eighths and gives each outside pixel the bias whose colour comes closest to
 * the GPU's, with what is left, the pixel's LOD and its triangle's least height on screen.  A pixel some
 * bias matches is one of LOD, by that much; one no bias matches is not.
 * Exit status: 0 no disagreement and nothing unreadable; 1 otherwise.
 */

#include "ffreference/ffcapture.h"

#include <dirent.h>
#include <algorithm>
#include <map>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace FFRef;
using namespace FFCapture;

namespace {

std::vector<std::string> captureNames( const std::string &dir )
{
	std::vector<std::string> names;
	DIR *d = opendir( dir.c_str() );
	if (d == NULL)
		return names;
	while (dirent *e = readdir( d ))
	{
		const std::string n = e->d_name;
		if (n.size() > 4 && n.compare( n.size() - 4, 4, ".cap" ) == 0 && n.compare( 0, 5, "draw_" ) == 0)
			names.push_back( n.substr( 0, n.size() - 4 ) );
	}
	closedir( d );
	std::sort( names.begin(), names.end() );
	return names;
}

/* The port's D3DX stub assembles nothing: for D3DXAssembleShader it emits a stream that carries the text.
	 Its layout, as the capture side described it in words (not read from d3dx9posix.cpp): word 0 the version token; word 1
	 a comment token, 0xFFFE | N << 16, N = 2 + ceil(L / 4); word 2 the tag 0x5253485A ("ZHSR"); word 3 L,
	 the text's byte count; then the L bytes, zero-padded to a word; then END (0x0000FFFF), no instructions.
	 Recognised only when all of that holds; anything else is taken as real tokens. */
bool stubText( const std::vector<uint32_t> &t, std::string &text )
{
	if (t.size() < 5 || (t[1] & 0xFFFF) != 0xFFFE || t[2] != 0x5253485Au)
		return false;
	const uint32_t n = (t[1] >> 16) & 0x7FFF, length = t[3];
	if (n < 2 || (size_t)length > (size_t)(n - 2) * 4 || 2 + (size_t)n + 1 != t.size() || t.back() != 0x0000FFFFu)
		return false;
	text.assign( (const char *)&t[4], length );
	return true;
}

const char *primitiveName( uint32_t t )
{
	static const char *names[] = { "?", "points", "lines", "linestrip", "triangles", "strip", "fan" };
	return t < 7 ? names[t] : "?";
}

/// A triangle's least height on screen: twice its area over its longest edge (a sliver's is under a pixel)
double leastHeight( const double c[3][2] )
{
	const double area2 = fabs( (c[1][0] - c[0][0]) * (c[2][1] - c[0][1]) - (c[2][0] - c[0][0]) * (c[1][1] - c[0][1]) );
	double longest = 0;
	for (int e = 0; e < 3; ++e)
		longest = std::max( longest, hypot( c[(e + 1) % 3][0] - c[e][0], c[(e + 1) % 3][1] - c[e][1] ) );
	return longest > 0 ? area2 / longest : 0;
}

/// --lod-sweep: see the file comment.  `target` is the unbiased draw, with detail; `gpu` is R, G, B, A.
void sweepLod( const DrawState &state, const Header &h, const std::vector<Vertex> &vertices, const Capture &cap,
	int count, const Target &target, const std::vector<uint8_t> &gpu, unsigned mutations )
{
	const double base = 2.0 / 255.0;
	const int channels = target.hasAlpha ? 4 : 3;
	std::vector<size_t> outside;
	for (size_t i = 0; i < target.color.size(); ++i)
	{
		const uint8_t *g = &gpu[i * 4];
		const Color &lo = target.lo[i], &hi = target.hi[i];
		const double gv[4] = { g[0] / 255.0, g[1] / 255.0, g[2] / 255.0, g[3] / 255.0 };
		const double los[4] = { lo.r, lo.g, lo.b, lo.a }, his[4] = { hi.r, hi.g, hi.b, hi.a };
		bool out = false;
		for (int k = 0; k < channels; ++k)
			out = out || gv[k] < los[k] - base || gv[k] > his[k] + base;
		if (out)
			outside.push_back( i );
	}
	std::vector<double> bestBias( outside.size(), 0 ), bestLeft( outside.size(), 1e9 ), unbiased( outside.size(), 0 );
	for (int step = -32; step <= 32; ++step)
	{
		const double bias = step / 8.0;
		DrawState biased = state;
		for (int st = 0; st < MAX_STAGES; ++st)
			biased.samplerState[st][SAMP_MIPMAPLODBIAS] = floatBits( (float)bias );	// every captured bias is 0
		Target t;
		t.create( target.width, target.height, target.hasAlpha );
		t.clear( colorFromD3D( 0xFF3F2F1Fu ), 1.0, 0 );
		Report report;
		if (!draw( biased, (int)h.primitiveType, vertices.empty() ? NULL : &vertices[0], (int)h.vertexCount,
				h.indexCount ? &cap.indices[0] : NULL, count, t, &report, mutations ))
			return;
		for (size_t k = 0; k < outside.size(); ++k)
		{
			const size_t i = outside[k];
			const uint8_t *g = &gpu[i * 4];
			const Color &c = t.color[i];
			const double cv[4] = { c.r, c.g, c.b, c.a };
			double left = 0;
			for (int ch = 0; ch < channels; ++ch)
				left = std::max( left, fabs( g[ch] / 255.0 - cv[ch] ) * 255.0 );
			if (step == 0)
				unbiased[k] = left;
			if (left < bestLeft[k] - 1e-9 || (fabs( left - bestLeft[k] ) <= 1e-9 && fabs( bias ) < fabs( bestBias[k] )))
				bestLeft[k] = left, bestBias[k] = bias;
		}
	}
	int matched = 0;
	for (size_t k = 0; k < outside.size(); ++k)
	{
		const size_t i = outside[k];
		const PixelDetail &d = target.detail[i];
		const double a0 = hypot( d.axes[0][0], d.axes[0][1] ), a1 = hypot( d.axes[1][0], d.axes[1][1] );
		const bool ok = bestLeft[k] <= 2.0;
		matched += ok;
		printf( "    sweep (%d, %d): lod %.2f, footprint %.2f x %.2f texels, triangle %d least height %.2f px;"
			" unbiased off by %.0f, best bias %+.3f leaves %.0f/255%s\n", (int)(i % target.width), (int)(i / target.width),
			d.lod[0], a0, a1, d.primitive, leastHeight( d.screen ), unbiased[k], bestBias[k], bestLeft[k],
			ok ? "" : "  <- no bias matches" );
	}
	printf( "    sweep: %d of %d outside pixels matched within 2/255 by some LOD bias in -4..+4\n", matched, (int)outside.size() );
}

}  // namespace

int main( int argc, char *argv[] )
{
	if (argc < 2)
	{
		fprintf( stderr, "usage: ffref_judge <capture dir> [--gpu <dump dir>] [--only draw_NNNNN]\n" );
		return 2;
	}
	const std::string dir = argv[1];
	std::string gpuDir, only;
	unsigned mutations = 0;
	bool shiftGpu = false, detail = false, lodSweep = false;
	Freedoms freedoms;
	for (int i = 2; i < argc; ++i)
	{
		if (strcmp( argv[i], "--shift-gpu" ) == 0)
			shiftGpu = true;
		else if (strcmp( argv[i], "--detail" ) == 0)
			detail = true;
		else if (strcmp( argv[i], "--lod-sweep" ) == 0)
			lodSweep = detail = true;
		else if (i + 1 < argc && strcmp( argv[i], "--gpu" ) == 0)
			gpuDir = argv[++i];
		else if (i + 1 < argc && strcmp( argv[i], "--only" ) == 0)
			only = argv[++i];
		else if (i + 1 < argc && strcmp( argv[i], "--mutate" ) == 0)
			mutations = (unsigned)strtoul( argv[++i], NULL, 0 );
		else if (i + 1 < argc && strcmp( argv[i], "--lod-delta" ) == 0)
		{
			freedoms.lodDelta = atof( argv[++i] );
			printf( "DIAGNOSTIC: the LOD freedom widened to +-%g levels; not a verdict\n", freedoms.lodDelta );
		}
	}
	std::vector<std::string> names = captureNames( dir );
	if (names.empty())
	{
		fprintf( stderr, "ffref_judge: no draw_*.cap in %s\n", dir.c_str() );
		return 2;
	}
	std::map<std::string, Texture> textures;
	std::map<std::string, std::string> textureErrors;
	int drawn = 0, refused = 0, unreadable = 0, agree = 0, disagree = 0, anisotropic = 0, empty = 0;
	int stubPrograms = 0;
	for (size_t n = 0; n < names.size(); ++n)
	{
		if (!only.empty() && names[n] != only)
			continue;
		Capture cap;
		std::string error;
		if (!readCapture( dir, names[n], cap, error ))
		{
			printf( "unreadable %s: %s\n", names[n].c_str(), error.c_str() );
			++unreadable;
			continue;
		}
		const Header &h = cap.header;
		VertexLayout layout;
		if (!layoutFromFVF( h.fvf, layout, error ) || layout.size != h.stride)
		{
			if (error.empty())
			{
				char what[ 96 ];
				snprintf( what, sizeof( what ), "FVF 0x%X describes %u bytes, the stride is %u", (unsigned)h.fvf,
					layout.size, (unsigned)h.stride );
				error = what;
			}
			printf( "unreadable %s: %s\n", names[n].c_str(), error.c_str() );
			++unreadable;
			continue;
		}
		const Texture *stageTextures[8] = { 0 };
		std::string textureProblem;
		for (int s = 0; s < 8; ++s)
		{
			const std::string &t = h.textures[s];
			if (t.empty())
				continue;
			if (!textures.count( t ) && !textureErrors.count( t ))
			{
				Texture tex;
				std::string terr;
				if (readTexture( dir + "/" + t, tex, terr ))
					textures[t] = tex;
				else
					textureErrors[t] = terr;
			}
			if (textureErrors.count( t ))
				textureProblem = textureErrors[t];
			else
				stageTextures[s] = &textures[t];
		}
		if (!textureProblem.empty())
		{
			printf( "unreadable %s: %s\n", names[n].c_str(), textureProblem.c_str() );
			++unreadable;
			continue;
		}
		Program vertexProgram, pixelProgram;
		const Program *vp = NULL, *pp = NULL;
		std::string programRefusal;
		if (cap.hasProg)
		{
			if (cap.prog.vertexPresent)
			{
				if (!decodeProgram( &cap.prog.vertexTokens[0], cap.prog.vertexTokens.size(), vertexProgram )
						|| !vertexProgram.refusals.empty())
					programRefusal = "the vertex program " + cap.prog.vertexName + ": "
						+ (vertexProgram.refusals.empty() ? std::string( "undecodable" ) : vertexProgram.refusals[0]);
				vp = &vertexProgram;
			}
			if (cap.prog.pixelPresent)
			{
				std::vector<uint32_t> assembled;
				std::string text;
				const std::vector<uint32_t> *tokens = &cap.prog.pixelTokens;
				if (stubText( cap.prog.pixelTokens, text ))
				{
					// the D3DX stub's text carrier: the program is the carried text, assembled here
					std::string aerr;
					if (!assemblePixelProgram( text, assembled, aerr ))
						programRefusal = "the pixel program " + cap.prog.pixelName + ": its carried text: " + aerr;
					tokens = &assembled;
					++stubPrograms;
				}
				if (programRefusal.empty() && (!decodeProgram( &(*tokens)[0], tokens->size(), pixelProgram )
						|| !pixelProgram.refusals.empty()))
					programRefusal = "the pixel program " + cap.prog.pixelName + ": "
						+ (pixelProgram.refusals.empty() ? std::string( "undecodable" ) : pixelProgram.refusals[0]);
				pp = &pixelProgram;
			}
		}
		DrawState state;
		if (!drawState( cap, layout, stageTextures, vp, pp, state, error ))
		{
			printf( "refused %s: %s\n", names[n].c_str(), error.c_str() );
			++refused;
			continue;
		}
		bool substituted = false;
		for (int s = 0; s < MAX_STAGES; ++s)
			for (int f = SAMP_MAGFILTER; f <= SAMP_MINFILTER; ++f)
				if (state.samplerState[s][f] == TEXF_ANISOTROPIC)
				{
					state.samplerState[s][f] = TEXF_LINEAR;
					substituted = true;
				}
		anisotropic += substituted ? 1 : 0;
		if (!programRefusal.empty())
		{
			printf( "refused %s: %s\n", names[n].c_str(), programRefusal.c_str() );
			++refused;
			continue;
		}
		std::vector<Vertex> vertices( h.vertexCount );
		for (uint32_t v = 0; v < h.vertexCount; ++v)
			decodeVertex( layout, &cap.vertices[(size_t)v * h.stride], vertices[v] );
		if (vp != NULL && !cap.prog.elements.empty())
		{
			// the vertex program reads the declaration's elements from each vertex's own bytes
			unsigned present = 0;
			for (uint32_t v = 0; v < h.vertexCount; ++v)
			{
				unsigned p = 0;
				if (!declarationInputs( &cap.vertices[(size_t)v * h.stride], &cap.prog.elements[0],
						(int)cap.prog.elements.size(), vertices[v].programInput, p, error ))
				{
					programRefusal = error;
					break;
				}
				present = p;
			}
			if (!programRefusal.empty())
			{
				printf( "refused %s: the declaration: %s\n", names[n].c_str(), programRefusal.c_str() );
				++refused;
				continue;
			}
			state.programInputsGiven = true;
			state.programInputPresent = present;
		}
		Target target;
		// A8R8G8B8 keeps alpha; X8R8G8B8 reads destination alpha as 1 (D3DFORMAT)
		target.create( (int)h.targetWidth, (int)h.targetHeight, h.targetFormat != 22 );
		target.clear( colorFromD3D( 0xFF3F2F1Fu ), 1.0, 0 );
		target.recordDetail = detail;
		Report report;
		const int count = h.indexCount ? (int)h.indexCount : (int)h.vertexCount;
		const bool ok = draw( state, (int)h.primitiveType, vertices.empty() ? NULL : &vertices[0], (int)h.vertexCount,
			h.indexCount ? &cap.indices[0] : NULL, count, target, &report, mutations, freedoms );
		if (!ok)
		{
			printf( "refused %s: %s\n", names[n].c_str(), report.refusals.empty() ? "?" : report.refusals[0].c_str() );
			++refused;
			continue;
		}
		++drawn;
		printf( "drawn %s: v%u %s x%u, fvf 0x%X%s%s, %ld triangles (%ld culled, %ld clipped away), %ld pixels written\n",
			names[n].c_str(), (unsigned)h.version, primitiveName( h.primitiveType ), (unsigned)h.primitiveCount,
			(unsigned)h.fvf, vp ? " vs" : "", pp ? " ps" : "", report.trianglesIn, report.trianglesCulled,
			report.trianglesClippedAway, report.pixelsWritten );
		if (!gpuDir.empty())
		{
			const std::string &gpuName = shiftGpu ? names[(n + 1) % names.size()] : names[n];
			const std::string dump = gpuDir + "/" + gpuName + ".cap.bgra";
			FILE *f = fopen( dump.c_str(), "rb" );
			const size_t bytes = (size_t)h.targetWidth * h.targetHeight * 4;
			std::vector<uint8_t> gpu( bytes );
			const size_t got = f ? fread( &gpu[0], 1, bytes, f ) : 0;
			const bool extra = f && fgetc( f ) != EOF;
			if (f)
				fclose( f );
			if (got != bytes || extra)
			{
				printf( "  no GPU picture: %s %s\n", dump.c_str(), f ? "has the wrong size" : "is missing" );
				++unreadable;
				continue;
			}
			long gpuChanged = 0;
			for (size_t i = 0; i < bytes; i += 4)
			{
				// the clear colour 0xFF3F2F1F is B 0x1F, G 0x2F, R 0x3F, A 0xFF in the dump's byte order
				if (gpu[i] != 0x1F || gpu[i + 1] != 0x2F || gpu[i + 2] != 0x3F || gpu[i + 3] != 0xFF)
					++gpuChanged;
				std::swap( gpu[i], gpu[i + 2] );		// B, G, R, A -> R, G, B, A
			}
			const Comparison c = compare( target, &gpu[0], (int)h.targetWidth * 4 );
			const bool nothing = report.pixelsWritten == 0 && gpuChanged == 0;
			if (!c.passed())
				++disagree;
			else if (nothing)
				++empty;
			else
				++agree;
			printf( "  %s: reference wrote %ld, GPU changed %ld; %ld pixels, %ld exact, %ld in a freedom, %ld outside",
				!c.passed() ? "DISAGREE" : nothing ? "empty" : "agree", report.pixelsWritten, gpuChanged,
				c.pixels, c.exact, c.inFreedom, c.outside );
			if (!c.passed())
				printf( ", worst %.1f/255 at (%d, %d)", c.worst * 255, c.worstX, c.worstY );
			printf( "\n" );
			if (!c.passed())
			{
				print( c, stdout, ("    " + names[n]).c_str() );
				// the outside pixels themselves, as compare() judges them: past the envelope by more than base
				const double base = 2.0 / 255.0;
				int listed = 0;
				for (int y = 0; y < (int)h.targetHeight && listed < 12; ++y)
					for (int x = 0; x < (int)h.targetWidth && listed < 12; ++x)
					{
						const size_t i = (size_t)y * h.targetWidth + x;
						const uint8_t *g = &gpu[i * 4];
						const double gv[4] = { g[0] / 255.0, g[1] / 255.0, g[2] / 255.0, g[3] / 255.0 };
						const Color &lo = target.lo[i], &hi = target.hi[i], &nom = target.color[i];
						const double los[4] = { lo.r, lo.g, lo.b, lo.a }, his[4] = { hi.r, hi.g, hi.b, hi.a };
						bool out = false;
						for (int k = 0; k < (target.hasAlpha ? 4 : 3); ++k)
							out = out || gv[k] < los[k] - base || gv[k] > his[k] + base;
						if (!out)
							continue;
						++listed;
						printf( "    outside (%d, %d): gpu %d %d %d %d, nominal %.1f %.1f %.1f %.1f, envelope r %.1f-%.1f g %.1f-%.1f b %.1f-%.1f a %.1f-%.1f (x255)\n",
							x, y, g[0], g[1], g[2], g[3], nom.r * 255, nom.g * 255, nom.b * 255, nom.a * 255,
							lo.r * 255, hi.r * 255, lo.g * 255, hi.g * 255, lo.b * 255, hi.b * 255, lo.a * 255, hi.a * 255 );
						if (detail && !lodSweep && i < target.detail.size())
						{
							const PixelDetail &d = target.detail[i];
							printf( "      triangle %d of %d layer(s); stage 0 uv (%.6f, %.6f) lod %.3f; stage 1 uv (%.6f, %.6f) lod %.3f;"
								" corners (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n", d.primitive, d.layers, d.uv[0][0], d.uv[0][1],
								d.lod[0], d.uv[1][0], d.uv[1][1], d.lod[1], d.screen[0][0], d.screen[0][1], d.screen[1][0],
								d.screen[1][1], d.screen[2][0], d.screen[2][1] );
						}
					}
				if (lodSweep)
					sweepLod( state, h, vertices, cap, count, target, gpu, mutations );
			}
		}
	}
	printf( "ffref_judge: %d drawn, %d refused, %d unreadable", drawn, refused, unreadable );
	if (!gpuDir.empty())
		printf( "; %d agree, %d disagree, %d empty (nothing written on either side)", agree, disagree, empty );
	printf( " (of %d captures)\n", (int)names.size() );
	printf( "  %d draws: ANISOTROPIC replayed as LINEAR\n", anisotropic );
	printf( "  %d draws: a D3DX stub's carried text assembled by FFReference\n", stubPrograms );
	return (unreadable || disagree) ? 1 : 0;
}
