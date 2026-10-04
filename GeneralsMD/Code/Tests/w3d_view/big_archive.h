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
 * A read-only .big archive: the game's directory format, read the way
 * GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp's openArchiveFile reads it -
 * "BIGF", then the entry count big-endian at offset 8, then from 0x10 one big-endian offset and size
 * per entry followed by its NUL-terminated name.
 *
 * Why not that class itself: it is an ArchiveFileSystem, which needs gameengine's File, AsciiString
 * and subsystem machinery, and gameengine does not build off Windows yet (B5).  This is the directory
 * format and nothing else, for a tool that must not wait for B5.
 */
#pragma once

#include <stdio.h>

#include <string>
#include <vector>

class BigArchive
{
public:
	BigArchive() : m_file(NULL) {}
	~BigArchive();

	bool open(const std::string &path);
	// Looks a name up the way the game's file system does: case-insensitively, '/' or '\\'.
	bool read(const std::string &name, std::vector<unsigned char> *out) const;
	bool contains(const std::string &name) const;
	const std::string &path() const { return m_path; }

private:
	struct Entry
	{
		std::string key;	// lower case, backslashes
		unsigned offset;
		unsigned size;
	};

	const Entry *find(const std::string &name) const;

	std::string m_path;
	FILE *m_file;
	std::vector<Entry> m_entries;	// sorted by key
};

// Several archives searched in order: the first that has a name wins, as ZH's archives shadow
// Generals' when the game loads both.
class ArchiveSet
{
public:
	~ArchiveSet();
	// Opens every existing path; returns how many opened.
	int open(const std::vector<std::string> &paths);
	bool read(const std::string &name, std::vector<unsigned char> *out, std::string *from = NULL) const;

private:
	std::vector<BigArchive *> m_archives;
};
