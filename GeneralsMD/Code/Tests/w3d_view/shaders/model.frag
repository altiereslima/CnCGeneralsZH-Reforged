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
#version 450
// w3d_view's fragment shader: one texture stage modulated by the vertex material's diffuse and a
// single directional light, which is the shape of the game's common fixed-function unit shader,
// not a copy of it (D3 owns that).  Set 2 holds sampled textures and set 3 uniform buffers, as
// SDL_gpu.h requires of SPIR-V fragment shaders.

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D diffuseTexture;

layout(set = 3, binding = 0) uniform FragmentUniforms
{
	vec4 materialDiffuse;	// rgb diffuse, a opacity
	vec4 tint;				// house colour for HOUSECOLOR meshes, white otherwise
	vec4 ambient;			// rgb
	vec4 lightDirection;	// xyz, towards the scene
	vec4 lightColor;		// rgb; a is the alpha test reference, 0 for none
} fragmentUniforms;

void main()
{
	vec4 texel = texture(diffuseTexture, inTexCoord);
	float facing = max(dot(normalize(inNormal), -fragmentUniforms.lightDirection.xyz), 0.0);
	vec3 light = fragmentUniforms.ambient.rgb + fragmentUniforms.lightColor.rgb * facing;
	vec4 color = vec4(texel.rgb * fragmentUniforms.tint.rgb * fragmentUniforms.materialDiffuse.rgb * light,
		texel.a * fragmentUniforms.materialDiffuse.a);
	float alphaReference = fragmentUniforms.lightColor.a;
	if (alphaReference > 0.0 && color.a < alphaReference) {
		discard;
	}
	outColor = color;
}
