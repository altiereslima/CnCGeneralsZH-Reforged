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

// A3e-asm (b): the port's D3DXAssembleShader is not an assembler, by decision.  It hands the device the
// source text in a comment (capture v2's description: the version token the first word names, then
// 0xFFFE | (N << 16), the tag "ZHSR", the text's length L, the text zero-padded to whole words, N = 2 +
// ceil(L / 4), then 0x0000FFFF), and the device picks a transcription by the registered name.  This
// pins that each of the game's four water texts (read from W3DWater.cpp) arrives exactly, through the
// public entry point Bind_D3DX9_Runtime binds.  Microsoft's tokens for the same texts are pinned by
// d3dx_assemble_oracle; this test cannot see what the device does with the text.

#include "d3dx9runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static int failures = 0;
static bool quiet = false;		// the armed control's own mismatches are expected, not reported
static void check( bool ok, const char *what, size_t k )
{
	if (!ok)
	{
		if (!quiet)
			printf( "  FAIL: program %zu: %s\n", k, what );
		++failures;
	}
}

/// Every "ps.1.1..." string literal in a C++ file, as the compiler would read it
static std::vector<std::string> pixelProgramsIn( const std::string &path )
{
	std::vector<std::string> programs;
	FILE *f = fopen( path.c_str(), "rb" );
	if (f == NULL)
		return programs;
	std::string source;
	char buffer[ 4096 ];
	size_t got;
	while ((got = fread( buffer, 1, sizeof( buffer ), f )) > 0)
		source.append( buffer, got );
	fclose( f );
	size_t at = 0;
	while ((at = source.find( "\"ps.1.1", at )) != std::string::npos)
	{
		std::string text;
		size_t i = at + 1;
		for (; i < source.size() && source[i] != '"'; ++i)
		{
			if (source[i] != '\\') { text += source[i]; continue; }
			++i;
			if (source[i] == 'n') text += '\n';
			else if (source[i] == '\n') {}
			else if (source[i] == '\r' && source[i + 1] == '\n') ++i;
			else text += source[i];
		}
		programs.push_back( text );
		at = i;
	}
	return programs;
}

/// The stub's stream for `text`, checked word by word; the number of problems
static int checkStream( const std::vector<uint32_t> &t, const std::string &text, size_t k )
{
	const int before = failures;
	const uint32_t length = (uint32_t)text.size(), words = 2 + (length + 3) / 4;
	check( t.size() == (size_t)(1 + 1 + words + 1), "the stream's length is not version + comment + END", k );
	if (t.size() < 4)
		return failures - before;
	check( t[0] == 0xFFFF0101u, "the version token is not ps_1_1's", k );
	check( t[1] == (0xFFFEu | (words << 16)), "the comment token's length is not 2 + ceil(L / 4)", k );
	check( t[2] == 0x5253485Au, "the tag is not \"ZHSR\"", k );
	check( t[3] == length, "the recorded length is not the text's", k );
	if (t.size() == (size_t)(1 + 1 + words + 1))
	{
		std::string carried( (const char *)&t[4], length );
		check( carried == text, "the carried text differs from the source's", k );
		const uint8_t *padding = (const uint8_t *)&t[4] + length;
		bool zeroed = true;
		for (uint32_t i = length; i < (words - 2) * 4; ++i)
			zeroed = zeroed && padding[i - length] == 0;
		check( zeroed, "the padding is not zero", k );
		check( t.back() == 0x0000FFFFu, "the last word is not the end token", k );
	}
	return failures - before;
}

int main()
{
	if (!Bind_D3DX9_Runtime())
	{
		printf( "FAIL: Bind_D3DX9_Runtime\n" );
		return 1;
	}
	const std::vector<std::string> programs = pixelProgramsIn(
		std::string( ZH_CODE_DIR ) + "/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp" );
	check( programs.size() == 4, "W3DWater.cpp does not hold the census's four ps.1.1 texts", 0 );
	std::vector<uint32_t> first;
	for (size_t k = 0; k < programs.size(); ++k)
	{
		LPD3DXBUFFER shader = NULL, errors = NULL;
		const RenderResult hr = D3DXAssembleShader( programs[k].c_str(), (unsigned)programs[k].size(), NULL, NULL, 0, &shader, &errors );
		check( hr == 0 && shader != NULL, "D3DXAssembleShader failed", k );
		if (shader == NULL)
			continue;
		std::vector<uint32_t> tokens( shader->GetBufferSize() / 4 );
		memcpy( tokens.data(), shader->GetBufferPointer(), tokens.size() * 4 );
		shader->Release();
		if (errors)
			errors->Release();
		if (checkStream( tokens, programs[k], k ) == 0)
			printf( "  ok: program %zu: %zu words, the %zu-byte text carried exactly\n", k, tokens.size(), programs[k].size() );
		if (k == 0)
			first = tokens;
	}
	// the armed control: the same stream against the text with one byte changed must be caught
	if (!first.empty())
	{
		std::string spoiled = programs[0];
		spoiled[spoiled.size() / 2] ^= 1;
		const int saved = failures;
		quiet = true;
		const int caught = checkStream( first, spoiled, 0 );
		quiet = false;
		failures = saved;
		if (caught > 0)
			printf( "  ok: the control, one byte of the text changed, is caught\n" );
		else
		{
			printf( "  FAIL: the control was not caught\n" );
			++failures;
		}
	}
	printf( "d3dx_assemble_stub: %s\n", failures ? "FAILED" : "the port's D3DXAssembleShader carries every water text exactly" );
	return failures ? 1 : 0;
}
