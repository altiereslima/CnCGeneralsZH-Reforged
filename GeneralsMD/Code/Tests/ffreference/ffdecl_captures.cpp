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
 * The engine's own D3D8 declaration against the D3D9 elements the port's device made of it, for every
 * programmable capture given (capture v3's draw_<n>.prog).  FFReference reads the D3D8 tokens itself
 * (decodeD3D8Declaration, from d3d8types.h) and the elements through the mapping page
 * (bindingsFromElements); any register bound to other bytes or another type is either a device table
 * bug or a finding.  Also: every stream's declared extent fits its stride, and every stream the D3D8
 * declaration reads is one the capture recorded.
 *
 * The .prog reader is written from the capture side's description of the format (not from the device's writer):
 *   "ZHPG", u32 version (1, or 2 for capture v3);
 *   VERTEX: u32 Present, then if Present char[64] name, u32 TokenCount, tokens; then (Present or not)
 *     u32 ElementCount and ElementCount 8-byte D3DVERTEXELEMENT9s, no D3DDECL_END;
 *   PIXEL: u32 Present, then if Present char[64] name, u32 TokenCount, tokens;
 *   f32 VS c0-c95 [4], f32 PS c0-c7 [4];
 *   version 2: u32 D3D8Count, D3D8Count tokens through D3DVSD_END (0 when the declaration did not come
 *     from D3D8); u32 StreamCount, then per stream u32 Stream, Stride, FirstVertexOffset; the file ends.
 *
 * The armed control: for each capture with a D3D8 declaration, the same comparison with the first
 * register's D3D8 type moved to the next one must disagree - so a pass means the comparison can see a
 * difference in these very captures.
 *
 *   ffdecl_captures <dir or .prog>...
 * Exit status: 0 all agree (and every control was caught); 1 a disagreement or an unreadable file;
 * 77 when no capture held a D3D8 declaration (nothing was compared).
 */

#include "ffreference/ffprogram.h"

#include <dirent.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace FFRef;

namespace {

struct Reader
{
	std::vector<unsigned char> bytes;
	size_t at;
	bool ok;
	uint32_t u32()
	{
		if (!ok || at + 4 > bytes.size())
		{
			ok = false;
			return 0;
		}
		uint32_t v;
		memcpy( &v, &bytes[at], 4 );
		at += 4;
		return v;
	}
	void skip( size_t n )
	{
		if (!ok || at + n > bytes.size())
			ok = false;
		else
			at += n;
	}
};

struct ProgFile
{
	uint32_t version;
	std::vector<DeclarationElement> elements;
	std::vector<uint32_t> d3d8;
	struct Stream { uint32_t stream, stride, first; };
	std::vector<Stream> streams;
};

bool readProg( const std::string &path, ProgFile &out, std::string &error )
{
	Reader r;
	r.at = 0;
	r.ok = true;
	FILE *f = fopen( path.c_str(), "rb" );
	if (f == NULL)
	{
		error = "cannot open";
		return false;
	}
	unsigned char buffer[ 65536 ];
	size_t got;
	while ((got = fread( buffer, 1, sizeof( buffer ), f )) > 0)
		r.bytes.insert( r.bytes.end(), buffer, buffer + got );
	fclose( f );
	if (r.bytes.size() < 8 || memcmp( &r.bytes[0], "ZHPG", 4 ) != 0)
	{
		error = "not a ZHPG file";
		return false;
	}
	r.at = 4;
	out.version = r.u32();
	if (out.version != 1 && out.version != 2)
	{
		error = "a .prog version other than 1 or 2";
		return false;
	}
	for (int section = 0; section < 2; ++section)
	{
		if (r.u32() != 0)
		{
			r.skip( 64 );
			const uint32_t tokens = r.u32();
			r.skip( 4 * (size_t)tokens );
		}
		if (section == 0)
		{
			const uint32_t count = r.u32();
			for (uint32_t i = 0; r.ok && i < count; ++i)
			{
				if (r.at + 8 > r.bytes.size())
				{
					r.ok = false;
					break;
				}
				DeclarationElement e;
				memcpy( &e.stream, &r.bytes[r.at], 2 );
				memcpy( &e.offset, &r.bytes[r.at + 2], 2 );
				e.type = r.bytes[r.at + 4];
				e.method = r.bytes[r.at + 5];
				e.usage = r.bytes[r.at + 6];
				e.usageIndex = r.bytes[r.at + 7];
				out.elements.push_back( e );
				r.at += 8;
			}
		}
	}
	r.skip( (96 + 8) * 4 * 4 );
	if (out.version == 2)
	{
		const uint32_t count = r.u32();
		for (uint32_t i = 0; r.ok && i < count; ++i)
			out.d3d8.push_back( r.u32() );
		const uint32_t streams = r.u32();
		for (uint32_t i = 0; r.ok && i < streams; ++i)
		{
			ProgFile::Stream s;
			s.stream = r.u32();
			s.stride = r.u32();
			s.first = r.u32();
			out.streams.push_back( s );
		}
	}
	if (!r.ok || r.at != r.bytes.size())
	{
		char what[ 96 ];
		snprintf( what, sizeof( what ), "%s at byte %u of %u", r.ok ? "bytes left over" : "short", (unsigned)r.at,
			(unsigned)r.bytes.size() );
		error = what;
		return false;
	}
	return true;
}

void collect( const std::string &path, std::vector<std::string> &files )
{
	DIR *d = opendir( path.c_str() );
	if (d == NULL)
	{
		files.push_back( path );
		return;
	}
	std::vector<std::string> names;
	while (dirent *e = readdir( d ))
	{
		const std::string name = e->d_name;
		if (name.size() > 5 && name.compare( name.size() - 5, 5, ".prog" ) == 0)
			names.push_back( path + "/" + name );
	}
	closedir( d );
	std::sort( names.begin(), names.end() );
	files.insert( files.end(), names.begin(), names.end() );
}

/// D1's widths, by D3DVSDT_ order: the extent a binding reaches
unsigned extent( const RegisterBinding &b )
{
	static const unsigned width[ 8 ] = { 4, 8, 12, 16, 4, 4, 4, 8 };
	return b.offset + (b.type < 8 ? width[b.type] : 0);
}

}	// namespace

int main( int argc, char **argv )
{
	if (argc < 2)
	{
		fprintf( stderr, "usage: ffdecl_captures <dir or .prog>...\n" );
		return 2;
	}
	std::vector<std::string> files;
	for (int i = 1; i < argc; ++i)
		collect( argv[i], files );
	int compared = 0, agreed = 0, versionOne = 0, noD3D8 = 0, failures = 0, controlsCaught = 0;
	for (size_t i = 0; i < files.size(); ++i)
	{
		const std::string &path = files[i];
		const char *name = strrchr( path.c_str(), '/' ) ? strrchr( path.c_str(), '/' ) + 1 : path.c_str();
		ProgFile prog;
		std::string error;
		if (!readProg( path, prog, error ))
		{
			printf( "FAIL %s: unreadable: %s\n", name, error.c_str() );
			++failures;
			continue;
		}
		if (prog.version == 1)
		{
			printf( "     %s: .prog version 1 (before capture v3): no D3D8 tokens\n", name );
			++versionOne;
			continue;
		}
		if (prog.d3d8.empty())
		{
			printf( "     %s: no D3D8 declaration (D3D8Count 0); %u D3D9 elements\n", name, (unsigned)prog.elements.size() );
			++noD3D8;
			continue;
		}
		++compared;
		std::vector<RegisterBinding> fromD3D8, fromElements;
		if (!decodeD3D8Declaration( &prog.d3d8[0], prog.d3d8.size(), fromD3D8, error ))
		{
			printf( "FAIL %s: the D3D8 declaration: %s\n", name, error.c_str() );
			++failures;
			continue;
		}
		if (!bindingsFromElements( prog.elements.empty() ? NULL : &prog.elements[0], (int)prog.elements.size(), fromElements, error ))
		{
			printf( "FAIL %s: the D3D9 elements: %s\n", name, error.c_str() );
			++failures;
			continue;
		}
		std::string problems = compareBindings( fromD3D8, fromElements );
		for (size_t k = 0; k < fromD3D8.size(); ++k)
		{
			const ProgFile::Stream *s = NULL;
			for (size_t j = 0; j < prog.streams.size(); ++j)
				if (prog.streams[j].stream == fromD3D8[k].stream)
					s = &prog.streams[j];
			char line[ 128 ];
			if (s == NULL)
				snprintf( line, sizeof( line ), "v%u reads stream %u, which the capture did not record\n",
					(unsigned)fromD3D8[k].reg, (unsigned)fromD3D8[k].stream );
			else if (extent( fromD3D8[k] ) > s->stride)
				snprintf( line, sizeof( line ), "v%u reaches byte %u, past stream %u's stride %u\n", (unsigned)fromD3D8[k].reg,
					extent( fromD3D8[k] ), (unsigned)s->stream, (unsigned)s->stride );
			else
				continue;
			problems += line;
		}
		std::string layout;
		for (size_t k = 0; k < fromD3D8.size(); ++k)
		{
			char one[ 48 ];
			snprintf( one, sizeof( one ), "%sv%u@%u:%u", k ? " " : "", (unsigned)fromD3D8[k].reg, (unsigned)fromD3D8[k].offset,
				(unsigned)fromD3D8[k].type );
			layout += one;
		}
		// The armed control: the first REG token's type moved on by one must be seen
		std::vector<uint32_t> mutated = prog.d3d8;
		for (size_t k = 0; k < mutated.size(); ++k)
			if ((mutated[k] >> D3D8::TOKEN_TYPE_SHIFT) == D3D8::TOKEN_STREAMDATA && !(mutated[k] & D3D8::DATA_LOAD_SKIP_BIT))
			{
				const uint32_t type = (mutated[k] & D3D8::DATA_TYPE_MASK) >> D3D8::DATA_TYPE_SHIFT;
				mutated[k] = (mutated[k] & ~D3D8::DATA_TYPE_MASK) | (((type + 1) % 8) << D3D8::DATA_TYPE_SHIFT);
				break;
			}
		std::vector<RegisterBinding> fromMutated;
		const bool caught = !decodeD3D8Declaration( &mutated[0], mutated.size(), fromMutated, error )
			|| !compareBindings( fromMutated, fromElements ).empty();
		controlsCaught += caught ? 1 : 0;
		const uint32_t stride = prog.streams.empty() ? 0 : prog.streams[0].stride;
		if (problems.empty() && caught)
		{
			printf( "ok   %s: %u D3D8 bindings = %u D3D9 elements [%s], stride %u; control caught\n", name,
				(unsigned)fromD3D8.size(), (unsigned)prog.elements.size(), layout.c_str(), (unsigned)stride );
			++agreed;
		}
		else
		{
			printf( "FAIL %s: [%s], stride %u\n%s%s", name, layout.c_str(), (unsigned)stride, problems.c_str(),
				caught ? "" : "  the control (a mutated type) was NOT seen\n" );
			++failures;
		}
	}
	printf( "%u .prog files: %d compared (%d agree), %d without a D3D8 declaration, %d version 1, %d failures; "
		"%d of %d controls caught\n", (unsigned)files.size(), compared, agreed, noD3D8, versionOne, failures, controlsCaught, compared );
	if (failures > 0)
		return 1;
	return compared > 0 ? 0 : 77;
}
