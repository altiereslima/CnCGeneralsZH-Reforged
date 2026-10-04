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

// Windows only, not part of the build: the measurement that settled F7 (FFReference's N7), kept so it can
// be run again.  Build in a VS x64 prompt: cl /EHsc /O2 f7probe_windows.cpp d3d9.lib user32.lib; run it in
// a desktop session (a D3D9 device needs one): f7probe_windows.exe out.txt [ref].  2026-09-27, Windows 11
// 26200 in the project's VM: the Microsoft Basic Render Driver (WARP, d3d10warp.dll 10.0.26100.5074), HAL
// and REF alike: A 0xFF000000, B 0xFFFF0000, D 0x00FF0000, C 0xFFFFFFFF (see ffreference.h's N7).

// f7probe: what Direct3D 9 itself uses for a vertex colour the FVF does not carry (F7 / FFReference's N7).
// Draws 8x8 pretransformed quads into an A8R8G8B8 render target on the adapter's HAL device (or the
// reference rasteriser when asked), reads the centre pixel back, and prints it.  Cases:
//   A  specular as a stage argument: COLOROP SELECTARG1, COLORARG1 = D3DTA_SPECULAR, FVF XYZRHW|DIFFUSE
//   B  specular add: SPECULARENABLE, LIGHTING off, COLOROP SELECTARG1 = DIFFUSE (red), FVF XYZRHW|DIFFUSE
//   C  diffuse as a stage argument, FVF XYZRHW only (the page's other absent colour)
// Controls, with the colour present in the FVF: A1/B1 specular green, A0/B0 specular black.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

struct VD { float x, y, z, rhw; DWORD diffuse; };
struct VDS { float x, y, z, rhw; DWORD diffuse, specular; };
struct V { float x, y, z, rhw; };

static IDirect3DDevice9 *dev;
static IDirect3DSurface9 *rt, *sys;
static FILE *out;

static DWORD readCentre()
{
	dev->GetRenderTargetData( rt, sys );
	D3DLOCKED_RECT lr;
	sys->LockRect( &lr, NULL, D3DLOCK_READONLY );
	const DWORD c = *(const DWORD *)((const BYTE *)lr.pBits + 4 * lr.Pitch + 4 * 4);
	sys->UnlockRect();
	return c;
}

static void begin( DWORD fvf )
{
	dev->Clear( 0, NULL, D3DCLEAR_TARGET, 0xFF3F2F1F, 1.0f, 0 );
	dev->SetFVF( fvf );
	dev->SetRenderState( D3DRS_LIGHTING, FALSE );
	dev->SetRenderState( D3DRS_ZENABLE, FALSE );
	dev->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
	dev->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
	dev->SetRenderState( D3DRS_FOGENABLE, FALSE );
	dev->SetRenderState( D3DRS_SPECULARENABLE, FALSE );
	dev->SetTexture( 0, NULL );
	dev->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
	dev->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
	dev->SetTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
	dev->SetTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
}

template <class T> static void quad( T *v )
{
	const float xy[4][2] = { { -0.5f, -0.5f }, { 7.5f, -0.5f }, { -0.5f, 7.5f }, { 7.5f, 7.5f } };
	for (int i = 0; i < 4; ++i) { v[i].x = xy[i][0]; v[i].y = xy[i][1]; v[i].z = 0.5f; v[i].rhw = 1.0f; }
	dev->BeginScene();
	dev->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, v, sizeof( T ) );
	dev->EndScene();
}

static void report( const char *name, const char *what, DWORD c )
{
	fprintf( out, "%-3s %-68s -> 0x%08lX (A %lu R %lu G %lu B %lu)\n", name, what, c, c >> 24, (c >> 16) & 255,
		(c >> 8) & 255, c & 255 );
}

int main( int argc, char **argv )
{
	out = fopen( argc > 1 ? argv[1] : "f7probe.txt", "w" );
	const D3DDEVTYPE type = (argc > 2 && !strcmp( argv[2], "ref" )) ? D3DDEVTYPE_REF : D3DDEVTYPE_HAL;
	IDirect3D9 *d3d = Direct3DCreate9( D3D_SDK_VERSION );
	if (!d3d) { fprintf( out, "Direct3DCreate9 failed\n" ); return 1; }
	D3DADAPTER_IDENTIFIER9 id;
	d3d->GetAdapterIdentifier( D3DADAPTER_DEFAULT, 0, &id );
	fprintf( out, "adapter: %s (%s), driver %u.%u.%u.%u, vendor 0x%04lX device 0x%04lX; device type %s\n",
		id.Description, id.Driver, HIWORD( id.DriverVersion.HighPart ), LOWORD( id.DriverVersion.HighPart ),
		HIWORD( id.DriverVersion.LowPart ), LOWORD( id.DriverVersion.LowPart ), id.VendorId, id.DeviceId,
		type == D3DDEVTYPE_REF ? "REF" : "HAL" );
	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_UNKNOWN;
	pp.hDeviceWindow = GetDesktopWindow();
	HRESULT hr = d3d->CreateDevice( D3DADAPTER_DEFAULT, type, GetDesktopWindow(),
		D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &dev );
	if (FAILED( hr )) { fprintf( out, "CreateDevice failed 0x%08lX\n", hr ); return 1; }
	dev->CreateRenderTarget( 8, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, NULL );
	dev->CreateOffscreenPlainSurface( 8, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL );
	dev->SetRenderTarget( 0, rt );

	// A: specular read as a stage argument
	{
		begin( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
		dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
		dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_SPECULAR );
		VD v[4]; for (int i = 0; i < 4; ++i) v[i].diffuse = 0xFFFF0000;
		quad( v );
		report( "A", "D3DTA_SPECULAR, FVF has no specular (diffuse red)", readCentre() );
	}
	for (int k = 0; k < 2; ++k)
	{
		begin( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR );
		dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
		dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_SPECULAR );
		VDS v[4]; for (int i = 0; i < 4; ++i) { v[i].diffuse = 0xFFFF0000; v[i].specular = k ? 0xFF00FF00 : 0xFF000000; }
		quad( v );
		report( k ? "A1" : "A0", k ? "control: D3DTA_SPECULAR, FVF specular green" : "control: D3DTA_SPECULAR, FVF specular black", readCentre() );
	}
	// B: specular add
	{
		begin( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
		dev->SetRenderState( D3DRS_SPECULARENABLE, TRUE );
		dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
		dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
		VD v[4]; for (int i = 0; i < 4; ++i) v[i].diffuse = 0xFFFF0000;
		quad( v );
		report( "B", "SPECULARENABLE, lighting off, FVF has no specular (diffuse red)", readCentre() );
	}
	for (int k = 0; k < 2; ++k)
	{
		begin( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR );
		dev->SetRenderState( D3DRS_SPECULARENABLE, TRUE );
		dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
		dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
		VDS v[4]; for (int i = 0; i < 4; ++i) { v[i].diffuse = 0xFFFF0000; v[i].specular = k ? 0xFF00FF00 : 0xFF000000; }
		quad( v );
		report( k ? "B1" : "B0", k ? "control: SPECULARENABLE, FVF specular green" : "control: SPECULARENABLE, FVF specular black", readCentre() );
	}
	// D: the absent specular's ALPHA, through ALPHAARG1 = D3DTA_SPECULAR (colour from the diffuse)
	{
		begin( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
		dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
		dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
		dev->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_SPECULAR );
		VD v[4]; for (int i = 0; i < 4; ++i) v[i].diffuse = 0x40FF0000;
		quad( v );
		report( "D", "ALPHAARG1 D3DTA_SPECULAR, FVF has no specular (diffuse alpha 0x40)", readCentre() );
	}
	{
		begin( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR );
		dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
		dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
		dev->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_SPECULAR );
		VDS v[4]; for (int i = 0; i < 4; ++i) { v[i].diffuse = 0x40FF0000; v[i].specular = 0x80000000; }
		quad( v );
		report( "D1", "control: ALPHAARG1 D3DTA_SPECULAR, FVF specular alpha 0x80", readCentre() );
	}
	// C: diffuse read with no diffuse in the FVF
	{
		begin( D3DFVF_XYZRHW );
		dev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
		dev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
		V v[4];
		quad( v );
		report( "C", "D3DTA_DIFFUSE, FVF has no diffuse", readCentre() );
	}
	fprintf( out, "done\n" );
	fclose( out );
	return 0;
}
