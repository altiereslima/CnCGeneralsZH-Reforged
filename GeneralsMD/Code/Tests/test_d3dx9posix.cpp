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

// The POSIX D3DX (WW3D2/d3dx9posix.cpp, decision 7 phase A1): the renderer's matrix functions and the
// FVF vertex size, which Windows takes from d3dx9_43.dll.
//
// What it checks:
//   - Get_FVF_Vertex_Size against sizeof each of dx8fvf.h's vertex structures - the layouts
//     the engine writes its vertex buffers through, so an independent answer, for twelve of them (the
//     thirteenth disagrees with its own FVF and is summed by hand) - and a few hand sums
//     for the parts no structure uses (RHW, blend weights, point size, specular, one-float sets);
//   - the matrix product against a double-precision reference, and in place on either side;
//   - the inverse: M times its inverse is the identity for a view-like, a projection-like and a
//     general matrix; diag(2,3,4,5)'s determinant is 120 exactly; an exactly singular matrix gives null and
//     leaves the output alone;
//   - transpose, scaling, translation and rotation about z through D3DXVec3Transform, on points
//     whose images are known (the matrices are the SDK's documented ones, D3D's row-vector form);
//   - that binding succeeds, a texture call on a null device is D3DERR_INVALIDCALL, and error names
//     come back;
//   - the stub "assembler" (A3e): a version token from the text's first word, the text back out of
//     its comment token byte for byte (the water's river program, an odd length, a leading comment),
//     the end token where the length says, and text naming no version refused.
//
// WHAT THIS DOES NOT PROVE:
//   - That the rounding matches d3dx9_43.dll's.  Tolerances here are 1e-5 relative; the DLL was
//     not run.  Tests/d3dx_oracle could compare these bodies against the DLL's exports, and that
//     is left for A3, where the matrices reach the picture.  None of them reaches the simulation:
//     BezierSegment uses only D3DXVec4Transform and D3DXVec4Dot (d3dxportable.h).
//   - Anything about the texture functions beyond their failure codes: they are A2's.

#include "d3dx9runtime.h"
#include "d3dx9math.h"
#include "d3dx9posix.h"
#include "dx8fvf.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static bool near_value(float actual, double expected)
{
	return fabs(actual - expected) <= 1e-5 * (1.0 + fabs(expected));
}

static bool is_identity(const D3DXMATRIX & m)
{
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			if (!near_value(m.m[r][c], r == c ? 1.0 : 0.0)) return false;
	return true;
}

static D3DXMATRIX sample(float seed)
{
	D3DXMATRIX m;
	for (int i = 0; i < 16; ++i) {
		m.m[i / 4][i % 4] = sinf(seed + 1.7f * i) * 3.0f + (i % 5 == 0 ? 4.0f : 0.0f);
	}
	return m;
}

static void check_fvf_sizes()
{
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZ) == sizeof(VertexFormatXYZ));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZN) == sizeof(VertexFormatXYZN));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZNUV1) == sizeof(VertexFormatXYZNUV1));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZNUV2) == sizeof(VertexFormatXYZNUV2));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZNDUV1) == sizeof(VertexFormatXYZNDUV1));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZNDUV2) == sizeof(VertexFormatXYZNDUV2));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZDUV1) == sizeof(VertexFormatXYZDUV1));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZDUV2) == sizeof(VertexFormatXYZDUV2));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZUV1) == sizeof(VertexFormatXYZUV1));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZUV2) == sizeof(VertexFormatXYZUV2));
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZNDUV1TG3) == sizeof(VertexFormatXYZNDUV1TG3));
	// Not its structure: DX8_FVF_XYZNUV2DMAP declares three sets (one, four and two floats) and
	// VertexFormatXYZNUV2DMAP holds only the last two, 48 bytes.  Nothing sizes a buffer by either;
	// dx8fvf.cpp only names the format.  What D3DX computes is the FVF's own 52.
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZNUV2DMAP) == 12 + 12 + 4 + 16 + 8);
	CHECK(Get_FVF_Vertex_Size(DX8_FVF_XYZNDCUBEMAP) == sizeof(VertexFormatXYZNDCUBEMAP));

	// HeightMap's software path: position with RHW, a colour and two two-float sets.
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2) == 16 + 4 + 16);
	// XYZW is sized as nothing, as Microsoft's D3DX sizes it (test_d3dx9portable_oracle, against the DLL).
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZW) == 0);
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZW | D3DFVF_TEX1) == 8);
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL) == 12 + 16 + 12);
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZB1 | D3DFVF_PSIZE | D3DFVF_SPECULAR) == 16 + 4 + 4);
	// Each blend weight is four bytes, the last one included whatever LASTBETA says it holds.
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZB2) == 20);
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZB3) == 24);
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZB5 | D3DFVF_LASTBETA_D3DCOLOR) == 32);
	CHECK(Get_FVF_Vertex_Size(D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE1(0)) == 12 + 4);
	CHECK(Get_FVF_Vertex_Size(0) == 0);
}

static void check_multiply()
{
	const D3DXMATRIX a = sample(0.3f);
	const D3DXMATRIX b = sample(2.1f);
	D3DXMATRIX product;
	CHECK(D3DXMatrixMultiply(&product, &a, &b) == &product);
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			double expected = 0.0;
			for (int k = 0; k < 4; ++k) expected += (double)a.m[r][k] * b.m[k][c];
			CHECK(near_value(product.m[r][c], expected));
		}
	}
	D3DXMATRIX left = a;
	D3DXMatrixMultiply(&left, &left, &b);
	CHECK(memcmp(&left, &product, sizeof(product)) == 0);
	D3DXMATRIX right = b;
	D3DXMatrixMultiply(&right, &a, &right);
	CHECK(memcmp(&right, &product, sizeof(product)) == 0);
	const D3DXMATRIX by_operator = a * b;
	CHECK(memcmp(&product, &by_operator, sizeof(product)) == 0);
}

static void check_inverse()
{
	D3DXMATRIX view;	// a rotation about z and a translation, as a camera's view matrix is built
	D3DXMATRIX rotation, translation;
	D3DXMatrixRotationZ(&rotation, 0.7f);
	D3DXMatrixTranslation(&translation, 10.0f, -4.0f, 250.0f);
	D3DXMatrixMultiply(&view, &rotation, &translation);

	D3DXMATRIX projection(1.5f, 0, 0, 0,  0, 2.0f, 0, 0,  0, 0, 1.001f, 1.0f,  0, 0, -1.001f, 0);

	const D3DXMATRIX cases[] = { view, projection, sample(5.0f) };
	for (const D3DXMATRIX & m : cases) {
		D3DXMATRIX inverse, product;
		float det = 0.0f;
		CHECK(D3DXMatrixInverse(&inverse, &det, &m) == &inverse);
		CHECK(det != 0.0f);
		CHECK(is_identity(*D3DXMatrixMultiply(&product, &m, &inverse)));
		CHECK(is_identity(*D3DXMatrixMultiply(&product, &inverse, &m)));
	}

	D3DXMATRIX diagonal(2, 0, 0, 0,  0, 3, 0, 0,  0, 0, 4, 0,  0, 0, 0, 5);
	D3DXMATRIX inverse;
	float det = 0.0f;
	D3DXMatrixInverse(&inverse, &det, &diagonal);
	CHECK(det == 120.0f);
	CHECK(near_value(inverse._11, 0.5) && near_value(inverse._44, 0.2));

	// Singular in floating point too: small integers, and the third row twice the first, so every
	// product and difference in the determinant is exact and it comes to exactly zero.
	D3DXMATRIX singular(1, 2, 3, 4,  0, 1, 5, 2,  2, 4, 6, 8,  7, 1, 0, 3);
	D3DXMATRIX untouched = diagonal;
	CHECK(D3DXMatrixInverse(&untouched, &det, &singular) == NULL);
	CHECK(det == 0.0f);
	CHECK(memcmp(&untouched, &diagonal, sizeof(diagonal)) == 0);

	// In place, as W3DVolumetricShadow's caller could.
	D3DXMATRIX self = sample(3.0f), copy = self, expected;
	D3DXMatrixInverse(&expected, NULL, &copy);
	D3DXMatrixInverse(&self, NULL, &self);
	CHECK(memcmp(&self, &expected, sizeof(self)) == 0);
}

static bool transforms_to(const D3DXMATRIX & m, D3DXVECTOR3 point, double x, double y, double z, double w)
{
	D3DXVECTOR4 out;
	D3DXVec3Transform(&out, &point, &m);
	return near_value(out.x, x) && near_value(out.y, y) && near_value(out.z, z) && near_value(out.w, w);
}

static void check_builders()
{
	D3DXMATRIX m;
	CHECK(transforms_to(*D3DXMatrixScaling(&m, 2, 3, 4), D3DXVECTOR3(1, 1, 1), 2, 3, 4, 1));
	CHECK(transforms_to(*D3DXMatrixTranslation(&m, 5, 6, 7), D3DXVECTOR3(1, 2, 3), 6, 8, 10, 1));
	CHECK(transforms_to(*D3DXMatrixRotationZ(&m, D3DX_PI / 2), D3DXVECTOR3(1, 0, 0), 0, 1, 0, 1));
	CHECK(transforms_to(*D3DXMatrixRotationZ(&m, D3DX_PI / 2), D3DXVECTOR3(0, 1, 5), -1, 0, 5, 1));

	const D3DXMATRIX a = sample(4.0f);
	D3DXMATRIX t, tt;
	D3DXMatrixTranspose(&t, &a);
	CHECK(t._12 == a._21 && t._43 == a._34);
	D3DXMatrixTranspose(&tt, &t);
	CHECK(memcmp(&tt, &a, sizeof(a)) == 0);
	D3DXMatrixTranspose(&t, &t);	// in place
	CHECK(memcmp(&t, &a, sizeof(a)) == 0);

	// D3DXMATRIX is a D3DMATRIX, so it goes to SetTransform as it does on Windows.
	const D3DMATRIX * as_d3d = &a;
	CHECK(as_d3d->_23 == a._23);
}

/// One stub assembly: the version it names, and the text back exactly.
static void check_stub(const char * text, size_t length, RenderUInt32 version)
{
	ID3DXBuffer * shader = NULL;
	CHECK(D3DXAssembleShader(text, (unsigned int)length, NULL, NULL, 0, &shader, NULL) == D3D_OK && shader != NULL);
	if (shader == NULL) {
		return;
	}
	const RenderUInt32 * tokens = (const RenderUInt32 *)shader->GetBufferPointer();
	const size_t words = shader->GetBufferSize() / 4;
	CHECK(tokens[0] == version);
	CHECK((tokens[1] & 0xFFFF) == 0xFFFE && ((tokens[1] >> 16) & 0x7FFF) + 3 == words);
	CHECK(tokens[words - 1] == 0x0000FFFF);
	std::string back;
	CHECK(D3DX9Posix_Stub_Shader_Source(tokens, back));
	CHECK(back.size() == length && memcmp(back.data(), text, length) == 0);
	shader->Release();
}

static void check_stub_assembler()
{
	CHECK(Bind_D3DX9_Runtime());
	// The water's river program, as W3DWater.cpp passes it (its length is not a multiple of four).
	static const char RIVER[] = "ps.1.1\n \t\t\ttex t0 \n\t\t\ttex t1\t\n\t\t\tmul r0.rgb, v0, t0 ; blend \n"
		"\t\t\t+mul r0.a, r0, t3\n\t\t\tadd r0.rgb, r0, r1 \n";
	CHECK((sizeof(RIVER) - 1) % 4 != 0);
	check_stub(RIVER, sizeof(RIVER) - 1, 0xFFFF0101);
	static const char COMMENTED[] = "  // a comment first\n; and another\nvs_1_1\nmov oPos, v0\n";
	check_stub(COMMENTED, sizeof(COMMENTED) - 1, 0xFFFE0101);
	check_stub("ps.1.4", 6, 0xFFFF0104);
	// Text naming no version, and a stream that is not a stub, are refused.
	ID3DXBuffer * shader = (ID3DXBuffer *)1;
	CHECK(D3DXAssembleShader("tex t0\n", 7, NULL, NULL, 0, &shader, NULL) == D3DERR_NOTAVAILABLE && shader == NULL);
	CHECK(D3DXAssembleShader("ps.9.9\n", 7, NULL, NULL, 0, &shader, NULL) == D3DERR_NOTAVAILABLE && shader == NULL);
	const RenderUInt32 real[] = { 0xFFFF0101, 0x00000042, 0x800F0000, 0x0000FFFF };
	std::string back;
	CHECK(!D3DX9Posix_Stub_Shader_Source(real, back));
	Unbind_D3DX9_Runtime();
}

static void check_runtime()
{
	CHECK(Bind_D3DX9_Runtime());
	CHECK(D3DXCreateTexture != NULL && D3DXAssembleShader != NULL && D3DXLoadSurfaceFromSurface != NULL);
	IDirect3DTexture9 * texture = (IDirect3DTexture9 *)1;
	CHECK(D3DXCreateTexture(NULL, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture) == D3DERR_INVALIDCALL);
	CHECK(texture == NULL);
	CHECK(strcmp(Get_D3D_Error_String(D3DERR_INVALIDCALL), "D3DERR_INVALIDCALL") == 0);
	CHECK(strcmp(Get_D3D_Error_String(RENDER_FAIL), "E_FAIL") == 0);
	CHECK(strcmp(Get_D3D_Error_String((RenderResult)0x8876ffffu), "0x8876ffff") == 0);
	Unbind_D3DX9_Runtime();
	CHECK(D3DXCreateTexture == NULL && D3DXAssembleShader == NULL);
}

int main()
{
	check_fvf_sizes();
	check_multiply();
	check_inverse();
	check_builders();
	check_runtime();
	check_stub_assembler();
	if (failures != 0) {
		printf("d3dx9posix_selfcheck: %d FAILED\n", failures);
		return 1;
	}
	printf("d3dx9posix_selfcheck: FVF sizes (12 engine vertex structures), product, inverse, builders, binding and the stub assembler all hold\n");
	return 0;
}
