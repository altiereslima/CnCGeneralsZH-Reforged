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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: SelectionXlat.h ///////////////////////////////////////////////////////////
// Author: Steven Johnson, Dec 2001

#pragma once

#ifndef _H_SelectionXlat
#define _H_SelectionXlat

#include "GameClient/InGameUI.h"
#include "Common/Player.h"		// NUM_HOTKEY_SQUADS

class ThingTemplate;

typedef std::map<const ThingTemplate *, int> SelectCountMap;
typedef SelectCountMap::iterator SelectCountMapIt;

//-----------------------------------------------------------------------------
class SelectionTranslator : public GameMessageTranslator
{
	// this is an evil friend wrapper due to the current limitations of Drawable iterators.
	friend Bool selectFriendsWrapper( Drawable *draw, void *userData );
	friend Bool killThemKillThemAllWrapper( Drawable *draw, void *userData );
private:

	Bool m_leftMouseButtonIsDown;
	Bool m_dragSelecting;
	Bool m_dragOrderJustIssued;					///< an attack circle or a left-drawn order line just went out, so eat the click that ends it (fork)
	UnsignedInt m_lastGroupSelTime;
	Int m_lastGroupSelGroup;
	ICoord2D m_selectFeedbackAnchor;		// Note: Used for drawing feedback only.
	ICoord2D m_deselectFeedbackAnchor;	///< where Classic's right press went down, for 1.04's click test on its release
	UnsignedInt m_lastClick;						///< when it went down
	Coord3D m_deselectDownCameraPosition;	///< and where the camera stood then
	Bool m_displayedMaxWarning;	// did we already display a warning about selecting too many units?

	SelectCountMap m_selectCountMap;

	// A team key's new members reach the logic a run-ahead after the press in a network game.  A
	// select in between drew the squad the logic still held, while the logic, taking the two in
	// order, selected the new one: the screen and the orders went to different units.  What each
	// key last sent and the frame it lands on, 0 for nothing on the way.
	std::vector<ObjectID> m_pendingSquad[ NUM_HOTKEY_SQUADS ];
	UnsignedInt m_pendingSquadLands[ NUM_HOTKEY_SQUADS ];

	Bool isSquadPending( Int group ) const;
	Bool isTakenByLaterSquad( ObjectID id, Int group ) const;
	void selectHotkeySquad( Player *player, Int group, Bool ownOnly );	///< what the logic will have in the squad by the time a select sent now lands

	Bool selectFriends( Drawable *draw, GameMessage *createTeamMsg, Bool dragSelecting );
	Bool killThemKillThemAll( Drawable *draw, GameMessage *killThemAllMsg );

  

public:
	SelectionTranslator();
	~SelectionTranslator();
	virtual GameMessageDisposition translateGameMessage(const GameMessage *msg);

	//Added By Sadullah Nader
	//added for fix to the drag selection when entering control bar
	//changes the mode of drag selecting to it's opposite
	void setDragSelecting(Bool dragSelect);
	void setLeftMouseButton(Bool state);
	Bool isLeftMouseButtonDown( void ) const { return m_leftMouseButtonIsDown; }	///< the left button went down on the world, not on a window
	void forgetPendingSquads();	///< a match is over: what its team keys sent will never land

#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
  Bool m_HandOfGodSelectionMode;
  Bool isHandOfGodSelectionMode( void) { return m_HandOfGodSelectionMode; };
#endif

};	

Bool CanSelectDrawable( const Drawable *draw, Bool dragSelecting );
extern SelectionTranslator *TheSelectionTranslator;

#endif
