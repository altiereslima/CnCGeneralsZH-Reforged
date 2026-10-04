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

// SdlKeyboard.cpp: see SdlKeyboard.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/KeyDefs.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlKeyboard.h"

#include <SDL3/SDL_keyboard.h>

SdlKeyboard *SdlKeyboard::s_active = NULL;

SdlKeyboard::SdlKeyboard( void ) : m_head( 0 ), m_count( 0 ), m_nextSequence( 0 )
{
	memset( m_queue, 0, sizeof( m_queue ) );
	s_active = this;
	SdlInput_install();		// before a text field can attach to the IME manager
}

SdlKeyboard::~SdlKeyboard( void )
{
	if (s_active == this)
		s_active = NULL;
}

void SdlKeyboard::init( void )
{
	Keyboard::init();
}

void SdlKeyboard::reset( void )
{
	Keyboard::reset();
}

void SdlKeyboard::update( void )
{
	Keyboard::update();
}

Bool SdlKeyboard::getCapsState( void )
{
	return (SDL_GetModState() & SDL_KMOD_CAPS) != 0;
}

Bool SdlKeyboard::addKey( UnsignedByte dik, Bool down )
{
	if (dik == KEY_NONE || m_count >= QUEUE_SIZE)
		return FALSE;
	QueuedKey &slot = m_queue[ (m_head + m_count) % QUEUE_SIZE ];
	slot.dik = dik;
	slot.down = down;
	slot.sequence = m_nextSequence++;
	++m_count;
	return TRUE;
}

// DirectInputKeyboard::getKey's contract: one key, or KEY_NONE when there are none
void SdlKeyboard::getKey( KeyboardIO *key )
{
	key->sequence = 0;
	key->key = KEY_NONE;
	if (m_count == 0)
		return;
	const QueuedKey &next = m_queue[ m_head ];
	m_head = (m_head + 1) % QUEUE_SIZE;
	--m_count;
	key->key = next.dik;
	key->sequence = next.sequence;
	key->state = next.down ? KEY_STATE_DOWN : KEY_STATE_UP;	// an assignment, the start of this key's state
	key->status = KeyboardIO::STATUS_UNUSED;
}
