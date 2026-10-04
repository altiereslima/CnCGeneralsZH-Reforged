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
/*
 * terrain_grid.h - the sample grid behind T1's terrain golden, shared by the oracle (the original W3D
 * code, Tests/terrain_oracle.cpp) and the test of the moved code (Tests/test_terrain_golden.cpp), so
 * the two cannot drift in what they sample or how they print it.
 *
 * A Sampler provides:
 *   Real height( Real x, Real y, Coord3D *normal )   getHeightMapHeight
 *   Bool cliff( Real x, Real y )                     isCliffCell
 *   Real maxcell( Real x, Real y )                   getMaxCellHeight
 *   Bool sight( const Coord3D &a, const Coord3D &b ) isClearLineOfSight
 *
 * The grid is 7.25 world units apart, from 60 units outside the map to 60 past its far edge, so the
 * border and the off-map clamps are sampled too.  Output, into a std::string: the map's line, then one
 * FNV-1a hash and a count per kind of sample.  With dump, every sample is printed as well.
 */

#pragma once

#include <stdio.h>
#include <string.h>
#include <string>

namespace terrain_grid {

inline unsigned fnv( unsigned hash, const void *data, size_t size )
{
	const unsigned char *p = (const unsigned char *)data;
	for (size_t i = 0; i < size; ++i)
		hash = (hash ^ p[i]) * 0x01000193u;
	return hash;
}

inline unsigned bitsOf( Real value )
{
	unsigned bits;
	memcpy( &bits, &value, sizeof( bits ) );
	return bits;
}

struct Section { const char *name; unsigned hash; unsigned count; };

inline void note( Section &s, unsigned value, bool dump, Real x, Real y )
{
	s.hash = fnv( s.hash, &value, sizeof( value ) );
	++s.count;
	if (dump)
		printf( "%s %08x %08x %08x\n", s.name, bitsOf( x ), bitsOf( y ), value );
}

/** width, height and border are the map's, in cells; mapXYFactor is MAP_XY_FACTOR. */
template <class Sampler>
std::string run( Sampler &sampler, int width, int height, int border, int boundaries, Real maxHeight,
								 Real mapXYFactor, bool dump )
{
	char line[ 128 ];
	snprintf( line, sizeof( line ), "map %d x %d border %d boundaries %d maxheight %08x\n", width, height,
		border, boundaries, bitsOf( maxHeight ) );
	std::string out = line;

	Section h = { "height", 0x811C9DC5u, 0 }, bare = { "bare", 0x811C9DC5u, 0 },
		cliff = { "cliff", 0x811C9DC5u, 0 }, maxcell = { "maxcell", 0x811C9DC5u, 0 },
		sight = { "sight", 0x811C9DC5u, 0 };

	const Real step = 7.25f, margin = 60.0f;
	const Real extentX = (Real)(width - 2 * border) * mapXYFactor;
	const Real extentY = (Real)(height - 2 * border) * mapXYFactor;
	const int columns = (int)((extentX + 2 * margin) / step) + 1;
	const int rows = (int)((extentY + 2 * margin) / step) + 1;
	for (int j = 0; j < rows; ++j)
	{
		const Real y = (Real)j * step - margin;
		for (int i = 0; i < columns; ++i)
		{
			const Real x = (Real)i * step - margin;
			Coord3D normal;
			const Real z = sampler.height( x, y, &normal );
			note( h, bitsOf( z ), dump, x, y );
			note( h, bitsOf( normal.x ), dump, x, y );
			note( h, bitsOf( normal.y ), dump, x, y );
			note( h, bitsOf( normal.z ), dump, x, y );
			note( bare, bitsOf( sampler.height( x, y, NULL ) ), dump, x, y );
			note( cliff, sampler.cliff( x, y ) ? 1u : 0u, dump, x, y );
			note( maxcell, bitsOf( sampler.maxcell( x, y ) ), dump, x, y );

			// Lines of sight from every fifth point: to a point further along both axes, eyes a unit
			// and a half above the ground at one end and three units above at the other.
			if (i % 5 == 0 && j % 5 == 0)
			{
				Coord3D from, to;
				from.x = x; from.y = y; from.z = z + 1.5f;
				to.x = x + 237.5f; to.y = y + 113.25f;
				to.z = sampler.height( to.x, to.y, NULL ) + 3.0f;
				note( sight, sampler.sight( from, to ) ? 1u : 0u, dump, x, y );
				note( sight, sampler.sight( to, from ) ? 1u : 0u, dump, x, y );
			}
		}
	}

	const Section *all[] = { &h, &bare, &cliff, &maxcell, &sight };
	for (const Section *s : all)
	{
		snprintf( line, sizeof( line ), "%s %08x %u\n", s->name, s->hash, s->count );
		out += line;
	}
	return out;
}

}  // namespace terrain_grid
