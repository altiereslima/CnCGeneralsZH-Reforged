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
 * A3b: FFReference's own tests.  Every expected value below is worked by hand from the formula on the
 * named learn.microsoft.com page (the arithmetic is in the comments), not taken from FFReference's
 * output.  Each Mutation is an armed control: a test that applies it and checks the result MOVES, so
 * a check that could not fail would show.
 *
 * What these cannot see: whether a GPU agrees (A3's harness, against this); whether the NAMED CHOICES
 * in ffreference.h are what D3D9 drivers did (only a Windows machine could say); game draws (A3's
 * harness feeds them).
 */

#include "test_harness.h"

#include "ffreference/ffreference.h"

#include <algorithm>
#include <math.h>
#include <vector>

using namespace FFRef;

namespace {

const double EPS = 1e-9;

Color rgba( double r, double g, double b, double a ) { Color c = { r, g, b, a }; return c; }

Light directional( double dx, double dy, double dz )
{
	Light l;
	memset( &l, 0, sizeof( l ) );
	l.enabled = true;
	l.type = LIGHT_DIRECTIONAL;
	l.direction[0] = dx; l.direction[1] = dy; l.direction[2] = dz;
	l.range = 1000;
	l.attenuation0 = 1;
	return l;
}

Vertex screenVertex( double x, double y, double z, double rhw, Color diffuse )
{
	Vertex v;
	memset( &v, 0, sizeof( v ) );
	v.position[0] = x; v.position[1] = y; v.position[2] = z; v.position[3] = rhw;
	v.diffuse = diffuse;
	return v;
}

Vertex worldVertex( double x, double y, double z, Color diffuse )
{
	Vertex v;
	memset( &v, 0, sizeof( v ) );
	v.position[0] = x; v.position[1] = y; v.position[2] = z;
	v.diffuse = diffuse;
	return v;
}

/// Pretransformed, untextured, unlit, uncull'd: the plainest draw
DrawState screenState( int w, int h )
{
	DrawState s;
	s.setDefaults( w, h );
	s.pretransformed = true;
	s.hasDiffuse = true;
	s.renderState[RS_CULLMODE] = CULL_NONE;
	return s;
}

Target exactTarget( int w, int h )
{
	Target t;
	t.create( w, h );
	t.colorBits = 0;		// hand values are exact; no 8-bit rounding
	t.clear( rgba( 0, 0, 0, 0 ) );
	return t;
}

Color at( const Target &t, int x, int y ) { return t.color[(size_t)y * t.width + x]; }

/// A triangle far larger than the target, so every pixel is well inside it
void fill( const DrawState &s, Target &t, double z, Color c, Report *report = 0, unsigned mutations = 0 )
{
	const Vertex v[3] = { screenVertex( -100, -100, z, 1, c ), screenVertex( 300, -100, z, 1, c ), screenVertex( -100, 300, z, 1, c ) };
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, report, mutations ) );
}

std::vector<uint8_t> toRGBA8( const Target &t )
{
	std::vector<uint8_t> out( (size_t)t.width * t.height * 4 );
	for (size_t i = 0; i < t.color.size(); ++i)
	{
		const Color &c = t.color[i];
		const double ch[4] = { c.r, c.g, c.b, c.a };
		for (int k = 0; k < 4; ++k)
			out[i * 4 + k] = (uint8_t)floor( ch[k] * 255.0 + 0.5 );
	}
	return out;
}

Texture solidLevels( const double *reds, int levels, int size )
{
	Texture t;
	t.type = TEXTURE_2D;
	for (int l = 0; l < levels; ++l)
	{
		TextureLevel level;
		level.width = level.height = size >> l;
		level.texels.assign( (size_t)level.width * level.height, rgba( reds[l], 0, 0, 1 ) );
		t.levels.push_back( level );
	}
	return t;
}

/// 4x4, texel (i, j) = (i/4, j/4, 0, 1)
Texture ramp4()
{
	Texture t;
	t.type = TEXTURE_2D;
	TextureLevel level;
	level.width = level.height = 4;
	for (int j = 0; j < 4; ++j)
		for (int i = 0; i < 4; ++i)
			level.texels.push_back( rgba( i / 4.0, j / 4.0, 0, 1 ) );
	t.levels.push_back( level );
	return t;
}

}  // namespace

// ---- lighting: "Mathematics of Lighting" and subpages ---------------------------------------------

TEST(ffref_lighting_directional_ambient_diffuse)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	s.renderState[RS_AMBIENT] = 0x00202020;		// Ga = 32/255
	s.material.ambient = rgba( 1, 1, 1, 1 );
	s.material.diffuse = rgba( 0.5, 0.5, 0.5, 0.8 );
	s.lights[0] = directional( 0, 0, 1 );			// Ldir = -(0,0,1)
	s.lights[0].diffuse = rgba( 1, 0.5, 0.25, 1 );
	s.lights[0].ambient = rgba( 0.1, 0.1, 0.1, 1 );
	const double P[3] = { 0, 0, 10 }, N[3] = { 0, 0, -1 };	// N.Ldir = 1
	const LitVertex lit = light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) );
	// Ca(Ga + La) + Cd Ld (N.L): r = (32/255 + .1) + .5 * 1, g = .. + .5 * .5, b = .. + .5 * .25
	const double amb = 32.0 / 255.0 + 0.1;
	CHECK_NEAR( lit.diffuse.r, amb + 0.5, EPS );
	CHECK_NEAR( lit.diffuse.g, amb + 0.25, EPS );
	CHECK_NEAR( lit.diffuse.b, amb + 0.125, EPS );
	CHECK_NEAR( lit.diffuse.a, 0.8, EPS );		// the diffuse source's alpha (N5)
	// no normal: N.L = 0, only ambient remains (D3DRS_LIGHTING)
	const LitVertex bare = light( s, P, N, false, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) );
	CHECK_NEAR( bare.diffuse.r, amb, EPS );
}

TEST(ffref_lighting_point_attenuation_and_range)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	Light &l = s.lights[0];
	memset( &l, 0, sizeof( l ) );
	l.enabled = true;
	l.type = LIGHT_POINT;
	l.diffuse = rgba( 1, 1, 1, 1 );
	l.range = 100;
	l.attenuation0 = 1; l.attenuation1 = 0.1; l.attenuation2 = 0.01;
	const double P[3] = { 0, 0, 10 }, N[3] = { 0, 0, -1 };
	// d = 10: 1 / (1 + .1*10 + .01*100) = 1/3; Ldir = norm(Lp - P) = (0,0,-1), N.L = 1
	CHECK_NEAR( light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).diffuse.r, 1.0 / 3.0, EPS );
	// armed: without attenuation it would be 1
	CHECK_NEAR( light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ), MUTATE_NO_ATTENUATION ).diffuse.r, 1.0, EPS );
	l.range = 5;		// beyond the range: Atten = 0
	CHECK_NEAR( light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).diffuse.r, 0.0, EPS );
}

TEST(ffref_lighting_spot_cone)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	Light &l = s.lights[0];
	memset( &l, 0, sizeof( l ) );
	l.enabled = true;
	l.type = LIGHT_SPOT;
	l.diffuse = rgba( 1, 1, 1, 1 );
	l.direction[2] = 1;
	l.range = 100;
	l.attenuation0 = 1;
	l.theta = 0.5; l.phi = 1.0; l.falloff = 1;
	const double cosTheta = cos( 0.25 ), cosPhi = cos( 0.5 );
	struct At
	{
		static double diffuse( const DrawState &s, double rho )
		{
			const double a = acos( rho );
			double P[3] = { 10 * tan( a ), 0, 10 };
			const double len = sqrt( P[0] * P[0] + P[2] * P[2] );
			const double N[3] = { -P[0] / len, 0, -P[2] / len };	// facing the light: N.L = 1
			return light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).diffuse.r;
		}
	};
	// halfway between cos(phi/2) and cos(theta/2): ((rho - cos(phi/2)) / (cos(theta/2) - cos(phi/2)))^1 = .5
	CHECK_NEAR( At::diffuse( s, (cosTheta + cosPhi) / 2 ), 0.5, 1e-9 );
	CHECK_NEAR( At::diffuse( s, cos( 0.1 ) ), 1.0, 1e-9 );		// inside theta/2
	CHECK_NEAR( At::diffuse( s, cos( 0.6 ) ), 0.0, 1e-9 );		// outside phi/2
	l.falloff = 2;
	CHECK_NEAR( At::diffuse( s, (cosTheta + cosPhi) / 2 ), 0.25, 1e-9 );
}

TEST(ffref_lighting_specular_halfway)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	s.renderState[RS_SPECULARENABLE] = 1;
	s.renderState[RS_LOCALVIEWER] = 0;
	const double r2 = sqrt( 0.5 );
	s.lights[0] = directional( -r2, 0, r2 );		// Ldir = (r2, 0, -r2)
	s.lights[0].specular = rgba( 1, 1, 1, 1 );
	s.material.specular = rgba( 0.5, 0.5, 0.5, 1 );
	s.material.power = 2;
	const double P[3] = { 0, 0, 10 }, down[3] = { 0, 0, -1 };
	// non-local: H = norm((0,0,-1) + Ldir) (N27, measured) = (.38268, 0, -.92388); N = (0,0,-1):
	// N.H = cos(22.5 deg); .5 * cos^2 = .5 * .853553...
	const double c = cos( M_PI / 8 );
	CHECK_NEAR( light( s, P, down, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).specular.r, 0.5 * c * c, EPS );
	// local: E = norm(-P) = (0,0,-1), the same here, so the same value
	s.renderState[RS_LOCALVIEWER] = 1;
	CHECK_NEAR( light( s, P, down, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).specular.r, 0.5 * c * c, EPS );
	// the light behind the surface: Ldir = (r2, 0, r2), N.L = -r2 < 0, while N.H = sin(22.5) > 0.  Gated: 0
	// (N3, measured; the page's formula alone would give .5 * sin^2)
	s.lights[0] = directional( -r2, 0, -r2 );
	s.lights[0].specular = rgba( 1, 1, 1, 1 );
	const LitVertex behind = light( s, P, down, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) );
	CHECK_NEAR( behind.specular.r, 0.0, EPS );
	CHECK_NEAR( behind.diffuse.r, 0.0, EPS );
	// SPECULARENABLE off: no specular lighting (N6)
	s.lights[0] = directional( -r2, 0, r2 );
	s.lights[0].specular = rgba( 1, 1, 1, 1 );
	s.renderState[RS_SPECULARENABLE] = 0;
	CHECK_NEAR( light( s, P, down, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).specular.r, 0.0, EPS );
}

TEST(ffref_lighting_material_sources)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	s.hasDiffuse = s.hasSpecular = true;
	s.renderState[RS_EMISSIVEMATERIALSOURCE] = MCS_COLOR2;
	const double P[3] = { 0, 0, 10 }, N[3] = { 0, 0, -1 };
	// no lights, no ambient: the lit diffuse is the emissive, here the vertex specular colour
	LitVertex lit = light( s, P, N, true, rgba( 0.9, 0.9, 0.9, 0.6 ), rgba( 0.2, 0.3, 0.4, 1 ) );
	CHECK_NEAR( lit.diffuse.r, 0.2, EPS );
	CHECK_NEAR( lit.diffuse.g, 0.3, EPS );
	CHECK_NEAR( lit.diffuse.b, 0.4, EPS );
	CHECK_NEAR( lit.diffuse.a, 0.6, EPS );		// diffuse source COLOR1: the vertex's alpha
	// COLORVERTEX off: every source is the material's (emissive 0, diffuse alpha 1)
	s.renderState[RS_COLORVERTEX] = 0;
	lit = light( s, P, N, true, rgba( 0.9, 0.9, 0.9, 0.6 ), rgba( 0.2, 0.3, 0.4, 1 ) );
	CHECK_NEAR( lit.diffuse.r, 0.0, EPS );
	CHECK_NEAR( lit.diffuse.a, 1.0, EPS );
	// a source naming a colour the vertex lacks falls back to the material ("Specular Lighting")
	s.renderState[RS_COLORVERTEX] = 1;
	s.hasSpecular = false;
	s.material.emissive = rgba( 0.7, 0, 0, 1 );
	CHECK_NEAR( light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 0.2, 0.3, 0.4, 1 ) ).diffuse.r, 0.7, EPS );
}

/* N7 as Windows' Direct3D 9 measured it (WARP, HAL and REF): an absent specular is 0x00000000, an absent
   diffuse 0xFFFFFFFF.  Read through D3DTA_SPECULAR (colour and alpha), added with SPECULARENABLE, and the
   diffuse read with no diffuse in the vertex. */
TEST(ffref_absent_vertex_colours_as_windows_measures_them)
{
	DrawState s = screenState( 4, 4 );
	s.hasSpecular = false;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_COLORARG1] = TA_SPECULAR;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAARG1] = TA_SPECULAR;
	Target t = exactTarget( 4, 4 );
	fill( s, t, 0.5, rgba( 1, 0, 0, 0.25 ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.0, EPS );		// case A: black
	CHECK_NEAR( at( t, 1, 1 ).a, 0.0, EPS );		// case D: alpha 0
	// specular add with no specular: red stays red (case B)
	s = screenState( 4, 4 );
	s.hasSpecular = false;
	s.renderState[RS_SPECULARENABLE] = 1;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE;
	s.stageState[0][TSS_ALPHAARG1] = TA_DIFFUSE;		// the default reads a texture this draw has not got
	t = exactTarget( 4, 4 );
	fill( s, t, 0.5, rgba( 1, 0, 0, 1 ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 1.0, EPS );
	CHECK_NEAR( at( t, 1, 1 ).g, 0.0, EPS );
	// the diffuse read with no diffuse in the vertex: white (case C)
	s = screenState( 4, 4 );
	s.hasDiffuse = false;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE;
	s.stageState[0][TSS_ALPHAARG1] = TA_DIFFUSE;
	t = exactTarget( 4, 4 );
	fill( s, t, 0.5, rgba( 0, 0, 0, 0 ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 1.0, EPS );
	CHECK_NEAR( at( t, 1, 1 ).b, 1.0, EPS );
}

// ---- the device's known list, measured on Windows' own Direct3D 9 --------------------------------
// Each value below is what Tests/ffreference/knownprobe_windows.cpp read back on the Microsoft Basic
// Render Driver (WARP) as a HAL device and on the reference rasteriser (d3dref9.dll) as REF, 2026-09-27
// (ffreference/knownprobe_windows.cpp).  The probe's scene is rebuilt here
// draw for draw: a 64x64 A8R8G8B8 target cleared to 0xFF3F2F1F, identity transforms, lighting off,
// stage 0 SELECTARG1 DIFFUSE for colour and alpha.  Values are bytes; a tolerance of 1 covers the
// 8-bit rounding where WARP and REF themselves differ by one.

namespace {

DrawState probeState( bool pretransformed )
{
	DrawState s;
	s.setDefaults( 64, 64 );
	s.pretransformed = pretransformed;
	s.hasDiffuse = pretransformed;
	s.hasSpecular = pretransformed;
	s.renderState[RS_CULLMODE] = CULL_NONE;
	s.renderState[RS_ZENABLE] = 0;
	s.renderState[RS_LIGHTING] = 0;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAARG1] = TA_DIFFUSE;
	return s;
}

Target probeTarget()
{
	Target t;
	t.create( 64, 64 );
	t.colorBits = 8;
	t.hasAlpha = true;
	t.clear( colorFromD3D( 0xFF3F2F1F ) );
	return t;
}

/// The probe's pretransformed vertex: pixel corner (x, y), as it placed them (x - .5)
Vertex tl( double x, double y, uint32_t diffuse, uint32_t specular = 0xFF000000 )
{
	Vertex v = screenVertex( x - 0.5, y - 0.5, 0.5, 1, colorFromD3D( diffuse ) );
	v.specular = colorFromD3D( specular );
	return v;
}

/// The probe's lit quad: clip-space corners at z .5, normal (0,0,-1), as a strip
void litQuad( const DrawState &s, Target &t )
{
	Vertex v[4] = { worldVertex( -1, 1, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( 1, 1, 0.5, rgba( 0, 0, 0, 0 ) ),
		worldVertex( -1, -1, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( 1, -1, 0.5, rgba( 0, 0, 0, 0 ) ) };
	for (int i = 0; i < 4; ++i)
		v[i].normal[2] = -1;
	CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
}

DrawState probeLit()
{
	DrawState s = probeState( false );
	s.hasNormal = true;
	s.renderState[RS_LIGHTING] = 1;
	s.renderState[RS_COLORVERTEX] = 0;
	s.renderState[RS_AMBIENT] = 0;
	memset( &s.material, 0, sizeof( s.material ) );
	return s;
}

/// Every channel of pixel (x, y) within tol of the measured 0xAARRGGBB
void measured( const Target &t, int x, int y, uint32_t argb, int tol )
{
	const Color c = at( t, x, y ), m = colorFromD3D( argb );
	CHECK_NEAR( c.a * 255.0, m.a * 255.0, tol + 1e-6 );
	CHECK_NEAR( c.r * 255.0, m.r * 255.0, tol + 1e-6 );
	CHECK_NEAR( c.g * 255.0, m.g * 255.0, tol + 1e-6 );
	CHECK_NEAR( c.b * 255.0, m.b * 255.0, tol + 1e-6 );
}

}  // namespace

TEST(ffref_flat_shading_as_windows_measures_it)
{
	const uint32_t red = 0xFFFF0000, green = 0xFF00FF00, blue = 0xFF0000FF, white = 0xFFFFFFFF;
	DrawState s = probeState( true );
	s.renderState[RS_SHADEMODE] = SHADE_FLAT;
	// F8a: a list takes its first vertex's colour
	{
		const Vertex v[3] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 0, 64, blue ) };
		Target t = probeTarget();
		CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
		measured( t, 10, 10, 0xFFFF0000, 0 );
		measured( t, 30, 20, 0xFFFF0000, 0 );
	}
	// F8b: a strip's triangle i takes vertex i
	{
		const Vertex v[4] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 0, 64, blue ), tl( 64, 64, white ) };
		Target t = probeTarget();
		CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
		measured( t, 10, 10, 0xFFFF0000, 0 );
		measured( t, 54, 54, 0xFF00FF00, 0 );
	}
	// F8c: a fan's triangle i takes vertex i + 1
	{
		const Vertex v[4] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 64, 64, blue ), tl( 0, 64, white ) };
		Target t = probeTarget();
		CHECK( draw( s, PT_TRIANGLEFAN, v, 4, 0, 4, t ) );
		measured( t, 54, 10, 0xFF00FF00, 0 );
		measured( t, 10, 54, 0xFF0000FF, 0 );
	}
	// F8d: indexed, the first index's vertex
	{
		const Vertex v[3] = { tl( 0, 0, red ), tl( 64, 0, green ), tl( 0, 64, blue ) };
		const uint32_t idx[3] = { 2, 0, 1 };
		Target t = probeTarget();
		CHECK( draw( s, PT_TRIANGLELIST, v, 3, idx, 3, t ) );
		measured( t, 10, 10, 0xFF0000FF, 0 );
	}
	// F8e: alpha is flat too
	{
		const Vertex v[3] = { tl( 0, 0, 0x00FFFFFF ), tl( 64, 0, 0x80FFFFFF ), tl( 0, 64, 0xFFFFFFFF ) };
		Target t = probeTarget();
		CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
		measured( t, 10, 10, 0x00FFFFFF, 0 );
		measured( t, 30, 20, 0x00FFFFFF, 0 );
	}
	// F8f: the specular is flat on REF (WARP interpolates it: 0xFFAB2A2A and 0xFF347A52, its Gouraud values)
	{
		DrawState f = s;
		f.renderState[RS_SPECULARENABLE] = 1;
		const Vertex v[3] = { tl( 0, 0, 0xFF000000, red ), tl( 64, 0, 0xFF000000, green ), tl( 0, 64, 0xFF000000, blue ) };
		Target t = probeTarget();
		CHECK( draw( f, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
		measured( t, 10, 10, 0xFFFF0000, 0 );
		measured( t, 30, 20, 0xFFFF0000, 0 );
		// F8g, the control: the same Gouraud, where WARP and REF agree
		f.renderState[RS_SHADEMODE] = SHADE_GOURAUD;
		t = probeTarget();
		CHECK( draw( f, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
		measured( t, 10, 10, 0xFFAB2A2A, 1 );
		measured( t, 30, 20, 0xFF347A52, 1 );
	}
}

TEST(ffref_known_list_lighting_as_windows_measures_it)
{
	// F6: an unlit vertex specular is added under SPECULARENABLE
	{
		DrawState s = probeState( true );
		s.renderState[RS_SPECULARENABLE] = 1;
		const Vertex v[4] = { tl( 0, 0, 0xFFFF0000, 0xFF00FF00 ), tl( 64, 0, 0xFFFF0000, 0xFF00FF00 ),
			tl( 0, 64, 0xFFFF0000, 0xFF00FF00 ), tl( 64, 64, 0xFFFF0000, 0xFF00FF00 ) };
		Target t = probeTarget();
		CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
		measured( t, 32, 32, 0xFFFFFF00, 0 );
		s.renderState[RS_SPECULARENABLE] = 0;
		t = probeTarget();
		CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
		measured( t, 32, 32, 0xFFFF0000, 0 );
	}
	// F1: per-light ambient, attenuated, and none out of range (material ambient white, the rest 0)
	{
		DrawState s = probeLit();
		s.material.ambient = rgba( 1, 1, 1, 1 );
		s.lights[0] = directional( 0, 0, 1 );
		s.lights[0].ambient = rgba( 0.4, 0.2, 0, 1 );
		Target t = probeTarget();
		litQuad( s, t );
		measured( t, 32, 32, 0x00663300, 0 );
		Light p;
		memset( &p, 0, sizeof( p ) );
		p.enabled = true;
		p.type = LIGHT_POINT;
		p.position[2] = -1;
		p.range = 1000;
		p.attenuation0 = 2;
		p.ambient = rgba( 0.8, 0, 0.4, 1 );
		s.lights[0] = p;
		t = probeTarget();
		litQuad( s, t );
		measured( t, 32, 32, 0x00660033, 0 );
		s.lights[0].range = 0.1;
		t = probeTarget();
		litQuad( s, t );
		measured( t, 32, 32, 0x00000000, 0 );
	}
	// F2, N27, N3: specular white, power 1, normal (0,0,-1); LOCALVIEWER on and off
	{
		struct Case { double d[3]; int localViewer; uint32_t centre, corner11, corner6262; };
		const Case cases[] = {
			{ { 0, 0.6, 0.8 }, 1, 0x00BDBDBD, 0x009F9F9F, 0x00DADADA },
			{ { 0, 0.6, 0.8 }, 0, 0x00F2F2F2, 0x00F2F2F2, 0x00F2F2F2 },
			{ { 0, -0.9, -0.4359 }, 1, 0, 0, 0 },
			{ { 0, -0.9, -0.4359 }, 0, 0, 0, 0 },		// N3's gate: N.H would be .531
			{ { 0.6, 0, 0.8 }, 1, 0x00BDBDBD, 0x00DBDBDB, 0x00A0A0A0 },
			{ { 0.6, 0, 0.8 }, 0, 0x00F2F2F2, 0x00F2F2F2, 0x00F2F2F2 },
		};
		for (const Case &c : cases)
		{
			DrawState s = probeLit();
			s.renderState[RS_SPECULARENABLE] = 1;
			s.renderState[RS_LOCALVIEWER] = c.localViewer;
			s.material.specular = rgba( 1, 1, 1, 1 );
			s.material.power = 1;
			s.lights[0] = directional( c.d[0], c.d[1], c.d[2] );
			s.lights[0].specular = rgba( 1, 1, 1, 1 );
			Target t = probeTarget();
			litQuad( s, t );
			measured( t, 32, 32, c.centre, 1 );
			measured( t, 1, 1, c.corner11, 1 );
			measured( t, 62, 62, c.corner6262, 1 );
		}
	}
}

TEST(ffref_known_list_stages_as_windows_measures_them)
{
	// F3, N17: DOTPRODUCT3 of DIFFUSE and TFACTOR, signed, replicated into alpha
	{
		const uint32_t cases[3][3] = { { 0x40FF8080, 0xFFFF8080, 0xFFFFFFFF }, { 0x40C08080, 0xFFC08080, 0x41414141 },
			{ 0x40A0A0A0, 0xFFA0A0A0, 0x32323232 } };
		for (int c = 0; c < 3; ++c)
		{
			DrawState s = probeState( true );
			s.renderState[RS_TEXTUREFACTOR] = cases[c][1];
			s.stageState[0][TSS_COLOROP] = TOP_DOTPRODUCT3;
			s.stageState[0][TSS_COLORARG2] = TA_TFACTOR;
			const Vertex v[4] = { tl( 0, 0, cases[c][0] ), tl( 64, 0, cases[c][0] ), tl( 0, 64, cases[c][0] ), tl( 64, 64, cases[c][0] ) };
			Target t = probeTarget();
			CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
			measured( t, 32, 32, cases[c][2], 0 );
		}
	}
	// F4, N13: under TTFF_COUNT2, (u, v) is padded (u, v, 1, 0), so _31/_32 move it and _41/_42 do not.
	// 4x4 texture, texel (i, j) = R 64i G 64j, POINT, uv (.1, .1)
	{
		Texture tex;
		tex.type = TEXTURE_2D;
		TextureLevel level;
		level.width = level.height = 4;
		for (int j = 0; j < 4; ++j)
			for (int i = 0; i < 4; ++i)
				level.texels.push_back( rgba( 64 * i / 255.0, 64 * j / 255.0, 0, 1 ) );
		tex.levels.push_back( level );
		const uint32_t expect[3] = { 0xFF000000, 0xFF804000, 0xFF000000 };
		for (int c = 0; c < 3; ++c)
		{
			DrawState s = probeState( false );
			s.texCoordSets = 1;
			s.texCoordSize[0] = 2;
			s.textures[0] = &tex;
			s.stageState[0][TSS_COLORARG1] = TA_TEXTURE;
			if (c == 1) { s.textureTransform[0].m[2][0] = 0.5; s.textureTransform[0].m[2][1] = 0.25; }
			if (c == 2) { s.textureTransform[0].m[3][0] = 0.5; s.textureTransform[0].m[3][1] = 0.25; }
			if (c > 0)
				s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT2;
			Vertex v[4] = { worldVertex( -1, 1, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( 1, 1, 0.5, rgba( 0, 0, 0, 0 ) ),
				worldVertex( -1, -1, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( 1, -1, 0.5, rgba( 0, 0, 0, 0 ) ) };
			for (int i = 0; i < 4; ++i) { v[i].tex[0][0] = 0.1; v[i].tex[0][1] = 0.1; }
			Target t = probeTarget();
			CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
			measured( t, 32, 32, expect[c], 0 );
		}
	}
	// F11, N15: the LOD.  256x256, 9 levels, level L = R 28L, LINEAR everywhere: R = 28 lambda.  WARP and
	// REF both follow the L2 length of the longer axis (the rotated cases rule out L-infinity and L1);
	// where they differ by one, FFReference's exact lambda lies between
	{
		Texture tex;
		tex.type = TEXTURE_2D;
		for (int l = 0; l < 9; ++l)
		{
			TextureLevel level;
			level.width = level.height = 256 >> l;
			level.texels.assign( (size_t)level.width * level.height, rgba( 28 * l / 255.0, 0, 0, 1 ) );
			tex.levels.push_back( level );
		}
		struct Case { double d[4]; int warp, ref; };
		const Case cases[4] = { { { 3, 0, 0, 3 }, 44, 45 }, { { 2, -2, 2, 2 }, 42, 42 }, { { 4, 0, 0, 1 }, 56, 56 },
			{ { 2.5, 1.5, -1.5, 2.5 }, 42, 43 } };
		for (const Case &c : cases)
		{
			DrawState s = probeState( true );
			s.hasDiffuse = s.hasSpecular = false;
			s.texCoordSets = 1;
			s.texCoordSize[0] = 2;
			s.textures[0] = &tex;
			s.stageState[0][TSS_COLORARG1] = TA_TEXTURE;
			s.samplerState[0][SAMP_MINFILTER] = s.samplerState[0][SAMP_MAGFILTER] = s.samplerState[0][SAMP_MIPFILTER] = TEXF_LINEAR;
			Vertex v[4];
			const double xy[4][2] = { { 0, 0 }, { 64, 0 }, { 0, 64 }, { 64, 64 } };
			for (int i = 0; i < 4; ++i)
			{
				v[i] = screenVertex( xy[i][0] - 0.5, xy[i][1] - 0.5, 0.5, 1, rgba( 1, 1, 1, 1 ) );
				v[i].tex[0][0] = (c.d[0] * xy[i][0] + c.d[2] * xy[i][1]) / 256.0;
				v[i].tex[0][1] = (c.d[1] * xy[i][0] + c.d[3] * xy[i][1]) / 256.0;
			}
			Target t = probeTarget();
			CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
			const double r = at( t, 32, 32 ).r * 255.0;
			CHECK( r >= std::min( c.warp, c.ref ) - 1e-6 && r <= std::max( c.warp, c.ref ) + 1e-6 );
		}
	}
}

TEST(ffref_known_list_second_probe_as_windows_measures_it)
{
	// N14: the camera-space reflection vector, read through a 256x256 coordinate texture (texel (x, y) =
	// R x, G y, POINT, CLAMP) with COUNT2 and u = .25 Rz + .5, v = .25 Ry + .5
	{
		Texture tex;
		tex.type = TEXTURE_2D;
		TextureLevel level;
		level.width = level.height = 256;
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
				level.texels.push_back( rgba( x / 255.0, y / 255.0, 0, 1 ) );
		tex.levels.push_back( level );
		struct Case { double ny, nz; int localViewer; uint32_t centre, corner11; };
		const Case cases[4] = { { 0, -1, 1, 0xFF6A8000, 0xFF6AA900 }, { 0, -1, 0, 0xFF408000, 0xFF408000 },
			{ 0.6, -0.8, 1, 0xFF7A9400, 0xFFA1A000 }, { 0.6, -0.8, 0, 0xFF6EBD00, 0xFF6EBD00 } };
		for (const Case &c : cases)
		{
			DrawState s = probeState( false );
			s.hasNormal = true;
			s.renderState[RS_LOCALVIEWER] = c.localViewer;
			s.textures[0] = &tex;
			s.stageState[0][TSS_COLORARG1] = TA_TEXTURE;
			s.stageState[0][TSS_TEXCOORDINDEX] = TSS_TCI_CAMERASPACEREFLECTIONVECTOR;
			s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT2;
			s.samplerState[0][SAMP_ADDRESSU] = s.samplerState[0][SAMP_ADDRESSV] = TADDRESS_CLAMP;
			Matrix &m = s.textureTransform[0];
			memset( &m, 0, sizeof( m ) );
			m.m[2][0] = 0.25; m.m[1][1] = 0.25; m.m[3][0] = 0.5; m.m[3][1] = 0.5; m.m[3][3] = 1;
			Vertex v[4] = { worldVertex( -1, 1, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( 1, 1, 0.5, rgba( 0, 0, 0, 0 ) ),
				worldVertex( -1, -1, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( 1, -1, 0.5, rgba( 0, 0, 0, 0 ) ) };
			for (int i = 0; i < 4; ++i) { v[i].normal[1] = c.ny; v[i].normal[2] = c.nz; }
			Target t = probeTarget();
			CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
			measured( t, 32, 32, c.centre, 1 );
			measured( t, 1, 1, c.corner11, 1 );
		}
	}
	// N28: a stage naming a coordinate set the vertices lack reads (0, 0): texel (0, 0), not (2, 2)
	{
		Texture tex = ramp4();
		for (Color &c : tex.levels[0].texels)
			c.r = floor( c.r * 4 + 0.5 ) * 64 / 255.0, c.g = floor( c.g * 4 + 0.5 ) * 64 / 255.0;
		for (int missing = 0; missing < 2; ++missing)
		{
			DrawState s = probeState( false );
			s.texCoordSets = 1;
			s.texCoordSize[0] = 2;
			s.textures[0] = &tex;
			s.stageState[0][TSS_COLORARG1] = TA_TEXTURE;
			s.stageState[0][TSS_TEXCOORDINDEX] = missing;
			Vertex v[4] = { worldVertex( -1, 1, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( 1, 1, 0.5, rgba( 0, 0, 0, 0 ) ),
				worldVertex( -1, -1, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( 1, -1, 0.5, rgba( 0, 0, 0, 0 ) ) };
			for (int i = 0; i < 4; ++i) { v[i].tex[0][0] = 0.6; v[i].tex[0][1] = 0.6; }
			Target t = probeTarget();
			CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
			measured( t, 32, 32, missing ? 0xFF000000 : 0xFF808000, 0 );
		}
	}
	// N3: the gate's controls, N.L > 0: N.H .848 and .755 with the viewer at (0,0,-1)
	{
		const double dirs[2][3] = { { 0, -0.9, 0.4359 }, { 0, -0.99, 0.1411 } };
		const uint32_t expect[2] = { 0x00D8D8D8, 0x00C1C1C1 };
		for (int d = 0; d < 2; ++d)
		{
			DrawState s = probeLit();
			s.renderState[RS_SPECULARENABLE] = 1;
			s.renderState[RS_LOCALVIEWER] = 0;
			s.material.specular = rgba( 1, 1, 1, 1 );
			s.material.power = 1;
			s.lights[0] = directional( dirs[d][0], dirs[d][1], dirs[d][2] );
			s.lights[0].specular = rgba( 1, 1, 1, 1 );
			Target t = probeTarget();
			litQuad( s, t );
			measured( t, 32, 32, expect[d], 1 );
		}
	}
	// N11: fog (from the specular alpha, no fog mode) is interpolated under flat shading
	for (int flat = 1; flat >= 0; --flat)
	{
		DrawState s = probeState( true );
		s.renderState[RS_SHADEMODE] = flat ? SHADE_FLAT : SHADE_GOURAUD;
		s.renderState[RS_FOGENABLE] = 1;
		s.renderState[RS_FOGCOLOR] = 0xFF000000;
		s.renderState[RS_FOGTABLEMODE] = FOG_NONE;
		s.renderState[RS_FOGVERTEXMODE] = FOG_NONE;
		const Vertex v[3] = { tl( 0, 0, 0xFFFFFFFF, 0x00000000 ), tl( 64, 0, 0xFFFFFFFF, 0x80000000 ), tl( 0, 64, 0xFFFFFFFF, 0xFF000000 ) };
		Target t = probeTarget();
		CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
		measured( t, 10, 10, 0xFF3F3F3F, 1 );
		measured( t, 30, 20, 0xFF8F8F8F, 1 );
	}
}

TEST(ffref_normals_by_the_inverse_transpose)
{
	// world shears x by y (x' = x + y): the plane x = 0 becomes x' = y', whose normal is (1,-1,0)/r2.
	// The inverse-transpose takes (1,0,0) there; the plain matrix would not.  Light along that normal.
	DrawState s;
	s.setDefaults( 4, 4 );
	s.renderState[RS_CULLMODE] = CULL_NONE;
	s.renderState[RS_NORMALIZENORMALS] = 1;
	s.world.m[1][0] = 1.0;
	const double r2 = sqrt( 0.5 );
	s.lights[0] = directional( -r2, r2, 0 );		// Ldir = (r2, -r2, 0)
	s.lights[0].diffuse = rgba( 1, 1, 1, 1 );
	s.hasNormal = true;
	Vertex v[3] = { worldVertex( -10, -10, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( -10, 30, 0.5, rgba( 0, 0, 0, 0 ) ),
		worldVertex( 30, -10, 0.5, rgba( 0, 0, 0, 0 ) ) };
	for (int i = 0; i < 3; ++i)
		v[i].normal[0] = 1.0;
	Target t = exactTarget( 4, 4 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 2, 2 ).r, 1.0, EPS );		// N.L = 1; the plain matrix's normal would give r2
}

// ---- fog: "Fog Formulas", "Vertex Fog", "Pixel Fog" -----------------------------------------------

TEST(ffref_fog_formulas)
{
	CHECK_NEAR( fogFactor( FOG_LINEAR, 35, 10, 110, 0 ), 0.75, EPS );		// (110 - 35) / 100
	CHECK_NEAR( fogFactor( FOG_LINEAR, 200, 10, 110, 0 ), 0.0, EPS );		// clamped
	CHECK_NEAR( fogFactor( FOG_EXP, 2, 0, 0, 0.5 ), exp( -1.0 ), EPS );
	CHECK_NEAR( fogFactor( FOG_EXP2, 1, 0, 0, 0.5 ), exp( -0.25 ), EPS );
	CHECK_NEAR( fogFactor( FOG_LINEAR, 5, 5, 5, 0 ), 1.0, EPS );			// end == start, N8
	CHECK_NEAR( fogFactor( FOG_NONE, 5, 0, 1, 1 ), 1.0, EPS );
}

TEST(ffref_fog_vertex_and_table)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	s.renderState[RS_CULLMODE] = CULL_NONE;
	s.renderState[RS_LIGHTING] = 0;
	s.hasDiffuse = true;
	s.renderState[RS_FOGENABLE] = 1;
	s.renderState[RS_FOGSTART] = floatBits( 0.0f );
	s.renderState[RS_FOGEND] = floatBits( 1.0f );
	s.renderState[RS_FOGVERTEXMODE] = FOG_LINEAR;
	const Color white = rgba( 1, 1, 1, 1 );
	const Vertex v[3] = { worldVertex( -10, -10, 0.25, white ), worldVertex( -10, 30, 0.25, white ), worldVertex( 30, -10, 0.25, white ) };

	// vertex fog: |z camera| = .25 -> f = .75, fog colour black
	Target t = exactTarget( 4, 4 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.75, EPS );
	CHECK_NEAR( at( t, 1, 1 ).a, 1.0, EPS );		// fog leaves alpha alone (N9)
	// armed: f and 1 - f swapped
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, 0, MUTATE_FOG_REVERSED ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.25, EPS );

	// table fog, affine projection: z-based, z/w = .25
	s.renderState[RS_FOGVERTEXMODE] = FOG_NONE;
	s.renderState[RS_FOGTABLEMODE] = FOG_LINEAR;
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.75, EPS );

	// table fog, perspective projection: w-based, eye distance 10 against end 40 -> .75
	s.projection.m[2][2] = 100.0 / 99.0; s.projection.m[3][2] = -100.0 / 99.0;
	s.projection.m[2][3] = 1.0; s.projection.m[3][3] = 0.0;
	s.renderState[RS_FOGEND] = floatBits( 40.0f );
	const Vertex far[3] = { worldVertex( -300, -300, 10, white ), worldVertex( -300, 900, 10, white ), worldVertex( 900, -300, 10, white ) };
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, far, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.75, 1e-6 );
}

// ---- the cascade: D3DTEXTUREOP, D3DTA ---------------------------------------------------------------

TEST(ffref_texture_ops)
{
	const Color a0 = rgba( 0.25, 0, 0, 0 ), a1 = rgba( 0.2, 0.4, 0.6, 0.8 ), a2 = rgba( 0.5, 0.25, 1.0, 0.5 );
	struct Op { int op; double r; };
	// red channel, arg1 .2, arg2 .5, arg0 .25, blend alpha .25
	const Op ops[] = {
		{ TOP_SELECTARG1, 0.2 }, { TOP_SELECTARG2, 0.5 }, { TOP_MODULATE, 0.1 }, { TOP_MODULATE2X, 0.2 },
		{ TOP_MODULATE4X, 0.4 }, { TOP_ADD, 0.7 }, { TOP_ADDSIGNED, 0.2 }, { TOP_ADDSIGNED2X, 0.4 },
		{ TOP_SUBTRACT, -0.3 }, { TOP_ADDSMOOTH, 0.6 },				// .2 + .5 * .8
		{ TOP_BLENDDIFFUSEALPHA, 0.425 }, { TOP_BLENDTEXTUREALPHA, 0.425 }, { TOP_BLENDFACTORALPHA, 0.425 },
		{ TOP_BLENDCURRENTALPHA, 0.425 },							// .2 * .25 + .5 * .75
		{ TOP_BLENDTEXTUREALPHAPM, 0.575 },							// .2 + .5 * .75
		{ TOP_MODULATEALPHA_ADDCOLOR, 0.6 },						// .2 + .8 * .5
		{ TOP_MODULATECOLOR_ADDALPHA, 0.9 },						// .2 * .5 + .8
		{ TOP_MODULATEINVALPHA_ADDCOLOR, 0.3 },						// .2 * .5 + .2
		{ TOP_MODULATEINVCOLOR_ADDALPHA, 1.2 },						// .8 * .5 + .8
		{ TOP_DOTPRODUCT3, 0.3 },		// 4 * ((-.3)(0) + (-.1)(-.25) + (.1)(.5)) = 4 * .075
		{ TOP_MULTIPLYADD, 0.35 },		// .25 + .2 * .5
		{ TOP_LERP, 0.425 } };			// .25 * .2 + .75 * .5
	for (size_t i = 0; i < sizeof( ops ) / sizeof( ops[0] ); ++i)
	{
		const double got = textureOp( ops[i].op, 0, a0, a1, a2, 0.25 );
		if (fabs( got - ops[i].r ) > EPS)
			printf( "  D3DTOP %d: %.6f, want %.6f\n", ops[i].op, got, ops[i].r );
		CHECK_NEAR( got, ops[i].r, EPS );
	}
	// armed: LERP with ARG1 and ARG2 swapped: .25 * .5 + .75 * .2
	CHECK_NEAR( textureOp( TOP_LERP, 0, a0, a1, a2, 0.25, MUTATE_LERP_SWAPPED ), 0.275, EPS );
}

TEST(ffref_cascade_through_draws)
{
	DrawState s = screenState( 2, 2 );
	Target t = exactTarget( 2, 2 );
	Texture red;
	red.type = TEXTURE_2D;
	TextureLevel one;
	one.width = one.height = 1;
	one.texels.push_back( rgba( 1, 0.5, 0, 0.25 ) );
	red.levels.push_back( one );
	s.texCoordSets = 1;

	// N20: stage 0 MODULATE of TEXTURE with no texture: the cascade is the diffuse
	fill( s, t, 0.5, rgba( 0.3, 0.6, 0.9, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).g, 0.6, EPS );

	// stage 0 texture into TEMP, stage 1 TFACTOR * TEMP into CURRENT
	s.textures[0] = &red;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_RESULTARG] = TA_TEMP;
	s.stageState[1][TSS_COLOROP] = TOP_MODULATE;
	s.stageState[1][TSS_COLORARG1] = TA_TFACTOR;
	s.stageState[1][TSS_COLORARG2] = TA_TEMP;
	s.stageState[1][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[1][TSS_ALPHAARG1] = TA_TEMP | TA_COMPLEMENT;
	s.renderState[RS_TEXTUREFACTOR] = 0xFF808080;
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( s, t, 0.5, rgba( 0.3, 0.6, 0.9, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 128.0 / 255.0, EPS );			// 128/255 * 1
	CHECK_NEAR( at( t, 0, 0 ).g, 0.5 * 128.0 / 255.0, EPS );
	CHECK_NEAR( at( t, 0, 0 ).a, 0.75, EPS );					// 1 - .25

	// DOTPRODUCT3 as a COLOROP writes alpha too (N17): diffuse (.75, .5, .5) . TFACTOR (1, 128/255, 128/255)
	// = 4 * ((.75-.5)(1-.5) + 0 * (128/255-.5) + 0 * (128/255-.5)) = .5, in RGB and in alpha
	DrawState d = screenState( 2, 2 );
	d.stageState[0][TSS_COLOROP] = TOP_DOTPRODUCT3;
	d.stageState[0][TSS_COLORARG1] = TA_DIFFUSE;
	d.stageState[0][TSS_COLORARG2] = TA_TFACTOR;
	d.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	d.stageState[0][TSS_ALPHAARG1] = TA_DIFFUSE;
	d.renderState[RS_TEXTUREFACTOR] = 0xFFFF8080;		// (1, 128/255, 128/255)
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( d, t, 0.5, rgba( 0.75, 0.5, 0.5, 0.1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5, EPS );
	CHECK_NEAR( at( t, 0, 0 ).b, 0.5, EPS );
	CHECK_NEAR( at( t, 0, 0 ).a, 0.5, EPS );		// not the alpha op's .1

	// PREMODULATE: stage 0 outputs ARG1 (diffuse .5); stage 1's CURRENT is premultiplied by its texture
	DrawState p = screenState( 2, 2 );
	p.texCoordSets = 1;
	p.stageState[0][TSS_COLOROP] = TOP_PREMODULATE;
	p.stageState[0][TSS_COLORARG1] = TA_DIFFUSE;
	p.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	p.stageState[0][TSS_ALPHAARG1] = TA_DIFFUSE;
	p.textures[1] = &red;
	p.stageState[1][TSS_COLOROP] = TOP_SELECTARG1;
	p.stageState[1][TSS_COLORARG1] = TA_CURRENT;
	p.stageState[1][TSS_ALPHAOP] = TOP_SELECTARG1;
	p.stageState[1][TSS_ALPHAARG1] = TA_CURRENT;
	p.stageState[1][TSS_TEXCOORDINDEX] = 0;
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( p, t, 0.5, rgba( 0.5, 0.5, 0.5, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5, EPS );		// .5 * 1
	CHECK_NEAR( at( t, 0, 0 ).g, 0.25, EPS );		// .5 * .5
	CHECK_NEAR( at( t, 0, 0 ).a, 0.25, EPS );		// 1 * .25
}

TEST(ffref_bump_env_map_offsets_the_next_stage)
{
	DrawState s = screenState( 2, 2 );
	s.texCoordSets = 1;
	Texture bump;
	bump.type = TEXTURE_2D;
	TextureLevel b;
	b.width = b.height = 1;
	b.texels.push_back( rgba( 0.5, 0.25, 0.8, 1 ) );		// du .5, dv .25, L .8
	bump.levels.push_back( b );
	Texture env = ramp4();
	s.textures[0] = &bump;
	s.stageState[0][TSS_COLOROP] = TOP_BUMPENVMAPLUMINANCE;
	s.stageState[0][TSS_BUMPENVMAT00] = floatBits( 0.5f );		// du' = du M00 + dv M10 = .25 + .25 = .5
	s.stageState[0][TSS_BUMPENVMAT10] = floatBits( 1.0f );		// dv' = du M01 + dv M11 = 0
	s.stageState[0][TSS_BUMPENVLSCALE] = floatBits( 1.0f );
	s.stageState[0][TSS_BUMPENVLOFFSET] = floatBits( 0.1f );	// L' = .8 + .1 = .9
	s.textures[1] = &env;
	s.stageState[1][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[1][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[1][TSS_ALPHAARG1] = TA_DIFFUSE;
	s.stageState[1][TSS_TEXCOORDINDEX] = 0;
	Target t = exactTarget( 2, 2 );
	Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ),
		screenVertex( -100, 300, 0.5, 1, rgba( 1, 1, 1, 1 ) ) };
	for (int i = 0; i < 3; ++i)
	{
		v[i].tex[0][0] = 0.0625;	// + .5 = .5625: texel 2 (M01 in M10's place would give .3125, texel 1)
		v[i].tex[0][1] = 0.125;
	}
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5 * 0.9, EPS );		// texel 2 (r = 2/4), times L'
}

// ---- sampling ---------------------------------------------------------------------------------------

TEST(ffref_sampling_point_linear_and_address_modes)
{
	const Texture t = ramp4();
	DrawState s;
	s.setDefaults( 1, 1 );
	uint32_t *sp = s.samplerState[0];
	// point: texel floor(u * 4)
	CHECK_NEAR( sample( t, sp, 0.375, 0.1, -1 ).r, 0.25, EPS );
	CHECK_NEAR( sample( t, sp, 0.49, 0.1, -1 ).r, 0.25, EPS );
	CHECK_NEAR( sample( t, sp, 0.5, 0.1, -1 ).r, 0.5, EPS );
	// armed: texel centres at i, not i + .5: floor(1.5 + .5) = 2
	CHECK_NEAR( sample( t, sp, 0.375, 0.1, -1, MUTATE_TEXEL_CORNER ).r, 0.5, EPS );
	// linear: at a centre, the texel; halfway between centres .375 and .625, the mean
	sp[SAMP_MAGFILTER] = sp[SAMP_MINFILTER] = TEXF_LINEAR;
	CHECK_NEAR( sample( t, sp, 0.375, 0.375, -1 ).r, 0.25, EPS );
	CHECK_NEAR( sample( t, sp, 0.5, 0.375, -1 ).r, 0.375, EPS );
	// at u = 0 the taps are texels -1 and 0, half each
	sp[SAMP_ADDRESSU] = TADDRESS_WRAP;
	CHECK_NEAR( sample( t, sp, 0.0, 0.375, -1 ).r, (0.75 + 0.0) / 2, EPS );
	sp[SAMP_ADDRESSU] = TADDRESS_CLAMP;
	CHECK_NEAR( sample( t, sp, 0.0, 0.375, -1 ).r, 0.0, EPS );
	sp[SAMP_ADDRESSU] = TADDRESS_MIRROR;
	CHECK_NEAR( sample( t, sp, 0.0, 0.375, -1 ).r, 0.0, EPS );
	CHECK_NEAR( sample( t, sp, 1.0, 0.375, -1 ).r, 0.75, EPS );		// taps 3 and 4 -> 3, 3
	sp[SAMP_ADDRESSU] = TADDRESS_BORDER;
	sp[SAMP_BORDERCOLOR] = 0xFF0000FF;		// blue
	const Color border = sample( t, sp, 1.0, 0.375, -1 );		// texel 3 and the border, half each
	CHECK_NEAR( border.r, 0.375, EPS );
	CHECK_NEAR( border.b, 0.5, EPS );
	// MIRRORONCE, point: floor(-1.2) = -2 mirrors to 1; far past the end clamps to 3
	sp[SAMP_ADDRESSU] = TADDRESS_MIRRORONCE;
	sp[SAMP_MAGFILTER] = sp[SAMP_MINFILTER] = TEXF_POINT;
	CHECK_NEAR( sample( t, sp, -0.3, 0.1, -1 ).r, 0.25, EPS );
	CHECK_NEAR( sample( t, sp, -3.0, 0.1, -1 ).r, 0.75, EPS );
	// rows run down from v = 0
	CHECK_NEAR( sample( t, sp, 0.1, 0.9, -1 ).g, 0.75, EPS );
}

TEST(ffref_mip_selection)
{
	const double reds[3] = { 0.0, 0.5, 1.0 };
	const Texture t = solidLevels( reds, 3, 4 );
	DrawState s;
	s.setDefaults( 1, 1 );
	uint32_t *sp = s.samplerState[0];
	sp[SAMP_MIPFILTER] = TEXF_NONE;
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 1.5 ).r, 0.0, EPS );		// no mipmapping: level 0
	sp[SAMP_MIPFILTER] = TEXF_POINT;
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 0.4 ).r, 0.0, EPS );		// nearest: 0
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 0.6 ).r, 0.5, EPS );		// nearest: 1
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 1.6 ).r, 1.0, EPS );		// nearest: 2
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 9.0 ).r, 1.0, EPS );		// clamped to the last
	sp[SAMP_MIPFILTER] = TEXF_LINEAR;
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 0.25 ).r, 0.125, EPS );	// .75 * 0 + .25 * .5
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 1.5 ).r, 0.75, EPS );
	sp[SAMP_MAXMIPLEVEL] = 1;
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, -1.0 ).r, 0.5, EPS );		// magnified, from level MAXMIPLEVEL
	sp[SAMP_MAXMIPLEVEL] = 0;
	sp[SAMP_MIPFILTER] = TEXF_POINT;
	sp[SAMP_MIPMAPLODBIAS] = floatBits( 1.0f );
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, -0.5 ).r, 0.5, EPS );		// -.5 + 1 = .5: minified, level round(.5) = 1
}

TEST(ffref_lod_from_screen_derivatives)
{
	// An 8x8 texture over an 8x8-pixel square: one texel a pixel, lambda 0.  Over a 4x4 square the
	// same texture is minified 2x: lambda 1, mip level 1.
	Texture t;
	t.type = TEXTURE_2D;
	const double reds[4] = { 0.0, 0.5, 1.0, 1.0 };
	t = solidLevels( reds, 4, 8 );
	DrawState s = screenState( 8, 8 );
	s.texCoordSets = 1;
	s.textures[0] = &t;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.samplerState[0][SAMP_MIPFILTER] = TEXF_POINT;
	struct Quad
	{
		static void draw( const DrawState &s, Target &t, double size, Report *report )
		{
			Vertex v[4];
			const double xy[4][2] = { { 0, 0 }, { size, 0 }, { size, size }, { 0, size } };
			for (int i = 0; i < 4; ++i)
			{
				v[i] = screenVertex( xy[i][0], xy[i][1], 0.5, 1, rgba( 1, 1, 1, 1 ) );
				v[i].tex[0][0] = xy[i][0] / size;
				v[i].tex[0][1] = xy[i][1] / size;
			}
			const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
			CHECK( FFRef::draw( s, PT_TRIANGLELIST, v, 4, idx, 6, t, report ) );
		}
	};
	Target tg = exactTarget( 8, 8 );
	Report r;
	Quad::draw( s, tg, 8, &r );
	CHECK_NEAR( r.minLod, 0.0, 1e-6 );
	CHECK_NEAR( r.maxLod, 0.0, 1e-6 );
	tg.clear( rgba( 0, 0, 0, 0 ) );
	Report r2;
	Quad::draw( s, tg, 4, &r2 );
	CHECK_NEAR( r2.minLod, 1.0, 1e-6 );
	CHECK_NEAR( at( tg, 1, 1 ).r, 0.5, EPS );
}

// ---- rasterisation: "Rasterization Rules", "Directly Mapping Texels to Pixels" ---------------------

TEST(ffref_raster_centres_and_top_left_rule)
{
	// The square [0,2]x[0,2] as two triangles sharing the diagonal, added ONE/ONE at .25: centres
	// (0,0), (1,0), (0,1), (1,1) each exactly once (left and top edges in, right and bottom out, and
	// the diagonal's centres go to one triangle only).
	DrawState s = screenState( 4, 4 );
	s.renderState[RS_ALPHABLENDENABLE] = 1;
	s.renderState[RS_SRCBLEND] = BLEND_ONE;
	s.renderState[RS_DESTBLEND] = BLEND_ONE;
	s.renderState[RS_ZENABLE] = ZB_FALSE;
	const Color q = rgba( 0.25, 0.25, 0.25, 0.25 );
	const Vertex v[6] = { screenVertex( 0, 0, 0.5, 1, q ), screenVertex( 2, 0, 0.5, 1, q ), screenVertex( 2, 2, 0.5, 1, q ),
		screenVertex( 0, 0, 0.5, 1, q ), screenVertex( 2, 2, 0.5, 1, q ), screenVertex( 0, 2, 0.5, 1, q ) };
	Target t = exactTarget( 4, 4 );
	Report r;
	CHECK( draw( s, PT_TRIANGLELIST, v, 6, 0, 6, t, &r ) );
	int wrong = 0;
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
		{
			const double want = (x < 2 && y < 2) ? 0.25 : 0.0;
			if (fabs( at( t, x, y ).r - want ) > EPS)
			{
				printf( "  pixel %d,%d: %.3f, want %.3f\n", x, y, at( t, x, y ).r, want );
				++wrong;
			}
		}
	CHECK_EQ( wrong, 0 );
	CHECK_EQ( r.pixelsWritten, 4L );
	// armed: centres on any edge counted in: column 2 and row 2 join, the diagonal's twice
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 6, 0, 6, t, 0, MUTATE_TOP_LEFT_OFF ) );
	CHECK_NEAR( at( t, 2, 1 ).r, 0.25, EPS );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.5, EPS );
}

TEST(ffref_raster_half_pixel_is_detected)
{
	// [0.25, 2.25]^2: D3D9's centres 1 and 2 are inside, D3D10's .5 and 1.5 (pixels 0 and 1) instead.
	// Split on the diagonal x + y = 2.5, which no centre lies on, so no pixel is an edge freedom.
	DrawState s = screenState( 4, 4 );
	const Color w = rgba( 1, 1, 1, 1 );
	const Vertex v[4] = { screenVertex( 0.25, 0.25, 0.5, 1, w ), screenVertex( 2.25, 0.25, 0.5, 1, w ),
		screenVertex( 2.25, 2.25, 0.5, 1, w ), screenVertex( 0.25, 2.25, 0.5, 1, w ) };
	const uint32_t idx[6] = { 0, 1, 3, 1, 2, 3 };
	Target t = exactTarget( 4, 4 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 4, idx, 6, t ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	CHECK_NEAR( at( t, 2, 2 ).r, 1.0, EPS );
	Target m = exactTarget( 4, 4 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 4, idx, 6, m, 0, MUTATE_RASTER_HALF_PIXEL ) );
	CHECK_NEAR( at( m, 0, 0 ).r, 1.0, EPS );
	CHECK_NEAR( at( m, 2, 2 ).r, 0.0, EPS );
	// and compare() calls it a failure, not a freedom
	t.colorBits = 8;
	const std::vector<uint8_t> gpu = toRGBA8( m );
	const Comparison c = compare( t, &gpu[0], 16 );
	print( c, stdout, "half-pixel raster against D3D9's" );
	CHECK( !c.passed() );
	CHECK_EQ( c.outside, 6L );		// the two L-shapes that differ: 3 + 3 pixels
}

TEST(ffref_perspective_correct_interpolation)
{
	// (0,0) w 1 red 0, (8,0) w 4 red 1, (0,8) w 1 red 0.  At pixel (4,1) the screen weights are
	// .375, .5, .125; divided by w: .375, .125, .125 of .625, so red = .125 / .625 = .2 (linear: .5)
	DrawState s = screenState( 8, 8 );
	const Vertex v[3] = { screenVertex( 0, 0, 0.5, 1, rgba( 0, 0, 0, 1 ) ), screenVertex( 8, 0, 0.5, 0.25, rgba( 1, 0, 0, 1 ) ),
		screenVertex( 0, 8, 0.5, 1, rgba( 0, 0, 0, 1 ) ) };
	Target t = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 4, 1 ).r, 0.2, EPS );
	Target m = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, m, 0, MUTATE_SCREEN_LINEAR ) );	// armed
	CHECK_NEAR( at( m, 4, 1 ).r, 0.5, EPS );
}

TEST(ffref_flat_shading_first_vertex)
{
	DrawState s = screenState( 8, 8 );
	s.renderState[RS_SHADEMODE] = SHADE_FLAT;
	const Vertex v[4] = { screenVertex( 0, 0, 0.5, 1, rgba( 0.1, 0, 0, 1 ) ), screenVertex( 8, 0, 0.5, 1, rgba( 0.9, 0, 0, 1 ) ),
		screenVertex( 0, 8, 0.5, 1, rgba( 0.7, 0, 0, 1 ) ), screenVertex( 8, 8, 0.5, 1, rgba( 0.5, 0, 0, 1 ) ) };
	Target t = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 2, 2 ).r, 0.1, EPS );		// list: vertex 3i
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
	CHECK_NEAR( at( t, 6, 6 ).r, 0.9, EPS );		// strip triangle 1 (v1, v2, v3): vertex 1
	t.clear( rgba( 0, 0, 0, 0 ) );
	const Vertex fan[4] = { v[0], v[1], v[3], v[2] };
	CHECK( draw( s, PT_TRIANGLEFAN, fan, 4, 0, 4, t ) );
	CHECK_NEAR( at( t, 2, 6 ).r, 0.5, EPS );		// fan triangle 1 (0, 2, 3): vertex 2 is fan[2] = v3
	// a strip's odd triangles are wound the other way round and still face the same way (N12): with
	// counterclockwise culling both triangles of this clockwise strip are drawn
	s.renderState[RS_CULLMODE] = CULL_CCW;
	t.clear( rgba( 0, 0, 0, 0 ) );
	Report r;
	CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t, &r ) );
	CHECK_EQ( r.trianglesCulled, 0L );
	CHECK_NEAR( at( t, 6, 6 ).r, 0.9, EPS );
}

TEST(ffref_near_plane_clipping_and_culling)
{
	// clip z = .5 x (w = 1): z >= 0 keeps x >= 0, which is screen columns 4..7 of an 8-wide viewport
	DrawState s;
	s.setDefaults( 8, 8 );
	s.renderState[RS_LIGHTING] = 0;
	s.hasDiffuse = true;
	const Color w = rgba( 1, 1, 1, 1 );
	const Vertex v[3] = { worldVertex( -1, -1, -0.5, w ), worldVertex( 3, -1, 1.5, w ), worldVertex( -1, 3, -0.5, w ) };
	Target t = exactTarget( 8, 8 );
	Report culled;
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, &culled ) );	// counterclockwise on screen: culled by default
	CHECK_EQ( culled.trianglesCulled, 1L );
	CHECK_EQ( culled.pixelsWritten, 0L );
	s.renderState[RS_CULLMODE] = CULL_NONE;
	Report r;
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, &r ) );
	CHECK_EQ( r.pixelsWritten, 32L );
	CHECK_NEAR( at( t, 3, 4 ).r, 0.0, EPS );
	CHECK_NEAR( at( t, 4, 4 ).r, 1.0, EPS );		// on the clip edge x = 0: a left edge, in
	CHECK_NEAR( t.depth[4 * 8 + 6], 0.25, EPS );	// X = 6: x = .5, z = .25
}

// ---- the frame buffer ------------------------------------------------------------------------------

TEST(ffref_specular_add)
{
	DrawState s = screenState( 2, 2 );
	s.hasSpecular = true;
	s.renderState[RS_SPECULARENABLE] = 1;
	Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 0.5, 0.5, 0.5, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 0.5, 0.5, 0.5, 1 ) ),
		screenVertex( -100, 300, 0.5, 1, rgba( 0.5, 0.5, 0.5, 1 ) ) };
	for (int i = 0; i < 3; ++i)
		v[i].specular = rgba( 0.25, 0.75, 0, 0.3 );
	Target t = exactTarget( 2, 2 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.75, EPS );
	CHECK_NEAR( at( t, 0, 0 ).g, 1.0, EPS );		// saturated
	CHECK_NEAR( at( t, 0, 0 ).a, 1.0, EPS );		// RGB only
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, 0, MUTATE_NO_SPECULAR_ADD ) );	// armed
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5, EPS );
}

TEST(ffref_blend_factors_and_ops)
{
	DrawState s;
	s.setDefaults( 1, 1 );
	uint32_t *rs = s.renderState;
	const Color src = rgba( 0.8, 0.6, 0.4, 0.25 ), dst = rgba( 0.2, 0.4, 0.6, 0.5 );
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8, EPS );		// blending off: the source
	rs[RS_ALPHABLENDENABLE] = 1;
	rs[RS_SRCBLEND] = BLEND_SRCALPHA; rs[RS_DESTBLEND] = BLEND_INVSRCALPHA;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8 * 0.25 + 0.2 * 0.75, EPS );
	CHECK_NEAR( blend( s, src, dst, true ).a, 0.25 * 0.25 + 0.5 * 0.75, EPS );
	CHECK_NEAR( blend( s, src, dst, true, MUTATE_BLEND_SWAPPED ).r, 0.8 * 0.75 + 0.2 * 0.25, EPS );	// armed
	rs[RS_SRCBLEND] = BLEND_BOTHSRCALPHA; rs[RS_DESTBLEND] = BLEND_ZERO;		// DESTBLEND overridden
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8 * 0.25 + 0.2 * 0.75, EPS );
	rs[RS_SRCBLEND] = BLEND_SRCALPHASAT; rs[RS_DESTBLEND] = BLEND_ZERO;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8 * 0.25, EPS );			// min(.25, 1 - .5)
	CHECK_NEAR( blend( s, src, dst, true ).a, 0.25, EPS );				// alpha factor 1
	rs[RS_SRCBLEND] = BLEND_DESTCOLOR;
	CHECK_NEAR( blend( s, src, dst, true ).g, 0.6 * 0.4, EPS );
	rs[RS_SRCBLEND] = BLEND_DESTALPHA;
	CHECK_NEAR( blend( s, src, dst, false ).r, 0.8, EPS );		// X8 target: destination alpha 1
	rs[RS_SRCBLEND] = BLEND_BLENDFACTOR; rs[RS_BLENDFACTOR] = 0x00800000;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8 * 128.0 / 255.0, EPS );
	rs[RS_SRCBLEND] = BLEND_ONE; rs[RS_DESTBLEND] = BLEND_ONE;
	rs[RS_BLENDOP] = BLENDOP_SUBTRACT;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.6, EPS );
	rs[RS_BLENDOP] = BLENDOP_REVSUBTRACT;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.0, EPS );		// clamped
	CHECK_NEAR( blend( s, src, dst, true ).b, 0.2, EPS );
	rs[RS_BLENDOP] = BLENDOP_MIN;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.2, EPS );
	rs[RS_BLENDOP] = BLENDOP_MAX;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8, EPS );
	rs[RS_BLENDOP] = BLENDOP_ADD;
	rs[RS_SEPARATEALPHABLENDENABLE] = 1;
	rs[RS_SRCBLENDALPHA] = BLEND_ZERO; rs[RS_DESTBLENDALPHA] = BLEND_ONE;
	CHECK_NEAR( blend( s, src, dst, true ).a, 0.5, EPS );
	CHECK_NEAR( blend( s, src, dst, true ).r, 1.0, EPS );		// .8 + .2
}

TEST(ffref_compare_funcs)
{
	CHECK( !compareFunc( CMP_NEVER, 1, 0 ) );
	CHECK( compareFunc( CMP_LESS, 0.2, 0.3 ) && !compareFunc( CMP_LESS, 0.3, 0.3 ) );
	CHECK( compareFunc( CMP_EQUAL, 0.3, 0.3 ) && !compareFunc( CMP_EQUAL, 0.3, 0.4 ) );
	CHECK( compareFunc( CMP_LESSEQUAL, 0.3, 0.3 ) && !compareFunc( CMP_LESSEQUAL, 0.4, 0.3 ) );
	CHECK( compareFunc( CMP_GREATER, 0.4, 0.3 ) && !compareFunc( CMP_GREATER, 0.3, 0.3 ) );
	CHECK( compareFunc( CMP_NOTEQUAL, 0.4, 0.3 ) && !compareFunc( CMP_NOTEQUAL, 0.3, 0.3 ) );
	CHECK( compareFunc( CMP_GREATEREQUAL, 0.3, 0.3 ) && !compareFunc( CMP_GREATEREQUAL, 0.2, 0.3 ) );
	CHECK( compareFunc( CMP_ALWAYS, 0, 1 ) );
}

TEST(ffref_alpha_test)
{
	DrawState s = screenState( 2, 2 );
	s.renderState[RS_ALPHATESTENABLE] = 1;
	s.renderState[RS_ALPHAFUNC] = CMP_GREATER;
	s.renderState[RS_ALPHAREF] = 0x7780;		// low 8 bits: 128
	Target t = exactTarget( 2, 2 );
	fill( s, t, 0.5, rgba( 1, 1, 1, 0.6 ) );		// 153 > 128
	CHECK_NEAR( at( t, 0, 0 ).r, 1.0, EPS );
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( s, t, 0.5, rgba( 1, 1, 1, 0.4 ) );		// 102: rejected
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	CHECK_EQ( t.zones[0], 0u );
	// at the reference exactly: nominally rejected, but inside the alpha-test freedom
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( s, t, 0.5, rgba( 1, 1, 1, 128.0 / 255.0 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	CHECK( (t.zones[0] & ZONE_ALPHA_TEST) != 0 );
	CHECK_NEAR( t.hi[0].r, 1.0, EPS );
}

TEST(ffref_undecided_alpha_test_writes_the_hull)
{
	// N30.  Two texels, colour falling as alpha rises: (1, 0) and (.2, 1), so t along the pair gives
	// alpha t and colour 1 - .8t.  Sampled at t = .485 (u = .4925, bilinear, clamped): the nominal alpha
	// fails ALPHAREF 126 (.4941) even with the 1.5/255 alpha freedom, while a 1/128-texel shift gives
	// t = .4928, which passes.  A sample between them, t = .489, passes within the alpha freedom too, and
	// writes colour .6088 - above the passing variant's .6058.  The envelope must hold it.
	Texture pair;
	pair.type = TEXTURE_2D;
	TextureLevel l;
	l.width = 2; l.height = 1;
	l.texels.push_back( rgba( 1, 1, 1, 0 ) );
	l.texels.push_back( rgba( 0.2, 0.2, 0.2, 1 ) );
	pair.levels.push_back( l );
	DrawState s = screenState( 1, 1 );
	s.textures[0] = &pair;
	s.texCoordSets = 1;
	s.texCoordSize[0] = 2;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_COLORARG1] = TA_TEXTURE;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAARG1] = TA_TEXTURE;
	s.samplerState[0][SAMP_MAGFILTER] = TEXF_LINEAR;
	s.samplerState[0][SAMP_MINFILTER] = TEXF_LINEAR;
	s.samplerState[0][SAMP_ADDRESSU] = TADDRESS_CLAMP;
	s.samplerState[0][SAMP_ADDRESSV] = TADDRESS_CLAMP;
	s.renderState[RS_ALPHATESTENABLE] = 1;
	s.renderState[RS_ALPHAFUNC] = CMP_GREATEREQUAL;
	s.renderState[RS_ALPHAREF] = 126;
	Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ),
		screenVertex( -100, 300, 0.5, 1, rgba( 1, 1, 1, 1 ) ) };
	for (int i = 0; i < 3; ++i)
	{
		v[i].tex[0][0] = 0.4925; v[i].tex[0][1] = 0.5;
	}
	Target t = exactTarget( 1, 1 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );					// nominally rejected: the clear
	CHECK( (t.zones[0] & ZONE_ALPHA_TEST) != 0 );
	CHECK( t.hi[0].r >= 1.0 - 0.8 * 0.489 );					// the in-between pass
	CHECK( t.hi[0].r <= 1.0 - 0.8 * (0.485 - 1.0 / 128.0) + EPS );	// and no more than the variants reach (the other shift)

	// the control: a decided test (ALPHAREF far below) adds nothing from rejected variants
	s.renderState[RS_ALPHAREF] = 10;
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_EQ( t.zones[0] & ZONE_ALPHA_TEST, 0u );
	CHECK_NEAR( at( t, 0, 0 ).r, 1.0 - 0.8 * 0.485, 1e-6 );
}

TEST(ffref_depth_and_stencil)
{
	DrawState s = screenState( 2, 2 );
	Target t = exactTarget( 2, 2 );
	fill( s, t, 0.5, rgba( 0.5, 0, 0, 1 ) );
	CHECK_NEAR( t.depth[0], 0.5, EPS );
	s.renderState[RS_ZFUNC] = CMP_LESS;
	fill( s, t, 0.7, rgba( 0.9, 0, 0, 1 ) );		// behind: rejected
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5, EPS );
	fill( s, t, 0.3, rgba( 0.1, 0, 0, 1 ) );		// in front
	CHECK_NEAR( at( t, 0, 0 ).r, 0.1, EPS );
	s.renderState[RS_ZWRITEENABLE] = 0;
	fill( s, t, 0.2, rgba( 0.2, 0, 0, 1 ) );
	CHECK_NEAR( t.depth[0], 0.3, EPS );			// tested, not written
	s.renderState[RS_ZWRITEENABLE] = 1;
	// depth bias: .9 + (-.8) = .1 passes against .3
	s.renderState[RS_DEPTHBIAS] = floatBits( -0.8f );
	fill( s, t, 0.9, rgba( 0.3, 0, 0, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.3, EPS );
	CHECK_NEAR( t.depth[0], 0.9 + (double)-0.8f, 1e-7 );
	s.renderState[RS_DEPTHBIAS] = 0;

	// stencil: REPLACE 3, then EQUAL 3 passes and EQUAL 2 does not
	s.renderState[RS_ZENABLE] = ZB_FALSE;
	s.renderState[RS_STENCILENABLE] = 1;
	s.renderState[RS_STENCILPASS] = STENCILOP_REPLACE;
	s.renderState[RS_STENCILREF] = 3;
	fill( s, t, 0.5, rgba( 0.4, 0, 0, 1 ) );
	CHECK_EQ( t.stencil[0], 3u );
	s.renderState[RS_STENCILPASS] = STENCILOP_KEEP;
	s.renderState[RS_STENCILFUNC] = CMP_EQUAL;
	s.renderState[RS_STENCILREF] = 2;
	s.renderState[RS_STENCILFAIL] = STENCILOP_INVERT;
	fill( s, t, 0.5, rgba( 0.6, 0, 0, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.4, EPS );
	CHECK_EQ( t.stencil[0], 252u );		// ~3 in 8 bits
	struct Op { int op; uint32_t from, to; };
	const Op ops[] = { { STENCILOP_INCRSAT, 255, 255 }, { STENCILOP_INCR, 255, 0 }, { STENCILOP_DECRSAT, 0, 0 },
		{ STENCILOP_DECR, 0, 255 }, { STENCILOP_ZERO, 9, 0 }, { STENCILOP_INVERT, 0x0F, 0xF0 } };
	s.renderState[RS_STENCILFUNC] = CMP_ALWAYS;
	for (size_t i = 0; i < sizeof( ops ) / sizeof( ops[0] ); ++i)
	{
		t.clear( rgba( 0, 0, 0, 0 ), 1.0, ops[i].from );
		s.renderState[RS_STENCILPASS] = ops[i].op;
		fill( s, t, 0.5, rgba( 1, 0, 0, 1 ) );
		CHECK_EQ( t.stencil[0], ops[i].to );
	}
	// write mask: only the low nibble changes
	t.clear( rgba( 0, 0, 0, 0 ), 1.0, 0xA5 );
	s.renderState[RS_STENCILPASS] = STENCILOP_ZERO;
	s.renderState[RS_STENCILWRITEMASK] = 0x0F;
	fill( s, t, 0.5, rgba( 1, 0, 0, 1 ) );
	CHECK_EQ( t.stencil[0], 0xA0u );
	// two-sided: a counterclockwise triangle takes the CCW operations
	s.renderState[RS_STENCILWRITEMASK] = 0xFFFFFFFF;
	s.renderState[RS_TWOSIDEDSTENCILMODE] = 1;
	s.renderState[RS_STENCILPASS] = STENCILOP_INCR;
	s.renderState[RS_CCW_STENCILFUNC] = CMP_ALWAYS;
	s.renderState[RS_CCW_STENCILPASS] = STENCILOP_DECR;
	t.clear( rgba( 0, 0, 0, 0 ), 1.0, 5 );
	const Color c = rgba( 1, 0, 0, 1 );
	const Vertex ccw[3] = { screenVertex( -100, -100, 0.5, 1, c ), screenVertex( -100, 300, 0.5, 1, c ), screenVertex( 300, -100, 0.5, 1, c ) };
	CHECK( draw( s, PT_TRIANGLELIST, ccw, 3, 0, 3, t ) );
	CHECK_EQ( t.stencil[0], 4u );
	fill( s, t, 0.5, c );		// clockwise
	CHECK_EQ( t.stencil[0], 5u );
}

TEST(ffref_write_mask_scissor_viewport)
{
	DrawState s = screenState( 4, 4 );
	Target t = exactTarget( 4, 4 );
	s.renderState[RS_COLORWRITEENABLE] = 0x2 | 0x8;	// green, alpha
	fill( s, t, 0.5, rgba( 1, 1, 1, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	CHECK_NEAR( at( t, 0, 0 ).g, 1.0, EPS );
	s.renderState[RS_COLORWRITEENABLE] = 0xF;
	t.clear( rgba( 0, 0, 0, 0 ) );
	s.renderState[RS_SCISSORTESTENABLE] = 1;
	s.scissor.left = 1; s.scissor.top = 1; s.scissor.right = 3; s.scissor.bottom = 2;
	Report r;
	fill( s, t, 0.5, rgba( 1, 1, 1, 1 ), &r );
	CHECK_EQ( r.pixelsWritten, 2L );
	CHECK_NEAR( at( t, 1, 1 ).r, 1.0, EPS );
	s.renderState[RS_SCISSORTESTENABLE] = 0;
	t.clear( rgba( 0, 0, 0, 0 ) );
	s.viewport.x = 2; s.viewport.width = 2;
	Report rv;
	fill( s, t, 0.5, rgba( 1, 1, 1, 1 ), &rv );
	CHECK_EQ( rv.pixelsWritten, 8L );
	CHECK_NEAR( at( t, 1, 0 ).r, 0.0, EPS );
}

// ---- texture coordinates: D3DTSS_TCI, "Texture Coordinate Transformations" -------------------------

TEST(ffref_texcoord_generation_transform_and_projection)
{
	// camera-space position, scaled by .5 and moved by .5 + 1/16: u = X/8 + 1/16, so pixel X reads
	// texel X of an 8x1 ramp, well clear of the texel edges
	Texture ramp;
	ramp.type = TEXTURE_2D;
	TextureLevel l;
	l.width = 8; l.height = 1;
	for (int i = 0; i < 8; ++i)
		l.texels.push_back( rgba( i / 8.0, 0, 0, 1 ) );
	ramp.levels.push_back( l );
	DrawState s;
	s.setDefaults( 8, 8 );
	s.renderState[RS_CULLMODE] = CULL_NONE;
	s.renderState[RS_LIGHTING] = 0;
	s.textures[0] = &ramp;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[0][TSS_TEXCOORDINDEX] = TSS_TCI_CAMERASPACEPOSITION;
	s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT2;
	s.textureTransform[0].m[0][0] = 0.5;
	s.textureTransform[0].m[3][0] = 0.5 + 1.0 / 16.0;
	const Color w = rgba( 1, 1, 1, 1 );
	const Vertex v[3] = { worldVertex( -10, -10, 0.5, w ), worldVertex( -10, 30, 0.5, w ), worldVertex( 30, -10, 0.5, w ) };
	Target t = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );		// x = X/4 - 1, u = x/2 + .5 + 1/16
	CHECK_NEAR( at( t, 5, 3 ).r, 5.0 / 8.0, EPS );
	CHECK_NEAR( at( t, 2, 3 ).r, 2.0 / 8.0, EPS );

	// projected: (s, t, q) = (.6, .2, 2) passed through, COUNT3 | PROJECTED: u = .3 -> texel 2
	s.texCoordSets = 1;
	s.texCoordSize[0] = 3;
	s.stageState[0][TSS_TEXCOORDINDEX] = 0;
	s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT3 | TTFF_PROJECTED;
	s.textureTransform[0] = Matrix::identity();
	Vertex p[3] = { v[0], v[1], v[2] };
	for (int i = 0; i < 3; ++i)
	{
		p[i].tex[0][0] = 0.6; p[i].tex[0][1] = 0.2; p[i].tex[0][2] = 2.0;
	}
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, p, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 5, 3 ).r, 2.0 / 8.0, EPS );
	// without PROJECTED the first element is u = .6 -> texel 4
	s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT3;
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, p, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 5, 3 ).r, 4.0 / 8.0, EPS );
}

TEST(ffref_missing_coordinate_set_reads_zero)
{
	// N28: stage 0 names set 1 of vertices with one set; D3DTSS_TEXCOORDINDEX says u, v = (0, 0).  An 8x1
	// ramp, point-sampled and clamped: set 0 (u = .6) reads texel 4, the missing set 1 reads texel 0.
	Texture ramp;
	ramp.type = TEXTURE_2D;
	TextureLevel l;
	l.width = 8; l.height = 1;
	for (int i = 0; i < 8; ++i)
		l.texels.push_back( rgba( (i + 1) / 8.0, 0, 0, 1 ) );
	ramp.levels.push_back( l );
	for (int pretransformed = 0; pretransformed < 2; ++pretransformed)
	{
		DrawState s;
		s.setDefaults( 8, 8 );
		s.renderState[RS_CULLMODE] = CULL_NONE;
		s.renderState[RS_LIGHTING] = 0;
		s.pretransformed = pretransformed != 0;
		s.hasDiffuse = true;
		s.textures[0] = &ramp;
		s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
		s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
		s.samplerState[0][SAMP_MAGFILTER] = TEXF_POINT;
		s.samplerState[0][SAMP_MINFILTER] = TEXF_POINT;
		s.samplerState[0][SAMP_ADDRESSU] = TADDRESS_CLAMP;
		s.samplerState[0][SAMP_ADDRESSV] = TADDRESS_CLAMP;
		s.texCoordSets = 1;
		s.texCoordSize[0] = 2;
		const Color w = rgba( 1, 1, 1, 1 );
		Vertex v[3] = { pretransformed ? screenVertex( -100, -100, 0.5, 1, w ) : worldVertex( -10, -10, 0.5, w ),
			pretransformed ? screenVertex( 300, -100, 0.5, 1, w ) : worldVertex( -10, 30, 0.5, w ),
			pretransformed ? screenVertex( -100, 300, 0.5, 1, w ) : worldVertex( 30, -10, 0.5, w ) };
		for (int i = 0; i < 3; ++i)
		{
			v[i].tex[0][0] = 0.6; v[i].tex[0][1] = 0.5;
		}
		Target t = exactTarget( 8, 8 );
		Report r;
		s.stageState[0][TSS_TEXCOORDINDEX] = 0;		// the control: the set the vertices have
		CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, &r ) );
		CHECK_NEAR( at( t, 4, 4 ).r, 5.0 / 8.0, EPS );
		t.clear( rgba( 0, 0, 0, 0 ) );
		s.stageState[0][TSS_TEXCOORDINDEX] = 1;		// a set they lack: drawn, at (0, 0)
		Report missing;
		CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, &missing ) );
		CHECK( missing.refusals.empty() );
		CHECK_NEAR( at( t, 4, 4 ).r, 1.0 / 8.0, EPS );
		if (!pretransformed)
		{
			// padded as a 2-component set, (0, 0, 1, 0): a translation in _31 (the row the padded 1
			// meets) moves it to u = .6, texel 4.  Unpadded zeros would stay at texel 0.
			s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT2;
			s.textureTransform[0].m[2][0] = 0.6;
			t.clear( rgba( 0, 0, 0, 0 ) );
			CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
			CHECK_NEAR( at( t, 4, 4 ).r, 5.0 / 8.0, EPS );
		}
	}
}

TEST(ffref_undefined_alpha_op_is_drawn_with_an_envelope)
{
	// N29: ALPHAOP DISABLE under an enabled COLOROP.  The alpha written is CURRENT's (the diffuse's .25),
	// and the envelope runs from 0 to 1, marked ZONE_UNDEFINED; the colour, which does not read it, is
	// exact.  ALPHAARG1 is left at its default, D3DTA_TEXTURE, with no texture: a DISABLE reads nothing.
	DrawState s = screenState( 2, 2 );
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE;
	s.stageState[0][TSS_ALPHAOP] = TOP_DISABLE;
	Target t = exactTarget( 2, 2 );
	Report r;
	fill( s, t, 0.5, rgba( 0.5, 0.5, 0.5, 0.25 ), &r );
	CHECK( r.refusals.empty() );
	CHECK_NEAR( at( t, 0, 0 ).a, 0.25, EPS );
	CHECK_NEAR( t.lo[0].a, 0.0, EPS );
	CHECK_NEAR( t.hi[0].a, 1.0, EPS );
	CHECK_NEAR( t.lo[0].r, 0.5, EPS );
	CHECK_NEAR( t.hi[0].r, 0.5, EPS );
	CHECK( (t.zones[0] & ZONE_UNDEFINED) != 0 );

	// blended by that alpha onto black, the colour itself is anywhere from 0 to .5
	s.renderState[RS_ALPHABLENDENABLE] = 1;
	s.renderState[RS_SRCBLEND] = BLEND_SRCALPHA;
	s.renderState[RS_DESTBLEND] = BLEND_INVSRCALPHA;
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( s, t, 0.5, rgba( 0.5, 0.5, 0.5, 0.25 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.125, EPS );
	CHECK_NEAR( t.lo[0].r, 0.0, EPS );
	CHECK_NEAR( t.hi[0].r, 0.5, EPS );

	// the control: a defined ALPHAOP has no such envelope
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAARG1] = TA_DIFFUSE;
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( s, t, 0.5, rgba( 0.5, 0.5, 0.5, 0.25 ) );
	CHECK_NEAR( t.lo[0].r, 0.125, EPS );
	CHECK_NEAR( t.hi[0].r, 0.125, EPS );
	CHECK_EQ( t.zones[0] & ZONE_UNDEFINED, 0u );
}

// ---- refusals, and compare()'s classification ------------------------------------------------------

TEST(ffref_refusals_draw_nothing)
{
	struct Case { const char *what; void (*set)( DrawState &, Texture & ); };
	struct Sets
	{
		static void shader( DrawState &s, Texture & ) { s.vertexShaderBound = true; }
		static void spheremap( DrawState &s, Texture &t ) { s.textures[0] = &t; s.stageState[0][TSS_TEXCOORDINDEX] = TSS_TCI_SPHEREMAP; }
		static void aniso( DrawState &s, Texture &t ) { s.textures[0] = &t; s.samplerState[0][SAMP_MINFILTER] = TEXF_ANISOTROPIC; s.samplerState[0][SAMP_MAXANISOTROPY] = 4; }
		static void cube( DrawState &s, Texture &t ) { t.type = TEXTURE_CUBE; s.textures[0] = &t; }
		static void clipPlane( DrawState &s, Texture & ) { s.renderState[RS_CLIPPLANEENABLE] = 1; }
		static void wrap( DrawState &s, Texture & ) { s.renderState[RS_WRAP0] = 1; }
		static void colourOnlyAlpha( DrawState &s, Texture & ) { s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE; s.stageState[0][TSS_ALPHAOP] = TOP_MODULATEALPHA_ADDCOLOR; }
		static void textureless( DrawState &s, Texture & ) { s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE; s.stageState[0][TSS_COLORARG2] = TA_TEXTURE; }
	};
	const Case cases[] = { { "shader", Sets::shader }, { "spheremap", Sets::spheremap },
		{ "anisotropic 4", Sets::aniso }, { "cube", Sets::cube }, { "clip plane", Sets::clipPlane }, { "wrap", Sets::wrap },
		{ "colour-only alpha op", Sets::colourOnlyAlpha }, { "texture read without texture", Sets::textureless } };
	for (size_t i = 0; i < sizeof( cases ) / sizeof( cases[0] ); ++i)
	{
		DrawState s = screenState( 2, 2 );
		s.texCoordSets = 1;
		Texture tex = ramp4();
		cases[i].set( s, tex );
		Target t = exactTarget( 2, 2 );
		const Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ),
			screenVertex( -100, 300, 0.5, 1, rgba( 1, 1, 1, 1 ) ) };
		Report r;
		const bool drew = draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, &r );
		if (drew || r.refusals.empty())
			printf( "  %s: not refused\n", cases[i].what );
		CHECK( !drew );
		CHECK( !r.refusals.empty() );
		CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	}
	// and the plainest draw is not refused
	DrawState s = screenState( 2, 2 );
	Target t = exactTarget( 2, 2 );
	Report r;
	fill( s, t, 0.5, rgba( 1, 1, 1, 1 ), &r );
	CHECK( r.refusals.empty() );
}

TEST(ffref_compare_classifies_freedoms)
{
	// A triangle whose hypotenuse runs through pixel centres: those are ZONE_EDGE; a GPU that drew or
	// skipped them is inside the freedom, one that changed an interior pixel is not.
	DrawState s = screenState( 8, 8 );
	const Color w = rgba( 1, 1, 1, 1 );
	const Vertex v[3] = { screenVertex( 0, 0, 0.5, 1, w ), screenVertex( 8, 0, 0.5, 1, w ), screenVertex( 0, 8, 0.5, 1, w ) };
	Target ref;
	ref.create( 8, 8 );
	ref.clear( rgba( 0, 0, 0, 1 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, ref ) );
	std::vector<uint8_t> gpu = toRGBA8( ref );
	Comparison same = compare( ref, &gpu[0], 32 );
	CHECK_EQ( same.outside, 0L );
	CHECK_EQ( same.exact, 64L );
	// flip every hypotenuse centre (x + y = 8 is outside by the rule; x + y = 7 is not on it)
	int flipped = 0;
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			if (x + y == 8)
			{
				uint8_t *p = &gpu[((size_t)y * 8 + x) * 4];
				p[0] = p[1] = p[2] = 255;
				++flipped;
			}
	// and skip a top-edge centre the rule draws
	uint8_t *top = &gpu[((size_t)0 * 8 + 3) * 4];
	top[0] = top[1] = top[2] = 0;
	++flipped;
	Comparison edge = compare( ref, &gpu[0], 32 );
	print( edge, stdout, "hypotenuse centres drawn, a top-edge centre skipped" );
	CHECK_EQ( edge.outside, 0L );
	CHECK_EQ( edge.inFreedom, (long)flipped );
	CHECK_EQ( edge.zoneCounts[0], (long)flipped );
	int outsideOwnEnvelope = 0;
	for (size_t i = 0; i < ref.color.size(); ++i)
		if (ref.color[i].r < ref.lo[i].r || ref.color[i].r > ref.hi[i].r)
			++outsideOwnEnvelope;
	CHECK_EQ( outsideOwnEnvelope, 0 );
	// an interior pixel off by 10 levels is outside
	gpu[((size_t)2 * 8 + 2) * 4] = 245;
	Comparison bad = compare( ref, &gpu[0], 32 );
	CHECK_EQ( bad.outside, 1L );
	CHECK_EQ( bad.worstX, 2 );
	CHECK_EQ( bad.worstY, 2 );
	// base tolerance: off by 2 levels is exact, by 3 is not
	gpu[((size_t)2 * 8 + 2) * 4] = 253;
	CHECK_EQ( compare( ref, &gpu[0], 32 ).outside, 0L );
	gpu[((size_t)2 * 8 + 2) * 4] = 252;
	CHECK_EQ( compare( ref, &gpu[0], 32 ).outside, 1L );
}

TEST(ffref_bilinear_and_lod_freedoms_widen_the_envelope)
{
	// Bilinear between a black and a white texel: a weight-precision shift of 1/128 texel moves the
	// colour by 1/128, so the envelope spans about +-2 levels around the nominal; ZONE_TEXEL marks it.
	Texture bw;
	bw.type = TEXTURE_2D;
	TextureLevel l;
	l.width = 2; l.height = 1;
	l.texels.push_back( rgba( 0, 0, 0, 1 ) );
	l.texels.push_back( rgba( 1, 1, 1, 1 ) );
	bw.levels.push_back( l );
	DrawState s = screenState( 1, 1 );
	s.texCoordSets = 1;
	s.textures[0] = &bw;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.samplerState[0][SAMP_MAGFILTER] = s.samplerState[0][SAMP_MINFILTER] = TEXF_LINEAR;
	s.samplerState[0][SAMP_ADDRESSU] = TADDRESS_CLAMP;
	Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ),
		screenVertex( -100, 300, 0.5, 1, rgba( 1, 1, 1, 1 ) ) };
	for (int i = 0; i < 3; ++i)
	{
		v[i].tex[0][0] = 0.5; v[i].tex[0][1] = 0.5;		// halfway between the centres .25 and .75
	}
	Target t;
	t.create( 1, 1 );
	t.colorBits = 0;
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( t.color[0].r, 0.5, EPS );
	CHECK( (t.zones[0] & ZONE_TEXEL) != 0 );
	CHECK_NEAR( t.lo[0].r, 0.5 - 1.0 / 128.0, 1e-9 );
	CHECK_NEAR( t.hi[0].r, 0.5 + 1.0 / 128.0, 1e-9 );
}

TEST(ffref_lod_freedom_reaches_every_level_inside_its_window)
{
	// Trilinear at lambda 1.5 over levels whose red is 0, 1, 0, 0, 0: nominal .5.  The +-0.6 window
	// [0.9, 2.1] has red .9 and 0 at its ends, but passes through level 1 itself at lambda 1, so the
	// envelope must reach 1.0 (F11: endpoints alone were not a superset of the narrower window).
	const double reds[5] = { 0.0, 1.0, 0.0, 0.0, 0.0 };
	Texture t = solidLevels( reds, 5, 16 );
	DrawState s = screenState( 8, 8 );
	s.texCoordSets = 1;
	s.textures[0] = &t;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.samplerState[0][SAMP_MAGFILTER] = s.samplerState[0][SAMP_MINFILTER] = TEXF_LINEAR;
	s.samplerState[0][SAMP_MIPFILTER] = TEXF_LINEAR;
	const double size = 16.0 / pow( 2.0, 1.5 );		// 2^1.5 texels a pixel: lambda 1.5 everywhere
	Vertex v[4];
	const double xy[4][2] = { { 0, 0 }, { size, 0 }, { size, size }, { 0, size } };
	for (int i = 0; i < 4; ++i)
	{
		v[i] = screenVertex( xy[i][0], xy[i][1], 0.5, 1, rgba( 1, 1, 1, 1 ) );
		v[i].tex[0][0] = xy[i][0] / size;
		v[i].tex[0][1] = xy[i][1] / size;
	}
	const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	Target tg = exactTarget( 8, 8 );
	Report r;
	CHECK( draw( s, PT_TRIANGLELIST, v, 4, idx, 6, tg, &r ) );
	const size_t i = 2 * 8 + 3;		// pixel (3, 2): inside, off the diagonal
	CHECK_NEAR( r.minLod, 1.5, 1e-6 );
	CHECK_NEAR( tg.color[i].r, 0.5, 1e-9 );
	CHECK_NEAR( tg.hi[i].r, 1.0, 1e-9 );
	CHECK_NEAR( tg.lo[i].r, 0.0, 1e-9 );
	CHECK( (tg.zones[i] & ZONE_LOD) != 0 );
	// armed: at the old +-0.2 the window [1.3, 1.7] holds no integer, and the envelope is its ends
	Freedoms narrow;
	narrow.lodDelta = 0.2;
	Target tn = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 4, idx, 6, tn, 0, 0, narrow ) );
	CHECK_NEAR( tn.hi[i].r, 0.7, 1e-9 );		// lambda 1.3: .7 of level 1
	CHECK_NEAR( tn.lo[i].r, 0.3, 1e-9 );
}

TEST(ffref_pixel_detail_names_the_last_writer)
{
	// Two overlapping full-target triangles, blended: every pixel is written twice, the second last
	DrawState s = screenState( 2, 2 );
	s.renderState[RS_ALPHABLENDENABLE] = 1;
	s.renderState[RS_SRCBLEND] = BLEND_SRCALPHA;
	s.renderState[RS_DESTBLEND] = BLEND_INVSRCALPHA;
	Target t = exactTarget( 2, 2 );
	t.recordDetail = true;
	const Color a = rgba( 1, 0, 0, 0.5 ), b = rgba( 0, 1, 0, 0.25 );
	const Vertex v[6] = { screenVertex( -100, -100, 0.5, 1, a ), screenVertex( 300, -100, 0.5, 1, a ), screenVertex( -100, 300, 0.5, 1, a ),
		screenVertex( -100, -100, 0.5, 1, b ), screenVertex( 300, -100, 0.5, 1, b ), screenVertex( -100, 300, 0.5, 1, b ) };
	CHECK( draw( s, PT_TRIANGLELIST, v, 6, 0, 6, t ) );
	CHECK_EQ( t.detail.size(), (size_t)4 );
	CHECK_EQ( t.detail[3].primitive, 1 );
	CHECK_EQ( t.detail[3].layers, 2 );
	CHECK( t.detail[3].writers[0] == 0 && t.detail[3].writers[1] == 1 );
	CHECK_NEAR( t.detail[3].source.a, 0.25, EPS );
	CHECK( t.detail[3].alphaPassed );
	CHECK_NEAR( t.detail[3].screen[1][0], 300.0, EPS );
	t.clear( rgba( 0, 0, 0, 0 ) );		// cleared with the pixels
	CHECK( t.detail.empty() );
	// the footprint: a 4x4 texture across the 2x2 target is 2 level-0 texels per pixel on each axis
	Texture tex = ramp4();
	DrawState ts = screenState( 2, 2 );
	ts.textures[0] = &tex;
	ts.texCoordSets = 1;
	ts.texCoordSize[0] = 2;
	Vertex tv[3] = { screenVertex( 0, 0, 0.5, 1, rgba( 1, 1, 1, 1 ) ), screenVertex( 4, 0, 0.5, 1, rgba( 1, 1, 1, 1 ) ),
		screenVertex( 0, 4, 0.5, 1, rgba( 1, 1, 1, 1 ) ) };
	tv[1].tex[0][0] = 2.0;		// u 0..2 over 4 pixels: .5 per pixel, 2 texels of a 4-wide level 0
	tv[2].tex[0][1] = 2.0;
	Target tt = exactTarget( 2, 2 );
	tt.recordDetail = true;
	CHECK( draw( ts, PT_TRIANGLELIST, tv, 3, 0, 3, tt ) );
	CHECK_NEAR( tt.detail[0].axes[0][0], 2.0, EPS );
	CHECK_NEAR( tt.detail[0].axes[0][1], 2.0, EPS );
	// and nothing is recorded unless asked
	Target quiet = exactTarget( 2, 2 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 6, 0, 6, quiet ) );
	CHECK( quiet.detail.empty() );
}
