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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: INICommandSet.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Command sets are a configurable set of CommandButtons, we will use the sets as
//				 part of the context sensitive user interface
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "Common/CommandStore.h"
#include "Common/StackDump.h"	// g_LastErrorDump

//-------------------------------------------------------------------------------------------------
/** Parse a command set */
//-------------------------------------------------------------------------------------------------
void INI::parseCommandSetDefinition( INI *ini )
{
	// as a CommandButton block: one a mod put among its objects comes before the store exists
	if( TheCommandStore == NULL )
	{
		g_LastErrorDump.format( "A CommandSet block in '%s' is read before command sets exist; it belongs in Data\\INI\\CommandSet.ini\n",
			ini->getFilename().str() );
		throw INI_INVALID_DATA;
	}
	CommandStore::parseCommandSetDefinition(ini);
}  // end parseCommandSetDefinition
