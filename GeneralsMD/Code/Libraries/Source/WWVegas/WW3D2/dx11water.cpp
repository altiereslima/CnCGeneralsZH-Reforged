/*
** The Modern water's half of DX11BackendClass: what a water draw binds beside the generated
** pipeline, and the GPU ocean its open sea is displaced by.  The SDL GPU device does the same in
** PosixDevice9Draw.cpp and SdlGpuFrame.cpp, and both read one text (engineshader.cpp,
** write_modern_water) and one constant block (Platform/WaterConstants.h).
**
** A Modern water draw differs from every other draw in five ways.  Its pixel half reads fifteen
** stages, where the backend mirrors four: t4 the shore's optical field, t5 a copy of the scene it
** refracts, t6 a copy of the scene's depth, t7 the bed's detail, t8 to t13 the ocean's six maps and
** t14 the vessels' foam.  It reads four constant blocks, the water's own and three of hull packets,
** none of which the combiners' block holds.  Over open sea its vertex half samples three of the
** ocean maps and the terrain, and reads the water's block at b1.  The ocean is rendered on the
** context first, which takes the targets away from the scene.  And a destination alpha feather
** becomes a plain alpha blend, because the shader already attenuates the bed it refracts.
**
** Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
*/

#include "dx11backend.h"
#include "dx11water.h"
#include "Platform/WaterConstants.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterFftSpectrum.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

namespace Spectrum = WaterFftSpectrum;

namespace {

template <class T> void drop(T *& object)
{
	if (object != NULL) {
		object->Release();
		object = NULL;
	}
}

// One render target the ocean draws into and samples back.
struct OceanSurface
{
	ID3D11Texture2D * Texture = NULL;
	ID3D11ShaderResourceView * View = NULL;
	ID3D11RenderTargetView * Target = NULL;
};

void drop(OceanSurface & surface)
{
	drop(surface.Target);
	drop(surface.View);
	drop(surface.Texture);
}

bool make(ID3D11Device * device, OceanSurface & surface, unsigned width, unsigned levels,
	bool target, const float * initial)
{
	D3D11_TEXTURE2D_DESC description;
	memset(&description, 0, sizeof(description));
	description.Width = width;
	description.Height = Spectrum::Resolution;
	description.MipLevels = levels;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	description.SampleDesc.Count = 1;
	description.Usage = target ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE | (target ? D3D11_BIND_RENDER_TARGET : 0);
	description.MiscFlags = levels > 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	D3D11_SUBRESOURCE_DATA data;
	data.pSysMem = initial;
	data.SysMemPitch = width * 4 * sizeof(float);
	data.SysMemSlicePitch = 0;
	return SUCCEEDED(device->CreateTexture2D(&description, initial != NULL ? &data : NULL, &surface.Texture))
		&& SUCCEEDED(device->CreateShaderResourceView(surface.Texture, NULL, &surface.View))
		&& (!target || SUCCEEDED(device->CreateRenderTargetView(surface.Texture, NULL, &surface.Target)));
}

ID3D11SamplerState * make_sampler(ID3D11Device * device, D3D11_FILTER filter,
	D3D11_TEXTURE_ADDRESS_MODE address, float most_detailed_levels)
{
	D3D11_SAMPLER_DESC description;
	memset(&description, 0, sizeof(description));
	description.Filter = filter;
	description.AddressU = address;
	description.AddressV = address;
	description.AddressW = address;
	description.ComparisonFunc = D3D11_COMPARISON_NEVER;
	description.MaxLOD = most_detailed_levels;
	ID3D11SamplerState * sampler = NULL;
	device->CreateSamplerState(&description, &sampler);
	return sampler;
}

bool upload(ID3D11DeviceContext * context, ID3D11Buffer * buffer, const void * bytes, unsigned size)
{
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return false;
	}
	memcpy(mapped.pData, bytes, size);
	context->Unmap(buffer, 0);
	return true;
}

ID3D11Buffer * make_constants(ID3D11Device * device, unsigned size)
{
	D3D11_BUFFER_DESC description;
	memset(&description, 0, sizeof(description));
	description.ByteWidth = size;
	description.Usage = D3D11_USAGE_DYNAMIC;
	description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	ID3D11Buffer * buffer = NULL;
	device->CreateBuffer(&description, NULL, &buffer);
	return buffer;
}

// The texture behind a view, for its size and whether it is a render target.
bool view_texture(ID3D11ShaderResourceView * view, D3D11_TEXTURE2D_DESC & description)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC how;
	view->GetDesc(&how);
	if (how.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) {
		return false;
	}
	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	static_cast<ID3D11Texture2D *>(resource)->GetDesc(&description);
	resource->Release();
	return true;
}

} // namespace

bool DX11Water_Ocean_Vertex_Program(const VertexPipelineDescription & description, std::string & hlsl)
{
	return !description.LightingEnabled
		&& VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, hlsl)
		&& WaterFftSampling::WrapVertex(hlsl);
}

std::string DX11Water_Ocean_Pass_Program(unsigned pass)
{
	std::string source = pass == 0 ? WaterFftSpectrum::SpectrumHlsl()
		: pass == 1 ? WaterFftSpectrum::ButterflyHlsl() : WaterFftSpectrum::PostHlsl();
	// Register spaces are shader model 5.1; the passes bind nothing else, so plain slots are the same.
	static const char * const SPACES[] = { ", space2)", ", space3)" };
	for (const char * space : SPACES) {
		const size_t length = strlen(space);
		for (size_t at = source.find(space); at != std::string::npos; at = source.find(space, at)) {
			source.replace(at, length, ")");
		}
	}
	return source;
}

const char * DX11Water_Ocean_Pass_Vertex_Program()
{
	return WaterFftSpectrum::VertexHlsl();
}

struct DX11BackendClass::WaterResources
{
	float Registers[DX11_WATER_REGISTERS][4] = {};
	// What the engine bound at the stages past the backend's four, as it bound them.
	ID3D11ShaderResourceView * Textures[DX11_WATER_STAGES] = {};

	// Made with the first draw; Refused for good when the device would not.
	bool Made = false;
	bool Refused = false;
	ID3D11Texture2D * WhiteTexture = NULL;
	ID3D11ShaderResourceView * White = NULL;
	ID3D11SamplerState * ClampLinear = NULL;
	ID3D11SamplerState * ClampPoint = NULL;
	ID3D11SamplerState * WrapLinear = NULL;
	ID3D11Buffer * Constants = NULL;
	ID3D11Buffer * Packets[WaterHullVisualLayout::BlockCount] = {};
	float HeldPackets[WaterHullVisualLayout::PacketRegisters][4] = {};
	bool PacketsHeld = false;

	// The scene before the first water draw of a frame into a target, and its depth.
	ID3D11Texture2D * Colour = NULL;
	ID3D11ShaderResourceView * ColourView = NULL;
	ID3D11Resource * ColourSource = NULL;
	bool ColourStale = true;
	unsigned TargetWidth = 1;
	unsigned TargetHeight = 1;
	ID3D11Texture2D * Depth = NULL;
	ID3D11ShaderResourceView * DepthView = NULL;
	ID3D11Resource * DepthSource = NULL;

	// The ocean: three initial spectra, two sets of six to ping-pong the FFT through, the six maps
	// the water samples with their mips, and the butterfly table.
	bool OceanFailed = false;
	bool OceanReady = false;
	float OceanTime = 0.0f;
	std::string OceanStatus = "not asked for";
	OceanSurface Initial[Spectrum::Cascades];
	OceanSurface Ping[2][Spectrum::Maps];
	OceanSurface Fields[Spectrum::Maps];
	OceanSurface Twiddles;
	ID3D11VertexShader * OceanVertex = NULL;
	ID3D11PixelShader * OceanPixels[DX11_WATER_OCEAN_PASSES] = {};
	ID3D11Buffer * OceanConstants = NULL;
	ID3D11SamplerState * OceanPoint = NULL;
	ID3D11SamplerState * OceanLinear = NULL;
	ID3D11RasterizerState * NoCull = NULL;
	std::map<std::string, ID3D11VertexShader *> VertexPrograms;

	// The draw in hand.
	bool Fft = false;
	ID3D11VertexShader * DrawVertex = NULL;
	bool HaveColour = false;
	bool HaveDepth = false;

	unsigned long long Draws = 0;
	unsigned long long DisplacedDraws = 0;
	unsigned long long OceanUpdates = 0;

	bool Make(ID3D11Device * device);
	bool Fail_Ocean(const char * reason);
	bool Make_Ocean(DX11BackendClass & backend, ID3D11Device * device);
	bool Prepare_Ocean(DX11BackendClass & backend, ID3D11Device * device, ID3D11DeviceContext * context,
		float time);
	void Ocean_Pass(ID3D11DeviceContext * context, unsigned which, OceanSurface * targets,
		ID3D11ShaderResourceView * const * sources, unsigned count, const float parameters[8]);
	void Take_Scene(DX11DeviceClass & device, ID3D11Resource * source, ID3D11RenderTargetView * view,
		bool scene_depth);
	void Release();
};

bool DX11BackendClass::WaterResources::Make(ID3D11Device * device)
{
	if (Made || Refused) {
		return Made;
	}
	D3D11_TEXTURE2D_DESC description;
	memset(&description, 0, sizeof(description));
	description.Width = 1;
	description.Height = 1;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	const unsigned white = 0xffffffff;
	D3D11_SUBRESOURCE_DATA data = { &white, 4, 0 };
	device->CreateTexture2D(&description, &data, &WhiteTexture);
	if (WhiteTexture != NULL) {
		device->CreateShaderResourceView(WhiteTexture, NULL, &White);
	}
	ClampLinear = make_sampler(device, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f);
	ClampPoint = make_sampler(device, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f);
	WrapLinear = make_sampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, FLT_MAX);
	Constants = make_constants(device, sizeof(WaterConstants::VisualBlock));
	bool packets = true;
	for (unsigned block = 0; block < WaterHullVisualLayout::BlockCount; ++block) {
		Packets[block] = make_constants(device, WaterHullVisualLayout::BlockRegisters(block) * 16);
		packets = packets && Packets[block] != NULL;
	}
	Made = White != NULL && ClampLinear != NULL && ClampPoint != NULL && WrapLinear != NULL
		&& Constants != NULL && packets;
	Refused = !Made;
	return Made;
}

bool DX11BackendClass::WaterResources::Fail_Ocean(const char * reason)
{
	OceanFailed = true;
	OceanReady = false;
	OceanStatus = std::string("analytic: ") + reason;
	return false;
}

bool DX11BackendClass::WaterResources::Make_Ocean(DX11BackendClass & backend, ID3D11Device * device)
{
	// Mip generation is what keeps a distant swell from shimmering; without it the analytic surface
	// stands in, as it does on the SDL device without float targets.
	UINT support = 0;
	const UINT wanted = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET
		| D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
	if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_R32G32B32A32_FLOAT, &support)) || (support & wanted) != wanted) {
		return Fail_Ocean("no filtered, mip-generated RGBA32F target");
	}
	const unsigned side = Spectrum::Resolution;
	const std::vector<float> initial = Spectrum::InitialSpectrum();
	const std::vector<float> butterfly = Spectrum::Butterfly();
	bool made = true;
	for (unsigned cascade = 0; cascade < Spectrum::Cascades; ++cascade) {
		made = made && make(device, Initial[cascade], side, 1, false, &initial[cascade * side * side * 4]);
	}
	for (unsigned set = 0; set < 2; ++set) {
		for (unsigned map = 0; map < Spectrum::Maps; ++map) {
			made = made && make(device, Ping[set][map], side, 1, true, NULL);
		}
	}
	for (unsigned map = 0; map < Spectrum::Maps; ++map) {
		made = made && make(device, Fields[map], side, Spectrum::Phases + 1, true, NULL);
	}
	made = made && make(device, Twiddles, Spectrum::Phases, 1, false, &butterfly[0]);
	if (!made) {
		return Fail_Ocean("the device refused the ocean's textures");
	}

	std::vector<unsigned char> code;
	if (!backend.Compile_Program(DX11Water_Ocean_Pass_Vertex_Program(), "ocean vertex", "vs_5_0", code)
		|| FAILED(device->CreateVertexShader(&code[0], code.size(), NULL, &OceanVertex))) {
		return Fail_Ocean("the ocean's vertex pass would not compile");
	}
	for (unsigned pass = 0; pass < DX11_WATER_OCEAN_PASSES; ++pass) {
		if (!backend.Compile_Program(DX11Water_Ocean_Pass_Program(pass), "ocean pass", "ps_5_0", code)
			|| FAILED(device->CreatePixelShader(&code[0], code.size(), NULL, &OceanPixels[pass]))) {
			return Fail_Ocean("an ocean pass would not compile");
		}
	}
	OceanConstants = make_constants(device, 8 * sizeof(float));
	OceanPoint = make_sampler(device, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f);
	OceanLinear = make_sampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP,
		float(Spectrum::Phases));
	D3D11_RASTERIZER_DESC rasterizer;
	memset(&rasterizer, 0, sizeof(rasterizer));
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	device->CreateRasterizerState(&rasterizer, &NoCull);
	if (OceanConstants == NULL || OceanPoint == NULL || OceanLinear == NULL || NoCull == NULL) {
		return Fail_Ocean("the device refused the ocean's states");
	}
	OceanStatus = "GPU field";
	return true;
}

void DX11BackendClass::WaterResources::Ocean_Pass(ID3D11DeviceContext * context, unsigned which,
	OceanSurface * targets, ID3D11ShaderResourceView * const * sources, unsigned count, const float parameters[8])
{
	// The sources of this pass were the last one's targets: they are let go as targets before they
	// are bound to be read, or the runtime unbinds them as textures and the pass reads zero.
	ID3D11ShaderResourceView * const none[Spectrum::Maps + 1] = {};
	context->PSSetShaderResources(0, Spectrum::Maps + 1, none);
	ID3D11RenderTargetView * views[Spectrum::Maps];
	for (unsigned map = 0; map < Spectrum::Maps; ++map) {
		views[map] = targets[map].Target;
	}
	context->OMSetRenderTargets(Spectrum::Maps, views, NULL);
	context->PSSetShaderResources(0, count, sources);
	upload(context, OceanConstants, parameters, 8 * sizeof(float));
	context->PSSetShader(OceanPixels[which], NULL, 0);
	context->Draw(3, 0);
}

bool DX11BackendClass::WaterResources::Prepare_Ocean(DX11BackendClass & backend, ID3D11Device * device,
	ID3D11DeviceContext * context, float time)
{
	const unsigned maps = Spectrum::Maps;
	if (!(time == time) || OceanFailed) {
		return false;
	}
	if (OceanReady && time == OceanTime) {
		return true;
	}
	if (OceanVertex == NULL && !Make_Ocean(backend, device)) {
		return false;
	}

	ID3D11RenderTargetView * saved_target = NULL;
	ID3D11DepthStencilView * saved_depth = NULL;
	context->OMGetRenderTargets(1, &saved_target, &saved_depth);
	D3D11_VIEWPORT saved_viewport;
	UINT viewports = 1;
	context->RSGetViewports(&viewports, &saved_viewport);

	context->IASetInputLayout(NULL);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(OceanVertex, NULL, 0);
	context->RSSetState(NoCull);
	context->OMSetBlendState(NULL, NULL, 0xffffffff);
	context->OMSetDepthStencilState(NULL, 0);
	ID3D11SamplerState * points[maps + 1];
	for (unsigned slot = 0; slot <= maps; ++slot) {
		points[slot] = OceanPoint;
	}
	context->PSSetSamplers(0, maps + 1, points);
	context->PSSetConstantBuffers(0, 1, &OceanConstants);
	const float side = float(Spectrum::Resolution);
	const D3D11_VIEWPORT field = { 0.0f, 0.0f, side, side, 0.0f, 1.0f };
	context->RSSetViewports(1, &field);

	float parameters[8] = { side, time, 0.0f, 0.0f, Spectrum::Sizes[0], Spectrum::Sizes[1], Spectrum::Sizes[2], 0.0f };
	ID3D11ShaderResourceView * sources[maps + 1];
	for (unsigned cascade = 0; cascade < Spectrum::Cascades; ++cascade) {
		sources[cascade] = Initial[cascade].View;
	}
	Ocean_Pass(context, 0, Ping[0], sources, Spectrum::Cascades, parameters);
	unsigned ping = 0;
	for (unsigned axis = 0; axis < 2; ++axis) {
		for (unsigned phase = 0; phase < Spectrum::Phases; ++phase) {
			for (unsigned map = 0; map < maps; ++map) {
				sources[map] = Ping[ping][map].View;
			}
			sources[maps] = Twiddles.View;
			parameters[2] = float(phase);
			parameters[3] = float(axis);
			Ocean_Pass(context, 1, Ping[1 - ping], sources, maps + 1, parameters);
			ping = 1 - ping;
		}
	}
	for (unsigned map = 0; map < maps; ++map) {
		sources[map] = Ping[ping][map].View;
	}
	Ocean_Pass(context, 2, Fields, sources, maps, parameters);

	ID3D11ShaderResourceView * const none[maps + 1] = {};
	context->PSSetShaderResources(0, maps + 1, none);
	context->OMSetRenderTargets(1, &saved_target, saved_depth);
	for (unsigned map = 0; map < maps; ++map) {
		context->GenerateMips(Fields[map].View);
	}
	if (viewports == 1) {
		context->RSSetViewports(1, &saved_viewport);
	}
	drop(saved_target);
	drop(saved_depth);
	backend.Forget_Bindings();

	OceanTime = time;
	OceanReady = true;
	++OceanUpdates;
	return true;
}

/** The scene the water refracts, copied at the first water draw of a frame into a target, and the
		depth with it when the draw tests against the scene's own: later water draws of the frame see
		the bed and not the water already laid over it, as on the SDL device. */
void DX11BackendClass::WaterResources::Take_Scene(DX11DeviceClass & device, ID3D11Resource * source,
	ID3D11RenderTargetView * view, bool scene_depth)
{
	if (!ColourStale && source == ColourSource) {
		HaveColour = ColourView != NULL;
		HaveDepth = scene_depth && DepthView != NULL && DepthSource == source;
		return;
	}
	ID3D11Device * const d3d = device.Get_Device();
	ID3D11DeviceContext * const context = device.Get_Context();
	D3D11_TEXTURE2D_DESC description;
	static_cast<ID3D11Texture2D *>(source)->GetDesc(&description);
	if (Colour != NULL) {
		D3D11_TEXTURE2D_DESC held;
		Colour->GetDesc(&held);
		if (held.Width != description.Width || held.Height != description.Height || held.Format != description.Format) {
			drop(ColourView);
			drop(Colour);
		}
	}
	if (Colour == NULL && description.SampleDesc.Count == 1) {
		D3D11_RENDER_TARGET_VIEW_DESC written;
		view->GetDesc(&written);
		D3D11_TEXTURE2D_DESC copy = description;
		copy.MipLevels = 1;
		copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copy.MiscFlags = 0;
		copy.Usage = D3D11_USAGE_DEFAULT;
		copy.CPUAccessFlags = 0;
		D3D11_SHADER_RESOURCE_VIEW_DESC read;
		memset(&read, 0, sizeof(read));
		read.Format = written.Format;
		read.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		read.Texture2D.MipLevels = 1;
		if (FAILED(d3d->CreateTexture2D(&copy, NULL, &Colour))
			|| FAILED(d3d->CreateShaderResourceView(Colour, &read, &ColourView))) {
			drop(ColourView);
			drop(Colour);
		}
	}
	if (Colour != NULL && description.MipLevels == 1) {
		context->CopyResource(Colour, source);
	}
	else if (Colour != NULL) {
		context->CopySubresourceRegion(Colour, 0, 0, 0, 0, source, 0, NULL);
	}
	TargetWidth = description.Width;
	TargetHeight = description.Height;
	ColourSource = source;
	ColourStale = false;
	HaveColour = ColourView != NULL;

	DepthSource = NULL;
	ID3D11ShaderResourceView * const depth_view = device.Get_Depth_Texture();
	if (scene_depth && depth_view != NULL) {
		ID3D11Resource * depth = NULL;
		depth_view->GetResource(&depth);
		D3D11_TEXTURE2D_DESC depth_description;
		static_cast<ID3D11Texture2D *>(depth)->GetDesc(&depth_description);
		if (Depth != NULL) {
			D3D11_TEXTURE2D_DESC held;
			Depth->GetDesc(&held);
			if (held.Width != depth_description.Width || held.Height != depth_description.Height
					|| held.Format != depth_description.Format) {
				drop(DepthView);
				drop(Depth);
			}
		}
		if (Depth == NULL) {
			D3D11_TEXTURE2D_DESC copy = depth_description;
			copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			copy.MiscFlags = 0;
			copy.Usage = D3D11_USAGE_DEFAULT;
			copy.CPUAccessFlags = 0;
			D3D11_SHADER_RESOURCE_VIEW_DESC read;
			depth_view->GetDesc(&read);
			if (FAILED(d3d->CreateTexture2D(&copy, NULL, &Depth))
				|| FAILED(d3d->CreateShaderResourceView(Depth, &read, &DepthView))) {
				drop(DepthView);
				drop(Depth);
			}
		}
		if (Depth != NULL) {
			context->CopyResource(Depth, depth);
			DepthSource = source;
		}
		depth->Release();
	}
	HaveDepth = DepthSource != NULL;
}

void DX11BackendClass::WaterResources::Release()
{
	drop(White);
	drop(WhiteTexture);
	drop(ClampLinear);
	drop(ClampPoint);
	drop(WrapLinear);
	drop(Constants);
	for (ID3D11Buffer *& buffer : Packets) {
		drop(buffer);
	}
	PacketsHeld = false;
	drop(ColourView);
	drop(Colour);
	drop(DepthView);
	drop(Depth);
	ColourSource = NULL;
	DepthSource = NULL;
	ColourStale = true;
	for (OceanSurface & surface : Initial) {
		drop(surface);
	}
	for (unsigned set = 0; set < 2; ++set) {
		for (OceanSurface & surface : Ping[set]) {
			drop(surface);
		}
	}
	for (OceanSurface & surface : Fields) {
		drop(surface);
	}
	drop(Twiddles);
	drop(OceanVertex);
	for (ID3D11PixelShader *& program : OceanPixels) {
		drop(program);
	}
	drop(OceanConstants);
	drop(OceanPoint);
	drop(OceanLinear);
	drop(NoCull);
	for (std::map<std::string, ID3D11VertexShader *>::iterator entry = VertexPrograms.begin();
			entry != VertexPrograms.end(); ++entry) {
		drop(entry->second);
	}
	VertexPrograms.clear();
	// A new device may take what this one refused.
	Made = false;
	Refused = false;
	OceanFailed = false;
	OceanReady = false;
	OceanStatus = "not asked for";
	DrawVertex = NULL;
	Fft = false;
	memset(Textures, 0, sizeof(Textures));
}

DX11BackendClass::WaterResources * DX11BackendClass::Create_Water()
{
	return new WaterResources;
}

void DX11BackendClass::Destroy_Water(WaterResources * water)
{
	delete water;
}

void DX11BackendClass::Set_Water_Constants(unsigned first_register, const float * values, unsigned count)
{
	memcpy(Water->Registers[first_register], values, sizeof(float) * 4 * count);
}

void DX11BackendClass::Set_Water_Texture(unsigned stage, ID3D11ShaderResourceView * texture)
{
	if (stage < DX11_WATER_STAGES) {
		Water->Textures[stage] = texture;
	}
}

void DX11BackendClass::Water_Scene_Changed(ID3D11Resource * target)
{
	// A filter target cleared mid-frame leaves the scene's copy as it was, as on the SDL device
	// (Invalidate_Water_Snapshots); clearing the copied target itself makes it old.
	// ponytail: one copy, keyed by its target; a frame drawing water into A, B, then A again takes A
	// a second time with the first water in it, where SDL keeps a copy per target.
	if (target == NULL || target == Water->ColourSource) {
		Water->ColourStale = true;
	}
}

void DX11BackendClass::Release_Water()
{
	Water->Release();
}

std::string DX11BackendClass::Water_Report() const
{
	char line[256];
	snprintf(line, sizeof(line), "%llu Modern draws, %llu displaced by the ocean (%llu field updates); ocean %s",
		Water->Draws, Water->DisplacedDraws, Water->OceanUpdates, Water->OceanStatus.c_str());
	return line;
}

bool DX11BackendClass::Prepare_Water()
{
	if (!Water->Make(Device->Get_Device())) {
		return false;
	}
	WaterResources & water = *Water;
	++water.Draws;
	water.Fft = false;
	water.DrawVertex = NULL;
	const float (*registers)[4] = water.Registers;
	// Open sea with its optical context and height correction is displaced on the GPU.  A sampled
	// ocean that cannot be made leaves the analytic CPU surface the patch was built with.
	if (PixelProgram == ENGINE_SHADER_WATER_TRAPEZOID && VertexProgram == ENGINE_SHADER_NONE
		&& RenderStates.Get_Render_State(D3DRS_LIGHTING) == FALSE
		&& registers[6][2] > 0.5f && registers[6][3] > 0.5f
		&& water.Prepare_Ocean(*this, Device->Get_Device(), Device->Get_Context(),
			registers[1][0] * WaterFftSampling::TimeScale)) {
		VertexPipelineDescription description;
		Build_Vertex_Description(description);
		const std::string key = VertexShader_Key(description);
		std::map<std::string, ID3D11VertexShader *>::iterator found = water.VertexPrograms.find(key);
		if (found == water.VertexPrograms.end()) {
			ID3D11VertexShader * program = NULL;
			std::string hlsl;
			std::vector<unsigned char> code;
			if (DX11Water_Ocean_Vertex_Program(description, hlsl)
				&& Compile_Program(hlsl, "water vertex", "vs_5_0", code)) {
				Device->Get_Device()->CreateVertexShader(&code[0], code.size(), NULL, &program);
			}
			found = water.VertexPrograms.insert(std::make_pair(key, program)).first;
		}
		water.DrawVertex = found->second;
		water.Fft = water.DrawVertex != NULL;
	}

	ID3D11RenderTargetView * const view = (CurrentTarget != NULL) ? CurrentTarget : Device->Get_Scene_View();
	ID3D11Resource * source = CurrentTargetResource;
	if (CurrentTarget == NULL) {
		view->GetResource(&source);
		source->Release();	// the device holds the scene target for as long as it lives
	}
	water.Take_Scene(*Device, source, view, CurrentTarget == NULL || CurrentDepth == Device->Get_Depth_Stencil_View());
	return true;
}

void DX11BackendClass::Bind_Water()
{
	WaterResources & water = *Water;
	ID3D11DeviceContext * const context = Device->Get_Context();
	const float (*registers)[4] = water.Registers;

	ID3D11ShaderResourceView * const history = water.Textures[14];
	D3D11_TEXTURE2D_DESC atlas;
	const bool foam = history != NULL && WaterConstants::FoamHistoryRegisters(registers)
		&& view_texture(history, atlas) && (atlas.BindFlags & D3D11_BIND_RENDER_TARGET) == 0
		&& WaterConstants::FoamHistoryAtlas(registers, atlas.Width, atlas.Height);

	const WaterConstants::Camera camera = { View, Projection, float(ViewportX), float(ViewportY),
		float(ViewportWidth), float(ViewportHeight), 0.0f, 1.0f };
	WaterConstants::Block block;
	WaterConstants::Fill(registers, camera, foam, block);
	RenderStates.Get_Texture_Factor(block.TextureFactor);
	const DWORD fog_colour = RenderStates.Get_Render_State(D3DRS_FOGCOLOR);
	block.FogColour[0] = float((fog_colour >> 16) & 0xff) / 255.0f;
	block.FogColour[1] = float((fog_colour >> 8) & 0xff) / 255.0f;
	block.FogColour[2] = float(fog_colour & 0xff) / 255.0f;
	block.FogColour[3] = float((fog_colour >> 24) & 0xff) / 255.0f;
	block.AlphaReference[0] = float(RenderStates.Get_Render_State(D3DRS_ALPHAREF) & 0xff);
	if (water.Fft && Textures[2] == NULL) {
		block.TerrainParameters[3] = 0.0f;
	}
	block.SceneParameters[0] = water.HaveColour ? 1.0f : 0.0f;
	block.SceneParameters[1] = 1.0f / float(water.TargetWidth);
	block.SceneParameters[2] = 1.0f / float(water.TargetHeight);
	block.SceneParameters[3] = water.HaveDepth ? 1.0f : 0.0f;
	block.FftParameters[0] = water.Fft ? 1.0f : 0.0f;
	WaterConstants::VisualBlock visual;
	WaterConstants::FillVisual(block, registers, visual);
	upload(context, water.Constants, &visual, sizeof(visual));
	const float (*packets)[4] = registers + WaterHullVisualLayout::PacketRegister;
	if (!water.PacketsHeld || memcmp(water.HeldPackets, packets, sizeof(water.HeldPackets)) != 0) {
		bool uploaded = true;
		for (unsigned index = 0; index < WaterHullVisualLayout::BlockCount; ++index) {
			uploaded = upload(context, water.Packets[index], packets + WaterHullVisualLayout::BlockOffset(index),
				WaterHullVisualLayout::BlockRegisters(index) * 16) && uploaded;
		}
		memcpy(water.HeldPackets, packets, sizeof(water.HeldPackets));
		water.PacketsHeld = uploaded;
	}
	ID3D11Buffer * const blocks[1 + WaterHullVisualLayout::BlockCount] = {
		water.Constants, water.Packets[0], water.Packets[1], water.Packets[2] };
	context->PSSetConstantBuffers(0, 1 + WaterHullVisualLayout::BlockCount, blocks);

	// A stage with nothing bound reads white, as on the SDL device, where Direct3D 11 reads zero.
	ID3D11ShaderResourceView * views[DX11_WATER_STAGES];
	ID3D11SamplerState * samplers[DX11_WATER_STAGES];
	for (unsigned stage = 0; stage < DX11_WATER_STAGES; ++stage) {
		const bool own = stage < DX11_BACKEND_TEXTURE_STAGES;
		ID3D11ShaderResourceView * const bound = own ? Bound.Textures[stage] : water.Textures[stage];
		views[stage] = bound != NULL ? bound : water.White;
		samplers[stage] = own ? Bound.Samplers[stage] : water.ClampLinear;
	}
	views[5] = water.HaveColour ? water.ColourView : water.White;
	views[6] = water.HaveDepth ? water.DepthView : water.White;
	views[14] = foam ? history : water.White;
	for (unsigned map = 0; map < Spectrum::Maps; ++map) {
		views[8 + map] = water.Fft ? water.Fields[map].View : water.White;
		samplers[8 + map] = water.Fft ? water.OceanLinear : water.ClampLinear;
	}
	// The terrain's heights, the scene's depth and the bed: the sampling the SDL device fixes for
	// them whatever the engine's stage states say.
	samplers[2] = water.ClampLinear;
	samplers[6] = water.ClampPoint;
	samplers[7] = water.WrapLinear;
	context->PSSetShaderResources(0, DX11_WATER_STAGES, views);
	context->PSSetSamplers(0, DX11_WATER_STAGES, samplers);

	if (water.Fft) {
		++water.DisplacedDraws;
		context->VSSetShader(water.DrawVertex, NULL, 0);
		context->VSSetConstantBuffers(1, 1, &water.Constants);
		ID3D11ShaderResourceView * const vertex_views[4] = { water.Fields[0].View, water.Fields[2].View,
			water.Fields[4].View, Textures[2] != NULL ? Textures[2] : water.White };
		ID3D11SamplerState * const vertex_samplers[4] = { water.OceanLinear, water.OceanLinear,
			water.OceanLinear, water.ClampLinear };
		context->VSSetShaderResources(0, 4, vertex_views);
		context->VSSetSamplers(0, 4, vertex_samplers);
	}

	// The shader refracts and attenuates the bed itself; the Classic feather through destination
	// alpha would blend the unattenuated bed in a second time.
	if (PixelProgram == ENGINE_SHADER_WATER_TRAPEZOID
		&& RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) != FALSE
		&& RenderStates.Get_Render_State(D3DRS_SRCBLEND) == D3DBLEND_DESTALPHA
		&& RenderStates.Get_Render_State(D3DRS_DESTBLEND) == D3DBLEND_INVDESTALPHA) {
		D3D11_BLEND_DESC description;
		RenderStates.Build_Blend_Description(description);
		description.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		description.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		BlendStateMap::const_iterator existing = BlendStates.find(description);
		ID3D11BlendState * state = (existing != BlendStates.end()) ? existing->second : NULL;
		if (state == NULL && SUCCEEDED(Device->Get_Device()->CreateBlendState(&description, &state))) {
			BlendStates[description] = state;
		}
		if (state != NULL) {
			context->OMSetBlendState(state, NULL, 0xffffffff);
		}
	}
}

void DX11BackendClass::Unbind_Water()
{
	ID3D11DeviceContext * const context = Device->Get_Context();
	ID3D11ShaderResourceView * const none[DX11_WATER_STAGES] = {};
	// Nothing the water read stays bound: the ocean's maps are drawn into again next frame.
	context->PSSetShaderResources(DX11_BACKEND_TEXTURE_STAGES, DX11_WATER_STAGES - DX11_BACKEND_TEXTURE_STAGES,
		none + DX11_BACKEND_TEXTURE_STAGES);
	if (Water->Fft) {
		context->VSSetShaderResources(0, 4, none);
	}
	Forget_Bindings();
}
