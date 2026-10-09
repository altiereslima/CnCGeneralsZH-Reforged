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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// W3DParticleSys.cpp
// W3D Particle System implementation
// Author: Michael S. Booth, November 2001

#include "Common/GlobalData.h"
#include "Lib/Clock.h"
#include "GameClient/Color.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DSnow.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "WW3D2/camera.h"
#include "W3DDevice/GameClient/W3DSmoothMotion.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx11runtime.h"
#include "Common/JobSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/LifetimeUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/ObserverCamera.h"
#include <map>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------


#include "Common/QuickTrig.h"
#include "Platform/MsvcFloatCasts.h"

#ifdef DEBUG_LOGGING
extern Real TheParticleFillMS;
extern UnsignedInt TheParticlesPastGroupLimit;
#endif

/** The preset a particle shader type draws with, and whether it names one at all.  An invalid type
	* leaves the point group's previous shader in place on the old path, so it does not take the
	* direct one. */
static Bool particlePresetShader( ParticleSystemInfo::ParticleShaderType type, ShaderClass *shader )
{
	switch( type )
	{
		case ParticleSystemInfo::ADDITIVE:	*shader = ShaderClass::_PresetAdditiveSpriteShader;				return TRUE;
		case ParticleSystemInfo::ALPHA:			*shader = ShaderClass::_PresetAlphaSpriteShader;					return TRUE;
		case ParticleSystemInfo::ALPHA_TEST:	*shader = ShaderClass::_PresetATestSpriteShader;					return TRUE;
		case ParticleSystemInfo::MULTIPLY:	*shader = ShaderClass::_PresetMultiplicativeSpriteShader;	return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// Fire lights the smoke around it.  Each frame the additive systems (flames, explosions, muzzle
// flashes) are reduced to one light apiece at their brightness-weighted centre, the scene's enabled
// light pulses join them, and every alpha-blended particle near one of those is lit by it.  A fire's
// light is the colour of fire, not of its particles: an additive flame's vertex colour is near white
// and the orange is in its texture, so the hue taken from the vertices made the light grey.  It is
// strongest just over the flames and falls away with height above them and sideways from them, so a
// plume is lit at its base and goes grey as it rises; it flickers with the flames because the
// flames' own particles are what measure it.  The light a particle takes is reflected by its own
// colour and, past white, keeps its hue rather than clipping, so white smoke turns orange and soot
// dark orange.  All of it is client-side colour on the CPU, so both devices draw it.  It goes in
// before the shade, being vertex colour, so a plume's own shade dims it too (the shaders have no
// channel to carry it past the shade); a thin particle takes less of it, which keeps the plume's thin
// edge, unshaded, from glowing brighter than its shaded core.
//-------------------------------------------------------------------------------------------------

static const Int	SMOKE_LIGHTS_MAX							= 32;			///< lights gathered a frame, the strongest kept
static const Int	SMOKE_LIGHTS_PER_SYSTEM				= 4;			///< lights one smoke system evaluates per particle
static const Real	FIRE_LIGHT_FULL_WEIGHT				= 120.0f;	///< sum of brightness x size at which a fire is full strength
static const Real	FIRE_LIGHT_MIN_WEIGHT					= 4.0f;		///< below this an additive system lights nothing
static const Real	FIRE_LIGHT_RADIUS							= 35.0f;	///< sideways reach of a fire's light beyond its own spread
static const Real	FIRE_LIGHT_SPREAD_SCALE				= 2.0f;		///< times the flames' RMS spread, added to the reach
static const Real	FIRE_LIGHT_MAX_RADIUS					= 140.0f;
static const Real	FIRE_LIGHT_HEIGHT							= 90.0f;	///< height over the flames at which their light is gone
static const Real	FIRE_LIGHT_BELOW_SHARE				= 0.25f;	///< share of the sideways reach the light goes below them
static const Real	FIRE_LIGHT_GAIN								= 0.7f;		///< light a full-strength fire puts on the smoke over it
static const Real	PULSE_LIGHT_GAIN							= 0.4f;		///< same for an FX light pulse, whose colour is already full
static const Real	SMOKE_LIGHT_MAX_GLOW					= 0.9f;		///< most light any particle takes, brightest channel
static const Real	SMOKE_LIGHT_SOOT_SHARE				= 0.35f;	///< share of the light even black smoke gives back
static const Real	SMOKE_LIGHT_THIN_ALPHA				= 0.25f;	///< a particle thinner than this takes its share of the light
static const Real	FIRE_LIGHT_RED = 1.0f, FIRE_LIGHT_GREEN = 0.55f, FIRE_LIGHT_BLUE = 0.24f;	///< 255,140,60


/** A light as the smoke sees it: its colour is already scaled by its strength and gain.  A fire's
	* reaches its radius sideways, its height upwards and a share of the radius downwards; a pulse's
	* (invHeight zero) is a ball.  reachSq is the ball that holds either, for picking. */
struct SmokeLight
{
	Real x, y, z;
	Real radiusSq, invRadiusSq;
	Real invHeight, invBelow;
	Real reachSq;
	Real r, g, b;
	Real strength;	///< orders the list, strongest first
};

/** Keeps the list at SMOKE_LIGHTS_MAX by dropping the weakest. */
static void addSmokeLight( SmokeLight *lights, Int &count, const SmokeLight &light )
{
	Int slot = count;
	if (count == SMOKE_LIGHTS_MAX)
	{
		slot = 0;
		for (Int i = 1; i < count; ++i)
			if (lights[ i ].strength < lights[ slot ].strength)
				slot = i;
		if (lights[ slot ].strength >= light.strength)
			return;
	}
	else
		++count;
	lights[ slot ] = light;
}

/** An additive system's light before the view box has had its say.  It depends on the particles
	* alone, and they only move when ParticleSystemManager::update runs, once a logic frame, so the
	* walk over them is kept for every pass drawn until the next one. */
struct FireLight
{
	SmokeLight	light;
	Real				reach;	///< the light's reach in any one axis, for the view box test
};

static std::vector<FireLight>	s_fireLights;					///< in system list order, as the walk found them
static Bool										s_fireLightsValid = FALSE;
static UnsignedInt						s_fireLightsFrame;			///< the particle update they were taken after
static UnsignedInt						s_fireLightsParticles;
static UnsignedInt						s_fireLightsSystems;

/** One additive system as a point light, if it is bright enough. */
static void gatherFireLight( ParticleSystem *sys )
{
	Real w = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f, sq = 0.0f;
	for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
	{
		const RGBColor *c = p->getColor();
		const Real pw = (c->red + c->green + c->blue) * (1.0f / 3.0f) * p->getSize();
		if (pw <= 0.0f)
			continue;
		const Coord3D *pos = p->getPosition();
		w += pw;
		x += pw * pos->x;
		y += pw * pos->y;
		z += pw * pos->z;
		sq += pw * (pos->x * pos->x + pos->y * pos->y + pos->z * pos->z);
	}
	if (w < FIRE_LIGHT_MIN_WEIGHT)
		return;

	FireLight fire;
	SmokeLight &light = fire.light;
	const Real inv = 1.0f / w;
	light.x = x * inv;
	light.y = y * inv;
	light.z = z * inv;
	const Real variance = sq * inv - (light.x * light.x + light.y * light.y + light.z * light.z);
	Real radius = FIRE_LIGHT_RADIUS + FIRE_LIGHT_SPREAD_SCALE * (variance > 0.0f ? sqrtf( variance ) : 0.0f);
	if (radius > FIRE_LIGHT_MAX_RADIUS)
		radius = FIRE_LIGHT_MAX_RADIUS;

	const Real height = FIRE_LIGHT_HEIGHT;
	const Real reach = radius > height ? radius : height;
	fire.reach = reach;

	// fire's own colour, and the strength from how much of the flames there is
	light.strength = w < FIRE_LIGHT_FULL_WEIGHT ? w / FIRE_LIGHT_FULL_WEIGHT : 1.0f;
	const Real scale = light.strength * FIRE_LIGHT_GAIN;
	light.r = FIRE_LIGHT_RED * scale;
	light.g = FIRE_LIGHT_GREEN * scale;
	light.b = FIRE_LIGHT_BLUE * scale;
	light.radiusSq = radius * radius;
	light.invRadiusSq = 1.0f / light.radiusSq;
	light.invHeight = 1.0f / height;
	light.invBelow = 1.0f / (radius * FIRE_LIGHT_BELOW_SHARE);
	light.reachSq = reach * reach;
	s_fireLights.push_back( fire );
}

/** The scene's enabled light pulses, which FX lists put on explosions. */
static void gatherPulseLights( SmokeLight *lights, Int &count, const AABoxClass &view )
{
	if (W3DDisplay::m_3DScene == NULL)
		return;

	RefRenderObjListIterator it( W3DDisplay::m_3DScene->getDynamicLights() );
	for (it.First(); !it.Is_Done(); it.Next())
	{
		W3DDynamicLight *pulse = (W3DDynamicLight *)it.Peek_Obj();
		if (!pulse->isEnabled() || pulse->Get_Type() != LightClass::POINT)
			continue;

		Real nearRange, farRange;
		pulse->Get_Far_Attenuation_Range( nearRange, farRange );
		if (farRange < 1.0f)
			continue;

		const Vector3 pos = pulse->Get_Position();
		if (WWMath::Fabs( pos.X - view.Center.X ) > view.Extent.X + farRange
				|| WWMath::Fabs( pos.Y - view.Center.Y ) > view.Extent.Y + farRange
				|| WWMath::Fabs( pos.Z - view.Center.Z ) > view.Extent.Z + farRange)
			continue;

		Vector3 diffuse;
		pulse->Get_Diffuse( &diffuse );
		SmokeLight light;
		light.x = pos.X;
		light.y = pos.Y;
		light.z = pos.Z;
		light.radiusSq = farRange * farRange;
		light.invRadiusSq = 1.0f / light.radiusSq;
		light.invHeight = 0.0f;		// a ball
		light.invBelow = 0.0f;
		light.reachSq = light.radiusSq;
		light.r = diffuse.X * PULSE_LIGHT_GAIN;
		light.g = diffuse.Y * PULSE_LIGHT_GAIN;
		light.b = diffuse.Z * PULSE_LIGHT_GAIN;
		light.strength = WWMath::Max( diffuse.X, WWMath::Max( diffuse.Y, diffuse.Z ) );
		if (light.strength > 0.0f)
			addSmokeLight( lights, count, light );
	}
}

/** Fills lights[] with this frame's lights, strongest first, and returns how many.  The fires are
	* offered in the order the systems walk found them, so the weakest-out list keeps the same ones. */
static Int gatherSmokeLights( SmokeLight *lights, const AABoxClass &view )
{
	Int count = 0;
	for (size_t i = 0; i < s_fireLights.size(); ++i)
	{
		const FireLight &fire = s_fireLights[ i ];
		const SmokeLight &light = fire.light;
		if (WWMath::Fabs( light.x - view.Center.X ) > view.Extent.X + fire.reach
				|| WWMath::Fabs( light.y - view.Center.Y ) > view.Extent.Y + fire.reach
				|| WWMath::Fabs( light.z - view.Center.Z ) > view.Extent.Z + fire.reach)
			continue;
		addSmokeLight( lights, count, light );
	}
	gatherPulseLights( lights, count, view );

	for (Int i = 1; i < count; ++i)
	{
		const SmokeLight light = lights[ i ];
		Int j = i;
		for (; j > 0 && lights[ j - 1 ].strength < light.strength; --j)
			lights[ j ] = lights[ j - 1 ];
		lights[ j ] = light;
	}
	return count;
}

/** The strongest lights that reach an alpha system's particles, at most SMOKE_LIGHTS_PER_SYSTEM.
	* Any other system gets none. */
static Int pickSmokeLights( ParticleSystem *sys, const SmokeLight *lights, Int count, SmokeLight *picked )
{
	if (count == 0 || sys->getShaderType() != ParticleSystemInfo::ALPHA)
		return 0;

	Particle *p = sys->getFirstParticle();
	if (p == NULL)
		return 0;
	Real minX = p->getPosition()->x, maxX = minX;
	Real minY = p->getPosition()->y, maxY = minY;
	Real minZ = p->getPosition()->z, maxZ = minZ;
	for (p = p->m_systemNext; p; p = p->m_systemNext)
	{
		const Coord3D *pos = p->getPosition();
		minX = WWMath::Min( minX, pos->x );	maxX = WWMath::Max( maxX, pos->x );
		minY = WWMath::Min( minY, pos->y );	maxY = WWMath::Max( maxY, pos->y );
		minZ = WWMath::Min( minZ, pos->z );	maxZ = WWMath::Max( maxZ, pos->z );
	}

	Int n = 0;
	for (Int i = 0; i < count && n < SMOKE_LIGHTS_PER_SYSTEM; ++i)
	{
		const SmokeLight &l = lights[ i ];
		const Real dx = l.x < minX ? minX - l.x : (l.x > maxX ? l.x - maxX : 0.0f);
		const Real dy = l.y < minY ? minY - l.y : (l.y > maxY ? l.y - maxY : 0.0f);
		const Real dz = l.z < minZ ? minZ - l.z : (l.z > maxZ ? l.z - maxZ : 0.0f);
		if (dx * dx + dy * dy + dz * dz < l.reachSq)
			picked[ n++ ] = l;
	}
	return n;
}

/** A smoke particle's colour lit by the picked lights.  A fire's light falls as (1 - h^2/R^2)^2
	* sideways and (1 - dz/H)^2 above the flames, much faster below them; a pulse's as (1 - d^2/R^2)^2
	* all round.  The sum is held to SMOKE_LIGHT_MAX_GLOW in its brightest channel, scaled down by how
	* thin the particle is, and reflected by the particle's own colour (lifted by the soot share, so
	* black smoke still shows the fire); a result past white is scaled back whole, which keeps its
	* hue. */
static inline void lightSmoke( const Coord3D *pos, Real alpha, const SmokeLight *lights, Int count,
	Real &red, Real &green, Real &blue )
{
	Real r = 0.0f, g = 0.0f, b = 0.0f;
	for (Int i = 0; i < count; ++i)
	{
		const SmokeLight &l = lights[ i ];
		const Real dx = pos->x - l.x, dy = pos->y - l.y, dz = pos->z - l.z;
		Real f;
		if (l.invHeight > 0.0f)
		{
			const Real side = 1.0f - (dx * dx + dy * dy) * l.invRadiusSq;
			const Real up = (dz >= 0.0f) ? 1.0f - dz * l.invHeight : 1.0f + dz * l.invBelow;
			if (side <= 0.0f || up <= 0.0f)
				continue;
			f = side * side * up * up;
		}
		else
		{
			const Real t = 1.0f - (dx * dx + dy * dy + dz * dz) * l.invRadiusSq;
			if (t <= 0.0f)
				continue;
			f = t * t;
		}
		r += f * l.r;
		g += f * l.g;
		b += f * l.b;
	}

	Real peak = WWMath::Max( r, WWMath::Max( g, b ) );
	if (peak <= 0.0f)
		return;
	Real scale = (peak > SMOKE_LIGHT_MAX_GLOW) ? SMOKE_LIGHT_MAX_GLOW / peak : 1.0f;
	if (alpha < SMOKE_LIGHT_THIN_ALPHA)
		scale *= alpha / SMOKE_LIGHT_THIN_ALPHA;

	const Real soot = SMOKE_LIGHT_SOOT_SHARE;
	red += r * scale * (soot + (1.0f - soot) * red);
	green += g * scale * (soot + (1.0f - soot) * green);
	blue += b * scale * (soot + (1.0f - soot) * blue);
	peak = WWMath::Max( red, WWMath::Max( green, blue ) );
	if (peak > 1.0f)
	{
		red /= peak;
		green /= peak;
		blue /= peak;
	}
}

/** What every job of one frame's billboard fill reads. */
struct BillboardFillJob
{
	W3DParticleSystemManager::BillboardFill *	fills;
	Matrix4x4																	view;
	Real																			centerX, centerY, centerZ;
	Real																			extentX, extentY, extentZ;
	SmokeLight																lights[ SMOKE_LIGHTS_MAX ];	///< this frame's, strongest first
	Int																				lightCount;
	/// Direct3D 11 is drawing the picture: the glow goes into the normals, and its pixel program adds
	/// it after the sun's shadow and the smoke's own shade, which dimmed a fire seen through the far
	/// side of a plume when it was in the colour.  Direct3D 9 shades nothing and keeps it baked.
	Bool																			glowAfterShade;
};

/// systems a pool thread claims at once; a system is a few hundred particles, so one claim a system
/// would spend more on the interlocked claim than the work is worth
static const Int BILLBOARD_FILLS_PER_CLAIM = 4;

/** One sorted billboard system's quads, written into the range reserved for it.  Reads the system's
	* particles and writes only that range and that fill's counts: no allocation, no device calls,
	* nothing another job touches.  The cull and the 512-a-system cut are the ones the serial loop
	* below applies to every other system. */
static void fillBillboards( Int index, void *context )
{
	BillboardFillJob *job = (BillboardFillJob *)context;
	W3DParticleSystemManager::BillboardFill &fill = job->fills[ index ];
	VertexFormatXYZNDUV2 *quad = fill.range.Vertices;
	Int drawn = 0;
	SmokeLight lights[ SMOKE_LIGHTS_PER_SYSTEM ];
	const Int lightCount = pickSmokeLights( fill.system, job->lights, job->lightCount, lights );

	for (Particle *p = fill.system->getFirstParticle(); p; p = p->m_systemNext)
	{
		const Coord3D *pos = p->getPosition();
		const Real psize = p->getSize();

		//Cull particle to edges of screen and terrain.
		if (WWMath::Fabs(pos->x - job->centerX) > (job->extentX + psize))
			continue;

		if (WWMath::Fabs(pos->y - job->centerY) > (job->extentY + psize))
			continue;

		if (WWMath::Fabs(pos->z - job->centerZ) > (job->extentZ + psize))
			continue;

		const RGBColor *color = p->getColor();
		Real red = color->red, green = color->green, blue = color->blue;
		Vector3 glow( 0.0f, 0.0f, 0.0f );
		if (lightCount > 0)
		{
			lightSmoke( pos, p->getAlpha(), lights, lightCount, red, green, blue );
			if (job->glowAfterShade)
			{
				glow.Set( red - color->red, green - color->green, blue - color->blue );
				red = color->red;
				green = color->green;
				blue = color->blue;
			}
		}
		const unsigned packed = DX8Wrapper::Convert_Color_Clamp( Vector4( red, green, blue, p->getAlpha() ) );
		// The orientation table's index wraps, as it did on Windows (Platform/MsvcFloatCasts.h).
		const uint8 orientation = floatToByteAsMsvc( p->getAngle() * 255.0f / (2.0f * PI) );
		PointGroupClass::Write_Billboard( quad, job->view, Vector3( pos->x, pos->y, pos->z ), psize,
			orientation, packed, glow );
		quad += 4;

		if (++drawn == fill.capacity)
		{
			fill.pastLimit = fill.system->getParticleCount() - drawn;
			break;
		}
	}

	fill.drawn = drawn;
	fill.glow = job->glowAfterShade && lightCount > 0;
}

//-------------------------------------------------------------------------------------------------
// A contamination field - toxin, anthrax, radiation - is ground-aligned AREA_EFFECT particles:
// squares up to 170 units across, every one at the emitter's height, which UseCallersRadius spreads
// over the weapon's damage radius (140 for a large toxin field).  Drawn flat, on anything but level
// ground the uphill side of a square sank into the terrain and the downhill side hung in the air.
// Each puddle square is a grid here instead, every vertex set on the terrain under it, so the field
// lies on the hill.  A field whose emitter stands clear of the terrain, on a bridge deck, keeps its
// height.  Which squares a field draws is collectGroundFields' business, below.
//
// Before the glow, the same grid takes light off the ground under the bright part of the texture,
// mostly from the channels the field's hue lacks, so the ground under the field turns the field's
// colour and the pool reads as liquid and not as light laid on the ground.  The hue is the
// template's own, so anthrax keeps its colour.  Every field's stain goes down before any field's
// glow: drawn system by system, a later system's stain darkened the glow already laid by the ones
// before it, and the overlaps came out as black-green blotches through the pool.  Both passes are
// plain DX8Wrapper draws in world space and both devices draw them.
//-------------------------------------------------------------------------------------------------

static const Real	FIELD_GRID_SPACING		= 5.0f;		///< world units between grid vertices; a terrain cell is ten
static const Int	FIELD_GRID_MAX_STEPS	= 16;			///< per side; the largest squares get a coarser grid
static const Real	FIELD_LIFT						= 1.0f;		///< over the terrain, for the chords between vertices
static const Real	FIELD_ON_GROUND				= 10.0f;	///< an emitter higher than this over the terrain is on a deck
static const Real	FIELD_STAIN_DARKEN		= 0.05f;	///< share of the ground's light the stain takes in every channel
static const Real	FIELD_STAIN_TINT			= 0.25f;	///< further share taken from the channels the hue lacks
static const Int	FIELD_CHUNK_VERTICES	= 8192;		///< one draw's worth; a 16 step square is 289

/** Is this system a field to be laid on the terrain?  The same pair the field particle count uses.
	* A system attached to a drawable or an object is not: its position is an offset in that model's
	* space (the damaged oil tank's ToxinTankPuddle hangs off a bone), and it moves with it, so it keeps
	* the ordinary particle path. */
static Bool isGroundField( ParticleSystem *sys )
{
	return sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned
		&& !sys->isUsingStreak() && sys->getVolumeParticleDepth() <= 1
		&& sys->getAttachedDrawable() == INVALID_DRAWABLE_ID && sys->getAttachedObject() == INVALID_ID;
}

struct FieldChunk
{
	Int firstVertex, vertexCount;
	Int firstIndex, indexCount;
};

static std::vector<Vector3>					s_fieldPos;
static std::vector<Vector2>					s_fieldUV;
static std::vector<unsigned>				s_fieldGlow;
static std::vector<unsigned>				s_fieldStain;
static std::vector<unsigned short>	s_fieldIndex;
static std::vector<FieldChunk>			s_fieldChunks;

/** One field system's chunks and how it draws. */
struct FieldBatch
{
	TextureClass *texture;		///< a reference held until the frame's fields are drawn
	ShaderClass glow, stain;
	Bool stained;
	Int firstChunk, chunkCount;
};

static std::vector<FieldBatch>			s_fieldBatches;

/** Writes one pass of a chunk and draws it; the index buffer is already set. */
static void drawFieldPass( const FieldChunk &chunk, const std::vector<unsigned> &colors, const ShaderClass &shader )
{
	DynamicVBAccessClass vbAccess( BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, (unsigned short)chunk.vertexCount );
	{
		DynamicVBAccessClass::WriteLockClass lock( &vbAccess );
		VertexFormatXYZNDUV2 *vb = lock.Get_Formatted_Vertex_Array();
		for (Int i = 0; i < chunk.vertexCount; ++i)
		{
			const Int v = chunk.firstVertex + i;
			vb[i].x = s_fieldPos[ v ].X;
			vb[i].y = s_fieldPos[ v ].Y;
			vb[i].z = s_fieldPos[ v ].Z;
			vb[i].nx = 0.0f;
			vb[i].ny = 0.0f;
			vb[i].nz = 1.0f;
			vb[i].diffuse = colors[ v ];
			vb[i].u1 = s_fieldUV[ v ].X;
			vb[i].v1 = s_fieldUV[ v ].Y;
			vb[i].u2 = 0.0f;
			vb[i].v2 = 0.0f;
		}
	}
	DX8Wrapper::Set_Shader( shader );
	DX8Wrapper::Set_Vertex_Buffer( vbAccess );
	DX8Wrapper::Draw_Triangles( 0, (unsigned short)(chunk.indexCount / 3), 0, (unsigned short)chunk.vertexCount );
}

/** One puddle square laid on the terrain as a grid, into the chunk being filled. */
static void addFieldPuddle( FieldChunk &chunk, Real cx, Real cy, Real flatZ, Bool onGround, Real size, Real angle,
	unsigned glowColor, unsigned stainColor )
{
	Int steps = (Int)ceilf( 2.0f * size / FIELD_GRID_SPACING );
	steps = steps < 1 ? 1 : (steps > FIELD_GRID_MAX_STEPS ? FIELD_GRID_MAX_STEPS : steps);
	const Int side = steps + 1;
	const Int vertices = side * side;

	if (chunk.vertexCount + vertices > FIELD_CHUNK_VERTICES)
	{
		s_fieldChunks.push_back( chunk );
		chunk.firstVertex += chunk.vertexCount;
		chunk.firstIndex += chunk.indexCount;
		chunk.vertexCount = 0;
		chunk.indexCount = 0;
	}

	// the square's half axes: PointGroupClass laid a ground quad out to size on either side
	const Real c = WWMath::Cos( angle ) * size;
	const Real s = WWMath::Sin( angle ) * size;
	const Real step = 2.0f / steps;
	for (Int j = 0; j < side; ++j)
	{
		const Real b = -1.0f + j * step;
		for (Int i = 0; i < side; ++i)
		{
			const Real a = -1.0f + i * step;
			const Real x = cx + a * c - b * s;
			const Real y = cy + a * s + b * c;
			const Real z = onGround ? TheTerrainRenderObject->getHeightMapHeight( x, y, NULL ) + FIELD_LIFT : flatZ;
			s_fieldPos.push_back( Vector3( x, y, z ) );
			s_fieldUV.push_back( Vector2( (1.0f - a) * 0.5f, (1.0f - b) * 0.5f ) );
			s_fieldGlow.push_back( glowColor );
			s_fieldStain.push_back( stainColor );
		}
	}

	const Int base = chunk.vertexCount;
	for (Int j = 0; j < steps; ++j)
	{
		for (Int i = 0; i < steps; ++i)
		{
			const unsigned short v = (unsigned short)(base + j * side + i);
			s_fieldIndex.push_back( v );
			s_fieldIndex.push_back( (unsigned short)(v + 1) );
			s_fieldIndex.push_back( (unsigned short)(v + side) );
			s_fieldIndex.push_back( (unsigned short)(v + 1) );
			s_fieldIndex.push_back( (unsigned short)(v + side + 1) );
			s_fieldIndex.push_back( (unsigned short)(v + side) );
		}
	}
	chunk.vertexCount += vertices;
	chunk.indexCount += steps * steps * 6;
}

//-------------------------------------------------------------------------------------------------
// A field does not draw its particles.  The object behind it fires its field weapon every half
// second, and each shot's FX starts a new system whose particles swell from black to the field's
// colour and back to black in two seconds, at fresh random places: drawn as they are, the pool
// throbbed, its patches came and went, and past MaxFieldParticleCount new ones were refused and it
// thinned out.  The systems only say where a field is.  Every system of one template at one place
// is one field, and it is drawn as a fixed scatter of puddles over the emission radius, placed by a
// hash of the place so the scatter is the same every frame, at the template's brightest colour.  It
// fades in over FIELD_FADE_IN and out over the last FIELD_FADE_OUT frames of the field object's
// life, read off its LifetimeUpdate; a field with no such object (a one-off contamination) fades
// with its last particle instead.  This reads the logic's objects and writes nothing back.
//
// Held dead still the pool looked painted on, so each puddle stays where it is and breathes: it
// fades down by up to FIELD_PULSE and back, and swells and shrinks by FIELD_BREATHE, every puddle
// on its own period and phase, on the logic clock blended between ticks as the models are.  The
// puddles fall out of step with each other, so the pool shimmers and never throbs as a whole.
//-------------------------------------------------------------------------------------------------

static const Real	FIELD_PULSE_PERIOD		= 2.5f;		///< seconds, from 0.6 to 1.4 of it per puddle
static const Real	FIELD_PULSE						= 0.55f;	///< share of a puddle's brightness it fades by
static const Real	FIELD_BREATHE					= 0.04f;	///< share of a puddle's size it swings by
static const Real	FIELD_BUBBLE_SPACING	= 30.0f;	///< radius per bubble slot on a radiation pool
static const Int	FIELD_MAX_BUBBLES			= 8;
static const Real	FIELD_BUBBLE_PERIOD		= 1.5f;		///< seconds a bubble lives, from 0.7 to 1.3 of it per slot
static const Real	FIELD_BUBBLE_SIZE			= 5.0f;		///< half width at the pop
static const Real	FIELD_BUBBLE_GAIN			= 0.6f;
static const Real	FIELD_COVER						= 2.0f;		///< puddles per (radius / puddle size) squared
static const Int	FIELD_MAX_PUDDLES			= 48;
static const Real	FIELD_GAIN						= 0.7f;		///< of the template's brightest colour, a puddle
static const Int	FIELD_FADE_IN					= 15;			///< logic frames
static const Int	FIELD_FADE_OUT				= 90;			///< logic frames before the field object dies
static const Int	FIELD_FADE_GONE				= FIELD_FADE_OUT;	///< logic frames, a field fogged over or whose object went early
static const Real	FIELD_OBJECT_REACH		= 5.0f;		///< how near the field's centre its object stands

struct FieldKey
{
	const ParticleSystemTemplate *tmpl;
	Int x, y;
	bool operator<( const FieldKey &o ) const
	{
		if (tmpl != o.tmpl) return tmpl < o.tmpl;
		if (x != o.x) return x < o.x;
		return y < o.y;
	}
};

/** A field as it is drawn, kept between frames: a shot whose particles the LOD refused leaves a
	* half second with no system at the place, and the pool stays through it. */
struct FieldState
{
	Int born;						///< logic frame it was first seen
	Int seen;						///< logic frame a system was last seen at it
	ObjectID object;		///< the object whose life it fades with, or INVALID_ID
	Int gone;						///< logic frame it began going out (fog, or its object gone), or -1
	Real goneFade;			///< the fade it had then
	Real shown;					///< the fade it was last drawn at
	Int particleEnd;		///< logic frame its last particle ends, for a field with no object
	Coord3D centre;
	Real radius;				///< the emission radius, which UseCallersRadius set from the weapon
	Real size;					///< a puddle's half width
	RGBColor color;			///< the template's brightest keyframe
	ParticleSystemInfo::ParticleShaderType shader;
	AsciiString texture;
};

static const Int	FIELD_GRACE						= 30;			///< logic frames a field with no object outlives its systems
static const Real	FIELD_STACK						= 2.5f;		///< puddles' worth of cover a spot may add up to
static const Real	FIELD_SATURATE				= 0.6f;		///< share of the weakest channel taken out of the colour

static std::map<FieldKey, FieldState>	s_fieldStates;

/** One puddle of this frame's fields, before it is laid. */
struct FieldPuddle
{
	Real x, y, size, angle;
	Real bright;				///< its own share of the field's colour
	Real fade;					///< its field's fade
	Real overlap;				///< how many puddles, itself included, cover its middle
};

/** One field of this frame, before it is laid. */
struct FieldDraw
{
	FieldBatch batch;
	Coord3D centre;
	Bool onGround;
	Real fade;
	Bool visible;				///< inside the view; one outside is collected only for its overlap
	RGBColor color;
	AsciiString texture;
	Int firstPuddle, puddleCount;
	Bool glowing;				///< a warm hue, radiation: lit rather than stained, and it bubbles
	Real radius;
	UnsignedInt seed;		///< the place's hash, for its bubbles
};

static std::vector<FieldPuddle>	s_fieldPuddles;
static std::vector<FieldDraw>		s_fieldDraws;

/** The CLEANUP_HAZARD object with a LifetimeUpdate standing on a field's centre, if there is one. */
static ObjectID findFieldObject( const Coord3D &centre )
{
	static NameKeyType key_LifetimeUpdate = NAMEKEY( "LifetimeUpdate" );
	ObjectID found = INVALID_ID;
	Real best = FIELD_OBJECT_REACH * FIELD_OBJECT_REACH;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!obj->isKindOf( KINDOF_CLEANUP_HAZARD ))
			continue;
		const Coord3D *pos = obj->getPosition();
		const Real dx = pos->x - centre.x, dy = pos->y - centre.y;
		const Real d = dx * dx + dy * dy;
		if (d <= best && obj->findUpdateModule( key_LifetimeUpdate ) != NULL)
		{
			best = d;
			found = obj->getID();
		}
	}
	return found;
}

/** The middle of a random range. */
static inline Real midValue( const GameClientRandomVariable &v )
{
	return 0.5f * (v.getMinimumValue() + v.getMaximumValue());
}

/** A field's fade this frame, from its object's remaining life or its particles'.
	*
	* Under the fog the pool goes out, as the retail one did: FXList::doFXPos plays nothing where the
	* watcher's cell is not clear, so no new puddles came and the old ones died off in two seconds.
	* A Scud Storm's reveal running out over its own fields did exactly that 32 seconds into their
	* 45.  It is asked here of the cell directly rather than read off the systems stopping, which
	* left the pool at full strength for the 30 frames the systems were given to come back. */
static Real fieldFade( FieldState &state, Int now )
{
	static NameKeyType key_LifetimeUpdate = NAMEKEY( "LifetimeUpdate" );
	Bool lost = now - state.seen > FIELD_GRACE
		|| ThePartitionManager->getShroudStatusForPlayer( TheObserverCamera.getShroudPlayerIndex(), &state.centre ) != CELLSHROUD_CLEAR;
	Int remaining = state.particleEnd - now;
	if (!lost && state.object != INVALID_ID)
	{
		Object *obj = TheGameLogic->findObjectByID( state.object );
		LifetimeUpdate *life = obj ? (LifetimeUpdate *)obj->findUpdateModule( key_LifetimeUpdate ) : NULL;
		if (life)
			remaining = (Int)life->getDieFrame() - now;
		else
			lost = TRUE;	// cleaned up early, or died
	}
	if (lost)
	{
		// the pool goes out over the same three seconds as at the end of its life, from where it was
		if (state.gone < 0)
		{
			state.gone = now;
			state.goneFade = state.shown;
		}
		const Real left = 1.0f - (Real)(now - state.gone) / FIELD_FADE_GONE;
		state.shown = WWMath::Max( left, 0.0f ) * state.goneFade;
		return state.shown;
	}
	if (state.gone >= 0)
	{
		// out of the fog again: up from where it had got to
		state.born = now - REAL_TO_INT_FLOOR( state.shown * FIELD_FADE_IN );
		state.gone = -1;
	}
	const Real fadeIn = WWMath::Clamp( (Real)(now - state.born) / FIELD_FADE_IN, 0.0f, 1.0f );
	const Real fadeOut = WWMath::Clamp( (Real)remaining / FIELD_FADE_OUT, 0.0f, 1.0f );
	state.shown = fadeIn * fadeOut;
	return state.shown;
}

/** Collects this frame's fields from their systems and lays them on the terrain into the field
	* buffers; drawGroundFields draws them.  Returns the puddles laid. */
static Int collectGroundFields( ParticleSystemManager::ParticleSystemList &systems, const AABoxClass &view )
{
	const Int now = (Int)TheGameLogic->getFrame();
	// between the last two logic ticks, as the models are, so a pulse does not step at the logic rate
	const Real seconds = ((Real)now + (TheSmoothMotionActive ? TheSmoothMotionAlpha : 0.0f)) / LOGICFRAMES_PER_SECOND;
	s_fieldPuddles.clear();
	s_fieldDraws.clear();

	// where the fields are this frame, and what each one looks like
	for (ParticleSystemManager::ParticleSystemListIt it = systems.begin(); it != systems.end(); ++it)
	{
		ParticleSystem *sys = *it;
		if (!sys || sys->isUsingDrawables() || !isGroundField( sys ))
			continue;

		Coord3D pos;
		sys->getPosition( &pos );
		FieldKey key = { sys->getTemplate(), REAL_TO_INT_FLOOR( pos.x + 0.5f ), REAL_TO_INT_FLOOR( pos.y + 0.5f ) };
		const ParticleSystemInfo::EmissionVolumeType volume = sys->getEmisionVolumeType();
		const Real radius = volume == ParticleSystemInfo::SPHERE ? sys->m_emissionVolume.sphere.radius
			: (volume == ParticleSystemInfo::CYLINDER ? sys->m_emissionVolume.cylinder.radius : 0.0f);
		Int life = sys->isSystemForever() ? FIELD_FADE_OUT : (Int)sys->getSystemLifetimeLeft();
		life += REAL_TO_INT_CEIL( sys->m_lifetime.getMaximumValue() );
		for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
			life = WWMath::Max( life, (Int)p->getLifetimeLeft() );

		std::map<FieldKey, FieldState>::iterator st = s_fieldStates.find( key );
		if (st == s_fieldStates.end() || st->second.seen != now)
		{
			if (st == s_fieldStates.end())
			{
				FieldState fresh;
				fresh.born = now;
				fresh.seen = now;
				fresh.object = findFieldObject( pos );
				fresh.gone = -1;
				fresh.goneFade = 0.0f;
				fresh.shown = 0.0f;
				st = s_fieldStates.insert( std::make_pair( key, fresh ) ).first;
			}
			FieldState &state = st->second;
			state.seen = now;
			state.particleEnd = now + life;
			state.centre = pos;
			state.radius = radius;

			// a puddle the size a particle reaches halfway through its life
			const Real half = 0.5f * midValue( sys->m_lifetime );
			const Real rate = midValue( sys->m_sizeRate );
			const Real damping = midValue( sys->m_sizeRateDamping );
			const Real growth = damping < 1.0f ? rate * (1.0f - powf( damping, half )) / (1.0f - damping) : rate * half;
			state.size = WWMath::Max( midValue( sys->m_startSize ) + growth, 1.0f );

			// the template's brightest keyframe is the field's colour
			state.color = sys->m_colorKey[ 0 ].color;
			for (Int k = 1; k < MAX_KEYFRAMES; ++k)
			{
				const RGBColor &c = sys->m_colorKey[ k ].color;
				if (c.red + c.green + c.blue > state.color.red + state.color.green + state.color.blue)
					state.color = c;
			}
			state.shader = sys->getShaderType();
			state.texture = sys->getParticleTypeName();
		}
		else
		{
			FieldState &state = st->second;
			state.radius = WWMath::Max( state.radius, radius );
			state.particleEnd = WWMath::Max( state.particleEnd, now + life );
		}
	}

	Int laid = 0;
	for (std::map<FieldKey, FieldState>::iterator st = s_fieldStates.begin(); st != s_fieldStates.end(); )
	{
		const FieldKey &key = st->first;
		FieldState &state = st->second;

		// Kept while its systems are, even faded out: the systems outlive the object by a few
		// seconds, and a field forgotten under them would be found again as a new one.
		const Real fade = fieldFade( state, now );
		const Bool stale = now - state.seen > FIELD_GRACE;
		if ((stale && fade <= 0.0f) || now < state.seen)
		{
			// out, or a frame count from before a load
			s_fieldStates.erase( st++ );
			continue;
		}
		++st;
		if (fade <= 0.0f)
			continue;

		// a field off screen is still collected, as its puddles dim the ones it overlaps on screen
		const Real size = state.size;
		const Real reach = state.radius + size;
		const Bool visible = WWMath::Fabs( state.centre.x - view.Center.X ) <= view.Extent.X + reach
			&& WWMath::Fabs( state.centre.y - view.Center.Y ) <= view.Extent.Y + reach
			&& WWMath::Fabs( state.centre.z - view.Center.Z ) <= view.Extent.Z + reach;

		FieldBatch batch;
		if (!particlePresetShader( state.shader, &batch.glow ))
			continue;
		batch.glow.Set_Cull_Mode( ShaderClass::CULL_MODE_DISABLE );
		batch.glow.Set_Primary_Gradient( ShaderClass::GRADIENT_MODULATE );
		batch.glow.Set_Texturing( ShaderClass::TEXTURING_ENABLE );
		// dst * (1 - texel * colour): black texels leave the ground alone, as they add nothing to it
		batch.stain = batch.glow;
		batch.stain.Set_Src_Blend_Func( ShaderClass::SRCBLEND_ZERO );
		batch.stain.Set_Dst_Blend_Func( ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR );
		batch.stained = state.shader == ParticleSystemInfo::ADDITIVE;
		batch.texture = NULL;

		FieldDraw draw;
		draw.batch = batch;
		draw.centre = state.centre;
		draw.onGround = state.centre.z
			- TheTerrainRenderObject->getHeightMapHeight( state.centre.x, state.centre.y, NULL ) < FIELD_ON_GROUND;
		draw.fade = fade;
		draw.visible = visible;
		draw.texture = state.texture;

		// More saturated than the keyframe: pulling the weakest channel down keeps the hue and keeps
		// the pool from going mint where the puddles overlap.
		const RGBColor &key_color = state.color;
		const Real low = WWMath::Min( key_color.red, WWMath::Min( key_color.green, key_color.blue ) ) * FIELD_SATURATE;
		const Real peak = WWMath::Max( key_color.red, WWMath::Max( key_color.green, key_color.blue ) );
		const Real lift = peak > low ? peak / (peak - low) : 1.0f;
		draw.color.red = (key_color.red - low) * lift;
		draw.color.green = (key_color.green - low) * lift;
		draw.color.blue = (key_color.blue - low) * lift;

		// Radiation is the one warm field.  Its orange stained the ground brown and glowed at 0.7 of a
		// dim keyframe, which read as a scorch; it is lit to full strength instead and leaves the
		// ground under it alone.
		draw.glowing = draw.color.red >= draw.color.green && draw.color.red >= draw.color.blue;
		if (draw.glowing)
		{
			const Real full = WWMath::Max( draw.color.red, 1.0f / 255.0f );
			draw.color.red = 1.0f;
			draw.color.green /= full;
			draw.color.blue /= full;
			draw.batch.stained = FALSE;
		}
		draw.radius = state.radius;

		const Real spread = state.radius / size;
		const Int count = WWMath::Clamp_Int( (Int)ceilf( FIELD_COVER * spread * spread ), 1, FIELD_MAX_PUDDLES );

		// the same scatter every frame: a little generator seeded by the place
		UnsignedInt seed = (UnsignedInt)key.x * 73856093u ^ (UnsignedInt)key.y * 19349663u ^ 0x9E3779B9u;
		draw.seed = seed;
		draw.firstPuddle = (Int)s_fieldPuddles.size();
		draw.puddleCount = count;
		for (Int i = 0; i < count; ++i)
		{
			Real u[ 4 ];
			for (Int k = 0; k < 4; ++k)
			{
				seed = seed * 1664525u + 1013904223u;
				u[ k ] = (Real)(seed >> 8) * (1.0f / 16777216.0f);
			}
			const Real r = count == 1 ? 0.0f : state.radius * sqrtf( u[ 0 ] );
			const Real a = u[ 1 ] * 2.0f * PI;
			FieldPuddle puddle;
			// its own beat, from a second generator so the scatter stays where it was
			UnsignedInt h = seed * 2654435761u;
			Real w[ 3 ];
			for (Int k = 0; k < 3; ++k)
			{
				h = h * 1664525u + 1013904223u;
				w[ k ] = (Real)(h >> 8) * (1.0f / 16777216.0f);
			}
			const Real phase = w[ 0 ] * 2.0f * PI;
			const Real cycle = 2.0f * PI * seconds / (FIELD_PULSE_PERIOD * (0.6f + 0.8f * w[ 1 ]));
			const Real pulse = 0.5f + 0.5f * WWMath::Sin( cycle + phase );
			puddle.x = state.centre.x + r * WWMath::Cos( a );
			puddle.y = state.centre.y + r * WWMath::Sin( a );
			puddle.size = size * (1.0f + FIELD_BREATHE * WWMath::Sin( 0.7f * cycle + w[ 2 ] * 2.0f * PI ));
			puddle.angle = u[ 2 ] * 2.0f * PI;
			puddle.bright = (0.8f + 0.2f * u[ 3 ]) * (1.0f - FIELD_PULSE * pulse);
			puddle.fade = fade;
			puddle.overlap = 0.0f;
			s_fieldPuddles.push_back( puddle );
		}
		s_fieldDraws.push_back( draw );
	}

	// How many puddles cover each one's middle, over every field: four Scud fields on one spot and the
	// thick middle of each, added up, went past white.  Each puddle is dimmed by how far its cover
	// goes over FIELD_STACK, so the whole pool tops out at about that many puddles' worth.  A
	// neighbour counts at its own fade, so a field coming up or going out dims the others with it
	// rather than in one frame.
	// ponytail: every pair, a few hundred puddles on the map at most; a grid when a map holds more
	const Int puddleCount = (Int)s_fieldPuddles.size();
	for (Int i = 0; i < puddleCount; ++i)
	{
		FieldPuddle &p = s_fieldPuddles[ i ];
		p.overlap = 1.0f;
		for (Int j = 0; j < puddleCount; ++j)
		{
			const FieldPuddle &q = s_fieldPuddles[ j ];
			const Real reach = p.size + q.size;
			const Real dx = p.x - q.x, dy = p.y - q.y;
			const Real t = 1.0f - (dx * dx + dy * dy) / (reach * reach);
			if (j != i && t > 0.0f)
				p.overlap += t * t * q.fade;
		}
	}

	for (size_t d = 0; d < s_fieldDraws.size(); ++d)
	{
		FieldDraw &draw = s_fieldDraws[ d ];
		if (!draw.visible)
			continue;
		FieldBatch &batch = draw.batch;
		batch.firstChunk = (Int)s_fieldChunks.size();
		FieldChunk chunk = { (Int)s_fieldPos.size(), 0, (Int)s_fieldIndex.size(), 0 };
		for (Int i = draw.firstPuddle; i < draw.firstPuddle + draw.puddleCount; ++i)
		{
			const FieldPuddle &puddle = s_fieldPuddles[ i ];
			const Real stack = puddle.overlap > FIELD_STACK ? FIELD_STACK / puddle.overlap : 1.0f;
			const Real bright = FIELD_GAIN * draw.fade * puddle.bright * stack;
			const Real red = draw.color.red * bright, green = draw.color.green * bright, blue = draw.color.blue * bright;
			const unsigned glowColor = DX8Wrapper::Convert_Color_Clamp( Vector4( red, green, blue, 1.0f ) );
			unsigned stainColor = 0;
			const Real peak = WWMath::Max( red, WWMath::Max( green, blue ) );
			if (batch.stained && peak > 0.0f)
			{
				// once more by the fade, so the stain is gone with the glow and not after it
				const Real inv = 1.0f / peak;
				const Real k = peak * draw.fade;
				stainColor = DX8Wrapper::Convert_Color_Clamp( Vector4(
					k * (FIELD_STAIN_DARKEN + FIELD_STAIN_TINT * (1.0f - red * inv)),
					k * (FIELD_STAIN_DARKEN + FIELD_STAIN_TINT * (1.0f - green * inv)),
					k * (FIELD_STAIN_DARKEN + FIELD_STAIN_TINT * (1.0f - blue * inv)),
					1.0f ) );
			}
			addFieldPuddle( chunk, puddle.x, puddle.y, draw.centre.z, draw.onGround, puddle.size, puddle.angle,
				glowColor, stainColor );
			++laid;
		}

		// Radiation bubbles: a few small light blobs that swell out of the pool and pop, each slot on
		// its own beat and somewhere new every time.  They take no part in the overlap above.
		if (draw.glowing)
		{
			const Int bubbles = WWMath::Clamp_Int( (Int)(draw.radius / FIELD_BUBBLE_SPACING), 1, FIELD_MAX_BUBBLES );
			for (Int b = 0; b < bubbles; ++b)
			{
				UnsignedInt h = draw.seed ^ ((UnsignedInt)b * 2246822519u);
				h = h * 1664525u + 1013904223u;
				const Real period = FIELD_BUBBLE_PERIOD * (0.7f + 0.6f * (Real)(h >> 8) * (1.0f / 16777216.0f));
				h = h * 1664525u + 1013904223u;
				const Real t = seconds / period + (Real)(h >> 8) * (1.0f / 16777216.0f);
				const Real round = floorf( t );
				const Real life = t - round;

				// where this round's bubble comes up
				UnsignedInt p = h ^ ((UnsignedInt)(Int)round * 3266489917u);
				p = p * 1664525u + 1013904223u;
				const Real r = draw.radius * 0.9f * sqrtf( (Real)(p >> 8) * (1.0f / 16777216.0f) );
				p = p * 1664525u + 1013904223u;
				const Real a = (Real)(p >> 8) * (1.0f / 16777216.0f) * 2.0f * PI;

				// up over most of its life, gone in the last fifth; lighter and yellower than the pool
				const Real swell = life < 0.8f ? life / 0.8f : (1.0f - life) / 0.2f;
				const Real bright = FIELD_BUBBLE_GAIN * draw.fade * swell;
				const unsigned bubbleColor = DX8Wrapper::Convert_Color_Clamp( Vector4( bright,
					bright * (0.5f + 0.5f * draw.color.green), bright * (0.3f + 0.7f * draw.color.blue), 1.0f ) );
				addFieldPuddle( chunk, draw.centre.x + r * WWMath::Cos( a ), draw.centre.y + r * WWMath::Sin( a ),
					draw.centre.z, draw.onGround, FIELD_BUBBLE_SIZE * (0.4f + 0.6f * life), a, bubbleColor, 0 );
				++laid;
			}
		}
		s_fieldChunks.push_back( chunk );

		batch.chunkCount = (Int)s_fieldChunks.size() - batch.firstChunk;
		batch.texture = W3DDisplay::m_assetManager->Get_Texture( draw.texture.str() );
		s_fieldBatches.push_back( batch );
	}
	return laid;
}

/** Draws every field the frame collected: all their stains, then all their glows. */
static void drawGroundFields( void )
{
	if (s_fieldBatches.empty())
		return;

	VertexMaterialClass *material = VertexMaterialClass::Get_Preset( VertexMaterialClass::PRELIT_DIFFUSE );
	Matrix4x4 identity( true );
	DX8Wrapper::Set_Transform( D3DTS_WORLD, identity );
	DX8Wrapper::Set_Material( material );
	REF_PTR_RELEASE( material );

	for (Int pass = 0; pass < 2; ++pass)
	{
		const Bool stainPass = pass == 0;
		for (size_t b = 0; b < s_fieldBatches.size(); ++b)
		{
			const FieldBatch &batch = s_fieldBatches[ b ];
			if (stainPass && !batch.stained)
				continue;
			DX8Wrapper::Set_Texture( 0, batch.texture );
			for (Int k = batch.firstChunk; k < batch.firstChunk + batch.chunkCount; ++k)
			{
				const FieldChunk &fc = s_fieldChunks[ k ];
				DynamicIBAccessClass ibAccess( BUFFER_TYPE_DYNAMIC_DX8, (unsigned short)fc.indexCount );
				{
					DynamicIBAccessClass::WriteLockClass lock( &ibAccess );
					memcpy( lock.Get_Index_Array(), &s_fieldIndex[ fc.firstIndex ], fc.indexCount * sizeof( unsigned short ) );
				}
				DX8Wrapper::Set_Index_Buffer( ibAccess, 0 );
				if (stainPass)
					drawFieldPass( fc, s_fieldStain, batch.stain );
				else
					drawFieldPass( fc, s_fieldGlow, batch.glow );
			}
		}
	}

	for (size_t b = 0; b < s_fieldBatches.size(); ++b)
		s_fieldBatches[ b ].texture->Release_Ref();	// the draw state holds its own reference to the last one
	s_fieldBatches.clear();
}

W3DParticleSystemManager::W3DParticleSystemManager()
{
	m_pointGroup = NULL;
	m_streakLine = NULL;
	m_posBuffer = NULL;
	m_RGBABuffer = NULL;
	m_sizeBuffer = NULL;
	m_angleBuffer = NULL;
	m_readyToRender = false;

	m_onScreenParticleCount = 0;

	m_pointGroup = NEW PointGroupClass();
	//m_streakLine = NULL;
	m_streakLine = NEW StreakLineClass();
	
	m_posBuffer = NEW_REF( ShareBufferClass<Vector3>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_posBuffer") );
	m_RGBABuffer = NEW_REF( ShareBufferClass<Vector4>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_RGBABuffer") );
	m_sizeBuffer = NEW_REF( ShareBufferClass<float>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_sizeBuffer") );
	m_angleBuffer = NEW_REF( ShareBufferClass<uint8>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_angleBuffer") );
}

W3DParticleSystemManager::~W3DParticleSystemManager()
{
	delete m_pointGroup;

//	W3DDisplay::m_3DScene->Remove_Render_Object( m_streakLine );

	if (m_streakLine)
	{
		REF_PTR_RELEASE(m_streakLine);
	}

	REF_PTR_RELEASE(m_posBuffer);
	REF_PTR_RELEASE(m_RGBABuffer);
	REF_PTR_RELEASE(m_sizeBuffer);
	REF_PTR_RELEASE(m_angleBuffer);
}

/** A reset can be followed by an update on the same logic frame, so the fires' frame alone would
	* not tell the lights taken before it from the ones after. */
void W3DParticleSystemManager::reset()
{
	ParticleSystemManager::reset();
	s_fireLightsValid = FALSE;
	s_fieldStates.clear();	// the object IDs it holds belong to the game that ended
}

/**
 * Hack because DoParticles is called from Flush(), which is called
 * multiple times per frame.  We only want to render once.
 * @todo Clean up the flag/Flush hack.
 */
void W3DParticleSystemManager::queueParticleRender()
{
	m_readyToRender = true;
}

/**
 * Nasty hack to render particles last. Called directly by WW3D::Flush()
 */
void DoParticles( RenderInfoClass &rinfo )
{
	if (TheParticleSystemManager)
		TheParticleSystemManager->doParticles(rinfo);
}

void W3DParticleSystemManager::doParticles(RenderInfoClass &rinfo)
{

	if (m_readyToRender == false)
		return;

	// external mechanism must tell us when it's OK to render again...
	m_readyToRender = false;

	//reset each frame
	/// @todo lorenzen sez: this should be debug only:
	m_onScreenParticleCount = 0;

	Int visibleSmudgeCount = 0;
	if (TheSmudgeManager)
		TheSmudgeManager->setSmudgeCountLastFrame(0);	//keep track of visible smudges

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);

	//@todo lorenzen sez: put these in registers for sure
	Real bcX = bbox.Center.X;
	Real bcY = bbox.Center.Y;
	Real bcZ = bbox.Center.Z;
	Real beX = bbox.Extent.X;
	Real beY = bbox.Extent.Y;
	Real beZ = bbox.Extent.Z;

	unsigned int personalities[MAX_POINTS_PER_GROUP];


	m_fieldParticleCount = 0;

	SmudgeSet *set=NULL;
	if (TheSmudgeManager)
		set=TheSmudgeManager->addSmudgeSet();	//global smudge set through which all smudges are rendered.

#ifdef DEBUG_LOGGING
	Int64 tFillStart, tFillEnd;
	tFillStart = Clock_Ticks();
#endif

	ParticleSystemManager::ParticleSystemList &particleSysList = TheParticleSystemManager->getAllParticleSystems();

	// Sorted billboards - most smoke, fire and dust - are written on the job pool.  With a hundred
	// thousand particles, walking them one system after another was a quarter of the frame.  Their
	// room in the sorting array is reserved here in list order, and the loop below inserts each one
	// into the sorting pool at its place in the same order, so the pool sees them as it always did.
	// Smudges, which draw on the client's random stream, streaks, volume particles, ground-aligned and
	// alpha-tested systems stay in that loop as they were.
	m_billboardFills.clear();
	for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
	{
		ParticleSystem *sys = (*it);
		if (!sys || sys->isUsingDrawables())
			continue;
		if (*((UnsignedInt *)sys->getParticleTypeName().str()) == 0x44554D53)
			continue;

		BillboardFill fill;
		if (sys->isUsingStreak() || !sys->shouldBillboard() || sys->getVolumeParticleDepth() > 1
				|| !particlePresetShader( sys->getShaderType(), &fill.shader )
				|| !PointGroupClass::Would_Sort_Billboards( fill.shader ))
			continue;

		const Int particles = (Int)sys->getParticleCount();
		if (particles == 0)
			continue;

		fill.system = sys;
		fill.capacity = (particles < (Int)MAX_POINTS_PER_GROUP) ? particles : (Int)MAX_POINTS_PER_GROUP;
		fill.fieldIncrement = ( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE ) ? 1 : 0;
		fill.drawn = 0;
		fill.pastLimit = 0;
		fill.glow = FALSE;
		fill.texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );
		PointGroupClass::Reserve_Sorted_Billboards( fill.capacity, &fill.range );
		m_billboardFills.push_back( fill );
	}

	BillboardFillJob fillJob;
	fillJob.fills = m_billboardFills.empty() ? NULL : &m_billboardFills[ 0 ];
	DX8Wrapper::Get_Transform( D3DTS_VIEW, fillJob.view );
	fillJob.centerX = bcX;
	fillJob.centerY = bcY;
	fillJob.centerZ = bcZ;
	fillJob.extentX = beX;
	fillJob.extentY = beY;
	fillJob.extentZ = beZ;
	fillJob.lightCount = 0;
	if (TheGlobalData->m_smokeFireLighting)
	{
		// Between two particle updates nothing moves a particle; a system made in between has none
		// yet, and the counts catch a load or anything else that adds or takes some away.
		if (!s_fireLightsValid || s_fireLightsFrame != m_lastLogicFrameUpdate
				|| s_fireLightsParticles != m_particleCount || s_fireLightsSystems != m_particleSystemCount)
		{
			s_fireLights.clear();
			for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
			{
				ParticleSystem *sys = *it;
				if (sys && !sys->isUsingDrawables() && sys->getShaderType() == ParticleSystemInfo::ADDITIVE
						&& sys->getParticleCount() > 0)
					gatherFireLight( sys );
			}
			s_fireLightsValid = TRUE;
			s_fireLightsFrame = m_lastLogicFrameUpdate;
			s_fireLightsParticles = m_particleCount;
			s_fireLightsSystems = m_particleSystemCount;
		}
		fillJob.lightCount = gatherSmokeLights( fillJob.lights, bbox );
	}
	fillJob.glowAfterShade = Direct3D11_Present_Is_Enabled();
	JobSystem::parallel_for( (Int)m_billboardFills.size(), BILLBOARD_FILLS_PER_CLAIM, fillBillboards, &fillJob );

	s_fieldPos.clear();
	s_fieldUV.clear();
	s_fieldGlow.clear();
	s_fieldStain.clear();
	s_fieldIndex.clear();
	s_fieldChunks.clear();
	{
		const Int puddles = collectGroundFields( particleSysList, bbox );
		// counted as field particles on purpose: past MaxFieldParticleCount the field systems keep
		// one particle each, and that one is all the steady pool needs from them
		m_fieldParticleCount += puddles;
		m_onScreenParticleCount += puddles;
	}
	drawGroundFields();

	size_t nextFill = 0;
	for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
	{
		ParticleSystem *sys = (*it);
		if (!sys) {
			continue;
		}

		// only look at particle/point style systems
		if (sys->isUsingDrawables())
			continue;

		//temporary hack that checks if texture name starts with "SMUD" - if so, we can assume it's a smudge type
		if (/*sys->isUsingSmudge()*/ *((UnsignedInt *)sys->getParticleTypeName().str()) == 0x44554D53)
		{
			if (TheSmudgeManager && ((W3DSmudgeManager*)TheSmudgeManager)->getHardwareSupport() && TheGlobalData->m_useHeatEffects)
			{
				//set-up all the per-particle
				for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
				{
					const Coord3D *pos = p->getPosition();
					Real psize = p->getSize();

					//Cull particle to edges of screen and terrain.
					if (WWMath::Fabs( pos->x - bcX ) > ( beX + psize ) )
						continue;

					if (WWMath::Fabs( pos->y - bcY ) > ( beY + psize ) )
						continue;

					if (WWMath::Fabs( pos->z - bcZ ) > ( beZ + psize ) )
						continue;

					Smudge *smudge = set->addSmudgeToSet();

					smudge->m_pos.Set( pos->x, pos->y, pos->z );
					// Same range on both axes. The vertical one was half the horizontal, which
					// nobody could see while the centre vertex was reading the horizontal offset
					// for both - now that it reads .Y, a heat smudge pulled twice as far sideways.
					smudge->m_offset.Set( GameClientRandomValueReal(-0.06f,0.06f), GameClientRandomValueReal(-0.06f,0.06f) );
					smudge->m_size = psize;
					smudge->m_opacity = p->getAlpha();
					visibleSmudgeCount++;
				}
			}
			continue;
		}

		if (nextFill < m_billboardFills.size() && m_billboardFills[ nextFill ].system == sys)
		{
			BillboardFill &fill = m_billboardFills[ nextFill++ ];
			PointGroupClass::Insert_Sorted_Billboards( &fill.range, fill.drawn, fill.texture, fill.shader, fill.glow != FALSE );
			fill.texture->Release_Ref();	// the draw state took its own reference
			m_fieldParticleCount += fill.fieldIncrement * fill.drawn;
			m_onScreenParticleCount += fill.drawn;
#ifdef DEBUG_LOGGING
			TheParticlesPastGroupLimit += fill.pastLimit;
#endif
			continue;
		}

		if (isGroundField( sys ))
			continue;	// drawn above, every field at once

		/// @todo lorenzen sez: declare these outside the sys loop, and put some in registers
		// initialize them here still, of course
		// build W3D particle buffer
		Int count = 0;
		Vector3 *posArray = m_posBuffer->Get_Array();
		Real *sizeArray = m_sizeBuffer->Get_Array();
		Vector4 *RGBAArray = m_RGBABuffer->Get_Array();
		uint8 *angleArray = m_angleBuffer->Get_Array();
		const Coord3D *pos;
		const RGBColor *color;
		Real psize;
		// the same answer for every particle of the system, so asked once rather than once a particle
		const UnsignedInt fieldParticleIncrement =
			( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE ) ? 1 : 0;
		SmokeLight lights[ SMOKE_LIGHTS_PER_SYSTEM ];
		const Int lightCount = pickSmokeLights( sys, fillJob.lights, fillJob.lightCount, lights );




		//set-up all the per-particle
		for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
		{
			pos = p->getPosition();
			psize = p->getSize();

			//Cull particle to edges of screen and terrain.
			if (WWMath::Fabs(pos->x - bcX) > (beX + psize))
				continue;

			if (WWMath::Fabs(pos->y - bcY) > (beY + psize))
				continue;

			if (WWMath::Fabs(pos->z - bcZ) > (beZ + psize))
				continue;

			m_fieldParticleCount += fieldParticleIncrement;
			
			//@todo lorenzen sez: use pointer arithmetic for these arrays
			personalities[count] = p->getPersonality();
			
			posArray[count].X = pos->x;
			posArray[count].Y = pos->y;
			posArray[count].Z = pos->z;

			sizeArray[count] = psize;

			color = p->getColor();
			RGBAArray[count].X = color->red;
			RGBAArray[count].Y = color->green;
			RGBAArray[count].Z = color->blue;
			RGBAArray[count].W = p->getAlpha();
			if (lightCount > 0)
				lightSmoke( pos, RGBAArray[count].W, lights, lightCount, RGBAArray[count].X, RGBAArray[count].Y, RGBAArray[count].Z );
		
			angleArray[count] = floatToByteAsMsvc( p->getAngle() * 255.0f / (2.0f * PI) );
			
			if (++count == MAX_POINTS_PER_GROUP)
			{
#ifdef DEBUG_LOGGING
				TheParticlesPastGroupLimit += sys->getParticleCount() - count;
#endif
				break;
			}
		}

		if ( count == 0 )
			continue;	//this system has no particles to render

		TextureClass *texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );

		
		if ( m_streakLine && sys->isUsingStreak() && (count >= 2) ) 
		{
			m_streakLine->Reset_Line();

			m_streakLine->Set_Texture( texture );
			texture->Release_Ref();//release reference since it's held by streakline
			switch( sys->getShaderType() )
			{
				case ParticleSystemInfo::ADDITIVE:
					m_streakLine->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA:
					m_streakLine->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA_TEST:
					m_streakLine->Set_Shader( ShaderClass::_PresetATestSpriteShader );
					break;
				case ParticleSystemInfo::MULTIPLY:
					m_streakLine->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
					break;
			}
			
			//UPDATE THE STREAK'S ARRAYS
			m_streakLine->Set_LocsWidthsColors( 
				count,
				m_posBuffer->Get_Array(),
				m_sizeBuffer->Get_Array(),
				m_RGBABuffer->Get_Array(),
				&personalities[0]
				);

			//WWASSERT( m_streakLine->Get_Num_Points() == count );

			// This is the happy place for this!
			RGBAArray[0].X = 0;//eliminates the scissor edge on the trailing edge of the streak
			RGBAArray[0].Y = 0;
			RGBAArray[0].Z = 0;
			RGBAArray[0].W = 0;


			//RENDER STREAK!
			m_streakLine->Render( rinfo );
			
		}
		else 
		{

			WWASSERT( m_pointGroup );

			if ( m_pointGroup ) // this catches the particle and volumeparticle cases
			{
				// render all the systems' particles
				m_pointGroup->Set_Texture( texture );
				texture->Release_Ref();//release reference since it's held by pointGroup
				m_pointGroup->Set_Flag( PointGroupClass::TRANSFORM, true );	// transform to screen space

				switch( sys->getShaderType() )
				{
					case ParticleSystemInfo::ADDITIVE:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA_TEST:
						m_pointGroup->Set_Shader( ShaderClass::_PresetATestSpriteShader );
						break;
					case ParticleSystemInfo::MULTIPLY:
						m_pointGroup->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
						break;
				}

				/// @todo Use both QUADS and TRIS for particles
				m_pointGroup->Set_Point_Mode( PointGroupClass::QUADS );
				m_pointGroup->Set_Arrays( m_posBuffer, m_RGBABuffer, NULL, m_sizeBuffer, m_angleBuffer, NULL, count );
				m_pointGroup->Set_Billboard(sys->shouldBillboard());

				/// @todo Support animated texture particles
				/// @todo lorenzen sez: unimplemented code wastes cpu cycles
				m_pointGroup->Set_Point_Frame( 0 );

				//RENDER IT!
				if( sys->getVolumeParticleDepth() > 1 )
				{
					m_pointGroup->RenderVolumeParticle( rinfo, sys->getVolumeParticleDepth() );
				}
				else
					m_pointGroup->Render( rinfo );
		
			}
		}


		/// @todo lorenzen sez: this should be debug only:
		//add particle count to total
		m_onScreenParticleCount += count;

	/*
		// draw the wind vector for this particle system on the screen
		UnsignedInt width = TheDisplay->getWidth();
		UnsignedInt height = TheDisplay->getHeight();
		Coord3D worldStart, worldEnd;
		ICoord2D pixelStart, pixelEnd;
		sys->getPosition( &worldStart );
		worldEnd.x = Cos( sys->getWindAngle() ) * 50.0f + worldStart.x;
		worldEnd.y = Sin( sys->getWindAngle() ) * 50.0f + worldStart.y;
		worldEnd.z = worldStart.z;
		TheTacticalView->worldToScreen( &worldStart, &pixelStart );
		TheTacticalView->worldToScreen( &worldEnd, &pixelEnd );
		Color colorStart = GameMakeColor( 255, 255, 255, 255 );
		Color colorEnd = GameMakeColor( 255, 128, 128, 255 );
		TheDisplay->drawLine( pixelStart.x, pixelStart.y, pixelEnd.x, pixelEnd.y, 1.0f, colorStart, colorEnd );
	*/


	}// next system

		/// @todo lorenzen sez: this should be debug only:
	TheParticleSystemManager->setOnScreenParticleCount(m_onScreenParticleCount);

#ifdef DEBUG_LOGGING
	tFillEnd = Clock_Ticks();
	{
		Int64 freq;
		freq = Clock_Ticks_Per_Second();
		if( freq > 0 )
			TheParticleFillMS += (Real)((double)(tFillEnd - tFillStart) * 1000.0 / (double)freq);
	}
#endif

	//Draw any particles belonging to weather effects
	if (TheSnowManager)
		((W3DSnowManager *)TheSnowManager)->render(rinfo);

	//Now process screen smudges which are particles that distort the background behind them.
	if(TheSmudgeManager)
	{
		((W3DSmudgeManager *)TheSmudgeManager)->render(rinfo);
		TheSmudgeManager->reset();	//clear all the smudges after rendering since we fill again each frame.
		TheSmudgeManager->setSmudgeCountLastFrame(visibleSmudgeCount);
	}
}
