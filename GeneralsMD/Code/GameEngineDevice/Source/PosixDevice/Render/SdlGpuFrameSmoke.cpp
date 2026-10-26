// Native smoke moments. The scene's colour/depth attachments and CPU state are untouched.
#include "SdlGpuFrame.h"
#include "SdlSunShadowDebug.h"
#include "Platform/SunSmoke.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cstdio>

struct SdlGpuFrame::SmokeResources {
    SDL_GPUTexture *Texture = nullptr;
    SDL_GPUSampler *Sampler = nullptr;
    SDL_GPUShader *Vertex = nullptr, *Pixel = nullptr;
    SDL_GPUGraphicsPipeline *Pipeline = nullptr;
    SDL_GPUBuffer *Instances = nullptr;
    SDL_GPUTransferBuffer *Upload = nullptr;
    std::vector<float> Splats;
};

void SdlGpuFrame::Release_Sun_Smoke()
{
    if (!Smoke) return;
    if (Smoke->Texture) SDL_ReleaseGPUTexture(GpuDevice, Smoke->Texture);
    if (Smoke->Sampler) SDL_ReleaseGPUSampler(GpuDevice, Smoke->Sampler);
    if (Smoke->Vertex) SDL_ReleaseGPUShader(GpuDevice, Smoke->Vertex);
    if (Smoke->Pixel) SDL_ReleaseGPUShader(GpuDevice, Smoke->Pixel);
    if (Smoke->Pipeline) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, Smoke->Pipeline);
    if (Smoke->Instances) SDL_ReleaseGPUBuffer(GpuDevice, Smoke->Instances);
    if (Smoke->Upload) SDL_ReleaseGPUTransferBuffer(GpuDevice, Smoke->Upload);
    delete Smoke; Smoke = nullptr; SmokeCount = 0;
}

bool SdlGpuFrame::Fail_Sun_Smoke(const char *reason)
{
    if (!SmokeFailed) std::fprintf(stderr,"SunShadow: smoke map unavailable (%s); legacy particle shadows\n",reason);
    // Keep resources until destruction: a previously recorded receiver may still reference them.
    SmokeFailed = true; SmokeCount = 0;
    return false;
}

bool SdlGpuFrame::Create_Sun_Smoke()
{
    if (SmokeFailed) return false;
    if (Smoke) return true;
    if (Sdl_Sun_Shadow_Fault("smoke-target")) return Fail_Sun_Smoke("injected smoke target failure");
    const auto format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    const auto usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!SDL_GPUTextureSupportsFormat(GpuDevice,format,SDL_GPU_TEXTURETYPE_2D,usage))
        return Fail_Sun_Smoke("RGBA32F colour target/sampler unsupported");
    Smoke = new SmokeResources;
    SDL_GPUTextureCreateInfo texture = {};
    texture.type = SDL_GPU_TEXTURETYPE_2D; texture.format = format; texture.usage = usage;
    texture.width = texture.height = SunSmoke::Texels;
    texture.layer_count_or_depth = texture.num_levels = 1;
    Smoke->Texture = SDL_CreateGPUTexture(GpuDevice,&texture);
    SDL_GPUSamplerCreateInfo sampler = {};
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    Smoke->Sampler = SDL_CreateGPUSampler(GpuDevice,&sampler);
    SDL_GPUBufferCreateInfo buffer = {};
    buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer.size = SunSmoke::MaxCasters * SunSmoke::SplatFloats * sizeof(float);
    Smoke->Instances = SDL_CreateGPUBuffer(GpuDevice,&buffer);
    SDL_GPUTransferBufferCreateInfo transfer = {};
    transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; transfer.size = buffer.size;
    Smoke->Upload = SDL_CreateGPUTransferBuffer(GpuDevice,&transfer);
    if (!Smoke->Texture || !Smoke->Sampler || !Smoke->Instances || !Smoke->Upload)
        return Fail_Sun_Smoke(SDL_GetError());
    // These two stages have matching declaration-order varyings and no resources/uniforms.
    // Explicit instance locations avoid depending on the compiler's semantic assignment.
    std::string vertex = SunSmoke::SMOKE_SPLAT_VERTEX_PROGRAM;
    vertex.insert(vertex.find("float4 Centre"),"[[vk::location(0)]] ");
    vertex.insert(vertex.find("float2 Shape"),"[[vk::location(1)]] ");
    std::vector<unsigned char> spirv; std::string error;
    if (!SDL3_Compile_HLSL_To_SPIRV(vertex,true,spirv,error)
        || !(Smoke->Vertex = SDL3_Create_Shader(GpuDevice,spirv,true,error))
        || !SDL3_Compile_HLSL_To_SPIRV(SunSmoke::SMOKE_SPLAT_PIXEL_PROGRAM,false,spirv,error)
        || !(Smoke->Pixel = SDL3_Create_Shader(GpuDevice,spirv,false,error)))
        return Fail_Sun_Smoke(error.c_str());
    if (Sdl_Sun_Shadow_Fault("smoke-pipeline")) return Fail_Sun_Smoke("injected smoke pipeline failure");
    SDL_GPUVertexBufferDescription input = {};
    input.pitch = SunSmoke::SplatFloats * sizeof(float);
    input.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    SDL_GPUVertexAttribute attributes[2] = {};
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[1].location = 1; attributes[1].offset = 4*sizeof(float);
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    SDL_GPUColorTargetDescription target = {}; target.format = format;
    auto &blend = target.blend_state;
    blend.enable_blend = true;
    blend.src_color_blendfactor = blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_MIN;
    SDL_GPUGraphicsPipelineCreateInfo pipeline = {};
    pipeline.vertex_shader = Smoke->Vertex; pipeline.fragment_shader = Smoke->Pixel;
    pipeline.vertex_input_state.vertex_buffer_descriptions = &input;
    pipeline.vertex_input_state.num_vertex_buffers = 1;
    pipeline.vertex_input_state.vertex_attributes = attributes;
    pipeline.vertex_input_state.num_vertex_attributes = 2;
    pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline.target_info.color_target_descriptions = &target;
    pipeline.target_info.num_color_targets = 1;
    Smoke->Pipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice,&pipeline);
    return Smoke->Pipeline || Fail_Sun_Smoke(SDL_GetError());
}

bool SdlGpuFrame::Fill_Sun_Smoke(const float *sun, const float *casters, unsigned count)
{
    SmokeCount = 0;
    if (!casters || !count) return !SmokeFailed;
    if (!Create_Sun_Smoke()) return false;
    const unsigned splats = SunSmoke::Project(sun,casters,count,Smoke->Splats);
    if (!splats) return true;
    // All previously queued readers must be submitted before this overwrite. GPU submissions
    // run in order; cycling the upload and vertex buffer protects their in-flight versions.
    if (!Flush()) return Fail_Sun_Smoke("queued reader submission failed");
    void *mapped = SDL_MapGPUTransferBuffer(GpuDevice,Smoke->Upload,true);
    if (!mapped) return Fail_Sun_Smoke(SDL_GetError());
    const Uint32 bytes = splats * SunSmoke::SplatFloats * sizeof(float);
    std::memcpy(mapped,Smoke->Splats.data(),bytes);
    SDL_UnmapGPUTransferBuffer(GpuDevice,Smoke->Upload);
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
    if (!commands) return Fail_Sun_Smoke(SDL_GetError());
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
    if (!copy) { SDL_CancelGPUCommandBuffer(commands); return Fail_Sun_Smoke(SDL_GetError()); }
    SDL_GPUTransferBufferLocation source = {}; source.transfer_buffer = Smoke->Upload;
    SDL_GPUBufferRegion destination = {}; destination.buffer = Smoke->Instances; destination.size = bytes;
    SDL_UploadToGPUBuffer(copy,&source,&destination,true);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUColorTargetInfo colour = {}; colour.texture = Smoke->Texture;
    colour.clear_color.a = 1; colour.load_op = SDL_GPU_LOADOP_CLEAR; colour.store_op = SDL_GPU_STOREOP_STORE;
    // Do not cycle the map: already submitted readers use this version, and the ordered write
    // must be the one subsequently queued receivers sample.
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands,&colour,1,nullptr);
    if (!pass) { SDL_CancelGPUCommandBuffer(commands); return Fail_Sun_Smoke(SDL_GetError()); }
    SDL_BindGPUGraphicsPipeline(pass,Smoke->Pipeline);
    SDL_GPUBufferBinding binding = {}; binding.buffer = Smoke->Instances;
    SDL_BindGPUVertexBuffers(pass,0,&binding,1);
    SDL_DrawGPUPrimitives(pass,4,splats,0,0);
    SDL_EndGPURenderPass(pass);
    if (!(SerializeSubmits ? Submit(commands) : Submit_Flush(commands)))
        return Fail_Sun_Smoke("smoke submission failed");
    SmokeCount = splats;
    return true;
}

SDL_GPUTexture *SdlGpuFrame::Sun_Smoke_Texture() const { return SmokeCount && Smoke ? Smoke->Texture : nullptr; }
SDL_GPUSampler *SdlGpuFrame::Sun_Smoke_Sampler() const { return Smoke ? Smoke->Sampler : nullptr; }

// Explicit tests/reports only. Four float channels cannot use the BGRA8 scene download.
bool SdlGpuFrame::Read_Sun_Smoke(std::vector<float> &moments)
{
    moments.clear();
    if (!Sun_Smoke_Texture() || !Flush()) return false;
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = SunSmoke::Texels * SunSmoke::Texels * 4 * sizeof(float);
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(GpuDevice,&info);
    if (!transfer) return false;
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
    if (!commands) { SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer); return false; }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
    if (!copy) { SDL_CancelGPUCommandBuffer(commands); SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer); return false; }
    SDL_GPUTextureRegion region = {}; region.texture = Smoke->Texture;
    region.w = region.h = SunSmoke::Texels; region.d = 1;
    SDL_GPUTextureTransferInfo target = {}; target.transfer_buffer = transfer;
    target.pixels_per_row = target.rows_per_layer = SunSmoke::Texels;
    SDL_DownloadFromGPUTexture(copy,&region,&target);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    bool ok = fence && SDL_WaitForGPUFences(GpuDevice,true,&fence,1);
    if (fence) SDL_ReleaseGPUFence(GpuDevice,fence);
    if (ok) {
        const float *mapped = static_cast<const float*>(SDL_MapGPUTransferBuffer(GpuDevice,transfer,false));
        ok = mapped != nullptr;
        if (ok) { moments.assign(mapped,mapped+info.size/sizeof(float)); SDL_UnmapGPUTransferBuffer(GpuDevice,transfer); }
    }
    SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);
    return ok;
}
