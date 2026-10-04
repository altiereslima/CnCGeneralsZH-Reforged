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

// PosixFileResolutionDump.cpp: see PosixFileResolutionDump.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "PosixDevice/Common/PosixFileResolutionDump.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/file.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "zhio.h"

namespace {

// FNV-1a 64 over a file's bytes, through the engine's own file system (what the game reads)
unsigned long long hashOf( const char *path, Int *sizeOut )
{
	unsigned long long hash = 1469598103934665603ULL;
	*sizeOut = -1;
	File *file = TheLocalFileSystem->openFile( path, File::READ | File::BINARY );
	if (file == NULL)
		return 0;
	*sizeOut = file->size();
	unsigned char block[ 65536 ];
	Int got;
	while ((got = file->read( block, sizeof( block ) )) > 0)
		for (Int i = 0; i < got; ++i)
		{
			hash ^= block[i];
			hash *= 1099511628211ULL;
		}
	file->close();
	return hash;
}

Bool isArchiveName( const AsciiString &path )
{
	const Int length = path.getLength();
	return length > 4 && strcasecmp( path.str() + length - 4, ".big" ) == 0;
}

}  // namespace

Bool PosixDumpFileResolution( const char *dumpPath )
{
	if (TheLocalFileSystem == NULL || TheArchiveFileSystem == NULL || TheGlobalData == NULL)
		return FALSE;

	// Every path the game can open: the loose files under the roots (the install and, through the
	// union, the overlays) and every file in the mounted archives, one entry a name without case.
	FilenameList paths;
	TheLocalFileSystem->getFileListInDirectory( AsciiString( "" ), AsciiString( "" ), AsciiString( "*" ), paths, TRUE );
	FilenameList archived;
	TheArchiveFileSystem->getFileListInDirectory( AsciiString( "" ), AsciiString( "" ), AsciiString( "*" ), archived, TRUE );
	paths.insert( archived.begin(), archived.end() );

	FILE *out = zh_fopen( dumpPath, "wb" );
	if (out == NULL)
		return FALSE;
	for (FilenameListIter it = paths.begin(); it != paths.end(); ++it)
	{
		const AsciiString &path = *it;
		if (TheLocalFileSystem->doesFileExist( path.str() ))
		{
			// A loose file wins over every archive (FileSystem::openFile).  Its bytes are hashed, except
			// an archive's own: both layouts link the same .big files, and hashing 5 GB proves nothing
			// the name and size do not.
			Int size;
			if (isArchiveName( path ))
			{
				File *file = TheLocalFileSystem->openFile( path.str(), File::READ | File::BINARY );
				size = file != NULL ? file->size() : -1;
				if (file != NULL)
					file->close();
				fprintf( out, "%s\tloose\t%d\n", path.str(), size );
			}
			else
			{
				const unsigned long long hash = hashOf( path.str(), &size );
				fprintf( out, "%s\tloose\t%d\t%016llx\n", path.str(), size, hash );
			}
			continue;
		}
		// Otherwise the archive that won the path, and the member's size: which archive wins is the
		// question, and its bytes are that archive's in both layouts.
		const AsciiString archive = TheArchiveFileSystem->getArchiveFilenameForFile( path );
		File *file = TheArchiveFileSystem->openFile( path.str() );
		const Int size = file != NULL ? file->size() : -1;
		if (file != NULL)
			file->close();
		fprintf( out, "%s\tarchive %s\t%d\n", path.str(), archive.str(), size );
	}
	fprintf( out, "INI CRC 0x%08X\n", TheGlobalData->m_iniCRC );
	fprintf( out, "EXE CRC 0x%08X\n", TheGlobalData->m_exeCRC );
	fclose( out );
	return TRUE;
}
