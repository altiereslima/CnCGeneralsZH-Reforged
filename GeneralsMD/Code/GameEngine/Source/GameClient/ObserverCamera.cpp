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
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/ObserverCamera.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Module/AIUpdate.h"
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
/// no cut sooner than this after the last one, however big the other fight
static const UnsignedInt DIRECTOR_SETTLE_FRAMES = 2 * LOGICFRAMES_PER_SECOND;
/// this much hotter elsewhere and the director goes before its hold is up
static const Real DIRECTOR_BIG_MARGIN = 3.0f;
/// after this long on one fight any hotter one elsewhere will do
static const UnsignedInt DIRECTOR_TIRED_FRAMES = 20 * LOGICFRAMES_PER_SECOND;
static const Real DIRECTOR_TIRED_MARGIN = 1.1f;
/// a hit on a thing that cost this much counts twice what a free one does
static const Real DIRECTOR_COST_PER_WEIGHT = 500.0f;
/// a kill counts this many times a hit, and anything on a superweapon this many times again
static const Real DIRECTOR_KILL_FACTOR = 2.0f;
static const Real DIRECTOR_SUPERWEAPON_FACTOR = 3.0f;
/// with no fight on, how long the director looks at one army, base or building site
static const UnsignedInt DIRECTOR_SIGHT_FRAMES = 8 * LOGICFRAMES_PER_SECOND;
/// how many of the last sights the director will not go back to while there is another
static const size_t DIRECTOR_SEEN_COUNT = 3;
/// further apart than this the camera cuts rather than pans: a pan across the map shows nothing
static const Real CUT_DISTANCE = 700.0f;
/// the camera found further than this from where it was put was moved by something else, a click on
/// the production strip or a jump to a group; the edge of the map pulls it back by less.  The radar
/// hands the camera over itself, since a click near the camera and a drag both move it by less
static const Real HAND_JUMP_DISTANCE = 400.0f;
/// how quickly the camera closes on where it is going, in seconds to cover about two thirds of it
static const Real DIRECTOR_PAN_SECONDS = 0.35f;
/// a player's camera comes a few times a second, and this smooths the steps between
static const Real PLAYER_PAN_SECONDS = 0.12f;
/// a frame longer than this is a hitch, and is not allowed to throw the camera across the map
static const UnsignedInt LONGEST_STEP_MILLISECONDS = 100;
static const Real MILLISECONDS_PER_SECOND = 1000.0f;

//-------------------------------------------------------------------------------------------------
static Bool sameFight( const Coord2D &a, const Coord2D &b )
{
	const Real dx = a.x - b.x;
	const Real dy = a.y - b.y;
	return dx * dx + dy * dy <= DIRECTOR_GATHER_RADIUS * DIRECTOR_GATHER_RADIUS;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_hitWeight( Int cost, Bool killed, Bool superweapon )
{
	Real weight = 1.0f + cost / DIRECTOR_COST_PER_WEIGHT;
	if( killed )
		weight *= DIRECTOR_KILL_FACTOR;
	if( superweapon )
		weight *= DIRECTOR_SUPERWEAPON_FACTOR;
	return weight;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_sightWeight( Int cost, Bool structure, Bool busy, Bool superweapon )
{
	Real weight = cost / DIRECTOR_COST_PER_WEIGHT;
	if( busy )
		weight *= 2.0f;
	else if( structure )
		weight *= 0.5f;
	if( superweapon )
		weight *= DIRECTOR_SUPERWEAPON_FACTOR;
	return weight;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_nextSight( const std::vector< DirectorHeat > &sights, const std::vector< Coord2D > &seen, Coord2D *place )
{
	std::vector< DirectorHeat > fresh;
	for( size_t index = 0; index < sights.size(); index++ )
	{
		Bool wasSeen = FALSE;
		for( size_t look = 0; look < seen.size() && !wasSeen; look++ )
			wasSeen = sameFight( sights[ index ].position, seen[ look ] );
		if( !wasSeen )
			fresh.push_back( sights[ index ] );
	}
	Real heat = 0.0f;
	return ObserverCamera_hottestPlace( fresh, place, &heat );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_heatAround( const std::vector< DirectorHeat > &hits, const Coord2D &around, Coord2D *middle )
{
	Real heat = 0.0f;
	Coord2D sum = { 0.0f, 0.0f };
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const DirectorHeat &hit = hits[ index ];
		if( !sameFight( hit.position, around ) )
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
	if( framesHere < DIRECTOR_SETTLE_FRAMES )
		return FALSE;
	if( heatThere > heatHere * DIRECTOR_BIG_MARGIN )
		return TRUE;
	if( framesHere < DIRECTOR_HOLD_FRAMES )
		return FALSE;
	const Real margin = framesHere >= DIRECTOR_TIRED_FRAMES ? DIRECTOR_TIRED_MARGIN : DIRECTOR_SWITCH_MARGIN;
	return heatThere > heatHere * margin;
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
	m_placeIsFight = FALSE;
	m_seen.clear();
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
	m_driving = FALSE;
	m_placeValid = FALSE;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::followPlayer( Int playerIndex )
{
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
	* starts keeping everybody's the first time he asks for somebody else's; what the new viewer saw
	* before that has no snapshot and is not drawn in his fog, shadow included.
	*
	* The frame each drawable was last seen clear belongs to the old viewer and is cleared, or a unit
	* he saw stays drawn two seconds into the new viewer's fog. */
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
		draw->setShroudClearFrame( 0 );
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
/** Where the director looks: the fight with the most at stake, held for a while, followed as it
	* moves, and left for a clearly bigger one.  With no fight anywhere it goes round what is worth
	* seeing instead, an army on the move, a base going up, a superweapon, a few seconds each and not
	* straight back to one it has just shown.  Narrowed to one player it counts only the hits on his
	* things, the hits his things made and his own sights.  It only reads the logic. */
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
	std::vector< DirectorHeat > sights;
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		const Int cost = obj->getTemplate()->friend_getBuildCost();
		const Bool superweapon = obj->isKindOf( KINDOF_FS_SUPERWEAPON );
		const Bool mine = narrowTo == NULL || obj->getControllingPlayer() == narrowTo;
		DirectorHeat heat;
		heat.position.x = obj->getPosition()->x;
		heat.position.y = obj->getPosition()->y;

		if( mine && cost > 0 && !obj->isEffectivelyDead() )
		{
			const AIUpdateInterface *ai = obj->getAIUpdateInterface();
			const Bool marching = ai != NULL && ai->isMoving() && obj->isAbleToAttack();
			const Bool busy = marching || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION );
			heat.weight = ObserverCamera_sightWeight( cost, obj->isKindOf( KINDOF_STRUCTURE ), busy, superweapon );
			sights.push_back( heat );
		}

		const BodyModuleInterface *body = obj->getBodyModule();
		const UnsignedInt hitAt = body->getLastDamageTimestamp();
		if( hitAt == 0 || frame >= hitAt + DIRECTOR_HEAT_FRAMES )
			continue;
		if( !mine && ( body->getLastDamageInfo()->in.m_sourcePlayerMask & narrowTo->getPlayerMask() ) == 0 )
			continue;

		heat.weight = ObserverCamera_hitWeight( cost, obj->isEffectivelyDead(), superweapon );
		hits.push_back( heat );
	}

	Coord2D hottest;
	Real hottestHeat = 0.0f;
	ObserverCamera_hottestPlace( hits, &hottest, &hottestHeat );

	const UnsignedInt held = frame - m_placeSince;
	Coord2D followed = m_place;
	if( m_placeValid && m_placeIsFight )
	{
		const Real heatHere = ObserverCamera_heatAround( hits, m_place, &followed );
		if( heatHere > 0.0f && ( sameFight( hottest, followed ) || !ObserverCamera_shouldMove( heatHere, hottestHeat, held ) ) )
		{
			m_place = followed;
			*place = m_place;
			return TRUE;
		}
	}
	else if( m_placeValid && hottestHeat <= 0.0f && held < DIRECTOR_SIGHT_FRAMES )
	{
		if( ObserverCamera_heatAround( sights, m_place, &followed ) > 0.0f )
			m_place = followed;
		*place = m_place;
		return TRUE;
	}

	if( hottestHeat > 0.0f )
	{
		DEBUG_LOG(( "OBSCAM frame %u director to fight (%.0f,%.0f) heat %.1f\n", frame, hottest.x, hottest.y, hottestHeat ));
		m_place = hottest;
		m_placeIsFight = TRUE;
	}
	else
	{
		Coord2D sight;
		if( !ObserverCamera_nextSight( sights, m_seen, &sight ) )
		{
			m_seen.clear();
			if( !ObserverCamera_nextSight( sights, m_seen, &sight ) )
			{
				m_placeValid = FALSE;
				return FALSE;
			}
		}
		DEBUG_LOG(( "OBSCAM frame %u director to sight (%.0f,%.0f)\n", frame, sight.x, sight.y ));
		m_place = sight;
		m_placeIsFight = FALSE;
		m_seen.push_back( sight );
		if( m_seen.size() > DIRECTOR_SEEN_COUNT )
			m_seen.erase( m_seen.begin() );
	}
	m_placeSince = frame;
	m_placeValid = TRUE;
	*place = m_place;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::isShowingPlayerView( void ) const
{
	return m_mode == OBSERVER_CAMERA_PLAYER && m_followed != NO_PLAYER && m_playerViews[ m_followed ].isValid();
}

//-------------------------------------------------------------------------------------------------
/** The director with a player picked keeps to his fights; a player's camera with nobody picked has
	* nothing to show and leaves the camera where it is. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::chooseTarget( const ViewLocation &current, ViewLocation *target )
{
	if( m_mode == OBSERVER_CAMERA_PLAYER && m_followed == NO_PLAYER )
		return FALSE;

	if( isShowingPlayerView() )
	{
		*target = m_playerViews[ m_followed ];
		return TRUE;
	}

	const Player *narrowTo = m_followed == NO_PLAYER ? NULL : ThePlayerList->getNthPlayer( m_followed );
	const Coord3D &at = current.getPosition();
	Coord2D place;
	if( !directorPlace( narrowTo, &place ) )
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

	holdHeight( isShowingPlayerView() );
	const Real timeConstant = m_mode == OBSERVER_CAMERA_PLAYER ? PLAYER_PAN_SECONDS : DIRECTOR_PAN_SECONDS;
	const ViewLocation step = ObserverCamera_approach( current, target, elapsed / MILLISECONDS_PER_SECOND, timeConstant );
	TheTacticalView->setLocation( &step );
	m_drivenTo = step.getPosition();
	m_driving = TRUE;
}
