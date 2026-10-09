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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: CleanupHazardUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Kris Morness, August 2002
// Desc:   Update module to handle independent targeting of hazards to cleanup.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_WEAPONSLOTTYPE_NAMES

#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/Module/CleanupHazardUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/AIUpdate.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

/** Scans in a row an area cleaner stands out of reach of its hazard before it gives the area up. */
static const Int MAX_CLEANUP_APPROACHES = 5;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CleanupHazardUpdateModuleData::CleanupHazardUpdateModuleData()
{
	m_weaponSlot				= PRIMARY_WEAPON;
	m_scanFrames				= 0;
	m_scanRange					= 0.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void CleanupHazardUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "WeaponSlot",						INI::parseLookupList,						TheWeaponSlotTypeNamesLookupList, offsetof( CleanupHazardUpdateModuleData, m_weaponSlot ) },
		{ "ScanRate",							INI::parseDurationUnsignedInt,	NULL, offsetof( CleanupHazardUpdateModuleData, m_scanFrames ) },
		{ "ScanRange",						INI::parseReal,									NULL, offsetof( CleanupHazardUpdateModuleData, m_scanRange ) },
		{ 0, 0, 0, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
CleanupHazardUpdate::CleanupHazardUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_bestTargetID							= INVALID_ID;
	m_nextScanFrames						= 0;
	m_nextShotAvailableInFrames = 0;
	m_inRange  									= false;
	m_weaponTemplate						= NULL;
	m_moveRange									= 0.0f;
	m_pos.zero();
	m_resumeGuard								= FALSE;
	m_resumeGuardMode						= GUARDMODE_NORMAL;
	m_returning									= FALSE;
	m_approaches								= 0;

} 

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CleanupHazardUpdate::~CleanupHazardUpdate( void )
{

}


//-------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::onObjectCreated()
{
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *self = getObject();
	
	//Make sure we have a weapon template
	self->setWeaponSetFlag( WEAPONSET_VETERAN );
	Weapon *weapon = self->getWeaponInWeaponSlot( data->m_weaponSlot );
	if( !weapon )
	{
		DEBUG_CRASH( ("CleanupHazardUpdate for %s doesn't have a valid weapon template", 
			getObject()->getTemplate()->getName().str() ) );
		return;
	}
	m_weaponTemplate = weapon->getTemplate();

	//Make sure our firing range is smaller than the scan range.
	WeaponBonus bonus;
	bonus.clear();
	Real attackRange = m_weaponTemplate->getAttackRange( bonus );
	if( data->m_scanRange <= attackRange )
	{
		DEBUG_CRASH( ("CleanupHazardUpdate for %s requires the scan range (%.1f) being larger than the firing range (%.1f)",
			getObject()->getTemplate()->getName().str(), data->m_scanRange, attackRange ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime CleanupHazardUpdate::update()
{	
/// @todo srj use SLEEPY_UPDATE here
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *obj = getObject();

	//Make sure we are "busy" for scripting purposes if the unit is cleaning up an area.
	if( m_moveRange > 0.0f )
	{
		//Means we are cleaning up an AREA, not just things immediately in range.
		AIUpdateInterface *ai = obj->getAI();
		if( ai )
		{
			if( ai->isIdle() )
			{
				//Keep him busy even though he's not moving -- he might be cleaning up.
				ai->aiBusy( CMD_FROM_AI );
			}
			else if( ai->getLastCommandSource() != CMD_FROM_AI )
			{
				//Either the player or a script gave a NEW order so abandon the cleanup area cause.
				m_moveRange = 0.0f;
				m_resumeGuard = FALSE;
				m_returning = FALSE;
				m_approaches = 0;
				return UPDATE_SLEEP_NONE;
			}
		}
	}

	//Optimized firing at acquired target
	if( m_nextScanFrames > 0 )
	{
		m_nextScanFrames--;
		fireWhenReady(); //Only happens if something is tracked.
		return UPDATE_SLEEP_NONE;
	}
	m_nextScanFrames = data->m_scanFrames;

	if( m_moveRange == 0.0f )
		startAutoCleanup();

	//Periodic scanning (expensive)
	Object *found = scanClosestTarget();
	if( found )
	{
		m_returning = FALSE;
		// Cleaning an area: standing still out of the weapon's reach on a scan, scan after scan, is a hazard
		// it cannot drive up to. It gives the area up rather than order the same drive for good.
		// ponytail: it may notice the same hazard again from home and try once more, bounded by the
		// hazard's own lifetime; a list of hazards given up on would end that
		AIUpdateInterface *ai = obj->getAI();
		WeaponBonus bonus;
		bonus.clear();
		if( m_moveRange > 0.0f && ai && (ai->isIdle() || ai->isBusy()) &&
				ThePartitionManager->getDistanceSquared( obj, found, FROM_CENTER_2D ) > sqr( m_weaponTemplate->getAttackRange( bonus ) ) )
		{
			if( ++m_approaches > MAX_CLEANUP_APPROACHES )
			{
				finishArea( "gave up on a hazard it cannot reach", TRUE );
				return UPDATE_SLEEP_NONE;
			}
		}
		else
			m_approaches = 0;
		//1 frame can make a big difference so fire ASAP!
		fireWhenReady();
	}
	else if( m_moveRange )
	{
		//There's nothing nearby, so if we are cleaning up an area versus hazards 
		//immediately in range, set the AI to idle so it can advance to the next script!
		AIUpdateInterface *ai = obj->getAI();
		if( ai && (ai->isIdle() || ai->isBusy()) )
		{
			Real fDist = sqrt( ThePartitionManager->getDistanceSquared( obj, &m_pos, FROM_CENTER_2D ) );
			// the walk back counts as done once it has stopped: a unit parked on the spot used to send it
			// back there every second for good
			if( fDist < 25.0f || m_returning )
			{
				//Abort clean area because there's nothing left to clean!
				finishArea( "area clean", FALSE );
			}
			else
			{
				//Abort clean area AFTER we move back to our initial position!
				ai->aiMoveToPosition( &m_pos, CMD_FROM_AI );
				m_returning = TRUE;
			}
		}
	}
	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::fireWhenReady()
{
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *self = getObject();

	//Track our target (this code prevents the object from moving beyond range)
	//If we are ordered to clean an area, then range doesn't matter because
	//we allow the unit to move to the area.
	Object *target = TheGameLogic->findObjectByID( m_bestTargetID );
	if( target && m_moveRange == 0.0f )
	{
		WeaponBonus bonus;
		bonus.clear();
		Real fireRange = m_weaponTemplate->getAttackRange( bonus );
		Object *me = getObject();
		Real fDist = sqrt( ThePartitionManager->getDistanceSquared( me, target, FROM_CENTER_2D ) );
		if( fDist < fireRange )
		{
			//We are currently in range!
			m_inRange = true;
		}
		else
		{
			if( m_inRange )
			{
				//We were in range last frame, but the target has moved out of firing range, so 
				//re-evaluate by forcing a new scan.
				m_nextScanFrames = GameLogicRandomValue( 0, 3 );
				m_bestTargetID = INVALID_ID;
				m_inRange = false;	// or every later out-of-range target rescans and drops too
				if( !m_nextScanFrames )
				{
					scanClosestTarget();
					m_nextScanFrames = data->m_scanFrames;
					target = NULL; //Set target to NULL so we don't shoot at it (might be out of range)
				}
			}
			else
			{
				//Not in range
				m_inRange = false;
			}
		}
	}
	
	if( m_nextShotAvailableInFrames > 0 )
	{
		//We can't fire this frame.
		m_nextShotAvailableInFrames--;
		return;
	}

	//Fire control!
	if( target )
	{
		AIUpdateInterface *ai = self->getAI();
		if( ai )
		{
			WeaponBonus bonus;
			bonus.clear();
			if( (ai->isIdle() || ai->isBusy()) && m_moveRange > 0.0f &&
					ThePartitionManager->getDistanceSquared( self, target, FROM_CENTER_2D ) > sqr( m_weaponTemplate->getAttackRange( bonus ) ) )
			{
				// cleaning an area, with the hazard out of reach: drive up to it first, since the attack's own
				// approach never moved the unit and it stood shooting at nothing until the area was given up.
				// Out of the busy state first: an AI move given to a busy unit is only a temporary state laid
				// over it, ordered again every frame, and the unit crept at a step a frame
				ai->aiIdle( CMD_FROM_AI );
				ai->aiMoveToPosition( target->getPosition(), CMD_FROM_AI );
			}
			else if( ai->isIdle() || ai->isBusy() )
			{
				// lock it just till the weapon is empty or the attack is "done"
				self->setWeaponLock( data->m_weaponSlot, LOCKED_TEMPORARILY );
				ai->aiAttackObject( target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
Object* CleanupHazardUpdate::scanClosestTarget()
{
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *me = getObject();
	Object *bestTargetInRange = NULL;
	m_bestTargetID = INVALID_ID;

	PartitionFilterAcceptByKindOf kindFilter(MAKE_KINDOF_MASK(KINDOF_CLEANUP_HAZARD), KINDOFMASK_NONE);
	PartitionFilterSameMapStatus filterMapStatus(getObject());
	PartitionFilter* filters[] = { &kindFilter, &filterMapStatus, NULL };

	if( m_moveRange > 0.0f )
	{
		//Look for targets around the target position only (but add scan range and move range).
		//This case only happens when we are performing a cleanup area command.
		bestTargetInRange = ThePartitionManager->getClosestObject( &m_pos, data->m_scanRange + m_moveRange, FROM_CENTER_2D, filters );
	}
	else
	{
		//Look for targets near me -- passive default.
		bestTargetInRange = ThePartitionManager->getClosestObject( me->getPosition(), data->m_scanRange, FROM_CENTER_2D, filters );
	}

	if( bestTargetInRange ) 
	{
		m_bestTargetID = bestTargetInRange->getID();
	}
	return bestTargetInRange;
}

//-------------------------------------------------------------------------------------------------
/** The area is done with: back on guard if a cleanup it started took it off guard, otherwise out of
	the busy state the area kept it in, which has no way out of its own. Left busy, it never read as
	idle again, so scripts waiting on it stalled and it never noticed another hazard. goHome walks it
	back to where the area started, for an area given up away from there. */
void CleanupHazardUpdate::finishArea( const char *why, Bool goHome )
{
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAI();
	DEBUG_LOG(("CLEANUP frame %d '%s' %d %s%s\n", TheGameLogic->getFrame(), obj->getTemplate()->getName().str(), obj->getID(),
		why, m_resumeGuard ? ", back on guard" : ""));
	m_moveRange = 0.0f;
	m_returning = FALSE;
	m_approaches = 0;
	if( m_resumeGuard )
	{
		m_resumeGuard = FALSE;
		ai->aiGuardPosition( &m_pos, (GuardMode)m_resumeGuardMode, CMD_FROM_AI );
	}
	else if( goHome )
		ai->aiMoveToPosition( &m_pos, CMD_FROM_AI );
	else
		ai->aiIdle( CMD_FROM_AI );
}

//-------------------------------------------------------------------------------------------------
/** How far past its own scan range a cleaner standing idle or on guard goes for a hazard by itself.
	The scan range is the weapon's reach, so before this an Ambulance cleaned only what it was parked
	on and watched a toxin field a truck's length away. With the Ambulance's 100 it notices up to 300,
	the reach its cleanup ability is given around the point it is sent to. */
static const Real AUTO_CLEANUP_MOVE_RANGE = 200.0f;

/** Idle, or on a guard order, with a hazard in reach: clean the area round where it stands the way the
	cleanup ability does, which brings it back here when nothing is left, and guard here again if it
	was guarding. Any other order, the player's or a script's, is left alone, and one given while it is
	cleaning ends the cleanup as it ends the ability's. */
void CleanupHazardUpdate::startAutoCleanup()
{
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAI();
	if( !ai || obj->isContained() )
		return;
	const Bool guarding = ai->getCurrentStateID() == AI_GUARD;
	if( !guarding && !ai->isIdle() )
		return;

	PartitionFilterAcceptByKindOf kindFilter(MAKE_KINDOF_MASK(KINDOF_CLEANUP_HAZARD), KINDOFMASK_NONE);
	PartitionFilterSameMapStatus filterMapStatus(obj);
	PartitionFilter* filters[] = { &kindFilter, &filterMapStatus, NULL };
	if( !ThePartitionManager->getClosestObject( obj->getPosition(), data->m_scanRange + AUTO_CLEANUP_MOVE_RANGE, FROM_CENTER_2D, filters ) )
		return;

	m_pos = *obj->getPosition();
	m_moveRange = AUTO_CLEANUP_MOVE_RANGE;
	// ponytail: an object guard comes back as a guard on the spot it stood, since EA's guard target type
	// keeps the first guard order ever given rather than the current one
	m_resumeGuard = guarding;
	m_resumeGuardMode = ai->getGuardMode();
	ai->aiBusy( CMD_FROM_AI );
	DEBUG_LOG(("CLEANUP frame %d '%s' %d of player %d goes to clean by itself round (%.0f,%.0f)%s\n", TheGameLogic->getFrame(),
		obj->getTemplate()->getName().str(), obj->getID(), obj->getControllingPlayer() ? obj->getControllingPlayer()->getPlayerIndex() : -1,
		m_pos.x, m_pos.y, guarding ? ", off guard" : ""));
}

//-------------------------------------------------------------------------------------------------
//This allows the unit to cleanup an area until clean, then the AI goes idle.
//-------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::setCleanupAreaParameters( const Coord3D *pos, Real range )
{
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAI();

	//Setting the move range triggers that passive conditions for it to be allowed to move
	//from the specified position.
	m_moveRange = range;
	m_pos = *pos;
	m_resumeGuard = FALSE;
	m_returning = FALSE;
	m_approaches = 0;

	if( ai )
	{
		//CMD_FROM_AI important because it'll abort when other types are used -- like if a player
		//or script orders the unit to do something else, we need a way to cancel this passive
		//situation.
		ai->aiMoveToPosition( pos, CMD_FROM_AI ); 
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: the guard a cleanup it started by itself gives back, the walk back, the drives toward a hazard */
// ------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// best target id
	xfer->xferObjectID( &m_bestTargetID );

	// in range
	xfer->xferBool( &m_inRange );

	// next scan frames
	xfer->xferInt( &m_nextScanFrames );

	// next shot available in frames
	xfer->xferInt( &m_nextShotAvailableInFrames );

	// don't need to save weapon template, it's retrieved onObjectCreated
	// const WeaponTemplate *m_weaponTemplate;

	// pos
	xfer->xferCoord3D( &m_pos );

	// move range
	xfer->xferReal( &m_moveRange );

	if( version >= 2 )
	{
		xfer->xferBool( &m_resumeGuard );
		xfer->xferInt( &m_resumeGuardMode );
		xfer->xferBool( &m_returning );
		xfer->xferInt( &m_approaches );
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
