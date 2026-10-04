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

// strdup as the Windows build computes it, for the whole tree: one spelling.
//
// On Windows strdup is the UCRT's _strdup, which returns NULL for a NULL argument (as the UCRT's source reads;
// not measured by this project).  Darwin's and glibc's strdup read through the pointer instead.  The tree
// was written where copying a NULL name gave a NULL name, and at least one path depends on it: a particle
// emitter's user string is NULL until set, and the fog of war clones every emitter it ghosts, which crashed
// the Mac and Linux within seconds of a match on Seaside Mutiny (port defect 31).
//
// Windows calls _strdup itself, exactly the function strdup already was there, so nothing changes on
// Windows by construction.  Elsewhere a NULL argument gives NULL, and anything else is strdup's copy.

#pragma once

#ifndef STRDUPASWINDOWS_H
#define STRDUPASWINDOWS_H

#include <stdlib.h>
#include <string.h>

inline char *strdupAsWindows(const char *string)
{
#if defined(_WIN32)
	return _strdup(string);
#else
	return string != NULL ? strdup(string) : NULL;
#endif
}

#endif // STRDUPASWINDOWS_H
