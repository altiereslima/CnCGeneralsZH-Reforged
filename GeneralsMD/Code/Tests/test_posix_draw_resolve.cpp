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

// The draw's resolve (PosixDevice/Render/PosixDevice9Draw.cpp, A3c) on a real headless device made
// through CreateDevice, its state set through the D3D9 calls the engine makes:
//   - D3D9's documented initial states;
//   - the combiner walk: stage count, the arguments carried as set, whether a texture is bound, a
//     disabled stage 0 as the one-stage diffuse combiner, and a stage whose COLORARG1 is an unbound
//     texture ending the walk as a disabled one does;
//   - the vertex description's D3D9 simplifications: no lighting for pretransformed vertices, a material
//     source the vertex cannot supply read from the material; table fog refused with its reason;
//   - the vertex description: the enabled lights packed down in order, and more than the generator
//     carries refused;
//   - the constants: world * view * projection against a double product, the normal transform of a
//     non-uniform scale, a light carried into camera space (a point moves with the view's translation,
//     a direction does not), the attenuation and cone packing, fog's floats, the alpha reference as a
//     level, and D3DCOLORs as R, G, B, A.
//
// WHAT THIS DOES NOT PROVE: that a draw built from these looks right.  That is the reference's
// comparison (A3b, FFReference), which checks the pixels against D3D9's formulas, not these numbers
// against this file's reading of them.

#include "PosixDevice9.h"
#include "SdlConstants.h"
#include "ffshader.h"
#include "ffvertex.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static bool near_value(double actual, double expected)
{
	return fabs(actual - expected) <= 1e-5 * (1.0 + fabs(expected));
}

static D3DMATRIX matrix_of(const double m[4][4])
{
	D3DMATRIX out;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			out.m[r][c] = (float)m[r][c];
	return out;
}

static PosixDevice9 *make_device(IDirect3D9 *&d3d)
{
	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (d3d == NULL) return NULL;
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = 64;
	parameters.BackBufferHeight = 32;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *device = NULL;
	if (Render_Failed(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &device))) return NULL;
	return static_cast<PosixDevice9 *>(device);
}

static void check_defaults(PosixDevice9 *device)
{
	RenderUInt32 value = 0;
	device->GetRenderState(D3DRS_CULLMODE, &value);		CHECK(value == D3DCULL_CCW);
	device->GetRenderState(D3DRS_ZFUNC, &value);		CHECK(value == D3DCMP_LESSEQUAL);
	device->GetRenderState(D3DRS_ZENABLE, &value);		CHECK(value == D3DZB_TRUE);	// an auto depth surface
	device->GetRenderState(D3DRS_TEXTUREFACTOR, &value);	CHECK(value == 0xFFFFFFFFu);
	device->GetRenderState(D3DRS_COLORWRITEENABLE, &value);	CHECK(value == 0xF);
	device->GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, &value);	CHECK(value == D3DMCS_COLOR1);
	device->GetRenderState(D3DRS_FOGEND, &value);
	float fog_end;
	memcpy(&fog_end, &value, sizeof(fog_end));
	CHECK(fog_end == 1.0f);
	device->GetTextureStageState(0, D3DTSS_COLOROP, &value);	CHECK(value == D3DTOP_MODULATE);
	device->GetTextureStageState(1, D3DTSS_COLOROP, &value);	CHECK(value == D3DTOP_DISABLE);
	device->GetTextureStageState(0, D3DTSS_ALPHAOP, &value);	CHECK(value == D3DTOP_SELECTARG1);
	device->GetTextureStageState(3, D3DTSS_TEXCOORDINDEX, &value);	CHECK(value == 3);
}

static void check_combiner(PosixDevice9 *device)
{
	CombinerDescription description;
	// The default stage 0 reads COLORARG1 = TEXTURE: with nothing bound, D3D9 ends the cascade there.
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 1);
	CHECK(description.Stages[0].ColourOperation == D3DTOP_SELECTARG1 && description.Stages[0].ColourArgument1 == D3DTA_DIFFUSE);

	IDirect3DTexture9 *texture = NULL;
	CHECK(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL) == D3D_OK);
	device->SetTexture(0, texture);
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 1);
	CHECK(description.Stages[0].ColourOperation == D3DTOP_MODULATE);
	CHECK(description.Stages[0].ColourArgument1 == D3DTA_TEXTURE && description.Stages[0].ColourArgument2 == D3DTA_CURRENT);
	CHECK(description.Stages[0].TextureBound);

	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD);
	device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT);
	device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, 1);
	device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 2);
	CHECK(description.Stages[1].ColourOperation == D3DTOP_ADD);
	CHECK(description.Stages[1].ColourArgument1 == (D3DTA_TFACTOR | D3DTA_COMPLEMENT));
	CHECK(description.Stages[1].TextureCoordinateIndex == 0);
	CHECK(description.PixelPipeline.AlphaTestEnabled && description.PixelPipeline.AlphaFunction == D3DCMP_GREATEREQUAL);
	// D3DRS_SPECULARENABLE is the pixel program's specular add.
	CHECK(!description.SpecularAdd);
	device->SetRenderState(D3DRS_SPECULARENABLE, 1);
	device->Build_Combiner_Description(description);
	CHECK(description.SpecularAdd && CombinerShader_Key(description).find(":SP") != std::string::npos);
	device->SetRenderState(D3DRS_SPECULARENABLE, 0);

	// No texturing: the diffuse colour and alpha, as one stage.
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 1);
	CHECK(description.Stages[0].ColourOperation == D3DTOP_SELECTARG1 && description.Stages[0].ColourArgument1 == D3DTA_DIFFUSE);
	CHECK(description.Stages[0].AlphaOperation == D3DTOP_SELECTARG1 && description.Stages[0].AlphaArgument1 == D3DTA_DIFFUSE);
	VertexPipelineDescription vertex;
	CHECK(device->Build_Vertex_Description(vertex));
	CHECK(vertex.StageCount == 1);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

	// Stage 1 with COLORARG1 = TEXTURE and nothing bound at 1 ends the walk after stage 0.
	device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 1);
	CHECK(device->Build_Vertex_Description(vertex) && vertex.StageCount == 1);

	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
	device->SetTexture(0, NULL);
	texture->Release();

	// D3D9's simplifications in the vertex description.
	device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	device->SetRenderState(D3DRS_LIGHTING, 1);
	CHECK(device->Build_Vertex_Description(vertex) && !vertex.LightingEnabled);
	device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL);
	device->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	CHECK(device->Build_Vertex_Description(vertex) && vertex.LightingEnabled);
	CHECK(vertex.SpecularMaterialSource == D3DMCS_MATERIAL && vertex.DiffuseMaterialSource == D3DMCS_MATERIAL);
	device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
	CHECK(device->Build_Vertex_Description(vertex) && vertex.DiffuseMaterialSource == D3DMCS_COLOR1);
	// D3DRS_LOCALVIEWER, TRUE by default, reaches the description, and the key of a lit specular program.
	device->SetRenderState(D3DRS_SPECULARENABLE, 1);
	CHECK(device->Build_Vertex_Description(vertex) && vertex.LocalViewer);
	CHECK(VertexShader_Key(vertex).find(":V") != std::string::npos);
	device->SetRenderState(D3DRS_LOCALVIEWER, 0);
	CHECK(device->Build_Vertex_Description(vertex) && !vertex.LocalViewer);
	CHECK(VertexShader_Key(vertex).find(":V") == std::string::npos);
	device->SetRenderState(D3DRS_LOCALVIEWER, 1);
	device->SetRenderState(D3DRS_SPECULARENABLE, 0);
	// Lit without specular still writes the halfway vector, so its key still says which (a review's reading).
	CHECK(device->Build_Vertex_Description(vertex) && VertexShader_Key(vertex).find(":V") != std::string::npos);
	device->SetRenderState(D3DRS_FOGENABLE, 1);
	device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
	std::string refusal;
	CHECK(!device->Build_Vertex_Description(vertex, &refusal) && refusal == "table fog");
	device->SetRenderState(D3DRS_FOGENABLE, 0);
	device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	device->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	device->SetFVF(0);
}

static void check_lights(PosixDevice9 *device)
{
	D3DLIGHT9 light;
	memset(&light, 0, sizeof(light));
	light.Type = D3DLIGHT_DIRECTIONAL;
	device->SetLight(0, &light);
	light.Type = D3DLIGHT_POINT;
	device->SetLight(2, &light);
	light.Type = D3DLIGHT_SPOT;
	device->SetLight(5, &light);
	device->LightEnable(0, 1);
	device->LightEnable(2, 1);
	device->LightEnable(5, 1);
	VertexPipelineDescription description;
	CHECK(device->Build_Vertex_Description(description));
	CHECK(description.LightCount == 3);
	CHECK(description.Lights[0].Type == D3DLIGHT_DIRECTIONAL);
	CHECK(description.Lights[1].Type == D3DLIGHT_POINT);
	CHECK(description.Lights[2].Type == D3DLIGHT_SPOT);

	device->LightEnable(1, 1);
	device->LightEnable(3, 1);	// five: one more than the generator carries
	CHECK(!device->Build_Vertex_Description(description));
	for (RenderUInt32 index = 0; index < 8; ++index) device->LightEnable(index, 0);
}

static void check_constants(PosixDevice9 *device)
{
	const double world[4][4] = { { 2, 0, 0, 0 }, { 0, 3, 0, 0 }, { 0, 0, 4, 0 }, { 5, 6, 7, 1 } };
	const double view[4][4] = { { 0, 1, 0, 0 }, { -1, 0, 0, 0 }, { 0, 0, 1, 0 }, { 10, 20, 30, 1 } };
	const double projection[4][4] = { { 1.5, 0, 0, 0 }, { 0, 2, 0, 0 }, { 0, 0, 1.01, 1 }, { 0, 0, -0.5, 0 } };
	D3DMATRIX m = matrix_of(world);		device->SetTransform(D3DTS_WORLD, &m);
	m = matrix_of(view);				device->SetTransform(D3DTS_VIEW, &m);
	m = matrix_of(projection);			device->SetTransform(D3DTS_PROJECTION, &m);

	D3DLIGHT9 light;
	memset(&light, 0, sizeof(light));
	light.Type = D3DLIGHT_SPOT;
	light.Position.x = 1; light.Position.y = 2; light.Position.z = 3;
	light.Direction.x = 1; light.Direction.y = 0; light.Direction.z = 0;
	light.Diffuse.r = 0.25f; light.Diffuse.a = 1.0f;
	light.Attenuation0 = 1; light.Attenuation1 = 0.5f; light.Attenuation2 = 0.25f; light.Range = 100;
	light.Theta = 1.0f; light.Phi = 2.0f; light.Falloff = 1.5f;
	device->SetLight(3, &light);
	device->LightEnable(3, 1);

	device->SetRenderState(D3DRS_FOGSTART, 0x42C80000u);		// 100.0f
	device->SetRenderState(D3DRS_FOGEND, 0x44FA0000u);			// 2000.0f
	device->SetRenderState(D3DRS_ALPHAREF, 0x60);
	device->SetRenderState(D3DRS_TEXTUREFACTOR, 0x80FF4000u);	// A 0x80, R 0xFF, G 0x40, B 0x00
	device->SetRenderState(D3DRS_AMBIENT, 0x00336699u);

	SdlVertexConstants vertex;
	SdlPixelConstants pixel;
	device->Build_Constants(vertex, pixel);

	// world * view * projection, in double.
	double world_view[4][4], wvp[4][4];
	for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
		world_view[r][c] = 0;
		for (int k = 0; k < 4; ++k) world_view[r][c] += world[r][k] * view[k][c];
	}
	for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
		wvp[r][c] = 0;
		for (int k = 0; k < 4; ++k) wvp[r][c] += world_view[r][k] * projection[k][c];
	}
	bool product = true;
	for (int i = 0; i < 16; ++i) product = product && near_value(vertex.WorldViewProjection[i], wvp[i / 4][i % 4])
		&& near_value(vertex.WorldView[i], world_view[i / 4][i % 4]);
	CHECK(product);

	// The normal transform of scale(2, 3, 4) under a rotation of 90 degrees is the rotation with the
	// scale inverted: (inverse(S R))^T = S^-1 R for a rotation R, in D3D's row-vector order.
	const double expected_normal[3][3] = { { 0, 0.5, 0 }, { -1.0 / 3.0, 0, 0 }, { 0, 0, 0.25 } };
	bool normal = true;
	for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) normal = normal && near_value(vertex.NormalTransform[r * 4 + c], expected_normal[r][c]);
	CHECK(normal);

	// The light, packed into slot 0 (the only one enabled), in camera space.
	const float *position = vertex.LightFields[0][0];
	const float *direction = vertex.LightFields[0][1];
	// (1, 2, 3, 1) * view: x = -2 + 10, y = 1 + 20, z = 3 + 30.
	CHECK(near_value(position[0], 8) && near_value(position[1], 21) && near_value(position[2], 33) && near_value(position[3], 1));
	// (1, 0, 0, 0) * view: the translation does not reach a direction.
	CHECK(near_value(direction[0], 0) && near_value(direction[1], 1) && near_value(direction[2], 0) && near_value(direction[3], 0));
	CHECK(near_value(vertex.LightFields[0][2][0], 0.25) && near_value(vertex.LightFields[0][2][3], 1.0));
	CHECK(vertex.LightFields[0][4][0] == 1.0f && vertex.LightFields[0][4][1] == 0.5f && vertex.LightFields[0][4][2] == 0.25f
		&& vertex.LightFields[0][4][3] == 100.0f);
	CHECK(near_value(vertex.LightFields[0][5][0], cos(0.5)) && near_value(vertex.LightFields[0][5][1], cos(1.0))
		&& vertex.LightFields[0][5][2] == 1.5f);

	CHECK(vertex.FogParameters[0] == 100.0f && vertex.FogParameters[1] == 2000.0f);
	CHECK(near_value(vertex.ViewportInverse[0], 1.0 / 64) && near_value(vertex.ViewportInverse[1], 1.0 / 32));
	CHECK(pixel.AlphaReference[0] == 96.0f);
	CHECK(pixel.TextureFactor[0] == 1.0f && near_value(pixel.TextureFactor[1], 0x40 / 255.0)
		&& pixel.TextureFactor[2] == 0.0f && near_value(pixel.TextureFactor[3], 0x80 / 255.0));
	CHECK(near_value(vertex.GlobalAmbient[0], 0x33 / 255.0) && near_value(vertex.GlobalAmbient[2], 0x99 / 255.0));
}

int main()
{
	IDirect3D9 *d3d = NULL;
	PosixDevice9 *device = make_device(d3d);
	CHECK(device != NULL);
	if (device == NULL) {
		printf("posix_draw_resolve_selfcheck: no headless device\n");
		return 1;
	}
	CHECK(device->Get_Gpu() == NULL);	// headless: no GPU device
	check_defaults(device);
	check_combiner(device);
	check_lights(device);
	check_constants(device);
	device->Release();
	d3d->Release();
	if (failures != 0) {
		printf("posix_draw_resolve_selfcheck: %d FAILED\n", failures);
		return 1;
	}
	printf("posix_draw_resolve_selfcheck: defaults, combiner walk, lights and constants all hold\n");
	return 0;
}
