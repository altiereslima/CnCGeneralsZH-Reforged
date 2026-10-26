// Independent sun-depth allocation and diagnostic readback. No scene attachments are sampled.
#include "SdlGpuFrame.h"
#include "SdlSunShadowDebug.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>

bool SdlGpuFrame::Sun_Shadow_Target(unsigned int size, SdlTarget &target, std::string &error)
{
	target = {};
	const SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	if (size == 0 || size > 8192 || !SDL_GPUTextureSupportsFormat(GpuDevice, format, SDL_GPU_TEXTURETYPE_2D, usage)) {
		error = "unsupported sampleable sun depth target or size";
		return false;
	}
	if (!SunShadowTexture || SunShadowSize != size) {
		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = format;
		info.usage = usage;
		info.width = info.height = size;
		info.layer_count_or_depth = info.num_levels = 1;
		info.sample_count = SDL_GPU_SAMPLECOUNT_1;
		SDL_GPUTexture *replacement = nullptr;
		if (Sdl_Sun_Shadow_Fault("target")) SDL_SetError("injected sun depth allocation failure");
		else replacement = SDL_CreateGPUTexture(GpuDevice, &info);
		if (!replacement) { error = SDL_GetError(); return false; }
		if (SunShadowTexture) Release_After_Batch(SunShadowTexture, NULL);
		SunShadowTexture = replacement;
		SunShadowSize = size;
	}
	target = { NULL, SunShadowTexture, size, size, (uint32_t)format };
	error.clear();
	return true;
}

void SdlGpuFrame::Release_Sun_Shadow()
{
    if (SunShadowTexture) SDL_ReleaseGPUTexture(GpuDevice, SunShadowTexture);
    SunShadowTexture = NULL;
    SunShadowSize = 0;
}

// The depth-to-R32 pass the scene snapshots use avoids backend-specific raw depth download restrictions.
bool SdlGpuFrame::Read_Depth(SDL_GPUTexture *texture, unsigned width, unsigned height,
    std::vector<float> &depth)
{
    depth.clear();
    if (!texture || !width || !height || !Flush() || !Create_Scene_Depth_Pipeline()) return false;
    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = info.num_levels = 1;
    SDL_GPUTexture *resolved = SDL_CreateGPUTexture(GpuDevice, &info);
    if (!resolved) return false;

    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
    bool submitted = false;
    if (commands) {
        if (Resolve_Scene_Depth(commands, texture, resolved, width, height)) {
            // Previously queued readers were flushed above; this ordered submission owns
            // the resolve, and the existing download waits for it before mapping bytes.
            submitted = SerializeSubmits ? Submit(commands) : Submit_Flush(commands);
        } else SDL_CancelGPUCommandBuffer(commands);
    }
    std::vector<uint8_t> bytes;
    const bool read = submitted && Read_Back(resolved, width, height, bytes);
    const size_t expected = size_t(width) * height * sizeof(float);
    const bool ok = read && bytes.size() == expected;
    if (ok) {
        depth.resize(size_t(width) * height);
        std::memcpy(depth.data(), bytes.data(), expected);
    }
    Release_After_Batch(resolved, NULL);
    return ok;
}
