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
// The arithmetic half of D3DX, shared by every platform that cannot bind d3dx9_43.dll: see d3dx9portable.cpp.
// Each is the D3DX function of the same name, with D3DX's semantics: D3DXPortable_Matrix_Inverse returns
// null for a singular matrix and leaves the output alone.  None reaches the simulation.

#pragma once

#ifndef D3DX9PORTABLE_H
#define D3DX9PORTABLE_H

#include "d3dx9math.h"

D3DXMATRIX * D3DXPortable_Matrix_Inverse(D3DXMATRIX * out, float * determinant, const D3DXMATRIX * matrix);
D3DXMATRIX * D3DXPortable_Matrix_Multiply(D3DXMATRIX * out, const D3DXMATRIX * left, const D3DXMATRIX * right);
D3DXMATRIX * D3DXPortable_Matrix_Transpose(D3DXMATRIX * out, const D3DXMATRIX * matrix);
D3DXMATRIX * D3DXPortable_Matrix_Scaling(D3DXMATRIX * out, float x, float y, float z);
D3DXMATRIX * D3DXPortable_Matrix_Translation(D3DXMATRIX * out, float x, float y, float z);
D3DXMATRIX * D3DXPortable_Matrix_Rotation_Z(D3DXMATRIX * out, float angle);
D3DXVECTOR4 * D3DXPortable_Vec3_Transform(D3DXVECTOR4 * out, const D3DXVECTOR3 * vector, const D3DXMATRIX * matrix);
unsigned int D3DXPortable_FVF_Vertex_Size(unsigned int fvf);

#endif // D3DX9PORTABLE_H
