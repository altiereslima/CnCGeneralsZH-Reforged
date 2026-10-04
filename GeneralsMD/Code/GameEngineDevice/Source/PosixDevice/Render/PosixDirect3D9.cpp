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

// The adapter off Windows (decision 7): Direct3DCreate9, the one adapter, its modes and CreateDevice.
// The caps, the Check* answers and the identifier are in PosixD3D9Caps.cpp.  See PosixDevice9.h.

#include "PosixDevice9.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

// DX8Wrapper::Init calls this directly off Windows, where Windows loads d3d9.dll and looks it up.
IDirect3D9 * Direct3DCreate9(unsigned int sdk_version)
{
	if (sdk_version != D3D_SDK_VERSION) {
		fprintf(stderr, "Direct3DCreate9: SDK version %u asked for, %u built\n", sdk_version, (unsigned)D3D_SDK_VERSION);
		return NULL;
	}
	return new PosixDirect3D9();
}

PosixDirect3D9::PosixDirect3D9()
{
}

unsigned int PosixDirect3D9::GetAdapterCount()
{
	return 1;
}

// The display modes the options screen offers and Set_Render_Device checks a full-screen request
// against.  Fixed until the window (C2) and A3's swap chain report what the display really has; a
// windowed request, which is what -headless makes, is not checked against this list.
struct PosixMode
{
	unsigned int Width;
	unsigned int Height;
};

static const PosixMode MODES[] =
{
	{  640,  480 }, {  800,  600 }, { 1024,  768 }, { 1152,  864 }, { 1280,  720 }, { 1280,  800 },
	{ 1280, 1024 }, { 1366,  768 }, { 1440,  900 }, { 1600,  900 }, { 1680, 1050 }, { 1920, 1080 },
	{ 1920, 1200 }, { 2560, 1440 }
};
static const unsigned int MODE_COUNT = sizeof(MODES) / sizeof(MODES[0]);
static const unsigned int REFRESH_RATE = 60;

static bool mode_format(D3DFORMAT format)
{
	// The two formats DX8Wrapper enumerates: 32-bit, and the 16-bit it retries with.
	return format == D3DFMT_X8R8G8B8 || format == D3DFMT_R5G6B5;
}

unsigned int PosixDirect3D9::GetAdapterModeCount(unsigned int adapter, D3DFORMAT format)
{
	return (adapter == 0 && mode_format(format)) ? MODE_COUNT : 0;
}

RenderResult PosixDirect3D9::EnumAdapterModes(unsigned int adapter, D3DFORMAT format, unsigned int mode,
	D3DDISPLAYMODE *display_mode)
{
	if (adapter != 0 || !mode_format(format) || mode >= MODE_COUNT || display_mode == NULL) {
		return D3DERR_INVALIDCALL;
	}
	display_mode->Width = MODES[mode].Width;
	display_mode->Height = MODES[mode].Height;
	display_mode->RefreshRate = REFRESH_RATE;
	display_mode->Format = format;
	return D3D_OK;
}

RenderResult PosixDirect3D9::GetAdapterDisplayMode(unsigned int adapter, D3DDISPLAYMODE *mode)
{
	if (adapter != 0 || mode == NULL) {
		return D3DERR_INVALIDCALL;
	}
	// The desktop: a windowed back buffer takes its format.
	mode->Width = 1920;
	mode->Height = 1080;
	mode->RefreshRate = REFRESH_RATE;
	mode->Format = D3DFMT_X8R8G8B8;
	return D3D_OK;
}

RenderResult PosixDirect3D9::CreateDevice(unsigned int adapter, D3DDEVTYPE type, RenderWindow focus_window,
	RenderUInt32, D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice9 **device)
{
	if (device == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*device = NULL;
	if (adapter != 0 || type != D3DDEVTYPE_HAL || parameters == NULL) {
		return D3DERR_INVALIDCALL;
	}
	// D3D9 draws into hDeviceWindow, or the focus window when that is null.  Both null is -headless.
	RenderWindow window = parameters->hDeviceWindow != NULL ? parameters->hDeviceWindow : focus_window;

	// A windowed back buffer of 0 x 0 takes the window's size; with no window there is none to take, so
	// it is refused rather than guessed.
	if (parameters->BackBufferWidth == 0 || parameters->BackBufferHeight == 0) {
		fprintf(stderr, "CreateDevice: a back buffer of %u x %u; this device needs its size given\n",
			parameters->BackBufferWidth, parameters->BackBufferHeight);
		return D3DERR_INVALIDCALL;
	}

	PosixDevice9 *created = new PosixDevice9(this, window, *parameters);
	// -offscreen (SdlGameEngine::startOffscreen): no window, and still a GPU frame, drawn into its own target
	// and "presented" into a texture of its own.  Without the hint no window is -headless: no GPU device.
	const bool offscreen = window == NULL && SDL_GetHintBoolean("ZH_OFFSCREEN_FRAMES", false);
	RenderResult result = created->Create_Gpu_Frame(offscreen);
	if (Render_Succeeded(result) && offscreen) {
		const char *hz = SDL_GetHint("ZH_OFFSCREEN_HZ");
		created->Present_Offscreen(hz != NULL ? (unsigned int)strtoul(hz, NULL, 10) : 0);
	}
	if (Render_Succeeded(result)) {
		result = created->Create_Implicit_Surfaces();
	}
	if (Render_Failed(result)) {
		created->Release();
		return result;
	}
	*device = created;
	return D3D_OK;
}
