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

// PosixLocalFileSystem: the engine's real files on a POSIX system (C1), doing what
// Win32LocalFileSystem does on Windows.  Names are spelled the Windows way throughout, and resolved
// against the disk only where they reach the operating system (WWLib's posixpath.h, decision D1), so
// what the engine lists, compares and hashes is what Windows gives it.

#pragma once

#ifndef __POSIXLOCALFILESYSTEM_H
#define __POSIXLOCALFILESYSTEM_H

#include "Common/LocalFileSystem.h"

class PosixLocalFileSystem : public LocalFileSystem
{
public:
	PosixLocalFileSystem();
	virtual ~PosixLocalFileSystem();

	virtual void init();
	virtual void reset();
	virtual void update();

	virtual File * openFile(const Char *filename, Int access = 0);
	virtual Bool doesFileExist(const Char *filename) const;

	virtual void getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const;
	virtual Bool getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const;

	virtual Bool createDirectory(AsciiString directory);

	virtual Bool copyFile(const Char *from, const Char *to, Bool failIfExists);
	virtual Bool deleteFile(const Char *path);
	virtual Bool moveFileReplacing(const Char *from, const Char *to);
	virtual void getFilesInDirectory(const AsciiString& directory, const AsciiString& searchName, std::vector<AsciiString> &names) const;
	virtual AsciiString getCurrentDirectory() const;
};

#endif // __POSIXLOCALFILESYSTEM_H
