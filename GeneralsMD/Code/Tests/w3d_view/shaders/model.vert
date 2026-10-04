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
// w3d_view's vertex shader: the one source for both Vulkan (SPIR-V, compiled by glslang) and Metal
// (MSL, translated from that SPIR-V).  Resource sets follow SDL_gpu.h's rule for SPIR-V vertex
// shaders: set 1 holds uniform buffers.  Vertices arrive already in model space, Z up.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;

layout(set = 1, binding = 0) uniform VertexUniforms
{
	mat4 viewProjection;
} vertexUniforms;

void main()
{
	gl_Position = vertexUniforms.viewProjection * vec4(inPosition, 1.0);
	outNormal = inNormal;
	outTexCoord = inTexCoord;
}
