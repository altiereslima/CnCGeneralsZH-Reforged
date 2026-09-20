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
// Paints the player's colour onto a Razer keyboard, lights the command bar
// keys that can be pressed right now, and pulses red while the base is taking
// fire.  Talks to the Chroma REST server on localhost, so it needs no SDK
// header, no import library and no DLL beside the exe: with Synapse absent the
// first request fails and the whole thing goes quiet for the run.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CHROMA_KEYBOARD_H_
#define __CHROMA_KEYBOARD_H_

/** Read the game state and hand the keyboard its next frame.  Called once per
	* engine update from GameEngine::update; cheap, and never blocks on the
	* network - the request itself belongs to a worker thread. */
extern void updateChromaKeyboard( void );

/** Stop the worker thread.  Called from GameEngine::reset on the way out. */
extern void shutdownChromaKeyboard( void );

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

#endif // __CHROMA_KEYBOARD_H_
