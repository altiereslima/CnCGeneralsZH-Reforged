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

// ObserverCamera.h ///////////////////////////////////////////////////////////////////////////////
// Who drives a watcher's camera, and whose fog his screen is drawn in.
//
// The mode and the followed player are picked apart, from two lists.  Free is the camera in the
// watcher's own hands.  Director goes wherever the most things have been hit in the last few
// seconds and stays there a while before it looks for a hotter fight; with a player picked it
// counts only the fights that player is in.  Player shows what the followed player's own screen
// shows: a player's camera comes over the network a few times a second (MSG_SET_REPLAY_CAMERA), an
// AI, which has no camera, gets the narrowed director, and with nobody picked it does nothing.
// Scrolling with the keys or a drag, turning the camera or clicking the radar hands it back to the
// watcher, who keeps the player he picked.  The screen's edge does not scroll while this drives:
// the page's flags stand on the top edge and its panel on the right one, and reaching for either
// used to scroll the map and take the camera from the director with nobody asking.
//
// The followed player is picked from his own list, not the seats: clicking a unit makes its owner
// the watched player, and the camera jumping to an enemy's screen on a click would be no use.  Fog
// on draws the followed player's fog, what he has seen and what he has not, and hides what he
// cannot see, stealthed units he has not detected included; following nobody it changes nothing.
// Nothing here is logic: the camera and the fog are this machine's picture only.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _OBSERVER_CAMERA_H_
#define _OBSERVER_CAMERA_H_

#include "Common/GameCommon.h"
#include "GameClient/View.h"

#include <vector>

enum ObserverCameraMode
{
	OBSERVER_CAMERA_FREE,
	OBSERVER_CAMERA_DIRECTOR,
	OBSERVER_CAMERA_PLAYER
};

/// one thing hit lately, where it stands and how much it counts for
struct DirectorHeat
{
	Coord2D position;
	Real weight;
};

/// the place the hits crowd most: each hit's weight summed over those within DIRECTOR_GATHER_RADIUS
/// of it, and the best one's neighbours averaged by weight.  FALSE when nothing was hit
Bool ObserverCamera_hottestPlace( const std::vector< DirectorHeat > &hits, Coord2D *place, Real *heat );
/// the weight of the hits within DIRECTOR_GATHER_RADIUS of a place, and their weighted middle
Real ObserverCamera_heatAround( const std::vector< DirectorHeat > &hits, const Coord2D &around, Coord2D *middle );
/// whether a director holding a place with heatHere for framesHere should cut to one with heatThere
Bool ObserverCamera_shouldMove( Real heatHere, Real heatThere, UnsignedInt framesHere );
/// a step of the camera towards where it is going, easing with timeConstant, or the jump there when
/// the two are further apart than a pan should cross
ViewLocation ObserverCamera_approach( const ViewLocation &from, const ViewLocation &to, Real elapsedSeconds, Real timeConstant );

class Player;

class ObserverCamera
{
public:
	ObserverCamera();

	void reset( void );
	/// once a frame on a watcher's machine: move the camera and swap the fog when it has to
	void update( UnsignedInt nowMilliseconds );

	/// a player's camera as it came over the network or out of a replay
	void notePlayerView( Int playerIndex, const ViewLocation &view );

	ObserverCameraMode getMode( void ) const { return m_mode; }
	void setMode( ObserverCameraMode mode );
	/// the player whose screen the player mode shows, whose fights the director keeps to and whose
	/// fog is drawn while fog is on; NO_PLAYER for nobody
	void followPlayer( Int playerIndex );
	/// the player being followed, still while the watcher has the camera in his own hands, or
	/// NO_PLAYER
	Int getFollowedPlayerIndex( void ) const { return m_followed; }
	/// the camera is where this put it last frame, the director's or a player's
	Bool isDriving( void ) const { return m_driving; }
	Bool isFogOn( void ) const { return m_fog; }
	void setFog( Bool fog ) { m_fog = fog; }

	/// the player whose fog the screen is drawn in: the local player, or whoever a watcher with fog
	/// on is following
	Int getShroudPlayerIndex( void ) const;

	enum { NO_PLAYER = -1 };

private:
	void updateShroudViewer( void );
	void holdHeight( Bool hold );
	Bool takenByHand( const ViewLocation &current ) const;
	Bool isShowingPlayerView( void ) const;
	Bool chooseTarget( const ViewLocation &current, ViewLocation *target );
	Bool directorPlace( const Player *narrowTo, Coord2D *place );

	ObserverCameraMode m_mode;
	Int m_followed;
	Bool m_fog;
	Int m_shroudViewer;
	Bool m_driving;									///< the camera was put where it is by this, last frame
	Bool m_holdingHeight;						///< the view's own height easing is off while a player's zoom is shown
	Coord3D m_drivenTo;
	UnsignedInt m_lastUpdate;

	ViewLocation m_playerViews[ MAX_PLAYER_COUNT ];

	Bool m_placeValid;
	Coord2D m_place;								///< where the director is looking
	UnsignedInt m_placeSince;				///< the logic frame it went there
	UnsignedInt m_placeScanned;			///< the logic frame the hits were last counted
	const Player *m_placeFor;				///< whose fights the place was picked from, NULL for everybody's
};

extern ObserverCamera TheObserverCamera;

#endif
