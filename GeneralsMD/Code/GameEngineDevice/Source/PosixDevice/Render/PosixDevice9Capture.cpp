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

// The capture writer (DrawCapture.h): ZH_GPU_CAPTURE=<dir> writes the first draw of each signature there,
// ZH_GPU_CAPTURE_MB (64 by default) caps what it writes.  Everything is read from A2's CPU side, which is
// what the draw's GPU copies are made from, so capturing changes nothing the draw does.

#include "PosixDevice9.h"
#include "PosixResources9.h"
#include "DrawCapture.h"

#include <map>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

struct CaptureState
{
	bool Asked;
	std::string Directory;
	uint64_t Budget;
	uint64_t Written;
	unsigned int Captured;
	std::set<std::string> Signatures;
	std::set<std::string> TextureNames;
	std::map<std::string, std::set<std::string> > Skipped;	///< reason: the signatures skipped for it
	std::map<std::string, unsigned int> OffScreen;		///< a signature's draws passed over as off screen
	unsigned int TakenOffScreen;
};

/// How many off-screen draws of a signature are passed over before one is taken anyway.
enum { OFF_SCREEN_TRIES = 64 };

CaptureState &capture_state()
{
	static CaptureState state;
	static bool made = false;
	if (!made) {
		made = true;
		const char *directory = getenv("ZH_GPU_CAPTURE");
		state.Asked = directory != NULL && directory[0] != '\0';
		state.Directory = state.Asked ? directory : "";
		const char *megabytes = getenv("ZH_GPU_CAPTURE_MB");
		state.Budget = (uint64_t)(megabytes != NULL ? strtoul(megabytes, NULL, 10) : 64) << 20;
		state.Written = 0;
		state.Captured = 0;
		state.TakenOffScreen = 0;
	}
	return state;
}

uint64_t fnv1a(const void *data, size_t size, uint64_t hash = 14695981039346656037ull)
{
	const uint8_t *bytes = (const uint8_t *)data;
	for (size_t i = 0; i < size; ++i) {
		hash = (hash ^ bytes[i]) * 1099511628211ull;
	}
	return hash;
}

void skip(CaptureState &state, const std::string &signature, const std::string &reason)
{
	// Counted by signature, not by draw: the same draw comes every frame, and a later one of the
	// signature may still be captured.
	state.Skipped[reason].insert(signature);
}

/// Whether any of the vertices lands inside the view volume: -w <= x, y <= w and 0 <= z <= w after the
/// world, view and projection, or inside the viewport for pretransformed ones.  The first draw of a
/// signature is often a model or a terrain tile off screen, and a replay of it compares nothing.
bool any_vertex_in_view(const uint8_t *vertices, uint32_t count, unsigned int stride, bool pretransformed,
	const D3DVIEWPORT9 &viewport, const D3DMATRIX &world, const D3DMATRIX &view, const D3DMATRIX &projection)
{
	float m[16], wv[16];
	const float *w = &world._11, *v = &view._11, *p = &projection._11;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			wv[r * 4 + c] = w[r * 4] * v[c] + w[r * 4 + 1] * v[4 + c] + w[r * 4 + 2] * v[8 + c] + w[r * 4 + 3] * v[12 + c];
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			m[r * 4 + c] = wv[r * 4] * p[c] + wv[r * 4 + 1] * p[4 + c] + wv[r * 4 + 2] * p[8 + c] + wv[r * 4 + 3] * p[12 + c];
	for (uint32_t i = 0; i < count; ++i) {
		float position[3];
		memcpy(position, vertices + (size_t)i * stride, sizeof(position));
		if (pretransformed) {
			if (position[0] >= viewport.X && position[0] < viewport.X + viewport.Width
				&& position[1] >= viewport.Y && position[1] < viewport.Y + viewport.Height) {
				return true;
			}
			continue;
		}
		float clip[4];
		for (int c = 0; c < 4; ++c) {
			clip[c] = position[0] * m[c] + position[1] * m[4 + c] + position[2] * m[8 + c] + m[12 + c];
		}
		if (clip[3] > 0.0f && clip[0] >= -clip[3] && clip[0] <= clip[3] && clip[1] >= -clip[3] && clip[1] <= clip[3]
			&& clip[2] >= 0.0f && clip[2] <= clip[3]) {
			return true;
		}
	}
	return false;
}

}  // namespace

bool PosixDevice9::Capture_Is_Asked()
{
	return capture_state().Asked;
}

void PosixDevice9::Capture_Draw(const DrawCall &call, const std::string &signature, unsigned int stride,
	unsigned int reads, unsigned int sampled_stages, unsigned int target_width, unsigned int target_height)
{
	CaptureState &state = capture_state();
	if (!state.Asked || state.Signatures.count(signature) != 0) {
		return;
	}
	if (state.Written >= state.Budget) {
		skip(state, signature, "over ZH_GPU_CAPTURE_MB");
		return;
	}

	// The textures the program samples: 2D ones only, and none whose pixels are the GPU's.
	PosixTexture9 *textures[DRAW_CAPTURE_STAGES] = {};
	for (unsigned int stage = 0; stage < DRAW_CAPTURE_STAGES; ++stage) {
		if ((sampled_stages & (1u << stage)) == 0 || Textures[stage] == NULL) {
			continue;
		}
		if (Textures[stage]->GetType() != D3DRTYPE_TEXTURE) {
			skip(state, signature, "a cube or volume texture");
			return;
		}
		PosixTexture9 *texture = static_cast<PosixTexture9 *>(Textures[stage]);
		D3DSURFACE_DESC desc;
		if (texture->GetLevelDesc(0, &desc) != D3D_OK || (desc.Usage & D3DUSAGE_RENDERTARGET) != 0) {
			skip(state, signature, "samples a render target (version 1)");
			return;
		}
		textures[stage] = texture;
	}

	// The vertices the draw reads, and its indices rebased to the first of them.
	const uint8_t *vertex_bytes = NULL;
	size_t vertex_limit = 0;
	if (call.UserVertices != NULL) {
		vertex_bytes = (const uint8_t *)call.UserVertices;
		vertex_limit = (size_t)reads * stride;
	}
	else {
		const PosixBufferStorage &storage = static_cast<PosixVertexBuffer9 *>(Streams[0])->storage();
		if (StreamOffsets[0] > storage.length()) {
			skip(state, signature, "a stream offset past its buffer");
			return;
		}
		vertex_bytes = storage.bytes() + StreamOffsets[0];
		vertex_limit = storage.length() - StreamOffsets[0];
	}
	std::vector<uint32_t> indices;
	uint32_t first = 0, count = reads;
	if (call.Indexed) {
		PosixIndexBuffer9 *index_buffer = static_cast<PosixIndexBuffer9 *>(Indices);
		const unsigned int index_size = index_buffer->format() == D3DFMT_INDEX32 ? 4 : 2;
		const PosixBufferStorage &storage = index_buffer->storage();
		if ((size_t)(call.StartIndex + reads) * index_size > storage.length()) {
			skip(state, signature, "indices past their buffer");
			return;
		}
		const uint8_t *source = storage.bytes() + (size_t)call.StartIndex * index_size;
		int64_t lowest = INT64_MAX, highest = INT64_MIN;
		indices.resize(reads);
		for (unsigned int i = 0; i < reads; ++i) {
			uint32_t index;
			if (index_size == 4) {
				memcpy(&index, source + (size_t)i * 4, 4);
			}
			else {
				uint16_t short_index;
				memcpy(&short_index, source + (size_t)i * 2, 2);
				index = short_index;
			}
			const int64_t vertex = (int64_t)index + call.BaseVertex;
			indices[i] = (uint32_t)index;
			lowest = vertex < lowest ? vertex : lowest;
			highest = vertex > highest ? vertex : highest;
		}
		if (lowest < 0) {
			skip(state, signature, "a negative vertex index");
			return;
		}
		first = (uint32_t)lowest;
		count = (uint32_t)(highest - lowest + 1);
		for (unsigned int i = 0; i < reads; ++i) {
			indices[i] = (uint32_t)((int64_t)indices[i] + call.BaseVertex - lowest);
		}
	}
	else if (call.UserVertices == NULL) {
		first = call.StartVertex;
	}
	if (((size_t)first + count) * stride > vertex_limit) {
		skip(state, signature, "vertices past their buffer");
		return;
	}
	// A transcribed vertex program places its vertices from its own constants, so the transforms say
	// nothing about where they land: its first draw is taken.
	if (VertexShader == NULL
		&& !any_vertex_in_view(vertex_bytes + (size_t)first * stride, count, stride, (FVF & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW,
			Viewport, Transforms[D3DTS_WORLD], Transforms[D3DTS_VIEW], Transforms[D3DTS_PROJECTION])) {
		if (++state.OffScreen[signature] <= OFF_SCREEN_TRIES) {
			return;
		}
		++state.TakenOffScreen;
	}

	DrawCaptureHeader header;
	memset(&header, 0, sizeof(header));
	memcpy(header.Magic, "ZHDC", 4);
	const bool programmable = VertexShader != NULL || PixelShader != NULL;
	header.Version = programmable ? DRAW_CAPTURE_VERSION_PROGRAMMABLE : DRAW_CAPTURE_VERSION;
	header.Primitive = call.Type;
	header.PrimitiveCount = call.PrimitiveCount;
	header.FVF = FVF;
	header.Stride = stride;
	header.VertexCount = count;
	header.IndexCount = (uint32_t)indices.size();
	header.TargetWidth = target_width;
	header.TargetHeight = target_height;
	D3DSURFACE_DESC target_desc;
	header.TargetFormat = RenderTargets[0]->GetDesc(&target_desc) == D3D_OK ? target_desc.Format : D3DFMT_UNKNOWN;
	header.DepthBound = DepthStencil != NULL;
	header.Viewport = Viewport;
	memcpy(header.RenderStates, RenderStates, sizeof(header.RenderStates));
	for (unsigned int stage = 0; stage < DRAW_CAPTURE_STAGES; ++stage) {
		memcpy(header.StageStates[stage], TextureStageStates[stage], sizeof(header.StageStates[stage]));
		memcpy(header.SamplerStates[stage], SamplerStates[stage], sizeof(header.SamplerStates[stage]));
		header.TextureMatrices[stage] = Transforms[D3DTS_TEXTURE0 + stage];
	}
	header.World = Transforms[D3DTS_WORLD];
	header.View = Transforms[D3DTS_VIEW];
	header.Projection = Transforms[D3DTS_PROJECTION];
	header.Material = Material;
	for (unsigned int light = 0; light < LIGHT_COUNT; ++light) {
		header.Lights[light] = Lights[light];
		header.LightsEnabled[light] = LightsEnabled[light] ? 1 : 0;
	}
	snprintf(header.Signature, sizeof(header.Signature), "%s", signature.c_str());

	// The textures, each written once per content.
	for (unsigned int stage = 0; stage < DRAW_CAPTURE_STAGES; ++stage) {
		PosixTexture9 *texture = textures[stage];
		if (texture == NULL) {
			continue;
		}
		const PosixImage &top = texture->level(0);
		DrawCaptureTexture description;
		memset(&description, 0, sizeof(description));
		memcpy(description.Magic, "ZHTX", 4);
		description.Format = top.format();
		description.Width = top.width();
		description.Height = top.height();
		description.Levels = texture->levelCount();
		uint64_t hash = fnv1a(&description, sizeof(description));
		for (unsigned int level = 0; level < description.Levels; ++level) {
			hash = fnv1a(texture->level(level).bytes(), texture->level(level).size(), hash);
		}
		char name[DRAW_CAPTURE_NAME];
		snprintf(name, sizeof(name), "%016llx.tex", (unsigned long long)hash);
		snprintf(header.Textures[stage], sizeof(header.Textures[stage]), "%s", name);
		if (state.TextureNames.count(name) != 0) {
			continue;
		}
		const std::string path = state.Directory + "/" + name;
		FILE *file = fopen(path.c_str(), "wb");
		if (file == NULL) {
			skip(state, signature, "a texture file could not be written");
			return;
		}
		fwrite(&description, sizeof(description), 1, file);
		state.Written += sizeof(description);
		for (unsigned int level = 0; level < description.Levels; ++level) {
			const PosixImage &image = texture->level(level);
			const uint32_t size = (uint32_t)image.size();
			fwrite(&size, sizeof(size), 1, file);
			fwrite(image.bytes(), 1, size, file);
			state.Written += sizeof(size) + size;
		}
		fclose(file);
		state.TextureNames.insert(name);
	}

	char leaf[32];
	snprintf(leaf, sizeof(leaf), "/draw_%05u.cap", state.Captured);
	const std::string path = state.Directory + leaf;
	FILE *file = fopen(path.c_str(), "wb");
	if (file == NULL) {
		skip(state, signature, "a draw file could not be written");
		return;
	}
	fwrite(&header, sizeof(header), 1, file);
	fwrite(vertex_bytes + (size_t)first * stride, stride, count, file);
	if (!indices.empty()) {
		fwrite(&indices[0], sizeof(uint32_t), indices.size(), file);
	}
	fclose(file);
	state.Written += sizeof(header) + (uint64_t)count * stride + indices.size() * sizeof(uint32_t);
	if (programmable) {
		char program_leaf[32];
		snprintf(program_leaf, sizeof(program_leaf), "/draw_%05u.prog", state.Captured);
		state.Written += Write_Programs(state.Directory + program_leaf, stride);
	}
	state.Signatures.insert(signature);
	++state.Captured;
}

uint64_t PosixDevice9::Write_Programs(const std::string &path, unsigned int stride)
{
	FILE *file = fopen(path.c_str(), "wb");
	if (file == NULL) {
		return 0;
	}
	uint64_t written = 0;
	struct Out
	{
		static void bytes(FILE *file, uint64_t &written, const void *data, size_t size)
		{
			fwrite(data, 1, size, file);
			written += size;
		}
		static void u32(FILE *file, uint64_t &written, uint32_t value) { bytes(file, written, &value, 4); }
		static void program(FILE *file, uint64_t &written, const void *shader)
		{
			std::vector<RenderUInt32> tokens;
			const bool present = shader != NULL && Shader_Tokens_Of(shader, tokens);
			u32(file, written, present ? 1 : 0);
			if (!present) {
				return;
			}
			char name[DRAW_CAPTURE_PROGRAM_NAME];
			memset(name, 0, sizeof(name));
			snprintf(name, sizeof(name), "%s", Engine_Name_Of(shader).c_str());
			bytes(file, written, name, sizeof(name));
			u32(file, written, (uint32_t)tokens.size());
			bytes(file, written, &tokens[0], tokens.size() * sizeof(RenderUInt32));
		}
	};
	Out::bytes(file, written, "ZHPG", 4);
	Out::u32(file, written, 2);	// the .prog version: 2 appends the D3D8 declaration and the streams (capture v3)
	Out::program(file, written, VertexShader);
	std::vector<D3DVERTEXELEMENT9> elements;
	Declaration_Elements_Of(DeclarationIsCurrent ? Declaration : NULL, elements);
	Out::u32(file, written, (uint32_t)elements.size());
	for (size_t i = 0; i < elements.size(); ++i) {
		const uint16_t stream = (uint16_t)elements[i].Stream, offset = (uint16_t)elements[i].Offset;
		const uint8_t small[4] = { (uint8_t)elements[i].Type, (uint8_t)elements[i].Method, (uint8_t)elements[i].Usage,
			(uint8_t)elements[i].UsageIndex };
		Out::bytes(file, written, &stream, 2);
		Out::bytes(file, written, &offset, 2);
		Out::bytes(file, written, small, 4);
	}
	Out::program(file, written, PixelShader);
	Out::bytes(file, written, VertexShaderConstants, DRAW_CAPTURE_VS_CONSTANTS * sizeof(VertexShaderConstants[0]));
	Out::bytes(file, written, PixelShaderConstants, DRAW_CAPTURE_PS_CONSTANTS * sizeof(PixelShaderConstants[0]));
	// Version 2's additions (capture v3): the engine's own D3D8 declaration, when the current one came
	// from one, and the streams the draw reads - stream 0 only, the one the device draws from: its stride,
	// and where the first stored vertex is in the .cap's vertex bytes (0: they start at it).
	std::vector<RenderUInt32> d3d8;
	Declaration_D3D8_Tokens_Of(DeclarationIsCurrent ? Declaration : NULL, d3d8);
	Out::u32(file, written, (uint32_t)d3d8.size());
	if (!d3d8.empty()) {
		Out::bytes(file, written, &d3d8[0], d3d8.size() * sizeof(RenderUInt32));
	}
	Out::u32(file, written, 1);
	Out::u32(file, written, 0);
	Out::u32(file, written, stride);		// the draw's: the stream's, or DrawPrimitiveUP's own
	Out::u32(file, written, 0);
	fclose(file);
	return written;
}

void PosixDevice9::Capture_Report()
{
	CaptureState &state = capture_state();
	if (!state.Asked) {
		return;
	}
	fprintf(stderr, "PosixDevice9: captured %u draws and %zu textures to %s, %.1f MB; %u of the draws off screen"
		" (their signature had no draw on screen in %d tries)\n", state.Captured, state.TextureNames.size(),
		state.Directory.c_str(), state.Written / 1048576.0, state.TakenOffScreen, (int)OFF_SCREEN_TRIES);
	for (std::map<std::string, std::set<std::string> >::const_iterator it = state.Skipped.begin(); it != state.Skipped.end(); ++it) {
		fprintf(stderr, "PosixDevice9:   signatures not captured %zu: %s\n", it->second.size(), it->first.c_str());
	}
}
