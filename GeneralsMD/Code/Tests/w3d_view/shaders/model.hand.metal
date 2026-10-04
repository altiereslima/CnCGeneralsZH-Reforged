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
// w3d_view's shaders written by hand in MSL: the other side of the shader-toolchain comparison.
// They compute what model.vert and model.frag compute.  SDL_gpu.h's order for MSL: textures and
// samplers from index 0, uniform buffers from [[buffer(0)]], vertex attributes through [[stage_in]]
// (SDL binds vertex buffer 0 at [[buffer(14)]] and describes it to Metal from the pipeline).
#include <metal_stdlib>
using namespace metal;

struct VertexUniforms
{
	float4x4 viewProjection;
};

struct FragmentUniforms
{
	float4 materialDiffuse;
	float4 tint;
	float4 ambient;
	float4 lightDirection;
	float4 lightColor;	// a is the alpha test reference, 0 for none
};

struct VertexIn
{
	float3 position [[attribute(0)]];
	float3 normal [[attribute(1)]];
	float2 texCoord [[attribute(2)]];
};

struct VertexOut
{
	float4 position [[position]];
	float3 normal [[user(locn0)]];
	float2 texCoord [[user(locn1)]];
};

vertex VertexOut vertexMain(VertexIn in [[stage_in]], constant VertexUniforms &uniforms [[buffer(0)]])
{
	VertexOut out;
	out.position = uniforms.viewProjection * float4(in.position, 1.0);
	out.normal = in.normal;
	out.texCoord = in.texCoord;
	return out;
}

fragment float4 fragmentMain(VertexOut in [[stage_in]], constant FragmentUniforms &uniforms [[buffer(0)]],
	texture2d<float> diffuseTexture [[texture(0)]], sampler diffuseSampler [[sampler(0)]])
{
	float4 texel = diffuseTexture.sample(diffuseSampler, in.texCoord);
	float facing = max(dot(normalize(in.normal), -uniforms.lightDirection.xyz), 0.0);
	float3 light = uniforms.ambient.rgb + uniforms.lightColor.rgb * facing;
	float4 color = float4(texel.rgb * uniforms.tint.rgb * uniforms.materialDiffuse.rgb * light,
		texel.a * uniforms.materialDiffuse.a);
	float alphaReference = uniforms.lightColor.a;
	if (alphaReference > 0.0 && color.a < alphaReference) {
		discard_fragment();
	}
	return color;
}
