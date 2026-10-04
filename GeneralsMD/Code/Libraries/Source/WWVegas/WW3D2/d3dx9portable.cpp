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
// The D3DX functions whose bodies are arithmetic and nothing else: the renderer's matrix functions and the
// FVF vertex size.  macOS and Linux use them as D3DX itself (d3dx9posix.cpp forwards to them), and Windows on
// ARM64, which has no d3dx9_43.dll (Microsoft shipped it for x86 and x64 only), binds them in the DLL's
// place (d3dx9runtime.cpp).  Moved here unchanged from d3dx9posix.cpp, where they were written (phase A1);
// Tests/test_d3dx9portable_oracle.cpp holds them against the DLL on Windows x64.

#include "d3dx9runtime.h"
#include "d3dx9math.h"
#include "d3dx9portable.h"

#include <math.h>

//-------------------------------------------------------------------------------------------------
// The matrix functions: D3D's row-vector convention, a vector times the matrix.
//-------------------------------------------------------------------------------------------------


D3DXMATRIX * D3DXPortable_Matrix_Multiply(D3DXMATRIX * out, const D3DXMATRIX * left, const D3DXMATRIX * right)
{
	// Into a temporary first: the engine multiplies in place (D3DXMATRIX::operator*=, W3DTreeBuffer).
	D3DXMATRIX product;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			product.m[row][column] = left->m[row][0] * right->m[0][column]
				+ left->m[row][1] * right->m[1][column]
				+ left->m[row][2] * right->m[2][column]
				+ left->m[row][3] * right->m[3][column];
		}
	}
	*out = product;
	return out;
}

D3DXMATRIX * D3DXPortable_Matrix_Transpose(D3DXMATRIX * out, const D3DXMATRIX * matrix)
{
	D3DXMATRIX transposed;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			transposed.m[row][column] = matrix->m[column][row];
		}
	}
	*out = transposed;
	return out;
}

D3DXMATRIX * D3DXPortable_Matrix_Scaling(D3DXMATRIX * out, float x, float y, float z)
{
	D3DXMatrixIdentity(out);
	out->_11 = x;
	out->_22 = y;
	out->_33 = z;
	return out;
}

D3DXMATRIX * D3DXPortable_Matrix_Translation(D3DXMATRIX * out, float x, float y, float z)
{
	D3DXMatrixIdentity(out);
	out->_41 = x;
	out->_42 = y;
	out->_43 = z;
	return out;
}

D3DXMATRIX * D3DXPortable_Matrix_Rotation_Z(D3DXMATRIX * out, float angle)
{
	const float c = cosf(angle);
	const float s = sinf(angle);
	D3DXMatrixIdentity(out);
	out->_11 = c;
	out->_12 = s;
	out->_21 = -s;
	out->_22 = c;
	return out;
}

D3DXVECTOR4 * D3DXPortable_Vec3_Transform(D3DXVECTOR4 * out, const D3DXVECTOR3 * vector, const D3DXMATRIX * matrix)
{
	// (x, y, z, 1) times the matrix, into locals first in case out overlaps the vector.
	const float x = vector->x;
	const float y = vector->y;
	const float z = vector->z;
	D3DXVECTOR4 result;
	result.x = x * matrix->_11 + y * matrix->_21 + z * matrix->_31 + matrix->_41;
	result.y = x * matrix->_12 + y * matrix->_22 + z * matrix->_32 + matrix->_42;
	result.z = x * matrix->_13 + y * matrix->_23 + z * matrix->_33 + matrix->_43;
	result.w = x * matrix->_14 + y * matrix->_24 + z * matrix->_34 + matrix->_44;
	*out = result;
	return out;
}

D3DXMATRIX * D3DXPortable_Matrix_Inverse(D3DXMATRIX * out, float * determinant, const D3DXMATRIX * matrix)
{
	// The adjugate over the determinant, by the 2x2 minors of the top and bottom row pairs.
	const float (*a)[4] = matrix->m;
	const float s0 = a[0][0] * a[1][1] - a[1][0] * a[0][1];
	const float s1 = a[0][0] * a[1][2] - a[1][0] * a[0][2];
	const float s2 = a[0][0] * a[1][3] - a[1][0] * a[0][3];
	const float s3 = a[0][1] * a[1][2] - a[1][1] * a[0][2];
	const float s4 = a[0][1] * a[1][3] - a[1][1] * a[0][3];
	const float s5 = a[0][2] * a[1][3] - a[1][2] * a[0][3];
	const float c5 = a[2][2] * a[3][3] - a[3][2] * a[2][3];
	const float c4 = a[2][1] * a[3][3] - a[3][1] * a[2][3];
	const float c3 = a[2][1] * a[3][2] - a[3][1] * a[2][2];
	const float c2 = a[2][0] * a[3][3] - a[3][0] * a[2][3];
	const float c1 = a[2][0] * a[3][2] - a[3][0] * a[2][2];
	const float c0 = a[2][0] * a[3][1] - a[3][0] * a[2][1];

	const float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
	if (determinant != NULL) {
		*determinant = det;
	}
	if (det == 0.0f) {
		return NULL;	// singular: D3DX leaves out alone and returns null
	}
	const float r = 1.0f / det;

	D3DXMATRIX inverse;
	inverse.m[0][0] = ( a[1][1] * c5 - a[1][2] * c4 + a[1][3] * c3) * r;
	inverse.m[0][1] = (-a[0][1] * c5 + a[0][2] * c4 - a[0][3] * c3) * r;
	inverse.m[0][2] = ( a[3][1] * s5 - a[3][2] * s4 + a[3][3] * s3) * r;
	inverse.m[0][3] = (-a[2][1] * s5 + a[2][2] * s4 - a[2][3] * s3) * r;
	inverse.m[1][0] = (-a[1][0] * c5 + a[1][2] * c2 - a[1][3] * c1) * r;
	inverse.m[1][1] = ( a[0][0] * c5 - a[0][2] * c2 + a[0][3] * c1) * r;
	inverse.m[1][2] = (-a[3][0] * s5 + a[3][2] * s2 - a[3][3] * s1) * r;
	inverse.m[1][3] = ( a[2][0] * s5 - a[2][2] * s2 + a[2][3] * s1) * r;
	inverse.m[2][0] = ( a[1][0] * c4 - a[1][1] * c2 + a[1][3] * c0) * r;
	inverse.m[2][1] = (-a[0][0] * c4 + a[0][1] * c2 - a[0][3] * c0) * r;
	inverse.m[2][2] = ( a[3][0] * s4 - a[3][1] * s2 + a[3][3] * s0) * r;
	inverse.m[2][3] = (-a[2][0] * s4 + a[2][1] * s2 - a[2][3] * s0) * r;
	inverse.m[3][0] = (-a[1][0] * c3 + a[1][1] * c1 - a[1][2] * c0) * r;
	inverse.m[3][1] = ( a[0][0] * c3 - a[0][1] * c1 + a[0][2] * c0) * r;
	inverse.m[3][2] = (-a[3][0] * s3 + a[3][1] * s1 - a[3][2] * s0) * r;
	inverse.m[3][3] = ( a[2][0] * s3 - a[2][1] * s1 + a[2][2] * s0) * r;
	*out = inverse;
	return out;
}

//-------------------------------------------------------------------------------------------------
// The FVF vertex size.
//-------------------------------------------------------------------------------------------------

unsigned int D3DXPortable_FVF_Vertex_Size(unsigned int fvf)
{
	unsigned int size = 0;
	// D3DFVF_XYZW adds nothing: Microsoft's d3dx9_43 D3DXGetFVFVertexSize has no case for it and sizes
	// only the rest (0x4002 gives 0 bytes, 0x4102 gives 8).  The DLL is the reference, so this answers
	// the same; test_d3dx9portable_oracle holds it to the DLL.  The engine builds no XYZW format.
	switch (fvf & D3DFVF_POSITION_MASK) {
		case D3DFVF_XYZ:	size = 12; break;
		case D3DFVF_XYZRHW:	size = 16; break;
		case D3DFVF_XYZB1:	size = 16; break;
		case D3DFVF_XYZB2:	size = 20; break;
		case D3DFVF_XYZB3:	size = 24; break;
		case D3DFVF_XYZB4:	size = 28; break;
		case D3DFVF_XYZB5:	size = 32; break;
		default:			break;	// no position
	}
	if (fvf & D3DFVF_NORMAL)	size += 12;
	if (fvf & D3DFVF_PSIZE)		size += 4;
	if (fvf & D3DFVF_DIFFUSE)	size += 4;
	if (fvf & D3DFVF_SPECULAR)	size += 4;

	// Each set's two format bits, from bit 16 up: 0 is two floats, 1 three, 2 four, 3 one
	// (D3DFVF_TEXTUREFORMAT2, 3, 4 and 1).
	static const unsigned int TEXCOORD_BYTES[4] = { 8, 12, 16, 4 };
	const unsigned int sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (unsigned int set = 0; set < sets && set < 8; ++set) {
		size += TEXCOORD_BYTES[(fvf >> (16 + set * 2)) & 3];
	}
	return size;
}
