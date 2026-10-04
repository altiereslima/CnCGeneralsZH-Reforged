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
 * w3d_view - one real game model through SDL3's GPU API (the D-spike).
 *
 * Reads a W3D model and its DDS textures from the game's own .big archives (read only), and draws
 * it with an orbit camera: Metal underneath on macOS, Vulkan on Linux.  It exists to meet the
 * game's data with the API decision 3 chose before D2 designs an interface around that API
 * (PORTING.md, decision 4, has what it found).  Not a test, not in ctest.
 *
 *   w3d_view [--data DIR] [--model NAME] [--shaders glsl|hlsl|hand] [--depth auto|d24s8|d32s8]
 *            [--size WxH] [--yaw DEG] [--pitch DEG] [--zoom F]
 *            [--screenshot FILE.png] [--offscreen] [--frames N]
 *
 *   --data       a folder holding zerohour/ and generals/ (default: $ZH_DATA_DIR)
 *   --model      a W3D name without extension (default: avcrusader, the Crusader tank)
 *   --shaders    which program draws: glsl (the default; GLSL compiled by glslang, MSL translated
 *                from that SPIR-V), hlsl (HLSL through glslang's HLSL front end, then the same),
 *                hand (MSL written by hand; Metal only) - the three routes in
 *                shaders/build_shaders.sh - or generated: the game's own programs, written at run
 *                time by ffvertex and ffshader for the SDL3 GPU target and compiled through
 *                sdl3shadercompile, as the SDL3 backend will (D3)
 *   --depth      auto takes D24S8 and falls back to D32S8, as the game's backend will; the others
 *                force one, to exercise a path this machine would not choose
 *   --screenshot renders, saves the frame as PNG and exits (after --frames frames, default 3)
 *   --offscreen  no window at all: renders to a texture only, for a machine with a GPU and no
 *                display (lavapipe in a container)
 *
 * Mouse: drag to orbit, wheel to zoom.  Escape quits.
 */
#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "big_archive.h"
#include "dds_image.h"
#include "w3d_model.h"
#include "shaders/model_shaders.h"

#include "ffshader.h"
#include "ffvertex.h"
#include "sdl3shadercompile.h"

namespace {

// ---------------------------------------------------------------------------------------------
// The one mapping from what the game asks for to what this device can do.  The game creates
// D24S8 (WW3D2/dx11device.cpp) and its shadow volumes need the stencil, so the fallback must keep
// a stencil: D32S8.  Apple GPUs have no D24S8 at all (measured by sdl_gpu_probe, D2).
// D2's backend needs exactly this function; here it is written once, before D2 exists.
// ---------------------------------------------------------------------------------------------
SDL_GPUTextureFormat mapDepthStencilFormat(SDL_GPUDevice *device, const char *force)
{
	static const SDL_GPUTextureFormat PREFERENCE[] = {
		SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,	// what the game asks for
		SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,	// the stencil-bearing fallback
	};
	for (size_t i = 0; i < sizeof(PREFERENCE) / sizeof(PREFERENCE[0]); ++i) {
		if (strcmp(force, "d24s8") == 0 && PREFERENCE[i] != SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT) continue;
		if (strcmp(force, "d32s8") == 0 && PREFERENCE[i] != SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT) continue;
		if (SDL_GPUTextureSupportsFormat(device, PREFERENCE[i], SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
			return PREFERENCE[i];
		}
	}
	return SDL_GPU_TEXTUREFORMAT_INVALID;
}

const char *depthName(SDL_GPUTextureFormat format)
{
	switch (format) {
		case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
		case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT: return "D32_FLOAT_S8_UINT";
		default: return "none";
	}
}

// ---------------------------------------------------------------------------------------------
// Matrices, column-major as both GLSL and MSL read a mat4 / float4x4 from a uniform buffer.
// ---------------------------------------------------------------------------------------------
struct Mat4
{
	float m[16];
	float &at(int row, int column) { return m[column * 4 + row]; }
	float at(int row, int column) const { return m[column * 4 + row]; }
};

Mat4 multiply(const Mat4 &a, const Mat4 &b)
{
	Mat4 r;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (int k = 0; k < 4; ++k) sum += a.at(row, k) * b.at(k, column);
			r.at(row, column) = sum;
		}
	}
	return r;
}

void normalise3(float v[3])
{
	const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (length > 0.0f) { v[0] /= length; v[1] /= length; v[2] /= length; }
}

void cross3(const float a[3], const float b[3], float out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

// Right-handed, as W3D's camera is (CameraClass looks down its local -Z).
Mat4 lookAt(const float eye[3], const float target[3], const float up[3])
{
	float forward[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
	normalise3(forward);
	float side[3];
	cross3(forward, up, side);
	normalise3(side);
	float trueUp[3];
	cross3(side, forward, trueUp);
	Mat4 view;
	memset(&view, 0, sizeof(view));
	for (int i = 0; i < 3; ++i) {
		view.at(0, i) = side[i];
		view.at(1, i) = trueUp[i];
		view.at(2, i) = -forward[i];
	}
	view.at(0, 3) = -(side[0] * eye[0] + side[1] * eye[1] + side[2] * eye[2]);
	view.at(1, 3) = -(trueUp[0] * eye[0] + trueUp[1] * eye[1] + trueUp[2] * eye[2]);
	view.at(2, 3) = forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2];
	view.at(3, 3) = 1.0f;
	return view;
}

// Depth 0 at the near plane and 1 at the far, Y up: the clip space SDL_gpu presents on every backend
// (Direct3D's, and Metal's; its Vulkan backend flips the viewport to match).
Mat4 perspective(float verticalFovRadians, float aspect, float zNear, float zFar)
{
	const float f = 1.0f / tanf(verticalFovRadians * 0.5f);
	Mat4 p;
	memset(&p, 0, sizeof(p));
	p.at(0, 0) = f / aspect;
	p.at(1, 1) = f;
	p.at(2, 2) = zFar / (zNear - zFar);
	p.at(2, 3) = zNear * zFar / (zNear - zFar);
	p.at(3, 2) = -1.0f;
	return p;
}

// ---------------------------------------------------------------------------------------------
// Uniform blocks, laid out as model.vert / model.frag (std140) and model.hand.metal declare them.
// ---------------------------------------------------------------------------------------------
struct VertexUniforms
{
	float viewProjection[16];
};

struct FragmentUniforms
{
	float materialDiffuse[4];
	float tint[4];
	float ambient[4];
	float lightDirection[4];
	float lightColor[4];	// w: alpha test reference, 0 for none
};

// The game's alpha test reference: dx8renderer.cpp sets D3DRS_ALPHAREF to 0x60.
const float ALPHA_TEST_REFERENCE = (float)0x60 / 255.0f;
// A player colour for HOUSECOLOR meshes.  The game recolours those textures on the CPU
// (W3DAssetManager::Recolor_Texture, a 16-step palette scale); the spike multiplies in the shader.
const float HOUSE_TINT[4] = { 0.20f, 0.40f, 1.00f, 1.0f };

// ---------------------------------------------------------------------------------------------
// --shaders generated: the game's own programs.  What a W3D mesh draw sets in fixed-function state -
// lit, one directional light, the material's colours, one texture stage modulating the lit colour
// - handed to ffvertex and ffshader for the SDL3 GPU target, and compiled the way the SDL3 backend
// will compile them (sdl3shadercompile.h).  The constant blocks are the generators' own, in the
// order append_constants_d3d11 and ffshader declare them; every field a float4 or a row_major
// float4x4, so the layout is the declaration order.
// ---------------------------------------------------------------------------------------------
struct GeneratedVertexConstants
{
	float WorldViewProjection[16];	// row_major: a column-major matrix's floats, as they are
	float WorldView[16];
	float NormalTransform[16];
	float TextureMatrix[MAXIMUM_VERTEX_STAGES][16];
	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialSpecular[4];
	float MaterialEmissive[4];
	float MaterialPower[4];
	float GlobalAmbient[4];
	float FogParameters[4];
	float ViewportInverse[4];
	float Light0Position[4];
	float Light0Direction[4];
	float Light0Diffuse[4];
	float Light0Specular[4];
	float Light0Attenuation[4];
	float Light0Spot[4];
};

struct GeneratedPixelConstants
{
	float TextureFactor[4];
	float FogColour[4];
	float AlphaReference[4];	// x: the reference in whole levels, as ffshader compares it
};

// The vertex attributes by semantic, as sdl3target.h numbers them: POSITION 0, NORMAL 1, TEXCOORD0 4.
const Uint32 GENERATED_LOCATIONS[3] = { 0, 1, 4 };
const Uint32 SPIKE_LOCATIONS[3] = { 0, 1, 2 };

SDL_GPUShader *generatedShader(SDL_GPUDevice *device, const std::string &hlsl, bool vertexStage, const char *what,
	unsigned *samplerSlots)
{
	std::vector<unsigned char> spirv;
	std::string log;
	if (!SDL3_Compile_HLSL_To_SPIRV(hlsl, vertexStage, spirv, log)) {
		fprintf(stderr, "w3d_view: %s: HLSL -> SPIR-V failed:\n%s\n", what, log.c_str());
		return NULL;
	}
	SDL_GPUShader *shader = SDL3_Create_Shader(device, spirv, vertexStage, log);
	if (shader == NULL) {
		fprintf(stderr, "w3d_view: %s: the device refused it: %s\n", what, log.c_str());
		return NULL;
	}
	unsigned samplers = 0, uniformBuffers = 0;
	SDL3_Shader_Slots(spirv, vertexStage, samplers, uniformBuffers);
	if (samplerSlots != NULL && samplers > *samplerSlots) *samplerSlots = samplers;
	printf("generated %s: %zu bytes of HLSL, %zu of SPIR-V, %u sampler slot(s), %u constant buffer(s)\n", what,
		hlsl.size(), spirv.size(), samplers, uniformBuffers);
	return shader;
}

// The mesh draw's fixed-function state, as W3D sets it for a lit, textured, uncoloured mesh.
VertexPipelineDescription meshVertexState()
{
	VertexPipelineDescription description = VertexPipelineDescription();	// zero, and NormalMapped false
	description.FVF = FF_FVF_XYZ | FF_FVF_NORMAL | FF_FVF_TEX1;
	description.LightingEnabled = true;
	description.LightCount = 1;
	description.Lights[0].Type = FF_LIGHT_DIRECTIONAL;
	description.DiffuseMaterialSource = FF_MCS_MATERIAL;
	description.AmbientMaterialSource = FF_MCS_MATERIAL;
	description.EmissiveMaterialSource = FF_MCS_MATERIAL;
	description.SpecularMaterialSource = FF_MCS_MATERIAL;
	description.StageCount = 1;
	description.Stages[0].TextureCoordinateIndex = FF_TSS_TCI_PASSTHRU;
	description.Stages[0].TextureTransformFlags = FF_TTFF_DISABLE;
	description.FogVertexMode = FF_FOG_NONE;
	return description;
}

// Stage 0 modulating the texture by the lit colour, colour and alpha both.  With the alpha test the
// way shader.cpp sets it for an alpha-tested W3D shader: GREATEREQUAL against 0x60.
CombinerDescription meshPixelState(bool alphaTest)
{
	CombinerDescription description;
	memset(&description.Stages, 0, sizeof(description.Stages));
	description.StageCount = 1;
	description.Stages[0].ColourOperation = FF_TOP_MODULATE;
	description.Stages[0].ColourArgument1 = FF_TA_TEXTURE;
	description.Stages[0].ColourArgument2 = FF_TA_DIFFUSE;
	description.Stages[0].AlphaOperation = FF_TOP_MODULATE;
	description.Stages[0].AlphaArgument1 = FF_TA_TEXTURE;
	description.Stages[0].AlphaArgument2 = FF_TA_DIFFUSE;
	description.Stages[0].TextureCoordinateIndex = 0;
	description.Stages[0].TextureBound = true;
	memset(&description.PixelPipeline, 0, sizeof(description.PixelPipeline));
	description.PixelPipeline.AlphaTestEnabled = alphaTest;
	description.PixelPipeline.AlphaFunction = FF_CMP_GREATEREQUAL;
	description.NormalMapped = false;
	description.ShadowReceiving = false;
	return description;
}

struct Options
{
	std::string dataDir;
	std::string model;
	std::string shaders;
	std::string depth;
	std::string screenshot;
	bool offscreen;
	int frames;
	int width;
	int height;
	float yaw;
	float pitch;
	float zoom;
};

bool parseOptions(int argc, char **argv, Options *options)
{
	const char *env = getenv("ZH_DATA_DIR");
	options->dataDir = env != NULL ? env : "";
	options->model = "avcrusader";
	options->shaders = "glsl";
	options->depth = "auto";
	options->offscreen = false;
	options->frames = 3;
	options->width = 1280;
	options->height = 800;
	options->yaw = 215.0f;
	options->pitch = 25.0f;
	options->zoom = 0.8f;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const bool hasValue = i + 1 < argc;
		if (arg == "--data" && hasValue) options->dataDir = argv[++i];
		else if (arg == "--model" && hasValue) options->model = argv[++i];
		else if (arg == "--shaders" && hasValue) options->shaders = argv[++i];
		else if (arg == "--depth" && hasValue) options->depth = argv[++i];
		else if (arg == "--screenshot" && hasValue) options->screenshot = argv[++i];
		else if (arg == "--frames" && hasValue) options->frames = atoi(argv[++i]);
		else if (arg == "--yaw" && hasValue) options->yaw = (float)atof(argv[++i]);
		else if (arg == "--pitch" && hasValue) options->pitch = (float)atof(argv[++i]);
		else if (arg == "--zoom" && hasValue) options->zoom = (float)atof(argv[++i]);
		else if (arg == "--size" && hasValue) {
			if (sscanf(argv[++i], "%dx%d", &options->width, &options->height) != 2) return false;
		}
		else if (arg == "--offscreen") options->offscreen = true;
		else return false;
	}
	if (options->shaders != "glsl" && options->shaders != "hlsl" && options->shaders != "hand"
		&& options->shaders != "generated") return false;
	if (options->depth != "auto" && options->depth != "d24s8" && options->depth != "d32s8") return false;
	if (options->offscreen && options->screenshot.empty()) return false;
	return true;
}

// ---------------------------------------------------------------------------------------------
// GPU resources
// ---------------------------------------------------------------------------------------------

SDL_GPUShader *createShader(SDL_GPUDevice *device, SDL_GPUShaderFormat format, SDL_GPUShaderStage stage,
	const void *code, size_t size, const char *entry, Uint32 samplers, Uint32 uniformBuffers)
{
	SDL_GPUShaderCreateInfo info;
	SDL_zero(info);
	info.code = (const Uint8 *)code;
	info.code_size = size;
	info.entrypoint = entry;
	info.format = format;
	info.stage = stage;
	info.num_samplers = samplers;
	info.num_uniform_buffers = uniformBuffers;
	SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);
	if (shader == NULL) {
		fprintf(stderr, "w3d_view: %s shader: %s\n", stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vertex" : "fragment", SDL_GetError());
	}
	return shader;
}

SDL_GPUTexture *uploadDds(SDL_GPUDevice *device, const DdsImage &image)
{
	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = image.format == DDS_BC1 ? SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM
		: (image.format == DDS_BC2 ? SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM : SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM);
	info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = image.levels[0].width;
	info.height = image.levels[0].height;
	info.layer_count_or_depth = 1;
	info.num_levels = (Uint32)image.levels.size();
	SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);
	if (texture == NULL) {
		fprintf(stderr, "w3d_view: texture: %s\n", SDL_GetError());
		return NULL;
	}

	size_t total = 0;
	for (size_t i = 0; i < image.levels.size(); ++i) total += image.levels[i].size;
	SDL_GPUTransferBufferCreateInfo transferInfo;
	SDL_zero(transferInfo);
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size = (Uint32)total;
	SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
	Uint8 *mapped = (Uint8 *)SDL_MapGPUTransferBuffer(device, transfer, false);
	size_t at = 0;
	for (size_t i = 0; i < image.levels.size(); ++i) {
		memcpy(mapped + at, &image.bytes[image.levels[i].offset], image.levels[i].size);
		at += image.levels[i].size;
	}
	SDL_UnmapGPUTransferBuffer(device, transfer);

	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
	SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
	at = 0;
	for (size_t i = 0; i < image.levels.size(); ++i) {
		SDL_GPUTextureTransferInfo source;
		SDL_zero(source);
		source.transfer_buffer = transfer;
		source.offset = (Uint32)at;
		SDL_GPUTextureRegion destination;
		SDL_zero(destination);
		destination.texture = texture;
		destination.mip_level = (Uint32)i;
		destination.w = image.levels[i].width;
		destination.h = image.levels[i].height;
		destination.d = 1;
		SDL_UploadToGPUTexture(copy, &source, &destination, false);
		at += image.levels[i].size;
	}
	SDL_EndGPUCopyPass(copy);
	SDL_SubmitGPUCommandBuffer(commands);
	SDL_ReleaseGPUTransferBuffer(device, transfer);
	return texture;
}

SDL_GPUTexture *whiteTexture(SDL_GPUDevice *device)
{
	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = info.height = 1;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);
	SDL_GPUTransferBufferCreateInfo transferInfo;
	SDL_zero(transferInfo);
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size = 4;
	SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
	Uint8 *mapped = (Uint8 *)SDL_MapGPUTransferBuffer(device, transfer, false);
	memset(mapped, 0xff, 4);
	SDL_UnmapGPUTransferBuffer(device, transfer);
	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
	SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureTransferInfo source;
	SDL_zero(source);
	source.transfer_buffer = transfer;
	SDL_GPUTextureRegion destination;
	SDL_zero(destination);
	destination.texture = texture;
	destination.w = destination.h = destination.d = 1;
	SDL_UploadToGPUTexture(copy, &source, &destination, false);
	SDL_EndGPUCopyPass(copy);
	SDL_SubmitGPUCommandBuffer(commands);
	SDL_ReleaseGPUTransferBuffer(device, transfer);
	return texture;
}

SDL_GPUBuffer *uploadBuffer(SDL_GPUDevice *device, SDL_GPUBufferUsageFlags usage, const void *data, Uint32 size)
{
	SDL_GPUBufferCreateInfo info;
	SDL_zero(info);
	info.usage = usage;
	info.size = size;
	SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(device, &info);
	SDL_GPUTransferBufferCreateInfo transferInfo;
	SDL_zero(transferInfo);
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size = size;
	SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
	void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
	memcpy(mapped, data, size);
	SDL_UnmapGPUTransferBuffer(device, transfer);
	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
	SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTransferBufferLocation source;
	SDL_zero(source);
	source.transfer_buffer = transfer;
	SDL_GPUBufferRegion destination;
	SDL_zero(destination);
	destination.buffer = buffer;
	destination.size = size;
	SDL_UploadToGPUBuffer(copy, &source, &destination, false);
	SDL_EndGPUCopyPass(copy);
	SDL_SubmitGPUCommandBuffer(commands);
	SDL_ReleaseGPUTransferBuffer(device, transfer);
	return buffer;
}

SDL_GPUGraphicsPipeline *createPipeline(SDL_GPUDevice *device, SDL_GPUShader *vertexShader, SDL_GPUShader *fragmentShader,
	SDL_GPUTextureFormat colorFormat, SDL_GPUTextureFormat depthFormat, const DrawState &state, const Uint32 locations[3])
{
	SDL_GPUVertexBufferDescription buffer;
	SDL_zero(buffer);
	buffer.slot = 0;
	buffer.pitch = sizeof(ModelVertex);
	buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

	SDL_GPUVertexAttribute attributes[3];
	SDL_zeroa(attributes);
	attributes[0].location = locations[0];
	attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
	attributes[0].offset = offsetof(ModelVertex, position);
	attributes[1].location = locations[1];
	attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
	attributes[1].offset = offsetof(ModelVertex, normal);
	attributes[2].location = locations[2];
	attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	attributes[2].offset = offsetof(ModelVertex, texCoord);

	SDL_GPUColorTargetDescription color;
	SDL_zero(color);
	color.format = colorFormat;
	if (state.blend || state.additive) {
		color.blend_state.enable_blend = true;
		color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
		color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
		color.blend_state.src_color_blendfactor = state.additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		color.blend_state.dst_color_blendfactor = state.additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
		color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	}

	SDL_GPUGraphicsPipelineCreateInfo info;
	SDL_zero(info);
	info.vertex_shader = vertexShader;
	info.fragment_shader = fragmentShader;
	info.vertex_input_state.vertex_buffer_descriptions = &buffer;
	info.vertex_input_state.num_vertex_buffers = 1;
	info.vertex_input_state.vertex_attributes = attributes;
	info.vertex_input_state.num_vertex_attributes = 3;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	// shader.cpp culls D3DCULL_CW, so the game's front faces wind counter-clockwise on screen.
	info.rasterizer_state.cull_mode = state.twoSided ? SDL_GPU_CULLMODE_NONE : SDL_GPU_CULLMODE_BACK;
	info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	info.depth_stencil_state.enable_depth_test = true;
	info.depth_stencil_state.enable_depth_write = state.depthWrite;
	info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	info.target_info.color_target_descriptions = &color;
	info.target_info.num_color_targets = 1;
	info.target_info.depth_stencil_format = depthFormat;
	info.target_info.has_depth_stencil_target = true;
	SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
	if (pipeline == NULL) {
		fprintf(stderr, "w3d_view: pipeline: %s\n", SDL_GetError());
	}
	return pipeline;
}

SDL_GPUTexture *createTarget(SDL_GPUDevice *device, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int width, int height)
{
	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = format;
	info.usage = usage;
	info.width = (Uint32)width;
	info.height = (Uint32)height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	return SDL_CreateGPUTexture(device, &info);
}

// Reads the colour target back and writes it as PNG: what was drawn, not a screen grab.
bool saveTarget(SDL_GPUDevice *device, SDL_GPUTexture *target, SDL_GPUTextureFormat format, int width, int height, const char *path)
{
	const Uint32 bytes = (Uint32)width * (Uint32)height * 4;
	SDL_GPUTransferBufferCreateInfo transferInfo;
	SDL_zero(transferInfo);
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	transferInfo.size = bytes;
	SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
	SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureRegion source;
	SDL_zero(source);
	source.texture = target;
	source.w = (Uint32)width;
	source.h = (Uint32)height;
	source.d = 1;
	SDL_GPUTextureTransferInfo destination;
	SDL_zero(destination);
	destination.transfer_buffer = transfer;
	SDL_DownloadFromGPUTexture(copy, &source, &destination);
	SDL_EndGPUCopyPass(copy);
	SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	SDL_WaitForGPUFences(device, true, &fence, 1);
	SDL_ReleaseGPUFence(device, fence);

	void *pixels = SDL_MapGPUTransferBuffer(device, transfer, false);
	SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height,
		format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ? SDL_PIXELFORMAT_BGRA32 : SDL_PIXELFORMAT_RGBA32, pixels, width * 4);
	const bool saved = surface != NULL && SDL_SavePNG(surface, path);
	if (!saved) fprintf(stderr, "w3d_view: could not save %s: %s\n", path, SDL_GetError());
	SDL_DestroySurface(surface);
	SDL_UnmapGPUTransferBuffer(device, transfer);
	SDL_ReleaseGPUTransferBuffer(device, transfer);
	return saved;
}

} // namespace

int main(int argc, char **argv)
{
	Options options;
	if (!parseOptions(argc, argv, &options)) {
		fprintf(stderr, "usage: w3d_view [--data DIR] [--model NAME] [--shaders glsl|hlsl|hand|generated] [--depth auto|d24s8|d32s8]\n"
			"                [--size WxH] [--yaw DEG] [--pitch DEG] [--zoom F]\n"
			"                [--screenshot FILE.png] [--offscreen] [--frames N]\n"
			"  --offscreen needs --screenshot\n");
		return 2;
	}
	if (options.dataDir.empty()) {
		fprintf(stderr, "w3d_view: no game data: pass --data or set ZH_DATA_DIR to a folder holding zerohour/ and generals/\n");
		return 2;
	}

	// ---- the model and its textures, from the archives ----
	std::vector<std::string> modelArchives, textureArchives;
	modelArchives.push_back(options.dataDir + "/zerohour/W3DZH.big");
	modelArchives.push_back(options.dataDir + "/generals/W3D.big");
	textureArchives.push_back(options.dataDir + "/zerohour/TexturesZH.big");
	textureArchives.push_back(options.dataDir + "/generals/Textures.big");
	ArchiveSet models, textures;
	if (models.open(modelArchives) == 0 || textures.open(textureArchives) == 0) {
		fprintf(stderr, "w3d_view: no W3D or texture archives under %s\n", options.dataDir.c_str());
		return 1;
	}
	std::vector<unsigned char> w3d;
	std::string from;
	const std::string w3dName = "Art\\W3D\\" + options.model + ".w3d";
	if (!models.read(w3dName, &w3d, &from)) {
		fprintf(stderr, "w3d_view: %s is in neither W3D archive\n", w3dName.c_str());
		return 1;
	}
	Model model;
	std::string why;
	if (!loadW3dModel(w3d, &model, &why)) {
		fprintf(stderr, "w3d_view: %s: %s\n", w3dName.c_str(), why.c_str());
		return 1;
	}
	printf("model     %s from %s: %zu vertices, %zu triangles, %zu draws\n", model.name.c_str(), from.c_str(),
		model.vertices.size(), model.indices.size() / 3, model.draws.size());
	for (size_t i = 0; i < model.notes.size(); ++i) printf("          %s\n", model.notes[i].c_str());

	// ---- the device ----
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "w3d_view: SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, true, NULL);
	if (device == NULL) {
		fprintf(stderr, "w3d_view: SDL_CreateGPUDevice: %s\n", SDL_GetError());
		return 1;
	}
	const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
	const bool useMsl = (formats & SDL_GPU_SHADERFORMAT_MSL) != 0;
	const bool generated = options.shaders == "generated";
	const char *route = generated ? (useMsl ? "the game's, generated: ffvertex/ffshader -> HLSL -> SPIR-V -> MSL" : "the game's, generated: ffvertex/ffshader -> HLSL -> SPIR-V")
		: options.shaders == "hand" ? "MSL written by hand"
		: (options.shaders == "hlsl" ? (useMsl ? "HLSL -> SPIR-V -> MSL" : "HLSL -> SPIR-V") : (useMsl ? "GLSL -> SPIR-V -> MSL" : "GLSL -> SPIR-V"));
	printf("device    %s, shaders %s\n", SDL_GetGPUDeviceDriver(device), route);
	if (!useMsl && options.shaders == "hand") {
		fprintf(stderr, "w3d_view: --shaders hand is MSL, and this device takes SPIR-V\n");
		return 2;
	}

	SDL_Window *window = NULL;
	if (!options.offscreen) {
		window = SDL_CreateWindow("w3d_view", options.width, options.height, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
		if (window == NULL || !SDL_ClaimWindowForGPUDevice(device, window)) {
			fprintf(stderr, "w3d_view: window: %s\n", SDL_GetError());
			return 1;
		}
	}

	const SDL_GPUTextureFormat depthFormat = mapDepthStencilFormat(device, options.depth.c_str());
	printf("depth     asked D24_UNORM_S8_UINT%s, got %s\n", options.depth == "auto" ? "" : (" (forced " + options.depth + ")").c_str(), depthName(depthFormat));
	if (depthFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
		fprintf(stderr, "w3d_view: no depth-stencil format this device supports\n");
		return 1;
	}
	const SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

	// ---- shaders ----
	SDL_GPUShader *vertexShader = NULL, *fragmentShader = NULL, *alphaTestShader = NULL;
	// How many texture-sampler pairs a draw binds.  The generated pixel programs declare four stages
	// whatever they read, and SDL binds slots 0 to n - 1, so every slot is bound: the draw's texture
	// in 0, white in the rest (sdl3shadercompile.h, SDL3_Shader_Slots).
	unsigned samplerSlots = 1;
	if (generated) {
		std::string hlsl;
		const VertexPipelineDescription vertexState = meshVertexState();
		if (VertexShader_Generate(vertexState, VERTEX_SHADER_TARGET_SDL3_GPU, hlsl)) {
			vertexShader = generatedShader(device, hlsl, true, ("vertex " + VertexShader_Key(vertexState)).c_str(), NULL);
		}
		for (int alphaTest = 0; alphaTest < 2; ++alphaTest) {
			const CombinerDescription pixelState = meshPixelState(alphaTest != 0);
			if (CombinerShader_Generate(pixelState, COMBINER_SHADER_TARGET_SDL3_GPU, hlsl)) {
				(alphaTest ? alphaTestShader : fragmentShader) =
					generatedShader(device, hlsl, false, ("pixel " + CombinerShader_Key(pixelState)).c_str(), &samplerSlots);
			}
		}
		if (alphaTestShader == NULL) return 1;
	} else if (!useMsl && options.shaders == "hlsl") {
		vertexShader = createShader(device, SDL_GPU_SHADERFORMAT_SPIRV, SDL_GPU_SHADERSTAGE_VERTEX, MODEL_HLSL_VS_SPIRV, sizeof(MODEL_HLSL_VS_SPIRV), "vsMain", 0, 1);
		fragmentShader = createShader(device, SDL_GPU_SHADERFORMAT_SPIRV, SDL_GPU_SHADERSTAGE_FRAGMENT, MODEL_HLSL_PS_SPIRV, sizeof(MODEL_HLSL_PS_SPIRV), "psMain", 1, 1);
	} else if (!useMsl) {
		vertexShader = createShader(device, SDL_GPU_SHADERFORMAT_SPIRV, SDL_GPU_SHADERSTAGE_VERTEX, MODEL_VERT_SPIRV, sizeof(MODEL_VERT_SPIRV), "main", 0, 1);
		fragmentShader = createShader(device, SDL_GPU_SHADERFORMAT_SPIRV, SDL_GPU_SHADERSTAGE_FRAGMENT, MODEL_FRAG_SPIRV, sizeof(MODEL_FRAG_SPIRV), "main", 1, 1);
	} else if (options.shaders == "hlsl") {
		vertexShader = createShader(device, SDL_GPU_SHADERFORMAT_MSL, SDL_GPU_SHADERSTAGE_VERTEX, MODEL_HLSL_VS_MSL, strlen(MODEL_HLSL_VS_MSL), "vsMain", 0, 1);
		fragmentShader = createShader(device, SDL_GPU_SHADERFORMAT_MSL, SDL_GPU_SHADERSTAGE_FRAGMENT, MODEL_HLSL_PS_MSL, strlen(MODEL_HLSL_PS_MSL), "psMain", 1, 1);
	} else if (options.shaders == "hand") {
		vertexShader = createShader(device, SDL_GPU_SHADERFORMAT_MSL, SDL_GPU_SHADERSTAGE_VERTEX, MODEL_MSL_HAND, strlen(MODEL_MSL_HAND), "vertexMain", 0, 1);
		fragmentShader = createShader(device, SDL_GPU_SHADERFORMAT_MSL, SDL_GPU_SHADERSTAGE_FRAGMENT, MODEL_MSL_HAND, strlen(MODEL_MSL_HAND), "fragmentMain", 1, 1);
	} else {
		vertexShader = createShader(device, SDL_GPU_SHADERFORMAT_MSL, SDL_GPU_SHADERSTAGE_VERTEX, MODEL_VERT_MSL, strlen(MODEL_VERT_MSL), MODEL_MSL_ENTRY, 0, 1);
		fragmentShader = createShader(device, SDL_GPU_SHADERFORMAT_MSL, SDL_GPU_SHADERSTAGE_FRAGMENT, MODEL_FRAG_MSL, strlen(MODEL_FRAG_MSL), MODEL_MSL_ENTRY, 1, 1);
	}
	if (vertexShader == NULL || fragmentShader == NULL) return 1;

	// ---- geometry and textures ----
	SDL_GPUBuffer *vertexBuffer = uploadBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX, &model.vertices[0], (Uint32)(model.vertices.size() * sizeof(ModelVertex)));
	SDL_GPUBuffer *indexBuffer = uploadBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX, &model.indices[0], (Uint32)(model.indices.size() * sizeof(unsigned)));
	SDL_GPUTexture *white = whiteTexture(device);

	std::map<std::string, SDL_GPUTexture *> loaded;
	for (size_t i = 0; i < model.draws.size(); ++i) {
		const std::string &name = model.draws[i].textureName;
		if (name.empty() || loaded.count(name) != 0) continue;
		// DDSFileClass (WW3D2/ddsfile.cpp) swaps the extension for .dds; the file factory finds it
		// under Art\Textures.
		std::string dds = "Art\\Textures\\" + name;
		const size_t dot = dds.rfind('.');
		dds = (dot == std::string::npos ? dds : dds.substr(0, dot)) + ".dds";
		std::vector<unsigned char> bytes;
		DdsImage image;
		std::string reason;
		if (!textures.read(dds, &bytes, &from)) {
			printf("texture   %s: not in the texture archives, drawn white\n", dds.c_str());
			loaded[name] = NULL;
		} else if (!parseDds(bytes, &image, &reason)) {
			printf("texture   %s: %s, drawn white\n", dds.c_str(), reason.c_str());
			loaded[name] = NULL;
		} else {
			loaded[name] = uploadDds(device, image);
			printf("texture   %s from %s: BC%d %ux%u, %zu mip levels\n", dds.c_str(), from.c_str(),
				image.format == DDS_BC1 ? 1 : (image.format == DDS_BC2 ? 2 : 3), image.levels[0].width, image.levels[0].height, image.levels.size());
		}
	}

	SDL_GPUSamplerCreateInfo samplerInfo;
	SDL_zero(samplerInfo);
	samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
	samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
	samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
	samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	samplerInfo.max_lod = 1000.0f;
	SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &samplerInfo);

	// One pipeline per distinct draw state; opaque draws first, then blended ones.
	std::map<DrawState, SDL_GPUGraphicsPipeline *> pipelines;
	for (size_t i = 0; i < model.draws.size(); ++i) {
		const DrawState &state = model.draws[i].state;
		if (pipelines.count(state) == 0) {
			pipelines[state] = createPipeline(device, vertexShader, generated && state.alphaTest ? alphaTestShader : fragmentShader,
				colorFormat, depthFormat, state, generated ? GENERATED_LOCATIONS : SPIKE_LOCATIONS);
			if (pipelines[state] == NULL) return 1;
		}
	}
	printf("pipelines %zu\n", pipelines.size());
	std::vector<size_t> order;
	for (int pass = 0; pass < 2; ++pass) {
		for (size_t i = 0; i < model.draws.size(); ++i) {
			const bool translucent = model.draws[i].state.blend || model.draws[i].state.additive;
			if (translucent == (pass == 1)) order.push_back(i);
		}
	}

	// ---- camera ----
	// Framed on the opaque geometry: a translucent effect (the Crusader's muzzle flash) reaches well
	// past the hull and would pull the model off-centre.
	float lowest[3] = { 1e30f, 1e30f, 1e30f }, highest[3] = { -1e30f, -1e30f, -1e30f };
	for (size_t i = 0; i < model.draws.size(); ++i) {
		const ModelDraw &draw = model.draws[i];
		if (draw.state.blend || draw.state.additive) continue;
		for (unsigned k = 0; k < draw.indexCount; ++k) {
			const float *p = model.vertices[model.indices[draw.firstIndex + k]].position;
			for (int axis = 0; axis < 3; ++axis) {
				if (p[axis] < lowest[axis]) lowest[axis] = p[axis];
				if (p[axis] > highest[axis]) highest[axis] = p[axis];
			}
		}
	}
	float centre[3];
	float radius = 0.0f;
	for (int axis = 0; axis < 3; ++axis) {
		centre[axis] = (lowest[axis] + highest[axis]) * 0.5f;
		const float half = (highest[axis] - lowest[axis]) * 0.5f;
		radius += half * half;
	}
	radius = sqrtf(radius);
	float yaw = options.yaw, pitch = options.pitch, zoom = options.zoom;

	int width = options.width, height = options.height;
	if (window != NULL) SDL_GetWindowSizeInPixels(window, &width, &height);
	SDL_GPUTexture *colorTarget = createTarget(device, colorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height);
	SDL_GPUTexture *depthTarget = createTarget(device, depthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height);

	int exitCode = 0;
	bool running = true, dragging = false;
	for (int frame = 0; running; ++frame) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) running = false;
			else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
			else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) dragging = true;
			else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
			else if (event.type == SDL_EVENT_MOUSE_MOTION && dragging) {
				yaw -= event.motion.xrel * 0.4f;
				pitch += event.motion.yrel * 0.4f;
				pitch = pitch < -85.0f ? -85.0f : (pitch > 85.0f ? 85.0f : pitch);
			} else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				zoom *= event.wheel.y > 0 ? 0.9f : 1.1f;
			}
		}
		if (!running) break;

		if (window != NULL) {
			int w, h;
			SDL_GetWindowSizeInPixels(window, &w, &h);
			if ((w != width || h != height) && w > 0 && h > 0) {
				width = w;
				height = h;
				SDL_ReleaseGPUTexture(device, colorTarget);
				SDL_ReleaseGPUTexture(device, depthTarget);
				colorTarget = createTarget(device, colorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height);
				depthTarget = createTarget(device, depthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height);
			}
		}

		const float yawRadians = yaw * 3.14159265f / 180.0f, pitchRadians = pitch * 3.14159265f / 180.0f;
		const float distance = radius * 2.6f * zoom;
		const float eye[3] = {
			centre[0] + distance * cosf(pitchRadians) * cosf(yawRadians),
			centre[1] + distance * cosf(pitchRadians) * sinf(yawRadians),
			centre[2] + distance * sinf(pitchRadians),
		};
		const float up[3] = { 0.0f, 0.0f, 1.0f };
		VertexUniforms vertexUniforms;
		const Mat4 view = lookAt(eye, centre, up);
		const Mat4 viewProjection = multiply(perspective(0.8f, (float)width / (float)height, distance * 0.05f, distance * 4.0f), view);
		memcpy(vertexUniforms.viewProjection, viewProjection.m, sizeof(viewProjection.m));

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
		SDL_GPUColorTargetInfo colorInfo;
		SDL_zero(colorInfo);
		colorInfo.texture = colorTarget;
		colorInfo.clear_color.r = 0.22f;
		colorInfo.clear_color.g = 0.24f;
		colorInfo.clear_color.b = 0.27f;
		colorInfo.clear_color.a = 1.0f;
		colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
		colorInfo.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPUDepthStencilTargetInfo depthInfo;
		SDL_zero(depthInfo);
		depthInfo.texture = depthTarget;
		depthInfo.clear_depth = 1.0f;
		depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
		depthInfo.store_op = SDL_GPU_STOREOP_DONT_CARE;
		depthInfo.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
		depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

		SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colorInfo, 1, &depthInfo);
		SDL_GPUBufferBinding vertexBinding = { vertexBuffer, 0 };
		SDL_GPUBufferBinding indexBinding = { indexBuffer, 0 };
		SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
		SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
		if (!generated) SDL_PushGPUVertexUniformData(commands, 0, &vertexUniforms, sizeof(vertexUniforms));
		for (size_t k = 0; k < order.size(); ++k) {
			const ModelDraw &draw = model.draws[order[k]];
			SDL_BindGPUGraphicsPipeline(pass, pipelines[draw.state]);
			SDL_GPUTexture *texture = draw.textureName.empty() ? NULL : loaded[draw.textureName];
			SDL_GPUTextureSamplerBinding bindings[16];
			for (unsigned slot = 0; slot < samplerSlots && slot < 16; ++slot) {
				bindings[slot].texture = slot == 0 && texture != NULL ? texture : white;
				bindings[slot].sampler = sampler;
			}
			SDL_BindGPUFragmentSamplers(pass, 0, bindings, samplerSlots);
			FragmentUniforms fragmentUniforms;
			memcpy(fragmentUniforms.materialDiffuse, draw.diffuse, sizeof(draw.diffuse));
			for (int c = 0; c < 4; ++c) fragmentUniforms.tint[c] = draw.houseColor ? HOUSE_TINT[c] : 1.0f;
			fragmentUniforms.ambient[0] = fragmentUniforms.ambient[1] = fragmentUniforms.ambient[2] = 0.35f;
			fragmentUniforms.ambient[3] = 0.0f;
			float light[3] = { -0.4f, 0.3f, -0.85f };
			normalise3(light);
			fragmentUniforms.lightDirection[0] = light[0];
			fragmentUniforms.lightDirection[1] = light[1];
			fragmentUniforms.lightDirection[2] = light[2];
			fragmentUniforms.lightDirection[3] = 0.0f;
			fragmentUniforms.lightColor[0] = fragmentUniforms.lightColor[1] = fragmentUniforms.lightColor[2] = 0.85f;
			fragmentUniforms.lightColor[3] = draw.state.alphaTest ? ALPHA_TEST_REFERENCE : 0.0f;
			if (generated) {
				// The same scene through the generators' own constants.  A matrix they declare
				// row_major and multiply as mul(v, M) is this column-major matrix's floats unchanged.
				GeneratedVertexConstants vertexConstants;
				memset(&vertexConstants, 0, sizeof(vertexConstants));
				memcpy(vertexConstants.WorldViewProjection, viewProjection.m, sizeof(viewProjection.m));
				memcpy(vertexConstants.WorldView, view.m, sizeof(view.m));
				memcpy(vertexConstants.NormalTransform, view.m, sizeof(view.m));
				for (int c = 0; c < 4; ++c) {
					const float tint = draw.houseColor ? HOUSE_TINT[c] : 1.0f;
					vertexConstants.MaterialDiffuse[c] = draw.diffuse[c] * (c < 3 ? tint : 1.0f);
					vertexConstants.MaterialAmbient[c] = vertexConstants.MaterialDiffuse[c];
					vertexConstants.GlobalAmbient[c] = c < 3 ? 0.35f : 1.0f;
					vertexConstants.Light0Diffuse[c] = c < 3 ? 0.85f : 1.0f;
				}
				vertexConstants.MaterialPower[0] = 1.0f;
				// The light's direction in camera space, where the program lights.
				for (int r = 0; r < 3; ++r) {
					vertexConstants.Light0Direction[r] = view.at(r, 0) * light[0] + view.at(r, 1) * light[1] + view.at(r, 2) * light[2];
				}
				GeneratedPixelConstants pixelConstants;
				memset(&pixelConstants, 0, sizeof(pixelConstants));
				pixelConstants.AlphaReference[0] = (float)0x60;
				SDL_PushGPUVertexUniformData(commands, 0, &vertexConstants, sizeof(vertexConstants));
				SDL_PushGPUFragmentUniformData(commands, 0, &pixelConstants, sizeof(pixelConstants));
			}
			else {
				SDL_PushGPUFragmentUniformData(commands, 0, &fragmentUniforms, sizeof(fragmentUniforms));
			}
			SDL_DrawGPUIndexedPrimitives(pass, draw.indexCount, 1, draw.firstIndex, 0, 0);
		}
		SDL_EndGPURenderPass(pass);

		if (window != NULL) {
			SDL_GPUTexture *swapchain = NULL;
			Uint32 swapWidth = 0, swapHeight = 0;
			if (SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, &swapWidth, &swapHeight) && swapchain != NULL) {
				SDL_GPUBlitInfo blit;
				SDL_zero(blit);
				blit.source.texture = colorTarget;
				blit.source.w = (Uint32)width;
				blit.source.h = (Uint32)height;
				blit.destination.texture = swapchain;
				blit.destination.w = swapWidth;
				blit.destination.h = swapHeight;
				blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
				blit.filter = SDL_GPU_FILTER_LINEAR;
				SDL_BlitGPUTexture(commands, &blit);
			}
		}
		SDL_SubmitGPUCommandBuffer(commands);

		if (!options.screenshot.empty() && frame + 1 >= options.frames) {
			exitCode = saveTarget(device, colorTarget, colorFormat, width, height, options.screenshot.c_str()) ? 0 : 1;
			if (exitCode == 0) printf("saved     %s (%dx%d)\n", options.screenshot.c_str(), width, height);
			running = false;
		}
	}

	SDL_WaitForGPUIdle(device);
	for (std::map<DrawState, SDL_GPUGraphicsPipeline *>::iterator p = pipelines.begin(); p != pipelines.end(); ++p) {
		SDL_ReleaseGPUGraphicsPipeline(device, p->second);
	}
	for (std::map<std::string, SDL_GPUTexture *>::iterator t = loaded.begin(); t != loaded.end(); ++t) {
		if (t->second != NULL) SDL_ReleaseGPUTexture(device, t->second);
	}
	SDL_ReleaseGPUTexture(device, white);
	SDL_ReleaseGPUTexture(device, colorTarget);
	SDL_ReleaseGPUTexture(device, depthTarget);
	SDL_ReleaseGPUSampler(device, sampler);
	SDL_ReleaseGPUBuffer(device, vertexBuffer);
	SDL_ReleaseGPUBuffer(device, indexBuffer);
	SDL_ReleaseGPUShader(device, vertexShader);
	SDL_ReleaseGPUShader(device, fragmentShader);
	if (alphaTestShader != NULL) SDL_ReleaseGPUShader(device, alphaTestShader);
	if (window != NULL) {
		SDL_ReleaseWindowFromGPUDevice(device, window);
		SDL_DestroyWindow(window);
	}
	SDL_DestroyGPUDevice(device);
	SDL_Quit();
	return exitCode;
}
