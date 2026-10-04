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
// w3d_view's pixel shader in HLSL, the language the game's own generators emit (ffshader.cpp,
// ffvertex.cpp, engineshader.h).  The same computation as model.vert / model.frag.  It exists to
// draw a real frame through the route D3 is being recommended, not only to compile through it:
// HLSL --glslang--> SPIR-V (Vulkan) --SPIRV-Cross--> MSL (Metal).  One file per stage, as the
// generators emit them.
//
// Registers carry SDL_gpu.h's spaces: vertex uniforms in space1; fragment textures and samplers in
// space2 and fragment uniforms in space3.  That is the one change the generators' output needs.
// Texture and sampler are declared apart, as the generators declare them.

struct VertexOutput
{
	float4 position : SV_Position;
	[[vk::location(0)]] float3 normal : TEXCOORD0;
	[[vk::location(1)]] float2 texCoord : TEXCOORD1;
};

Texture2D diffuseTexture : register(t0, space2);
SamplerState diffuseSampler : register(s0, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
	float4 materialDiffuse;
	float4 tint;
	float4 ambient;
	float4 lightDirection;
	float4 lightColor;	// a is the alpha test reference, 0 for none
};

float4 psMain(VertexOutput input) : SV_Target
{
	float4 texel = diffuseTexture.Sample(diffuseSampler, input.texCoord);
	float facing = max(dot(normalize(input.normal), -lightDirection.xyz), 0.0);
	float3 light = ambient.rgb + lightColor.rgb * facing;
	float4 color = float4(texel.rgb * tint.rgb * materialDiffuse.rgb * light, texel.a * materialDiffuse.a);
	float alphaReference = lightColor.a;
	if (alphaReference > 0.0 && color.a < alphaReference) {
		discard;
	}
	return color;
}
