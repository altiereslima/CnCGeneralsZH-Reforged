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

// Windows only, not part of the build: the measurement that settled the device's known list (F1-F4, F6,
// F8, F11) and the FFReference readings behind it (N3, N4, N11, N13, N14, N15, N17, N27, N28, N29), kept
// so it can be run again.  Build in a VS x64 prompt: cl /EHsc /O2 knownprobe_windows.cpp d3d9.lib
// user32.lib; run it in a desktop session (a D3D9 device needs one): knownprobe_windows.exe out.txt [ref].
// 2026-09-27, Windows 11 26200 in the project's VM: the Microsoft Basic Render Driver (WARP, d3d10warp.dll
// 10.0.26100.5074) as HAL, and the reference rasteriser (d3dref9.dll, loaded, as the output shows) as REF.
// The values are in test_ffreference.cpp's known-list tests.

// knownprobe: what Direct3D 9 itself draws for each item on the device's known list (F1-F4, F6, F8, F11)
// and for the page readings FFReference rests on there (N3, N4, N11, N13, N15, N17, N27, N28, N29).
// Draws into a 64x64 A8R8G8B8 render target on the HAL device (or REF when asked), reads pixels back.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static IDirect3DDevice9 *dev;
static IDirect3DSurface9 *rt, *sys;
static FILE *out;
enum { W = 64 };

struct TL { float x, y, z, rhw; DWORD diffuse, specular; };			// XYZRHW|DIFFUSE|SPECULAR
struct TLT { float x, y, z, rhw; float u, v; };						// XYZRHW|TEX1
struct LV { float x, y, z; float nx, ny, nz; };						// XYZ|NORMAL
struct UV { float x, y, z; float u, v; };							// XYZ|TEX1
static const DWORD FVF_TL = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR;
static const DWORD FVF_TLT = D3DFVF_XYZRHW | D3DFVF_TEX1;
static const DWORD FVF_LV = D3DFVF_XYZ | D3DFVF_NORMAL;
static const DWORD FVF_UV = D3DFVF_XYZ | D3DFVF_TEX1;

static void readBack()
{
	dev->GetRenderTargetData( rt, sys );
}

static DWORD px( int x, int y )
{
	D3DLOCKED_RECT lr;
	sys->LockRect( &lr, NULL, D3DLOCK_READONLY );
	const DWORD c = *(const DWORD *)((const BYTE *)lr.pBits + y * lr.Pitch + 4 * x);
	sys->UnlockRect();
	return c;
}

static void report( const char *name, const char *what, int x, int y )
{
	const DWORD c = px( x, y );
	fprintf( out, "%-4s %-66s (%2d,%2d) -> 0x%08lX (A %3lu R %3lu G %3lu B %3lu)\n", name, what, x, y, c, c >> 24,
		(c >> 16) & 255, (c >> 8) & 255, c & 255 );
}

static void identity( D3DMATRIX &m )
{
	memset( &m, 0, sizeof( m ) );
	m._11 = m._22 = m._33 = m._44 = 1.0f;
}

/// Every case starts here: cleared target, identity transforms, lighting off, stage 0 SELECTARG1 DIFFUSE.
static void begin()
{
	dev->Clear( 0, NULL, D3DCLEAR_TARGET, 0xFF3F2F1F, 1.0f, 0 );
	D3DMATRIX m;
	identity( m );
	dev->SetTransform( D3DTS_WORLD, &m );
	dev->SetTransform( D3DTS_VIEW, &m );
	dev->SetTransform( D3DTS_PROJECTION, &m );
	dev->SetTransform( D3DTS_TEXTURE0, &m );
	dev->SetRenderState( D3DRS_LIGHTING, FALSE );
	dev->SetRenderState( D3DRS_ZENABLE, FALSE );
	dev->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
	dev->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
	dev->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
	dev->SetRenderState( D3DRS_FOGENABLE, FALSE );
	dev->SetRenderState( D3DRS_SPECULARENABLE, FALSE );
	dev->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
	dev->SetRenderState( D3DRS_LOCALVIEWER, TRUE );
	dev->SetRenderState( D3DRS_NORMALIZENORMALS, FALSE );
	dev->SetRenderState( D3DRS_COLORVERTEX, TRUE );
	dev->SetRenderState( D3DRS_AMBIENT, 0 );
	dev->SetRenderState( D3DRS_TEXTUREFACTOR, 0xFFFFFFFF );
	for (int i = 0; i < 8; ++i)
		dev->LightEnable( i, FALSE );
	D3DMATERIAL9 mat = {};
	dev->SetMaterial( &mat );
	dev->SetTexture( 0, NULL );
	dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
	dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
	dev->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_CURRENT );
	dev->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
	dev->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
	dev->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
	dev->SetTextureStageState( 0, D3DTSS_TEXCOORDINDEX, 0 );
	dev->SetTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
	dev->SetTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
	dev->SetTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
	dev->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
	dev->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
	dev->SetSamplerState( 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE );
	dev->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP );
	dev->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP );
}

template <class T> static void drawUP( DWORD fvf, D3DPRIMITIVETYPE type, int primitives, const T *v )
{
	dev->SetFVF( fvf );
	dev->BeginScene();
	dev->DrawPrimitiveUP( type, primitives, v, sizeof( T ) );
	dev->EndScene();
	readBack();
}

static TL tl( float x, float y, DWORD d, DWORD s = 0xFF000000 )
{
	TL v = { x - 0.5f, y - 0.5f, 0.5f, 1.0f, d, s };
	return v;
}

/// The whole target as a clip-space quad (strip) at z 0.5, normal toward the viewer
static void litQuad()
{
	const LV v[4] = { { -1, 1, 0.5f, 0, 0, -1 }, { 1, 1, 0.5f, 0, 0, -1 }, { -1, -1, 0.5f, 0, 0, -1 }, { 1, -1, 0.5f, 0, 0, -1 } };
	drawUP( FVF_LV, D3DPT_TRIANGLESTRIP, 2, v );
}

static D3DLIGHT9 light( D3DLIGHTTYPE type )
{
	D3DLIGHT9 l = {};
	l.Type = type;
	l.Range = 1000.0f;
	l.Attenuation0 = 1.0f;
	return l;
}

static D3DCOLORVALUE rgb( float r, float g, float b )
{
	D3DCOLORVALUE c = { r, g, b, 1.0f };
	return c;
}

static IDirect3DTexture9 *texture( int size, int levels, DWORD (*colour)( int level, int x, int y ) )
{
	IDirect3DTexture9 *t = NULL;
	if (FAILED( dev->CreateTexture( size, size, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL ) ))
		return NULL;
	for (int l = 0; l < levels; ++l)
	{
		const int s = size >> l;
		D3DLOCKED_RECT lr;
		t->LockRect( l, &lr, NULL, 0 );
		for (int y = 0; y < s; ++y)
			for (int x = 0; x < s; ++x)
				*(DWORD *)((BYTE *)lr.pBits + y * lr.Pitch + 4 * x) = colour( l, x, y );
		t->UnlockRect( l );
	}
	return t;
}

static DWORD grid4( int, int x, int y ) { return 0xFF000000 | ((DWORD)(x * 64) << 16) | ((DWORD)(y * 64) << 8); }
static DWORD coordTexel( int, int x, int y ) { return 0xFF000000 | ((DWORD)x << 16) | ((DWORD)y << 8); }
static DWORD levelRamp( int l, int, int ) { return 0xFF000000 | ((DWORD)(28 * l) << 16); }

int main( int argc, char **argv )
{
	out = fopen( argc > 1 ? argv[1] : "knownprobe.txt", "w" );
	const D3DDEVTYPE type = (argc > 2 && !strcmp( argv[2], "ref" )) ? D3DDEVTYPE_REF : D3DDEVTYPE_HAL;
	IDirect3D9 *d3d = Direct3DCreate9( D3D_SDK_VERSION );
	if (!d3d) { fprintf( out, "Direct3DCreate9 failed\n" ); return 1; }
	D3DADAPTER_IDENTIFIER9 id;
	d3d->GetAdapterIdentifier( D3DADAPTER_DEFAULT, 0, &id );
	fprintf( out, "adapter: %s (%s), driver %u.%u.%u.%u; device type %s\n", id.Description, id.Driver,
		HIWORD( id.DriverVersion.HighPart ), LOWORD( id.DriverVersion.HighPart ), HIWORD( id.DriverVersion.LowPart ),
		LOWORD( id.DriverVersion.LowPart ), type == D3DDEVTYPE_REF ? "REF" : "HAL" );
	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_UNKNOWN;
	pp.hDeviceWindow = GetDesktopWindow();
	HRESULT hr = d3d->CreateDevice( D3DADAPTER_DEFAULT, type, GetDesktopWindow(),
		D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &dev );
	if (FAILED( hr )) { fprintf( out, "CreateDevice failed 0x%08lX\n", hr ); return 1; }
	// which rasteriser answers: the REF device needs d3d9's reference DLL, which only the SDK installs
	const char *dlls[] = { "d3dref9.dll", "d3d9ref.dll", "d3d10warp.dll", "d3d11.dll" };
	for (int i = 0; i < 4; ++i)
		fprintf( out, "module %-14s %s\n", dlls[i], GetModuleHandleA( dlls[i] ) ? "loaded" : "not loaded" );
	D3DCAPS9 caps;
	dev->GetDeviceCaps( &caps );
	fprintf( out, "caps: ShadeCaps 0x%08lX TextureOpCaps 0x%08lX VertexProcessingCaps 0x%08lX\n", caps.ShadeCaps,
		caps.TextureOpCaps, caps.VertexProcessingCaps );
	dev->CreateRenderTarget( W, W, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, NULL );
	dev->CreateOffscreenPlainSurface( W, W, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL );
	dev->SetRenderTarget( 0, rt );
	D3DVIEWPORT9 vp = { 0, 0, W, W, 0.0f, 1.0f };
	dev->SetViewport( &vp );

	const DWORD red = 0xFFFF0000, green = 0xFF00FF00, blue = 0xFF0000FF, white = 0xFFFFFFFF;

	// ---- F8 / N11: flat shading, which vertex gives the colour --------------------------------------
	fprintf( out, "-- F8 flat shading (N11)\n" );
	{
		begin();
		dev->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
		const TL v[3] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 0, 64, blue ) };
		drawUP( FVF_TL, D3DPT_TRIANGLELIST, 1, v );
		report( "F8a", "list, vertices red green blue", 10, 10 );
		report( "F8a", "list, same triangle, another pixel", 30, 20 );
	}
	{
		begin();
		dev->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
		const TL v[4] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 0, 64, blue ), tl( 64, 64, white ) };
		drawUP( FVF_TL, D3DPT_TRIANGLESTRIP, 2, v );
		report( "F8b", "strip v0..v3 red green blue white, triangle 0", 10, 10 );
		report( "F8b", "strip, triangle 1 (v1 v2 v3)", 54, 54 );
	}
	{
		begin();
		dev->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
		const TL v[4] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 64, 64, blue ), tl( 0, 64, white ) };
		drawUP( FVF_TL, D3DPT_TRIANGLEFAN, 2, v );
		report( "F8c", "fan v0..v3 red green blue white, triangle 0 (v0 v1 v2)", 54, 10 );
		report( "F8c", "fan, triangle 1 (v0 v2 v3)", 10, 54 );
	}
	{
		begin();
		dev->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
		const TL v[3] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 0, 64, blue ) };
		const WORD idx[3] = { 2, 0, 1 };
		dev->SetFVF( FVF_TL );
		dev->BeginScene();
		dev->DrawIndexedPrimitiveUP( D3DPT_TRIANGLELIST, 0, 3, 1, idx, D3DFMT_INDEX16, v, sizeof( TL ) );
		dev->EndScene();
		readBack();
		report( "F8d", "indexed list, indices 2 0 1 (vertex 2 blue first)", 10, 10 );
	}
	{
		begin();
		dev->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
		const TL v[3] = { tl( 0, 0, 0x00FFFFFF ), tl( 64, 0, 0x80FFFFFF ), tl( 0, 64, 0xFFFFFFFF ) };
		drawUP( FVF_TL, D3DPT_TRIANGLELIST, 1, v );
		report( "F8e", "list, diffuse alpha 00 80 FF: is alpha flat too?", 10, 10 );
		report( "F8e", "list, alpha, another pixel", 30, 20 );
	}
	{
		begin();
		dev->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
		dev->SetRenderState( D3DRS_SPECULARENABLE, TRUE );
		const TL v[3] = { tl( 0, 0, 0xFF000000, red ), tl( 64, 0, 0xFF000000, green ), tl( 0, 64, 0xFF000000, blue ) };
		drawUP( FVF_TL, D3DPT_TRIANGLELIST, 1, v );
		report( "F8f", "list, black diffuse, specular red green blue, SPECULARENABLE", 10, 10 );
		report( "F8f", "list, specular, another pixel", 30, 20 );
	}
	{
		begin();
		const TL v[3] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 0, 64, blue ) };
		drawUP( FVF_TL, D3DPT_TRIANGLELIST, 1, v );
		report( "F8g", "control: the same list Gouraud", 10, 10 );
	}

	// ---- F6: vertex specular with lighting off ---------------------------------------------------
	fprintf( out, "-- F6 unlit vertex specular\n" );
	{
		begin();
		dev->SetRenderState( D3DRS_SPECULARENABLE, TRUE );
		const TL v[4] = { tl( 0, 0, red, 0xFF00FF00 ), tl( 64, 0, red, 0xFF00FF00 ), tl( 0, 64, red, 0xFF00FF00 ), tl( 64, 64, red, 0xFF00FF00 ) };
		drawUP( FVF_TL, D3DPT_TRIANGLESTRIP, 2, v );
		report( "F6a", "diffuse red, specular green, SPECULARENABLE", 32, 32 );
		dev->SetRenderState( D3DRS_SPECULARENABLE, FALSE );
		drawUP( FVF_TL, D3DPT_TRIANGLESTRIP, 2, v );
		report( "F6b", "control: the same without SPECULARENABLE", 32, 32 );
	}

	// ---- F1 / N4: per-light ambient ------------------------------------------------------------
	fprintf( out, "-- F1 per-light ambient (N4); material ambient white, diffuse/specular/emissive 0, AMBIENT 0\n" );
	{
		D3DMATERIAL9 mat = {};
		mat.Ambient = rgb( 1, 1, 1 );
		begin();
		dev->SetRenderState( D3DRS_LIGHTING, TRUE );
		dev->SetRenderState( D3DRS_COLORVERTEX, FALSE );
		dev->SetMaterial( &mat );
		D3DLIGHT9 l = light( D3DLIGHT_DIRECTIONAL );
		l.Direction.z = 1.0f;
		l.Ambient = rgb( 0.4f, 0.2f, 0.0f );
		dev->SetLight( 0, &l );
		dev->LightEnable( 0, TRUE );
		litQuad();
		report( "F1a", "directional light, ambient (.4 .2 0), diffuse 0", 32, 32 );
		D3DLIGHT9 p = light( D3DLIGHT_POINT );
		p.Position.z = -1.0f;
		p.Attenuation0 = 2.0f;
		p.Ambient = rgb( 0.8f, 0.0f, 0.4f );
		dev->SetLight( 0, &p );
		litQuad();
		report( "F1b", "point light, Attenuation0 2, ambient (.8 0 .4)", 32, 32 );
		p.Range = 0.1f;
		dev->SetLight( 0, &p );
		litQuad();
		report( "F1c", "the same point light out of range (Range .1)", 32, 32 );
		l.Ambient = rgb( 0, 0, 0 );
		dev->SetLight( 0, &l );
		litQuad();
		report( "F1d", "control: directional light, ambient 0", 32, 32 );
	}

	// ---- F2 / N27 / N3: the halfway vector, LOCALVIEWER, the N.L gate ------------------------------
	fprintf( out, "-- F2 specular: material specular white, power 1, diffuse 0; normal (0,0,-1), z .5\n" );
	{
		D3DMATERIAL9 mat = {};
		mat.Specular = rgb( 1, 1, 1 );
		mat.Power = 1.0f;
		const float dirs[3][3] = { { 0, 0.6f, 0.8f }, { 0, -0.9f, -0.4359f }, { 0.6f, 0, 0.8f } };
		const char *names[3] = { "Direction (0,.6,.8): Ldir toward the viewer side", "Direction (0,-.9,-.436): light behind (N.L < 0)",
			"Direction (.6,0,.8)" };
		for (int d = 0; d < 3; ++d)
			for (int lv = 1; lv >= 0; --lv)
			{
				begin();
				dev->SetRenderState( D3DRS_LIGHTING, TRUE );
				dev->SetRenderState( D3DRS_COLORVERTEX, FALSE );
				dev->SetRenderState( D3DRS_SPECULARENABLE, TRUE );
				dev->SetRenderState( D3DRS_LOCALVIEWER, lv );
				dev->SetMaterial( &mat );
				D3DLIGHT9 l = light( D3DLIGHT_DIRECTIONAL );
				l.Direction.x = dirs[d][0]; l.Direction.y = dirs[d][1]; l.Direction.z = dirs[d][2];
				l.Specular = rgb( 1, 1, 1 );
				dev->SetLight( 0, &l );
				dev->LightEnable( 0, TRUE );
				litQuad();
				char name[8], what[128];
				sprintf( name, "F2%c%d", 'a' + d, lv );
				sprintf( what, "%s, LOCALVIEWER %d", names[d], lv );
				report( name, what, 32, 32 );
				report( name, "  corner (all four vertices differ in V under LOCALVIEWER)", 1, 1 );
				report( name, "  corner", 62, 62 );
			}
	}

	// ---- F3 / N17: DOTPRODUCT3 --------------------------------------------------------------------
	fprintf( out, "-- F3 DOTPRODUCT3 (N17): COLORARG1 DIFFUSE, COLORARG2 TFACTOR, ALPHAOP SELECTARG1 DIFFUSE\n" );
	{
		const DWORD cases[3][2] = { { 0x40FF8080, 0xFFFF8080 }, { 0x40C08080, 0xFFC08080 }, { 0x40A0A0A0, 0xFFA0A0A0 } };
		for (int c = 0; c < 3; ++c)
		{
			begin();
			dev->SetRenderState( D3DRS_TEXTUREFACTOR, cases[c][1] );
			dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3 );
			dev->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR );
			const TL v[4] = { tl( 0, 0, cases[c][0] ), tl( 64, 0, cases[c][0] ), tl( 0, 64, cases[c][0] ), tl( 64, 64, cases[c][0] ) };
			drawUP( FVF_TL, D3DPT_TRIANGLESTRIP, 2, v );
			char name[8], what[128];
			sprintf( name, "F3%c", 'a' + c );
			sprintf( what, "diffuse 0x%08lX . tfactor 0x%08lX", cases[c][0], cases[c][1] );
			report( name, what, 32, 32 );
		}
	}

	// ---- F4 / N13 / N28: texture-coordinate padding under TTFF_COUNT2 ---------------------------------
	fprintf( out, "-- F4 texture transform (N13): 4x4 texture, texel (i,j) = R 64i G 64j, POINT, uv (.1,.1)\n" );
	{
		IDirect3DTexture9 *t = texture( 4, 1, grid4 );
		const UV q[4] = { { -1, 1, 0.5f, 0.1f, 0.1f }, { 1, 1, 0.5f, 0.1f, 0.1f }, { -1, -1, 0.5f, 0.1f, 0.1f }, { 1, -1, 0.5f, 0.1f, 0.1f } };
		const UV q6[4] = { { -1, 1, 0.5f, 0.6f, 0.6f }, { 1, 1, 0.5f, 0.6f, 0.6f }, { -1, -1, 0.5f, 0.6f, 0.6f }, { 1, -1, 0.5f, 0.6f, 0.6f } };
		for (int c = 0; c < 6; ++c)
		{
			begin();
			dev->SetTexture( 0, t );
			dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			D3DMATRIX m;
			identity( m );
			const char *what = "";
			if (c == 0) what = "control: no transform";
			if (c == 1) { m._31 = 0.5f; m._32 = 0.25f; what = "COUNT2, _31 .5 _32 .25 (D3D9 pads (u,v,1,0): moves)"; }
			if (c == 2) { m._41 = 0.5f; m._42 = 0.25f; what = "COUNT2, _41 .5 _42 .25 (pads (u,v,0,1): moves)"; }
			if (c == 3) { what = "N28: TEXCOORDINDEX 1, vertices have one set (uv .1: cannot tell)"; dev->SetTextureStageState( 0, D3DTSS_TEXCOORDINDEX, 1 ); }
			if (c == 4) what = "control: uv (.6,.6), no transform";
			if (c == 5) { what = "N28: TEXCOORDINDEX 1, vertices have one set, uv (.6,.6)"; dev->SetTextureStageState( 0, D3DTSS_TEXCOORDINDEX, 1 ); }
			dev->SetTransform( D3DTS_TEXTURE0, &m );
			if (c == 1 || c == 2)
				dev->SetTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2 );
			drawUP( FVF_UV, D3DPT_TRIANGLESTRIP, 2, c >= 4 ? q6 : q );
			char name[8];
			sprintf( name, "F4%c", 'a' + c );
			report( name, what, 32, 32 );
		}
		if (t) t->Release();
	}

	// ---- F11 / N15: the LOD's footprint norm ------------------------------------------------------------
	fprintf( out, "-- F11 LOD (N15): 256x256, 9 levels, level L = R 28L; LINEAR/LINEAR/LINEAR; lambda = R/28\n" );
	{
		IDirect3DTexture9 *t = texture( 256, 9, levelRamp );
		// texel-space derivatives per pixel: d(uW,vH)/dx and /dy
		const float cases[4][4] = { { 3, 0, 0, 3 }, { 2, -2, 2, 2 }, { 4, 0, 0, 1 }, { 2.5f, 1.5f, -1.5f, 2.5f } };
		const char *names[4] = { "isotropic 3: L2 1.585", "rotated (2,-2),(2,2): L2 1.5, Linf 1.0, L1 2.0",
			"anisotropic (4,0),(0,1): max 2.0", "rotated (2.5,1.5),(-1.5,2.5): L2 1.543, Linf 1.322, L1 2.0" };
		for (int c = 0; c < 4; ++c)
		{
			begin();
			dev->SetTexture( 0, t );
			dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			dev->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
			dev->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
			dev->SetSamplerState( 0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR );
			TLT v[4];
			const float xy[4][2] = { { 0, 0 }, { 64, 0 }, { 0, 64 }, { 64, 64 } };
			for (int i = 0; i < 4; ++i)
			{
				v[i].x = xy[i][0] - 0.5f; v[i].y = xy[i][1] - 0.5f; v[i].z = 0.5f; v[i].rhw = 1.0f;
				v[i].u = (cases[c][0] * xy[i][0] + cases[c][2] * xy[i][1]) / 256.0f;
				v[i].v = (cases[c][1] * xy[i][0] + cases[c][3] * xy[i][1]) / 256.0f;
			}
			drawUP( FVF_TLT, D3DPT_TRIANGLESTRIP, 2, v );
			char name[8];
			sprintf( name, "F11%c", 'a' + c );
			report( name, names[c], 32, 32 );
			report( name, "  another pixel", 17, 45 );
		}
		if (t) t->Release();
	}

	// ---- N14: the camera-space reflection vector, read back through a coordinate texture ----------------
	// texture 256x256, texel (x, y) = R x, G y, POINT; TTFF_COUNT2 with u = .25 Rz + .5, v = .25 Ry + .5 (rows
	// 3 and 2 of the matrix, translation in row 4 since the generated set is (x,y,z,1)): Rz = (R/256 - .5)*4
	fprintf( out, "-- N14 reflection vector: normal (0,0,-1) at z .5; texel (R,G) -> Rz = (R+.5)/64 - 2, Ry = (G+.5)/64 - 2\n" );
	{
		IDirect3DTexture9 *t = texture( 256, 1, coordTexel );
		const float normals[2][3] = { { 0, 0, -1 }, { 0, 0.6f, -0.8f } };
		for (int n = 0; n < 2; ++n)
			for (int lv = 1; lv >= 0; --lv)
			{
				begin();
				dev->SetRenderState( D3DRS_LOCALVIEWER, lv );
				dev->SetTexture( 0, t );
				dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				dev->SetTextureStageState( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR );
				dev->SetTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2 );
				dev->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
				dev->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
				D3DMATRIX m;
				memset( &m, 0, sizeof( m ) );
				m._31 = 0.25f; m._22 = 0.25f; m._41 = 0.5f; m._42 = 0.5f; m._44 = 1.0f;
				dev->SetTransform( D3DTS_TEXTURE0, &m );
				LV v[4] = { { -1, 1, 0.5f }, { 1, 1, 0.5f }, { -1, -1, 0.5f }, { 1, -1, 0.5f } };
				for (int i = 0; i < 4; ++i) { v[i].nx = normals[n][0]; v[i].ny = normals[n][1]; v[i].nz = normals[n][2]; }
				drawUP( FVF_LV, D3DPT_TRIANGLESTRIP, 2, v );
				char name[8], what[128];
				sprintf( name, "N14%c", 'a' + 2 * n + (1 - lv) );
				sprintf( what, "normal (%g,%g,%g), LOCALVIEWER %d", normals[n][0], normals[n][1], normals[n][2], lv );
				report( name, what, 32, 32 );
				report( name, "  corner", 1, 1 );
			}
		if (t) t->Release();
	}

	// ---- N3: the specular gate, under LOCALVIEWER 0 with the infinite viewer either way round ------------
	fprintf( out, "-- N3 gate: material specular white, power 1; normal (0,0,-1); Direction d, Ldir = -d\n" );
	{
		D3DMATERIAL9 mat = {};
		mat.Specular = rgb( 1, 1, 1 );
		mat.Power = 1.0f;
		// Ldir (0,.9,.436): N.L = -.436; with V (0,0,-1) N.H = .531; with V (0,0,1) N.H < 0
		// Ldir (0,.9,-.436): N.L = +.436; with V (0,0,-1) N.H = .848 (control)
		// Ldir (0,.99,-.141): N.L = +.141, grazing; N.H = .656
		const float dirs[3][3] = { { 0, -0.9f, -0.4359f }, { 0, -0.9f, 0.4359f }, { 0, -0.99f, 0.1411f } };
		for (int d = 0; d < 3; ++d)
		{
			begin();
			dev->SetRenderState( D3DRS_LIGHTING, TRUE );
			dev->SetRenderState( D3DRS_COLORVERTEX, FALSE );
			dev->SetRenderState( D3DRS_SPECULARENABLE, TRUE );
			dev->SetRenderState( D3DRS_LOCALVIEWER, FALSE );
			dev->SetMaterial( &mat );
			D3DLIGHT9 l = light( D3DLIGHT_DIRECTIONAL );
			l.Direction.x = dirs[d][0]; l.Direction.y = dirs[d][1]; l.Direction.z = dirs[d][2];
			l.Specular = rgb( 1, 1, 1 );
			dev->SetLight( 0, &l );
			dev->LightEnable( 0, TRUE );
			litQuad();
			char name[8], what[128];
			sprintf( name, "N3%c", 'a' + d );
			sprintf( what, "Direction (%g,%g,%g)", dirs[d][0], dirs[d][1], dirs[d][2] );
			report( name, what, 32, 32 );
		}
	}

	// ---- N11: fog under flat shading (the page: fog is still interpolated) --------------------------------
	fprintf( out, "-- N11 fog under flat: white diffuse, specular alpha (fog) 00 80 FF per vertex, fog colour black, no fog mode\n" );
	for (int flat = 1; flat >= 0; --flat)
	{
		begin();
		dev->SetRenderState( D3DRS_SHADEMODE, flat ? D3DSHADE_FLAT : D3DSHADE_GOURAUD );
		dev->SetRenderState( D3DRS_FOGENABLE, TRUE );
		dev->SetRenderState( D3DRS_FOGCOLOR, 0xFF000000 );
		dev->SetRenderState( D3DRS_FOGTABLEMODE, D3DFOG_NONE );
		dev->SetRenderState( D3DRS_FOGVERTEXMODE, D3DFOG_NONE );
		const TL v[3] = { tl( 0, 0, white, 0x00000000 ), tl( 64, 0, white, 0x80000000 ), tl( 0, 64, white, 0xFF000000 ) };
		drawUP( FVF_TL, D3DPT_TRIANGLELIST, 1, v );
		report( flat ? "N11f" : "N11g", flat ? "flat" : "control: Gouraud", 10, 10 );
		report( flat ? "N11f" : "N11g", "  another pixel", 30, 20 );
	}

	// ---- N29: ALPHAOP DISABLE under an enabled COLOROP ("undefined") ------------------------------------
	fprintf( out, "-- N29 ALPHAOP DISABLE with COLOROP SELECTARG1 DIFFUSE (the page: undefined)\n" );
	{
		begin();
		dev->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
		const DWORD d = 0x40FF0000;
		const TL v[4] = { tl( 0, 0, d ), tl( 64, 0, d ), tl( 0, 64, d ), tl( 64, 64, d ) };
		drawUP( FVF_TL, D3DPT_TRIANGLESTRIP, 2, v );
		report( "N29", "diffuse 0x40FF0000", 32, 32 );
	}
	fprintf( out, "done\n" );
	fclose( out );
	return 0;
}
