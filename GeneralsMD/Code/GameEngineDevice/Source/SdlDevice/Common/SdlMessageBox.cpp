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

// SdlMessageBox.cpp: see SdlMessageBox.h.

#include "PreRTS.h"

#include "Common/MessageBoxFlags.h"
#include "SdlDevice/Common/SdlMessageBox.h"

#include <SDL3/SDL.h>

static SDL_Window *s_owner = NULL;

// MB_TYPEMASK, MB_ICONMASK and MB_DEFMASK: the fields of the flags, as Windows reads them.
static const unsigned int BUTTONS_FIELD = 0x0000000F;
static const unsigned int ICON_FIELD = 0x000000F0;
static const unsigned int DEFAULT_BUTTON_FIELD = 0x00000F00;

SdlMessageBoxLayout sdlMessageBoxLayout( unsigned int msgboxFlags )
{
	SdlMessageBoxLayout layout;
	SDL_zero( layout );

	switch (msgboxFlags & ICON_FIELD)
	{
		case MSGBOX_ICONERROR:		layout.flags = SDL_MESSAGEBOX_ERROR; break;
		case MSGBOX_ICONWARNING:	layout.flags = SDL_MESSAGEBOX_WARNING; break;
		default:									layout.flags = SDL_MESSAGEBOX_INFORMATION; break;
	}
	layout.flags |= SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;		// Windows' order

	switch (msgboxFlags & BUTTONS_FIELD)
	{
		case MSGBOX_ABORTRETRYIGNORE:
			layout.numButtons = 3;
			layout.buttons[0].buttonID = MSGBOX_ID_ABORT;		layout.buttons[0].text = "Abort";
			layout.buttons[1].buttonID = MSGBOX_ID_RETRY;		layout.buttons[1].text = "Retry";
			layout.buttons[2].buttonID = MSGBOX_ID_IGNORE;	layout.buttons[2].text = "Ignore";
			break;
		case MSGBOX_YESNO:
			layout.numButtons = 2;
			layout.buttons[0].buttonID = MSGBOX_ID_YES;			layout.buttons[0].text = "Yes";
			layout.buttons[1].buttonID = MSGBOX_ID_NO;			layout.buttons[1].text = "No";
			break;
		default:
			layout.numButtons = 1;
			layout.buttons[0].buttonID = MSGBOX_ID_OK;			layout.buttons[0].text = "OK";
			break;
	}

	// Return presses the default button: the first, or the third under MSGBOX_DEFBUTTON3 when there is one.
	int defaultButton = 0;
	if ((msgboxFlags & DEFAULT_BUTTON_FIELD) == MSGBOX_DEFBUTTON3 && layout.numButtons == 3)
		defaultButton = 2;
	layout.buttons[defaultButton].flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
	return layout;
}

static int showSdlMessageBox( const char *text, const char *caption, unsigned int flags )
{
	if (s_owner == NULL || !SDL_IsMainThread())
		return -1;

	const SdlMessageBoxLayout layout = sdlMessageBoxLayout( flags );
	SDL_MessageBoxData data;
	SDL_zero( data );
	data.flags = layout.flags;
	data.window = s_owner;		// modal to the game's window, as MSGBOX_TASKMODAL asks
	data.title = caption != NULL ? caption : "";
	data.message = text != NULL ? text : "";
	data.numbuttons = layout.numButtons;
	data.buttons = layout.buttons;

	int answer = -1;
	if (!SDL_ShowMessageBox( &data, &answer ))
		return -1;
	return answer;		// -1 if the box was closed without a button: MessageBoxWrapper's default then
}

void setSdlMessageBoxOwner( SDL_Window *owner )
{
	s_owner = owner;
	TheMessageBoxHook = (owner != NULL) ? showSdlMessageBox : NULL;
}
