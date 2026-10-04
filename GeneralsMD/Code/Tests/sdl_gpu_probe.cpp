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
 * What SDL3's GPU API can do on this machine, for the game as it stands.
 *
 * Decision 3 in PORTING.md chose SDL3's GPU API for the renderer off Windows, with one
 * Vulkan backend plus MoltenVK as the fallback if D2 finds something the game needs that it cannot
 * express.  This is the evidence for that call, gathered before anyone designs the interface.
 *
 * What it asks is taken from what the game asks of Direct3D today, read out of the D3D11 backend
 * that already answers it (WW3D2/dx11resource.cpp's format map, dx11device.cpp's depth format,
 * dx11backend.h's stage count, dx11state.cpp's samplers), not from a general wish list.
 *
 * Built on POSIX and deliberately not registered with ctest: it needs a GPU and a display, and a
 * machine without either cannot say anything about one that has them.  Run it by hand and record
 * the output with the machine and OS it came from.
 *
 * What it cannot see, because SDL cannot be asked:
 *   - Anything that lives in shader code rather than in the device: fog, alpha test, the texture
 *     stage combiners.  dx11backend generates those as shaders today, and D3 decides what an SDL3
 *     backend's generator emits.
 *   - Point size.  SDL_gpu.h requires a POINTLIST pipeline's vertex shader to write it
 *     ([[point_size]] in MSL); nothing to query.  The game never draws points (every D3DRS_POINT*
 *     reference in WW3D2 is commented out or a name table), so this is informational.
 *   - A per-stage sampler limit.  SDL_gpu.h documents none; the game uses 4 stages.
 *   - Whether a format *renders correctly*.  "Supported" is the device's claim, not a picture.
 */
#include <SDL3/SDL.h>
#include <stdio.h>

static const char *yes_no(bool b) { return b ? "yes" : "NO"; }

struct FormatRow
{
	SDL_GPUTextureFormat format;
	const char *name;
	const char *game_use;
};

static void print_formats(SDL_GPUDevice *device, const char *title, SDL_GPUTextureUsageFlags usage,
                          const FormatRow *rows, int count)
{
	printf("\n%s\n", title);
	for (int i = 0; i < count; ++i) {
		bool ok = SDL_GPUTextureSupportsFormat(device, rows[i].format, SDL_GPU_TEXTURETYPE_2D, usage);
		printf("  %-28s %-4s  %s\n", rows[i].name, yes_no(ok), rows[i].game_use);
	}
}

static void print_sample_counts(SDL_GPUDevice *device, SDL_GPUTextureFormat format, const char *name)
{
	printf("  %-28s 2x %-4s 4x %-4s 8x %s\n", name,
		yes_no(SDL_GPUTextureSupportsSampleCount(device, format, SDL_GPU_SAMPLECOUNT_2)),
		yes_no(SDL_GPUTextureSupportsSampleCount(device, format, SDL_GPU_SAMPLECOUNT_4)),
		yes_no(SDL_GPUTextureSupportsSampleCount(device, format, SDL_GPU_SAMPLECOUNT_8)));
}

int main(void)
{
	int v = SDL_GetVersion();
	printf("SDL %d.%d.%d (%s)\n", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
		SDL_GetRevision());

	printf("GPU drivers compiled in:");
	for (int i = 0; i < SDL_GetNumGPUDrivers(); ++i) printf(" %s", SDL_GetGPUDriver(i));
	printf("\n");

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("SDL_Init(VIDEO) failed: %s\n", SDL_GetError());
		return 2;
	}

	/* Every shader format this build could hand it: SDL picks the best driver that takes one. */
	SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
	SDL_GPUDevice *device = SDL_CreateGPUDevice(formats, false, NULL);
	if (device == NULL) {
		printf("SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 3;
	}

	SDL_PropertiesID props = SDL_GetGPUDeviceProperties(device);
	printf("\nDevice\n");
	printf("  backend                      %s\n", SDL_GetGPUDeviceDriver(device));
	printf("  name                         %s\n", SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "(not reported)"));
	printf("  driver name                  %s\n", SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "(not reported)"));
	printf("  driver version               %s\n", SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "(not reported)"));
	SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(device);
	printf("  shader formats               %s%s%s%s%s\n",
		(have & SDL_GPU_SHADERFORMAT_SPIRV) ? "SPIR-V " : "",
		(have & SDL_GPU_SHADERFORMAT_MSL) ? "MSL " : "",
		(have & SDL_GPU_SHADERFORMAT_METALLIB) ? "metallib " : "",
		(have & SDL_GPU_SHADERFORMAT_DXIL) ? "DXIL " : "",
		(have & SDL_GPU_SHADERFORMAT_DXBC) ? "DXBC " : "");
	/* Created with the defaults, which SDL_gpu.h documents as *requiring* these features; its
	   properties exist only to relax them.  So a device here has them. */
	printf("  clip distance, depth clamp,  yes (required by a default device; SDL_gpu.h)\n");
	printf("  anisotropy, indirect first\n");

	static const FormatRow sampled[] = {
		{ SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, "BC1 (DXT1)",          "the art: DDS" },
		{ SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM, "BC2 (DXT2, DXT3)",    "the art: DDS" },
		{ SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM, "BC3 (DXT4, DXT5)",    "the art: DDS" },
		{ SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, "B8G8R8A8 (A8R8G8B8)", "32-bit textures, TGA" },
		{ SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM,   "B5G6R5 (R5G6B5)",     "16-bit textures" },
		{ SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM, "B5G5R5A1 (A1R5G5B5)", "16-bit textures" },
		{ SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM, "B4G4R4A4 (A4R4G4B4)", "16-bit textures" },
		{ SDL_GPU_TEXTUREFORMAT_A8_UNORM,       "A8",                  "alpha-only" },
		{ SDL_GPU_TEXTUREFORMAT_R8_UNORM,       "R8 (L8)",             "luminance" },
	};
	print_formats(device, "Sampled textures (every format dx11resource.cpp maps)", SDL_GPU_TEXTUREUSAGE_SAMPLER,
		sampled, (int)(sizeof(sampled) / sizeof(sampled[0])));

	static const FormatRow targets[] = {
		{ SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, "B8G8R8A8", "render targets" },
		{ SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, "R8G8B8A8", "" },
		{ SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM,   "B5G6R5",   "16-bit render targets" },
		{ SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM, "B5G5R5A1", "" },
		{ SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM, "B4G4R4A4", "" },
	};
	print_formats(device, "Colour targets", SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
		targets, (int)(sizeof(targets) / sizeof(targets[0])));

	static const FormatRow depth[] = {
		{ SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, "D24S8",   "what the game asks for; shadow volumes use the stencil" },
		{ SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, "D32S8",   "the stencil-bearing alternative" },
		{ SDL_GPU_TEXTUREFORMAT_D16_UNORM,         "D16",     "D3DFMT_D16" },
		{ SDL_GPU_TEXTUREFORMAT_D24_UNORM,         "D24",     "" },
		{ SDL_GPU_TEXTUREFORMAT_D32_FLOAT,         "D32F",    "D3DFMT_D32" },
	};
	print_formats(device, "Depth-stencil targets", SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
		depth, (int)(sizeof(depth) / sizeof(depth[0])));

	printf("\nMultisampling\n");
	print_sample_counts(device, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, "B8G8R8A8");
	print_sample_counts(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, "D24S8");
	print_sample_counts(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, "D32S8");

	/* The swapchain needs a window.  Hidden, so running this does not flash one on screen. */
	printf("\nSwapchain\n");
	SDL_Window *window = SDL_CreateWindow("sdl_gpu_probe", 640, 480, SDL_WINDOW_HIDDEN);
	if (window == NULL || !SDL_ClaimWindowForGPUDevice(device, window)) {
		printf("  could not claim a window: %s\n", SDL_GetError());
	} else {
		SDL_GPUTextureFormat sc = SDL_GetGPUSwapchainTextureFormat(device, window);
		printf("  swapchain format             %s\n",
			sc == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ? "B8G8R8A8_UNORM" :
			sc == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ? "R8G8B8A8_UNORM" : "other");
		printf("  present vsync / immediate / mailbox   %s / %s / %s\n",
			yes_no(SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_VSYNC)),
			yes_no(SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE)),
			yes_no(SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX)));
		printf("  composition SDR / SDR linear          %s / %s\n",
			yes_no(SDL_WindowSupportsGPUSwapchainComposition(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR)),
			yes_no(SDL_WindowSupportsGPUSwapchainComposition(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR)));
		SDL_ReleaseWindowFromGPUDevice(device, window);
	}
	if (window) SDL_DestroyWindow(window);

	printf("\nNot queryable (see the header of this file)\n");
	printf("  point list                   in the API; the vertex shader must write point size. The game draws no points.\n");
	printf("  texture stages               the game uses 4 (dx11backend.h); SDL_gpu.h documents no per-stage sampler limit\n");
	printf("  fog, alpha test, combiners   shader code, generated as dx11backend does today (D3)\n");

	SDL_DestroyGPUDevice(device);
	SDL_Quit();
	return 0;
}
