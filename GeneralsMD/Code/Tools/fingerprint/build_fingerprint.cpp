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
 * The build fingerprint (N1, decision 5): one 32-bit value over the content of every tracked source file
 * under GeneralsMD/Code, which GlobalData.cpp folds into m_exeCRC in place of the executable's own bytes,
 * so that two builds of the same source agree whatever compiled them.
 *
 * "Tracked" is the committed list GeneralsMD/Code/BuildFingerprint.manifest: paths relative to
 * GeneralsMD/Code, '/' separators, sorted bytewise.  It needs no git at build time (a release tarball has
 * none); Tools/fingerprint-manifest.sh writes it from `git ls-files`, and ctest's
 * build_fingerprint_manifest checks it still matches wherever git is at hand.
 *
 * The value: CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320, initial and final XOR 0xFFFFFFFF, as
 * zlib's crc32) over, for each listed file in order, its path, a NUL, its content with every run of CRs
 * directly before an LF removed (a Windows checkout with autocrlf and a Mac one then agree, however the
 * conversion added CRs), and a NUL.  A missing listed file is an
 * error: that is not the same source.
 *
 *   build_fingerprint <GeneralsMD/Code> <manifest> <header>   write the header if its value changed
 *   build_fingerprint --print <GeneralsMD/Code> <manifest>    print the value and the file count
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

uint32_t table[ 256 ];

void makeTable()
{
	for (uint32_t i = 0; i < 256; ++i)
	{
		uint32_t c = i;
		for (int k = 0; k < 8; ++k)
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		table[i] = c;
	}
}

uint32_t update( uint32_t crc, const unsigned char *p, size_t n )
{
	for (size_t i = 0; i < n; ++i)
		crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return crc;
}

bool readFile( const std::string &path, std::vector<unsigned char> &bytes )
{
	FILE *f = fopen( path.c_str(), "rb" );
	if (f == NULL)
		return false;
	bytes.clear();
	unsigned char buffer[ 65536 ];
	size_t got;
	while ((got = fread( buffer, 1, sizeof( buffer ), f )) > 0)
		bytes.insert( bytes.end(), buffer, buffer + got );
	fclose( f );
	return true;
}

/// The fingerprint; FALSE with `error` for a missing file or an unreadable manifest
bool fingerprint( const std::string &root, const std::string &manifest, uint32_t &value, unsigned &files, std::string &error )
{
	std::vector<unsigned char> list;
	if (!readFile( manifest, list ))
	{
		error = "cannot read " + manifest;
		return false;
	}
	makeTable();
	uint32_t crc = 0xFFFFFFFFu;
	files = 0;
	std::string line;
	std::vector<unsigned char> content, normal;
	for (size_t i = 0; i <= list.size(); ++i)
	{
		if (i < list.size() && list[i] != '\n')
		{
			if (list[i] != '\r')
				line += (char)list[i];
			continue;
		}
		if (line.empty())
			continue;
		if (!readFile( root + "/" + line, content ))
		{
			error = "a listed file is missing: " + line;
			return false;
		}
		// Every run of CRs directly before an LF goes: idempotent, so it absorbs however a checkout's
		// conversion added CRs (the tree holds some \r\r\n, which one pass of CRLF-to-LF would leave as CRLF)
		normal.clear();
		for (size_t k = 0; k < content.size(); ++k)
		{
			if (content[k] == '\r')
			{
				size_t run = k;
				while (run < content.size() && content[run] == '\r')
					++run;
				if (run < content.size() && content[run] == '\n')
				{
					k = run - 1;		// drop the CRs; the LF comes next
					continue;
				}
			}
			normal.push_back( content[k] );
		}
		static const unsigned char nul = 0;
		crc = update( crc, (const unsigned char *)line.data(), line.size() );
		crc = update( crc, &nul, 1 );
		crc = update( crc, normal.data(), normal.size() );
		crc = update( crc, &nul, 1 );
		++files;
		line.clear();
	}
	value = crc ^ 0xFFFFFFFFu;
	return true;
}

}	// namespace

int main( int argc, char **argv )
{
	const bool print = argc == 4 && strcmp( argv[1], "--print" ) == 0;
	if (!print && argc != 4)
	{
		fprintf( stderr, "usage: build_fingerprint <GeneralsMD/Code> <manifest> <header>\n"
			"       build_fingerprint --print <GeneralsMD/Code> <manifest>\n" );
		return 2;
	}
	uint32_t value;
	unsigned files;
	std::string error;
	if (!fingerprint( argv[print ? 2 : 1], argv[print ? 3 : 2], value, files, error ))
	{
		fprintf( stderr, "build_fingerprint: %s\n", error.c_str() );
		return 1;
	}
	if (print)
	{
		printf( "0x%08X %u\n", value, files );
		return 0;
	}
	char header[ 512 ];
	snprintf( header, sizeof( header ),
		"// Generated by Tools/fingerprint/build_fingerprint.cpp at build time: do not edit.\n"
		"// CRC-32 over the %u files BuildFingerprint.manifest lists (N1, decision 5).\n"
		"#pragma once\n"
		"#define ZH_BUILD_FINGERPRINT 0x%08Xu\n"
		"#define ZH_BUILD_FINGERPRINT_FILES %uu\n", files, value, files );
	// Written only when it changed, so an unchanged value recompiles nothing
	std::vector<unsigned char> old;
	if (readFile( argv[3], old ) && std::string( old.begin(), old.end() ) == header)
		return 0;
	FILE *f = fopen( argv[3], "wb" );
	if (f == NULL || fputs( header, f ) < 0 || fclose( f ) != 0)
	{
		fprintf( stderr, "build_fingerprint: cannot write %s\n", argv[3] );
		return 1;
	}
	return 0;
}
