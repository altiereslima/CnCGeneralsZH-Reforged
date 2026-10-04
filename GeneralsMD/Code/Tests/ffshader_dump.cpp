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
 * ffshader_dump - writes every program in shader_cases.h, for every target, to a folder, with an
 * index.  Two dumps are compared with diff -r.
 *
 * It exists for one proof: that adding a target to the generators left the D3D9 and D3D11 text
 * byte-identical.  keys.txt beside it holds each case's pipeline-cache key (CombinerShader_Key,
 * VertexShader_Key, or the engine program's name and pipeline key), because the key is the other
 * thing the generators hand the backend.  The same file builds against the generators before and after the change, with
 * mingw-w64 for Windows (run under Wine) and natively off Windows, so the comparison covers both the
 * text and the platforms.  (D3.)
 *
 *   ffshader_dump <folder> [d3d9|d3d11|sdl3 ...]       default: every target the build has
 */
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "shader_cases.h"

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: ffshader_dump <folder> [d3d9|d3d11|sdl3 ...]\n");
		return 2;
	}
	const std::string folder = argv[1];
	std::vector<ShaderCaseTarget> targets;
	for (int i = 2; i < argc; ++i) {
		if (strcmp(argv[i], "d3d9") == 0) targets.push_back(SHADER_CASE_D3D9);
		else if (strcmp(argv[i], "d3d11") == 0) targets.push_back(SHADER_CASE_D3D11);
		else if (strcmp(argv[i], "sdl3") == 0) targets.push_back(SHADER_CASE_SDL3_GPU);
		else { fprintf(stderr, "unknown target %s\n", argv[i]); return 2; }
	}
	if (targets.empty()) {
		targets.push_back(SHADER_CASE_D3D9);
		targets.push_back(SHADER_CASE_D3D11);
#if !defined(SHADER_CASES_BEFORE_SDL3_TARGET)
		targets.push_back(SHADER_CASE_SDL3_GPU);
#endif
	}

	const std::vector<ShaderCase> cases = Shader_Cases();
	std::string index, keys;
	unsigned written = 0, refused = 0, reference = 0;
	for (size_t i = 0; i < cases.size(); ++i) {
		reference += cases[i].Reference ? 1 : 0;
		const ShaderCase & c = cases[i];
		const std::string key = c.Kind == SHADER_CASE_COMBINER ? CombinerShader_Key(c.Combiner)
			: c.Kind == SHADER_CASE_VERTEX ? VertexShader_Key(c.Vertex)
			: std::string(EngineShader_Name(c.Engine)) + (c.VertexStage ? "" : CombinerShader_Pipeline_Key(c.EnginePipeline));
		keys += c.Name + " " + key + "\n";
		for (size_t t = 0; t < targets.size(); ++t) {
			std::string hlsl;
			const bool generated = Shader_Case_Generate(cases[i], targets[t], hlsl);
			const std::string name = cases[i].Name + "." + Shader_Case_Target_Name(targets[t]) + ".hlsl";
			index += name + (generated ? " generated" : " refused") + (cases[i].Reference ? " reference\n" : " extra\n");
			if (!generated) {
				++refused;
				continue;
			}
			FILE *file = fopen((folder + "/" + name).c_str(), "wb");
			if (file == NULL || fwrite(hlsl.data(), 1, hlsl.size(), file) != hlsl.size()) {
				fprintf(stderr, "could not write %s\n", name.c_str());
				return 1;
			}
			fclose(file);
			++written;
		}
	}
	FILE *file = fopen((folder + "/index.txt").c_str(), "wb");
	if (file == NULL) return 1;
	fwrite(index.data(), 1, index.size(), file);
	fclose(file);
	file = fopen((folder + "/keys.txt").c_str(), "wb");
	if (file == NULL) return 1;
	fwrite(keys.data(), 1, keys.size(), file);
	fclose(file);
	printf("%zu cases (%u in the reference set), %zu targets: %u written, %u refused or not applicable\n",
		cases.size(), reference, targets.size(), written, refused);
	return 0;
}
