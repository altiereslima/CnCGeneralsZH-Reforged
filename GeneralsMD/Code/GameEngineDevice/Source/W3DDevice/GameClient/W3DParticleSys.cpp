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
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx11runtime.h"
#include "Common/JobSystem.h"

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

/** One additive system as a point light, if it is bright enough and near enough the view box. */
static void gatherFireLight( ParticleSystem *sys, SmokeLight *lights, Int &count,
	const AABoxClass &view )
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

	SmokeLight light;
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
	if (WWMath::Fabs( light.x - view.Center.X ) > view.Extent.X + reach
			|| WWMath::Fabs( light.y - view.Center.Y ) > view.Extent.Y + reach
			|| WWMath::Fabs( light.z - view.Center.Z ) > view.Extent.Z + reach)
		return;

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
	addSmokeLight( lights, count, light );
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

/** Fills lights[] with this frame's lights, strongest first, and returns how many. */
static Int gatherSmokeLights( SmokeLight *lights, const AABoxClass &view )
{
	Int count = 0;
	ParticleSystemManager::ParticleSystemList &systems = TheParticleSystemManager->getAllParticleSystems();
	for (ParticleSystemManager::ParticleSystemListIt it = systems.begin(); it != systems.end(); ++it)
	{
		ParticleSystem *sys = *it;
		if (sys && !sys->isUsingDrawables() && sys->getShaderType() == ParticleSystemInfo::ADDITIVE
				&& sys->getParticleCount() > 0)
			gatherFireLight( sys, lights, count, view );
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
	fillJob.lightCount = TheGlobalData->m_smokeFireLighting ? gatherSmokeLights( fillJob.lights, bbox ) : 0;
	fillJob.glowAfterShade = Direct3D11_Present_Is_Enabled();
	JobSystem::parallel_for( (Int)m_billboardFills.size(), BILLBOARD_FILLS_PER_CLAIM, fillBillboards, &fillJob );
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
