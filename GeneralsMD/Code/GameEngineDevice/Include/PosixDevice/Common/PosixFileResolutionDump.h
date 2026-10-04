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

// PosixFileResolutionDump.h: "-dumpFileResolution <file>", for P1's test_packaging_resolution.

#pragma once

#include "Lib/BaseType.h"

/** After GameEngine::init: every path the game can open (the loose files under its roots and every
	* file in the mounted archives), and for each where it resolves - "loose" with its size and the hash
	* of its bytes, or the archive that won it and the member's size - then the INI and EXE CRCs.  One
	* line a path, in the engine's case-insensitive order.  Two layouts that give the game the same
	* files give identical dumps: that is test_packaging_resolution's comparison (P1 step 3). */
Bool PosixDumpFileResolution( const char *dumpPath );
