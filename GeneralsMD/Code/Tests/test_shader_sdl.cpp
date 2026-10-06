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
 * test_shader_sdl - the POSIX twin of test_ffshadercompile and test_ffvertexcompile: every program
 * the generators write, under the SDL3 GPU target, taken the whole way to what SDL's GPU backends
 * accept (decision 4).
 *
 * For each program in shader_cases.h - the 49 decision 4 was measured on less the three bumped
 * terrain programs that went with the normal maps, and the extras - it:
 *   1. generates the SDL3_GPU text,
 *   2. compiles it to SPIR-V through SDL3_Compile_HLSL_To_SPIRV (the one place the HLSL front end is
 *      named, so this test follows the compiler if decision 4's exit is ever taken),
 *   3. validates the SPIR-V with spirv-val for Vulkan 1.0, when the build found one,
 *   4. translates it to MSL through SDL_shadercross, and checks the slot contract in that MSL: every
 *      [[texture(n)]] has its [[sampler(n)]], and n is below the slot count SDL3_Shader_Slots gives
 *      the device - SDL binds slots 0 to n - 1 as texture-sampler pairs, and a texture outside that
 *      range is one Metal accepts and nothing ever binds,
 *   5. on macOS, has Metal compile it: SDL_CreateGPUShader on a real device, which hands the MSL to
 *      the driver (newLibraryWithSource).  Not the offline `xcrun metal`, which is not installed.
 *
 * A second test, on macOS, links every generated vertex program with every pixel program it meets
 * in the game into a real SDL pipeline: the stages agree by the locations sdl3target.h pins, or Metal
 * says which input is not written.  Which programs meet is the game's pairing, not every pair:
 *   - the engine's Trees vertex program meets ffshader's combiner programs only.  W3DTreeBuffer
 *     binds Trees.vso and no pixel shader (it loads Trees.pso and never binds it), and engineshader
 *     transcribes no Trees pixel program;
 *   - the engine's pixel programs (water, terrain, road, monochrome) meet ffvertex's programs only:
 *     their draws bind a .pso over the fixed-function vertex pipeline.
 * Trees against a water or terrain pixel program does not link - they read coordinate sets 2 and 3
 * and Trees writes two - and the game never draws it.
 *
 * Steps 3 and 5 can be unavailable - no spirv-val on the machine, no GPU in the session - and then
 * they say SKIPPED on a line of their own, with the reason and the count they did not check.  A
 * skipped step is never folded into a pass.
 */
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "sdl3shadercompile.h"
#include "shader_cases.h"

namespace {

struct Tally
{
	unsigned programs;
	unsigned generated;
	unsigned compiled;
	unsigned validated;
	unsigned translated;
	unsigned slots_kept;
	unsigned accepted;
};

// The [[texture(n)]] or [[sampler(n)]] indices in an MSL entry point's signature.
std::vector<int> msl_indices(const std::string & msl, const char * kind)
{
	std::vector<int> indices;
	const std::string opening = std::string("[[") + kind + "(";
	for (size_t at = msl.find(opening); at != std::string::npos; at = msl.find(opening, at + 1)) {
		indices.push_back(atoi(msl.c_str() + at + opening.size()));
	}
	return indices;
}

// Every texture has the sampler of its own index, and every index is one SDL binds.
bool slots_kept(const std::string & msl, unsigned samplers, std::string & why)
{
	const std::vector<int> textures = msl_indices(msl, "texture");
	const std::vector<int> sampler_indices = msl_indices(msl, "sampler");
	for (size_t i = 0; i < textures.size(); ++i) {
		if (textures[i] < 0 || (unsigned)textures[i] >= samplers) {
			why = "texture " + std::to_string(textures[i]) + " is outside the " + std::to_string(samplers) + " slots SDL binds";
			return false;
		}
		bool paired = false;
		for (size_t k = 0; k < sampler_indices.size(); ++k) paired = paired || sampler_indices[k] == textures[i];
		if (!paired) {
			why = "texture " + std::to_string(textures[i]) + " has no sampler of its own index";
			return false;
		}
		for (size_t j = i + 1; j < textures.size(); ++j) {
			if (textures[j] == textures[i]) {
				why = "two textures share slot " + std::to_string(textures[i]);
				return false;
			}
		}
	}
	for (size_t k = 0; k < sampler_indices.size(); ++k) {
		if (sampler_indices[k] < 0 || (unsigned)sampler_indices[k] >= samplers) {
			why = "sampler " + std::to_string(sampler_indices[k]) + " is outside the slots SDL binds";
			return false;
		}
	}
	return true;
}

// spirv-val on one module, through a file, because it reads files.
bool validate(const char * spirv_val, const std::vector<unsigned char> & spirv, std::string & output)
{
	const char * temp = getenv("TMPDIR");
	char path[1024];
	snprintf(path, sizeof(path), "%s/test_shader_sdl_%d.spv", (temp != NULL && *temp) ? temp : "/tmp", (int)getpid());
	FILE * file = fopen(path, "wb");
	if (file == NULL) return false;
	fwrite(&spirv[0], 1, spirv.size(), file);
	fclose(file);

	std::string command = std::string("\"") + spirv_val + "\" --target-env vulkan1.0 \"" + path + "\" 2>&1";
	FILE * pipe = popen(command.c_str(), "r");
	output.clear();
	if (pipe == NULL) return false;
	char line[512];
	while (fgets(line, sizeof(line), pipe) != NULL) output += line;
	const int status = pclose(pipe);
	remove(path);
	return status == 0;
}

void print_tally(const char * what, const Tally & t, bool validating, bool metal)
{
	printf("  %-15s %3u programs: generated %u, compiled %u, spirv-val %s, MSL %u (slots kept %u), Metal %s\n", what,
		t.programs, t.generated, t.compiled,
		validating ? (std::to_string(t.validated) + "/" + std::to_string(t.compiled)).c_str() : "SKIPPED",
		t.translated, t.slots_kept,
		metal ? (std::to_string(t.accepted) + "/" + std::to_string(t.translated)).c_str() : "SKIPPED");
}

} // namespace

// "[[vk::location(4)]] float2 TexCoord0 : TEXCOORD0;" lines of a vertex program's Input structure,
// as SDL vertex attributes over one interleaved buffer.  D3DCOLOR is four bytes read as UBYTE4_NORM,
// which is why sdl3target.h swaps it back to RGBA in the program.
bool vertex_layout(const std::string & hlsl, std::vector<SDL_GPUVertexAttribute> & attributes, Uint32 & pitch)
{
	attributes.clear();
	pitch = 0;
	const size_t start = hlsl.find("struct Input\n{\n");
	if (start == std::string::npos) return false;
	const size_t end = hlsl.find("};", start);
	size_t line = hlsl.find('\n', start + 14) + 1;
	while (line < end) {
		const size_t stop = hlsl.find('\n', line);
		const std::string text = hlsl.substr(line, stop - line);
		line = stop + 1;
		const size_t open = text.find("[[vk::location(");
		if (open == std::string::npos) continue;
		SDL_GPUVertexAttribute attribute;
		SDL_zero(attribute);
		attribute.location = (Uint32)atoi(text.c_str() + open + 15);
		attribute.offset = pitch;
		if (text.find(": COLOR") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
			pitch += 4;
		}
		else if (text.find(" float4 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
			pitch += 16;
		}
		else if (text.find(" float3 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
			pitch += 12;
		}
		else if (text.find(" float2 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
			pitch += 8;
		}
		else return false;
		attributes.push_back(attribute);
	}
	return !attributes.empty();
}

TEST(every_generated_program_reaches_sdl3_gpu)
{
	const char * spirv_val = getenv("ZH_SPIRV_VAL");
	const bool validating = spirv_val != NULL && *spirv_val != '\0';
	if (!validating) {
		printf("  SKIPPED spirv-val: ZH_SPIRV_VAL is not set (CMake found no spirv-val; install spirv-tools)\n");
	}

	// Metal only.  On Linux the SPIR-V is the program, and spirv-val is the check there is.
	SDL_GPUDevice * device = NULL;
#if defined(__APPLE__)
	if (SDL_Init(SDL_INIT_VIDEO)) {
		device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, NULL);
	}
	if (device == NULL) {
		printf("  SKIPPED Metal: no GPU device in this session (%s)\n", SDL_GetError());
	}
#endif
	const bool metal = device != NULL;

	Tally reference, extra;
	memset(&reference, 0, sizeof(reference));
	memset(&extra, 0, sizeof(extra));

	const std::vector<ShaderCase> cases = Shader_Cases();
	for (size_t i = 0; i < cases.size(); ++i) {
		const ShaderCase & c = cases[i];
		Tally & t = c.Reference ? reference : extra;
		++t.programs;

		std::string hlsl;
		if (!Shader_Case_Generate(c, SHADER_CASE_SDL3_GPU, hlsl)) {
			printf("  %s: the generator refused the SDL3 target\n", c.Name.c_str());
			CHECK(false);
			continue;
		}
		++t.generated;

		std::vector<unsigned char> spirv;
		std::string log;
		if (!SDL3_Compile_HLSL_To_SPIRV(hlsl, c.VertexStage, spirv, log)) {
			printf("  %s: HLSL -> SPIR-V failed:\n%s\n", c.Name.c_str(), log.c_str());
			CHECK(false);
			continue;
		}
		++t.compiled;

		if (validating) {
			std::string output;
			if (validate(spirv_val, spirv, output)) {
				++t.validated;
			}
			else {
				printf("  %s: spirv-val refused it:\n%s\n", c.Name.c_str(), output.c_str());
				CHECK(false);
			}
		}

		std::string msl;
		if (!SDL3_Translate_SPIRV_To_MSL(spirv, c.VertexStage, msl, log)) {
			printf("  %s: SPIR-V -> MSL failed: %s\n", c.Name.c_str(), log.c_str());
			CHECK(false);
			continue;
		}
		++t.translated;

		unsigned samplers = 0, uniform_buffers = 0;
		SDL3_Shader_Slots(spirv, c.VertexStage, samplers, uniform_buffers);
		std::string why;
		if (slots_kept(msl, samplers, why)) {
			++t.slots_kept;
		}
		else {
			printf("  %s: slot contract broken in its MSL: %s\n", c.Name.c_str(), why.c_str());
			CHECK(false);
		}

		if (metal) {
			SDL_GPUShader * shader = SDL3_Create_Shader(device, spirv, c.VertexStage, log);
			if (shader != NULL) {
				++t.accepted;
				SDL_ReleaseGPUShader(device, shader);
			}
			else {
				printf("  %s: Metal refused it: %s\n", c.Name.c_str(), log.c_str());
				CHECK(false);
			}
		}
	}

	print_tally("reference set", reference, validating, metal);
	print_tally("beyond it", extra, validating, metal);
	CHECK_EQ(reference.programs, 46u);
	CHECK(extra.programs > 0);

	if (device != NULL) {
		SDL_DestroyGPUDevice(device);
	}
	SDL_Quit();
}

TEST(every_vertex_program_links_with_every_pixel_program_it_can_meet)
{
#if !defined(__APPLE__)
	printf("  SKIPPED pipeline linking: Metal only; on Linux the stages are checked by spirv-val alone\n");
#else
	SDL_GPUDevice * device = NULL;
	if (SDL_Init(SDL_INIT_VIDEO)) {
		device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, true, NULL);
	}
	if (device == NULL) {
		printf("  SKIPPED pipeline linking: no GPU device in this session (%s)\n", SDL_GetError());
		return;
	}

	struct Built
	{
		std::string name;
		std::string hlsl;
		SDL_GPUShader * shader;
		bool engine;
	};
	std::vector<Built> vertex, pixel;
	const std::vector<ShaderCase> cases = Shader_Cases();
	for (size_t i = 0; i < cases.size(); ++i) {
		Built built;
		built.name = cases[i].Name;
		std::vector<unsigned char> spirv;
		std::string log;
		if (!Shader_Case_Generate(cases[i], SHADER_CASE_SDL3_GPU, built.hlsl)
			|| !SDL3_Compile_HLSL_To_SPIRV(built.hlsl, cases[i].VertexStage, spirv, log)) {
			CHECK(false);
			continue;
		}
		built.shader = SDL3_Create_Shader(device, spirv, cases[i].VertexStage, log);
		built.engine = cases[i].Kind == SHADER_CASE_ENGINE;
		if (built.shader == NULL) {
			CHECK(false);
			continue;
		}
		(cases[i].VertexStage ? vertex : pixel).push_back(built);
	}

	unsigned pairs = 0, linked = 0;
	for (size_t v = 0; v < vertex.size(); ++v) {
		std::vector<SDL_GPUVertexAttribute> attributes;
		Uint32 pitch = 0;
		if (!vertex_layout(vertex[v].hlsl, attributes, pitch)) {
			printf("  %s: no vertex layout could be read from its Input structure\n", vertex[v].name.c_str());
			CHECK(false);
			continue;
		}
		SDL_GPUVertexBufferDescription buffer;
		SDL_zero(buffer);
		buffer.pitch = pitch;
		buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
		SDL_GPUColorTargetDescription target;
		SDL_zero(target);
		target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

		for (size_t p = 0; p < pixel.size(); ++p) {
			if (vertex[v].engine && pixel[p].engine) continue;	// the game never draws that pair
			++pairs;
			SDL_GPUGraphicsPipelineCreateInfo info;
			SDL_zero(info);
			info.vertex_shader = vertex[v].shader;
			info.fragment_shader = pixel[p].shader;
			info.vertex_input_state.vertex_buffer_descriptions = &buffer;
			info.vertex_input_state.num_vertex_buffers = 1;
			info.vertex_input_state.vertex_attributes = &attributes[0];
			info.vertex_input_state.num_vertex_attributes = (Uint32)attributes.size();
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.target_info.color_target_descriptions = &target;
			info.target_info.num_color_targets = 1;
			SDL_ClearError();
			SDL_GPUGraphicsPipeline * pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
			if (pipeline != NULL) {
				++linked;
				SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
			}
			else {
				printf("  %s + %s: Metal refused the pipeline: %s\n", vertex[v].name.c_str(), pixel[p].name.c_str(), SDL_GetError());
				CHECK(false);
			}
		}
	}
	printf("  %zu vertex x %zu pixel programs: %u pairs the game can form, %u linked on Metal\n", vertex.size(), pixel.size(),
		pairs, linked);
	CHECK(pairs > 0);
	CHECK_EQ(linked, pairs);

	for (size_t i = 0; i < vertex.size(); ++i) SDL_ReleaseGPUShader(device, vertex[i].shader);
	for (size_t i = 0; i < pixel.size(); ++i) SDL_ReleaseGPUShader(device, pixel[i].shader);
	SDL_DestroyGPUDevice(device);
	SDL_Quit();
#endif
}
