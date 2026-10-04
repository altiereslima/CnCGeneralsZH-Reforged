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

// FILE: ApplicationWindowTitle.h /////////////////////////////////////////////////////////////////
// Desc:   Off Windows, how GameText names the game's window without knowing what the window is.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* GameText.cpp names the window once it has the game's name from the .csf.  On Windows it sets
	 ApplicationHWnd's text.  Elsewhere the window belongs to the platform layer (C2's SdlGameEngine,
	 over SDL3), which gameengine does not link, so the layer that owns a window sets this hook and
	 GameText calls it.  NULL while there is no window: a headless run, or a test. */

#pragma once

#if !defined(_WIN32)
/** Names the game's window; the title is UTF-8. */
typedef void (*ApplicationWindowTitleHook)( const char *utf8Title );
extern ApplicationWindowTitleHook TheApplicationWindowTitleHook;
#endif
