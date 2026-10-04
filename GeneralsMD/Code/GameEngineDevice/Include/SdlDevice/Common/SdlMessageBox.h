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

// SdlMessageBox: MessageBoxWrapper's box off Windows, as SDL_ShowMessageBox (C2).  SdlGameEngine sets
// Common/MessageBoxFlags.h's TheMessageBoxHook while it has a window.  The box is shown only on the main
// thread, which is the thread that owns the window, as Windows' getThreadHWND only answers there;
// elsewhere the hook declines and MessageBoxWrapper takes Windows' no-window path.

#pragma once

#ifndef __SDLMESSAGEBOX_H
#define __SDLMESSAGEBOX_H

#include <SDL3/SDL_messagebox.h>

struct SDL_Window;

/** The SDL box for MessageBoxWrapper's MSGBOX_ flags: its kind, and its buttons in Windows' order, each
	* answering the MSGBOX_ID_ Windows would.  Built without showing anything, so it can be tested. */
struct SdlMessageBoxLayout
{
	SDL_MessageBoxFlags flags;
	int numButtons;
	SDL_MessageBoxButtonData buttons[3];
};
SdlMessageBoxLayout sdlMessageBoxLayout( unsigned int msgboxFlags );

/** Makes TheMessageBoxHook show boxes over this window; NULL takes it down. */
void setSdlMessageBoxOwner( SDL_Window *owner );

#endif // __SDLMESSAGEBOX_H
