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

// The device's frame on SDL3 GPU (decision 7, phase A3a).  See SdlGpuFrame.h.

#include "SdlGpuFrame.h"
#include "sdl3shadercompile.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The back buffer's format.  D3D9's X8R8G8B8 and A8R8G8B8 are these bytes in this order.
static const SDL_GPUTextureFormat BACK_BUFFER_FORMAT = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

// The gamma pass: one triangle over the whole target, and each channel looked up in its ramp.  Written
// for SDL3's register spaces (sdl3target.h: pixel textures and samplers in space2).  The ramp is read
// with a point sampler at its entry's centre, so the lookup is exact.
static const char GAMMA_VERTEX_HLSL[] =
	"struct Output\n"
	"{\n"
	"	float4 Position : SV_Position;\n"
	"	[[vk::location(0)]] float2 Uv : TEXCOORD0;\n"
	"};\n"
	"Output main(uint id : SV_VertexID)\n"
	"{\n"
	"	Output output;\n"
	"	float2 corner = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);\n"
	"	output.Position = float4(corner, 0.0, 1.0);\n"
	"	output.Uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);\n"
	"	return output;\n"
	"}\n";

static const char GAMMA_PIXEL_HLSL[] =
	"Texture2D BackBuffer : register(t0, space2);\n"
	"SamplerState BackSampler : register(s0, space2);\n"
	"Texture2D Ramp : register(t1, space2);\n"
	"SamplerState RampSampler : register(s1, space2);\n"
	"float entry(float value, int channel)\n"
	"{\n"
	"	float index = round(saturate(value) * 255.0);\n"
	"	return Ramp.SampleLevel(RampSampler, float2((index + 0.5) / 256.0, 0.5), 0.0)[channel];\n"
	"}\n"
	"float4 main([[vk::location(0)]] float2 uv : TEXCOORD0) : SV_Target0\n"
	"{\n"
	"	float4 colour = BackBuffer.SampleLevel(BackSampler, uv, 0.0);\n"
	"	return float4(entry(colour.r, 0), entry(colour.g, 1), entry(colour.b, 2), colour.a);\n"
	"}\n";

bool Sdl_Gamma_Is_Identity(const uint16_t (*ramp)[256])
{
	if (ramp == NULL) {
		return true;
	}
	for (int channel = 0; channel < 3; ++channel) {
		for (int index = 0; index < 256; ++index) {
			if (ramp[channel][index] != (uint16_t)(index * 257)) {
				return false;
			}
		}
	}
	return true;
}

unsigned int SdlGpuFrame::Target_Format()
{
	return BACK_BUFFER_FORMAT;
}

SdlGpuFrame::SdlGpuFrame() :
	SerializeSubmits(false),
	FlushMs(0.0),
	FenceMs(0.0),
	Flushes(0),
	AcquireMs(0.0),
	NotVisible(0),
	NotVisibleTotal(0),
	NotShown(0),
	NotShownTotal(0),
	ShownWidth(0),
	ShownHeight(0),
	FlushLimit(3),
	FlushesTotal(0),
	FlushInFlightMost(0),
	FlushWaits(0),
	BatchDrawLimit(2048),
	DrawLimitFlushes(0),
	OffscreenMs(0.0),
	OffscreenPresents(false),
	OffscreenHz(0),
	DisplayTexture(NULL),
	InFlightNext(0),
	NextTickNs(0),
	GpuDevice(NULL),
	Window(NULL),
	BackBuffer(NULL),
	DepthStencil(NULL),
	FrontCopy(NULL),
	DepthFormat(SDL_GPU_TEXTUREFORMAT_INVALID),
	BackWidth(0),
	BackHeight(0),
	BatchNumber(1),
	ClearVertex(NULL),
	ClearPixel(NULL),
	StreamBuffer(NULL),
	StreamBufferSize(0),
	Transfer(NULL),
	TransferSize(0),
	GammaVertex(NULL),
	GammaPixel(NULL),
	GammaPipeline(NULL),
	GammaPipelineFormat(SDL_GPU_TEXTUREFORMAT_INVALID),
	RampTexture(NULL),
	PointSampler(NULL)
{
	LastConstants[0] = LastConstants[1] = LastConstantsSize[0] = LastConstantsSize[1] = 0;
	memset(ClearPipelines, 0, sizeof(ClearPipelines));
	InFlight[0] = InFlight[1] = NULL;
	// ZH_GPU_FLUSH_LIMIT=<n>: at most n mid-frame flushes in flight (default 3); 0 lets them pile up as before
	// the ring, which is how the ring's effect is measured.
	if (const char * limit = getenv("ZH_GPU_FLUSH_LIMIT")) {
		FlushLimit = (unsigned int)strtoul(limit, NULL, 10);
	}
	// ZH_GPU_BATCH_DRAWS=<n>: at most n draws in one batch (default 2048); 0 lets a batch grow until Present.
	if (const char * draws = getenv("ZH_GPU_BATCH_DRAWS")) {
		BatchDrawLimit = (unsigned int)strtoul(draws, NULL, 10);
	}
	memset(&CurrentTarget, 0, sizeof(CurrentTarget));
	TargetSet = false;
}

SdlGpuFrame * SdlGpuFrame::Create(RenderWindow window, unsigned int width, unsigned int height, std::string & error)
{
	SdlGpuFrame * frame = new SdlGpuFrame();
	// Debug validation only when asked: it is slow, and the game runs without it.
	const bool debug = getenv("ZH_GPU_DEBUG") != NULL;
	frame->GpuDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, debug, NULL);
	if (frame->GpuDevice == NULL) {
		error = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
		delete frame;
		return NULL;
	}
	// The one place a RenderWindow is taken back to what it is: C2 hands the device its SDL_Window.
	frame->Window = reinterpret_cast<SDL_Window *>(window);
	if (frame->Window != NULL && !SDL_ClaimWindowForGPUDevice(frame->GpuDevice, frame->Window)) {
		error = std::string("SDL_ClaimWindowForGPUDevice: ") + SDL_GetError();
		frame->Window = NULL;
		delete frame;
		return NULL;
	}
	// D24S8 is what D3D9's auto depth-stencil almost always is; Apple's GPUs have none, and take D32S8.
	frame->DepthFormat = SDL_GPUTextureSupportsFormat(frame->GpuDevice, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
		SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
		? SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT : SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
	if (!frame->Create_Targets(width, height)) {
		error = std::string("the back buffer: ") + SDL_GetError();
		delete frame;
		return NULL;
	}
	return frame;
}

SdlGpuFrame::~SdlGpuFrame()
{
	if (GpuDevice == NULL) {
		return;
	}
	for (int i = 0; i < 2; ++i) {
		if (InFlight[i] != NULL) {
			SDL_WaitForGPUFences(GpuDevice, true, &InFlight[i], 1);
			SDL_ReleaseGPUFence(GpuDevice, InFlight[i]);
		}
	}
	for (size_t i = 0; i < FlushFences.size(); ++i) {
		SDL_WaitForGPUFences(GpuDevice, true, &FlushFences[i], 1);
		SDL_ReleaseGPUFence(GpuDevice, FlushFences[i]);
	}
	FlushFences.clear();
	Release_Targets();
	Commands.clear();
	End_Batch();
	if (StreamBuffer != NULL) SDL_ReleaseGPUBuffer(GpuDevice, StreamBuffer);
	for (size_t i = 0; i < ScratchDepths.size(); ++i) SDL_ReleaseGPUTexture(GpuDevice, ScratchDepths[i].Texture);
	for (int i = 0; i < 8; ++i) if (ClearPipelines[i] != NULL) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, ClearPipelines[i]);
	if (ClearVertex != NULL) SDL_ReleaseGPUShader(GpuDevice, ClearVertex);
	if (ClearPixel != NULL) SDL_ReleaseGPUShader(GpuDevice, ClearPixel);
	if (Transfer != NULL) SDL_ReleaseGPUTransferBuffer(GpuDevice, Transfer);
	if (GammaPipeline != NULL) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, GammaPipeline);
	if (GammaVertex != NULL) SDL_ReleaseGPUShader(GpuDevice, GammaVertex);
	if (GammaPixel != NULL) SDL_ReleaseGPUShader(GpuDevice, GammaPixel);
	if (RampTexture != NULL) SDL_ReleaseGPUTexture(GpuDevice, RampTexture);
	if (PointSampler != NULL) SDL_ReleaseGPUSampler(GpuDevice, PointSampler);
	if (Window != NULL) SDL_ReleaseWindowFromGPUDevice(GpuDevice, Window);
	SDL_DestroyGPUDevice(GpuDevice);
}

bool SdlGpuFrame::Create_Targets(unsigned int width, unsigned int height)
{
	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = BACK_BUFFER_FORMAT;
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = width;
	info.height = height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	BackBuffer = SDL_CreateGPUTexture(GpuDevice, &info);
	FrontCopy = SDL_CreateGPUTexture(GpuDevice, &info);
	info.format = (SDL_GPUTextureFormat)DepthFormat;
	info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	DepthStencil = SDL_CreateGPUTexture(GpuDevice, &info);
	BackWidth = width;
	BackHeight = height;
	return BackBuffer != NULL && DepthStencil != NULL && FrontCopy != NULL;
}

void SdlGpuFrame::Release_Targets()
{
	if (BackBuffer != NULL) SDL_ReleaseGPUTexture(GpuDevice, BackBuffer);
	if (DepthStencil != NULL) SDL_ReleaseGPUTexture(GpuDevice, DepthStencil);
	if (FrontCopy != NULL) SDL_ReleaseGPUTexture(GpuDevice, FrontCopy);
	if (DisplayTexture != NULL) SDL_ReleaseGPUTexture(GpuDevice, DisplayTexture);
	BackBuffer = NULL;
	DepthStencil = NULL;
	FrontCopy = NULL;
	DisplayTexture = NULL;		// made again at the new size by the next offscreen Present
}

bool SdlGpuFrame::Resize(unsigned int width, unsigned int height)
{
	// What is recorded runs first: the GPU copies count the uploads it carries as done.  The current
	// target named the old back buffer; the device sets it again before its next draw.
	Flush();
	TargetSet = false;
	Release_Targets();
	return Create_Targets(width, height);
}

void SdlGpuFrame::Clear_Back_Buffer(bool colour, bool depth, bool stencil, uint32_t argb, float z, uint32_t stencil_value)
{
	if (!colour && !depth && !stencil) {
		return;
	}
	Command command;
	memset(&command, 0, sizeof(command));
	command.Colour = colour;
	command.Depth = depth;
	command.Stencil = stencil;
	command.Argb = argb;
	command.Z = z;
	command.StencilValue = stencil_value;
	command.Target = Target_Index();
	Commands.push_back(command);
}


bool SdlGpuFrame::Flush()
{
	const Uint64 start = SDL_GetTicksNS();
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	const bool recorded = Record_Batch(commands);
	const bool submitted = SerializeSubmits ? Submit(commands) : Submit_Flush(commands);
	End_Batch();
	FlushMs += (double)(SDL_GetTicksNS() - start) / 1.0e6;
	++Flushes;
	++FlushesTotal;
	return submitted && recorded;
}

// A flush's submit, with a fence kept until the GPU is done: the ones done are let go, and past FlushLimit the
// oldest is waited for.  Present needs none of this, as the swapchain already bounds the frames in flight;
// flushes had no bound (a texture updated twice in one batch flushes, as does every restore after a reset).
bool SdlGpuFrame::Submit_Flush(SDL_GPUCommandBuffer * commands)
{
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	if (fence == NULL) {
		return false;
	}
	size_t kept = 0;
	for (size_t i = 0; i < FlushFences.size(); ++i) {
		if (SDL_QueryGPUFence(GpuDevice, FlushFences[i])) {
			SDL_ReleaseGPUFence(GpuDevice, FlushFences[i]);
		} else {
			FlushFences[kept++] = FlushFences[i];
		}
	}
	FlushFences.resize(kept);
	FlushFences.push_back(fence);
	if (FlushFences.size() > FlushInFlightMost) {
		FlushInFlightMost = (unsigned int)FlushFences.size();
		if (FlushInFlightMost >= 8 && (FlushInFlightMost & (FlushInFlightMost - 1)) == 0) {
			fprintf(stderr, "SdlGpuFrame: %u mid-frame flushes in flight at once (flush %u)\n", FlushInFlightMost, FlushesTotal + 1);
		}
	}
	if (FlushLimit > 0 && FlushFences.size() > FlushLimit) {
		SDL_WaitForGPUFences(GpuDevice, true, &FlushFences[0], 1);	// counted in Flush's own time (device flush)
		SDL_ReleaseGPUFence(GpuDevice, FlushFences[0]);
		FlushFences.erase(FlushFences.begin());
		++FlushWaits;
	}
	return true;
}

bool SdlGpuFrame::Submit(SDL_GPUCommandBuffer * commands)
{
	if (!SerializeSubmits) {
		return SDL_SubmitGPUCommandBuffer(commands);
	}
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	if (fence == NULL) {
		return false;
	}
	const Uint64 start = SDL_GetTicksNS();
	const bool waited = SDL_WaitForGPUFences(GpuDevice, true, &fence, 1);
	FenceMs += (double)(SDL_GetTicksNS() - start) / 1.0e6;
	SDL_ReleaseGPUFence(GpuDevice, fence);
	return waited;
}

void SdlGpuFrame::Set_Offscreen_Presents(unsigned int hz)
{
	OffscreenPresents = true;
	// 1 to 1000 a second; anything else (ZH_OFFSCREEN_HZ=-1 reads as 4294967295) is unpaced, never a
	// period of zero.
	OffscreenHz = hz <= 1000 ? hz : 0;
}

// -offscreen's submit: a fence for this frame, a wait for the one two frames back, then the pacer.
bool SdlGpuFrame::Submit_Offscreen(SDL_GPUCommandBuffer * commands)
{
	if (SerializeSubmits) {
		return Submit(commands);		// ZH_GPU_TIMING_SYNC: every submit waits for its fence already
	}
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	if (fence == NULL) {
		return false;
	}
	const Uint64 start = SDL_GetTicksNS();
	SDL_GPUFence *& oldest = InFlight[InFlightNext];
	if (oldest != NULL) {
		SDL_WaitForGPUFences(GpuDevice, true, &oldest, 1);
		SDL_ReleaseGPUFence(GpuDevice, oldest);
	}
	oldest = fence;
	InFlightNext = (InFlightNext + 1) % 2;
	if (OffscreenHz > 0) {
		// The next tick after the last one, as vsync puts a frame on the next refresh: a late frame waits
		// for the tick after, and the ticks keep their phase.
		const Uint64 period = 1000000000ull / OffscreenHz;
		const Uint64 now = SDL_GetTicksNS();
		if (NextTickNs == 0) {
			NextTickNs = now;
		}
		NextTickNs += period;
		if (NextTickNs < now) {
			NextTickNs += ((now - NextTickNs) / period + 1) * period;
		}
		SDL_DelayPrecise(NextTickNs - now);
	}
	OffscreenMs += (double)(SDL_GetTicksNS() - start) / 1.0e6;
	return true;
}

void SdlGpuFrame::Take_Timing(double & flush_ms, double & fence_ms, unsigned int & flushes, double & acquire_ms,
	double & offscreen_ms, unsigned int & not_visible, unsigned int & not_shown)
{
	flush_ms = FlushMs;
	fence_ms = FenceMs;
	flushes = Flushes;
	acquire_ms = AcquireMs;
	offscreen_ms = OffscreenMs;
	not_visible = NotVisible;
	NotVisible = 0;
	not_shown = NotShown;
	NotShown = 0;
	OffscreenMs = 0.0;
	FlushMs = 0.0;
	FenceMs = 0.0;
	Flushes = 0;
	AcquireMs = 0.0;
}

SDL_GPUGraphicsPipeline * SdlGpuFrame::Gamma_Pipeline(unsigned int format)
{
	if (GammaPipeline != NULL && GammaPipelineFormat == format) {
		return GammaPipeline;
	}
	std::string log;
	if (GammaVertex == NULL) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(GAMMA_VERTEX_HLSL, true, spirv, log)
			|| (GammaVertex = SDL3_Create_Shader(GpuDevice, spirv, true, log)) == NULL) {
			fprintf(stderr, "SdlGpuFrame: the gamma pass's vertex program: %s\n", log.c_str());
			return NULL;
		}
	}
	if (GammaPixel == NULL) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(GAMMA_PIXEL_HLSL, false, spirv, log)
			|| (GammaPixel = SDL3_Create_Shader(GpuDevice, spirv, false, log)) == NULL) {
			fprintf(stderr, "SdlGpuFrame: the gamma pass's pixel program: %s\n", log.c_str());
			return NULL;
		}
	}
	if (GammaPipeline != NULL) {
		SDL_ReleaseGPUGraphicsPipeline(GpuDevice, GammaPipeline);
		GammaPipeline = NULL;
	}
	SDL_GPUColorTargetDescription target;
	SDL_zero(target);
	target.format = (SDL_GPUTextureFormat)format;
	SDL_GPUGraphicsPipelineCreateInfo info;
	SDL_zero(info);
	info.vertex_shader = GammaVertex;
	info.fragment_shader = GammaPixel;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	GammaPipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice, &info);
	GammaPipelineFormat = format;
	if (GammaPipeline == NULL) {
		fprintf(stderr, "SdlGpuFrame: the gamma pass's pipeline: %s\n", SDL_GetError());
	}
	return GammaPipeline;
}

// The three ramps as a 256 x 1 R16G16B16A16 texture, uploaded only when they change.
bool SdlGpuFrame::Upload_Ramp(SDL_GPUCommandBuffer * commands, const uint16_t (*ramp)[256])
{
	std::vector<uint16_t> texels(256 * 4);
	for (int index = 0; index < 256; ++index) {
		texels[index * 4 + 0] = ramp[0][index];
		texels[index * 4 + 1] = ramp[1][index];
		texels[index * 4 + 2] = ramp[2][index];
		texels[index * 4 + 3] = 0xFFFF;
	}
	if (RampTexture != NULL && texels == UploadedRamp) {
		return true;
	}
	if (RampTexture == NULL) {
		SDL_GPUTextureCreateInfo info;
		SDL_zero(info);
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = 256;
		info.height = 1;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		RampTexture = SDL_CreateGPUTexture(GpuDevice, &info);
		SDL_GPUSamplerCreateInfo sampler;
		SDL_zero(sampler);
		sampler.min_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		PointSampler = SDL_CreateGPUSampler(GpuDevice, &sampler);
		if (RampTexture == NULL || PointSampler == NULL) {
			return false;
		}
	}
	const Uint32 bytes = (Uint32)(texels.size() * sizeof(uint16_t));
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
	if (mapped == NULL) {
		SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
		return false;
	}
	memcpy(mapped, &texels[0], bytes);
	SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureTransferInfo source;
	SDL_zero(source);
	source.transfer_buffer = transfer;
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = RampTexture;
	region.w = 256;
	region.h = 1;
	region.d = 1;
	SDL_UploadToGPUTexture(copy, &source, &region, false);
	SDL_EndGPUCopyPass(copy);
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	UploadedRamp = texels;
	return true;
}

bool SdlGpuFrame::Present_Into(SDL_GPUCommandBuffer * commands, SDL_GPUTexture * target, unsigned int width,
	unsigned int height, unsigned int format, const uint16_t (*ramp)[256])
{
	if (Sdl_Gamma_Is_Identity(ramp)) {
		SDL_GPUBlitInfo blit;
		SDL_zero(blit);
		blit.source.texture = BackBuffer;
		blit.source.w = BackWidth;
		blit.source.h = BackHeight;
		blit.destination.texture = target;
		blit.destination.w = width;
		blit.destination.h = height;
		blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
		blit.filter = SDL_GPU_FILTER_LINEAR;
		SDL_BlitGPUTexture(commands, &blit);
		return true;
	}
	SDL_GPUGraphicsPipeline * pipeline = Gamma_Pipeline(format);
	if (pipeline == NULL || !Upload_Ramp(commands, ramp)) {
		return false;
	}
	SDL_GPUColorTargetInfo colour;
	SDL_zero(colour);
	colour.texture = target;
	colour.load_op = SDL_GPU_LOADOP_DONT_CARE;
	colour.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass * pass = SDL_BeginGPURenderPass(commands, &colour, 1, NULL);
	if (pass == NULL) {
		return false;
	}
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	SDL_GPUTextureSamplerBinding bindings[2];
	bindings[0].texture = BackBuffer;
	bindings[0].sampler = PointSampler;
	bindings[1].texture = RampTexture;
	bindings[1].sampler = PointSampler;
	SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	return true;
}

// The back buffer into the front copy, after the batch and before the picture goes out.
bool SdlGpuFrame::Copy_To_Front(SDL_GPUCommandBuffer * commands)
{
	SDL_GPUBlitInfo blit;
	SDL_zero(blit);
	blit.source.texture = BackBuffer;
	blit.source.w = BackWidth;
	blit.source.h = BackHeight;
	blit.destination.texture = FrontCopy;
	blit.destination.w = BackWidth;
	blit.destination.h = BackHeight;
	blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
	blit.filter = SDL_GPU_FILTER_NEAREST;
	SDL_BlitGPUTexture(commands, &blit);
	return true;
}

bool SdlGpuFrame::Present(const uint16_t (*ramp)[256])
{
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	bool ok = Record_Batch(commands) && Copy_To_Front(commands);
	if (Window != NULL) {
		SDL_GPUTexture * swapchain = NULL;
		Uint32 width = 0;
		Uint32 height = 0;
		// Asked before the acquire: SDL keeps these flags from the window's own events (on macOS, OCCLUDED from
		// NSWindow's occlusion state), and a window in any of them is not paced by the display.
		if ((SDL_GetWindowFlags(Window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED | SDL_WINDOW_MINIMIZED)) != 0) {
			++NotVisible;
			++NotVisibleTotal;
		}
		// No texture (a minimised window) is not a failure: there is nothing to show this frame.
		const Uint64 acquire_start = SDL_GetTicksNS();
		const bool acquired = SDL_WaitAndAcquireGPUSwapchainTexture(commands, Window, &swapchain, &width, &height);
		AcquireMs += (double)(SDL_GetTicksNS() - acquire_start) / 1.0e6;
		if (acquired && swapchain != NULL) {
			if (width != ShownWidth || height != ShownHeight) {
				// What the picture is scaled to on its way out: on a Retina Mac, the window's pixels.
				fprintf(stderr, "SdlGpuFrame: the %ux%u back buffer is shown on a %ux%u swapchain\n", BackWidth, BackHeight,
					(unsigned)width, (unsigned)height);
				ShownWidth = width;
				ShownHeight = height;
			}
			ok = Present_Into(commands, swapchain, width, height, SDL_GetGPUSwapchainTextureFormat(GpuDevice, Window), ramp)
				&& ok;
		} else {
			// Nothing reaches the display and nothing waits for vsync, so this frame's time is not the
			// display's pacing: the timing report says how many there were.
			++NotShown;
			++NotShownTotal;
		}
	} else if (OffscreenPresents) {
		if (DisplayTexture == NULL) {
			SDL_GPUTextureCreateInfo info;
			SDL_zero(info);
			info.type = SDL_GPU_TEXTURETYPE_2D;
			info.format = BACK_BUFFER_FORMAT;
			info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
			info.width = BackWidth;
			info.height = BackHeight;
			info.layer_count_or_depth = 1;
			info.num_levels = 1;
			info.sample_count = SDL_GPU_SAMPLECOUNT_1;
			DisplayTexture = SDL_CreateGPUTexture(GpuDevice, &info);
		}
		if (DisplayTexture != NULL) {
			ok = Present_Into(commands, DisplayTexture, BackWidth, BackHeight, BACK_BUFFER_FORMAT, ramp) && ok;
		}
		const bool submitted = Submit_Offscreen(commands);
		End_Batch();
		return submitted && ok && DisplayTexture != NULL;
	}
	const bool submitted = Submit(commands);
	End_Batch();
	return submitted && ok;
}

bool SdlGpuFrame::Present_To(SDL_GPUTexture * target, unsigned int width, unsigned int height,
	const uint16_t (*ramp)[256])
{
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	bool ok = Record_Batch(commands) && Copy_To_Front(commands);
	ok = Present_Into(commands, target, width, height, BACK_BUFFER_FORMAT, ramp) && ok;
	const bool submitted = Submit(commands);
	End_Batch();
	return submitted && ok;
}

bool SdlGpuFrame::Upload_Back_Buffer(const std::vector<uint8_t> & bgra)
{
	const Uint32 bytes = BackWidth * BackHeight * 4;
	if (bgra.size() != bytes) {
		return false;
	}
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
	if (mapped == NULL) {
		SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
		return false;
	}
	memcpy(mapped, &bgra[0], bytes);
	SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureTransferInfo source;
	SDL_zero(source);
	source.transfer_buffer = transfer;
	source.pixels_per_row = BackWidth;
	source.rows_per_layer = BackHeight;
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = BackBuffer;
	region.w = BackWidth;
	region.h = BackHeight;
	region.d = 1;
	SDL_UploadToGPUTexture(copy, &source, &region, false);
	SDL_EndGPUCopyPass(copy);
	const bool ok = SDL_SubmitGPUCommandBuffer(commands);
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	return ok;
}

bool SdlGpuFrame::Read_Back(SDL_GPUTexture * texture, unsigned int width, unsigned int height, std::vector<uint8_t> & bgra)
{
	if (!Flush()) {
		return false;
	}
	const Uint32 bytes = width * height * 4;
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = texture;
	region.w = width;
	region.h = height;
	region.d = 1;
	SDL_GPUTextureTransferInfo destination;
	SDL_zero(destination);
	destination.transfer_buffer = transfer;
	destination.pixels_per_row = width;
	destination.rows_per_layer = height;
	SDL_DownloadFromGPUTexture(copy, &region, &destination);
	SDL_EndGPUCopyPass(copy);
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	bool ok = fence != NULL && SDL_WaitForGPUFences(GpuDevice, true, &fence, 1);
	if (fence != NULL) {
		SDL_ReleaseGPUFence(GpuDevice, fence);
	}
	if (ok) {
		const void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
		ok = mapped != NULL;
		if (ok) {
			bgra.assign((const uint8_t *)mapped, (const uint8_t *)mapped + bytes);
			SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
		}
	}
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	return ok;
}
