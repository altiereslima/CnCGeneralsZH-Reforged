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

// SdlKeyboard.h: the keyboard off Windows, from SDL3's key events (C3, decision 3).
//
// DirectInputKeyboard's counterpart.  DirectInput handed the engine buffered key data - one press or
// release at a time, in order, with no auto-repeat - and Keyboard::update turns that into key state,
// modifiers, its own repeat and the stream messages.  This gives it the same: SdlInput_dispatch queues
// each SDL key event here as a DIK code (SdlKeyTable) and getKey hands them over one at a time.  SDL's
// own repeat events never reach the queue, or every held key would repeat twice.  When the window loses
// the focus SDL releases the keys it holds, which is what DirectInput's re-acquire (KEY_LOST, then a
// reset) came to.

#pragma once

#ifndef __SDLKEYBOARD_H
#define __SDLKEYBOARD_H

#include "GameClient/Keyboard.h"

class SdlKeyboard : public Keyboard
{
public:
	SdlKeyboard( void );
	virtual ~SdlKeyboard( void );

	virtual void init( void );
	virtual void reset( void );
	virtual void update( void );
	virtual Bool getCapsState( void );		///< SDL's caps lock state

	/// A key press or release, as DirectInput would have buffered it; FALSE when the queue was full
	Bool addKey( UnsignedByte dik, Bool down );

	/// The keyboard SDL's events go to: the one the game made, or none yet
	static SdlKeyboard *active( void ) { return s_active; }

	enum { QUEUE_SIZE = 256 };	///< DirectInput's buffer (KEYBOARD_BUFFER_SIZE); a full one drops keys, as DirectInput did

protected:
	virtual void getKey( KeyboardIO *key );

	struct QueuedKey
	{
		UnsignedByte dik;
		Bool down;
		UnsignedInt sequence;	///< DirectInput's dwSequence: the order they came in
	};
	QueuedKey m_queue[ QUEUE_SIZE ];
	Int m_head;		///< the next key getKey hands over
	Int m_count;	///< keys waiting
	UnsignedInt m_nextSequence;

	static SdlKeyboard *s_active;
};

#endif // __SDLKEYBOARD_H
