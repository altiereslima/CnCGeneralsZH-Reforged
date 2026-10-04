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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

/////// LocalFileSystem.h ////////////////////////////////
// Bryan Cleveland, August 2002
//////////////////////////////////////////////////////////

#pragma once

#ifndef __LOCALFILESYSTEM_H
#define __LOCALFILESYSTEM_H

#include "Common/SubsystemInterface.h"
#include "FileSystem.h" // for typedefs, etc.

#include <vector>

class File;

class LocalFileSystem : public SubsystemInterface
{
public:
	virtual ~LocalFileSystem() {}

	virtual void init() = 0;
	virtual void reset() = 0;
	virtual void update() = 0;

	virtual File * openFile(const Char *filename, Int access = 0) = 0;
	virtual Bool doesFileExist(const Char *filename) const = 0;
	virtual void getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const = 0; ///< search the given directory for files matching the searchName (egs. *.ini, *.rep).  Possibly search subdirectories.
	virtual Bool getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const = 0; ///< see FileSystem.h
	virtual Bool createDirectory(AsciiString directory) = 0; ///< see FileSystem.h

	// The operations engine code used to make on the file system directly, behind the platform
	// (C1, decision D3).  Each is what the Windows call it replaces does; Win32LocalFileSystem makes
	// exactly that call.  Paths are spelled as the engine spells them.

	/// CopyFile(from, to, failIfExists): the file's bytes and last write time.  TRUE on success.
	virtual Bool copyFile(const Char *from, const Char *to, Bool failIfExists) = 0;
	/// DeleteFile(path): one file, never a directory.  TRUE on success.
	virtual Bool deleteFile(const Char *path) = 0;
	/// MoveFileEx(from, to, MOVEFILE_REPLACE_EXISTING): renamed over any file already at `to`, on the
	/// same volume.  TRUE on success.
	virtual Bool moveFileReplacing(const Char *from, const Char *to) = 0;
	/// The files - not the directories - in `directory` whose names match `searchName` (FindFirstFile
	/// patterns, "*" for all), as bare names, in the order the file system lists them.  Replaces
	/// changing into a directory to list "*" and changing back; the current directory is untouched.
	virtual void getFilesInDirectory(const AsciiString& directory, const AsciiString& searchName, std::vector<AsciiString> &names) const = 0;
	/// GetCurrentDirectory: for messages that name where the game was started.
	virtual AsciiString getCurrentDirectory() const = 0;

protected:
};

extern LocalFileSystem *TheLocalFileSystem;

#endif // __LOCALFILESYSTEM_H