/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

/*
** The fixed-function texture stages, written out as HLSL.
**
** D3D11 has no texture stage combiners, so the Direct3D 11 backend has to say in a shader
** what the stages were computing.  -ffprobe counted what the game actually asks for across four
** maps: 28 distinct combiner programs, never more than two stages, and a vocabulary of five
** operations over four arguments.  This turns one of those descriptions into the shader.
**
** It generates for D3D9 first, on purpose.  A pixel shader on the existing device can be compared
** against the fixed-function pipeline it replaces with tree-check.ps1, which turns the phase from
** one untestable port into a generator that is proved against the real game before any of it is
** carried to a second backend.  Fog and the alpha test are not generated for that reason: D3D9
** still applies both around a pixel shader, so leaving them alone keeps the comparison to the one
** thing being replaced.  D3D11 has neither and will want them here.
*/

#ifndef FFSHADER_H
#define FFSHADER_H

#include "ffstate.h"

#include <string>

// Two is what the game uses everywhere except the water, which sets four: the river texture, the
// sparkles, the noise and the shroud.  The generator refuses a description with more rather than
// emitting a shader nobody has compared against anything.
const unsigned MAXIMUM_COMBINER_STAGES = 4;

// One texture stage, in the terms D3D8 set it in.  Arguments carry D3DTA_COMPLEMENT and
// D3DTA_ALPHAREPLICATE the way the device does; the generator applies both.
struct CombinerStage
{
	FixedFunctionValue ColourOperation;
	FixedFunctionValue ColourArgument0;
	FixedFunctionValue ColourArgument1;
	FixedFunctionValue ColourArgument2;
	FixedFunctionValue AlphaOperation;
	FixedFunctionValue AlphaArgument0;
	FixedFunctionValue AlphaArgument1;
	FixedFunctionValue AlphaArgument2;
	FixedFunctionValue TextureCoordinateIndex;
	bool  TextureBound;
};

// What a draw asks the combiners to compute.  Stages past StageCount are not read.
// The two pieces of the D3D9 pixel pipeline that are neither texture stages nor shader
// instructions.  D3D9 applies both around a bound pixel shader and D3D11 has neither, so they are
// part of the program on one profile and absent from it on the other.  The alpha reference and the
// fog colour are not here: they are uniforms, and two draws differing only in one are one program.
struct PixelPipelineDescription
{
	bool AlphaTestEnabled;

	// D3DCMP_*, the comparison the surviving alpha has to pass.
	FixedFunctionValue AlphaFunction;

	bool FogEnabled;
};

struct CombinerDescription
{
	CombinerStage Stages[MAXIMUM_COMBINER_STAGES];
	unsigned      StageCount;

	// Only read when generating for D3D11.  On D3D9 the device still applies both itself around a
	// bound pixel shader, and generating them there would apply each of them twice.
	PixelPipelineDescription PixelPipeline;

	// D3D11 only: light the pixel again through the normal map at t4 before the
	// stages read the diffuse colour, and add a highlight scaled by the map's alpha after them.
	// The vertex half has to have been generated with the same flag.  Initialised here because
	// callers fill a description field by field and one written before this existed never sets it.
	bool NormalMapped = false;

	// D3D11 only: take the pixel back out of clip space, look it up in the sun's depth buffer at t5
	// and darken it by how much of the filter comes back blocked.  No vertex half is involved: the
	// position comes from SV_Position and one matrix, which is what keeps this off the varyings the
	// two generators have to agree on.  SHADOW-MAP-PLAN.md phase 2.
	bool ShadowReceiving = false;

	// D3DRS_SPECULARENABLE: after the stages the pixel gains the vertex's specular colour, RGB only
	// (D3DRENDERSTATETYPE: "added to the base color after the texture cascade but before alpha
	// blending").  Every profile writes it, D3D9's included: D3D9 does the add only for its
	// fixed-function stages, and a bound pixel shader, which the D3D9 profile's program is, replaces
	// it ("Writing HLSL Shaders in Direct3D 9").  A normal mapped program adds its own highlight
	// instead.  Initialised here for a caller that fills the rest field by field.
	bool SpecularAdd = false;

	// D3D11 only: the vertex half carries a fire's glow in the specular slot (VertexPipelineDescription::
	// SmokeGlow), and it is added after the shadow and the smoke's own shade, scaled by the last
	// stage's texel the way the stages scale the diffuse colour.  Baked into the vertex colour, the
	// glow of a fire behind a plume went dark with the plume's far side.  Replaces SpecularAdd, whose
	// slot it takes.  Initialised here for a caller that fills the rest field by field.
	bool SmokeGlow = false;
};

// The normal mapped pixel program reads this many directional lights from its constants.  Slots
// past the draw's own lights carry no colour and add nothing.
const unsigned NORMAL_MAPPED_LIGHTS = 4;

// How much of the sun reaches a pixel, shared by the generated programs and the transcribed ones so
// the two cannot drift.  The pixel is taken back out of clip space by one matrix, which lands it in
// the sun's own clip space, and the filter is a square of taps around it: the share that come back
// blocked is the share of the light that is missing.  ShadowParameters is the map's texel size, the
// depth bias, how dark a fully blocked pixel goes and the radius in texels; a strength of zero is a
// frame with no shadow map and every pixel in full sun.  SHADOW-MAP-PLAN.md phase 2.
#define SHADOW_SAMPLING \
	"Texture2D ShadowMap : register(t5);\n" \
	"SamplerState ShadowSampler : register(s5);\n" \
	"\n" \
	"float sun_reaching(float4 position)\n" \
	"{\n" \
	"    if (ShadowParameters.z <= 0.0) return 1.0;\n" \
	"    float2 ndc = float2(position.x * ShadowViewport.x * 2.0 - 1.0,\n" \
	"                        1.0 - position.y * ShadowViewport.y * 2.0);\n" \
	"    float4 sun = mul(float4(ndc, position.z, 1.0), ShadowFromClip);\n" \
	"    if (sun.w <= 0.0) return 1.0;\n" \
	"    sun /= sun.w;\n" \
	"    float2 map = float2(0.5 * sun.x + 0.5, 0.5 - 0.5 * sun.y);\n" \
	"    if (map.x < 0.0 || map.x > 1.0 || map.y < 0.0 || map.y > 1.0) return 1.0;\n" \
	"    if (sun.z < 0.0 || sun.z > 1.0) return 1.0;\n" \
	"    float texel = ShadowParameters.x;\n" \
	"    /* Two ways of keeping a surface from shadowing itself, and they are not the same thing.\n" \
	"       A depth bias pushes the comparison back, which works and costs the contact: the shadow\n" \
	"       lifts off the foot of whatever casts it and the building above it looks placed on the\n" \
	"       ground rather than standing on it.  So most of it is a normal offset instead - the\n" \
	"       lookup moves sideways, along the surface, by a fraction of a texel - and the depth bias\n" \
	"       keeps only what the slope across one pixel needs.  Pull the camera back and that slope\n" \
	"       grows, which is the case a fixed bias cannot cover. */\n" \
	"    float3 surface = normalize(cross(ddx(sun.xyz), ddy(sun.xyz)));\n" \
	"    map += surface.xy * texel * ShadowParameters.y * 400.0;\n" \
	"    float slope = max(abs(ddx(sun.z)), abs(ddy(sun.z)));\n" \
	"    float bias = ShadowParameters.y * 0.35 + slope * 1.5;\n" \
	"    float widest = ShadowParameters.w;\n" \
	"    float narrowest = ShadowSoftness.x;\n" \
	"\n" \
	"    // What is casting the shadow and how far above this pixel it is.  The search is the\n" \
	"    // widest the filter is allowed to be, because a blocker it does not find is a blocker\n" \
	"    // whose penumbra never opens.\n" \
	"    float blocker_depth = 0.0;\n" \
	"    float blockers = 0.0;\n" \
	"    for (int sy = -2; sy <= 2; ++sy) {\n" \
	"        for (int sx = -2; sx <= 2; ++sx) {\n" \
	"            float2 at = map + float2(sx, sy) * texel * widest * 0.5;\n" \
	"            float depth = ShadowMap.SampleLevel(ShadowSampler, at, 0).r;\n" \
	"            if (depth + bias < sun.z) { blocker_depth += depth; blockers += 1.0; }\n" \
	"        }\n" \
	"    }\n" \
	"    if (blockers < 0.5) return 1.0;\n" \
	"    blocker_depth /= blockers;\n" \
	"\n" \
	"    // The gap between the caster and this pixel, in world units, is what opens the filter:\n" \
	"    // a track on the ground stays hard, a helicopter's shadow spreads.\n" \
	"    float gap = max(sun.z - blocker_depth, 0.0) * ShadowSoftness.z;\n" \
	"    float radius = clamp(narrowest + gap * ShadowSoftness.y, narrowest, widest);\n" \
	"\n" \
	"    // Five by five rather than three by three: opened up to nine texels, nine taps stand so\n" \
	"    // far apart that a body as narrow as a helicopter's falls between them and casts nothing.\n" \
	"    // The grid is turned by an angle, which trades the steps a fixed grid leaves across a wide\n" \
	"    // penumbra for noise the eye reads as a gradient.  The angle comes from where the pixel is\n" \
	"    // in the sun's map and not from where it is on the screen: on the screen it swims as soon\n" \
	"    // as the camera moves, and a shadow that stands still shimmers.\n" \
	"    float turn = frac(sin(dot(map * 4096.0, float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;\n" \
	"    float2 turn_cos_sin = float2(cos(turn), sin(turn));\n" \
	"    float blocked = 0.0;\n" \
	"    for (int y = -2; y <= 2; ++y) {\n" \
	"        for (int x = -2; x <= 2; ++x) {\n" \
	"            float2 step = float2(x, y) * 0.5;\n" \
	"            step = float2(step.x * turn_cos_sin.x - step.y * turn_cos_sin.y,\n" \
	"                          step.x * turn_cos_sin.y + step.y * turn_cos_sin.x);\n" \
	"            float2 at = map + step * texel * radius;\n" \
	"            float depth = ShadowMap.SampleLevel(ShadowSampler, at, 0).r;\n" \
	"            blocked += (depth + bias < sun.z) ? 1.0 : 0.0;\n" \
	"        }\n" \
	"    }\n" \
	"\n" \
	"    // The sky is the other light in the scene and it fills a shadow back in the further its\n" \
	"    // caster is, which is why a shadow from high up reads pale as well as soft.\n" \
	"    float openness = saturate((radius - narrowest) / max(widest - narrowest, 1e-3));\n" \
	"    float strength = ShadowParameters.z * (1.0 - ShadowSoftness.w * openness);\n" \
	"    return 1.0 - strength * (blocked / 25.0);\n" \
	"}\n" \
	"\n"

// Putting the shadow on the pixel, which is not a plain multiply.  A pixel that is already dark is
// dark for a reason - it is under the shroud, or it is the fogged snapshot of a building somebody
// cannot see - and multiplying that again takes it to black: from a camera far enough out, a
// building in the fog turned into a black slab.  The shadow therefore only reaches a pixel as far
// as the pixel is lit, which leaves the sunlit ground exactly as it was.
//
// The threshold has to sit low.  A wall is a darker surface than the desert it stands on, and a
// gentler one took a good share of the shadow off every building while the ground beside it took
// all of it, which reads as the two being lit by different suns.  Full shadow from about a sixth
// of white upward; only what is darker than that is protected.
#define SHADOW_LIT \
	"    float shadow_lit = saturate(dot(current.rgb, float3(0.3333, 0.3333, 0.3333)) * 6.0);\n"
#define SHADOW_APPLY SHADOW_LIT \
	"    current.rgb *= lerp(1.0, sun_reaching(input.Position), shadow_lit);\n"

// The smoke in the sun's light, for the Direct3D 11 programs only: written after SHADOW_SAMPLING,
// whose map and matrix it reads, and kept out of the SDL3 GPU text, which declares neither the
// field it adds to the constant block nor a texture at t6.
//
// The smoke has a map of its own over the same sun (DX11BackendClass::Fill_Smoke_Map).  Each texel
// holds three sums over the particles the sun sees through it: their optical depth, that times
// their depth, and that times their depth squared plus their own thickness squared; its alpha holds
// the depth of the one nearest the sun, which the particles' own shade is measured from.  Taken
// together that is the smoke along the ray as one bell curve, with an amount, a centre and a
// width, and how much of it lies between the sun and a pixel is the curve's integral up to the
// pixel's depth.  A pixel under a plume gets all of it, a particle on the plume's near side gets
// a little and one on its far side most, which is the self-shading.  Bilinear filtering is right
// for this map where it is wrong for a depth map: the sums of a mixture are the mixture of the
// sums.
//
// VolumeParameters.x is how dark a pixel behind the thickest smoke goes, zero on a frame with no
// smoke in the map, and .y the same for a particle shading itself, with .w the power that keeps its
// plume's sun side lit.  .z is one for a draw whose vertices are in camera space already, which is the
// particles: they take the sun's map through four wide taps, because a smoke sprite drawn twenty
// deep would otherwise pay the fifty taps the ground pays, twenty times over.
#define VOLUMETRIC_SAMPLING \
	"Texture2D SmokeMap : register(t6);\n" \
	"SamplerState SmokeSampler : register(s6);\n" \
	"\n" \
	"bool sun_point(float4 position, out float3 sun)\n" \
	"{\n" \
	"    float2 ndc = float2(position.x * ShadowViewport.x * 2.0 - 1.0,\n" \
	"                        1.0 - position.y * ShadowViewport.y * 2.0);\n" \
	"    float4 at = mul(float4(ndc, position.z, 1.0), ShadowFromClip);\n" \
	"    sun = float3(0.0, 0.0, 0.0);\n" \
	"    if (at.w <= 0.0) return false;\n" \
	"    at /= at.w;\n" \
	"    sun = float3(0.5 * at.x + 0.5, 0.5 - 0.5 * at.y, at.z);\n" \
	"    return sun.x >= 0.0 && sun.x <= 1.0 && sun.y >= 0.0 && sun.y <= 1.0;\n" \
	"}\n" \
	"\n" \
	"float smoke_reaching(float4 position)\n" \
	"{\n" \
	"    float3 sun;\n" \
	"    bool particle = VolumeParameters.z > 0.5;\n" \
	"    float gain = particle ? VolumeParameters.y : VolumeParameters.x;\n" \
	"    if (gain <= 0.0 || !sun_point(position, sun)) return 1.0;\n" \
	"    float4 sums = SmokeMap.SampleLevel(SmokeSampler, sun.xy, 0);\n" \
	"    if (sums.x < 0.001) return 1.0;\n" \
	"    float centre = sums.y / sums.x;\n" \
	"    float width = sqrt(max(sums.z / sums.x - centre * centre, 1e-10));\n" \
	"    float behind = (sun.z - centre) / width;\n" \
	"    float ahead;\n" \
	"    if (particle) {\n" \
	"        // A particle inside its own plume, measured from the plume's front: the particle\n" \
	"        // nearest the sun in this texel (the map's alpha) has none of it ahead, and the share\n" \
	"        // reaches all of it as far behind the centre as the front is ahead of it.  Raised to a\n" \
	"        // power the sun side stays at nothing and the back darkens.  The bell curve's tails, and\n" \
	"        // a ramp from a width in front of the centre, both darkened the sun side as well.  A\n" \
	"        // front that bilinear filtering pulled back past the centre, at the plume's edge in the\n" \
	"        // map, falls back to a width in front of it.\n" \
	"        float front = (sums.w < centre) ? sums.w : centre - width;\n" \
	"        float span = max(2.0 * (centre - front), 1e-6);\n" \
	"        ahead = pow(saturate((sun.z - front) / span), VolumeParameters.w);\n" \
	"    }\n" \
	"    else {\n" \
	"        // the bell curve's integral up to here, the logistic stand-in for the normal distribution\n" \
	"        ahead = 1.0 / (1.0 + exp(-1.702 * behind));\n" \
	"    }\n" \
	"    return 1.0 - gain * (1.0 - exp(-sums.x * ahead));\n" \
	"}\n" \
	"\n" \
	"float sun_reaching_coarse(float4 position)\n" \
	"{\n" \
	"    float3 sun;\n" \
	"    if (ShadowParameters.z <= 0.0 || !sun_point(position, sun)) return 1.0;\n" \
	"    if (sun.z < 0.0 || sun.z > 1.0) return 1.0;\n" \
	"    float reach = ShadowParameters.x * ShadowParameters.w * 0.5;\n" \
	"    float bias = ShadowParameters.y * 2.0;\n" \
	"    float blocked = 0.0;\n" \
	"    blocked += (ShadowMap.SampleLevel(ShadowSampler, sun.xy + float2(-reach, -reach), 0).r + bias < sun.z) ? 1.0 : 0.0;\n" \
	"    blocked += (ShadowMap.SampleLevel(ShadowSampler, sun.xy + float2( reach, -reach), 0).r + bias < sun.z) ? 1.0 : 0.0;\n" \
	"    blocked += (ShadowMap.SampleLevel(ShadowSampler, sun.xy + float2(-reach,  reach), 0).r + bias < sun.z) ? 1.0 : 0.0;\n" \
	"    blocked += (ShadowMap.SampleLevel(ShadowSampler, sun.xy + float2( reach,  reach), 0).r + bias < sun.z) ? 1.0 : 0.0;\n" \
	"    return 1.0 - ShadowParameters.z * (1.0 - ShadowSoftness.w) * (blocked * 0.25);\n" \
	"}\n" \
	"\n" \
	"float light_reaching(float4 position)\n" \
	"{\n" \
	"    float sun = 1.0;\n" \
	"    if (VolumeParameters.z > 0.5) sun = sun_reaching_coarse(position);\n" \
	"    else sun = sun_reaching(position);\n" \
	"    return sun * smoke_reaching(position);\n" \
	"}\n" \
	"\n"

// The constant block's field for it, declared after SkyUp: DX11BackendClass::PixelConstantBlock.
#define VOLUMETRIC_CONSTANTS \
	"    float4 VolumeParameters;\n"

// SHADOW_APPLY with the smoke in it.  A particle takes the shade whatever its own brightness: the
// threshold is there for ground under the shroud, and on smoke it tied the shade to the colour, so
// the fire's glow, lifting a dark plume over the threshold, made it take more shade and go darker.
#define VOLUMETRIC_SHADOW_APPLY SHADOW_LIT \
	"    if (VolumeParameters.z > 0.5) shadow_lit = 1.0;\n" \
	"    current.rgb *= lerp(1.0, light_reaching(input.Position), shadow_lit);\n"

// The HLSL for one description, or false when the description names an operation or an argument
// this does not generate.  A refusal is not a failure: the caller keeps the fixed-function path for
// that draw, which is the only reason an unmeasured operation is safe to meet at run time.
// Which profile the generated text is for.  ps_2_0 and ps_4_0 are not the same language: a sampler
// is a sampler2D read with tex2D in one and a Texture2D beside a SamplerState read with Sample in
// the other, the output semantic is COLOR against SV_Target, and the texture factor is a constant
// register against a constant buffer.  The arithmetic between them is the same text.
//
// SDL3_GPU is the D3D11 text with its bindings rewritten for SDL3's GPU API (sdl3target.h): what the
// Metal and Vulkan backend compiles through glslang and SPIRV-Cross (decision 4).
enum CombinerShaderTarget
{
	COMBINER_SHADER_TARGET_D3D9,
	COMBINER_SHADER_TARGET_D3D11,
	COMBINER_SHADER_TARGET_SDL3_GPU
};

bool CombinerShader_Generate(const CombinerDescription & description, CombinerShaderTarget target,
	std::string & hlsl);

// The description two draws share iff they can share a compiled shader.  Stages past StageCount are
// zeroed, so two descriptions that differ only in a stage nobody reads compare equal.
std::string CombinerShader_Key(const CombinerDescription & description);

// The alpha test and the fog written into the program, applied to a float4 named current in the
// order the D3D9 pipeline applies them.  Public because a hand-written pixel program has to apply
// them too: D3D9 does both around a bound pixel shader and D3D11 does neither.  False when the
// comparison function is not one this writes.
bool CombinerShader_Append_Pixel_Pipeline(const PixelPipelineDescription & pipeline,
	std::string & hlsl);

// The same two, as the part of a key they account for.  Two draws differing in either are two
// programs on the D3D11 profile whatever else they share.
std::string CombinerShader_Pipeline_Key(const PixelPipelineDescription & pipeline);

#endif
