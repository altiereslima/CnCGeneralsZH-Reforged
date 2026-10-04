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

// FILE: PosixCDManager.cpp ///////////////////////////////////////////////////////////////////////
// Desc:   CreateCDManager off Windows: a CD manager that finds no drives.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* Win32CDManager asks GetDriveType of every drive letter and adds each CD-ROM it finds.  There are no
	 drive letters here, and the game is played from an installed copy, so this finds none: the same
	 answer a Windows PC without an optical drive gets, which is every Steam install.  FileSystem asks
	 it whether the music is on a disc (no), and GameEngine updates it every frame (CDManager::update
	 refreshes an empty list).  createDrive is only reached through newDrive, which only Win32CDManager
	 calls, so reaching it here is a bug. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/CDManager.h"

class PosixCDManager : public CDManager
{
	public:
		virtual void init( void ) { CDManager::init(); destroyAllDrives(); }

	protected:
		virtual CDDriveInterface* createDrive( void )
		{
			DEBUG_CRASH(("PosixCDManager::createDrive: there are no CD drives to create off Windows"));
			return NULL;
		}
};

CDManagerInterface* CreateCDManager( void )
{
	return NEW PosixCDManager;
}
