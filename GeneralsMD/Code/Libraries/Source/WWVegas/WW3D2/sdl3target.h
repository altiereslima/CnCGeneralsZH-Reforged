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
** The SDL3 GPU target: a generated program's D3D11 text, made into what SDL's GPU API binds.
**
** Decision 4 (PORTING.md) keeps HLSL as the one shader language.  Off Windows the
** generated text goes through glslang's HLSL front end to SPIR-V for Vulkan, and SPIRV-Cross turns
** that SPIR-V into MSL for Metal.  The arithmetic is the D3D11 profile's exactly; what differs is
** where things are bound, so the generators write their D3D11 text and this rewrites the bindings.
** Doing it here rather than in every generator's string literals leaves the D3D11 text - which the
** Windows backend compiles, and which D3's golden dump proves unchanged - untouched by construction.
**
** Five changes, and nothing else:
**
**   1. Register spaces.  SDL_gpu.h (SDL_CreateGPUShader) binds a vertex program's textures and
**      samplers from space0 and its constant buffers from space1, and a pixel program's from space2
**      and space3.  As written, every constant buffer lands in set 0 and SDL refuses the program.
**
**   2. Varying locations, by semantic.  glslang numbers a stage's inputs and outputs in declaration
**      order, and Vulkan and Metal link the two stages by that number.  D3D11 does not link them that
**      way, and the programs rely on it: the engine's Trees vertex program writes two coordinate sets
**      and the fog, while every pixel program reads four and the fog, so by declaration order the
**      tree's fog would arrive as the pixel's third coordinate set.  Numbering each varying by its
**      semantic instead makes any two programs that name the same semantic agree.
**
**   3. Vertex attribute locations, by semantic, for the same reason from the other side: SDL has no
**      semantic names, only locations, so the backend builds a vertex layout by the same table.
**
**   4. Vertex colours read as BGRA.  D3DCOLOR is B, G, R, A in memory.  D3D11 reads it with
**      DXGI_FORMAT_B8G8R8A8_UNORM (dx11layout.cpp) and hands the shader r, g, b, a; SDL has no such
**      vertex format, so the backend reads it as UBYTE4_NORM and the program swaps it back.
**
**   5. Every texture read through its own sampler, at its own slot.  SDL binds textures and
**      samplers as pairs, slot n being texture n with sampler n, and SDL_shadercross pairs them by
**      that rule too: a texture read through another slot's sampler is given no slot of its own and
**      collides with texture 0 on Metal.  The generators did exactly that with the normal map (t4),
**      read through stage 0's sampler, until the normal maps were taken out on 2026-10-06; the
**      rebinding stays for any such read.  So each such read is rebound:
**        - the reads through the lowest-numbered sampler keep the texture's own slot, with a sampler
**          of that slot beside it;
**        - the reads through any other sampler read a second name for the same texture, at the next
**          slot above every one the program declared, with its own sampler.
**      The program then opens with one "// SDL3 slot N: texture tT, sampler state sS" line per slot so
**      rebound, which is the backend's contract: slot N is bound to texture T with stage S's sampler
**      state.  Every other slot n is texture n with sampler state n.
**
** A program with anything this does not recognise - an unknown semantic, a register kind other than
** t, s or b - is refused, the way a generator refuses a description it does not know, rather than
** passed on half rewritten.
*/

#ifndef SDL3TARGET_H
#define SDL3TARGET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

// Where each varying goes: the diffuse and specular colours, eight coordinate sets (four for the
// stages, four for the normal mapped lighting) and the fog.
inline int SDL3_Varying_Location(const std::string & semantic)
{
	if (semantic == "COLOR0") return 0;
	if (semantic == "COLOR1") return 1;
	if (semantic.compare(0, 8, "TEXCOORD") == 0 && semantic.size() == 9 && semantic[8] >= '0' && semantic[8] <= '7') {
		return 2 + (semantic[8] - '0');
	}
	if (semantic == "FOG") return 10;
	return -1;
}

// Where each vertex attribute goes.  The backend's vertex layout for SDL is built by this table.
inline int SDL3_Attribute_Location(const std::string & semantic)
{
	if (semantic == "POSITION") return 0;
	if (semantic == "NORMAL") return 1;
	if (semantic == "COLOR0") return 2;
	if (semantic == "COLOR1") return 3;
	if (semantic.compare(0, 8, "TEXCOORD") == 0 && semantic.size() == 9 && semantic[8] >= '0' && semantic[8] <= '7') {
		return 4 + (semantic[8] - '0');
	}
	return -1;
}

namespace sdl3target_detail {

inline bool place_registers(std::string & hlsl, bool vertex_stage)
{
	static const char OPEN[] = "register(";
	size_t at = 0;
	while ((at = hlsl.find(OPEN, at)) != std::string::npos) {
		size_t cursor = at + sizeof(OPEN) - 1;
		const char kind = cursor < hlsl.size() ? hlsl[cursor] : '\0';
		if (kind != 't' && kind != 's' && kind != 'b') {
			return false;
		}
		++cursor;
		const size_t digits = cursor;
		while (cursor < hlsl.size() && hlsl[cursor] >= '0' && hlsl[cursor] <= '9') ++cursor;
		if (cursor == digits || cursor >= hlsl.size() || hlsl[cursor] != ')') {
			return false;
		}
		const char * space = kind == 'b' ? (vertex_stage ? ", space1" : ", space3") : (vertex_stage ? ", space0" : ", space2");
		hlsl.insert(cursor, space);
		at = cursor;
	}
	return true;
}

// Every member of "struct <name>" whose semantic is not a system value gets [[vk::location(N)]].
// The names of the members at COLOR0 and COLOR1 are handed back for the BGRA swap.
inline bool locate_members(std::string & hlsl, const char * name, bool attributes, std::string colours[2])
{
	const std::string opening = std::string("struct ") + name + "\n{\n";
	const size_t start = hlsl.find(opening);
	if (start == std::string::npos) {
		return true;	// not every program has both structures (a pixel program has no Output)
	}
	size_t line = start + opening.size();
	for (;;) {
		const size_t end = hlsl.find('\n', line);
		if (end == std::string::npos) return false;
		const std::string text = hlsl.substr(line, end - line);
		if (text.compare(0, 2, "};") == 0) {
			return true;
		}
		const size_t colon = text.find(" : ");
		const size_t semicolon = text.rfind(';');
		if (colon == std::string::npos || semicolon == std::string::npos || semicolon < colon) {
			return false;
		}
		const std::string semantic = text.substr(colon + 3, semicolon - colon - 3);
		if (semantic.compare(0, 3, "SV_") != 0) {
			const int location = attributes ? SDL3_Attribute_Location(semantic) : SDL3_Varying_Location(semantic);
			if (location < 0) {
				return false;
			}
			size_t indent = 0;
			while (indent < text.size() && (text[indent] == ' ' || text[indent] == '\t')) ++indent;
			if (semantic == "COLOR0" || semantic == "COLOR1") {
				// "float4 Diffuse" - the member name is the last word before the colon.
				size_t name_end = colon;
				while (name_end > indent && text[name_end - 1] == ' ') --name_end;
				size_t name_start = name_end;
				while (name_start > indent && text[name_start - 1] != ' ') --name_start;
				colours[semantic == "COLOR0" ? 0 : 1] = text.substr(name_start, name_end - name_start);
			}
			char prefix[32];
			snprintf(prefix, sizeof(prefix), "[[vk::location(%d)]] ", location);
			hlsl.insert(line + indent, prefix);
		}
		line = hlsl.find('\n', line) + 1;
	}
}

// "Texture2D Name : register(t4);" -> Name, 4.  Only the D3D11 form, before spaces are placed.
inline bool parse_declaration(const std::string & line, const char * type, char kind, std::string & name, int & slot)
{
	const std::string opening = std::string(type) + " ";
	if (line.compare(0, opening.size(), opening) != 0) return false;
	const size_t colon = line.find(" : register(");
	if (colon == std::string::npos) return false;
	name = line.substr(opening.size(), colon - opening.size());
	const size_t at = colon + 12;
	if (at >= line.size() || line[at] != kind) return false;
	slot = atoi(line.c_str() + at + 1);
	return true;
}

inline bool pair_samplers(std::string & hlsl)
{
	struct Declared { std::string name; int slot; size_t line_end; };
	std::vector<Declared> textures, samplers;
	int highest = -1;
	for (size_t line = 0; line < hlsl.size();) {
		size_t end = hlsl.find('\n', line);
		if (end == std::string::npos) end = hlsl.size();
		const std::string text = hlsl.substr(line, end - line);
		Declared d;
		if (parse_declaration(text, "Texture2D", 't', d.name, d.slot)) {
			d.line_end = end;
			textures.push_back(d);
			if (d.slot > highest) highest = d.slot;
		}
		else if (parse_declaration(text, "SamplerState", 's', d.name, d.slot)) {
			d.line_end = end;
			samplers.push_back(d);
			if (d.slot > highest) highest = d.slot;
		}
		line = end + 1;
	}

	std::string plan, declarations_after_last;
	for (size_t t = 0; t < textures.size(); ++t) {
		const Declared & texture = textures[t];
		// The samplers this texture is read through, lowest slot first.
		std::vector<const Declared *> through;
		for (size_t k = 0; k < samplers.size(); ++k) {
			const std::string sample = texture.name + ".Sample(" + samplers[k].name + ",";
			const std::string level = texture.name + ".SampleLevel(" + samplers[k].name + ",";
			if (hlsl.find(sample) != std::string::npos || hlsl.find(level) != std::string::npos) {
				through.push_back(&samplers[k]);
			}
		}
		for (size_t i = 1; i < through.size(); ++i) {
			for (size_t j = i; j > 0 && through[j - 1]->slot > through[j]->slot; --j) std::swap(through[j - 1], through[j]);
		}
		if (through.empty() || (through.size() == 1 && through[0]->slot == texture.slot)) {
			continue;
		}
		// A sampler of the texture's own slot, used or not, leaves that slot to it: every other read
		// goes to a new slot rather than declaring a second sampler there.
		bool slot_taken = false;
		for (size_t k = 0; k < samplers.size(); ++k) slot_taken = slot_taken || samplers[k].slot == texture.slot;
		for (size_t i = 0; i < through.size(); ++i) {
			const Declared & sampler = *through[i];
			if (sampler.slot == texture.slot) {
				continue;	// already its own pair
			}
			const bool keeps_slot = !slot_taken;
			slot_taken = true;
			const int slot = keeps_slot ? texture.slot : ++highest;
			const std::string texture_name = keeps_slot ? texture.name : texture.name + "_" + sampler.name;
			const std::string sampler_name = texture_name + "_Sampler";
			char line[256];
			if (!keeps_slot) {
				snprintf(line, sizeof(line), "Texture2D %s : register(t%d);\n", texture_name.c_str(), slot);
				declarations_after_last += line;
			}
			snprintf(line, sizeof(line), "SamplerState %s : register(s%d);\n", sampler_name.c_str(), slot);
			declarations_after_last += line;
			snprintf(line, sizeof(line), "// SDL3 slot %d: texture t%d, sampler state s%d\n", slot, texture.slot, sampler.slot);
			plan += line;
			static const char * const CALLS[] = { ".Sample(", ".SampleLevel(" };
			for (int c = 0; c < 2; ++c) {
				const std::string from = texture.name + CALLS[c] + sampler.name + ",";
				const std::string to = texture_name + CALLS[c] + sampler_name + ",";
				for (size_t at = hlsl.find(from); at != std::string::npos; at = hlsl.find(from, at + to.size())) {
					hlsl.replace(at, from.size(), to);
				}
			}
		}
	}
	if (!plan.empty()) {
		// The new declarations go after the last texture or sampler the program declared, which is
		// before anything reads them.
		size_t after = 0;
		// (std::max): windows.h's max macro would otherwise take the call apart under MSVC (W2)
		for (size_t t = 0; t < textures.size(); ++t) after = (std::max)(after, textures[t].line_end);
		for (size_t k = 0; k < samplers.size(); ++k) after = (std::max)(after, samplers[k].line_end);
		const size_t at = hlsl.find('\n', after);
		hlsl.insert(at == std::string::npos ? hlsl.size() : at + 1, declarations_after_last);
		hlsl.insert(0, plan);
	}
	return true;
}

} // namespace sdl3target_detail

// Rewrites one generated D3D11 program for SDL3 GPU in place.  False, leaving hlsl in an unspecified
// state, when the text holds something this does not know how to place.
inline bool SDL3_Shader_Retarget(std::string & hlsl, bool vertex_stage)
{
	using namespace sdl3target_detail;
	if (!pair_samplers(hlsl) || !place_registers(hlsl, vertex_stage)) {
		return false;
	}
	std::string attribute_colours[2], varying_colours[2];
	if (vertex_stage) {
		if (!locate_members(hlsl, "Input", true, attribute_colours) || !locate_members(hlsl, "Output", false, varying_colours)) {
			return false;
		}
	}
	else if (!locate_members(hlsl, "Input", false, varying_colours)) {
		return false;
	}

	if (vertex_stage && (!attribute_colours[0].empty() || !attribute_colours[1].empty())) {
		static const char ENTRY[] = "main(Input input)\n{\n";
		const size_t entry = hlsl.find(ENTRY);
		if (entry == std::string::npos) {
			return false;
		}
		std::string swaps;
		for (int k = 0; k < 2; ++k) {
			if (!attribute_colours[k].empty()) {
				swaps += "    input." + attribute_colours[k] + " = input." + attribute_colours[k] + ".bgra;\n";
			}
		}
		hlsl.insert(entry + sizeof(ENTRY) - 1, swaps);
	}
	return true;
}

#endif
