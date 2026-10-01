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

// FILE: DrawnPath.h ////////////////////////////////////////////////////////////////////////////
//
// The curve the player traces with the left button after arming a move, and the arithmetic that turns it into
// standing room.  Both sides of the engine need the same answers: the logic to decide who goes
// where, the client to draw it while the line is still being dragged.  Written once here so the
// two pictures cannot drift apart.
//
///////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _H_DrawnPath
#define _H_DrawnPath

#include "Lib/BaseType.h"
#include "Common/STLTypedefs.h"

class Object;

/// running length of the curve up to each of its points; arc[0] is zero, arc.back() is the whole
extern void buildPathArcLengths( const std::vector<Coord3D>& path, std::vector<Real>& arc );

/// how far along the curve the point on it nearest to (x,y) sits
extern Real distanceAlongPath( const std::vector<Coord3D>& path, const std::vector<Real>& arc,
															 Real x, Real y );

/// the point that far along the curve; the ground height is not filled in
extern void pointAlongPath( const std::vector<Coord3D>& path, const std::vector<Real>& arc,
														Real dist, Coord3D *out );

/**
 * Put these units in the order the stations are handed out: whoever is nearest the start of the
 * curve takes the first one, so nobody crosses anybody on the way in.  The object id breaks a tie,
 * which makes the order a total one - the same set of units sorts the same way on every machine
 * and in the client's own picture of it, whatever order they arrived in.
 */
extern void orderAlongPath( std::vector<Object *>& movers, const std::vector<Coord3D>& path,
														const std::vector<Real>& arc );

/**
 * One man or one target standing somewhere, named, so the attack circle can line both sides up
 * without the sort having to know what an object is. The id is the last tie, which makes the
 * order a total one.
 */
struct AttackAssignSlot
{
	ObjectID id;
	Real x;
	Real y;
};

/// The same order as the unit list above, for a position and an id. Nearest the start of the
/// curve first, and the id breaks a tie. The attack line stands both the guns and the enemies
/// this way.
extern void orderAlongPath( std::vector<AttackAssignSlot>& slots, const std::vector<Coord3D>& path,
														const std::vector<Real>& arc );

/// Who shoots, and which target in the sorted target list. Both indexes are into lists already
/// sorted the same way, around a circle or along a stroke.
struct AttackAssignPair
{
	Int attacker;
	Int target;
};

/// Stand these in the order they sit around (centerX, centerY), counter-clockwise from the +x
/// ray. No angle is computed: the half-plane, then the cross product, then the distance from the
/// centre, then the id. A man on the centre itself is the nearest thing on the +x ray.
extern void orderAroundPoint( std::vector<AttackAssignSlot>& slots, Real centerX, Real centerY );

/// Pair attackers with targets so the counts differ by one at most. Both counts are the lengths
/// of lists already sorted in the order the gesture drew. With at least as many attackers as targets,
/// each attacker takes one target and the extras share, in contiguous blocks. With more targets
/// than attackers, every target is still shot and each attacker queues a contiguous run of them.
/// Either side empty writes nothing.
extern void assignAttacks( Int attackerCount, Int targetCount, std::vector<AttackAssignPair>& pairs );

#endif // _H_DrawnPath
