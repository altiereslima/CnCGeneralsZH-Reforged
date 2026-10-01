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

// FILE: DrawnPath.cpp //////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "Common/DrawnPath.h"
#include "GameLogic/Object.h"

//-------------------------------------------------------------------------------------------------
void buildPathArcLengths( const std::vector<Coord3D>& path, std::vector<Real>& arc )
{
	arc.clear();
	if (path.empty())
		return;

	arc.push_back( 0.0f );
	for (Int i = 1; i < (Int)path.size(); i++)
	{
		const Real dx = path[ i ].x - path[ i - 1 ].x;
		const Real dy = path[ i ].y - path[ i - 1 ].y;
		arc.push_back( arc[ i - 1 ] + sqrtf( dx * dx + dy * dy ) );
	}
}

//-------------------------------------------------------------------------------------------------
Real distanceAlongPath( const std::vector<Coord3D>& path, const std::vector<Real>& arc,
												Real x, Real y )
{
	Real best = 0.0f;
	Real bestDistSqr = -1.0f;

	for (Int i = 1; i < (Int)path.size(); i++)
	{
		const Real sx = path[ i - 1 ].x;
		const Real sy = path[ i - 1 ].y;
		const Real dx = path[ i ].x - sx;
		const Real dy = path[ i ].y - sy;
		const Real segLenSqr = dx * dx + dy * dy;

		Real t = 0.0f;
		if (segLenSqr > 0.0f)
		{
			t = ((x - sx) * dx + (y - sy) * dy) / segLenSqr;
			if (t < 0.0f)
				t = 0.0f;
			else if (t > 1.0f)
				t = 1.0f;
		}

		const Real px = sx + dx * t;
		const Real py = sy + dy * t;
		const Real distSqr = (x - px) * (x - px) + (y - py) * (y - py);
		if (bestDistSqr < 0.0f || distSqr < bestDistSqr)
		{
			bestDistSqr = distSqr;
			best = arc[ i - 1 ] + sqrtf( segLenSqr ) * t;
		}
	}

	return best;
}

//-------------------------------------------------------------------------------------------------
void pointAlongPath( const std::vector<Coord3D>& path, const std::vector<Real>& arc,
										 Real dist, Coord3D *out )
{
	Int i = 1;
	while (i < (Int)path.size() - 1 && arc[ i ] < dist)
		i++;

	const Real segLen = arc[ i ] - arc[ i - 1 ];
	const Real t = (segLen > 0.0f) ? ((dist - arc[ i - 1 ]) / segLen) : 0.0f;
	out->x = path[ i - 1 ].x + (path[ i ].x - path[ i - 1 ].x) * t;
	out->y = path[ i - 1 ].y + (path[ i ].y - path[ i - 1 ].y) * t;
	out->z = 0.0f;
}

//-------------------------------------------------------------------------------------------------
void orderAlongPath( std::vector<Object *>& movers, const std::vector<Coord3D>& path,
										 const std::vector<Real>& arc )
{
	const Int count = movers.size();

	// the keys are worked out once: the sort below asks for them n-squared times, and each one is a
	// walk of the whole curve
	std::vector<Real> keys;
	keys.reserve( count );
	for (Int i = 0; i < count; i++)
		keys.push_back( distanceAlongPath( path, arc,
																			 movers[ i ]->getPosition()->x,
																			 movers[ i ]->getPosition()->y ) );

	// Insertion sort, keys and units together: the selection is a few dozen objects at most, and
	// the comparator is a total order, so the answer does not depend on the order they came in.
	for (Int i = 1; i < count; i++)
	{
		Object *held = movers[ i ];
		const Real heldKey = keys[ i ];

		Int j = i - 1;
		while (j >= 0)
		{
			if (keys[ j ] < heldKey || (keys[ j ] == heldKey && movers[ j ]->getID() < held->getID()))
				break;
			movers[ j + 1 ] = movers[ j ];
			keys[ j + 1 ] = keys[ j ];
			j--;
		}

		movers[ j + 1 ] = held;
		keys[ j + 1 ] = heldKey;
	}
}

//-------------------------------------------------------------------------------------------------
void orderAlongPath( std::vector<AttackAssignSlot>& slots, const std::vector<Coord3D>& path,
										 const std::vector<Real>& arc )
{
	const Int count = (Int)slots.size();

	std::vector<Real> keys;
	keys.reserve( count );
	for( Int i = 0; i < count; i++ )
		keys.push_back( distanceAlongPath( path, arc, slots[ i ].x, slots[ i ].y ) );

	// Same insertion sort as the unit list. A few dozen guns, a few hundred enemies on a stroke,
	// and the comparator is a total order, so the answer does not depend on who was found first.
	for( Int i = 1; i < count; i++ )
	{
		AttackAssignSlot held = slots[ i ];
		const Real heldKey = keys[ i ];

		Int j = i - 1;
		while( j >= 0 )
		{
			if( keys[ j ] < heldKey || (keys[ j ] == heldKey && slots[ j ].id < held.id) )
				break;
			slots[ j + 1 ] = slots[ j ];
			keys[ j + 1 ] = keys[ j ];
			j--;
		}

		slots[ j + 1 ] = held;
		keys[ j + 1 ] = heldKey;
	}
}

//-------------------------------------------------------------------------------------------------
/** Upper half, +x ray included, is 0. Lower half, -x ray included, is 1. The origin counts as
	* the +x ray so a man standing on the centre sorts with the start of the sweep. */
//-------------------------------------------------------------------------------------------------
static Int attackHalfPlane( Real vx, Real vy )
{
	if( vy > 0.0f )
		return 0;
	if( vy < 0.0f )
		return 1;
	return (vx >= 0.0f) ? 0 : 1;
}

//-------------------------------------------------------------------------------------------------
/** Strict order around the origin the vectors were measured from. Cross product, not an angle,
	* so nothing transcendental is in it and the same coordinates always sort the same way. */
//-------------------------------------------------------------------------------------------------
static Bool attackStandsBefore( Real ax, Real ay, ObjectID aId, Real bx, Real by, ObjectID bId )
{
	const Int halfA = attackHalfPlane( ax, ay );
	const Int halfB = attackHalfPlane( bx, by );
	if( halfA != halfB )
		return halfA < halfB;

	const Real cross = ax * by - ay * bx;
	if( cross > 0.0f )
		return TRUE;
	if( cross < 0.0f )
		return FALSE;

	const Real distA = ax * ax + ay * ay;
	const Real distB = bx * bx + by * by;
	if( distA < distB )
		return TRUE;
	if( distA > distB )
		return FALSE;

	return aId < bId;
}

//-------------------------------------------------------------------------------------------------
void orderAroundPoint( std::vector<AttackAssignSlot>& slots, Real centerX, Real centerY )
{
	const Int count = (Int)slots.size();

	// Insertion sort. A selection is a few dozen, a circle's targets a few hundred, and the
	// comparator is a total order, so the answer does not depend on the order they were found in.
	for( Int i = 1; i < count; i++ )
	{
		AttackAssignSlot held = slots[ i ];
		const Real heldX = held.x - centerX;
		const Real heldY = held.y - centerY;

		Int j = i - 1;
		while( j >= 0 )
		{
			const Real sx = slots[ j ].x - centerX;
			const Real sy = slots[ j ].y - centerY;
			if( !attackStandsBefore( heldX, heldY, held.id, sx, sy, slots[ j ].id ) )
				break;
			slots[ j + 1 ] = slots[ j ];
			j--;
		}

		slots[ j + 1 ] = held;
	}
}

//-------------------------------------------------------------------------------------------------
void assignAttacks( Int attackerCount, Int targetCount, std::vector<AttackAssignPair>& pairs )
{
	pairs.clear();
	if( attackerCount < 1 || targetCount < 1 )
		return;

	pairs.reserve( (attackerCount > targetCount) ? attackerCount : targetCount );

	// Enough guns that nobody needs a second target. Attacker i takes target
	// (i * M + M/2) / N, which lays the shares in contiguous blocks and puts a leftover
	// share in the middle block. 10 guns on 3 targets is 3, 4, 3.
	if( attackerCount >= targetCount )
	{
		for( Int i = 0; i < attackerCount; i++ )
		{
			AttackAssignPair pair;
			pair.attacker = i;
			pair.target = (i * targetCount + targetCount / 2) / attackerCount;
			pairs.push_back( pair );
		}
		return;
	}

	// More targets than guns. Attacker a's run is the targets from start(a) up to start(a+1),
	// start(a) = (a * M + N/2) / N. 3 guns on 10 targets queue 3, 4 and 3, in that spatial order.
	for( Int attacker = 0; attacker < attackerCount; attacker++ )
	{
		const Int begin = (attacker * targetCount + attackerCount / 2) / attackerCount;
		const Int end = ((attacker + 1) * targetCount + attackerCount / 2) / attackerCount;
		for( Int target = begin; target < end; target++ )
		{
			AttackAssignPair pair;
			pair.attacker = attacker;
			pair.target = target;
			pairs.push_back( pair );
		}
	}
}
