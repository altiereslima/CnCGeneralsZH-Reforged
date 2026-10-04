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

// The engine's shaders by name, off Windows (A3e).  W3DShaderManager and the water register every shader
// they make with Direct3D11_Register_Engine_Shader, under the file it was loaded from ("shaders\\Trees.vso")
// or the name the water gives its run-time programs ("river water ps.1.1").  On Windows the Direct3D 11
// backend maps that name to D3's HLSL transcription (engineshader.cpp).  Off Windows the POSIX device does
// the same, because it never runs D3D bytecode: dx11runtime_posix.cpp passes the registration on here, and
// the draw asks the shader it has bound for its name.
//
// Implemented by the POSIX device (posixd3d9).  A shader the device did not make is ignored.

#pragma once

#ifndef ENGINESHADERNAME_H
#define ENGINESHADERNAME_H

#if !defined(_WIN32)
void PosixDevice_Name_Shader(const void *shader, const char *name);
/// The engine's own D3D8 declaration a vertex declaration of the device's was decoded from
/// (d3d8shadertranslate), through its D3DVSD_END: kept on the declaration for capture version 3, so
/// the tests' shader interpreter can read the vN mapping from the tokens themselves.
void PosixDevice_Keep_D3D8_Declaration(void *declaration, const unsigned int *d3d8_tokens);
#endif

#endif // ENGINESHADERNAME_H
