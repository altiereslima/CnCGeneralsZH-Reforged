/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// D3DX9 for the native Direct3D 9 renderer.
//
// The Windows SDK ships d3d9.h and d3d9.lib but no D3DX9: that library only ever
// came with the DirectX SDK, which was retired in 2012.  Seventeen of its entry points
// are still wanted, nine here and eight more in d3dx9math.h, so they are bound out of
// d3dx9_43.dll at startup, which is what d3d8to9 does today (d3d8to9.cpp:224) and
// therefore a dependency this fork already ships with rather than a new one.
//
// The names match the D3DX9 entry points so the call sites in dx8wrapper.cpp,
// TerrainTex.cpp and W3DWater.cpp read the way they always did.  Every pointer is
// null until Bind_D3DX9_Runtime succeeds, so nothing may call one before that.

#ifndef D3DX9RUNTIME_H
#define D3DX9RUNTIME_H

#include <d3d9.h>

#if defined(_WIN32)

// D3DX filter and default values, from the DirectX SDK's d3dx9tex.h.  They are plain
// constants: the DLL reads them, it does not define them.
#define D3DX_DEFAULT			((UINT)-1)
#define D3DX_FILTER_NONE		(1 << 0)
#define D3DX_FILTER_POINT		(2 << 0)
#define D3DX_FILTER_LINEAR		(3 << 0)
#define D3DX_FILTER_TRIANGLE	(4 << 0)
#define D3DX_FILTER_BOX			(5 << 0)

typedef interface ID3DXBuffer * LPD3DXBUFFER;
typedef interface ID3DXInclude * LPD3DXINCLUDE;

DECLARE_INTERFACE_(ID3DXBuffer, IUnknown)
{
	STDMETHOD(QueryInterface)(THIS_ REFIID iid, LPVOID * ppv) PURE;
	STDMETHOD_(ULONG, AddRef)(THIS) PURE;
	STDMETHOD_(ULONG, Release)(THIS) PURE;
	STDMETHOD_(LPVOID, GetBufferPointer)(THIS) PURE;
	STDMETHOD_(DWORD, GetBufferSize)(THIS) PURE;
};

struct D3DXMACRO
{
	LPCSTR Name;
	LPCSTR Definition;
};

struct D3DXIMAGE_INFO
{
	UINT Width;
	UINT Height;
	UINT Depth;
	UINT MipLevels;
	D3DFORMAT Format;
	D3DRESOURCETYPE ResourceType;
	DWORD ImageFileFormat;
};

typedef HRESULT (WINAPI * D3DXAssembleShaderFunction)(LPCSTR source, UINT source_length,
	const D3DXMACRO * defines, LPD3DXINCLUDE include, DWORD flags,
	LPD3DXBUFFER * shader, LPD3DXBUFFER * errors);

// The HLSL compiler.  ffshader.cpp writes the fixed-function stages out as HLSL and this is what
// turns that into something a device will take; the constant table is not asked for, because the
// generated shader's registers are fixed by the generator rather than looked up.
typedef HRESULT (WINAPI * D3DXCompileShaderFunction)(LPCSTR source, UINT source_length,
	const D3DXMACRO * defines, LPD3DXINCLUDE include, LPCSTR entry_point, LPCSTR profile,
	DWORD flags, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors, void ** constant_table);

typedef HRESULT (WINAPI * D3DXDisassembleShaderFunction)(const DWORD * shader, BOOL colour_code,
	LPCSTR comments, LPD3DXBUFFER * disassembly);

typedef HRESULT (WINAPI * D3DXCreateTextureFunction)(LPDIRECT3DDEVICE9 device,
	UINT width, UINT height, UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
	LPDIRECT3DTEXTURE9 * texture);

typedef HRESULT (WINAPI * D3DXCreateCubeTextureFunction)(LPDIRECT3DDEVICE9 device,
	UINT edge_length, UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
	LPDIRECT3DCUBETEXTURE9 * texture);

typedef HRESULT (WINAPI * D3DXCreateVolumeTextureFunction)(LPDIRECT3DDEVICE9 device,
	UINT width, UINT height, UINT depth, UINT mip_levels, DWORD usage, D3DFORMAT format,
	D3DPOOL pool, LPDIRECT3DVOLUMETEXTURE9 * texture);

typedef HRESULT (WINAPI * D3DXCreateTextureFromFileExFunction)(LPDIRECT3DDEVICE9 device,
	LPCSTR file_name, UINT width, UINT height, UINT mip_levels, DWORD usage, D3DFORMAT format,
	D3DPOOL pool, DWORD filter, DWORD mip_filter, D3DCOLOR colour_key, D3DXIMAGE_INFO * info,
	PALETTEENTRY * palette, LPDIRECT3DTEXTURE9 * texture);

typedef HRESULT (WINAPI * D3DXFilterTextureFunction)(LPDIRECT3DBASETEXTURE9 texture,
	const PALETTEENTRY * palette, UINT source_level, DWORD filter);

typedef HRESULT (WINAPI * D3DXLoadSurfaceFromSurfaceFunction)(LPDIRECT3DSURFACE9 destination,
	const PALETTEENTRY * destination_palette, const RECT * destination_rect,
	LPDIRECT3DSURFACE9 source, const PALETTEENTRY * source_palette, const RECT * source_rect,
	DWORD filter, D3DCOLOR colour_key);

typedef UINT (WINAPI * D3DXGetFVFVertexSizeFunction)(DWORD fvf);

extern D3DXAssembleShaderFunction			D3DXAssembleShader;
extern D3DXCompileShaderFunction			D3DXCompileShader;
extern D3DXDisassembleShaderFunction		D3DXDisassembleShader;
extern D3DXCreateTextureFunction			D3DXCreateTexture;
extern D3DXCreateCubeTextureFunction		D3DXCreateCubeTexture;
extern D3DXCreateVolumeTextureFunction		D3DXCreateVolumeTexture;
extern D3DXCreateTextureFromFileExFunction	D3DXCreateTextureFromFileExA;
extern D3DXFilterTextureFunction			D3DXFilterTexture;
extern D3DXLoadSurfaceFromSurfaceFunction	D3DXLoadSurfaceFromSurface;
extern D3DXGetFVFVertexSizeFunction			D3DXGetFVFVertexSize;

// Loads d3dx9_43.dll and resolves all seventeen entry points, this header's and
// d3dx9math.h's.  Calling it a second time is free and reports the first call.  On ARM64,
// on x64 with ZH_D3DX_PORTABLE=1, and on x64 when the DLL is missing or any one of them
// is not exported, it binds the port's own bodies instead (D3DX9_Runtime_Name says which)
// and returns true, so on Windows it does not fail.
bool Bind_D3DX9_Runtime(void);

// Frees the DLL and nulls every pointer, so the next Bind_D3DX9_Runtime tries again.
// Belongs beside the device teardown, since nothing may call a D3DX entry point after it.
void Unbind_D3DX9_Runtime(void);

// What the bind landed on, for the log: "d3dx9_43.dll", the port's own, or "none".
const char * D3DX9_Runtime_Name(void);

// d3dx9_43.dll does not export D3DXGetErrorStringA, which dx8wrapper.cpp's two error
// loggers used.  This names the D3DERR and D3D_OK codes the renderer actually returns
// and falls back to the raw hex for anything else.  Never returns null.
const char * Get_D3D_Error_String(HRESULT result);

// The size in bytes of a vertex in the given FVF.  This is the one D3DX call the engine
// makes before there is a device, from FVFInfoClass and from HeightMap's software
// transform path, so it binds the runtime itself rather than trusting the caller to have
// done it.  Returns 0 if D3DX9 is not there at all.
UINT Get_FVF_Vertex_Size(DWORD fvf);


#else // !_WIN32

// Off Windows there is no d3dx9_43.dll: <d3d9.h> is Platform/D3D9Posix.h (decision 7), and the same
// names are bound by Bind_D3DX9_Runtime to bodies of our own in d3dx9posix.cpp.  They are pointers,
// as on Windows, because call sites test them: d3d8shadertranslate.cpp and dx8wrapper.cpp read a
// null D3DXAssembleShader as "no D3DX".  Every one is bound, so a call site that does not test one
// (W3DWater.cpp) gets a failure, not a null call:
//   - the matrix functions (d3dx9math.h) compute;
//   - the texture functions fail with D3DERR_NOTAVAILABLE, logged once each, until the device holds
//     resources (A2), and with D3DERR_INVALIDCALL on a null device, as D3DX does;
//   - the shader compiler and disassembler fail with D3DERR_NOTAVAILABLE for good: the SDL3 GPU draw
//     (A3) takes generated HLSL, not D3D9 bytecode;
//   - the shader "assembler" is not one: it wraps the source text in a comment token of an otherwise
//     empty token stream, so the water's run-time programs reach CreatePixelShader and are registered
//     by name, which is all the POSIX device reads (A3e; d3dx9posix.cpp).
// A palette is never passed (every call site gives NULL), so it is an untyped pointer rather than
// wingdi's PALETTEENTRY.

#define D3DX_DEFAULT			((unsigned int)-1)
#define D3DX_FILTER_NONE		(1 << 0)
#define D3DX_FILTER_POINT		(2 << 0)
#define D3DX_FILTER_LINEAR		(3 << 0)
#define D3DX_FILTER_TRIANGLE	(4 << 0)
#define D3DX_FILTER_BOX			(5 << 0)

class ID3DXBuffer : public D3D9PosixUnknown
{
public:
	virtual void * GetBufferPointer() = 0;
	virtual RenderUInt32 GetBufferSize() = 0;
};
class ID3DXInclude;

typedef ID3DXBuffer * LPD3DXBUFFER;
typedef ID3DXInclude * LPD3DXINCLUDE;

struct D3DXMACRO
{
	const char * Name;
	const char * Definition;
};

struct D3DXIMAGE_INFO
{
	unsigned int Width;
	unsigned int Height;
	unsigned int Depth;
	unsigned int MipLevels;
	D3DFORMAT Format;
	D3DRESOURCETYPE ResourceType;
	RenderUInt32 ImageFileFormat;
};

typedef RenderResult (* D3DXAssembleShaderFunction)(const char * source, unsigned int source_length,
	const D3DXMACRO * defines, LPD3DXINCLUDE include, RenderUInt32 flags,
	LPD3DXBUFFER * shader, LPD3DXBUFFER * errors);
typedef RenderResult (* D3DXCompileShaderFunction)(const char * source, unsigned int source_length,
	const D3DXMACRO * defines, LPD3DXINCLUDE include, const char * entry_point, const char * profile,
	RenderUInt32 flags, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors, void ** constant_table);
typedef RenderResult (* D3DXDisassembleShaderFunction)(const RenderUInt32 * shader, int colour_code,
	const char * comments, LPD3DXBUFFER * disassembly);
typedef RenderResult (* D3DXCreateTextureFunction)(LPDIRECT3DDEVICE9 device,
	unsigned int width, unsigned int height, unsigned int mip_levels, RenderUInt32 usage,
	D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE9 * texture);
typedef RenderResult (* D3DXCreateCubeTextureFunction)(LPDIRECT3DDEVICE9 device,
	unsigned int edge_length, unsigned int mip_levels, RenderUInt32 usage, D3DFORMAT format,
	D3DPOOL pool, LPDIRECT3DCUBETEXTURE9 * texture);
typedef RenderResult (* D3DXCreateVolumeTextureFunction)(LPDIRECT3DDEVICE9 device,
	unsigned int width, unsigned int height, unsigned int depth, unsigned int mip_levels,
	RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DVOLUMETEXTURE9 * texture);
typedef RenderResult (* D3DXCreateTextureFromFileExFunction)(LPDIRECT3DDEVICE9 device,
	const char * file_name, unsigned int width, unsigned int height, unsigned int mip_levels,
	RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, RenderUInt32 filter, RenderUInt32 mip_filter,
	D3DCOLOR colour_key, D3DXIMAGE_INFO * info, void * palette, LPDIRECT3DTEXTURE9 * texture);
typedef RenderResult (* D3DXFilterTextureFunction)(LPDIRECT3DBASETEXTURE9 texture,
	const void * palette, unsigned int source_level, RenderUInt32 filter);
typedef RenderResult (* D3DXLoadSurfaceFromSurfaceFunction)(LPDIRECT3DSURFACE9 destination,
	const void * destination_palette, const RenderRect * destination_rect,
	LPDIRECT3DSURFACE9 source, const void * source_palette, const RenderRect * source_rect,
	RenderUInt32 filter, D3DCOLOR colour_key);

extern D3DXAssembleShaderFunction			D3DXAssembleShader;
extern D3DXCompileShaderFunction			D3DXCompileShader;
extern D3DXDisassembleShaderFunction		D3DXDisassembleShader;
extern D3DXCreateTextureFunction			D3DXCreateTexture;
extern D3DXCreateCubeTextureFunction		D3DXCreateCubeTexture;
extern D3DXCreateVolumeTextureFunction		D3DXCreateVolumeTexture;
extern D3DXCreateTextureFromFileExFunction	D3DXCreateTextureFromFileExA;
extern D3DXFilterTextureFunction			D3DXFilterTexture;
extern D3DXLoadSurfaceFromSurfaceFunction	D3DXLoadSurfaceFromSurface;

// As on Windows: binds every pointer above (never fails here, since nothing is loaded), and nulls
// them again.
bool Bind_D3DX9_Runtime(void);
void Unbind_D3DX9_Runtime(void);

// The D3DERR and D3D_OK codes by name, or the raw hex.  Never returns null.
const char * Get_D3D_Error_String(RenderResult result);

// The size in bytes of a vertex in the given FVF, computed from the FVF bits as D3DX does.  Needs no
// binding: FVFInfoClass calls it before there is a device.
unsigned int Get_FVF_Vertex_Size(RenderUInt32 fvf);

#endif // _WIN32

#endif // D3DX9RUNTIME_H
