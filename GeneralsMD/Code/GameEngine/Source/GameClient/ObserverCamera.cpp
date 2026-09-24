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

// ObserverCamera.cpp /////////////////////////////////////////////////////////////////////////////
// The watcher's camera driven for him, and the watched player's fog.  ObserverCamera.h says which
// is which.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/ObserverCamera.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

#include <math.h>

ObserverCamera TheObserverCamera;

/// how far round a hit the others count as the same fight: about half a screen at the default zoom
static const Real DIRECTOR_GATHER_RADIUS = 220.0f;
/// how long a hit keeps a place hot
static const UnsignedInt DIRECTOR_HEAT_FRAMES = 5 * LOGICFRAMES_PER_SECOND;
/// how often the hits are counted again
static const UnsignedInt DIRECTOR_SCAN_FRAMES = LOGICFRAMES_PER_SECOND / 2;
/// how long the director stays with a fight that is still going before it looks for a better one
static const UnsignedInt DIRECTOR_HOLD_FRAMES = 6 * LOGICFRAMES_PER_SECOND;
/// how much hotter somewhere else has to be to be worth leaving a fight that is still going
static const Real DIRECTOR_SWITCH_MARGIN = 1.5f;
/// further apart than this the camera cuts rather than pans: a pan across the map shows nothing
static const Real CUT_DISTANCE = 700.0f;
/// the camera found further than this from where it was put was moved by something else, the radar
/// most likely; the edge of the map pulls it back by less
static const Real HAND_JUMP_DISTANCE = 400.0f;
/// how quickly the camera closes on where it is going, in seconds to cover about two thirds of it
static const Real DIRECTOR_PAN_SECONDS = 0.35f;
/// a player's camera comes a few times a second, and this smooths the steps between
static const Real PLAYER_PAN_SECONDS = 0.12f;
/// a frame longer than this is a hitch, and is not allowed to throw the camera across the map
static const UnsignedInt LONGEST_STEP_MILLISECONDS = 100;
static const Real MILLISECONDS_PER_SECOND = 1000.0f;

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_heatAround( const std::vector< DirectorHeat > &hits, const Coord2D &around, Coord2D *middle )
{
	Real heat = 0.0f;
	Coord2D sum = { 0.0f, 0.0f };
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const DirectorHeat &hit = hits[ index ];
		const Real dx = hit.position.x - around.x;
		const Real dy = hit.position.y - around.y;
		if( dx * dx + dy * dy > DIRECTOR_GATHER_RADIUS * DIRECTOR_GATHER_RADIUS )
			continue;

		heat += hit.weight;
		sum.x += hit.position.x * hit.weight;
		sum.y += hit.position.y * hit.weight;
	}

	*middle = around;
	if( heat > 0.0f )
	{
		middle->x = sum.x / heat;
		middle->y = sum.y / heat;
	}
	return heat;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_hottestPlace( const std::vector< DirectorHeat > &hits, Coord2D *place, Real *heat )
{
	// ponytail: every hit against every other, fine for the few hundred a big fight makes in five
	// seconds; a grid of cells if a match ever gets to thousands
	*heat = 0.0f;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		Coord2D middle;
		const Real around = ObserverCamera_heatAround( hits, hits[ index ].position, &middle );
		if( around > *heat )
		{
			*heat = around;
			*place = middle;
		}
	}
	return *heat > 0.0f;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_shouldMove( Real heatHere, Real heatThere, UnsignedInt framesHere )
{
	if( heatHere <= 0.0f )
		return heatThere > 0.0f;
	return framesHere >= DIRECTOR_HOLD_FRAMES && heatThere > heatHere * DIRECTOR_SWITCH_MARGIN;
}

//-------------------------------------------------------------------------------------------------
/** An angle's shortest way round to another, so a camera facing just west of north turns a few
	* degrees to just east of it rather than all the way back round. */
//-------------------------------------------------------------------------------------------------
static Real turnTowards( Real from, Real to, Real share )
{
	Real turn = to - from;
	while( turn > PI )
		turn -= 2.0f * PI;
	while( turn < -PI )
		turn += 2.0f * PI;
	return from + turn * share;
}

//-------------------------------------------------------------------------------------------------
ViewLocation ObserverCamera_approach( const ViewLocation &from, const ViewLocation &to, Real elapsedSeconds, Real timeConstant )
{
	const Coord3D &start = from.getPosition();
	const Coord3D &end = to.getPosition();
	const Real dx = end.x - start.x;
	const Real dy = end.y - start.y;
	if( dx * dx + dy * dy > CUT_DISTANCE * CUT_DISTANCE )
		return to;

	const Real share = 1.0f - expf( -elapsedSeconds / timeConstant );
	ViewLocation step;
	step.init( start.x + dx * share, start.y + dy * share, start.z + ( end.z - start.z ) * share,
						 turnTowards( from.getAngle(), to.getAngle(), share ),
						 from.getPitch() + ( to.getPitch() - from.getPitch() ) * share,
						 from.getZoom() + ( to.getZoom() - from.getZoom() ) * share );
	return step;
}

//-------------------------------------------------------------------------------------------------
ObserverCamera::ObserverCamera()
{
	reset();
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::reset( void )
{
	m_mode = OBSERVER_CAMERA_FREE;
	m_followed = NO_PLAYER;
	m_fog = FALSE;
	m_shroudViewer = NO_PLAYER;
	m_driving = FALSE;
	m_holdingHeight = FALSE;
	m_drivenTo.zero();
	m_lastUpdate = 0;
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
		m_playerViews[ index ] = ViewLocation();
	m_placeValid = FALSE;
	m_place.x = m_place.y = 0.0f;
	m_placeSince = 0;
	m_placeScanned = 0;
	m_placeFor = NULL;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::notePlayerView( Int playerIndex, const ViewLocation &view )
{
	if( !m_playerViews[ playerIndex ].isValid() )
		DEBUG_LOG(( "OBSCAM frame %u first camera from player %d\n", TheGameLogic->getFrame(), playerIndex ));
	m_playerViews[ playerIndex ] = view;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::setMode( ObserverCameraMode mode )
{
	m_mode = mode;
	m_followed = NO_PLAYER;
	m_driving = FALSE;
	m_placeValid = FALSE;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::followPlayer( Int playerIndex )
{
	m_mode = OBSERVER_CAMERA_PLAYER;
	m_followed = playerIndex;
	m_driving = FALSE;
	m_placeValid = FALSE;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera::getShroudPlayerIndex( void ) const
{
	return m_shroudViewer != NO_PLAYER ? m_shroudViewer : ThePlayerList->getLocalPlayer()->getPlayerIndex();
}

//-------------------------------------------------------------------------------------------------
/** The fog is the followed player's while it is on.  Swapping it is what the debug key that makes
	* you another player does to the fog: his ghosts of what he last saw in, and the ground redrawn
	* in his shroud.  A knocked-out player's machine kept only his own fog memory until now, so it
	* starts keeping everybody's the first time he asks for somebody else's.
	*
	* Two things each drawable remembers belong to the old viewer and are cleared: whether it is
	* obscured, which a building's shadow is only decided again on a change of, and the frame it was
	* last seen clear, which keeps a unit drawn two seconds after it goes into fog.  The drawable
	* walk in GameClient::update sets the first again for the new viewer before the next frame. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateShroudViewer( void )
{
	const Int viewer = m_fog ? m_followed : NO_PLAYER;
	if( viewer == m_shroudViewer )
		return;

	m_shroudViewer = viewer;
	TheGhostObjectManager->setTrackAllPlayers( TRUE );
	TheGhostObjectManager->setLocalPlayerIndex( getShroudPlayerIndex() );
	ThePartitionManager->refreshShroudForLocalPlayer();
	for( Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable() )
	{
		draw->setFullyObscuredByShroud( FALSE );
		draw->setShroudClearFrame( 0 );
	}
}

//-------------------------------------------------------------------------------------------------
/** While a player's camera is shown, his zoom is: the view otherwise eases its height back towards
	* the watcher's own every frame and the two meet two thirds of the way.  Only ever let go when
	* this took it, so the cinema's hold on the height is not undone. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::holdHeight( Bool hold )
{
	if( hold == m_holdingHeight )
		return;

	m_holdingHeight = hold;
	TheTacticalView->setOkToAdjustHeight( !hold );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::takenByHand( const ViewLocation &current ) const
{
	if( TheLookAtTranslator->isMovingCamera() )
		return TRUE;

	const Real dx = current.getPosition().x - m_drivenTo.x;
	const Real dy = current.getPosition().y - m_drivenTo.y;
	return dx * dx + dy * dy > HAND_JUMP_DISTANCE * HAND_JUMP_DISTANCE;
}

//-------------------------------------------------------------------------------------------------
/** Where the director looks: the hottest fight, held for a while, followed as it moves, and left
	* for a clearly hotter one.  Narrowed to one player it counts only the hits on his things and the
	* hits his things made, and with none of those it goes home to his command centre. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::directorPlace( const Player *narrowTo, Coord2D *place )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( narrowTo != m_placeFor )
	{
		m_placeFor = narrowTo;
		m_placeValid = FALSE;
	}
	if( m_placeValid && frame >= m_placeScanned && frame < m_placeScanned + DIRECTOR_SCAN_FRAMES )
	{
		*place = m_place;
		return TRUE;
	}
	m_placeScanned = frame;

	std::vector< DirectorHeat > hits;
	const Object *home = NULL;
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		if( narrowTo != NULL && home == NULL && obj->getControllingPlayer() == narrowTo
				&& obj->isKindOf( KINDOF_COMMANDCENTER ) )
			home = obj;

		const BodyModuleInterface *body = obj->getBodyModule();
		const UnsignedInt hitAt = body->getLastDamageTimestamp();
		if( hitAt == 0 || frame >= hitAt + DIRECTOR_HEAT_FRAMES )
			continue;
		if( narrowTo != NULL && obj->getControllingPlayer() != narrowTo
				&& ( body->getLastDamageInfo()->in.m_sourcePlayerMask & narrowTo->getPlayerMask() ) == 0 )
			continue;

		DirectorHeat hit;
		hit.position.x = obj->getPosition()->x;
		hit.position.y = obj->getPosition()->y;
		hit.weight = 1.0f;
		hits.push_back( hit );
	}

	Coord2D hottest;
	Real hottestHeat = 0.0f;
	ObserverCamera_hottestPlace( hits, &hottest, &hottestHeat );

	Coord2D followed = m_place;
	const Real heatHere = m_placeValid ? ObserverCamera_heatAround( hits, m_place, &followed ) : 0.0f;
	if( !m_placeValid || ObserverCamera_shouldMove( heatHere, hottestHeat, frame - m_placeSince ) )
	{
		if( hottestHeat > 0.0f )
		{
			DEBUG_LOG(( "OBSCAM frame %u director to (%.0f,%.0f) heat %.0f, was %.0f here\n",
									frame, hottest.x, hottest.y, hottestHeat, heatHere ));
			m_place = hottest;
			m_placeSince = frame;
			m_placeValid = TRUE;
		}
		else if( home != NULL )
		{
			m_place.x = home->getPosition()->x;
			m_place.y = home->getPosition()->y;
			m_placeSince = frame;
			m_placeValid = TRUE;
		}
	}
	else
		m_place = followed;

	*place = m_place;
	return m_placeValid;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::chooseTarget( const ViewLocation &current, ViewLocation *target )
{
	const Coord3D &at = current.getPosition();
	Coord2D place;
	if( m_mode == OBSERVER_CAMERA_DIRECTOR )
	{
		if( !directorPlace( NULL, &place ) )
			return FALSE;
		target->init( place.x, place.y, at.z, current.getAngle(), current.getPitch(), current.getZoom() );
		return TRUE;
	}

	const ViewLocation &view = m_playerViews[ m_followed ];
	if( view.isValid() )
	{
		*target = view;
		return TRUE;
	}

	if( !directorPlace( ThePlayerList->getNthPlayer( m_followed ), &place ) )
		return FALSE;
	target->init( place.x, place.y, at.z, current.getAngle(), current.getPitch(), current.getZoom() );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::update( UnsignedInt nowMilliseconds )
{
	const UnsignedInt elapsed = m_lastUpdate == 0 ? 0 : min( nowMilliseconds - m_lastUpdate, LONGEST_STEP_MILLISECONDS );
	m_lastUpdate = nowMilliseconds;

	updateShroudViewer();

	if( m_mode == OBSERVER_CAMERA_FREE )
	{
		m_driving = FALSE;
		holdHeight( FALSE );
		return;
	}

	ViewLocation current;
	TheTacticalView->getLocation( &current );
	// the followed player stays followed, for his fog, with the camera back in the watcher's hands
	if( m_driving && takenByHand( current ) )
	{
		DEBUG_LOG(( "OBSCAM frame %u the watcher took the camera\n", TheGameLogic->getFrame() ));
		m_mode = OBSERVER_CAMERA_FREE;
		m_driving = FALSE;
		holdHeight( FALSE );
		return;
	}

	ViewLocation target;
	if( !chooseTarget( current, &target ) )
	{
		m_driving = FALSE;
		holdHeight( FALSE );
		return;
	}

	holdHeight( m_mode == OBSERVER_CAMERA_PLAYER && m_playerViews[ m_followed ].isValid() );
	const Real timeConstant = m_mode == OBSERVER_CAMERA_PLAYER ? PLAYER_PAN_SECONDS : DIRECTOR_PAN_SECONDS;
	const ViewLocation step = ObserverCamera_approach( current, target, elapsed / MILLISECONDS_PER_SECOND, timeConstant );
	TheTacticalView->setLocation( &step );
	m_drivenTo = step.getPosition();
	m_driving = TRUE;
}
