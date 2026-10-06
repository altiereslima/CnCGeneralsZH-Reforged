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
// Portions adapted from GeneralsMD/Code/Tests/test_ffvertex.cpp and GeneralsMD/Code/Tests/test_ffshadercompile.cpp by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.
/*
 * Every shader program the D3D11 backend's generators are known to write, as descriptions, for the
 * tools and tests that have to walk all of them: ffshader_dump (the golden dump that proves a
 * target's text unchanged) and test_shader_sdl (the SDL3 GPU target through glslang, SPIR-V and
 * MSL).
 *
 * The reference set is the 49 programs decision 4 was measured on (PORTING.md), less the three
 * bumped terrain programs that went with the normal maps, so 46:
 *   - 19 pixel programs: every case test_ffshadercompile compiles,
 *   - 14 vertex programs: every case test_ffvertexcompile compiles,
 *   - 13 engine programs: every program engineshader.cpp transcribes.
 * Each is copied from the test that owns it, field for field.  The rest are marked Reference = false:
 * the shadow receiving and pre-transformed programs those tests never asked for,
 * which are exactly the branches a new target has to get right.
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "ffstate.h"
#include "ffshader.h"
#include "ffvertex.h"
#include "engineshader.h"

enum ShaderCaseKind
{
	SHADER_CASE_COMBINER,	// ffshader: CombinerShader_Generate
	SHADER_CASE_VERTEX,		// ffvertex: VertexShader_Generate
	SHADER_CASE_ENGINE		// engineshader: EngineShader_Vertex_Program or _Pixel_Program
};

enum ShaderCaseTarget
{
	SHADER_CASE_D3D9,
	SHADER_CASE_D3D11,
	SHADER_CASE_SDL3_GPU
};

struct ShaderCase
{
	std::string Name;
	ShaderCaseKind Kind;
	bool VertexStage;
	bool Reference;

	CombinerDescription Combiner;
	VertexPipelineDescription Vertex;
	EngineShaderProgram Engine;
	PixelPipelineDescription EnginePipeline;
};

inline const char *Shader_Case_Target_Name(ShaderCaseTarget target)
{
	return target == SHADER_CASE_D3D9 ? "d3d9" : (target == SHADER_CASE_D3D11 ? "d3d11" : "sdl3");
}

namespace shader_cases_detail {

inline CombinerStage one_stage(FixedFunctionValue colour_operation, FixedFunctionValue colour_argument1,
	FixedFunctionValue colour_argument2, FixedFunctionValue alpha_operation, FixedFunctionValue alpha_argument1,
	FixedFunctionValue alpha_argument2, FixedFunctionValue coordinate_index, bool texture_bound)
{
	CombinerStage stage;
	memset(&stage, 0, sizeof(stage));
	stage.ColourOperation = colour_operation;
	stage.ColourArgument1 = colour_argument1;
	stage.ColourArgument2 = colour_argument2;
	stage.AlphaOperation = alpha_operation;
	stage.AlphaArgument1 = alpha_argument1;
	stage.AlphaArgument2 = alpha_argument2;
	stage.TextureCoordinateIndex = coordinate_index;
	stage.TextureBound = texture_bound;
	return stage;
}

inline ShaderCase combiner(const std::string &name, bool reference)
{
	ShaderCase c;
	c.Name = name;
	c.Kind = SHADER_CASE_COMBINER;
	c.VertexStage = false;
	c.Reference = reference;
	memset(&c.Combiner.Stages, 0, sizeof(c.Combiner.Stages));
	c.Combiner.StageCount = 0;
	memset(&c.Combiner.PixelPipeline, 0, sizeof(c.Combiner.PixelPipeline));
	c.Combiner.ShadowReceiving = false;
	c.Vertex = VertexPipelineDescription();
	c.Engine = ENGINE_SHADER_NONE;
	memset(&c.EnginePipeline, 0, sizeof(c.EnginePipeline));
	return c;
}

// test_ffvertexcompile's plain_description.
inline VertexPipelineDescription plain_vertex()
{
	// Value-initialised: zero, and the flags their declared false.  A memset over a structure with a
	// default member initialiser is what gcc's -Wclass-memaccess warns about.
	VertexPipelineDescription description = VertexPipelineDescription();
	description.FVF = FF_FVF_XYZ | FF_FVF_NORMAL | FF_FVF_TEX2 | FF_FVF_DIFFUSE;
	description.ColourVertexEnabled = true;
	description.DiffuseMaterialSource = FF_MCS_COLOR1;
	description.AmbientMaterialSource = FF_MCS_MATERIAL;
	description.EmissiveMaterialSource = FF_MCS_MATERIAL;
	description.SpecularMaterialSource = FF_MCS_MATERIAL;
	description.StageCount = 1;
	description.Stages[0].TextureCoordinateIndex = FF_TSS_TCI_PASSTHRU;
	description.Stages[0].TextureTransformFlags = FF_TTFF_DISABLE;
	description.FogVertexMode = FF_FOG_NONE;
	return description;
}

inline ShaderCase vertex(const std::string &name, bool reference, const VertexPipelineDescription &description)
{
	ShaderCase c = combiner(name, reference);
	c.Kind = SHADER_CASE_VERTEX;
	c.VertexStage = true;
	c.Vertex = description;
	return c;
}

} // namespace shader_cases_detail

inline std::vector<ShaderCase> Shader_Cases()
{
	using namespace shader_cases_detail;
	std::vector<ShaderCase> cases;

	// ---- test_ffshadercompile ----
	static const FixedFunctionValue OPERATIONS[] = { FF_TOP_SELECTARG1, FF_TOP_SELECTARG2, FF_TOP_MODULATE,
		FF_TOP_MODULATE2X, FF_TOP_MODULATE4X, FF_TOP_ADD, FF_TOP_ADDSIGNED, FF_TOP_SUBTRACT, FF_TOP_DOTPRODUCT3 };
	static const char *const OPERATION_NAMES[] = { "selectarg1", "selectarg2", "modulate", "modulate2x",
		"modulate4x", "add", "addsigned", "subtract", "dotproduct3" };
	for (size_t i = 0; i < sizeof(OPERATIONS) / sizeof(OPERATIONS[0]); ++i) {
		ShaderCase c = combiner(std::string("ps_op_") + OPERATION_NAMES[i], true);
		c.Combiner.StageCount = 1;
		c.Combiner.Stages[0] = one_stage(OPERATIONS[i], FF_TA_TEXTURE, FF_TA_DIFFUSE, OPERATIONS[i], FF_TA_TEXTURE, FF_TA_DIFFUSE, 0, true);
		cases.push_back(c);
	}
	{
		// A lit specular draw's pixel half: one modulated stage and D3DRS_SPECULARENABLE.
		ShaderCase c = combiner("ps_extra_specular_add", false);
		c.Combiner.StageCount = 1;
		c.Combiner.Stages[0] = one_stage(FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_DIFFUSE, FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_DIFFUSE, 0, true);
		c.Combiner.SpecularAdd = true;
		cases.push_back(c);
	}
	{
		// The trees' shape (W3DTreeBuffer with the shroud on stage 1): stage 1 generates its coordinates
		// from the camera-space position, and its TEXCOORDINDEX's set bits are 0.
		ShaderCase c = combiner("ps_extra_texgen_on_stage_1", false);
		c.Combiner.StageCount = 2;
		c.Combiner.Stages[0] = one_stage(FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_DIFFUSE, FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_DIFFUSE, 0, true);
		c.Combiner.Stages[1] = one_stage(FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_CURRENT, FF_TOP_SELECTARG2, FF_TA_TEXTURE, FF_TA_CURRENT, FF_TSS_TCI_CAMERASPACEPOSITION, true);
		cases.push_back(c);
	}
	{
		ShaderCase c = combiner("ps_shroud_widest", true);
		c.Combiner.StageCount = 2;
		c.Combiner.Stages[0] = one_stage(FF_TOP_MULTIPLYADD, FF_TA_TEXTURE, FF_TA_DIFFUSE, FF_TOP_SELECTARG1, FF_TA_TEXTURE, FF_TA_CURRENT, 0, true);
		c.Combiner.Stages[0].ColourArgument0 = FF_TA_TFACTOR | FF_TA_ALPHAREPLICATE;
		c.Combiner.Stages[1] = one_stage(FF_TOP_DOTPRODUCT3, FF_TA_TEXTURE, FF_TA_CURRENT, FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_CURRENT, 1, true);
		cases.push_back(c);
	}
	static const FixedFunctionValue COMPARISONS[] = { FF_CMP_NEVER, FF_CMP_LESS, FF_CMP_EQUAL, FF_CMP_LESSEQUAL,
		FF_CMP_GREATER, FF_CMP_NOTEQUAL, FF_CMP_GREATEREQUAL, FF_CMP_ALWAYS };
	for (size_t i = 0; i < sizeof(COMPARISONS) / sizeof(COMPARISONS[0]); ++i) {
		char name[64];
		snprintf(name, sizeof(name), "ps_alphatest_fog_%lu", (unsigned long)COMPARISONS[i]);
		ShaderCase c = combiner(name, true);
		c.Combiner.StageCount = 1;
		c.Combiner.Stages[0] = one_stage(FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_DIFFUSE, FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_DIFFUSE, 0, true);
		c.Combiner.PixelPipeline.AlphaTestEnabled = true;
		c.Combiner.PixelPipeline.AlphaFunction = COMPARISONS[i];
		c.Combiner.PixelPipeline.FogEnabled = true;
		cases.push_back(c);
	}
	{
		ShaderCase c = combiner("ps_tree_shadow", true);
		c.Combiner.StageCount = 1;
		c.Combiner.Stages[0] = one_stage(FF_TOP_SELECTARG1, FF_TA_TFACTOR, FF_TA_DIFFUSE, FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_TFACTOR, 0, true);
		cases.push_back(c);
	}

	// ---- beyond the reference set: the pixel branches the compile tests never reached ----
	{
		ShaderCase c = combiner("ps_extra_shadow_receiving", false);
		c.Combiner.StageCount = 1;
		c.Combiner.Stages[0] = one_stage(FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_DIFFUSE, FF_TOP_MODULATE, FF_TA_TEXTURE, FF_TA_DIFFUSE, 0, true);
		c.Combiner.ShadowReceiving = true;
		c.Combiner.PixelPipeline.FogEnabled = true;
		cases.push_back(c);
	}

	// ---- test_ffvertexcompile ----
	struct VertexCase
	{
		const char *Name;
		bool LightingEnabled;
		bool SpecularEnabled;
		unsigned LightCount;
		FixedFunctionValue LightType;
		FixedFunctionValue TextureCoordinateIndex;
		FixedFunctionValue TextureTransformFlags;
		bool FogEnabled;
		FixedFunctionValue FogVertexMode;
	};
	static const VertexCase VERTEX_CASES[] = {
		{ "vs_unlit_passthrough", false, false, 0, 0, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, false, FF_FOG_NONE },
		{ "vs_one_directional", true, false, 1, FF_LIGHT_DIRECTIONAL, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, false, FF_FOG_NONE },
		{ "vs_three_directional", true, false, 3, FF_LIGHT_DIRECTIONAL, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, false, FF_FOG_NONE },
		{ "vs_point_light", true, false, 1, FF_LIGHT_POINT, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, false, FF_FOG_NONE },
		{ "vs_spot_light", true, false, 1, FF_LIGHT_SPOT, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, false, FF_FOG_NONE },
		{ "vs_specular", true, true, 1, FF_LIGHT_DIRECTIONAL, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, false, FF_FOG_NONE },
		{ "vs_camera_space_position", false, false, 0, 0, FF_TSS_TCI_CAMERASPACEPOSITION, FF_TTFF_COUNT2, false, FF_FOG_NONE },
		{ "vs_camera_space_normal", false, false, 0, 0, FF_TSS_TCI_CAMERASPACENORMAL, FF_TTFF_DISABLE, false, FF_FOG_NONE },
		{ "vs_reflection_vector", false, false, 0, 0, FF_TSS_TCI_CAMERASPACEREFLECTIONVECTOR, FF_TTFF_DISABLE, false, FF_FOG_NONE },
		{ "vs_projected_transform", false, false, 0, 0, FF_TSS_TCI_CAMERASPACEPOSITION, FF_TTFF_COUNT3 | FF_TTFF_PROJECTED, false, FF_FOG_NONE },
		{ "vs_linear_fog", false, false, 0, 0, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, true, FF_FOG_LINEAR },
		{ "vs_exp_fog", false, false, 0, 0, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, true, FF_FOG_EXP },
		{ "vs_exp2_fog", false, false, 0, 0, FF_TSS_TCI_PASSTHRU, FF_TTFF_DISABLE, true, FF_FOG_EXP2 },
	};
	for (size_t i = 0; i < sizeof(VERTEX_CASES) / sizeof(VERTEX_CASES[0]); ++i) {
		const VertexCase &source = VERTEX_CASES[i];
		VertexPipelineDescription description = plain_vertex();
		description.LightingEnabled = source.LightingEnabled;
		description.SpecularEnabled = source.SpecularEnabled;
		description.LightCount = source.LightCount;
		for (unsigned light = 0; light < source.LightCount; ++light) {
			description.Lights[light].Type = source.LightType;
		}
		description.Stages[0].TextureCoordinateIndex = source.TextureCoordinateIndex;
		description.Stages[0].TextureTransformFlags = source.TextureTransformFlags;
		description.FogEnabled = source.FogEnabled;
		description.FogVertexMode = source.FogVertexMode;
		cases.push_back(vertex(source.Name, true, description));
	}
	{
		VertexPipelineDescription description = plain_vertex();
		description.LightingEnabled = true;
		description.LightCount = 2;
		description.Lights[0].Type = FF_LIGHT_DIRECTIONAL;
		description.Lights[1].Type = FF_LIGHT_POINT;
		description.StageCount = 2;
		description.Stages[0].TextureCoordinateIndex = FF_TSS_TCI_PASSTHRU;
		description.Stages[0].TextureTransformFlags = FF_TTFF_DISABLE;
		description.Stages[1].TextureCoordinateIndex = FF_TSS_TCI_CAMERASPACENORMAL;
		description.Stages[1].TextureTransformFlags = FF_TTFF_COUNT2;
		cases.push_back(vertex("vs_two_stage", true, description));
	}

	// ---- beyond the reference set: the vertex branches the compile tests never reached ----
	{
		VertexPipelineDescription description = plain_vertex();
		description.FVF = FF_FVF_XYZRHW | FF_FVF_DIFFUSE | FF_FVF_TEX1;
		cases.push_back(vertex("vs_extra_pretransformed", false, description));
	}
	{
		VertexPipelineDescription description = plain_vertex();
		description.FVF = FF_FVF_XYZRHW | FF_FVF_TEX1;
		cases.push_back(vertex("vs_extra_pretransformed_no_colour", false, description));
	}
	{
		VertexPipelineDescription description = plain_vertex();
		description.FVF = FF_FVF_XYZ | FF_FVF_NORMAL | FF_FVF_TEX1;
		description.LightingEnabled = true;
		description.LightCount = 1;
		description.Lights[0].Type = FF_LIGHT_DIRECTIONAL;
		cases.push_back(vertex("vs_extra_lit_no_colour", false, description));
	}
	{
		// A mesh with a second colour array (D3DFVF_SPECULAR, dx8renderer.cpp) drawn unlit, and the
		// same pretransformed, and lit with its specular material read from that colour.
		VertexPipelineDescription description = plain_vertex();
		description.FVF = FF_FVF_XYZ | FF_FVF_DIFFUSE | FF_FVF_SPECULAR | FF_FVF_TEX1;
		cases.push_back(vertex("vs_extra_unlit_vertex_specular", false, description));
		description.FVF = FF_FVF_XYZRHW | FF_FVF_DIFFUSE | FF_FVF_SPECULAR | FF_FVF_TEX1;
		cases.push_back(vertex("vs_extra_pretransformed_vertex_specular", false, description));
		description.FVF = FF_FVF_XYZ | FF_FVF_NORMAL | FF_FVF_DIFFUSE | FF_FVF_SPECULAR | FF_FVF_TEX1;
		description.LightingEnabled = true;
		description.SpecularEnabled = true;
		description.ColourVertexEnabled = true;
		description.SpecularMaterialSource = FF_MCS_COLOR2;
		description.LightCount = 1;
		description.Lights[0].Type = FF_LIGHT_DIRECTIONAL;
		cases.push_back(vertex("vs_extra_lit_specular_from_vertex", false, description));
	}
	{
		// Specular with D3D9's default local viewer, as every lit specular draw of the engine's is.
		VertexPipelineDescription description = plain_vertex();
		description.LightingEnabled = true;
		description.SpecularEnabled = true;
		description.LocalViewer = true;
		description.LightCount = 1;
		description.Lights[0].Type = FF_LIGHT_DIRECTIONAL;
		cases.push_back(vertex("vs_extra_specular_local_viewer", false, description));
	}
	{
		// A scrolling texture as W3D's mappers set one up: the vertex's own set through a COUNT2
		// transform whose translation is in _31 and _32 (mapper.cpp).
		VertexPipelineDescription description = plain_vertex();
		description.Stages[0].TextureCoordinateIndex = FF_TSS_TCI_PASSTHRU;
		description.Stages[0].TextureTransformFlags = FF_TTFF_COUNT2;
		cases.push_back(vertex("vs_extra_passthrough_count2", false, description));
	}

	// ---- engineshader.cpp: every program, pixel and vertex ----
	for (int program = ENGINE_SHADER_TREES; program <= ENGINE_SHADER_MONOCHROME; ++program) {
		const EngineShaderProgram which = (EngineShaderProgram)program;
		std::string name = EngineShader_Name(which);
		for (size_t k = 0; k < name.size(); ++k) {
			if (name[k] == ':' || name[k] == ' ') name[k] = '_';
		}
		std::string scratch;
		ShaderCase c = combiner("", true);
		c.Kind = SHADER_CASE_ENGINE;
		c.Engine = which;
		if (EngineShader_Vertex_Program(which, scratch)) {
			c.Name = "vs_es_" + name;
			c.VertexStage = true;
			cases.push_back(c);
		}
		c.VertexStage = false;
		scratch.clear();
		if (EngineShader_Pixel_Program(which, c.EnginePipeline, scratch)) {
			c.Name = "ps_es_" + name;
			cases.push_back(c);
			// Beyond the reference set: the alpha test and the fog, which the engine's pixel programs
			// take from the render state the way ffshader's do.
			c.Name = "ps_es_" + name + "_extra_alphatest_fog";
			c.Reference = false;
			c.EnginePipeline.AlphaTestEnabled = true;
			c.EnginePipeline.AlphaFunction = FF_CMP_GREATEREQUAL;
			c.EnginePipeline.FogEnabled = true;
			cases.push_back(c);
			c.Reference = true;
			memset(&c.EnginePipeline, 0, sizeof(c.EnginePipeline));
		}
	}
	return cases;
}

// One case for one target.  False when the generator refuses it or when the
// target does not apply (the engine programs exist only on the shader model 4 profiles).
inline bool Shader_Case_Generate(const ShaderCase &c, ShaderCaseTarget target, std::string &hlsl)
{
	hlsl.clear();
	switch (c.Kind) {
		case SHADER_CASE_COMBINER: {
			CombinerShaderTarget t = target == SHADER_CASE_D3D9 ? COMBINER_SHADER_TARGET_D3D9 : COMBINER_SHADER_TARGET_D3D11;
#if !defined(SHADER_CASES_BEFORE_SDL3_TARGET)
			if (target == SHADER_CASE_SDL3_GPU) t = COMBINER_SHADER_TARGET_SDL3_GPU;
#endif
			return CombinerShader_Generate(c.Combiner, t, hlsl);
		}
		case SHADER_CASE_VERTEX: {
			VertexShaderTarget t = target == SHADER_CASE_D3D9 ? VERTEX_SHADER_TARGET_D3D9 : VERTEX_SHADER_TARGET_D3D11;
#if !defined(SHADER_CASES_BEFORE_SDL3_TARGET)
			if (target == SHADER_CASE_SDL3_GPU) t = VERTEX_SHADER_TARGET_SDL3_GPU;
#endif
			return VertexShader_Generate(c.Vertex, t, hlsl);
		}
		case SHADER_CASE_ENGINE:
			if (target == SHADER_CASE_D3D9) return false;
#if !defined(SHADER_CASES_BEFORE_SDL3_TARGET)
			if (target == SHADER_CASE_SDL3_GPU) {
				return c.VertexStage ? EngineShader_Vertex_Program(c.Engine, hlsl, VERTEX_SHADER_TARGET_SDL3_GPU)
					: EngineShader_Pixel_Program(c.Engine, c.EnginePipeline, hlsl, COMBINER_SHADER_TARGET_SDL3_GPU);
			}
#endif
			return c.VertexStage ? EngineShader_Vertex_Program(c.Engine, hlsl)
				: EngineShader_Pixel_Program(c.Engine, c.EnginePipeline, hlsl);
	}
	return false;
}
