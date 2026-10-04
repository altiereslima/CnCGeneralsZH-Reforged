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

// SdlMouse.h: the mouse off Windows, from SDL3's mouse events (C3, decision 3).
//
// Win32Mouse's counterpart, and meant to be read beside it: the same ring of events the window
// procedure fills (here SdlInput_dispatch), the same translation into MouseIO, the same update() that
// asks where the pointer really is until an event says, and the same confinement of the pointer to a
// window that covers the screen.  The differences, each on purpose:
//   - Positions are in the game's pixels: SDL's are the window's points, which differ on a Retina
//     screen and whenever the window is not the game's resolution.
//   - A double click is SDL's click count - the system's own interval and distance - with an even
//     count reported as MBS_DoubleClick and an odd one as a press, which is the sequence Windows sends
//     (down, up, double, up; a third click is a down again).
//   - The wheel is SDL's y at 120 a notch, fractions carried to the next event, so a trackpad's small
//     steps arrive the way a precision touchpad's small WM_MOUSEWHEEL deltas do.  The user's
//     natural-scrolling setting is left as SDL reports it.
//   - SDL captures the mouse while a button is down, so a release outside the window still arrives
//     (Windows' game never called SetCapture and lost it: a stuck drag).  Such positions are held to
//     the window's edge, the range the engine always saw.
// The cursors: RM_WINDOWS's Data/Cursors/*.ANI files, decoded by AniCursor into SDL colour or animated
// cursors, per cursor and direction as Win32Mouse keeps its HCURSORs.

#pragma once

#ifndef __SDLMOUSE_H
#define __SDLMOUSE_H

#include "GameClient/Mouse.h"

struct AniCursor;
struct SDL_Cursor;
struct SDL_Window;

class SdlMouse : public Mouse
{
public:
	SdlMouse( void );
	virtual ~SdlMouse( void );

	virtual void init( void );
	virtual void reset( void );
	virtual void update( void );
	virtual void initCursorResources( void );
	virtual void setCursor( MouseCursor cursor );
	virtual void capture( void );
	virtual void releaseCapture( void );
	virtual void setVisibility( Bool visible );
	virtual Bool isCursorInWindow( void ) const { return m_cursorInWindow; }

	/// What SdlInput_dispatch hands over, already in game pixels
	enum EventKind { EVENT_NONE = 0, EVENT_MOVE, EVENT_BUTTON_DOWN, EVENT_BUTTON_UP, EVENT_WHEEL };
	enum Button { BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT };
	void addEvent( EventKind kind, Int x, Int y, Button button, Int clicks, Int wheelDelta, UnsignedInt timeMs );

	void lostFocus( Bool state ) { m_lostFocus = state; }	///< Win32Mouse's, for the focus handling

	/// The mouse SDL's events go to: the one the game made, or none yet
	static SdlMouse *active( void ) { return s_active; }

	/// An SDL cursor from a decoded .ANI: a colour cursor for one step, an animated one for more
	static SDL_Cursor *createCursor( const AniCursor &cursor );

	/** Win32Mouse::update's clip condition: a focused window that covers its screen, with the pointer
		* over it.  Here, for the test, with the facts passed in. */
	static Bool wantsConfinement( Bool coversScreen, Bool focused, Bool cursorInWindow );

protected:
	virtual UnsignedByte getMouseEvent( MouseIO *result, Bool flush );
	void translateEvent( UnsignedInt eventIndex, MouseIO *result );

	struct SdlMouseEvent
	{
		EventKind kind;			///< EVENT_NONE marks a free slot, as msg == 0 does in Win32Mouse's
		Int x, y;
		Button button;
		Int clicks;
		Int wheelDelta;
		UnsignedInt time;
	};
	SdlMouseEvent m_eventBuffer[ Mouse::NUM_MOUSE_EVENTS ];
	UnsignedInt m_nextFreeIndex;
	UnsignedInt m_nextGetIndex;

	MouseCursor m_currentSdlCursor;
	Int m_directionFrame;				///< current frame of a directional cursor, as Win32Mouse's (W3DMouse sets it)
	Bool m_lostFocus;
	Bool m_cursorInWindow;
	Bool m_positionReported;
	Bool m_cursorConfined;

	static SdlMouse *s_active;
};

/** Destroys every cursor SdlMouse made, before SDL's video goes (SdlGameEngine::destroyWindow).  Left to
	* SDL_QuitMouse, an animated cursor on a video driver with no animated cursors of its own crashes it: SDL
	* builds the animation from per-frame cursors that are also on its cursor list, and destroying the whole
	* frees frames the list still holds (SDL 3.4.16, and main as of 2026-09-27; the Linux -offscreen run on
	* a Linux x86_64 host).  Destroyed here, the frames leave the list with it. */
void SdlMouse_releaseCursors( void );

#endif // __SDLMOUSE_H
