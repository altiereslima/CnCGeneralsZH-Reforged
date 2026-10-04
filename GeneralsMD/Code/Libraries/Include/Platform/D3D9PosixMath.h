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
** Direct3D 9's vector and matrix, off Windows (decision 7, phase A1): the two D3D9 structures that
** reach beyond the renderer.  D3DX's D3DXVECTOR3 and D3DXMATRIX derive from them, as they do on
** Windows, and d3dx9math.h is included by GameEngine (BezierSegment steers a shell with a
** D3DXMATRIX), which has no business with the rest of Direct3D.  So they have a header of their
** own: Platform/D3D9Posix.h includes it for the renderer, and d3dx9math.h includes it and nothing
** else of D3D9's.
**
** Written from Direct3D 9's published layout and checked with the rest of D3D9Posix.h by
** Tools/d3d9posix_check.py.  Plain floats only, so it needs no header of its own.
*/

#ifndef PLATFORM_D3D9POSIXMATH_H
#define PLATFORM_D3D9POSIXMATH_H

#if defined(_WIN32) && !defined(D3D9POSIX_CHECKER)
#error "Platform/D3D9PosixMath.h is the POSIX side of Direct3D 9; Windows uses the SDK's d3d9types.h"
#endif

struct D3DVECTOR		{ float x, y, z; };

struct D3DMATRIX
{
	union
	{
		struct
		{
			float _11, _12, _13, _14;
			float _21, _22, _23, _24;
			float _31, _32, _33, _34;
			float _41, _42, _43, _44;
		};
		float m[4][4];
	};
};

static_assert(sizeof(D3DVECTOR) == 12, "D3DVECTOR is three floats");
static_assert(sizeof(D3DMATRIX) == 64, "D3DMATRIX is sixteen floats");

#endif // PLATFORM_D3D9POSIXMATH_H
