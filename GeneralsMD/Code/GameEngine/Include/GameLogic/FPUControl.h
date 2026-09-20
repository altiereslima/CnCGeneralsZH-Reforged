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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: FPUControl.h /////////////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell, June 2002
// Desc:	 Routines for controlling the FPU state
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __FPUCONTROL_H__
#define __FPUCONTROL_H__

#include "Lib/BaseType.h"
#include <float.h>

/** The control word fields setFPMode owns.  There is no x87 precision field to pin any more, and
	* the CRT fails fast on any _controlfp call whose mask names _MCW_PC, so the rounding mode is
	* the whole of it.  SSE arithmetic rounds at the declared width, which is what the 24-bit
	* precision setting bought the x87. */
#define FP_MODE_FIELDS ( _MCW_RC )

/**
  * setFPMode sets the FPU internal precision and rounding mode.  As DirectX is not guaranteed to
	* leave the FPU in a good state, we must call this at the start of GameLogic::update() and
	* anywhere that touches DirectX inside GameLogic loops (LoadScreen).
	*/
void setFPMode( void );

/** The precision and rounding fields of the FPU control word, as they are right now. */
UnsignedInt getFPMode( void );

/** What getFPMode() must read back after setFPMode(): 24-bit precision, round to nearest. */
UnsignedInt expectedFPMode( void );

#endif // __FPUCONTROL_H__
