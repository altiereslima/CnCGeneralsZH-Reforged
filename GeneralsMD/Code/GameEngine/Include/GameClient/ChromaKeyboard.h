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

// FILE: ChromaKeyboard.h /////////////////////////////////////////////////////
//
// Puts the state of the match on Razer hardware: the command bar on the letter
// keys, power on the digits, the generals powers on the function row, alerts on
// the navigation cluster, superweapons on the numpad, production along the
// bottom, and money on the mousepad.  Talks to the Chroma REST server on
// localhost, so it needs no SDK header, no import library and no DLL beside the
// exe: with Synapse absent the first request fails and the whole thing goes
// quiet for the run.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CHROMA_KEYBOARD_H_
#define __CHROMA_KEYBOARD_H_

/** Read the game state and hand the hardware its next frame.  Called once per
	* engine update from GameEngine::update; cheap, and never blocks on the
	* network - the request itself belongs to a worker thread. */
extern void updateChromaKeyboard( void );

/** Stop the worker thread.  Called on the way out of GameEngine. */
extern void shutdownChromaKeyboard( void );

/** Turn the whole thing off for this run, from -nochroma or the option.  Has to
	* be called before the first update or the worker is already up. */
extern void disableChromaKeyboard( void );

/** Which cell of the six-by-twenty-two Chroma grid a command bar key lights, or
	* -1 for a key the map does not cover.  Only lower case letters and digits get
	* an answer, which is all the command bar ever binds. */
extern Int chromaCellForKey( char key );

/// How many keys the power meter spans, and what it answers once consumption has
/// passed production and the whole row goes to blinking red instead.
enum { CHROMA_POWER_SEGMENTS = 10, CHROMA_POWER_BROWNOUT = -1 };

/** How many of those keys light for a production and consumption pair: all of
	* them while nothing is drawing, none at all for a player who has not built a
	* power plant, and CHROMA_POWER_BROWNOUT once the draw has passed the supply. */
extern Int chromaPowerSegments( Int production, Int consumption );

/** How many lamps of a bar light for a fraction between zero and one.  Any
	* fraction above zero lights at least one, so a bar that has started is never
	* mistaken for a bar that has not. */
extern Int chromaBarSegments( Real fraction, Int segments );

/** How many lamps of the money bar light.  One lamp is a thousand credits, and
	* the bar stops at its own length rather than wrapping. */
extern Int chromaMoneySegments( UnsignedInt money, Int segments );

#endif // __CHROMA_KEYBOARD_H_
