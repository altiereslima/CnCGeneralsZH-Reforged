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

// PosixLocalFile: one open file on a POSIX system (C1).  All of it is LocalFile's, which resolves the
// engine's spelling of the name and reads TEXT files as Windows does; this names the pool.

#pragma once

#ifndef __POSIXLOCALFILE_H
#define __POSIXLOCALFILE_H

#include "Common/LocalFile.h"

class PosixLocalFile : public LocalFile
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(PosixLocalFile, "PosixLocalFile")
public:
	PosixLocalFile();
};

#endif // __POSIXLOCALFILE_H
