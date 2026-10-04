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

// FILE: MessageBoxFlags.h ////////////////////////////////////////////////////////////////////////
// Desc:   The flags MessageBoxWrapper takes and the answers it gives, in the engine's own names.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* MessageBoxWrapper (Common/System/Debug.cpp) is the engine's message box.  Its flags and answers
	 were Win32's MB_* and ID* names, which exist only on Windows; the port does not define Win32's
	 names elsewhere (as WCHAR is left undefined, and D3's generators use FF_* rather than D3D's).  So
	 these are the engine's names, with Win32's numbers: on Windows each is checked equal to the SDK's
	 below, so what reaches ::MessageBox is exactly what did; elsewhere they are the same numbers, for
	 the no-window path and, later, C2's SDL_ShowMessageBox.

	 Only the ones the engine passes or tests are here.  Calls straight to ::MessageBox, which are
	 Windows-only, keep the SDK's names. */

#pragma once

#if defined(_WIN32)
#include <windows.h>
#endif

enum MessageBoxFlag : unsigned int
{
	MSGBOX_OK									= 0x00000000,
	MSGBOX_ABORTRETRYIGNORE		= 0x00000002,
	MSGBOX_YESNO							= 0x00000004,
	MSGBOX_ICONERROR					= 0x00000010,
	MSGBOX_ICONWARNING				= 0x00000030,
	MSGBOX_DEFBUTTON3					= 0x00000200,
	MSGBOX_TASKMODAL					= 0x00002000,
};

enum MessageBoxAnswer
{
	MSGBOX_ID_OK							= 1,
	MSGBOX_ID_ABORT						= 3,
	MSGBOX_ID_RETRY						= 4,
	MSGBOX_ID_IGNORE					= 5,
	MSGBOX_ID_YES							= 6,
	MSGBOX_ID_NO							= 7,
};

#if defined(_WIN32)
static_assert( MSGBOX_OK == MB_OK, "MSGBOX_OK must be MB_OK" );
static_assert( MSGBOX_ABORTRETRYIGNORE == MB_ABORTRETRYIGNORE, "MSGBOX_ABORTRETRYIGNORE must be MB_ABORTRETRYIGNORE" );
static_assert( MSGBOX_YESNO == MB_YESNO, "MSGBOX_YESNO must be MB_YESNO" );
static_assert( MSGBOX_ICONERROR == MB_ICONERROR, "MSGBOX_ICONERROR must be MB_ICONERROR" );
static_assert( MSGBOX_ICONWARNING == MB_ICONWARNING, "MSGBOX_ICONWARNING must be MB_ICONWARNING" );
static_assert( MSGBOX_DEFBUTTON3 == MB_DEFBUTTON3, "MSGBOX_DEFBUTTON3 must be MB_DEFBUTTON3" );
static_assert( MSGBOX_TASKMODAL == MB_TASKMODAL, "MSGBOX_TASKMODAL must be MB_TASKMODAL" );
static_assert( MSGBOX_ID_OK == IDOK, "MSGBOX_ID_OK must be IDOK" );
static_assert( MSGBOX_ID_ABORT == IDABORT, "MSGBOX_ID_ABORT must be IDABORT" );
static_assert( MSGBOX_ID_RETRY == IDRETRY, "MSGBOX_ID_RETRY must be IDRETRY" );
static_assert( MSGBOX_ID_IGNORE == IDIGNORE, "MSGBOX_ID_IGNORE must be IDIGNORE" );
static_assert( MSGBOX_ID_YES == IDYES, "MSGBOX_ID_YES must be IDYES" );
static_assert( MSGBOX_ID_NO == IDNO, "MSGBOX_ID_NO must be IDNO" );
#endif

/** The engine's message box: text, caption, and MSGBOX_ flags; answers an MSGBOX_ID_.  See Debug.cpp. */
int MessageBoxWrapper( const char *lpText, const char *lpCaption, unsigned int uType );

#if !defined(_WIN32)
/** Off Windows the box is the platform layer's (C2's SdlGameEngine, SDL_ShowMessageBox), which gameengine
	* does not link: it sets this while it has a window.  Answers an MSGBOX_ID_, or -1 when it cannot show
	* one (not the main thread), and MessageBoxWrapper then takes Windows' no-window path. */
typedef int (*MessageBoxHook)( const char *text, const char *caption, unsigned int flags );
inline MessageBoxHook TheMessageBoxHook = nullptr;
#endif
