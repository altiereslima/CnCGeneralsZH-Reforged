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

// FILE: WellKnownKeys.cpp ////////////////////////////////////////////////////////////////////////
// Desc:   The one definition of every well-known Dict key (TheKey_teamName and the rest).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* WellKnownKeys.h declares each key and, in the one file that defines INSTANTIATE_WELL_KNOWN_KEYS
	 first, defines it.  That file was GameEngineDevice's WorldHeightMap.cpp, so the keys the
	 simulation reads - team, player, object and waypoint properties - lived in the renderer's
	 library (B6).  It is this file now, and WorldHeightMap.cpp only declares them. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define INSTANTIATE_WELL_KNOWN_KEYS
#include "Common/WellKnownKeys.h"

// The keys must be constant-initialized (see WellKnownKeys.h and StaticNameKey), and this is the check
// every compiler makes, MSVC included: a constexpr StaticNameKey compiles only if its constructor can
// run at compile time on a string literal, which is exactly what each DEFINE_KEY above asks of it.
static constexpr StaticNameKey s_constantInitProbe( "constantInitProbe" );

