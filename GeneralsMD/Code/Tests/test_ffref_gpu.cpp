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

// A3b's harness: the device's fixed-function draws on this machine's GPU against FFReference, an independent
// reading of Direct3D 9's documented pipeline (Tests/ffreference/ffreference.h; this file reads that
// header only, as agreed).
//
// Every state a scenario sets goes to both sides through one call: the device gets the D3D9 call, and
// the reference's DrawState gets the same raw value.  Neither side's defaults are copied into the
// other, so a default that differs is a finding too.  Each scenario draws into a cleared target on both,
// reads the GPU's back buffer and compares with FFRef::compare(): a pixel is exact (within 2/255), in a
// documented freedom (inside the reference's envelope), or outside, and only outside fails.
//
// Armed controls: the reference drawn with a deliberate departure (D3D10's pixel centres, texel centres
// at corners) must be found outside, so a pass here means the comparison can fail.
//
// WHAT THIS DOES NOT PROVE: the draws the game makes.  These are the documented pipeline's pieces, one
// scenario each; the game's own draws come through the capture layer (A3d).

#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "ffreference/ffreference.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static const int TARGET_SIZE = 64;
static int failures = 0;
static int scenarios = 0;
static int known_findings = 0;

struct V
{
	float pos[4];
	float normal[3];
	uint32_t diffuse, specular;
	float tex[4][4];
};

static V vertex(float x, float y, float z, float w_or_rhw = 1.0f)
{
	V v;
	memset(&v, 0, sizeof(v));
	v.pos[0] = x; v.pos[1] = y; v.pos[2] = z; v.pos[3] = w_or_rhw;
	v.normal[2] = -1.0f;
	v.diffuse = 0xFFFFFFFF;
	v.specular = 0xFF000000;
	return v;
}

static void matrix_to_ref(const D3DMATRIX &m, FFRef::Matrix &out)
{
	const float *f = &m._11;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			out.m[r][c] = f[r * 4 + c];
}

static D3DMATRIX identity()
{
	D3DMATRIX m;
	memset(&m, 0, sizeof(m));
	m._11 = m._22 = m._33 = m._44 = 1.0f;
	return m;
}

// D3DX's PerspectiveFovLH.
static D3DMATRIX perspective(float fov, float aspect, float zn, float zf)
{
	D3DMATRIX m;
	memset(&m, 0, sizeof(m));
	const float y = 1.0f / tanf(fov * 0.5f);
	m._11 = y / aspect;
	m._22 = y;
	m._33 = zf / (zf - zn);
	m._34 = 1.0f;
	m._43 = -zn * zf / (zf - zn);
	return m;
}

static FFRef::Color colour(const D3DCOLORVALUE &c)
{
	FFRef::Color out = { c.r, c.g, c.b, c.a };
	return out;
}

class Harness
{
public:
	explicit Harness(PosixDevice9 *device) : Device(device) {}

	PosixDevice9 *Device;
	FFRef::DrawState State;
	FFRef::Target Reference;
	std::vector<IDirect3DTexture9 *> DeviceTextures;
	std::vector<FFRef::Texture *> ReferenceTextures;

	/// A new scenario: both sides back to their own defaults, both targets cleared.
	void begin(uint32_t clear_argb)
	{
		for (int stage = 0; stage < 8; ++stage) Device->SetTexture(stage, NULL);
		release_textures();
		// The device keeps its state between scenarios, so each one restores what an earlier one set:
		// the device's own defaults come back by setting every state the reference defaults to.  That is
		// a copy of the reference's defaults into the device, which the scenarios' first draw (defaults
		// only) checks from the other side.
		State.setDefaults(TARGET_SIZE, TARGET_SIZE);
		for (int s = 0; s < 256; ++s) {
			if (s == D3DRS_ZENABLE || known_render_state(s)) Device->SetRenderState((D3DRENDERSTATETYPE)s, State.renderState[s]);
		}
		for (int stage = 0; stage < 8; ++stage) {
			for (int t = 1; t < 33; ++t) Device->SetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)t, State.stageState[stage][t]);
			for (int t = 1; t < 14; ++t) Device->SetSamplerState(stage, (D3DSAMPLERSTATETYPE)t, State.samplerState[stage][t]);
		}
		const D3DMATRIX id = identity();
		Device->SetTransform(D3DTS_WORLD, &id);
		Device->SetTransform(D3DTS_VIEW, &id);
		Device->SetTransform(D3DTS_PROJECTION, &id);
		for (int stage = 0; stage < 8; ++stage) Device->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + stage), &id);
		for (int i = 0; i < 8; ++i) Device->LightEnable(i, 0);
		D3DMATERIAL9 material;
		memset(&material, 0, sizeof(material));
		material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1.0f;
		Device->SetMaterial(&material);
		State.material.diffuse = colour(material.Diffuse);
		State.material.ambient = colour(material.Ambient);
		State.material.specular = colour(material.Specular);
		State.material.emissive = colour(material.Emissive);
		State.material.power = 0.0;
		Reference.create(TARGET_SIZE, TARGET_SIZE, true);
		Reference.clear(FFRef::colorFromD3D(clear_argb), 1.0, 0);
		Device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, clear_argb, 1.0f, 0);
	}

	void rs(D3DRENDERSTATETYPE s, uint32_t v) { Device->SetRenderState(s, v); State.renderState[s] = v; }
	void rsf(D3DRENDERSTATETYPE s, float v) { rs(s, FFRef::floatBits(v)); }
	void tss(int stage, D3DTEXTURESTAGESTATETYPE t, uint32_t v) { Device->SetTextureStageState(stage, t, v); State.stageState[stage][t] = v; }
	void ss(int stage, D3DSAMPLERSTATETYPE t, uint32_t v) { Device->SetSamplerState(stage, t, v); State.samplerState[stage][t] = v; }

	void transform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX &m)
	{
		Device->SetTransform(t, &m);
		if (t == D3DTS_WORLD) matrix_to_ref(m, State.world);
		else if (t == D3DTS_VIEW) matrix_to_ref(m, State.view);
		else if (t == D3DTS_PROJECTION) matrix_to_ref(m, State.projection);
		else if (t >= D3DTS_TEXTURE0 && t <= D3DTS_TEXTURE7) matrix_to_ref(m, State.textureTransform[t - D3DTS_TEXTURE0]);
	}

	void light(int index, const D3DLIGHT9 &light)
	{
		Device->SetLight(index, &light);
		Device->LightEnable(index, 1);
		FFRef::Light &l = State.lights[index];
		l.enabled = true;
		l.type = light.Type;
		l.diffuse = colour(light.Diffuse);
		l.specular = colour(light.Specular);
		l.ambient = colour(light.Ambient);
		l.position[0] = light.Position.x; l.position[1] = light.Position.y; l.position[2] = light.Position.z;
		l.direction[0] = light.Direction.x; l.direction[1] = light.Direction.y; l.direction[2] = light.Direction.z;
		l.range = light.Range;
		l.falloff = light.Falloff;
		l.attenuation0 = light.Attenuation0;
		l.attenuation1 = light.Attenuation1;
		l.attenuation2 = light.Attenuation2;
		l.theta = light.Theta;
		l.phi = light.Phi;
	}

	void material(const D3DMATERIAL9 &m)
	{
		Device->SetMaterial(&m);
		State.material.diffuse = colour(m.Diffuse);
		State.material.ambient = colour(m.Ambient);
		State.material.specular = colour(m.Specular);
		State.material.emissive = colour(m.Emissive);
		State.material.power = m.Power;
	}

	/// An A8R8G8B8 texture with `levels` levels, texel (x, y) of level l from texel(l, x, y), on both.
	template <class Texel>
	void texture(int stage, int width, int height, int levels, Texel texel)
	{
		IDirect3DTexture9 *texture = NULL;
		if (Device->CreateTexture(width, height, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL) != D3D_OK) {
			++failures;
			printf("FAIL: CreateTexture\n");
			return;
		}
		FFRef::Texture *reference = new FFRef::Texture;
		reference->type = FFRef::TEXTURE_2D;
		for (int level = 0; level < levels; ++level) {
			const int w = width >> level > 0 ? width >> level : 1;
			const int h = height >> level > 0 ? height >> level : 1;
			D3DLOCKED_RECT locked;
			texture->LockRect(level, &locked, NULL, 0);
			FFRef::TextureLevel ref_level;
			ref_level.width = w;
			ref_level.height = h;
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					const uint32_t argb = texel(level, x, y);
					((uint32_t *)((uint8_t *)locked.pBits + y * locked.Pitch))[x] = argb;
					ref_level.texels.push_back(FFRef::colorFromD3D(argb));
				}
			}
			texture->UnlockRect(level);
			reference->levels.push_back(ref_level);
		}
		Device->SetTexture(stage, texture);
		State.textures[stage] = reference;
		DeviceTextures.push_back(texture);
		ReferenceTextures.push_back(reference);
	}

	/// DrawPrimitiveUP on the device, FFRef::draw on the reference.  `mutations` arms the reference.
	void draw(RenderUInt32 fvf, D3DPRIMITIVETYPE type, const std::vector<V> &vertices, unsigned mutations = 0)
	{
		Device->SetFVF(fvf);
		// The vertex as the FVF lays it out.
		const bool rhw = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
		const int sets = (int)((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
		int sizes[8];
		static const int SIZE_OF_BITS[4] = { 2, 3, 4, 1 };
		for (int set = 0; set < 8; ++set) sizes[set] = SIZE_OF_BITS[(fvf >> (16 + set * 2)) & 3];
		std::vector<float> bytes;
		for (size_t i = 0; i < vertices.size(); ++i) {
			const V &v = vertices[i];
			for (int k = 0; k < (rhw ? 4 : 3); ++k) bytes.push_back(v.pos[k]);
			if (fvf & D3DFVF_NORMAL) for (int k = 0; k < 3; ++k) bytes.push_back(v.normal[k]);
			float word;
			if (fvf & D3DFVF_DIFFUSE) { memcpy(&word, &v.diffuse, 4); bytes.push_back(word); }
			if (fvf & D3DFVF_SPECULAR) { memcpy(&word, &v.specular, 4); bytes.push_back(word); }
			for (int set = 0; set < sets; ++set) for (int k = 0; k < sizes[set]; ++k) bytes.push_back(v.tex[set][k]);
		}
		const unsigned stride = (unsigned)(bytes.size() * 4 / vertices.size());
		unsigned primitives = 0;
		switch (type) {
			case D3DPT_TRIANGLELIST: primitives = (unsigned)vertices.size() / 3; break;
			default: primitives = (unsigned)vertices.size() - 2; break;
		}
		Device->DrawPrimitiveUP(type, primitives, &bytes[0], stride);

		State.pretransformed = rhw;
		State.hasNormal = (fvf & D3DFVF_NORMAL) != 0;
		State.hasDiffuse = (fvf & D3DFVF_DIFFUSE) != 0;
		State.hasSpecular = (fvf & D3DFVF_SPECULAR) != 0;
		State.texCoordSets = sets;
		for (int set = 0; set < 8; ++set) State.texCoordSize[set] = sizes[set];
		std::vector<FFRef::Vertex> reference(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			const V &v = vertices[i];
			FFRef::Vertex &r = reference[i];
			memset(&r, 0, sizeof(r));
			for (int k = 0; k < 4; ++k) r.position[k] = k < 3 || rhw ? v.pos[k] : 1.0;
			for (int k = 0; k < 3; ++k) r.normal[k] = v.normal[k];
			r.diffuse = FFRef::colorFromD3D(v.diffuse);
			r.specular = FFRef::colorFromD3D(v.specular);
			for (int set = 0; set < 4; ++set) for (int k = 0; k < 4; ++k) r.tex[set][k] = v.tex[set][k];
		}
		FFRef::Report report;
		// The freedoms are FFReference's defaults, the LOD's +-0.6 among them (F11, N15).
		FFRef::draw(State, type, &reference[0], (int)reference.size(), NULL, (int)reference.size(), Reference, &report,
			mutations);
		for (size_t i = 0; i < report.refusals.size(); ++i) printf("  reference refused: %s\n", report.refusals[i].c_str());
	}

	/// Reads the GPU back and compares.  `expect_outside`: an armed control, which must fail.  `known`: a
	/// finding recorded during A3 and waiting on a decision; its failure is reported and not
	/// counted, and its passing is counted, so the list cannot outlive the finding.
	void check(const char *label, bool expect_outside = false, const char *known = NULL)
	{
		++scenarios;
		SdlGpuFrame *gpu = Device->Get_Gpu();
		std::vector<uint8_t> bgra;
		if (!gpu->Read_Back(gpu->Back_Buffer(), TARGET_SIZE, TARGET_SIZE, bgra)) {
			++failures;
			printf("FAIL %s: no read-back\n", label);
			return;
		}
		std::vector<uint8_t> rgba(bgra.size());
		for (size_t i = 0; i < bgra.size(); i += 4) {
			rgba[i] = bgra[i + 2];
			rgba[i + 1] = bgra[i + 1];
			rgba[i + 2] = bgra[i];
			rgba[i + 3] = bgra[i + 3];
		}
		const FFRef::Comparison comparison = FFRef::compare(Reference, &rgba[0], TARGET_SIZE * 4);
		if (getenv("FFREF_VERBOSE") != NULL || (comparison.passed() == expect_outside && known == NULL)) {
			FFRef::print(comparison, stdout, label);
		}
		if (known != NULL) {
			if (comparison.passed()) {
				++failures;
				printf("FAIL %s: listed as known (%s) and now passes: take it off the list\n", label, known);
			}
			else {
				++known_findings;
				printf("KNOWN %-39s %s: %ld outside, worst %.0f/255", label, known, comparison.outside,
					comparison.worst * 255.0);
				const int x = comparison.worstX, y = comparison.worstY;
				if (x >= 0) {
					const FFRef::Color &n = Reference.color[y * TARGET_SIZE + x];
					const uint8_t *g = &rgba[(y * TARGET_SIZE + x) * 4];
					printf(" at (%d, %d): gpu %u %u %u, reference %.0f %.0f %.0f", x, y, g[0], g[1], g[2], n.r * 255,
						n.g * 255, n.b * 255);
				}
				printf("\n");
			}
			return;
		}
		if (comparison.passed() == expect_outside) {
			++failures;
			const int x = comparison.worstX, y = comparison.worstY;
			if (x >= 0) {
				const FFRef::Color &n = Reference.color[y * TARGET_SIZE + x];
				const uint8_t *g = &rgba[(y * TARGET_SIZE + x) * 4];
				printf("FAIL %s%s: worst (%d, %d) gpu %u %u %u %u, reference %.1f %.1f %.1f %.1f\n", label,
					expect_outside ? " (an armed control passed)" : "", x, y, g[0], g[1], g[2], g[3],
					n.r * 255, n.g * 255, n.b * 255, n.a * 255);
			}
			else {
				printf("FAIL %s%s\n", label, expect_outside ? ": an armed control passed" : "");
			}
		}
		else {
			printf("ok   %-40s exact %ld, in a freedom %ld\n", label, comparison.exact, comparison.inFreedom);
		}
	}

	void release_textures()
	{
		for (size_t i = 0; i < DeviceTextures.size(); ++i) DeviceTextures[i]->Release();
		for (size_t i = 0; i < ReferenceTextures.size(); ++i) delete ReferenceTextures[i];
		DeviceTextures.clear();
		ReferenceTextures.clear();
		memset(State.textures, 0, sizeof(State.textures));
	}

	static bool known_render_state(int s)
	{
		// The states D3DRENDERSTATETYPE defines; others are left alone on the device.
		static const int STATES[] = { 7, 8, 9, 14, 15, 16, 19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 34, 35, 36, 37, 38,
			48, 52, 53, 54, 55, 56, 57, 58, 59, 60, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 139, 140, 141,
			142, 143, 145, 146, 147, 148, 151, 152, 168, 171, 174, 175, 185, 186, 187, 188, 189, 193, 194, 195,
			198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209 };
		for (size_t i = 0; i < sizeof(STATES) / sizeof(STATES[0]); ++i) if (STATES[i] == s) return true;
		return false;
	}
};

// A screen-space quad as two triangles, with texture coordinates 0..u_max, 0..v_max and a colour per
// corner (top-left, top-right, bottom-left, bottom-right).
static std::vector<V> screen_quad(float left, float top, float right, float bottom, const uint32_t corners[4],
	float u_max = 1.0f, float v_max = 1.0f, const float rhw[4] = NULL)
{
	V c[4] = { vertex(left, top, 0.5f), vertex(right, top, 0.5f), vertex(left, bottom, 0.5f), vertex(right, bottom, 0.5f) };
	for (int i = 0; i < 4; ++i) {
		c[i].diffuse = corners[i];
		c[i].tex[0][0] = c[i].tex[1][0] = (i & 1) ? u_max : 0.0f;
		c[i].tex[0][1] = c[i].tex[1][1] = (i & 2) ? v_max : 0.0f;
		if (rhw != NULL) c[i].pos[3] = rhw[i];
	}
	std::vector<V> out;
	out.push_back(c[0]); out.push_back(c[1]); out.push_back(c[2]);
	out.push_back(c[1]); out.push_back(c[3]); out.push_back(c[2]);
	return out;
}

// A grid of n x n cells over (x0..x1, y0..y1) in world space at depth z(x, y), with normals from a bump,
// as a triangle list.  Colours vary over the grid.
static std::vector<V> grid(int n, float x0, float x1, float y0, float y1, float z_near, float z_far, float bump)
{
	std::vector<V> corner((n + 1) * (n + 1));
	for (int j = 0; j <= n; ++j) {
		for (int i = 0; i <= n; ++i) {
			const float s = (float)i / n, t = (float)j / n;
			V v = vertex(x0 + (x1 - x0) * s, y0 + (y1 - y0) * t, z_near + (z_far - z_near) * t);
			const float nx = bump * sinf(s * 6.0f), ny = bump * cosf(t * 5.0f);
			const float length = sqrtf(nx * nx + ny * ny + 1.0f);
			v.normal[0] = nx / length; v.normal[1] = ny / length; v.normal[2] = -1.0f / length;
			const uint32_t r = (uint32_t)(255 * s), g = (uint32_t)(255 * t), b = (uint32_t)(255 * (1 - s * t));
			v.diffuse = 0xFF000000 | (r << 16) | (g << 8) | b;
			v.specular = 0xFF000000 | (b << 16) | (r << 8) | g;
			v.tex[0][0] = v.tex[1][0] = s * 2.0f;
			v.tex[0][1] = v.tex[1][1] = t * 2.0f;
			corner[j * (n + 1) + i] = v;
		}
	}
	std::vector<V> out;
	for (int j = 0; j < n; ++j) {
		for (int i = 0; i < n; ++i) {
			const V &a = corner[j * (n + 1) + i], &b = corner[j * (n + 1) + i + 1];
			const V &c = corner[(j + 1) * (n + 1) + i], &d = corner[(j + 1) * (n + 1) + i + 1];
			out.push_back(a); out.push_back(b); out.push_back(c);
			out.push_back(b); out.push_back(d); out.push_back(c);
		}
	}
	return out;
}

static uint32_t checker(int level, int x, int y)
{
	static const uint32_t LEVELS[7] = { 0xFFFF4020, 0xFF20C040, 0xFF4060FF, 0xFFE0E020, 0xFFE020E0, 0xFF20E0E0, 0xFF808080 };
	const uint32_t base = LEVELS[level % 7];
	return ((x + y) & 1) ? base : (base & 0xFF7F7F7F);
}

static uint32_t gradient(int, int x, int y)
{
	return 0x80000000u | ((uint32_t)(x * 16 + 8) << 16) | ((uint32_t)(y * 16 + 8) << 8) | (uint32_t)((x ^ y) * 16 + 8);
}

static const RenderUInt32 SCREEN = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static const RenderUInt32 LIT = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static const uint32_t CORNERS[4] = { 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0x80FFFFFF };

static void set_camera(Harness &h)
{
	h.transform(D3DTS_PROJECTION, perspective(1.0f, 1.0f, 1.0f, 50.0f));
	h.rs(D3DRS_CULLMODE, D3DCULL_NONE);
}

static void scenarios_raster(Harness &h)
{
	h.begin(0xFF203040);
	h.rs(D3DRS_LIGHTING, 0);
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(5.25f, 7.75f, 58.5f, 50.25f, CORNERS));
	h.check("gouraud, pretransformed");

	h.begin(0xFF203040);
	h.rs(D3DRS_LIGHTING, 0);
	const float rhw[4] = { 1.0f, 0.25f, 0.5f, 0.125f };
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(3, 3, 61, 61, CORNERS, 1, 1, rhw));
	h.check("gouraud, perspective-correct (rhw)");

	h.begin(0xFF000000);
	h.rs(D3DRS_LIGHTING, 0);
	set_camera(h);
	h.draw(D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, grid(6, -3, 3, -2, 1.5f, 3, 12, 0));
	h.check("transformed grid, depth");

	// No depth-stencil surface bound (a capture's DepthBound = 0): D3D9 then draws without depth or stencil,
	// whatever ZENABLE says.  The device keeps ZENABLE on with GREATER, which nothing passes against the
	// cleared 1.0, so any depth test draws nothing - in either draw, and in a replay of the first draw alone
	// (the capture keeps one per signature).  The far quad goes over the near one, as it would without depth.
	// The reference is told what D3D9 does.
	h.begin(0xFF000000);
	h.rs(D3DRS_LIGHTING, 0);
	IDirect3DSurface9 *depth_surface = NULL;
	h.Device->GetDepthStencilSurface(&depth_surface);
	h.Device->SetDepthStencilSurface(NULL);
	h.Device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	h.Device->SetRenderState(D3DRS_ZWRITEENABLE, 1);
	h.Device->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATER);
	h.State.renderState[D3DRS_ZENABLE] = D3DZB_FALSE;
	h.State.renderState[D3DRS_ZWRITEENABLE] = 0;
	h.State.renderState[D3DRS_STENCILENABLE] = 0;
	std::vector<V> near_quad = screen_quad(6, 6, 42, 42, CORNERS), far_quad = screen_quad(22, 22, 58, 58, CORNERS);
	for (size_t k = 0; k < near_quad.size(); ++k) { near_quad[k].pos[2] = 0.2f; far_quad[k].pos[2] = 0.8f; far_quad[k].diffuse = 0xFF30C0A0; }
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, near_quad);
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, far_quad);
	h.check("no depth surface: depth off whatever ZENABLE says");
	h.Device->SetDepthStencilSurface(depth_surface);
	if (depth_surface != NULL) depth_surface->Release();

	h.begin(0xFF000000);
	h.rs(D3DRS_LIGHTING, 0);
	h.rs(D3DRS_SHADEMODE, D3DSHADE_FLAT);
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(4, 4, 60, 60, CORNERS));
	h.check("flat shading", false, "F8: flat shading");
}

static void scenarios_textures(Harness &h)
{
	h.begin(0xFF000000);
	h.texture(0, 4, 4, 1, gradient);
	h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	h.draw(SCREEN, D3DPT_TRIANGLELIST, screen_quad(-0.5f, -0.5f, 63.5f, 63.5f, CORNERS));
	h.check("point magnified, modulate");

	h.begin(0xFF000000);
	h.texture(0, 4, 4, 1, gradient);
	h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	h.tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	h.draw(SCREEN, D3DPT_TRIANGLELIST, screen_quad(-0.5f, -0.5f, 63.5f, 63.5f, CORNERS));
	h.check("bilinear magnified");

	static const struct { const char *name; RenderUInt32 mode; } ADDRESS[] = {
		{ "address wrap", D3DTADDRESS_WRAP }, { "address mirror", D3DTADDRESS_MIRROR }, { "address clamp", D3DTADDRESS_CLAMP } };
	for (int i = 0; i < 3; ++i) {
		h.begin(0xFF000000);
		h.texture(0, 4, 4, 1, gradient);
		h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		h.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		h.ss(0, D3DSAMP_ADDRESSU, ADDRESS[i].mode);
		h.ss(0, D3DSAMP_ADDRESSV, ADDRESS[i].mode);
		h.tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		h.draw(SCREEN, D3DPT_TRIANGLELIST, screen_quad(-0.5f, -0.5f, 63.5f, 63.5f, CORNERS, 2.5f, 2.5f));
		h.check(ADDRESS[i].name);
	}

	static const struct { const char *name; RenderUInt32 min, mip; const char *known; } MIPS[] = {
		{ "mips: point, no mip filter", D3DTEXF_POINT, D3DTEXF_NONE, NULL },
		{ "mips: point, point mips", D3DTEXF_POINT, D3DTEXF_POINT, NULL },
		{ "mips: linear, linear mips", D3DTEXF_LINEAR, D3DTEXF_LINEAR, NULL } };
	for (int i = 0; i < 3; ++i) {
		h.begin(0xFF000000);
		h.rs(D3DRS_LIGHTING, 0);
		set_camera(h);
		h.texture(0, 64, 64, 7, checker);
		h.ss(0, D3DSAMP_MINFILTER, MIPS[i].min);
		h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		h.ss(0, D3DSAMP_MIPFILTER, MIPS[i].mip);
		h.tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		std::vector<V> floor = grid(4, -4, 4, -1.5f, -1.5f, 2, 30, 0);
		for (size_t k = 0; k < floor.size(); ++k) { floor[k].tex[0][0] *= 4; floor[k].tex[0][1] *= 16; }
		h.draw(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPT_TRIANGLELIST, floor);
		h.check(MIPS[i].name, false, MIPS[i].known);
	}

	// COUNT2 as the engine's mappers use it (mapper.cpp: scale on the diagonal, a scroll in _31 and _32,
	// "According to the docs this should work since its 2D").
	for (int translate = 0; translate < 2; ++translate) {
		h.begin(0xFF000000);
		h.texture(0, 4, 4, 1, gradient);
		h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		h.tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		h.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		D3DMATRIX scale = identity();
		scale._11 = 0.5f; scale._22 = 0.25f;
		if (translate) { scale._31 = 0.25f; scale._32 = 0.5f; }
		h.transform(D3DTS_TEXTURE0, scale);
		h.rs(D3DRS_LIGHTING, 0);
		set_camera(h);
		h.draw(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPT_TRIANGLELIST, grid(3, -2, 2, -2, 2, 3, 6, 0));
		if (translate) h.check("texture transform, count 2, scrolled");
		else h.check("texture transform, count 2, scale");
	}
}

static void scenarios_cascade(Harness &h)
{
	static const struct { const char *name; RenderUInt32 op; } OPS[] = {
		{ "op SELECTARG1", D3DTOP_SELECTARG1 }, { "op SELECTARG2", D3DTOP_SELECTARG2 }, { "op MODULATE", D3DTOP_MODULATE },
		{ "op MODULATE2X", D3DTOP_MODULATE2X }, { "op MODULATE4X", D3DTOP_MODULATE4X }, { "op ADD", D3DTOP_ADD },
		{ "op ADDSIGNED", D3DTOP_ADDSIGNED }, { "op ADDSIGNED2X", D3DTOP_ADDSIGNED2X }, { "op SUBTRACT", D3DTOP_SUBTRACT },
		{ "op ADDSMOOTH", D3DTOP_ADDSMOOTH }, { "op BLENDDIFFUSEALPHA", D3DTOP_BLENDDIFFUSEALPHA },
		{ "op BLENDTEXTUREALPHA", D3DTOP_BLENDTEXTUREALPHA }, { "op BLENDFACTORALPHA", D3DTOP_BLENDFACTORALPHA },
		{ "op BLENDCURRENTALPHA", D3DTOP_BLENDCURRENTALPHA }, { "op BLENDTEXTUREALPHAPM", D3DTOP_BLENDTEXTUREALPHAPM },
		{ "op MODULATEALPHA_ADDCOLOR", D3DTOP_MODULATEALPHA_ADDCOLOR },
		{ "op MODULATECOLOR_ADDALPHA", D3DTOP_MODULATECOLOR_ADDALPHA },
		{ "op MODULATEINVALPHA_ADDCOLOR", D3DTOP_MODULATEINVALPHA_ADDCOLOR },
		{ "op MODULATEINVCOLOR_ADDALPHA", D3DTOP_MODULATEINVCOLOR_ADDALPHA }, { "op DOTPRODUCT3", D3DTOP_DOTPRODUCT3 },
		{ "op MULTIPLYADD", D3DTOP_MULTIPLYADD }, { "op LERP", D3DTOP_LERP } };
	for (size_t i = 0; i < sizeof(OPS) / sizeof(OPS[0]); ++i) {
		h.begin(0xFF000000);
		h.texture(0, 4, 4, 1, gradient);
		h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		h.rs(D3DRS_TEXTUREFACTOR, 0x60A0C0E0);
		h.tss(0, D3DTSS_COLOROP, OPS[i].op);
		h.tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		h.tss(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		h.tss(0, D3DTSS_COLORARG0, D3DTA_TFACTOR);
		h.tss(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		h.draw(SCREEN, D3DPT_TRIANGLELIST, screen_quad(-0.5f, -0.5f, 63.5f, 63.5f, CORNERS));
		h.check(OPS[i].name);
	}

	h.begin(0xFF000000);
	h.texture(0, 4, 4, 1, gradient);
	h.texture(1, 4, 4, 1, checker);
	h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	h.ss(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	h.tss(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	h.tss(1, D3DTSS_COLOROP, D3DTOP_ADD);
	h.tss(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	h.tss(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	h.tss(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
	h.tss(1, D3DTSS_TEXCOORDINDEX, 1);
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2, D3DPT_TRIANGLELIST, screen_quad(-0.5f, -0.5f, 63.5f, 63.5f, CORNERS, 1, 1));
	h.check("two stages, texture coordinate set 1");

	// The shroud's shape: stage 0 reads the vertex's set 0, stage 1 generates its own coordinates from
	// the camera-space position (TCI set bits 0) through a COUNT2 transform.  Each stage has to read
	// its own coordinates, not the set its TCI's low bits name.
	h.begin(0xFF000000);
	h.rs(D3DRS_LIGHTING, 0);
	set_camera(h);
	h.texture(0, 4, 4, 1, gradient);
	h.texture(1, 4, 4, 1, checker);
	h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	// Linear, as the shroud is sampled (the trees' trace: min and mag LINEAR).  Point sampling put two
	// generated coordinates on a texel boundary within float precision, a tie rather than the fault.
	h.ss(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	h.ss(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	h.ss(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	h.ss(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	h.tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	h.tss(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
	h.tss(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	h.tss(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	h.tss(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
	h.tss(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	h.tss(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	D3DMATRIX shroud = identity();
	shroud._11 = 0.37f; shroud._22 = 0.37f; shroud._41 = 0.5f; shroud._42 = 0.5f;
	h.transform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + 1), shroud);
	h.draw(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPT_TRIANGLELIST, grid(3, -2, 2, -2, 2, 3, 6, 0));
	h.check("stage 1 texgen beside stage 0's set 0");

	h.begin(0xFF000000);
	h.tss(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(4, 4, 60, 60, CORNERS));
	h.check("stage 0 disabled: the diffuse colour");

	h.begin(0xFF000000);
	h.rs(D3DRS_SPECULARENABLE, 1);
	h.rs(D3DRS_LIGHTING, 0);
	std::vector<V> quad = screen_quad(4, 4, 60, 60, CORNERS);
	for (size_t k = 0; k < quad.size(); ++k) quad[k].specular = 0xFF402010;
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR, D3DPT_TRIANGLELIST, quad);
	h.check("specular add (vertex specular)");

	h.begin(0xFF000000);
	h.rs(D3DRS_SPECULARENABLE, 1);
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(4, 4, 60, 60, CORNERS));
	// F7, settled by Windows' own D3D9 (WARP, HAL and REF): an absent vertex specular reads as
	// 0x00000000, so SPECULARENABLE adds nothing.  FFReference's N7 says so now; an ordinary check again.
	h.check("specular add, no vertex specular (N7)");
}

static void scenarios_lighting(Harness &h)
{
	D3DMATERIAL9 material;
	memset(&material, 0, sizeof(material));
	material.Diffuse.r = 0.8f; material.Diffuse.g = 0.7f; material.Diffuse.b = 0.6f; material.Diffuse.a = 1.0f;
	material.Ambient.r = material.Ambient.g = material.Ambient.b = 0.3f;
	material.Specular.r = material.Specular.g = material.Specular.b = 0.9f;
	material.Emissive.b = 0.1f;
	material.Power = 12.0f;

	static const struct { const char *name; D3DLIGHTTYPE type; bool specular, local_viewer; float ambient; const char *known; } LIGHTS[] = {
		{ "light directional", D3DLIGHT_DIRECTIONAL, false, true, 0.0f, NULL },
		{ "light point, attenuated", D3DLIGHT_POINT, false, true, 0.0f, NULL },
		{ "light spot", D3DLIGHT_SPOT, false, true, 0.0f, NULL },
		{ "light specular, no local viewer", D3DLIGHT_DIRECTIONAL, true, false, 0.0f, NULL },
		{ "light point, spot specular, no local viewer", D3DLIGHT_SPOT, true, false, 0.0f, NULL },
		{ "light point with its own ambient", D3DLIGHT_POINT, false, true, 0.1f, NULL },
		{ "light specular, local viewer", D3DLIGHT_DIRECTIONAL, true, true, 0.0f, NULL } };
	for (size_t i = 0; i < sizeof(LIGHTS) / sizeof(LIGHTS[0]); ++i) {
		h.begin(0xFF000000);
		set_camera(h);
		h.material(material);
		h.rs(D3DRS_AMBIENT, 0xFF202020);
		h.rs(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
		h.rs(D3DRS_SPECULARENABLE, LIGHTS[i].specular ? 1 : 0);
		h.rs(D3DRS_LOCALVIEWER, LIGHTS[i].local_viewer ? 1 : 0);
		D3DLIGHT9 light;
		memset(&light, 0, sizeof(light));
		light.Type = LIGHTS[i].type;
		light.Diffuse.r = 1.0f; light.Diffuse.g = 0.9f; light.Diffuse.b = 0.8f;
		light.Specular.r = light.Specular.g = light.Specular.b = 1.0f;
		light.Ambient.r = LIGHTS[i].ambient;
		light.Direction.x = 0.3f; light.Direction.y = -0.5f; light.Direction.z = 1.0f;
		light.Position.x = 0.5f; light.Position.y = 1.0f; light.Position.z = 3.0f;
		light.Range = 20.0f;
		light.Attenuation0 = 0.5f; light.Attenuation1 = 0.1f; light.Attenuation2 = 0.02f;
		light.Theta = 0.4f; light.Phi = 1.2f; light.Falloff = 1.0f;
		h.light(0, light);
		h.draw(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, grid(10, -3, 3, -2.5f, 2.5f, 4, 8, 0.8f));
		h.check(LIGHTS[i].name, false, LIGHTS[i].known);
	}

	h.begin(0xFF000000);
	set_camera(h);
	h.material(material);
	h.rs(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	h.rs(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
	D3DLIGHT9 light;
	memset(&light, 0, sizeof(light));
	light.Type = D3DLIGHT_DIRECTIONAL;
	light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
	light.Direction.z = 1.0f;
	h.light(0, light);
	light.Direction.x = -1.0f; light.Direction.z = 0.2f;
	light.Diffuse.g = 0.2f;
	h.light(1, light);
	h.draw(LIT & ~D3DFVF_TEX1, D3DPT_TRIANGLELIST, grid(10, -3, 3, -2.5f, 2.5f, 4, 8, 0.8f));
	h.check("two lights, colour from the vertex");

	// The reflection vector, R = 2(N.E)N - E, with E the vertex's own direction to the eye under
	// LOCALVIEWER and the fixed (0, 0, -1) without it, as Windows' D3D9 draws it (knownprobe_windows.cpp, N14).
	// Unlit, through a COUNT2 transform that puts R.xy's -1..1 onto the texture, over a bumped grid so
	// the normals vary.
	for (int local = 1; local >= 0; --local) {
		h.begin(0xFF000000);
		h.rs(D3DRS_LIGHTING, 0);
		h.rs(D3DRS_LOCALVIEWER, local);
		set_camera(h);
		h.texture(0, 16, 16, 1, gradient);
		h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		h.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		h.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		h.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		h.tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		h.tss(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
		h.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		D3DMATRIX onto = identity();
		onto._11 = 0.5f; onto._22 = 0.5f; onto._41 = 0.5f; onto._42 = 0.5f;
		h.transform(D3DTS_TEXTURE0, onto);
		h.draw(LIT, D3DPT_TRIANGLELIST, grid(10, -3, 3, -2.5f, 2.5f, 4, 8, 0.8f));
		h.check(local ? "reflection vector, local viewer" : "reflection vector, no local viewer (N14)");
	}
}

static void scenarios_fog_blend_test(Harness &h)
{
	static const struct { const char *name; RenderUInt32 vertex_mode, table_mode; } FOGS[] = {
		{ "fog vertex linear", D3DFOG_LINEAR, D3DFOG_NONE }, { "fog vertex exp", D3DFOG_EXP, D3DFOG_NONE },
		{ "fog vertex exp2", D3DFOG_EXP2, D3DFOG_NONE }, { "fog table linear", D3DFOG_NONE, D3DFOG_LINEAR } };
	for (int i = 0; i < 4; ++i) {
		h.begin(0xFF000000);
		h.rs(D3DRS_LIGHTING, 0);
		set_camera(h);
		h.rs(D3DRS_FOGENABLE, 1);
		h.rs(D3DRS_FOGCOLOR, 0xFF8090A0);
		h.rs(D3DRS_FOGVERTEXMODE, FOGS[i].vertex_mode);
		h.rs(D3DRS_FOGTABLEMODE, FOGS[i].table_mode);
		h.rsf(D3DRS_FOGSTART, 3.0f);
		h.rsf(D3DRS_FOGEND, 20.0f);
		h.rsf(D3DRS_FOGDENSITY, 0.1f);
		const unsigned int recorded = h.Device->Draws_Recorded();
		h.draw(D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, grid(6, -4, 4, -1.5f, -1.5f, 2, 30, 0));
		if (FOGS[i].table_mode != D3DFOG_NONE) {
			// The engine fogs per vertex only (dx8wrapper's defaults), and the generator has no table
			// fog: the draw is refused, by name, and draws nothing.
			++scenarios;
			const bool refused = h.Device->Draws_Recorded() == recorded && h.Device->Draw_Refusals().count("table fog") == 1;
			if (!refused) { ++failures; printf("FAIL %s: not refused as table fog\n", FOGS[i].name); }
			else printf("ok   %-40s refused, as it should be\n", FOGS[i].name);
			continue;
		}
		h.check(FOGS[i].name);
	}

	h.begin(0xFF000000);
	h.rs(D3DRS_LIGHTING, 0);
	h.rs(D3DRS_ALPHATESTENABLE, 1);
	h.rs(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
	h.rs(D3DRS_ALPHAREF, 0x80);
	uint32_t alpha_corners[4] = { 0x00FF0000, 0xFF00FF00, 0x400000FF, 0xC0FFFFFF };
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(2, 2, 62, 62, alpha_corners));
	h.check("alpha test GREATER 0x80");

	static const struct { const char *name; RenderUInt32 source, destination, operation; } BLENDS[] = {
		{ "blend srcalpha/invsrcalpha", D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD },
		{ "blend one/one", D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD },
		{ "blend destcolor/zero", D3DBLEND_DESTCOLOR, D3DBLEND_ZERO, D3DBLENDOP_ADD },
		{ "blend revsubtract", D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT },
		{ "blend bothsrcalpha", D3DBLEND_BOTHSRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD } };
	for (int i = 0; i < 5; ++i) {
		h.begin(0xFF406080);
		h.rs(D3DRS_LIGHTING, 0);
		h.rs(D3DRS_ALPHABLENDENABLE, 1);
		h.rs(D3DRS_SRCBLEND, BLENDS[i].source);
		h.rs(D3DRS_DESTBLEND, BLENDS[i].destination);
		h.rs(D3DRS_BLENDOP, BLENDS[i].operation);
		h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(2, 2, 62, 62, alpha_corners));
		h.check(BLENDS[i].name);
	}

	h.begin(0xFF406080);
	h.rs(D3DRS_LIGHTING, 0);
	h.rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_ALPHA);
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(2, 2, 62, 62, CORNERS));
	h.check("colour write mask");
}

static uint32_t grey_level(int level, int, int)
{
	const uint32_t g = (uint32_t)(level * 36);
	return 0xFF000000u | (g << 16) | (g << 8) | g;
}

// Not a check: each level a solid grey of level * 36, filtered trilinearly, so a pixel's grey / 36 is the
// LOD the sampler used.  Prints the GPU's LOD against the reference's, for the LOD freedom's width.
static void lod_probe(Harness &h, float u_scale, float v_scale)
{
	h.begin(0xFF000000);
	h.rs(D3DRS_LIGHTING, 0);
	set_camera(h);
	h.texture(0, 64, 64, 7, grey_level);
	h.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	h.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	h.tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	std::vector<V> floor = grid(4, -4, 4, -1.5f, -1.5f, 2, 30, 0);
	for (size_t k = 0; k < floor.size(); ++k) { floor[k].tex[0][0] *= u_scale; floor[k].tex[0][1] *= v_scale; }
	h.draw(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPT_TRIANGLELIST, floor);
	SdlGpuFrame *gpu = h.Device->Get_Gpu();
	std::vector<uint8_t> bgra;
	gpu->Read_Back(gpu->Back_Buffer(), TARGET_SIZE, TARGET_SIZE, bgra);
	double worst = 0.0, sum = 0.0;
	int count = 0, worst_x = -1, worst_y = -1;
	for (int y = 0; y < TARGET_SIZE; ++y) {
		for (int x = 0; x < TARGET_SIZE; ++x) {
			const double reference = h.Reference.color[y * TARGET_SIZE + x].r * 255.0 / 36.0;
			const double measured = bgra[(y * TARGET_SIZE + x) * 4 + 2] / 36.0;
			if (h.Reference.color[y * TARGET_SIZE + x].r <= 0.0 || h.Reference.color[y * TARGET_SIZE + x].r >= 216.0 / 255.0) continue;
			const double d = measured - reference;
			sum += d;
			++count;
			if (fabs(d) > fabs(worst)) { worst = d; worst_x = x; worst_y = y; }
		}
	}
	printf("  lod probe (u x%.0f, v x%.0f): %d pixels between levels, mean gpu - reference %+.3f, worst %+.3f at (%d, %d)\n",
		u_scale, v_scale, count, count ? sum / count : 0.0, worst, worst_x, worst_y);
}

static void armed_controls(Harness &h)
{
	h.begin(0xFF000000);
	h.rs(D3DRS_LIGHTING, 0);
	h.draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPT_TRIANGLELIST, screen_quad(5.25f, 7.75f, 58.5f, 50.25f, CORNERS),
		FFRef::MUTATE_RASTER_HALF_PIXEL);
	h.check("armed: D3D10 pixel centres", true);

	h.begin(0xFF000000);
	h.texture(0, 4, 4, 1, gradient);
	h.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	h.tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	h.draw(SCREEN, D3DPT_TRIANGLELIST, screen_quad(-0.5f, -0.5f, 63.5f, 63.5f, CORNERS), FFRef::MUTATE_TEXEL_CORNER);
	h.check("armed: texel centres at corners", true);
}

int main()
{
	IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = TARGET_SIZE;
	parameters.BackBufferHeight = TARGET_SIZE;
	parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *created = NULL;
	if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &created) != D3D_OK) {
		printf("ffref_gpu: FAIL - no device\n");
		return 1;
	}
	PosixDevice9 *device = static_cast<PosixDevice9 *>(created);
	if (!SDL_Init(SDL_INIT_VIDEO) || device->Create_Gpu_Frame(true) != D3D_OK) {
		printf("ffref_gpu: SKIP - no GPU device here: %s\n", SDL_GetError());
		device->Release();
		d3d->Release();
		return 77;
	}
	{
		Harness h(device);
		scenarios_raster(h);
		scenarios_textures(h);
		scenarios_cascade(h);
		scenarios_lighting(h);
		scenarios_fog_blend_test(h);
		armed_controls(h);
		lod_probe(h, 4, 4);
		lod_probe(h, 4, 16);
		lod_probe(h, 16, 4);
		for (int stage = 0; stage < 8; ++stage) device->SetTexture(stage, NULL);
		h.release_textures();
	}
	for (std::map<std::string, unsigned int>::const_iterator it = device->Draw_Refusals().begin();
		it != device->Draw_Refusals().end(); ++it) {
		printf("  device refused %u: %s\n", it->second, it->first.c_str());
	}
	printf("ffref_gpu: %s: %d scenarios, %d failed, %d known findings\n", SDL_GetGPUDeviceDriver(device->Get_Gpu()->Device()),
		scenarios, failures, known_findings);
	device->Release();
	d3d->Release();
	SDL_Quit();
	return failures == 0 ? 0 : 1;
}
