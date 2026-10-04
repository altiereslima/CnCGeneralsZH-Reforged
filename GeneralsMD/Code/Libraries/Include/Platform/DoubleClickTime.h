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

// How far apart two clicks may be and still be a double click, in milliseconds.
//
// A platform service, so it lives behind this name.  Windows returns the player's own setting,
// GetDoubleClickTime(), exactly as its callers always read it.  macOS returns the player's own setting
// too, NSEvent's doubleClickInterval (C3).  Linux has 500, Windows' own default, until its milestone.
//
// On macOS the Objective-C runtime is found at run time rather than linked: every program that links
// gameengine includes this, and only the game (where SDL has brought in AppKit) has an NSEvent to ask.
// A program without one gets 500, as does a setting outside 100 ms to 5 s.
//
// Callers: GlobalData's m_doubleClickTimeMS, and GadgetListBox's static, which is initialised before
// GlobalData exists and so cannot read that field.

#pragma once

#if defined(_WIN32)
#include <windows.h>
inline unsigned int systemDoubleClickTimeMS( void ) { return ::GetDoubleClickTime(); }
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <objc/objc.h>
inline unsigned int systemDoubleClickTimeMS( void )
{
	typedef void *(*GetClass)( const char * );
	typedef SEL (*RegisterName)( const char * );
	typedef double (*SendDouble)( void *, SEL );	// arm64 and x86_64 both return a double in a register
	GetClass getClass = (GetClass)::dlsym( RTLD_DEFAULT, "objc_getClass" );
	RegisterName registerName = (RegisterName)::dlsym( RTLD_DEFAULT, "sel_registerName" );
	SendDouble send = (SendDouble)::dlsym( RTLD_DEFAULT, "objc_msgSend" );
	void *nsEvent = (getClass != 0 && registerName != 0 && send != 0) ? getClass( "NSEvent" ) : 0;
	if (nsEvent == 0)
		return 500;
	const double seconds = send( nsEvent, registerName( "doubleClickInterval" ) );
	if (!(seconds >= 0.1 && seconds <= 5.0))
		return 500;
	return (unsigned int)( seconds * 1000.0 + 0.5 );
}
#else
inline unsigned int systemDoubleClickTimeMS( void ) { return 500; }
#endif
