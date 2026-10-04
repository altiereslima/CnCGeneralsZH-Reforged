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

/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/systimer.cpp                           $*
 *                                                                                             *
 *                      $Author:: Steve_t                                                     $*
 *                                                                                             *
 *                     $Modtime:: 12/09/01 6:11p                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "systimer.h"
#include "Lib/Clock.h"

SysTimeClass SystemTime;


/***********************************************************************************************
 * SysTimeClass::SysTimeClass -- default constructor, sets resolution                          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/04/2003 : Created by Mark Wilczynski (EAP)                                             *
 *=============================================================================================*/
SysTimeClass::SysTimeClass(void)
{
	//tell windows we need single ms precision.
	Clock_Begin_Fine_Resolution();
}

/***********************************************************************************************
 * SysTimeClass::~SysTimeClass -- default destructor, resets resolution                         *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/04/2003 : Created by Mark Wilczynski (EAP)                                             *
 *=============================================================================================*/
SysTimeClass::~SysTimeClass(void)
{
	//tell windows we need single ms precision.
	Clock_End_Fine_Resolution();
}

/***********************************************************************************************
 * SysTimeClass::Reset -- Reset class to good state                                            *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/9/2001 5:51PM ST : Created                                                             *
 *=============================================================================================*/
void SysTimeClass::Reset(void)
{
	StartTime = Clock_Milliseconds();
	WrapAdd = 0 - StartTime;
}



/***********************************************************************************************
 * SysTimeClass::Is_Getting_Late -- Are we running out of timer time?                          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/9/2001 6:04PM ST : Created                                                             *
 *=============================================================================================*/
bool SysTimeClass::Is_Getting_Late(void)
{
	/*
	** Even though the timers are all unsigned so we have a max time of 0xffffffff the game casts it to int in various places
	** so it's safer to assume a signed max value.
	*/
	if (Get() > 0x6fffffff) {
		return(true);
	}
	return(false);
}

























