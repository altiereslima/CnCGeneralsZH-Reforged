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
 * FFReference: see ffreference.h - its conventions, refusals and NAMED CHOICES (N1-N30) are the key to
 * every function here.  Each function names the learn.microsoft.com page (under
 * /windows/win32/direct3d9/) it follows.  Double precision throughout.
 */

#include "ffreference.h"

#include <math.h>
#include <string.h>

#include <algorithm>

namespace FFRef {

namespace {

const int SET_LIMIT = MAX_STAGES;

double clamp01( double x ) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
double stateFloat( uint32_t bits ) { float f; memcpy( &f, &bits, 4 ); return (double)f; }

double channel( const Color &c, int i ) { return i == 0 ? c.r : i == 1 ? c.g : i == 2 ? c.b : c.a; }
void setChannel( Color &c, int i, double v ) { (i == 0 ? c.r : i == 1 ? c.g : i == 2 ? c.b : c.a) = v; }
Color saturate( Color c ) { c.r = clamp01( c.r ); c.g = clamp01( c.g ); c.b = clamp01( c.b ); c.a = clamp01( c.a ); return c; }
Color mulColor( const Color &a, const Color &b ) { Color c = { a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a }; return c; }
Color lerpColor( const Color &a, const Color &b, double t )
{
	Color c = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
	return c;
}

double dot3( const double a[3], const double b[3] ) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void normalise3( double v[3] )
{
	const double l = sqrt( dot3( v, v ) );
	if (l > 0.0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

/// v' = v * M, v a row vector of four
void transform4( const double v[4], const Matrix &m, double out[4] )
{
	for (int c = 0; c < 4; ++c)
		out[c] = v[0] * m.m[0][c] + v[1] * m.m[1][c] + v[2] * m.m[2][c] + v[3] * m.m[3][c];
}

Matrix multiply( const Matrix &a, const Matrix &b )
{
	Matrix r;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
	return r;
}

/// The inverse-transpose of M's upper 3x3, for normals ("Camera-Space Transformations")
void normalMatrix( const Matrix &m, double out[3][3] )
{
	const double (*a)[4] = m.m;
	const double c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1];
	const double c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2];
	const double c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
	const double det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
	const double inv = det != 0.0 ? 1.0 / det : 0.0;
	// cofactor matrix / det is the inverse-transpose
	out[0][0] = c00 * inv; out[0][1] = c01 * inv; out[0][2] = c02 * inv;
	out[1][0] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * inv;
	out[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * inv;
	out[1][2] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * inv;
	out[2][0] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * inv;
	out[2][1] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * inv;
	out[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * inv;
}

/// n' = n * (M^-1)^T, a row vector: it[][] holds the cofactors C[i][j] / det, which is (M^-1)^T
void transformNormal( const double n[3], const double it[3][3], double out[3] )
{
	for (int j = 0; j < 3; ++j)
		out[j] = n[0] * it[0][j] + n[1] * it[1][j] + n[2] * it[2][j];
}

void transformDirection( const double d[3], const Matrix &m, double out[3] )
{
	for (int c = 0; c < 3; ++c)
		out[c] = d[0] * m.m[0][c] + d[1] * m.m[1][c] + d[2] * m.m[2][c];
}

// ---- after vertex processing -----------------------------------------------------------------------
struct VOut
{
	double clip[4];		///< clip space; pretransformed: X, Y, Z, RHW
	Color diffuse, specular;
	double fog;
	double tex[MAX_STAGES][4];	///< per stage, after TCI and the texture transform (before any divide)
	bool programReported;		///< a vertex program ran through ffprogram.h's P1 or P2
};

VOut lerpVOut( const VOut &a, const VOut &b, double t )
{
	VOut o;
	for (int i = 0; i < 4; ++i)
		o.clip[i] = a.clip[i] + (b.clip[i] - a.clip[i]) * t;
	o.diffuse = lerpColor( a.diffuse, b.diffuse, t );
	o.specular = lerpColor( a.specular, b.specular, t );
	o.fog = a.fog + (b.fog - a.fog) * t;
	for (int s = 0; s < MAX_STAGES; ++s)
		for (int i = 0; i < 4; ++i)
			o.tex[s][i] = a.tex[s][i] + (b.tex[s][i] - a.tex[s][i]) * t;
	return o;
}

/// N28: the u and v of a coordinate set the vertices lack
const double MISSING_SET_UV[2] = { 0.0, 0.0 };

/// A coordinate set, padded as N13 says
void padCoordinates( const double *in, int size, double out[4] )
{
	static const double pad[4][4] = { { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }, { 0, 0, 0, 0 } };
	const int n = size < 1 ? 1 : (size > 4 ? 4 : size);
	for (int i = 0; i < 4; ++i)
		out[i] = i < n ? in[i] : pad[n - 1][i];
}

struct Context
{
	const DrawState &state;
	const uint32_t *rs;
	unsigned mutations;
	const Freedoms &freedoms;
	Matrix worldView;
	double normalIT[3][3];
	Context( const DrawState &s, unsigned m, const Freedoms &f )
		: state( s ), rs( s.renderState ), mutations( m ), freedoms( f )
	{
		worldView = multiply( s.world, s.view );
		normalMatrix( worldView, normalIT );
	}
};

/// Vertex processing: "Fixed Function Vertex Processing", "Camera-Space Transformations",
/// "Vertex Fog", D3DTSS_TCI, "Texture Coordinate Transformations"
VOut processVertex( const Context &ctx, const Vertex &v )
{
	const DrawState &s = ctx.state;
	const uint32_t *rs = ctx.rs;
	const Color white = { 1, 1, 1, 1 };
	const Color none = { 0, 0, 0, 0 };
	const Color vDiffuse = s.hasDiffuse ? v.diffuse : white;		// N7: an absent diffuse is 0xFFFFFFFF,
	const Color vSpecular = s.hasSpecular ? v.specular : none;		// an absent specular 0x00000000 (measured)
	VOut o;
	memset( &o, 0, sizeof( o ) );

	if (s.vertexProgram != NULL)
	{
		// A3e: the declaration's elements into v0-v15, (0, 0, 0, 1) where a stream stops short ("Input
		// Register - vs"); the program's outputs out (oPos as the clip position, oTn for stage n)
		double inputs[ 16 ][ 4 ];
		for (int k = 0; k < 16 && s.programInputsGiven; ++k)
			memcpy( inputs[k], v.programInput[k], sizeof( inputs[k] ) );
		for (int k = 0; k < 16 && !s.programInputsGiven; ++k)
		{
			static const double partial[ 4 ] = { 0, 0, 0, 1 };
			double element[ 4 ] = { 0, 0, 0, 1 };
			const int what = s.vertexInput[k];
			if (what == INPUT_POSITION)
				memcpy( element, v.position, sizeof( element ) );
			else if (what == INPUT_NORMAL)
				memcpy( element, v.normal, 3 * sizeof( double ) );
			else if (what == INPUT_DIFFUSE || what == INPUT_SPECULAR)
			{
				const Color &c = what == INPUT_DIFFUSE ? v.diffuse : v.specular;		// D3DCOLOR as (R, G, B, A)
				element[0] = c.r; element[1] = c.g; element[2] = c.b; element[3] = c.a;
			}
			else if (what >= INPUT_TEXCOORD0 && what < INPUT_TEXCOORD0 + MAX_STAGES)
				memcpy( element, v.tex[what - INPUT_TEXCOORD0], sizeof( element ) );
			for (int i = 0; i < 4; ++i)
				inputs[k][i] = what != INPUT_NONE && i < s.vertexInputSize[k] ? element[i] : partial[i];
		}
		VertexRun run;
		runVertexProgram( *s.vertexProgram, s.vertexConstants, 96, inputs, run );
		for (int i = 0; i < 4; ++i)
			o.clip[i] = run.position[i];
		const Color d = { run.colour[0][0], run.colour[0][1], run.colour[0][2], run.colour[0][3] };
		const Color sp = { run.colour[1][0], run.colour[1][1], run.colour[1][2], run.colour[1][3] };
		o.diffuse = d;
		o.specular = sp;
		o.fog = 1.0;
		for (int st = 0; st < MAX_STAGES; ++st)
			memcpy( o.tex[st], run.texture[st], sizeof( o.tex[st] ) );
		o.programReported = run.addressAmbiguous || run.reciprocalOfZero;
		return o;
	}

	if (s.pretransformed)
	{
		for (int i = 0; i < 4; ++i)
			o.clip[i] = v.position[i];
		o.diffuse = vDiffuse;
		o.specular = vSpecular;
		o.fog = vSpecular.a;		// N8: the application's fog, in specular alpha
		for (int st = 0; st < MAX_STAGES; ++st)
		{
			const int set = (int)(s.stageState[st][TSS_TEXCOORDINDEX] & 0xFFFF);
			if (set < s.texCoordSets)
				padCoordinates( v.tex[set], s.texCoordSize[set], o.tex[st] );
			else
				padCoordinates( MISSING_SET_UV, 2, o.tex[st] );		// N28
		}
		return o;
	}

	const double p[4] = { v.position[0], v.position[1], v.position[2], 1.0 };
	double cam4[4];
	transform4( p, ctx.worldView, cam4 );
	transform4( cam4, s.projection, o.clip );
	const double cam[3] = { cam4[0], cam4[1], cam4[2] };

	double normal[3] = { 0, 0, 0 };
	if (s.hasNormal)
	{
		transformNormal( v.normal, ctx.normalIT, normal );
		if (rs[RS_NORMALIZENORMALS])
			normalise3( normal );
	}

	if (rs[RS_LIGHTING])
	{
		const LitVertex lit = light( s, cam, normal, s.hasNormal, vDiffuse, vSpecular, ctx.mutations );
		o.diffuse = lit.diffuse;
		o.specular = lit.specular;
	}
	else
	{
		o.diffuse = vDiffuse;
		o.specular = vSpecular;
	}

	// Vertex fog (N8)
	const int vertexMode = (int)rs[RS_FOGVERTEXMODE];
	if (rs[RS_FOGTABLEMODE] == FOG_NONE && vertexMode != FOG_NONE)
	{
		const double d = rs[RS_RANGEFOGENABLE] ? sqrt( dot3( cam, cam ) ) : fabs( cam[2] );
		o.fog = fogFactor( vertexMode, d, stateFloat( rs[RS_FOGSTART] ), stateFloat( rs[RS_FOGEND] ),
				stateFloat( rs[RS_FOGDENSITY] ) );
	}
	else
		o.fog = o.specular.a;

	// Texture coordinates, per stage
	for (int st = 0; st < MAX_STAGES; ++st)
	{
		const uint32_t tci = s.stageState[st][TSS_TEXCOORDINDEX];
		double in[4] = { 0, 0, 0, 0 };
		switch (tci & 0xFFFF0000u)
		{
			case TSS_TCI_PASSTHRU:
			{
				const int set = (int)(tci & 0xFFFF);
				if (set < s.texCoordSets)
					padCoordinates( v.tex[set], s.texCoordSize[set], in );
				else
					padCoordinates( MISSING_SET_UV, 2, in );		// N28
				break;
			}
			case TSS_TCI_CAMERASPACENORMAL:
				in[0] = normal[0]; in[1] = normal[1]; in[2] = normal[2]; in[3] = 1;
				break;
			case TSS_TCI_CAMERASPACEPOSITION:
				in[0] = cam[0]; in[1] = cam[1]; in[2] = cam[2]; in[3] = 1;
				break;
			case TSS_TCI_CAMERASPACEREFLECTIONVECTOR:
			{
				// N14: R = 2(E.N)N - E, the infinite viewer at (0,0,-1) as N27's
				double e[3] = { 0, 0, -1 };
				if (rs[RS_LOCALVIEWER])
				{
					e[0] = -cam[0]; e[1] = -cam[1]; e[2] = -cam[2];
					normalise3( e );
				}
				const double en = dot3( e, normal );
				for (int i = 0; i < 3; ++i)
					in[i] = 2.0 * en * normal[i] - e[i];
				in[3] = 1;
				break;
			}
			default:
				break;	// refused in validation
		}
		const uint32_t ttff = s.stageState[st][TSS_TEXTURETRANSFORMFLAGS];
		const int count = (int)(ttff & 0xFF);
		if (count == 0)
		{
			for (int i = 0; i < 4; ++i)
				o.tex[st][i] = in[i];
		}
		else
		{
			double t[4];
			transform4( in, s.textureTransform[st], t );
			for (int i = 0; i < 4; ++i)
				o.tex[st][i] = i < count ? t[i] : 0.0;
		}
	}
	return o;
}

// ---- rasterisation ---------------------------------------------------------------------------------
struct SVert
{
	double X, Y, Z;		///< screen
	double invW;		///< 1/w (RHW)
	double zNdc;		///< z/w before the viewport's depth scale, for z-based table fog
	VOut v;
};

struct PixelIn
{
	Color diffuse, specular;
	double fog;
	double uv[MAX_STAGES][2];
	double dx[MAX_STAGES][2], dy[MAX_STAGES][2];	///< d(uv)/dx, d(uv)/dy per pixel, normalised units
	double eyeW, zNdc, Z;
};

/// A variant of the freedoms.  undefinedAlpha (N29): 0 passes CURRENT's alpha through a stage whose
/// ALPHAOP is DISABLE under an enabled COLOROP, 1 makes it 0, 2 makes it 1.
struct Perturb { double lod; double du, dv; int undefinedAlpha; };

/// Texture coordinates for one stage from interpolated elements (N13's divide)
void stageUV( const DrawState &s, int st, const double t[4], double uv[2] )
{
	const uint32_t ttff = s.stageState[st][TSS_TEXTURETRANSFORMFLAGS];
	const int count = (int)(ttff & 0xFF);
	if (count >= 2 && (ttff & TTFF_PROJECTED))
	{
		const double q = t[count - 1];
		uv[0] = t[0] / q;
		uv[1] = count >= 3 ? t[1] / q : 0.0;
	}
	else
	{
		uv[0] = t[0];
		uv[1] = count == 1 ? 0.0 : t[1];
	}
}

struct Triangle
{
	SVert v[3];
	double area2;
	double depthOffset;
	bool clockwise;		///< the original winding on screen (N12)
};

/// Screen-space barycentrics of (x, y): b[i] belongs to v[i]
void barycentrics( const Triangle &t, double x, double y, double b[3] )
{
	const SVert *v = t.v;
	b[0] = ((v[2].X - v[1].X) * (y - v[1].Y) - (v[2].Y - v[1].Y) * (x - v[1].X)) / t.area2;
	b[1] = ((v[0].X - v[2].X) * (y - v[2].Y) - (v[0].Y - v[2].Y) * (x - v[2].X)) / t.area2;
	b[2] = 1.0 - b[0] - b[1];
}

/// Perspective weights from screen barycentrics (N10)
void perspectiveWeights( const Triangle &t, const double b[3], unsigned mutations, double w[3] )
{
	if (mutations & MUTATE_SCREEN_LINEAR)
	{
		w[0] = b[0]; w[1] = b[1]; w[2] = b[2];
		return;
	}
	const double q0 = b[0] * t.v[0].invW, q1 = b[1] * t.v[1].invW, q2 = b[2] * t.v[2].invW;
	const double sum = q0 + q1 + q2;
	w[0] = q0 / sum; w[1] = q1 / sum; w[2] = q2 / sum;
}

void interpolateUV( const DrawState &s, const Triangle &t, const double w[3], double uv[MAX_STAGES][2] )
{
	for (int st = 0; st < MAX_STAGES; ++st)
	{
		double e[4];
		for (int i = 0; i < 4; ++i)
			e[i] = w[0] * t.v[0].v.tex[st][i] + w[1] * t.v[1].v.tex[st][i] + w[2] * t.v[2].v.tex[st][i];
		stageUV( s, st, e, uv[st] );
	}
}

void interpolate( const Context &ctx, const Triangle &t, double x, double y, PixelIn &in )
{
	double b[3], w[3];
	barycentrics( t, x, y, b );
	perspectiveWeights( t, b, ctx.mutations, w );
	const VOut &a = t.v[0].v, &c = t.v[1].v, &d = t.v[2].v;
	for (int ch = 0; ch < 4; ++ch)
	{
		setChannel( in.diffuse, ch, w[0] * channel( a.diffuse, ch ) + w[1] * channel( c.diffuse, ch ) + w[2] * channel( d.diffuse, ch ) );
		setChannel( in.specular, ch, w[0] * channel( a.specular, ch ) + w[1] * channel( c.specular, ch ) + w[2] * channel( d.specular, ch ) );
	}
	in.fog = w[0] * a.fog + w[1] * c.fog + w[2] * d.fog;
	interpolateUV( ctx.state, t, w, in.uv );
	in.Z = b[0] * t.v[0].Z + b[1] * t.v[1].Z + b[2] * t.v[2].Z + t.depthOffset;
	in.zNdc = b[0] * t.v[0].zNdc + b[1] * t.v[1].zNdc + b[2] * t.v[2].zNdc;
	in.eyeW = 1.0 / (b[0] * t.v[0].invW + b[1] * t.v[1].invW + b[2] * t.v[2].invW);

	// N15: exact derivatives, by a central difference far below a pixel
	const double h = 1.0 / 1024.0;
	double uvA[MAX_STAGES][2], uvB[MAX_STAGES][2];
	for (int axis = 0; axis < 2; ++axis)
	{
		const double dxh = axis == 0 ? h : 0.0, dyh = axis == 1 ? h : 0.0;
		barycentrics( t, x + dxh, y + dyh, b ); perspectiveWeights( t, b, ctx.mutations, w ); interpolateUV( ctx.state, t, w, uvA );
		barycentrics( t, x - dxh, y - dyh, b ); perspectiveWeights( t, b, ctx.mutations, w ); interpolateUV( ctx.state, t, w, uvB );
		for (int st = 0; st < MAX_STAGES; ++st)
			for (int i = 0; i < 2; ++i)
				(axis == 0 ? in.dx : in.dy)[st][i] = (uvA[st][i] - uvB[st][i]) / (2.0 * h);
	}
}

// ---- sampling: "Texture Filtering", "Bilinear Texture Filtering", "Texture Filtering with Mipmaps",
// D3DTEXTUREADDRESS, D3DSAMPLERSTATETYPE, "Directly Mapping Texels to Pixels" ----------------------
int addressTexel( int i, int size, int mode, bool *border )
{
	*border = false;
	switch (mode)
	{
		case TADDRESS_WRAP:
		{
			const int m = i % size;
			return m < 0 ? m + size : m;
		}
		case TADDRESS_MIRROR:
		{
			int m = i % (2 * size);
			if (m < 0) m += 2 * size;
			return m < size ? m : 2 * size - 1 - m;
		}
		case TADDRESS_CLAMP:
			return i < 0 ? 0 : (i >= size ? size - 1 : i);
		case TADDRESS_BORDER:
			if (i < 0 || i >= size) *border = true;
			return i;
		case TADDRESS_MIRRORONCE:
		{
			const int m = i < 0 ? -1 - i : i;
			return m >= size ? size - 1 : m;
		}
	}
	return 0;
}

Color fetch( const TextureLevel &level, const uint32_t sampler[14], int i, int j )
{
	bool borderU, borderV;
	const int x = addressTexel( i, level.width, (int)sampler[SAMP_ADDRESSU], &borderU );
	const int y = addressTexel( j, level.height, (int)sampler[SAMP_ADDRESSV], &borderV );
	if (borderU || borderV)
		return colorFromD3D( sampler[SAMP_BORDERCOLOR] );
	return level.texels[(size_t)y * level.width + x];
}

/// One level, one filter.  shiftU/V: the freedom's coordinate shift, in texels of this level.
Color filterLevel( const TextureLevel &level, const uint32_t sampler[14], int filter, double u, double v,
		double shiftU, double shiftV, unsigned mutations )
{
	const double corner = (mutations & MUTATE_TEXEL_CORNER) ? 0.5 : 0.0;
	if (filter == TEXF_POINT)
	{
		const int i = (int)floor( u * level.width + corner + shiftU );
		const int j = (int)floor( v * level.height + corner + shiftV );
		return fetch( level, sampler, i, j );
	}
	// LINEAR (and ANISOTROPIC at MAXANISOTROPY <= 1, N15): the 2x2 around texel centres at i + .5
	const double x = u * level.width - 0.5 + corner + shiftU;
	const double y = v * level.height - 0.5 + corner + shiftV;
	const int i0 = (int)floor( x ), j0 = (int)floor( y );
	const double fx = x - i0, fy = y - j0;
	const Color c00 = fetch( level, sampler, i0, j0 ), c10 = fetch( level, sampler, i0 + 1, j0 );
	const Color c01 = fetch( level, sampler, i0, j0 + 1 ), c11 = fetch( level, sampler, i0 + 1, j0 + 1 );
	return lerpColor( lerpColor( c00, c10, fx ), lerpColor( c01, c11, fx ), fy );
}

int magMinFilter( uint32_t f ) { return f == TEXF_ANISOTROPIC ? TEXF_LINEAR : (int)f; }

/// shiftU/V are multipliers (-1, 0, 1) of the freedom's texel shift for the filter in use
Color sampleImpl( const Texture &tex, const uint32_t sampler[14], double u, double v, double lambda,
		double shiftU, double shiftV, const Freedoms &fr, unsigned mutations )
{
	const int n = (int)tex.levels.size();
	int base = (int)sampler[SAMP_MAXMIPLEVEL];
	if (base > n - 1) base = n - 1;
	const double l = lambda + stateFloat( sampler[SAMP_MIPMAPLODBIAS] );
	if (l <= 0.0)
	{
		const int f = magMinFilter( sampler[SAMP_MAGFILTER] );
		const double sh = f == TEXF_POINT ? fr.pointTieTexels : fr.bilinearTexels;
		return filterLevel( tex.levels[base], sampler, f, u, v, shiftU * sh, shiftV * sh, mutations );
	}
	const int f = magMinFilter( sampler[SAMP_MINFILTER] );
	const double sh = f == TEXF_POINT ? fr.pointTieTexels : fr.bilinearTexels;
	const uint32_t mip = sampler[SAMP_MIPFILTER];
	if (mip == TEXF_NONE)
		return filterLevel( tex.levels[base], sampler, f, u, v, shiftU * sh, shiftV * sh, mutations );
	if (mip == TEXF_POINT)
	{
		int level = (int)floor( l + 0.5 );
		level = level < base ? base : (level > n - 1 ? n - 1 : level);
		return filterLevel( tex.levels[level], sampler, f, u, v, shiftU * sh, shiftV * sh, mutations );
	}
	const int l0 = (int)floor( l );
	const double frac = l - l0;
	const int a = l0 < base ? base : (l0 > n - 1 ? n - 1 : l0);
	const int b = l0 + 1 < base ? base : (l0 + 1 > n - 1 ? n - 1 : l0 + 1);
	const Color ca = filterLevel( tex.levels[a], sampler, f, u, v, shiftU * sh, shiftV * sh, mutations );
	if (a == b)
		return ca;
	const Color cb = filterLevel( tex.levels[b], sampler, f, u, v, shiftU * sh, shiftV * sh, mutations );
	return lerpColor( ca, cb, frac );
}

double lodOf( const Texture &tex, const double dx[2], const double dy[2] )
{
	const double w = tex.levels[0].width, h = tex.levels[0].height;
	const double rx = sqrt( dx[0] * w * dx[0] * w + dx[1] * h * dx[1] * h );
	const double ry = sqrt( dy[0] * w * dy[0] * w + dy[1] * h * dy[1] * h );
	const double rho = rx > ry ? rx : ry;
	return rho > 0.0 ? log2( rho ) : -1.0e9;
}

// ---- the texture-stage cascade: "Texture Blending", D3DTEXTUREOP, D3DTA, D3DTEXTURESTAGESTATETYPE ---
int arity( int op )
{
	switch (op)
	{
		case TOP_SELECTARG1: case TOP_PREMODULATE: return 1;
		case TOP_SELECTARG2: return 2;
		case TOP_MULTIPLYADD: case TOP_LERP: return 3;
		case TOP_BUMPENVMAP: case TOP_BUMPENVMAPLUMINANCE: return 0;
		case TOP_DISABLE: return 0;		// reads nothing (as ALPHAOP, N29)
	}
	return 12;
}

bool usesArg( int op, int which )
{
	switch (arity( op ))
	{
		case 0: return false;
		case 1: return which == 1;
		case 2: return which == 2;
		case 3: return true;
	}
	return which == 1 || which == 2;
}

/// The alpha a BLEND*ALPHA operation blends by
double blendAlphaFor( int op, const Color &diffuse, const Color &texel, const Color &tfactor, const Color &current )
{
	switch (op)
	{
		case TOP_BLENDDIFFUSEALPHA: return diffuse.a;
		case TOP_BLENDTEXTUREALPHA: case TOP_BLENDTEXTUREALPHAPM: return texel.a;
		case TOP_BLENDFACTORALPHA: return tfactor.a;
		case TOP_BLENDCURRENTALPHA: return current.a;
	}
	return 0.0;
}

/// A pixel program's result also carries the interval its range cap and precision allow (ffprogram.h)
struct ShadeOut { Color color; double lod[MAX_STAGES]; bool sampled[MAX_STAGES]; bool programInterval; Color programLo, programHi; };

/// The sampler a pixel program's tex and texbem call: the stage's own coordinates, moved by texbem's
/// (du, dv), sampled as the cascade samples (the LOD from the unmoved coordinates, N19), under the variant
struct ProgramSampling { const Context *ctx; const PixelIn *in; const Perturb *p; ShadeOut *out; };

Color cascade( const Context &ctx, const PixelIn &in, const Perturb &p, ShadeOut &out )
{
	const DrawState &s = ctx.state;
	const Color tfactor = colorFromD3D( ctx.rs[RS_TEXTUREFACTOR] );
	Color current = in.diffuse;
	Color temp = { 0, 0, 0, 0 };
	double bumpDu = 0, bumpDv = 0, bumpL = 1;
	bool bumpPending = false, premodulatePending = false;
	for (int st = 0; st < MAX_STAGES; ++st)
	{
		const uint32_t *ts = s.stageState[st];
		const int cop = (int)ts[TSS_COLOROP];
		if (cop == TOP_DISABLE)
			break;
		const Texture *tex = s.textures[st];
		if (tex == NULL && (ts[TSS_COLORARG1] & TA_SELECTMASK) == TA_TEXTURE)
			break;		// N20

		Color texel = { 0, 0, 0, 1 };
		out.sampled[st] = false;
		if (tex != NULL)
		{
			double u = in.uv[st][0], v = in.uv[st][1];
			if (bumpPending)
			{
				u += bumpDu;
				v += bumpDv;
			}
			const double lambda = lodOf( *tex, in.dx[st], in.dy[st] ) + p.lod;
			texel = sampleImpl( *tex, s.samplerState[st], u, v, lambda, p.du, p.dv, ctx.freedoms, ctx.mutations );
			if (bumpPending)
			{
				texel.r *= bumpL; texel.g *= bumpL; texel.b *= bumpL;	// N19
			}
			out.lod[st] = lambda + stateFloat( s.samplerState[st][SAMP_MIPMAPLODBIAS] );
			out.sampled[st] = true;
		}
		bumpPending = false;

		if (cop == TOP_BUMPENVMAP || cop == TOP_BUMPENVMAPLUMINANCE)
		{
			// "Bump Mapping Formulas", N19; CURRENT passes through
			const double m00 = stateFloat( ts[TSS_BUMPENVMAT00] ), m01 = stateFloat( ts[TSS_BUMPENVMAT01] );
			const double m10 = stateFloat( ts[TSS_BUMPENVMAT10] ), m11 = stateFloat( ts[TSS_BUMPENVMAT11] );
			bumpDu = texel.r * m00 + texel.g * m10;
			bumpDv = texel.r * m01 + texel.g * m11;
			bumpL = cop == TOP_BUMPENVMAPLUMINANCE
				? clamp01( texel.b * stateFloat( ts[TSS_BUMPENVLSCALE] ) + stateFloat( ts[TSS_BUMPENVLOFFSET] ) )
				: 1.0;
			bumpPending = true;
			continue;
		}

		Color cur = current;
		if (premodulatePending && tex != NULL)
			cur = mulColor( current, texel );		// N26
		premodulatePending = false;

		const Color constant = colorFromD3D( ts[TSS_CONSTANT] );
		struct Arg
		{
			static Color read( uint32_t a, const PixelIn &in, const Color &cur, const Color &texel,
					const Color &tfactor, const Color &temp, const Color &constant )
			{
				Color c;
				switch (a & TA_SELECTMASK)
				{
					case TA_DIFFUSE: c = in.diffuse; break;
					case TA_CURRENT: c = cur; break;
					case TA_TEXTURE: c = texel; break;
					case TA_TFACTOR: c = tfactor; break;
					case TA_SPECULAR: c = in.specular; break;
					case TA_TEMP: c = temp; break;
					default: c = constant; break;
				}
				if (a & TA_ALPHAREPLICATE)
					c.r = c.g = c.b = c.a;
				if (a & TA_COMPLEMENT)
				{
					c.r = 1 - c.r; c.g = 1 - c.g; c.b = 1 - c.b; c.a = 1 - c.a;
				}
				return c;
			}
		};
		const Color c0 = Arg::read( ts[TSS_COLORARG0], in, cur, texel, tfactor, temp, constant );
		const Color c1 = Arg::read( ts[TSS_COLORARG1], in, cur, texel, tfactor, temp, constant );
		const Color c2 = Arg::read( ts[TSS_COLORARG2], in, cur, texel, tfactor, temp, constant );
		const Color a0 = Arg::read( ts[TSS_ALPHAARG0], in, cur, texel, tfactor, temp, constant );
		const Color a1 = Arg::read( ts[TSS_ALPHAARG1], in, cur, texel, tfactor, temp, constant );
		const Color a2 = Arg::read( ts[TSS_ALPHAARG2], in, cur, texel, tfactor, temp, constant );

		const int aop = (int)ts[TSS_ALPHAOP];
		Color r;
		const double colourBlend = blendAlphaFor( cop, in.diffuse, texel, tfactor, cur );
		for (int ch = 0; ch < 3; ++ch)
			setChannel( r, ch, cop == TOP_PREMODULATE ? channel( c1, ch ) : textureOp( cop, ch, c0, c1, c2, colourBlend, ctx.mutations ) );
		if (aop == TOP_DISABLE)		// N29: undefined; the variant says what it gives
			r.a = p.undefinedAlpha == 1 ? 0.0 : (p.undefinedAlpha == 2 ? 1.0 : cur.a);
		else
			r.a = aop == TOP_PREMODULATE ? a1.a
				: textureOp( aop, 3, a0, a1, a2, blendAlphaFor( aop, in.diffuse, texel, tfactor, cur ), ctx.mutations );
		if (cop == TOP_DOTPRODUCT3)
			r.a = r.r;		// N17
		r = saturate( r );	// N16
		if (cop == TOP_PREMODULATE || aop == TOP_PREMODULATE)
			premodulatePending = true;
		if ((ts[TSS_RESULTARG] & TA_SELECTMASK) == TA_TEMP)
			temp = r;
		else
			current = r;
	}
	return current;
}

/// The fog factor shade() blends by (1: no fog)
double fogFactorOf( const Context &ctx, const PixelIn &in )
{
	const uint32_t *rs = ctx.rs;
	if (!rs[RS_FOGENABLE])
		return 1.0;
	double f = in.fog;
	const int table = (int)rs[RS_FOGTABLEMODE];
	if (table != FOG_NONE)
	{
		const Matrix &pr = ctx.state.projection;
		const bool affine = pr.m[0][3] == 0.0 && pr.m[1][3] == 0.0 && pr.m[2][3] == 0.0 && pr.m[3][3] == 1.0;
		f = fogFactor( table, affine ? in.zNdc : in.eyeW, stateFloat( rs[RS_FOGSTART] ),
				stateFloat( rs[RS_FOGEND] ), stateFloat( rs[RS_FOGDENSITY] ) );
	}
	f = clamp01( f );
	return (ctx.mutations & MUTATE_FOG_REVERSED) ? 1.0 - f : f;
}

void programSample( void *context, int stage, double du, double dv, double rgba[4] )
{
	const ProgramSampling &ps = *(const ProgramSampling *)context;
	const DrawState &s = ps.ctx->state;
	const Texture *tex = s.textures[stage];
	Color texel = { 0, 0, 0, 1 };
	if (tex != NULL)
	{
		const double lambda = lodOf( *tex, ps.in->dx[stage], ps.in->dy[stage] ) + ps.p->lod;
		texel = sampleImpl( *tex, s.samplerState[stage], ps.in->uv[stage][0] + du, ps.in->uv[stage][1] + dv, lambda,
			ps.p->du, ps.p->dv, ps.ctx->freedoms, ps.ctx->mutations );
		ps.out->lod[stage] = lambda + stateFloat( s.samplerState[stage][SAMP_MIPMAPLODBIAS] );
		ps.out->sampled[stage] = true;
	}
	rgba[0] = texel.r; rgba[1] = texel.g; rgba[2] = texel.b; rgba[3] = texel.a;
}

/// A pixel program in the texture cascade's place: no specular add (the program is the colour), then fog;
/// the output saturated as the frame buffer takes it, and its interval with it (ZONE_PROGRAM)
Color shadeProgram( const Context &ctx, const PixelIn &in, const Perturb &p, ShadeOut &out )
{
	const DrawState &s = ctx.state;
	ProgramSampling sampling = { &ctx, &in, &p, &out };
	PixelInputs pin;
	memset( &pin, 0, sizeof( pin ) );
	const Color *colours[ 2 ] = { &in.diffuse, &in.specular };
	for (int k = 0; k < 2; ++k)
	{
		pin.colour[k][0] = colours[k]->r; pin.colour[k][1] = colours[k]->g;
		pin.colour[k][2] = colours[k]->b; pin.colour[k][3] = colours[k]->a;
	}
	memcpy( pin.constants, s.pixelConstants, sizeof( pin.constants ) );
	for (int st = 0; st < 4 && st < MAX_STAGES; ++st)
	{
		pin.bumpMatrix[st][0] = stateFloat( s.stageState[st][TSS_BUMPENVMAT00] );
		pin.bumpMatrix[st][1] = stateFloat( s.stageState[st][TSS_BUMPENVMAT01] );
		pin.bumpMatrix[st][2] = stateFloat( s.stageState[st][TSS_BUMPENVMAT10] );
		pin.bumpMatrix[st][3] = stateFloat( s.stageState[st][TSS_BUMPENVMAT11] );
	}
	pin.sample = programSample;
	pin.context = &sampling;
	Interval4 r;
	runPixelProgram( *s.pixelProgram, pin, r );
	const double f = fogFactorOf( ctx, in );
	const Color fog = colorFromD3D( ctx.rs[RS_FOGCOLOR] );
	Color c, lo, hi;
	for (int ch = 0; ch < 4; ++ch)
	{
		const double fogChannel = ch == 3 ? 1.0 : channel( fog, ch );
		const double ff = ch == 3 ? 1.0 : f;		// fog blends the colour, not the alpha
		setChannel( c, ch, ff * clamp01( r.nominal[ch] ) + (1 - ff) * fogChannel );
		setChannel( lo, ch, ff * clamp01( r.lo[ch] ) + (1 - ff) * fogChannel );
		setChannel( hi, ch, ff * clamp01( r.hi[ch] ) + (1 - ff) * fogChannel );
	}
	out.programInterval = true;
	out.programLo = lo;
	out.programHi = hi;
	out.color = c;
	return c;
}

bool differs( const Color &a, const Color &b );

/// A pixel program's interval, as the four colours at its corners: the blend is linear in the colour and
/// in the alpha separately, so those four bound what any value inside gives (ZONE_PROGRAM)
void addProgramCorners( const ShadeOut &o, const Color &src, std::vector<Color> &others, unsigned &zones )
{
	if (!o.programInterval)
		return;
	for (int k = 0; k < 4; ++k)
	{
		Color c = (k & 1) ? o.programHi : o.programLo;
		c.a = (k & 2) ? o.programHi.a : o.programLo.a;
		if (differs( c, src ))
		{
			zones |= ZONE_PROGRAM;
			others.push_back( c );
		}
	}
}

/// The pixel's colour before the frame buffer: the cascade, specular add, fog
Color shade( const Context &ctx, const PixelIn &in, const Perturb &p, ShadeOut &out )
{
	const uint32_t *rs = ctx.rs;
	memset( &out, 0, sizeof( out ) );
	if (ctx.state.pixelProgram != NULL)
		return shadeProgram( ctx, in, p, out );
	Color c = cascade( ctx, in, p, out );
	if (rs[RS_SPECULARENABLE] && !(ctx.mutations & MUTATE_NO_SPECULAR_ADD))
	{
		c.r = clamp01( c.r + in.specular.r );		// N6
		c.g = clamp01( c.g + in.specular.g );
		c.b = clamp01( c.b + in.specular.b );
	}
	if (rs[RS_FOGENABLE])
	{
		// "Fog Formulas", "Pixel Fog", "Vertex Fog"; N8, N9
		double f = in.fog;
		const int table = (int)rs[RS_FOGTABLEMODE];
		if (table != FOG_NONE)
		{
			const Matrix &pr = ctx.state.projection;
			const bool affine = pr.m[0][3] == 0.0 && pr.m[1][3] == 0.0 && pr.m[2][3] == 0.0 && pr.m[3][3] == 1.0;
			f = fogFactor( table, affine ? in.zNdc : in.eyeW, stateFloat( rs[RS_FOGSTART] ),
					stateFloat( rs[RS_FOGEND] ), stateFloat( rs[RS_FOGDENSITY] ) );
		}
		f = clamp01( f );
		if (ctx.mutations & MUTATE_FOG_REVERSED)
			f = 1.0 - f;
		const Color fog = colorFromD3D( rs[RS_FOGCOLOR] );
		c.r = f * c.r + (1 - f) * fog.r;
		c.g = f * c.g + (1 - f) * fog.g;
		c.b = f * c.b + (1 - f) * fog.b;
	}
	out.color = c;
	return c;
}

// ---- the frame buffer: "Alpha Testing State", D3DSTENCILOP, D3DRS_STENCIL*, D3DRS_Z* --------------
uint32_t stencilOp( int op, uint32_t value, uint32_t ref, uint32_t max )
{
	switch (op)
	{
		case STENCILOP_ZERO: return 0;
		case STENCILOP_REPLACE: return ref & max;
		case STENCILOP_INCRSAT: return value >= max ? max : value + 1;
		case STENCILOP_DECRSAT: return value == 0 ? 0 : value - 1;
		case STENCILOP_INVERT: return ~value & max;
		case STENCILOP_INCR: return value >= max ? 0 : value + 1;
		case STENCILOP_DECR: return value == 0 ? max : value - 1;
	}
	return value;
}

bool differs( const Color &a, const Color &b )
{
	return fabs( a.r - b.r ) > 1e-12 || fabs( a.g - b.g ) > 1e-12 || fabs( a.b - b.b ) > 1e-12 || fabs( a.a - b.a ) > 1e-12;
}

Color writeMasked( const Color &src, const Color &dst, uint32_t mask )
{
	Color c = dst;
	if (mask & 1) c.r = src.r;
	if (mask & 2) c.g = src.g;
	if (mask & 4) c.b = src.b;
	if (mask & 8) c.a = src.a;
	return c;
}

double quantise( double x, int bits, int direction )
{
	if (bits <= 0)
		return x;
	const double levels = (double)((1 << bits) - 1);
	const double s = x * levels;
	const double q = direction < 0 ? floor( s + 1e-9 ) : direction > 0 ? ceil( s - 1e-9 ) : floor( s + 0.5 );
	return clamp01( q / levels );
}

Color quantiseColor( const Color &c, int bits, int direction )
{
	Color q = { quantise( c.r, bits, direction ), quantise( c.g, bits, direction ),
		quantise( c.b, bits, direction ), quantise( c.a, bits, direction ) };
	return q;
}

struct Raster
{
	const Context &ctx;
	Target &target;
	Report &report;
	int x0, y0, x1, y1;		///< pixel bounds, exclusive ends: viewport, target, scissor
	int primitive;			///< the primitive being drawn, for Target::detail
};

bool alphaPasses( const uint32_t *rs, double alpha )
{
	if (!rs[RS_ALPHATESTENABLE])
		return true;
	return compareFunc( (int)rs[RS_ALPHAFUNC], alpha, (double)(rs[RS_ALPHAREF] & 0xFF) / 255.0 );	// N21
}

bool topLeft( double dx, double dy ) { return dy < 0.0 || (dy == 0.0 && dx > 0.0); }	// "Rasterization Rules"

void rasterTriangle( Raster &r, Triangle t )
{
	const Context &ctx = r.ctx;
	const DrawState &s = ctx.state;
	const uint32_t *rs = ctx.rs;
	const Freedoms &fr = ctx.freedoms;
	Target &tg = r.target;
	bool undefinedAlphaStage = false;		// N29: a stage the cascade reaches with ALPHAOP DISABLE
	for (int st = 0; st < MAX_STAGES && s.stageState[st][TSS_COLOROP] != TOP_DISABLE; ++st)
		if (s.stageState[st][TSS_ALPHAOP] == TOP_DISABLE)
			undefinedAlphaStage = true;

	t.area2 = (t.v[1].X - t.v[0].X) * (t.v[2].Y - t.v[0].Y) - (t.v[2].X - t.v[0].X) * (t.v[1].Y - t.v[0].Y);
	if (t.area2 == 0.0)
		return;
	if (t.area2 < 0.0)
	{
		std::swap( t.v[1], t.v[2] );
		t.area2 = -t.area2;
	}

	// N22: depth bias from the plane's slope
	const double dZdX = ((t.v[1].Z - t.v[0].Z) * (t.v[2].Y - t.v[0].Y) - (t.v[2].Z - t.v[0].Z) * (t.v[1].Y - t.v[0].Y)) / t.area2;
	const double dZdY = ((t.v[2].Z - t.v[0].Z) * (t.v[1].X - t.v[0].X) - (t.v[1].Z - t.v[0].Z) * (t.v[2].X - t.v[0].X)) / t.area2;
	t.depthOffset = stateFloat( rs[RS_DEPTHBIAS] ) + stateFloat( rs[RS_SLOPESCALEDEPTHBIAS] ) * std::max( fabs( dZdX ), fabs( dZdY ) );

	double minX = t.v[0].X, maxX = minX, minY = t.v[0].Y, maxY = minY;
	for (int i = 1; i < 3; ++i)
	{
		minX = std::min( minX, t.v[i].X ); maxX = std::max( maxX, t.v[i].X );
		minY = std::min( minY, t.v[i].Y ); maxY = std::max( maxY, t.v[i].Y );
	}
	const int bx0 = std::max( r.x0, (int)floor( minX ) - 1 ), bx1 = std::min( r.x1, (int)ceil( maxX ) + 2 );
	const int by0 = std::max( r.y0, (int)floor( minY ) - 1 ), by1 = std::min( r.y1, (int)ceil( maxY ) + 2 );
	const double offset = (ctx.mutations & MUTATE_RASTER_HALF_PIXEL) ? 0.5 : 0.0;

	const uint32_t writeMask = rs[RS_COLORWRITEENABLE];
	const bool zOn = rs[RS_ZENABLE] == ZB_TRUE;
	const bool stencilOn = rs[RS_STENCILENABLE] != 0;
	const bool ccwStencil = rs[RS_TWOSIDEDSTENCILMODE] && !t.clockwise;
	const int sFunc = (int)rs[ccwStencil ? RS_CCW_STENCILFUNC : RS_STENCILFUNC];
	const int sFail = (int)rs[ccwStencil ? RS_CCW_STENCILFAIL : RS_STENCILFAIL];
	const int sZFail = (int)rs[ccwStencil ? RS_CCW_STENCILZFAIL : RS_STENCILZFAIL];
	const int sPass = (int)rs[ccwStencil ? RS_CCW_STENCILPASS : RS_STENCILPASS];
	const uint32_t sMax = tg.stencilBits >= 32 ? 0xFFFFFFFFu : ((1u << tg.stencilBits) - 1);
	const uint32_t sRef = rs[RS_STENCILREF] & sMax, sMask = rs[RS_STENCILMASK], sWrite = rs[RS_STENCILWRITEMASK];

	for (int py = by0; py < by1; ++py)
		for (int px = bx0; px < bx1; ++px)
		{
			const double sx = px + offset, sy = py + offset;
			bool inside = true, ambiguous = false;
			double nearest = 1e30;
			for (int e = 0; e < 3; ++e)
			{
				const SVert &a = t.v[(e + 1) % 3], &b = t.v[(e + 2) % 3];	// the edge opposite vertex e
				const double dx = b.X - a.X, dy = b.Y - a.Y;
				const double E = dx * (sy - a.Y) - dy * (sx - a.X);
				const double dist = E / sqrt( dx * dx + dy * dy );
				nearest = std::min( nearest, dist );
				if (fabs( dist ) < fr.edgePixels)
					ambiguous = true;
				const bool thisSide = E > 0.0 || (E == 0.0 && ((ctx.mutations & MUTATE_TOP_LEFT_OFF) || topLeft( dx, dy )));
				inside = inside && thisSide;
			}
			if (nearest < -fr.edgePixels || (!inside && !ambiguous))
				continue;
			const size_t idx = (size_t)py * tg.width + px;

			PixelIn in;
			interpolate( ctx, t, sx, sy, in );
			double z = clamp01( in.Z );		// N22
			if (s.pretransformed && (in.Z < 0.0 || in.Z > 1.0))
				continue;		// N25

			// The source colour, nominal and under each freedom
			ShadeOut nominalOut;
			Perturb p0 = { 0, 0, 0, 0 };
			const Color src = shade( ctx, in, p0, nominalOut );
			// The freedoms' variants (compare()'s envelope): the LOD at the window's ends and on both sides
			// of every integer level inside it, since trilinear colour is piecewise linear in lambda with
			// breakpoints at the integers and a level's colour need not be monotonic (N15, F11), and
			// the magnify/minify switch at 0 is a step; each LOD alone and with each diagonal texel
			// shift, because hardware has both freedoms at once.
			std::vector<double> lodOffsets;
			lodOffsets.push_back( 0.0 );
			lodOffsets.push_back( -fr.lodDelta );
			lodOffsets.push_back( fr.lodDelta );
			for (int st = 0; st < MAX_STAGES; ++st)
				if (nominalOut.sampled[st])
					for (double k = ceil( nominalOut.lod[st] - fr.lodDelta ); k <= nominalOut.lod[st] + fr.lodDelta; k += 1.0)
					{
						lodOffsets.push_back( k - nominalOut.lod[st] - 1e-6 );
						lodOffsets.push_back( k - nominalOut.lod[st] );
						lodOffsets.push_back( k - nominalOut.lod[st] + 1e-6 );
					}
			static const double shifts[5][2] = { { 0, 0 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
			std::vector<Color> others;
			unsigned zones = ambiguous ? ZONE_EDGE : 0;
			const int undefinedVariants = undefinedAlphaStage ? 3 : 1;		// N29: pass-through, 0 and 1
			for (int u = 0; u < undefinedVariants; ++u)
				for (size_t l = 0; l < lodOffsets.size(); ++l)
					for (int t = 0; t < 5; ++t)
					{
						if (u == 0 && l == 0 && t == 0)
							continue;		// the nominal itself
						const Perturb p = { lodOffsets[l], shifts[t][0], shifts[t][1], u };
						ShadeOut o;
						const Color c = shade( ctx, in, p, o );
						if (differs( c, src ))
						{
							zones |= (l != 0 ? ZONE_LOD : 0) | (t != 0 ? ZONE_TEXEL : 0) | (u != 0 ? ZONE_UNDEFINED : 0);
							others.push_back( c );
						}
						addProgramCorners( o, src, others, zones );
					}
			addProgramCorners( nominalOut, src, others, zones );
			for (int st = 0; st < MAX_STAGES; ++st)
				if (inside && nominalOut.sampled[st] && s.stageState[st][TSS_COLOROP] != TOP_DISABLE)
				{
					r.report.minLod = std::min( r.report.minLod, nominalOut.lod[st] );
					r.report.maxLod = std::max( r.report.maxLod, nominalOut.lod[st] );
				}

			// Alpha test, nominal and possible
			const bool alphaNominal = alphaPasses( rs, src.a );
			bool alphaCanPass = alphaNominal, alphaCanFail = !alphaNominal;
			std::vector<const Color *> all( 1, &src );
			for (size_t k = 0; k < others.size(); ++k)
				all.push_back( &others[k] );
			const int allCount = (int)all.size();
			for (int k = 0; k < allCount; ++k)
				for (int d = -1; d <= 1; ++d)
				{
					const bool pass = alphaPasses( rs, all[k]->a + d * fr.alphaRef );
					alphaCanPass = alphaCanPass || pass;
					alphaCanFail = alphaCanFail || !pass;
				}
			if (alphaCanPass && alphaCanFail && rs[RS_ALPHATESTENABLE])
				zones |= ZONE_ALPHA_TEST;

			// Stencil and depth, nominal and possible
			const uint32_t stencilValue = tg.stencil[idx];
			const bool stencilNominal = !stencilOn || compareFunc( sFunc, (double)(sRef & sMask), (double)(stencilValue & sMask) );
			const bool stencilAmbiguous = stencilOn && tg.stencilAmbiguous[idx];
			const bool depthNominal = !zOn || compareFunc( (int)rs[RS_ZFUNC], z, tg.depth[idx] );
			const bool depthAmbiguous = zOn && (fabs( z - tg.depth[idx] ) <= fr.depthTie || tg.depthAmbiguous[idx]);
			if (stencilAmbiguous) zones |= ZONE_STENCIL;
			if (depthAmbiguous) zones |= ZONE_DEPTH;

			const bool testsNominal = stencilNominal && depthNominal;
			const bool testsCanPass = testsNominal || stencilAmbiguous || depthAmbiguous;
			const bool testsCanFail = !testsNominal || stencilAmbiguous || depthAmbiguous;

			const bool writeNominal = inside && alphaNominal && testsNominal;
			const bool canWrite = (inside || ambiguous) && alphaCanPass && testsCanPass;
			const bool canSkip = !inside || ambiguous || alphaCanFail || testsCanFail;

			if (inside) ++r.report.pixelsCovered;
			if (ambiguous) ++r.report.pixelsAmbiguous;

			// The colour: the nominal result, and the envelope over every possible outcome
			const Color dstNominal = tg.color[idx], dstLo = tg.lo[idx], dstHi = tg.hi[idx];
			Color lo = { 2, 2, 2, 2 }, hi = { -1, -1, -1, -1 };
			struct Env
			{
				static void add( Color &lo, Color &hi, const Color &c )
				{
					for (int ch = 0; ch < 4; ++ch)
					{
						setChannel( lo, ch, std::min( channel( lo, ch ), channel( c, ch ) ) );
						setChannel( hi, ch, std::max( channel( hi, ch ), channel( c, ch ) ) );
					}
				}
			};
			if (canSkip)
			{
				Env::add( lo, hi, dstNominal ); Env::add( lo, hi, dstLo ); Env::add( lo, hi, dstHi );
			}
			if (canWrite)
			{
				const Color dsts[3] = { dstNominal, dstLo, dstHi };
				for (int k = 0; k < allCount; ++k)
				{
					const bool thisCanPass = alphaPasses( rs, all[k]->a ) || alphaPasses( rs, all[k]->a + fr.alphaRef )
						|| alphaPasses( rs, all[k]->a - fr.alphaRef );
					if (!thisCanPass && !(zones & ZONE_ALPHA_TEST))
						continue;		// N30: an undecided alpha test writes the hull of every variant
					for (int d = 0; d < 3; ++d)
					{
						const Color b = blend( s, *all[k], dsts[d], tg.hasAlpha, ctx.mutations );
						Env::add( lo, hi, writeMasked( b, dsts[d], writeMask ) );
					}
				}
			}
			if (!tg.hasAlpha)
			{
				lo.a = hi.a = 1.0;
			}

			Color result = dstNominal;
			if (writeNominal && tg.recordDetail)
			{
				PixelDetail &d = tg.detail[idx];
				d.primitive = r.primitive;
				if (d.layers < 8)
					d.writers[d.layers] = r.primitive;
				++d.layers;
				d.source = src;
				d.alphaPassed = alphaNominal;
				for (int st = 0; st < 2; ++st)
				{
					d.uv[st][0] = in.uv[st][0];
					d.uv[st][1] = in.uv[st][1];
					d.lod[st] = nominalOut.sampled[st] ? nominalOut.lod[st] : -1e9;
					const Texture *tex = s.textures[st];
					const double w = tex && !tex->levels.empty() ? tex->levels[0].width : 0, h = tex && !tex->levels.empty() ? tex->levels[0].height : 0;
					d.axes[st][0] = sqrt( in.dx[st][0] * w * in.dx[st][0] * w + in.dx[st][1] * h * in.dx[st][1] * h );
					d.axes[st][1] = sqrt( in.dy[st][0] * w * in.dy[st][0] * w + in.dy[st][1] * h * in.dy[st][1] * h );
				}
				for (int k = 0; k < 3; ++k)
				{
					d.screen[k][0] = t.v[k].X;
					d.screen[k][1] = t.v[k].Y;
				}
			}
			if (writeNominal)
			{
				result = writeMasked( blend( s, src, dstNominal, tg.hasAlpha, ctx.mutations ), dstNominal, writeMask );
				if (!tg.hasAlpha)
					result.a = 1.0;
				++r.report.pixelsWritten;
			}
			tg.color[idx] = quantiseColor( result, tg.colorBits, 0 );
			tg.lo[idx] = quantiseColor( lo, tg.colorBits, -1 );
			tg.hi[idx] = quantiseColor( hi, tg.colorBits, 1 );
			if (canWrite)
				tg.zones[idx] |= zones;
			r.report.zonesSeen |= canWrite ? zones : 0;

			// Stencil and depth updates, nominal; ambiguity where the outcome could differ
			const bool reached = inside && alphaNominal;
			if (reached && stencilOn)
			{
				const int op = !stencilNominal ? sFail : !depthNominal ? sZFail : sPass;
				const uint32_t v = stencilOp( op, stencilValue, sRef, sMax );
				tg.stencil[idx] = ((stencilValue & ~sWrite) | (v & sWrite)) & sMax;
			}
			if (stencilOn && (ambiguous || (alphaCanPass && alphaCanFail) || stencilAmbiguous || depthAmbiguous))
				tg.stencilAmbiguous[idx] = 1;
			if (zOn && rs[RS_ZWRITEENABLE])
			{
				if (writeNominal)
					tg.depth[idx] = z;
				if (canWrite && canSkip)
					tg.depthAmbiguous[idx] = 1;
				else if (writeNominal)
					tg.depthAmbiguous[idx] = 0;
			}
		}
}

/// Sutherland-Hodgman against one clip-space plane, dist(v) >= 0 kept
template <typename Dist>
std::vector<VOut> clipPlane( const std::vector<VOut> &poly, Dist dist )
{
	std::vector<VOut> out;
	for (size_t i = 0; i < poly.size(); ++i)
	{
		const VOut &a = poly[i], &b = poly[(i + 1) % poly.size()];
		const double da = dist( a ), db = dist( b );
		if (da >= 0.0)
			out.push_back( a );
		if ((da >= 0.0) != (db >= 0.0))
			out.push_back( lerpVOut( a, b, da / (da - db) ) );
	}
	return out;
}

double distNear( const VOut &v ) { return v.clip[2]; }
double distFar( const VOut &v ) { return v.clip[3] - v.clip[2]; }
double distW( const VOut &v ) { return v.clip[3] - 1e-12; }

SVert toScreen( const DrawState &s, const VOut &v )
{
	SVert o;
	o.v = v;
	if (s.pretransformed)
	{
		o.X = v.clip[0]; o.Y = v.clip[1]; o.Z = v.clip[2]; o.invW = v.clip[3]; o.zNdc = v.clip[2];
		return o;
	}
	const Viewport &vp = s.viewport;
	o.invW = 1.0 / v.clip[3];
	o.X = vp.x + (1.0 + v.clip[0] * o.invW) * vp.width * 0.5;
	o.Y = vp.y + (1.0 - v.clip[1] * o.invW) * vp.height * 0.5;
	o.zNdc = v.clip[2] * o.invW;
	o.Z = vp.minZ + o.zNdc * (vp.maxZ - vp.minZ);
	return o;
}

void drawTriangle( Raster &r, const VOut &a, const VOut &b, const VOut &c )
{
	const DrawState &s = r.ctx.state;
	++r.report.trianglesIn;
	std::vector<VOut> poly;
	poly.push_back( a ); poly.push_back( b ); poly.push_back( c );
	if (!s.pretransformed)
	{
		poly = clipPlane( poly, distNear );
		poly = clipPlane( poly, distFar );
		poly = clipPlane( poly, distW );
		if (poly.size() < 3)
		{
			++r.report.trianglesClippedAway;
			return;
		}
	}
	std::vector<SVert> sv;
	for (size_t i = 0; i < poly.size(); ++i)
		sv.push_back( toScreen( s, poly[i] ) );
	double area = 0.0;
	for (size_t i = 0; i < sv.size(); ++i)
	{
		const SVert &p = sv[i], &q = sv[(i + 1) % sv.size()];
		area += p.X * q.Y - q.X * p.Y;
	}
	if (area == 0.0)
		return;
	const bool clockwise = area > 0.0;		// N12: y down
	const int cull = (int)r.ctx.rs[RS_CULLMODE];
	if ((cull == CULL_CW && clockwise) || (cull == CULL_CCW && !clockwise))
	{
		++r.report.trianglesCulled;
		return;
	}
	for (size_t i = 1; i + 1 < sv.size(); ++i)
	{
		Triangle t;
		t.v[0] = sv[0]; t.v[1] = sv[i]; t.v[2] = sv[i + 1];
		t.clockwise = clockwise;
		t.depthOffset = 0.0;
		rasterTriangle( r, t );
	}
}

void refuse( Report &report, const std::string &why ) { report.refusals.push_back( why ); }

std::string stageText( int st, const char *what )
{
	char buf[96];
	snprintf( buf, sizeof( buf ), "stage %d: %s", st, what );
	return buf;
}

void validateSampling( const DrawState &s, int st, Report &report );

/// A3e: the programs, and what they need of the rest of the state
void validatePrograms( const DrawState &s, Report &report )
{
	const uint32_t *rs = s.renderState;
	if (s.vertexShaderBound && s.vertexProgram == NULL) refuse( report, "a vertex shader is bound" );
	if (s.pixelShaderBound && s.pixelProgram == NULL) refuse( report, "a pixel shader is bound" );
	if (s.vertexProgram != NULL)
	{
		if (!s.vertexProgram->refusals.empty() || s.vertexProgram->kind != PROGRAM_VERTEX)
			refuse( report, "a vertex program outside the census: "
				+ (s.vertexProgram->refusals.empty() ? std::string( "not a vertex program" ) : s.vertexProgram->refusals[0]) );
		if (s.pretransformed) refuse( report, "a vertex program with pretransformed vertices" );
		if (s.programInputsGiven)
			for (size_t i = 0; i < s.vertexProgram->code.size(); ++i)
				for (int k = 0; k < s.vertexProgram->code[i].sources; ++k)
				{
					const Operand &o = s.vertexProgram->code[i].src[k];
					if (o.type == Token::REG_INPUT && !(s.programInputPresent & (1u << o.index)))
						refuse( report, "a vertex program reads an input no declaration element feeds" );
				}
		if (rs[RS_FOGENABLE]) refuse( report, "fog with a vertex program (oFog is not in the census)" );
		for (int st = 0; st < MAX_STAGES; ++st)
			if (s.stageState[st][TSS_TEXTURETRANSFORMFLAGS] != 0 || (s.stageState[st][TSS_TEXCOORDINDEX] & 0xFFFF0000u) != 0)
				refuse( report, stageText( st, "a texture transform or generation with a vertex program (not in the census)" ) );
		if (s.pixelProgram == NULL)
		{
			// The fixed-function pixel stage behind a vertex program (Trees.vso, capture v2): D3DTSS_TEXCOORDINDEX,
			// "When rendering using vertex shaders, each stage's texture coordinate index must be set to its
			// default value", which is the stage's own number, so stage n reads oTn.  What the cascade reads
			// must have been written (P3): oD0 (it starts from the diffuse), oD1 under SPECULARENABLE, oTn at
			// every stage it samples.
			unsigned colours = 0, coordinates = 0;
			for (size_t i = 0; i < s.vertexProgram->code.size(); ++i)
			{
				const Operand &d = s.vertexProgram->code[i].dst;
				if (d.type == Token::REG_ATTROUT) colours |= 1u << d.index;
				if (d.type == Token::REG_TEXCRDOUT) coordinates |= 1u << d.index;
			}
			if (!(colours & 1u)) refuse( report, "the fixed-function pixel stage reads a diffuse the vertex program never wrote" );
			if (rs[RS_SPECULARENABLE] && !(colours & 2u))
				refuse( report, "a specular add of a specular the vertex program never wrote" );
			for (int st = 0; st < MAX_STAGES && s.stageState[st][TSS_COLOROP] != TOP_DISABLE; ++st)
			{
				if ((s.stageState[st][TSS_TEXCOORDINDEX] & 0xFFFF) != (uint32_t)st)
					refuse( report, stageText( st, "a coordinate index other than the stage's own with a vertex program (D3DTSS_TEXCOORDINDEX)" ) );
				if (s.textures[st] != NULL && !(coordinates & (1u << st)))
					refuse( report, stageText( st, "sampled at coordinates the vertex program never wrote" ) );
			}
		}
	}
	if (s.pixelProgram != NULL)
	{
		const Program &pp = *s.pixelProgram;
		if (!pp.refusals.empty() || pp.kind != PROGRAM_PIXEL)
			refuse( report, "a pixel program outside the census: "
				+ (pp.refusals.empty() ? std::string( "not a pixel program" ) : pp.refusals[0]) );
		// what the vertex program wrote (P3: an unwritten output has no value)
		unsigned colours = 0x3, coordinates = 0xFF;
		if (s.vertexProgram != NULL)
		{
			colours = coordinates = 0;
			for (size_t i = 0; i < s.vertexProgram->code.size(); ++i)
			{
				const Operand &d = s.vertexProgram->code[i].dst;
				if (d.type == Token::REG_ATTROUT) colours |= 1u << d.index;
				if (d.type == Token::REG_TEXCRDOUT) coordinates |= 1u << d.index;
			}
		}
		for (size_t i = 0; i < pp.code.size(); ++i)
		{
			const Instruction &in = pp.code[i];
			if (in.opcode == Token::OP_TEX || in.opcode == Token::OP_TEXBEM)
			{
				const int st = in.dst.index;
				if (s.textures[st] == NULL) refuse( report, stageText( st, "a pixel program samples a stage with no texture" ) );
				else validateSampling( s, st, report );
				if (!(coordinates & (1u << st))) refuse( report, stageText( st, "sampled at coordinates the vertex program never wrote" ) );
			}
			for (int k = 0; k < in.sources; ++k)
				if (in.src[k].type == Token::REG_INPUT && !(colours & (1u << in.src[k].index)))
					refuse( report, "a pixel program reads a colour the vertex program never wrote" );
		}
	}
}

/// Everything draw() refuses, checked before anything is drawn
void validate( const DrawState &s, int primitiveType, Report &report )
{
	const uint32_t *rs = s.renderState;
	validatePrograms( s, report );
	if (primitiveType < PT_TRIANGLELIST || primitiveType > PT_TRIANGLEFAN) refuse( report, "points and lines are not drawn" );
	if (rs[RS_FILLMODE] != FILL_SOLID) refuse( report, "FILLMODE other than solid" );
	if (rs[RS_SHADEMODE] != SHADE_FLAT && rs[RS_SHADEMODE] != SHADE_GOURAUD) refuse( report, "SHADEMODE other than flat or Gouraud" );
	if (rs[RS_VERTEXBLEND] != 0) refuse( report, "VERTEXBLEND" );
	if (rs[RS_CLIPPLANEENABLE] != 0) refuse( report, "user clip planes" );
	if (rs[RS_ZENABLE] == ZB_USEW) refuse( report, "w-buffering" );
	if (rs[RS_SRGBWRITEENABLE] != 0) refuse( report, "sRGB writes" );
	for (int i = 0; i < 8; ++i)
		if (rs[RS_WRAP0 + i] != 0 || rs[RS_WRAP8 + i] != 0)
			refuse( report, "D3DRS_WRAPn cylindrical wrapping" );
	for (int l = 0; l < MAX_LIGHTS; ++l)
		if (rs[RS_LIGHTING] && !s.pretransformed && s.lights[l].enabled
				&& (s.lights[l].type < LIGHT_POINT || s.lights[l].type > LIGHT_DIRECTIONAL))
			refuse( report, "a light of no documented type" );

	for (int st = 0; st < MAX_STAGES && s.pixelProgram == NULL; ++st)
	{
		const uint32_t *ts = s.stageState[st];
		const int cop = (int)ts[TSS_COLOROP], aop = (int)ts[TSS_ALPHAOP];
		if (cop == TOP_DISABLE)
			break;
		const Texture *tex = s.textures[st];
		if (tex == NULL && (ts[TSS_COLORARG1] & TA_SELECTMASK) == TA_TEXTURE)
			break;
		if (cop < TOP_SELECTARG1 || cop > TOP_LERP) refuse( report, stageText( st, "COLOROP of no documented value" ) );
		if (aop == TOP_DISABLE)
			;		// N29: undefined, drawn with its alpha unconstrained
		else if (aop < TOP_SELECTARG1 || aop > TOP_LERP) refuse( report, stageText( st, "ALPHAOP of no documented value" ) );
		else if (aop >= TOP_MODULATEALPHA_ADDCOLOR && aop <= TOP_BUMPENVMAPLUMINANCE)
			refuse( report, stageText( st, "a colour-only operation as ALPHAOP" ) );
		const uint32_t resultArg = ts[TSS_RESULTARG] & TA_SELECTMASK;
		if (resultArg != TA_CURRENT && resultArg != TA_TEMP) refuse( report, stageText( st, "RESULTARG other than CURRENT or TEMP" ) );
		if (cop == TOP_BUMPENVMAP || cop == TOP_BUMPENVMAPLUMINANCE)
		{
			if (tex == NULL) refuse( report, stageText( st, "bump mapping without a bump texture" ) );
		}
		const int colourArgs[3] = { TSS_COLORARG0, TSS_COLORARG1, TSS_COLORARG2 };
		const int alphaArgs[3] = { TSS_ALPHAARG0, TSS_ALPHAARG1, TSS_ALPHAARG2 };
		for (int k = 0; k < 3; ++k)
		{
			const int which = k;	// ARG0, ARG1, ARG2
			if (tex == NULL && usesArg( cop, which ) && (ts[colourArgs[k]] & TA_SELECTMASK) == TA_TEXTURE)
				refuse( report, stageText( st, "a colour argument reads D3DTA_TEXTURE with no texture bound" ) );
			if (tex == NULL && usesArg( aop, which ) && (ts[alphaArgs[k]] & TA_SELECTMASK) == TA_TEXTURE)
				refuse( report, stageText( st, "an alpha argument reads D3DTA_TEXTURE with no texture bound" ) );
			if ((ts[colourArgs[k]] & TA_SELECTMASK) > TA_CONSTANT || (ts[alphaArgs[k]] & TA_SELECTMASK) > TA_CONSTANT)
				refuse( report, stageText( st, "an argument of no documented value" ) );
		}
		if (tex == NULL && (cop == TOP_BLENDTEXTUREALPHA || cop == TOP_BLENDTEXTUREALPHAPM
				|| aop == TOP_BLENDTEXTUREALPHA || aop == TOP_BLENDTEXTUREALPHAPM))
			refuse( report, stageText( st, "a texture-alpha blend with no texture bound" ) );

		validateSampling( s, st, report );
	}
}

/// A stage's texture, sampler and coordinate states (the fixed-function cascade's and a pixel program's)
void validateSampling( const DrawState &s, int st, Report &report )
{
	const uint32_t *ts = s.stageState[st];
	const Texture *tex = s.textures[st];
	{
		const uint32_t tci = ts[TSS_TEXCOORDINDEX], gen = tci & 0xFFFF0000u;
		if (tex != NULL)
		{
			if (tex->type != TEXTURE_2D) refuse( report, stageText( st, "a cube or volume texture" ) );
			if (tex->levels.empty()) refuse( report, stageText( st, "a texture with no levels" ) );
			const uint32_t *sp = s.samplerState[st];
			if (sp[SAMP_SRGBTEXTURE]) refuse( report, stageText( st, "sRGB texture reads" ) );
			for (int f = SAMP_MAGFILTER; f <= SAMP_MINFILTER; ++f)
				if (!(sp[f] == TEXF_POINT || sp[f] == TEXF_LINEAR || (sp[f] == TEXF_ANISOTROPIC && sp[SAMP_MAXANISOTROPY] <= 1)))
					refuse( report, stageText( st, "a MAG/MIN filter other than point, linear, or anisotropic at 1" ) );
			if (sp[SAMP_MIPFILTER] > TEXF_LINEAR) refuse( report, stageText( st, "a MIPFILTER other than none, point, linear" ) );
			for (int a = SAMP_ADDRESSU; a <= SAMP_ADDRESSV; ++a)
				if (sp[a] < TADDRESS_WRAP || sp[a] > TADDRESS_MIRRORONCE)
					refuse( report, stageText( st, "an address mode of no documented value" ) );
		}
		if (gen > TSS_TCI_CAMERASPACEREFLECTIONVECTOR) refuse( report, stageText( st, "TCI SPHEREMAP or undocumented generation" ) );
		if (s.pretransformed && gen != 0) refuse( report, stageText( st, "texture generation on pretransformed vertices" ) );
		if (s.pretransformed && ts[TSS_TEXTURETRANSFORMFLAGS] != 0) refuse( report, stageText( st, "a texture transform on pretransformed vertices" ) );
		const uint32_t ttff = ts[TSS_TEXTURETRANSFORMFLAGS];
		if ((ttff & ~(uint32_t)(0xFF | TTFF_PROJECTED)) != 0 || (ttff & 0xFF) > 4)
			refuse( report, stageText( st, "TEXTURETRANSFORMFLAGS of no documented value" ) );
	}
}

}  // namespace

// ---- public ----------------------------------------------------------------------------------------
Color colorFromD3D( uint32_t argb )
{
	Color c = { ((argb >> 16) & 0xFF) / 255.0, ((argb >> 8) & 0xFF) / 255.0, (argb & 0xFF) / 255.0, ((argb >> 24) & 0xFF) / 255.0 };
	return c;
}

uint32_t floatBits( float f )
{
	uint32_t u;
	memcpy( &u, &f, 4 );
	return u;
}

Matrix Matrix::identity()
{
	Matrix m;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			m.m[i][j] = i == j ? 1.0 : 0.0;
	return m;
}

void DrawState::setDefaults( int width, int height )
{
	memset( this, 0, sizeof( *this ) );
	uint32_t *rs = renderState;
	rs[RS_ZENABLE] = ZB_TRUE;
	rs[RS_FILLMODE] = FILL_SOLID;
	rs[RS_SHADEMODE] = SHADE_GOURAUD;
	rs[RS_ZWRITEENABLE] = 1;
	rs[RS_LASTPIXEL] = 1;
	rs[RS_SRCBLEND] = BLEND_ONE;
	rs[RS_DESTBLEND] = BLEND_ZERO;
	rs[RS_CULLMODE] = CULL_CCW;
	rs[RS_ZFUNC] = CMP_LESSEQUAL;
	rs[RS_ALPHAFUNC] = CMP_ALWAYS;
	rs[RS_FOGEND] = floatBits( 1.0f );
	rs[RS_FOGDENSITY] = floatBits( 1.0f );
	rs[RS_STENCILFAIL] = rs[RS_STENCILZFAIL] = rs[RS_STENCILPASS] = STENCILOP_KEEP;
	rs[RS_STENCILFUNC] = CMP_ALWAYS;
	rs[RS_STENCILMASK] = rs[RS_STENCILWRITEMASK] = 0xFFFFFFFFu;
	rs[RS_TEXTUREFACTOR] = 0xFFFFFFFFu;
	rs[RS_CLIPPING] = 1;
	rs[RS_LIGHTING] = 1;
	rs[RS_COLORVERTEX] = 1;
	rs[RS_LOCALVIEWER] = 1;
	rs[RS_DIFFUSEMATERIALSOURCE] = MCS_COLOR1;
	rs[RS_SPECULARMATERIALSOURCE] = MCS_COLOR2;
	rs[RS_COLORWRITEENABLE] = 0xF;
	rs[RS_BLENDOP] = BLENDOP_ADD;
	rs[RS_CCW_STENCILFAIL] = rs[RS_CCW_STENCILZFAIL] = rs[RS_CCW_STENCILPASS] = STENCILOP_KEEP;
	rs[RS_CCW_STENCILFUNC] = CMP_ALWAYS;
	rs[RS_BLENDFACTOR] = 0xFFFFFFFFu;
	rs[RS_SRCBLENDALPHA] = BLEND_ONE;
	rs[RS_DESTBLENDALPHA] = BLEND_ZERO;
	rs[RS_BLENDOPALPHA] = BLENDOP_ADD;
	for (int st = 0; st < MAX_STAGES; ++st)
	{
		uint32_t *ts = stageState[st];
		ts[TSS_COLOROP] = st == 0 ? TOP_MODULATE : TOP_DISABLE;
		ts[TSS_COLORARG1] = TA_TEXTURE;
		ts[TSS_COLORARG2] = TA_CURRENT;
		ts[TSS_ALPHAOP] = st == 0 ? TOP_SELECTARG1 : TOP_DISABLE;
		ts[TSS_ALPHAARG1] = TA_TEXTURE;
		ts[TSS_ALPHAARG2] = TA_CURRENT;
		ts[TSS_TEXCOORDINDEX] = (uint32_t)st;
		ts[TSS_COLORARG0] = ts[TSS_ALPHAARG0] = ts[TSS_RESULTARG] = TA_CURRENT;
		ts[TSS_CONSTANT] = 0xFFFFFFFFu;
		uint32_t *sp = samplerState[st];
		sp[SAMP_ADDRESSU] = sp[SAMP_ADDRESSV] = sp[SAMP_ADDRESSW] = TADDRESS_WRAP;
		sp[SAMP_MAGFILTER] = sp[SAMP_MINFILTER] = TEXF_POINT;
		sp[SAMP_MIPFILTER] = TEXF_NONE;
		sp[SAMP_MAXANISOTROPY] = 1;
		textureTransform[st] = Matrix::identity();
		texCoordSize[st] = 2;
	}
	world = view = projection = Matrix::identity();
	const Color white = { 1, 1, 1, 1 };
	material.diffuse = white;
	viewport.width = width;
	viewport.height = height;
	viewport.maxZ = 1.0;
	scissor.right = width;
	scissor.bottom = height;
}

void Target::create( int w, int h, bool alpha, int bits )
{
	width = w;
	height = h;
	hasAlpha = alpha;
	colorBits = 8;
	stencilBits = bits;
	const Color black = { 0, 0, 0, 0 };
	color.assign( (size_t)w * h, black );
	lo = hi = color;
	depth.assign( (size_t)w * h, 1.0 );
	stencil.assign( (size_t)w * h, 0 );
	zones.assign( (size_t)w * h, 0 );
	recordDetail = false;
	detail.clear();
	depthAmbiguous.assign( (size_t)w * h, 0 );
	stencilAmbiguous.assign( (size_t)w * h, 0 );
}

void Target::clear( Color c, double z, uint32_t s )
{
	if (!hasAlpha)
		c.a = 1.0;
	c = quantiseColor( c, colorBits, 0 );
	std::fill( color.begin(), color.end(), c );
	std::fill( lo.begin(), lo.end(), c );
	std::fill( hi.begin(), hi.end(), c );
	std::fill( depth.begin(), depth.end(), z );
	std::fill( stencil.begin(), stencil.end(), s );
	std::fill( zones.begin(), zones.end(), 0u );
	std::fill( depthAmbiguous.begin(), depthAmbiguous.end(), 0 );
	std::fill( stencilAmbiguous.begin(), stencilAmbiguous.end(), 0 );
	detail.clear();		// sized again, empty, by the next draw that records
}

Freedoms::Freedoms()
	: edgePixels( 1.0 / 256.0 ), pointTieTexels( 1.0 / 512.0 ), bilinearTexels( 1.0 / 128.0 ),
	  lodDelta( 0.6 ), alphaRef( 1.5 / 255.0 ), depthTie( 1e-6 )
{
}

Report::Report()
	: trianglesIn( 0 ), trianglesCulled( 0 ), trianglesClippedAway( 0 ), pixelsCovered( 0 ),
	  pixelsAmbiguous( 0 ), pixelsWritten( 0 ), zonesSeen( 0 ), minLod( 1e30 ), maxLod( -1e30 ), programVerticesReported( 0 )
{
}

/// "Mathematics of Lighting", "Ambient/Diffuse/Specular/Emissive Lighting", "Attenuation and
/// Spotlight Factor", "Camera-Space Transformations", D3DLIGHT9, D3DRS_*MATERIALSOURCE; N1-N7
LitVertex light( const DrawState &s, const double P[3], const double N[3], bool hasNormal,
		const Color &vertexDiffuse, const Color &vertexSpecular, unsigned mutations )
{
	const uint32_t *rs = s.renderState;
	const bool colourVertex = rs[RS_COLORVERTEX] != 0;
	struct Source
	{
		static Color pick( uint32_t which, bool colourVertex, const DrawState &s, const Color &vd,
				const Color &vs, const Color &material )
		{
			if (!colourVertex) return material;
			if (which == MCS_COLOR1) return s.hasDiffuse ? vd : material;
			if (which == MCS_COLOR2) return s.hasSpecular ? vs : material;
			return material;
		}
	};
	const Material &m = s.material;
	const Color Cd = Source::pick( rs[RS_DIFFUSEMATERIALSOURCE], colourVertex, s, vertexDiffuse, vertexSpecular, m.diffuse );
	const Color Cs = Source::pick( rs[RS_SPECULARMATERIALSOURCE], colourVertex, s, vertexDiffuse, vertexSpecular, m.specular );
	const Color Ca = Source::pick( rs[RS_AMBIENTMATERIALSOURCE], colourVertex, s, vertexDiffuse, vertexSpecular, m.ambient );
	const Color Ce = Source::pick( rs[RS_EMISSIVEMATERIALSOURCE], colourVertex, s, vertexDiffuse, vertexSpecular, m.emissive );
	const Color Ga = colorFromD3D( rs[RS_AMBIENT] );

	double ambient[3] = { 0, 0, 0 }, diffuse[3] = { 0, 0, 0 }, specular[3] = { 0, 0, 0 };
	for (int l = 0; l < MAX_LIGHTS; ++l)
	{
		const Light &L = s.lights[l];
		if (!L.enabled)
			continue;
		double ldir[3];
		double atten = 1.0, spot = 1.0;
		if (L.type == LIGHT_DIRECTIONAL)
		{
			transformDirection( L.direction, s.view, ldir );		// N1
			normalise3( ldir );
			ldir[0] = -ldir[0]; ldir[1] = -ldir[1]; ldir[2] = -ldir[2];
		}
		else
		{
			const double lp4[4] = { L.position[0], L.position[1], L.position[2], 1.0 };
			double lp[4];
			transform4( lp4, s.view, lp );
			ldir[0] = lp[0] - P[0]; ldir[1] = lp[1] - P[1]; ldir[2] = lp[2] - P[2];	// N1
			const double d = sqrt( dot3( ldir, ldir ) );
			normalise3( ldir );
			if (d > L.range)
				continue;		// Atten = 0 beyond the range
			if (!(mutations & MUTATE_NO_ATTENUATION))
				atten = 1.0 / (L.attenuation0 + L.attenuation1 * d + L.attenuation2 * d * d);
			if (L.type == LIGHT_SPOT)
			{
				double dcs[3];
				transformDirection( L.direction, s.view, dcs );
				normalise3( dcs );
				const double rho = -dot3( dcs, ldir );		// norm(Ldcs) . norm(V - Lp), N2
				const double cosTheta = cos( L.theta * 0.5 ), cosPhi = cos( L.phi * 0.5 );
				if (rho > cosTheta)
					spot = 1.0;
				else if (rho <= cosPhi)
					spot = 0.0;
				else
					spot = pow( (rho - cosPhi) / (cosTheta - cosPhi), L.falloff );
			}
		}
		const double k = atten * spot;
		ambient[0] += k * L.ambient.r; ambient[1] += k * L.ambient.g; ambient[2] += k * L.ambient.b;	// N4
		const double nl = hasNormal ? std::max( 0.0, dot3( N, ldir ) ) : 0.0;		// N3
		diffuse[0] += L.diffuse.r * nl * k; diffuse[1] += L.diffuse.g * nl * k; diffuse[2] += L.diffuse.b * nl * k;
		double h[3];
		if (rs[RS_LOCALVIEWER])
		{
			double e[3] = { -P[0], -P[1], -P[2] };
			normalise3( e );
			h[0] = e[0] + ldir[0]; h[1] = e[1] + ldir[1]; h[2] = e[2] + ldir[2];
		}
		else
		{
			h[0] = ldir[0]; h[1] = ldir[1]; h[2] = ldir[2] - 1.0;		// N27: the viewer at (0,0,-1)
		}
		normalise3( h );
		const double nh = hasNormal ? std::max( 0.0, dot3( N, h ) ) : 0.0;
		const double sp = rs[RS_SPECULARENABLE] && nl > 0.0 ? pow( nh, m.power ) * k : 0.0;		// N6, N3's gate
		specular[0] += L.specular.r * sp; specular[1] += L.specular.g * sp; specular[2] += L.specular.b * sp;
	}
	LitVertex out;
	out.diffuse.r = clamp01( Ca.r * (Ga.r + ambient[0]) + Cd.r * diffuse[0] + Ce.r );
	out.diffuse.g = clamp01( Ca.g * (Ga.g + ambient[1]) + Cd.g * diffuse[1] + Ce.g );
	out.diffuse.b = clamp01( Ca.b * (Ga.b + ambient[2]) + Cd.b * diffuse[2] + Ce.b );
	out.diffuse.a = clamp01( Cd.a );			// N5
	out.specular.r = clamp01( Cs.r * specular[0] );
	out.specular.g = clamp01( Cs.g * specular[1] );
	out.specular.b = clamp01( Cs.b * specular[2] );
	out.specular.a = clamp01( Cs.a );
	return out;
}

/// "Fog Formulas"; N2, N8
double fogFactor( int mode, double d, double start, double end, double density )
{
	switch (mode)
	{
		case FOG_LINEAR:
			if (end == start)
				return d <= start ? 1.0 : 0.0;
			return clamp01( (end - d) / (end - start) );
		case FOG_EXP:
			return clamp01( exp( -d * density ) );
		case FOG_EXP2:
			return clamp01( exp( -(d * density) * (d * density) ) );
	}
	return 1.0;
}

/// D3DTEXTUREOP; N17, N18.  component 0-2 colour, 3 alpha.
double textureOp( int op, int component, const Color &arg0, const Color &arg1, const Color &arg2,
		double blendAlpha, unsigned mutations )
{
	const double x0 = channel( arg0, component ), x1 = channel( arg1, component ), x2 = channel( arg2, component );
	switch (op)
	{
		case TOP_SELECTARG1: return x1;
		case TOP_SELECTARG2: return x2;
		case TOP_MODULATE: return x1 * x2;
		case TOP_MODULATE2X: return 2.0 * x1 * x2;
		case TOP_MODULATE4X: return 4.0 * x1 * x2;
		case TOP_ADD: return x1 + x2;
		case TOP_ADDSIGNED: return x1 + x2 - 0.5;
		case TOP_ADDSIGNED2X: return 2.0 * (x1 + x2 - 0.5);
		case TOP_SUBTRACT: return x1 - x2;
		case TOP_ADDSMOOTH: return x1 + x2 * (1.0 - x1);
		case TOP_BLENDDIFFUSEALPHA: case TOP_BLENDTEXTUREALPHA: case TOP_BLENDFACTORALPHA: case TOP_BLENDCURRENTALPHA:
			return x1 * blendAlpha + x2 * (1.0 - blendAlpha);
		case TOP_BLENDTEXTUREALPHAPM: return x1 + x2 * (1.0 - blendAlpha);
		case TOP_PREMODULATE: return x1;
		case TOP_MODULATEALPHA_ADDCOLOR: return x1 + arg1.a * x2;
		case TOP_MODULATECOLOR_ADDALPHA: return x1 * x2 + arg1.a;
		case TOP_MODULATEINVALPHA_ADDCOLOR: return (1.0 - arg1.a) * x2 + x1;
		case TOP_MODULATEINVCOLOR_ADDALPHA: return (1.0 - x1) * x2 + arg1.a;
		case TOP_DOTPRODUCT3:
			return 4.0 * ((arg1.r - 0.5) * (arg2.r - 0.5) + (arg1.g - 0.5) * (arg2.g - 0.5) + (arg1.b - 0.5) * (arg2.b - 0.5));
		case TOP_MULTIPLYADD: return x0 + x1 * x2;
		case TOP_LERP:
			return (mutations & MUTATE_LERP_SWAPPED) ? x0 * x2 + (1.0 - x0) * x1 : x0 * x1 + (1.0 - x0) * x2;
	}
	return 0.0;
}

/// D3DCMPFUNC: "accept the new pixel if its value is [func] the current value"
bool compareFunc( int func, double incoming, double reference )
{
	switch (func)
	{
		case CMP_NEVER: return false;
		case CMP_LESS: return incoming < reference;
		case CMP_EQUAL: return incoming == reference;
		case CMP_LESSEQUAL: return incoming <= reference;
		case CMP_GREATER: return incoming > reference;
		case CMP_NOTEQUAL: return incoming != reference;
		case CMP_GREATEREQUAL: return incoming >= reference;
		case CMP_ALWAYS: return true;
	}
	return false;
}

/// D3DBLEND, D3DBLENDOP, "Alpha Blending State", D3DRS_SEPARATEALPHABLENDENABLE; N23
Color blend( const DrawState &s, const Color &src, const Color &dst0, bool dstHasAlpha, unsigned mutations )
{
	const uint32_t *rs = s.renderState;
	if (!rs[RS_ALPHABLENDENABLE])
		return src;
	Color dst = dst0;
	if (!dstHasAlpha)
		dst.a = 1.0;
	const Color bf = colorFromD3D( rs[RS_BLENDFACTOR] );
	int sb = (int)rs[RS_SRCBLEND], db = (int)rs[RS_DESTBLEND], op = (int)rs[RS_BLENDOP];
	int sba = sb, dba = db, opa = op;
	if (rs[RS_SEPARATEALPHABLENDENABLE])
	{
		sba = (int)rs[RS_SRCBLENDALPHA]; dba = (int)rs[RS_DESTBLENDALPHA]; opa = (int)rs[RS_BLENDOPALPHA];
	}
	struct Both
	{
		static void resolve( int &sf, int &df )
		{
			if (sf == BLEND_BOTHSRCALPHA) { sf = BLEND_SRCALPHA; df = BLEND_INVSRCALPHA; }
			else if (sf == BLEND_BOTHINVSRCALPHA) { sf = BLEND_INVSRCALPHA; df = BLEND_SRCALPHA; }
		}
		static double factor( int f, int ch, const Color &src, const Color &dst, const Color &bf )
		{
			switch (f)
			{
				case BLEND_ZERO: return 0.0;
				case BLEND_ONE: return 1.0;
				case BLEND_SRCCOLOR: return channel( src, ch );
				case BLEND_INVSRCCOLOR: return 1.0 - channel( src, ch );
				case BLEND_SRCALPHA: return src.a;
				case BLEND_INVSRCALPHA: return 1.0 - src.a;
				case BLEND_DESTALPHA: return dst.a;
				case BLEND_INVDESTALPHA: return 1.0 - dst.a;
				case BLEND_DESTCOLOR: return channel( dst, ch );
				case BLEND_INVDESTCOLOR: return 1.0 - channel( dst, ch );
				case BLEND_SRCALPHASAT: return ch < 3 ? std::min( src.a, 1.0 - dst.a ) : 1.0;
				case BLEND_BLENDFACTOR: return channel( bf, ch );
				case BLEND_INVBLENDFACTOR: return 1.0 - channel( bf, ch );
			}
			return 0.0;
		}
		static double combine( int op, double s, double fs, double d, double fd )
		{
			switch (op)
			{
				case BLENDOP_SUBTRACT: return s * fs - d * fd;
				case BLENDOP_REVSUBTRACT: return d * fd - s * fs;
				case BLENDOP_MIN: return std::min( s, d );
				case BLENDOP_MAX: return std::max( s, d );
			}
			return s * fs + d * fd;
		}
	};
	Both::resolve( sb, db );
	Both::resolve( sba, dba );
	if (mutations & MUTATE_BLEND_SWAPPED)
	{
		std::swap( sb, db );
		std::swap( sba, dba );
	}
	Color out;
	for (int ch = 0; ch < 4; ++ch)
	{
		const int fs = ch < 3 ? sb : sba, fd = ch < 3 ? db : dba, o = ch < 3 ? op : opa;
		setChannel( out, ch, clamp01( Both::combine( o, channel( src, ch ), Both::factor( fs, ch, src, dst, bf ),
				channel( dst, ch ), Both::factor( fd, ch, src, dst, bf ) ) ) );
	}
	return out;
}

Color sample( const Texture &texture, const uint32_t sampler[14], double u, double v, double lambda, unsigned mutations )
{
	return sampleImpl( texture, sampler, u, v, lambda, 0.0, 0.0, Freedoms(), mutations );
}

bool draw( const DrawState &state, int primitiveType, const Vertex *vertices, int vertexCount,
		const uint32_t *indices, int count, Target &target, Report *reportOut, unsigned mutations,
		const Freedoms &freedoms )
{
	Report local;
	Report &report = reportOut != NULL ? *reportOut : local;
	validate( state, primitiveType, report );
	for (int i = 0; indices != NULL && i < count; ++i)
		if ((int)indices[i] >= vertexCount)
		{
			refuse( report, "an index past the vertices" );
			break;
		}
	if (indices == NULL && count > vertexCount)
		refuse( report, "a count past the vertices" );
	if (!report.refusals.empty())
		return false;

	Context ctx( state, mutations, freedoms );
	const uint32_t *rs = state.renderState;
	std::vector<VOut> processed( (size_t)vertexCount );
	for (int i = 0; i < vertexCount; ++i)
	{
		processed[i] = processVertex( ctx, vertices[i] );
		report.programVerticesReported += processed[i].programReported ? 1 : 0;
	}

	Raster r = { ctx, target, report, 0, 0, target.width, target.height, -1 };
	if (target.recordDetail && target.detail.size() != target.color.size())
	{
		PixelDetail none;
		memset( &none, 0, sizeof( none ) );
		none.primitive = -1;
		target.detail.assign( target.color.size(), none );
	}
	const Viewport &vp = state.viewport;
	r.x0 = std::max( r.x0, (int)ceil( vp.x ) );
	r.y0 = std::max( r.y0, (int)ceil( vp.y ) );
	r.x1 = std::min( r.x1, (int)ceil( vp.x + vp.width ) );
	r.y1 = std::min( r.y1, (int)ceil( vp.y + vp.height ) );
	if (rs[RS_SCISSORTESTENABLE])
	{
		r.x0 = std::max( r.x0, state.scissor.left ); r.y0 = std::max( r.y0, state.scissor.top );
		r.x1 = std::min( r.x1, state.scissor.right ); r.y1 = std::min( r.y1, state.scissor.bottom );
	}

	const bool flat = rs[RS_SHADEMODE] == SHADE_FLAT;
	const int triangles = primitiveType == PT_TRIANGLELIST ? count / 3 : count - 2;
	for (int t = 0; t < triangles; ++t)
	{
		int i0, i1, i2, provoking;
		if (primitiveType == PT_TRIANGLELIST) { i0 = 3 * t; i1 = 3 * t + 1; i2 = 3 * t + 2; provoking = i0; }
		else if (primitiveType == PT_TRIANGLESTRIP)
		{
			provoking = t;
			if (t & 1) { i0 = t + 1; i1 = t; i2 = t + 2; }	// N12
			else { i0 = t; i1 = t + 1; i2 = t + 2; }
		}
		else { i0 = 0; i1 = t + 1; i2 = t + 2; provoking = t + 1; }
		const uint32_t n[4] = {
			indices ? indices[i0] : (uint32_t)i0, indices ? indices[i1] : (uint32_t)i1,
			indices ? indices[i2] : (uint32_t)i2, indices ? indices[provoking] : (uint32_t)provoking };
		VOut a = processed[n[0]], b = processed[n[1]], c = processed[n[2]];
		if (flat)
		{
			const VOut &p = processed[n[3]];		// N11
			a.diffuse = b.diffuse = c.diffuse = p.diffuse;
			a.specular = b.specular = c.specular = p.specular;
		}
		r.primitive = t;
		drawTriangle( r, a, b, c );
	}
	return true;
}

Comparison compare( const Target &reference, const uint8_t *gpu, int rowBytes, double base )
{
	Comparison c;
	memset( &c, 0, sizeof( c ) );
	c.worstX = c.worstY = -1;
	const int channels = reference.hasAlpha ? 4 : 3;
	for (int y = 0; y < reference.height; ++y)
		for (int x = 0; x < reference.width; ++x)
		{
			const size_t i = (size_t)y * reference.width + x;
			const uint8_t *g = gpu + (size_t)y * rowBytes + (size_t)x * 4;
			double maxNominal = 0.0, outside = 0.0;
			for (int ch = 0; ch < channels; ++ch)
			{
				const double v = g[ch] / 255.0;
				maxNominal = std::max( maxNominal, fabs( v - channel( reference.color[i], ch ) ) );
				outside = std::max( outside, channel( reference.lo[i], ch ) - base - v );
				outside = std::max( outside, v - channel( reference.hi[i], ch ) - base );
			}
			++c.pixels;
			const int bin = (int)floor( maxNominal * 255.0 + 0.5 );
			++c.histogram[bin > 255 ? 255 : bin];
			if (maxNominal <= base + 1e-9)
				++c.exact;
			else if (outside <= 1e-9)
			{
				++c.inFreedom;
				for (int z = 0; z < 8; ++z)
					if (reference.zones[i] & (1u << z))
						++c.zoneCounts[z];
			}
			else
			{
				++c.outside;
				if (outside > c.worst)
				{
					c.worst = outside;
					c.worstX = x;
					c.worstY = y;
				}
			}
		}
	return c;
}

void print( const Comparison &c, FILE *out, const char *label )
{
	static const char *zoneNames[8] = { "edge", "texel", "lod", "alpha-test", "depth", "stencil", "undefined", "program" };
	fprintf( out, "  %s: %ld pixels, %ld exact, %ld in a documented freedom, %ld outside", label, c.pixels,
			c.exact, c.inFreedom, c.outside );
	if (c.outside)
		fprintf( out, " (worst %.1f/255 past the envelope at %d,%d)", c.worst * 255.0, c.worstX, c.worstY );
	fprintf( out, "\n    freedoms:" );
	for (int z = 0; z < 8; ++z)
		fprintf( out, " %s %ld", zoneNames[z], c.zoneCounts[z] );
	fprintf( out, "\n    |gpu - nominal| in 1/255:" );
	for (int b = 0; b < 256; ++b)
		if (c.histogram[b])
			fprintf( out, " %d:%ld", b, c.histogram[b] );
	fprintf( out, "\n" );
}

}  // namespace FFRef
