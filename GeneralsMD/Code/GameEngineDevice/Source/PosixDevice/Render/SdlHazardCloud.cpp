// Shared world-space hazard clouds; game and isolated preview use one renderer.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PreRTS.h"
#include "Platform/HazardCloudCore.h"
#include "SdlHazardCloudEmbers.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/sdl3shadercompile.h"
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace SdlHazardCloud {
namespace {
using namespace HazardCloud;

const SDL_GPUTextureFormat LowTargetFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
const SDL_GPUTextureUsageFlags LowTargetUsage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
const SDL_GPUTextureFormat DensityTargetFormat = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
struct FieldCache {
    SdlTarget density = {};
    SdlTarget lighting = {};
    unsigned side = 0;
};
// Kept from the first layer until HazardCloud::ReleaseDevice, so a new map's
// first hazard compiles nothing.
struct SharedResources {
    SdlGpuFrame *frame = nullptr;
    SDL_GPUDevice *gpu = nullptr;
    unsigned users = 0;
    SDL_GPUShader *vertexShader = nullptr;
    SDL_GPUShader *pixelShader = nullptr;
    SDL_GPUShader *compositePixelShader = nullptr;
    SDL_GPUShader *heatPixelShader = nullptr;
    SDL_GPUShader *densityPixelShader = nullptr;
    SDL_GPUShader *lightingPixelShader = nullptr;
    SDL_GPUGraphicsPipeline *volumePipeline = nullptr;
    SDL_GPUGraphicsPipeline *compositePipeline = nullptr;
    SDL_GPUGraphicsPipeline *heatPipeline = nullptr;
    SDL_GPUGraphicsPipeline *densityPipeline = nullptr;
    SDL_GPUGraphicsPipeline *lightingPipeline = nullptr;
    SDL_GPUSampler *depthSampler = nullptr;
    SDL_GPUSampler *flowSampler = nullptr;
    SDL_GPUSampler *cacheSampler = nullptr;
    SdlTarget lowTarget = {};
    SdlTarget heatBackground = {};
};
SharedResources shared;

void releaseShared(bool deviceAlive)
{
    if (deviceAlive && shared.frame && shared.gpu) {
        if (shared.lowTarget.Colour) shared.frame->Release_After_Batch(shared.lowTarget.Colour, nullptr);
        if (shared.heatBackground.Colour) shared.frame->Release_After_Batch(shared.heatBackground.Colour, nullptr);
        // Pipelines cannot retire through Release_After_Batch.
        shared.frame->Flush();
        SDL_WaitForGPUIdle(shared.gpu);
        SdlHazardCloudEmbers::shutdown();
        if (shared.vertexShader) SDL_ReleaseGPUShader(shared.gpu, shared.vertexShader);
        if (shared.pixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.pixelShader);
        if (shared.compositePixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.compositePixelShader);
        if (shared.heatPixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.heatPixelShader);
        if (shared.densityPixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.densityPixelShader);
        if (shared.lightingPixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.lightingPixelShader);
        if (shared.volumePipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.volumePipeline);
        if (shared.compositePipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.compositePipeline);
        if (shared.heatPipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.heatPipeline);
        if (shared.densityPipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.densityPipeline);
        if (shared.lightingPipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.lightingPipeline);
        if (shared.depthSampler) SDL_ReleaseGPUSampler(shared.gpu, shared.depthSampler);
        if (shared.flowSampler) SDL_ReleaseGPUSampler(shared.gpu, shared.flowSampler);
        if (shared.cacheSampler) SDL_ReleaseGPUSampler(shared.gpu, shared.cacheSampler);
    } else {
        // The device owns native allocations; after its destruction only forget
        // our stale handles, never call through a dangling GPU/frame pointer.
        SdlHazardCloudEmbers::abandon();
    }
    shared = {};
}

bool sharedDeviceAlive()
{
    PosixDevice9 *device = dynamic_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    return device && shared.frame && device->Get_Gpu() == shared.frame;
}

class Layer final : public LayerCore {
public:
    ~Layer() override { shutdown(); }
    bool initialize() override;
    bool render(const Camera &camera) override;
    std::uint64_t estimatedCacheBytes() const override;
    void shutdown() override;

private:
    bool makeResources();
    bool makeFlowTexture();
    bool resizeLowTarget(unsigned width, unsigned height);
    bool prepareHeatBackground(const SdlTarget &target);
    void captureHeatBackground(const SdlTarget &target);
    void retireCache(FieldCache &cache);
    bool resizeFieldCache(unsigned index);
    void bindCachedTextures(SdlRecordedDraw &draw, bool density);
    bool prepareCaches(Uniforms &uniforms, SDL_GPUTexture *sceneDepth,
                       uint32_t vertexOffset, const SdlTarget &restoreTarget);
    bool uploadTerrain();
    void bindTerrain(SdlRecordedDraw &draw);
    bool uploadFlow();
    bool resolveSceneTarget(PosixDevice9 *device, SdlTarget &target);

    SdlGpuFrame *frame = nullptr;
    SDL_GPUDevice *gpu = nullptr;
    SDL_GPUTexture *flowTexture = nullptr;
    SDL_GPUTexture *terrainTexture = nullptr;
    uint64_t flowUsedBatch = ~uint64_t(0);
    FieldCache fieldCaches[2];
};

bool Layer::makeResources()
{
    std::vector<unsigned char> spirv;
    std::string log;
    if (!SDL3_Compile_HLSL_To_SPIRV(vertexShader(), true, spirv, log)
        || !(shared.vertexShader = SDL3_Create_Shader(gpu, spirv, true, log))) {
        fail(("vertex shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(pixelShader(Pass::Volume, true), false, spirv, log)
        || !(shared.pixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(pixelShader(Pass::Composite, true), false, spirv, log)
        || !(shared.compositePixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume composite shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(pixelShader(Pass::Heat, true), false, spirv, log)
        || !(shared.heatPixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume heat shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(pixelShader(Pass::Density, true), false, spirv, log)
        || !(shared.densityPixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume density cache shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(pixelShader(Pass::Lighting, true), false, spirv, log)
        || !(shared.lightingPixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume lighting cache shader: " + log).c_str());
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(gpu, LowTargetFormat, SDL_GPU_TEXTURETYPE_2D, LowTargetUsage)) {
        fail("RGBA16F cloud rendering and texture sampling are unavailable");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(gpu, DensityTargetFormat, SDL_GPU_TEXTURETYPE_2D, LowTargetUsage)) {
        fail("R16F cloud density rendering and texture sampling are unavailable");
        return false;
    }
    SDL_GPUColorTargetDescription target = {};
    target.format = LowTargetFormat;
    SDL_GPUGraphicsPipelineCreateInfo description = {};
    description.vertex_shader = shared.vertexShader;
    description.fragment_shader = shared.pixelShader;
    description.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    description.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    description.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    // The volume clips its ray against the sampled opaque scene depth. Its
    // fullscreen proxy neither tests nor modifies the real depth attachment.
    description.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    description.depth_stencil_state.enable_depth_test = false;
    description.depth_stencil_state.enable_depth_write = false;
    description.target_info.color_target_descriptions = &target;
    description.target_info.num_color_targets = 1;
    // The low pass owns only a colour target. Its shader reads the captured
    // full-resolution depth and writes premultiplied radiance without blending.
    shared.volumePipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.volumePipeline) { fail(SDL_GetError()); return false; }
    target.format = static_cast<SDL_GPUTextureFormat>(SdlGpuFrame::Target_Format());
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    description.fragment_shader = shared.compositePixelShader;
    description.target_info.has_depth_stencil_target = true;
    description.target_info.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(frame->Depth_Format());
    shared.compositePipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.compositePipeline) { fail(SDL_GetError()); return false; }
    description.fragment_shader = shared.heatPixelShader;
    shared.heatPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.heatPipeline) { fail(SDL_GetError()); return false; }
    target = {};
    target.format = DensityTargetFormat;
    description.fragment_shader = shared.densityPixelShader;
    description.target_info.has_depth_stencil_target = false;
    description.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    shared.densityPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.densityPipeline) { fail(SDL_GetError()); return false; }
    target.format = LowTargetFormat;
    description.fragment_shader = shared.lightingPixelShader;
    shared.lightingPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.lightingPipeline) { fail(SDL_GetError()); return false; }
    SDL_GPUSamplerCreateInfo sampler = {};
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w
        = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shared.depthSampler = SDL_CreateGPUSampler(gpu, &sampler);
    // Float32 linear filtering is optional on some backends. The volume shader
    // performs bilinear interpolation explicitly using this point sampler.
    shared.flowSampler = SDL_CreateGPUSampler(gpu, &sampler);
    // The atlas shader clamps XY to individual slice texel centres before the
    // bilinear lookup; it interpolates Z explicitly between adjacent slices.
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    shared.cacheSampler = SDL_CreateGPUSampler(gpu, &sampler);
    if (!shared.depthSampler || !shared.flowSampler || !shared.cacheSampler) {
        fail(SDL_GetError());
        return false;
    }
    return true;
}

bool Layer::makeFlowTexture()
{
    const SDL_GPUTextureFormat flowFormat = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    const SDL_GPUTextureUsageFlags flowUsage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!SDL_GPUTextureSupportsFormat(gpu, flowFormat, SDL_GPU_TEXTURETYPE_2D, flowUsage)) {
        fail("RGBA32F interaction texture sampling is unavailable");
        return false;
    }
    SDL_GPUTextureCreateInfo flowDescription = {};
    flowDescription.type = SDL_GPU_TEXTURETYPE_2D;
    flowDescription.format = flowFormat;
    flowDescription.usage = flowUsage;
    flowDescription.width = flow.width();
    flowDescription.height = flow.height();
    flowDescription.layer_count_or_depth = flowDescription.num_levels = 1;
    flowTexture = SDL_CreateGPUTexture(gpu, &flowDescription);
    if (!flowTexture) {
        fail(SDL_GetError());
        return false;
    }
    return true;
}

bool Layer::resizeLowTarget(unsigned width, unsigned height)
{
    const unsigned lowWidth = width / 2 + width % 2;
    const unsigned lowHeight = height / 2 + height % 2;
    if (shared.lowTarget.Colour && shared.lowTarget.Width == lowWidth && shared.lowTarget.Height == lowHeight) return true;
    SDL_GPUTextureCreateInfo description = {};
    description.type = SDL_GPU_TEXTURETYPE_2D;
    description.format = LowTargetFormat;
    description.usage = LowTargetUsage;
    description.width = lowWidth;
    description.height = lowHeight;
    description.layer_count_or_depth = description.num_levels = 1;
    description.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(gpu, &description);
    if (!texture) { fail(SDL_GetError()); return false; }
    // Recorded passes retain the old texture until this batch is submitted;
    // SDL then keeps it alive until the GPU has finished those users.
    if (shared.lowTarget.Colour) frame->Release_After_Batch(shared.lowTarget.Colour, nullptr);
    shared.lowTarget = { texture, nullptr, lowWidth, lowHeight };
    return true;
}

bool Layer::prepareHeatBackground(const SdlTarget &target)
{
    if (!shared.heatBackground.Colour || shared.heatBackground.Width != target.Width || shared.heatBackground.Height != target.Height) {
        SDL_GPUTextureCreateInfo description = {};
        description.type = SDL_GPU_TEXTURETYPE_2D;
        description.format = static_cast<SDL_GPUTextureFormat>(SdlGpuFrame::Target_Format());
        description.usage = LowTargetUsage;
        description.width = target.Width;
        description.height = target.Height;
        description.layer_count_or_depth = description.num_levels = 1;
        description.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture *texture = SDL_CreateGPUTexture(gpu, &description);
        if (!texture) { fail(SDL_GetError()); return false; }
        if (shared.heatBackground.Colour) frame->Release_After_Batch(shared.heatBackground.Colour, nullptr);
        shared.heatBackground = { texture, nullptr, target.Width, target.Height };
    }
    return true;
}

void Layer::captureHeatBackground(const SdlTarget &target)
{
    // Capture the actual scene at this draw, not a possibly earlier pre-water
    // snapshot. The queued blit separates attachment writes from shader reads;
    // it requires neither a readback nor an extra GPU submission/wait.
    const int32_t rectangle[4] = { 0, 0, static_cast<int32_t>(target.Width), static_cast<int32_t>(target.Height) };
    frame->Record_Blit(target.Colour, rectangle, shared.heatBackground.Colour, rectangle, false);
}

void Layer::retireCache(FieldCache &cache)
{
    if (cache.density.Colour) frame->Release_After_Batch(cache.density.Colour, nullptr);
    if (cache.lighting.Colour) frame->Release_After_Batch(cache.lighting.Colour, nullptr);
    cache = {};
}

bool Layer::resizeFieldCache(unsigned index)
{
    const unsigned side = cacheSide(fieldStates[index]);
    FieldCache &cache = fieldCaches[index];
    if (cache.side == side && cache.density.Colour && cache.lighting.Colour) return true;
    SDL_GPUTextureCreateInfo description = {};
    description.type = SDL_GPU_TEXTURETYPE_2D;
    description.format = DensityTargetFormat;
    description.usage = LowTargetUsage;
    description.width = side * CacheColumns;
    description.height = side * CacheRows;
    description.layer_count_or_depth = description.num_levels = 1;
    description.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *density = SDL_CreateGPUTexture(gpu, &description);
    if (!density) { fail(SDL_GetError()); return false; }
    description.format = LowTargetFormat;
    SDL_GPUTexture *lighting = SDL_CreateGPUTexture(gpu, &description);
    if (!lighting) {
        fail(SDL_GetError());
        SDL_ReleaseGPUTexture(gpu, density);
        return false;
    }
    retireCache(cache);
    cache.density = { density, nullptr, description.width, description.height };
    cache.lighting = { lighting, nullptr, description.width, description.height };
    cache.side = side;
    invalidateBake();
    return true;
}

void Layer::bindCachedTextures(SdlRecordedDraw &draw, bool density)
{
    for (unsigned i = 0; i < 2; ++i) {
        SDL_GPUTexture *texture = density ? fieldCaches[i].density.Colour : fieldCaches[i].lighting.Colour;
        // An inactive/unallocated field never samples its cache. Still bind a
        // valid 2D texture, with a point sampler for the RGBA32F placeholder.
        draw.Textures[3 + i] = texture ? texture : flowTexture;
        draw.Samplers[3 + i] = texture ? shared.cacheSampler : shared.flowSampler;
    }
}

bool Layer::prepareCaches(Uniforms &uniforms, SDL_GPUTexture *sceneDepth,
                          uint32_t vertexOffset, const SdlTarget &restoreTarget)
{
    float *grids[] = { uniforms.Cache0, uniforms.Cache1 };
    for (unsigned i = 0; i < fieldCount; ++i) {
        if (!fieldActive(i)) continue;
        if (!resizeFieldCache(i)) return false;
        grids[i][0] = grids[i][1] = static_cast<float>(fieldCaches[i].side);
        grids[i][2] = static_cast<float>(CacheSlices);
        grids[i][3] = static_cast<float>(CacheColumns);
    }
    if (!takeBake(uniforms)) return true;

    for (unsigned i = 0; i < fieldCount; ++i) {
        if (!fieldActive(i)) continue;
        const FieldCache &cache = fieldCaches[i];
        Uniforms bake = uniforms;
        bake.CachePass[0] = static_cast<float>(i);
        SdlRecordedDraw draw = {};
        draw.Pipeline = shared.densityPipeline;
        draw.VertexOffset = vertexOffset;
        draw.Count = 3;
        draw.PixelConstants = frame->Constants(1, &bake, sizeof(bake));
        draw.PixelConstantsSize = sizeof(bake);
        // Shader reflection can retain declared atlas bindings even when this
        // variant does not read them. Supply all five slots, with harmless
        // placeholders that never alias the density attachment being written.
        draw.SamplerCount = 5;
        bindTerrain(draw);
        draw.Textures[0] = sceneDepth;
        draw.Samplers[0] = shared.depthSampler;
        draw.Textures[1] = flowTexture;
        draw.Samplers[1] = shared.flowSampler;
        for (unsigned slot = 2; slot < 5; ++slot) {
            draw.Textures[slot] = flowTexture;
            draw.Samplers[slot] = shared.flowSampler;
        }
        draw.Viewport[2] = static_cast<float>(cache.density.Width);
        draw.Viewport[3] = static_cast<float>(cache.density.Height);
        draw.Viewport[5] = 1;
        draw.Scissor[2] = static_cast<int32_t>(cache.density.Width);
        draw.Scissor[3] = static_cast<int32_t>(cache.density.Height);
        frame->Set_Target(cache.density);
        frame->Clear_Back_Buffer(true, false, false, 0, 1.0f, 0);
        frame->Record_Draw(draw);

        // Switching targets ends the density-writing pass before the lighting
        // pass reads it. The later camera pass similarly reads completed light.
        draw.Pipeline = shared.lightingPipeline;
        draw.SamplerCount = 5;
        bindTerrain(draw);
        draw.Textures[2] = flowTexture;
        draw.Samplers[2] = shared.flowSampler;
        bindCachedTextures(draw, true);
        frame->Set_Target(cache.lighting);
        frame->Clear_Back_Buffer(true, false, false, 0, 1.0f, 0);
        frame->Record_Draw(draw);
    }
    frame->Set_Target(restoreTarget);
    return true;
}

bool Layer::uploadTerrain()
{
    if (!terrainEnabled[0] && !terrainEnabled[1]) return true;
    if (!terrainTexture) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = TerrainGridSide * 2;
        info.height = TerrainGridSide;
        info.layer_count_or_depth = info.num_levels = 1;
        if (!SDL_GPUTextureSupportsFormat(gpu, info.format, info.type, info.usage)
            || !(terrainTexture = SDL_CreateGPUTexture(gpu, &info))) {
            fail("terrain height texture is unavailable");
            return false;
        }
        terrainDirty = true;
    }
    if (!terrainDirty) return true;
    // Uploads precede every recorded draw in a batch; flush this layer's old
    // readers before replacing heights, just as for its independent flow map.
    if (flowUsedBatch == frame->Batch() && !frame->Flush()) {
        fail("could not submit preceding terrain texture users");
        return false;
    }
    const uint32_t bytes = static_cast<uint32_t>(terrainPixels.size() * sizeof(float));
    uint32_t offset = 0;
    std::memcpy(frame->Upload_Space(bytes, offset), terrainPixels.data(), bytes);
    frame->Queue_Texture_Upload(terrainTexture, 0, TerrainGridSide * 2, TerrainGridSide, offset);
    terrainDirty = false;
    invalidateBake();
    return true;
}

void Layer::bindTerrain(SdlRecordedDraw &draw)
{
    draw.SamplerCount = 7;
    draw.Textures[5] = flowTexture;
    draw.Samplers[5] = shared.flowSampler;
    draw.Textures[6] = terrainTexture ? terrainTexture : flowTexture;
    draw.Samplers[6] = shared.flowSampler;
}

bool Layer::uploadFlow()
{
    if (!flowNeedsUpload()) return true;
    if (!flow.valid()) {
        fail("interaction simulation produced invalid state");
        return false;
    }
    // Batch uploads run before all its draws. Submit any already-recorded user
    // before replacing the texture, otherwise that draw would read a later step.
    // Level-zero upload cycling then protects users in earlier submitted batches.
    if (flowUsedBatch == frame->Batch() && !frame->Flush()) {
        fail("could not submit preceding interaction texture users");
        return false;
    }
    const uint32_t bytes = flow.width() * flow.height() * 4 * sizeof(float);
    uint32_t offset = 0;
    std::memcpy(frame->Upload_Space(bytes, offset), flow.data(), bytes);
    frame->Queue_Texture_Upload(flowTexture, 0, flow.width(), flow.height(), offset);
    flowUploaded();
    return true;
}

bool Layer::resolveSceneTarget(PosixDevice9 *device, SdlTarget &target)
{
    IDirect3DSurface9 *current = nullptr, *back = nullptr, *depth = nullptr;
    device->GetRenderTarget(0, &current);
    device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    device->GetDepthStencilSurface(&depth);
    SdlResourceMirrors *mirrors = device->Get_Mirrors();
    std::string reason;
    target = {};
    if (current && current == back) target = frame->Back_Buffer_Target();
    else if (current && mirrors) {
        D3DSURFACE_DESC description;
        if (current->GetDesc(&description) == D3D_OK) {
            target.Width = description.Width;
            target.Height = description.Height;
            // The main scene can be an engine-owned postprocessing texture.
            // Its surface and texture share one GPU allocation, so resolve the
            // texture's existing mirror rather than creating a separate surface.
            IDirect3DTexture9 *texture = nullptr;
            if (current->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void **>(&texture)) == D3D_OK
                && texture) {
                IDirect3DSurface9 *level = nullptr;
                texture->GetSurfaceLevel(0, &level);
                if (level == current) target.Colour = mirrors->Texture(texture, reason);
                if (level) level->Release();
                texture->Release();
            } else {
                target.Colour = mirrors->Surface(current, false, reason);
            }
        }
    }
    if (target.Colour && mirrors) {
        D3DSURFACE_DESC description;
        // Explicit depth surfaces already used by opaque scene draws have a
        // mirror. The implicit depth surface is owned by SdlGpuFrame instead;
        // the device also borrows its size-matched depth for intermediate targets.
        // Read only a known explicit mirror, never allocate an empty depth here.
        if (depth && mirrors->Has_Copy(depth) && depth->GetDesc(&description) == D3D_OK
            && description.Width == target.Width && description.Height == target.Height)
            target.Depth = mirrors->Surface(depth, true, reason);
        else
            target.Depth = frame->Depth_For(target.Width, target.Height);
    }
    if (current) current->Release();
    if (back) back->Release();
    if (depth) depth->Release();
    if (!target.Colour || !target.Depth || !target.Width || !target.Height) {
        fail(reason.empty() ? "could not resolve the current scene colour/depth target" : reason.c_str());
        return false;
    }
    return true;
}

bool Layer::render(const Camera &camera)
{
    if (!initialized || failed || !anyFieldActive()) return false;
    PosixDevice9 *device = static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    if (!device || device->Get_Gpu() != frame) {
        fail("the preview graphics device was replaced");
        return false;
    }
    // The scene's normal pass can target an intermediate texture. Resolve that
    // actual colour/depth pair, rather than assuming it is the final back buffer.
    // Reflection and custom passes are excluded by the caller.
    SdlTarget target;
    if (!resolveSceneTarget(device, target)) return false;
    if (!resizeLowTarget(target.Width, target.Height)) return false;
    if (frame->Batch_Is_Full() && !frame->Flush()) {
        fail("could not submit preceding scene draws");
        return false;
    }
    if (!uploadFlow() || !uploadTerrain()) return false;
    SDL_GPUTexture *sceneDepth = frame->Scene_Depth(target);
    if (!sceneDepth) {
        fail("sampleable scene depth is unavailable; volume clipping cannot run");
        return false;
    }
    Uniforms uniforms;
    float viewProjection[16];
    if (!buildUniforms(camera, target.Width, target.Height, uniforms, viewProjection)) return false;

    // Complete all potentially failing material allocations before recording
    // a visible scene draw. The adapter can safely fall back to classic clouds.
    if (uniforms.Settings[1] > 0 && !prepareHeatBackground(target)) return false;
    if (!SdlHazardCloudEmbers::prepare(frame, target, fieldStates, fieldCount)) {
        fail("could not prepare nuclear embers");
        return false;
    }
    uint32_t vertexOffset = 0;
    std::memset(frame->Stage(16, vertexOffset), 0, 16);
    if (!prepareCaches(uniforms, sceneDepth, vertexOffset, target)) return false;
    SdlRecordedDraw draw = {};
    draw.Pipeline = shared.volumePipeline;
    // The common batch binds a vertex buffer even for SV_VertexID shaders.
    // Supply a valid unused allocation; the fullscreen triangle needs no mesh.
    draw.VertexOffset = vertexOffset;
    draw.Count = 3;
    draw.PixelConstants = frame->Constants(1, &uniforms, sizeof(uniforms));
    draw.PixelConstantsSize = sizeof(uniforms);
    draw.SamplerCount = 5;
    bindTerrain(draw);
    draw.Textures[0] = sceneDepth;
    draw.Samplers[0] = shared.depthSampler;
    draw.Textures[1] = flowTexture;
    draw.Samplers[1] = shared.flowSampler;
    // Slot 2 is only sampled by the composite. It must not alias the low target
    // while this pass writes that target, even though the low shader ignores it.
    draw.Textures[2] = flowTexture;
    draw.Samplers[2] = shared.flowSampler;
    bindCachedTextures(draw, false);
    draw.Viewport[2] = static_cast<float>(shared.lowTarget.Width);
    draw.Viewport[3] = static_cast<float>(shared.lowTarget.Height);
    draw.Viewport[5] = 1;
    draw.Scissor[2] = static_cast<int32_t>(shared.lowTarget.Width);
    draw.Scissor[3] = static_cast<int32_t>(shared.lowTarget.Height);
    frame->Set_Target(shared.lowTarget);
    frame->Clear_Back_Buffer(true, false, false, 0, 1.0f, 0);
    frame->Record_Draw(draw);

    // Target changes close the low-resolution pass before the composite samples
    // it. Both draws keep the original camera/depth uniforms; only the low pass's
    // raster viewport changes. No extra submission or GPU wait is necessary.
    draw.Pipeline = shared.compositePipeline;
    draw.SamplerCount = 5;
    bindTerrain(draw);
    draw.Textures[2] = shared.lowTarget.Colour;
    draw.Samplers[2] = shared.depthSampler;
    std::copy(uniforms.Viewport, uniforms.Viewport + 4, draw.Viewport);
    draw.Viewport[4] = camera.minZ;
    draw.Viewport[5] = camera.maxZ;
    draw.Scissor[0] = static_cast<int32_t>(camera.x);
    draw.Scissor[1] = static_cast<int32_t>(camera.y);
    draw.Scissor[2] = std::min(camera.width, target.Width - std::min(camera.x, target.Width));
    draw.Scissor[3] = std::min(camera.height, target.Height - std::min(camera.y, target.Height));
    frame->Set_Target(target);
    frame->Record_Draw(draw);
    SdlHazardCloudEmbers::render(frame, target, sceneDepth, viewProjection, draw.Viewport,
        fieldStates, fieldCount, simulationTime, terrainTexture, terrainEnabled);
    if (uniforms.Settings[1] > 0) {
        SdlRecordedDraw heatDraw = draw;
        int scissor[4];
        heatScissor(viewProjection, camera, target.Width, target.Height, scissor);
        std::copy(scissor, scissor + 4, heatDraw.Scissor);
        if (heatDraw.Scissor[2] > 0 && heatDraw.Scissor[3] > 0) {
            // Include the smoke and embers in the refraction. Warping only the
            // background makes heat disappear under a dense cloud's opacity.
            captureHeatBackground(target);
            heatDraw.Pipeline = shared.heatPipeline;
            heatDraw.SamplerCount = 7;
            heatDraw.Textures[5] = shared.heatBackground.Colour;
            heatDraw.Samplers[5] = shared.cacheSampler;
            frame->Record_Draw(heatDraw);
        }
    }
    flowUsedBatch = frame->Batch();
    // Recorded state is private to this draw. The next engine draw rebinds its
    // own pipeline, samplers and uniforms without any D3D9 state restoration.
    ++renderedFrameCount;
    return true;
}

bool Layer::initialize()
{
    if (initialized) return !failed;
    if (!HazardCloud::Supported()) return false;
    failed = false;
    PosixDevice9 *device = static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    if (!device || !(frame = device->Get_Gpu()) || !(gpu = frame->Device())) {
        fail("an SDL graphics device is required");
        return false;
    }
    if (shared.gpu && (shared.frame != frame || shared.gpu != gpu)) {
        if (shared.users) {
            fail("existing cloud layers belong to a different graphics device");
            frame = nullptr;
            gpu = nullptr;
            return false;
        }
        // The device was replaced without a ReleaseDevice; its objects went with it.
        releaseShared(false);
    }
    if (!shared.gpu) {
        shared.frame = frame;
        shared.gpu = gpu;
        if (!makeResources()) {
            releaseShared(true);
            frame = nullptr;
            gpu = nullptr;
            return false;
        }
    }
    ++shared.users;
    initialized = true;
    if (!makeFlowTexture()) { shutdown(); return false; }
    if (!explicitDomain && !fieldCount) flow.setDomain(-400, -220, 400, 220);
    flowDirty = true;
    return true;
}

void Layer::shutdown()
{
    if (!initialized) return;
    PosixDevice9 *device = static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    const bool alive = device && device->Get_Gpu() == frame;
    if (alive) {
        for (FieldCache &cache : fieldCaches) retireCache(cache);
        if (flowTexture) frame->Release_After_Batch(flowTexture, nullptr);
        if (terrainTexture) frame->Release_After_Batch(terrainTexture, nullptr);
    }
    for (FieldCache &cache : fieldCaches) cache = {};
    flowTexture = terrainTexture = nullptr;
    initialized = false;
    if (shared.users) --shared.users;
    frame = nullptr;
    gpu = nullptr;
    flowUsedBatch = ~uint64_t(0);
    resetCounters();
}

std::uint64_t Layer::estimatedCacheBytes() const
{
    std::uint64_t result = flowTexture ? 192u * 96u * 4u * sizeof(float) : 0;
    if (terrainTexture) result += TerrainGridSide * TerrainGridSide * 2u * sizeof(float);
    for (const FieldCache &cache : fieldCaches)
        result += std::uint64_t(cache.side) * cache.side * CacheSlices * 10u;
    return result;
}

} // namespace
} // namespace SdlHazardCloud

namespace HazardCloud {

bool Supported()
{
    PosixDevice9 *device = dynamic_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    SdlGpuFrame *frame = device ? device->Get_Gpu() : nullptr;
    if (!frame || !frame->Device() || !frame->Supports_Scene_Depth()) return false;
    const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    return SDL_GPUTextureSupportsFormat(frame->Device(), SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
               SDL_GPU_TEXTURETYPE_2D, usage)
        && SDL_GPUTextureSupportsFormat(frame->Device(), SDL_GPU_TEXTUREFORMAT_R16_FLOAT,
               SDL_GPU_TEXTURETYPE_2D, usage)
        && SDL_GPUTextureSupportsFormat(frame->Device(), SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
               SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

std::unique_ptr<Layer> CreateLayer()
{
    if (!Supported()) return nullptr;
    return std::make_unique<SdlHazardCloud::Layer>();
}

void ReleaseDevice()
{
    if (SdlHazardCloud::shared.gpu) SdlHazardCloud::releaseShared(SdlHazardCloud::sharedDeviceAlive());
}

} // namespace HazardCloud
