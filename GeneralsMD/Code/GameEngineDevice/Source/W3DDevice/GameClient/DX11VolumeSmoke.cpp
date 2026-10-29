// Explosion smoke as a fluid, behind the VolumeSmoke setting (Off, Low, High; -volsmoke for one
// run), on the Direct3D 11 frame only.  Distributed under
// the repository's GPL-3.0 terms; see LICENSE.md.
//
// Four kinds of blast, each told from the particle systems its FX spawns and each played from a
// timeline of its own, modelled on how the real thing looks:
//
// A tank's death is an ammunition cook-off, laid out on the dying hull's own box (its template's
// length, width and height and its heading, read from the drawable).  Orange flame and black soot
// burst out low along the hull and roll along the ground off both ends between near-black masses
// banked on its long flanks, a ragged plume of fire, white in the middle and orange at its edges, fans up out of the hull
// top in thirteen jets, the middle one tallest, on High twice as high as the band is long, and
// cools to grey-brown smoke, two to four
// smaller pops follow over a few seconds, and then the hull burns: a thick black column for half
// a minute that rises from its whole length, leans with the wind and thins.
//
// A shell or a bomb on the ground flashes for a frame, throws a hollow cone of dirt jets ten to
// twenty metres up that fall back inside three to six seconds, leaves a black-grey puff in the
// middle that rises slowly, and pushes a skirt of dust out along the ground.  Bombs are the same
// at a larger cell with a small mushroom on top; an airburst has no dirt, only the puff and a
// patch of dust under it.
//
// A building's death is laid out on its model's meshes, boxes read from its render object by
// Gather, and sized by them: each mesh fills with smoke and dust in its own place and slumps, fire
// bursts out of the tops of the biggest, a ragged plume climbs out of them and cools to grey-brown
// smoke, the collapse pours pale dust out from under every wall along the ground and round the
// buildings beside it, and the ruin burns under a column for four seconds.  A damaged stage
// blows fire out of the top of a building that stays standing.
//
// A nuclear-class blast (the missile, the GLA's, the MOAB) has a volume of its own, much bigger:
// a white flash, a white dome at the shock front, a fireball glowing from inside that rises,
// slows and flattens into a cauliflower cap three to four times as wide as its grey-brown stem of
// dust, a white collar round the stem for a few seconds, a low wall of dust racing out along the
// ground, and the cap cooling to cream, white on top and dark underneath.  The cap and the stem are
// shapes the cells are drawn towards, so they hold; the flow rolls and smears them.
//
// The sim is the same for all of them: a slab of a shared 3D atlas per blast, soot and dust in
// two channels (the dust settles under gravity, the soot rises with the heat), and each logic
// frame six compute passes: the curl, a semi-Lagrangian advection with buoyancy that fades under
// a ceiling (so a cap stops and spreads), vorticity confinement, speed-squared drag and the wind,
// the blast's forcing (the ground skirt, the vortex ring, the inflow stem, the lumpy hot core and
// the emitters the timeline places), the divergence, 14 Jacobi sweeps, the projection and a sun
// light march per cell.  The frame raymarches each box against the scene depth, far box first,
// with the heat glowing from deep red to white, a shell's or a nuke's flash and the condensation
// shell drawn from the timeline, and a billowy noise eroding the surface so the cap reads as
// cauliflower.
//
// Every blast sends a front out through every smoke it reaches: the other blasts' slabs, the fires,
// the air and the tall air.  The front moves the smoke where the advect pass reads it from, thins
// what it stretches and mixes away, and leaves eddies behind it (shockShift, VolumeShockCore): a
// Scud lands in the last one's smoke and pushes it aside, a second nuke scatters the first's cloud,
// whose cap then lets go of its shape.
//
// Nothing here is read by the logic or draws a random number: every variation is an integer hash.
// The D3D9 and SDL3 paths, -headless and every run with the setting Off keep the billboards, and
// with it Off Takes, Hides and Render return before touching anything.  With it on, every
// system of a blast this takes draws no billboards, its GPU overflow particles included; the
// shell keeps its additive fire and flash cards, the tank and the nuclear blast have none, and
// the tank's ground ring and the smoke trails behind its flying scrap are taken with them.  The
// scrap itself, and every gun's muzzle smoke but a tank gun's, stay the game's own.
//
// A tank gun's shot has a short volume of its own on a free slab, a few at once: a cone of flame
// out of the muzzle along the barrel's aim, a little grey smoke ahead of it and a dust cloud blown
// off the ground round the tank's front, gone in a second and a half.  A shell striking armour
// throws one burst of spark streaks out of the point it hit, the game's own particles, which Gather
// starts; the stock sparks of that hit are hidden.
#include "Common/GlobalData.h"
#include "Common/Debug.h"
#include "Common/LogicThread.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "Common/Geometry.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "hlod.h"
#include "mesh.h"
#include "meshmdl.h"
#include "Common/DrawModule.h"
#include "W3DDevice/GameClient/VolumeSmoke.h"
#include "W3DDevice/GameClient/VolumeFireCore.h"
#include "W3DDevice/GameClient/VolumeShockCore.h"
#include "GameLogic/PartitionManager.h"
#include "Platform/HazardCloud.h"
#include "Platform/HazardCloudCore.h"
#include "dx11backend.h"
#include "dx11device.h"
#include "dx11runtime.h"
#include "dx11resource.h"

#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// Two atlases: the small one for tanks and shells, the big one for nuclear blasts.  The slab
// counts and the small one's height are the setting's (Quality below); a change of setting builds
// the atlases again.
// A third, the fire tier, holds one burning building or wreck a slab (VolumeFireCore's sites).
struct TierShape { unsigned gx, gy, gz, slabs, first; };
TierShape TierShapes[3] = { { 48, 48, 40, 24, 0 }, { 96, 96, 80, 2, 24 }, { 32, 32, 48, 12, 26 } };
const unsigned MaxVolumes = 38;		// all three tiers' slabs at High, the small tier's first, the fires' last
const unsigned MaxSlabs = 24;		// the constant arrays' length, the same in both tiers
const unsigned MaxPuffs = 28;	// a tank's 28 (base, jets, stem, dark base masses and their rims); PUFFS in the HLSL is the same
const unsigned MaxMovers = 16;		// High's; Low takes the nearest 8
const unsigned MaxShocks = 16;		// blast fronts running at once (VolumeShockCore), High's; Low takes the newest 8; SHOCKS in the HLSL is the same
const unsigned NoiseSide = 32;

// The VolumeSmoke setting, Low and High (Off draws nothing).  Low keeps High's grids, since every
// timeline constant is written in cells, and halves what a frame pays instead: half as many
// volumes at once, 8 pressure sweeps for 14, a march step half as long again.
// ponytail: a coarser Low grid needs the timelines' cell constants scaled with it; do that if
// Low still costs too much on a weak card.
// High's small grid is 56 cells tall where Low's is 40, so a tank's plume has room to climb to
// twice its base's width under the grid's top (Low keeps the shorter plume and the cheaper grid).
// The fires' constants are in world units and are taken to cells per slab, so Low's fire grid is a
// coarser one over the same world box, stepped every other logic frame.
struct Quality { unsigned slabs[2]; unsigned sweeps; float stepCells; unsigned smallHeight;
	unsigned fireSlabs, fireSide, fireHeight, fireSweeps, fireEvery; float fireLight; };
const Quality Qualities[3] = { { { 0, 0 }, 0, 0.0f, 40, 0, 8, 8, 0, 1, 0.0f }, { { 12, 1 }, 8, 0.9f, 40, 6, 24, 32, 6, 2, 0.0f },
	{ { 24, 2 }, 14, 0.6f, 56, 8, 32, 48, 8, 2, 0.6f } };	// sweeps even, so the pressure ends where it started
// (High's fires were 12 slabs, 10 sweeps and a step every logic frame: eight burning buildings cost
// 1.85 ms of GPU time over the smoke alone, past the 1.5 the fires were allowed; 8 and 8 alone moved
// nothing, with six of them burning at once)	// sweeps even, so the pressure ends where it started
int level = 0;		// the setting the atlases were last made for

// The world, in game units (a tank is about 30, 7 m: a unit is about 0.23 m).
const float Gravity = 42.0f;			// units/s^2
const float DustSettle = 4.0f;			// units/s
const float WindX = 5.0f, WindY = 3.0f;	// units/s; the game has no wind for smoke to read
// The sim, in cells and seconds.
const float Buoyancy = 9.0f;			// cells/s^2 of lift at heat 1
const float Weight = 1.0f;				// cells/s^2 of sink at density 1
const float Vorticity = 3.0f;
const float Drag = 0.045f;				// per cell/s of speed
const float VelocityHalfLife = 3.0f;
const float HeatHalfLife = 1.8f;
const float DustHalfLife = 3.0f;			// (6 halved: a fallen War Factory's tan lobes lay beside it for 10 s)
// Dust lying on the ground thins on a clock of its own (AdvectPass keeps a slab's share a step): 0.38 s
// is the 0.97 a step at 60 every blast had, and a fallen building's ground roll died where it was
// laid under it.  A building's fall and a launch roll out along the ground on the longer one.
const float GroundDustHalfLife = 0.38f, GroundRollHalfLife = 1.5f;
// Cold dust's weight in cells/s^2 at density 1, over the shared Weight: a building's ground roll
// slumps and spreads as a gravity current (about 5 cells/s two cells deep at 10; at 20 it lay as a
// flat sheet), and a shell's thrown dirt falls back out of the air; at the shared 1 a Rocket Buggy's
// dirt fountains stood beside a fallen War Factory as tan fingers for ten seconds.
const float RollWeight = 10.0f, DirtWeight = 30.0f, TankMassWeight = 0.2f;
const float CapSpillHalfLife = 1.0f;	// a nuclear cap's soot once the flow has carried it out of the cap
const float FireballSeconds = 0.6f;		// a building's fall's fireball, burst to cooled
const float FireballHalfLife = 0.15f;	// and its heat after that, over the shared HeatHalfLife (the column's fire is laid again each step)
const float DebrisDustSeconds = 0.6f;	// how long after a fall its flying debris is given dust trails (Gather)
const float StructureSootHalfLife = 0.75f;	// a building's blast's soot: at 7 a fall's smoke was on screen for 14 s, at 3 for 7 to 10, at 1.5 for 5 to 6
// A tank's soot from 1.25 s on, once its column is falling: its black column is gone from the play
// camera about 2 s after the death and a thin smoulder fades out by 3.5 s (the volume lives 4.25, its
// end fade 1.5 s for a tank, so it starts as the smoulder's ends and does not dim it twice).  The
// owner halved it twice (2026-10-29) from 8 and 14 s, which had matched the game's billboard column
// (ReforgedTankSmoke, 1.5 s of puffs living 5 to 6 s); before that it hung for half a minute.
const float TankSootThinFrom = 1.25f, TankSootHalfLife = 0.375f;
const float ShortEndFade = 1.5f;		// seconds a tank's, a building's, a launch's or a shell's volume fades over at its end; the rest 3
// A tank gun's shot (Muzzle): its fireball's heat goes in a few frames, where a blast's is kept for
// seconds and lifted its smoke away as a glowing ball; its volume lives 1.6 s, the last 0.6 fading.
// At most this many at once (Low, High), on free slabs only: past that a shot keeps the game's own flash.
const float MuzzleHeatHalfLife = 0.07f, MuzzleLife = 1.6f, MuzzleEndFade = 0.6f;
const unsigned MuzzleCap[3] = { 0, 3, 6 };
// A nuclear cap another blast has let go of (VolumeShock::capHold) is free smoke, thinning out at this
// half-life, and its volume ends this long after.
const float NukeDisperseHalfLife = 4.0f, NukeDisperseLife = 8.0f;
const float ShadeLight = 0.26f;			// the light smoke keeps where the sun does not reach it
const float SunShare = 0.75f;			// and what the sun adds where it does
const float Extinction = 0.16f;			// per world unit at density 1

// The fires: fuel the burning surfaces give off, lit by the heat round it, burning into heat and
// soot (FireAdvectPass).  Heat is 300 K + 1200 K a unit, so 1 is a 1500 K flame.
const float FireBurnRate = 5.0f;		// a second, of the fuel in a cell hot enough to burn
const float FireIgnition = 0.45f;		// heat, about 840 K
const float FireRich = 1.5f;			// fuel at which a cell's air is used up: a diffusion flame burns on its envelope
const float FireSootYield = 2.0f;		// soot a unit of fuel burned rich or quenched leaves: a building's fire smokes dark (0.4 and 0.8 left no smoke to see)
const float FireQuench = 3.0f;			// a second: unburned fuel that has cooled under the ignition turns to smoke
const float FireTongueRise = 1.2f;		// flame heights a second the tongues climb (FireMarchPass)
const float FireRadiation = 1.0f;		// heat a second a sooty cell radiates away at 1500 K, going as T^4
const float FireSootBurn = 0.5f;		// a second: soot burns away where it is hotter than about 1100 K and has air (at 1.5 little left the tips)
const float FireExpansion = 0.5f;		// cells a second of outflow a unit of fuel burned makes, the gas expanding
const float FireHeatHalfLife = 0.5f;	// mixing with the air round it, seconds
const float FireSootHalfLife = 1.0f;	// the fire's smoke thins as it rises: gone about 3 s off the flame, as a building blast's
// Boussinesq's lift, g (T - T0) / T0, is 42 x 4 units/s^2 at heat 1; the cells cannot hold the
// mixing that holds a real flame back, so it is taken at 0.3 of that.  (At 0.15 the smoke off the
// tips rose 6 to 10 units a second and the wind laid it along the ground; a real plume over a 5 m
// fire climbs at about 7 m/s, 30 units.)
// ponytail: the 0.3 is by eye against the stock flames' height; measure it if a flame reads wrong.
const float FireLift = 42.0f * 4.0f * 0.3f;
const float FireExit = 4.0f;			// units/s: the gas leaving a burning surface
const float FireTurbulence = 10.0f;		// units/s^2: the sub-cell eddies' kick in hot gas (at 60 it tore the flames apart before they lit)
const float FireVorticity = 5.0f;		// confinement in the fire tier: more than a blast's 3, so the sheet curls into tongues
const float FirePilot = 0.6f;			// a pyrolysing surface's heat (about 1000 K), so its fuel lights where it leaves (at 1500 K the puff itself glowed as a ball)
const float FireSurfaceSoot = 0.3f;		// soot the surface gives off with each unit of fuel: what glows at a flame's foot
const float FireWarmSeconds = 0.6f;		// a new fire's slab runs this long under the billboards before taking them
const float FireGain = 1.5f;			// a 1500 K body's light against the scene's white (at 2, before the tongues, the flames burned out to yellow balls)
const float FireExposure = 0.6f;		// the black body's brightness is taken to this power: 800 K is a hundredth of 1500 K's
const float FireEndFade = 1.5f;

const DXGI_FORMAT VelocityFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;	// xyz cells/s, w heat
const DXGI_FORMAT DensityFormat = DXGI_FORMAT_R16G16_FLOAT;			// soot, dust
const DXGI_FORMAT ScalarFormat = DXGI_FORMAT_R16_FLOAT;

enum Kind { TankDeath, Shell, Airburst, Nuclear, Air, Structure, Fire, Launch, Muzzle };

template <class T> void drop(T *&object)
{
	if (object) {
		object->Release();
		object = nullptr;
	}
}

struct Field {
	ID3D11Texture3D *texture = nullptr;
	ID3D11ShaderResourceView *view = nullptr;
	ID3D11UnorderedAccessView *write = nullptr;
};

void drop(Field &field)
{
	drop(field.write);
	drop(field.view);
	drop(field.texture);
}

bool make(ID3D11Device *device, Field &field, DXGI_FORMAT format, const TierShape &shape)
{
	D3D11_TEXTURE3D_DESC description = {};
	description.Width = shape.gx;
	description.Height = shape.gy;
	description.Depth = shape.gz * shape.slabs;
	description.MipLevels = 1;
	description.Format = format;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	return SUCCEEDED(device->CreateTexture3D(&description, nullptr, &field.texture))
		&& SUCCEEDED(device->CreateShaderResourceView(field.texture, nullptr, &field.view))
		&& SUCCEEDED(device->CreateUnorderedAccessView(field.texture, nullptr, &field.write));
}

// The pass constants, one set a tier, indexed by slab.  Volumes: x live, y emitters, z world units
// a cell, w one on the step that starts it, so the slab's last tenant is not read.  Blasts: the
// hot core's centre and radius in cells.  Power, this step: the ground skirt's outward speed in
// cells/s, the core's soot and heat, the skirt's dust.  Shape: the vortex ring's speed and
// radius, the ceiling the buoyancy fades out under, the stem's speed.  Motion: the core's rise in
// cells/s.  Three float4 an emitter: cell position and radius; the velocity it pushes the flow
// to and its soot a second; its heat, its dust a second, how hard it pushes.
struct SimConstants {
	float Step[4];			// dt seconds, seconds, dust settling in cells/s, unused
	float Forces[4];		// buoyancy, weight, vorticity, drag
	float Keep[4];			// heat, velocity, dust kept over this step, unused
	float Sun[4];			// towards the sun, extinction per world unit
	float Wind[4];			// world units/s on x and y
	float Volumes[MaxSlabs][4];
	float Blasts[MaxSlabs][4];
	float Power[MaxSlabs][4];
	float Shape[MaxSlabs][4];
	float Motion[MaxSlabs][4];
	// A nuclear blast's mushroom, the shape its cells are drawn towards (MushroomPass).  Cap: the soot
	// it holds, its centre's height, its radius across and its half height, in cells.  Stem: the
	// dust it holds, its radius, the height of its top, how fast a second the cells reach the shape.
	float Cap[MaxSlabs][4];
	float Stem[MaxSlabs][4];
	float Puffs[MaxSlabs * MaxPuffs * 3][4];
	// each slab's world corner, for the movers and the blast fronts below; w, which fronts reach the
	// slab this step, a bit each (a float holds the 16 bits exactly)
	float Origins[MaxSlabs][4];
	float MoverCount[4];
	float Movers[MaxMovers * 4][4];	// the units that push the air this step, in world units (Mover)
	float Thin[MaxSlabs][4];		// a blast's soot kept over this step (a building's thins; 1 for the rest), its cold dust's weight, its ground dust kept, its heat kept, its heat kept on top of Keep.x (a gun's; 1 for the rest)
	// A fire's slab (read by the fire tier's programs only): burn rate, heat a unit of fuel gives, soot
	// yield, ignition heat; the fuel that starves it, radiation, soot burn, expansion; its lift in
	// cells/s^2 at heat 1.
	float Fire[MaxSlabs][4];
	float Fire2[MaxSlabs][4];
	float Fire3[MaxSlabs][4];
	// The blast fronts running this step (shockShift), world units: the centre and the front's radius;
	// its width, its particle speed a second, the share it mixes away a second, its stir in units/s^2.
	float ShockCount[4];
	float Shocks[MaxShocks * 2][4];
	// A nuclear cap's soot and stem dust kept this step once they leave its shape (MushroomPass):
	// CapSpillHalfLife, stretched to NukeDisperseHalfLife as another blast loosens the cap.
	float Spill[MaxSlabs][4];
};

struct DrawConstants {
	float WorldFromClip[16];
	float Camera[4];		// position, seconds
	float Viewport[4];		// as HazardCloud's: x + 0.5, y + 0.5, width, height
	float Box[4];			// world corner, world units a cell
	float Slab[4];			// slab, fade, march step in cells, extinction
	float Soot[4];			// soot colour, the fire's gain
	float Dust[4];			// dust colour
	float SunColor[4];
	float Ambient[4];
	float SunDir[4];		// towards the sun
	float Blast[4];			// the core's centre and radius in cells
	float Effects[4];		// flash, condensation shell radius in cells and opacity, the collar's opacity
	float Extra[4];			// the collar's radius in cells, the flash's height over its width, its radius in cells, the collar's height in cells
	float ClipLow[4];		// the world box the march keeps to: the grid's, or for the air where smoke is
	float ClipHigh[4];
	float Look[4];			// the light the shade side keeps, the fine billows' depth, the sun's share, how far thin smoke is spared
	// A fire's only (FireMarchPass): its light's gain, ignition heat, the fuel that starves it, the
	// black body's exposure; the reaction zone's blue and how much the flame lights its smoke.
	float FireLook[4];
	float FireTint[4];
};

// What every program starts with, after the tier's GX, GY, GZ and SLABS: the fade that keeps smoke
// off the box's walls, round about the column's axis so no square corner shows.
// The air's box (AIR) holds four density channels: soot, dust, and each times its brightness, so a
// cell's grey advects with its soot.  A blast's holds two and takes its colours from the timeline.
const char *const Grid = R"HLSL(
#ifdef AIR
#define DEN float4
float walls(float3 cell)
{
    float2 inside = min(cell.xy, float2(GX - 1, GY - 1) - cell.xy);
    return smoothstep(0, 4, inside.x) * smoothstep(0, 4, inside.y) * smoothstep(0, 6, GZ - 1 - cell.z);
}
#else
#define DEN float2
float walls(float3 cell)
{
    float2 across = (cell.xy - float2(GX - 1, GY - 1) * 0.5) / (GX * 0.5 - 1);
    // (from 0.6 of the way out a building's ground roll was gone by one and a half footprints)
    return (1 - smoothstep(0.8, 1.0, length(across))) * smoothstep(0, 10, GZ - 1 - cell.z);
}
#endif
uint hash(uint3 v)
{
    uint h = v.x * 73856093u ^ v.y * 19349663u ^ v.z * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
float lattice(int3 p, uint seed)
{
    return (hash(uint3(p + 4096) + uint3(seed * 7919u, seed * 104729u, seed * 1299709u)) & 1023) / 1023.0;
}
// Smooth value noise on a unit lattice, for the lumps the hot core is born with.
float valueNoise(float3 x, uint seed)
{
    int3 i = (int3)floor(x);
    float3 f = x - floor(x);
    f = f * f * (3 - 2 * f);
    float a = lerp(lattice(i, seed), lattice(i + int3(1, 0, 0), seed), f.x);
    float b = lerp(lattice(i + int3(0, 1, 0), seed), lattice(i + int3(1, 1, 0), seed), f.x);
    float c = lerp(lattice(i + int3(0, 0, 1), seed), lattice(i + int3(1, 0, 1), seed), f.x);
    float d = lerp(lattice(i + int3(0, 1, 1), seed), lattice(i + int3(1, 1, 1), seed), f.x);
    return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}
)HLSL";

const char *const Prelude = R"HLSL(
#define PUFFS 28
#define VOLS 24
#define SHOCKS 16
cbuffer Sim : register(b0)
{
    float4 Step;
    float4 Forces;
    float4 Keep;
    float4 SunToward;
    float4 Wind;
    float4 Volumes[VOLS];
    float4 Blasts[VOLS];
    float4 Power[VOLS];
    float4 Shape[VOLS];
    float4 Motion[VOLS];
    float4 Cap[VOLS];
    float4 Stem[VOLS];
    float4 Puffs[VOLS * PUFFS * 3];
    float4 Origins[VOLS];
    float4 MoverCount;
    float4 Movers[16 * 4];
    float4 Thin[VOLS];
    float4 Fire[VOLS];
    float4 Fire2[VOLS];
    float4 Fire3[VOLS];
    float4 ShockCount;
    float4 Shocks[SHOCKS * 2];
    float4 Spill[VOLS];
};
SamplerState Linear : register(s0);
float3 uvwOf(float3 cell, uint slab)
{
    cell = clamp(cell, float3(0, 0, 0), float3(GX - 1, GY - 1, GZ - 1));
    return float3((cell.x + 0.5) / GX, (cell.y + 0.5) / GY, (cell.z + 0.5 + slab * GZ) / (GZ * SLABS));
}
int4 at(int3 cell, uint slab)
{
    return int4(clamp(cell, int3(0, 0, 0), int3(GX - 1, GY - 1, GZ - 1)) + int3(0, 0, slab * GZ), 0);
}
// The ground and the buildings, as a height a column: the top of what is solid, in cells over the
// box's floor.  A cell whose centre is under it is solid: no smoke in it, no flow into it.
Texture2D<float> Floor : register(t4);
float groundAt(int2 xy, uint slab)
{
    xy = clamp(xy, int2(0, 0), int2(GX - 1, GY - 1));
    return Floor.Load(int3(xy.x, xy.y + slab * GY, 0));
}
bool solid(int3 c, uint slab) { return c.z < 0 || c.z + 0.5 < groundAt(c.xy, slab); }
// A flow that would run into a solid neighbour stops at its face; along it, it goes on.
float3 noPenetration(float3 v, int3 c, uint slab)
{
    if (solid(c + int3(1, 0, 0), slab)) v.x = min(v.x, 0);
    if (solid(c - int3(1, 0, 0), slab)) v.x = max(v.x, 0);
    if (solid(c + int3(0, 1, 0), slab)) v.y = min(v.y, 0);
    if (solid(c - int3(0, 1, 0), slab)) v.y = max(v.y, 0);
    if (solid(c - int3(0, 0, 1), slab)) v.z = max(v.z, 0);
    return v;
}
float3 jitterAt(uint3 id)
{
    uint h = hash(uint3(id.xy, id.z + (uint)(Step.y * 30) * 4099u));
    return float3(h & 1023, (h >> 10) & 1023, (h >> 20) & 1023) / 511.5 - 1;
}
// The skirt: a thin outflow along the ground from under the blast, the core's footprint swelling
// evenly and the 1/r sheet outside it, lobed by a hash of the slab, as no real blast pushes evenly.
float skirtLayer(float3 cell, uint slab)
{
    return 1 - smoothstep(1.5, 4.0, cell.z - groundAt((int2)cell.xy, slab) + 1.5);
}
float2 skirtFlow(float3 cell, uint slab)
{
    float4 blast = Blasts[slab];
    float speed = Power[slab].x;
    float2 offset = cell.xy - blast.xy;
    float r = max(length(offset), 0.5);
    float s = r < blast.w ? speed * r / blast.w : speed * blast.w / r;
    uint h = hash(uint3(slab, 17, 91));
    float3 phase = float3(h & 255, (h >> 8) & 255, (h >> 16) & 255) * (6.2831853 / 256);
    float a = atan2(offset.y, offset.x);
    s *= 1 + 0.35 * sin(3 * a + phase.x) * cos(2 * a + phase.y) + 0.15 * sin(5 * a + phase.z);
    return offset / r * s;
}
#define CELL_HEADER \
    uint slab = id.z / GZ; \
    int3 c = int3(id.x, id.y, id.z % GZ); \
    float4 volume = Volumes[slab]; \
    if (volume.x < 0.5) return;
)HLSL";

const char *const CurlPass = R"HLSL(
Texture3D<float4> Vel : register(t0);
RWTexture3D<float4> CurlOut : register(u0);
float3 v(int3 c, uint slab) { return Vel.Load(at(c, slab)).xyz; }
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    if (volume.w > 0.5) { CurlOut[id] = 0; return; }
    float3 dx = v(c + int3(1, 0, 0), slab) - v(c - int3(1, 0, 0), slab);
    float3 dy = v(c + int3(0, 1, 0), slab) - v(c - int3(0, 1, 0), slab);
    float3 dz = v(c + int3(0, 0, 1), slab) - v(c - int3(0, 0, 1), slab);
    float3 curl = 0.5 * float3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x);
    CurlOut[id] = float4(curl, length(curl));
}
)HLSL";

// The units that push the air, read from the client's drawables once a logic frame (Mover in the
// C++).  Four float4 a unit, world units: where it is and its size; its velocity a second and its
// kind; its heading, and a width or a height; a spin sign and a strength.  Each kind pulls the
// flow towards what a real one makes, never the density: the smoke moves because the air does.
const char *const MoverPass = R"HLSL(
#define VEHICLE 0
#define JET 1
#define ROTOR 2
#define WALKER 3
float3 pullTowards(float3 v, float3 target, float k) { return lerp(v, target, saturate(k)); }
void applyMovers(inout float4 vt, inout DEN d, float3 cell, uint slab, float dt)
{
    uint count = (uint)MoverCount.x;
    float size = Volumes[slab].z;
    float3 world = Origins[slab].xyz + (cell + 0.5) * size;
    float floorZ = Origins[slab].z + groundAt((int2)cell.xy, slab) * size;	// the ground or roof under this cell
    float overGround = world.z - floorZ;
    for (uint i = 0; i < count; ++i) {
        float4 a = Movers[i * 4], b = Movers[i * 4 + 1], e = Movers[i * 4 + 2], f = Movers[i * 4 + 3];
        uint kind = (uint)(b.w + 0.5);
        float3 off = world - a.xyz;
        float3 v = b.xyz / size;	// cells a second
        if (kind == VEHICLE) {
            // The hull carries the air with it; behind it a wake runs at 0.6 of its speed and turns.
            float2 h = e.xy, side = float2(-h.y, h.x);
            float along = dot(off.xy, h), across = dot(off.xy, side), up = world.z - a.z;
            float L = a.w, W = e.z, H = e.w;
            if (up < -size || up > H * 2.5 || abs(across) > W * 2 + size || along > L + size || along < -L * 5) continue;
            if (abs(along) < L && abs(across) < W && up < H) { vt.xyz = pullTowards(vt.xyz, v, 0.8); continue; }
            if (along < -L * 0.5) {
                float behind = saturate((-along - L) / (L * 4));
                float k = (1 - behind) * (1 - saturate(abs(across) / (W * 1.8))) * (1 - saturate(up / (H * 2.2)));
                float3 spin = float3(side * (H * 0.6 - up), across) * (f.x * f.y * 0.8 / max(W, 1));
                vt.xyz = pullTowards(vt.xyz, v * 0.6 + spin / size, 0.35 * k);
            }
        } else if (kind == JET) {
            // A body of air dragged along its last 0.3 s, two counter-rotating vortices off the wing
            // tips over its last 1.5 s with the downwash between them, all fading with its height.
            float3 path = b.xyz * 1.5;		// from the nose back over 1.5 s
            float along = dot(-off, path) / max(dot(path, path), 1e-3);
            if (along < -0.05 || along > 1.05) continue;
            float s = saturate(along);
            float3 r = world - (a.xyz - path * s);
            float span = a.w;
            float2 side = normalize(float2(-path.y, path.x) + float2(1e-4, 0));
            float lateral = dot(r.xy, side);
            if (abs(lateral) > span * 3 || r.z > span * 3) continue;
            float fade = exp(-e.z / 150) * (1 - s);
            if (s < 0.2 && dot(r, r) < span * span) vt.xyz = pullTowards(vt.xyz, v * 0.5, 0.4 * fade);
            float strength = length(b.xyz) * 0.12 / size * f.y;
            for (int tip = -1; tip <= 1; tip += 2) {
                float2 rr = float2(lateral - tip * span, r.z);
                float rho = max(length(rr), span * 0.25);
                float swirl = strength * span * 0.5 / rho * exp(-rho * rho / (span * span * 2.5));
                float2 t = float2(-rr.y, rr.x) / rho * tip;
                vt.xyz = pullTowards(vt.xyz, vt.xyz + (float3(side * t.x, t.y) * swirl), 0.5 * fade);
            }
            // the sheet pushed down between the tips, reaching the ground under a low pass
            if (abs(lateral) < span && r.z < 0)
                vt.z = lerp(vt.z, -strength * 0.6, saturate(0.4 * fade * exp(r.z / 90)));
        } else if (kind == ROTOR) {
            // A rotor's downwash: a column 1.2 radii wide under the disc, as strong as the ground is
            // close; at the ground it turns into a thin sheet running outwards and lifts a ring of
            // dust about two radii out.
            float R = a.w, height = e.z, close = f.y;
            float2 flat = off.xy;
            float rho = length(flat);
            float below = a.z - world.z;
            if (rho > R * 5 || below < -R) continue;
            float wash = 45.0 / size * close;
            float columnR = R * 1.2 + max(below, 0) * 0.15;
            if (below > -R * 0.5 && rho < columnR && overGround > size * 1.5) {
                float k = (1 - rho / columnR) * (below < 0 ? 0.3 : 1);
                vt.xyz = pullTowards(vt.xyz, float3(vt.xy * 0.5, -wash), 0.6 * k);
            }
            float sheet = 1 - smoothstep(size, size * 3 + 8, overGround);
            if (sheet > 0 && rho > R * 0.3) {
                float2 outwards = flat / rho * wash * min(1.6, R * 1.2 / rho);
                float reach = 1 - smoothstep(R * 3, R * 5, rho);
                vt.xyz = pullTowards(vt.xyz, float3(outwards, 0), 0.6 * sheet * reach);
            }
            // the dust it lifts: a thin band, topped up only while thinner than a light haze
            float ring = exp(-pow((rho - R * 2) / (R * 0.5), 2)) * (1 - smoothstep(size, size * 3 + 8, overGround));
            if (ring > 0.05) {
                vt.z = max(vt.z, wash * 0.25 * ring);
                float lift = 0.5 * ring * close * dt * saturate(1 - d.y / 0.6);
                d.y += lift;
#ifdef AIR
                d.w += lift * 0.8;
#endif
            }
        } else if (kind == WALKER) {
            float k = 1 - saturate(length(off) / (a.w * 2 + size));
            vt.xyz = pullTowards(vt.xyz, v, 0.4 * k);
        }
    }
}
// The blast fronts (VolumeShockCore): this step's shift of the smoke by the fronts passing the cell,
// in cells, for the advect pass's backtrace.  A front is a pressure pulse, which the projection would
// take out of the velocity, so it moves the smoke where it is read from instead and never enters the
// flow; `kept` is the density the cell keeps (the shell's stretching, a compressible flow's
// continuity, and what it mixes away), `stir` the eddy kick behind the front in units/s^2.
// Origins.w says which fronts reach this slab.  A front is never thinner than the grid holds; its
// displacement is kept when it is widened, and its shift is held to 1.5 cells a step.
float3 shockShift(float3 cell, uint slab, float dt, inout float kept, inout float stir)
{
    float size = Volumes[slab].z;
    float3 world = Origins[slab].xyz + (cell + 0.5) * size;
    float3 shift = 0;
    float spread = 0;
    uint mask = (uint)Origins[slab].w;
    while (mask) {
        uint i = firstbitlow(mask);
        mask &= mask - 1;
        float4 a = Shocks[i * 2], b = Shocks[i * 2 + 1];
        float wide = max(b.x, 1.5 * size);
        float3 off = world - a.xyz;
        float r = length(off);
        float x = (r - a.w) / wide;
        if (abs(x) > 3) continue;
        float g = exp(-x * x);
        float moveBy = b.y * (b.x / wide) * g * dt;
        float rr = max(r, size);
        shift += off / rr * moveBy;
        spread += moveBy * (2 / rr - 2 * x / wide);
        kept *= exp(-b.z * g * dt);
        stir += b.w * g;
    }
    float moved = length(shift) / size;
    float held = moved > 1.5 ? 1.5 / moved : 1;
    // the stretching as the shift really made it; exp, so a squeeze and the stretch after it cancel
    kept *= clamp(exp(-spread * held), 0.5, 1.5);
    return shift / size * held;
}
// the eddies a front leaves behind it, cells/s
float3 shockStir(float3 cell, float stir, float dt, float size)
{
    float3 q = cell / 3 + float3(0, 0, -Step.y * 0.5);
    return (float3(valueNoise(q, 61), valueNoise(q + 4.3, 67), valueNoise(q + 8.9, 71)) - 0.5) * (stir * dt / size);
}
)HLSL";

// The literal is cut in two: MSVC refuses one longer than 16 KB (C2026).
const char *const AdvectPass = R"HLSL(
Texture3D<float4> Vel : register(t0);
Texture3D<DEN> Den : register(t1);
Texture3D<float4> Curl : register(t2);
RWTexture3D<float4> VelOut : register(u0);
RWTexture3D<DEN> DenOut : register(u1);
RWTexture3D<float> PresOut : register(u2);
#ifdef AIR
// The particles of every smoke system the air took, as spheres in cells: centre and radius, then
// the soot and the dust they stand for and their brightness.  Each 8 by 8 by 8 brick of cells has its
// list of the spheres that reach it.
StructuredBuffer<float4> Sources : register(t5);
StructuredBuffer<uint2> TileRange : register(t6);
StructuredBuffer<uint> TileIndex : register(t7);
#endif
float w(int3 c, uint slab) { return Curl.Load(at(c, slab)).w; }
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float dt = Step.x;
    float3 cell = float3(c);
    float4 blast = Blasts[slab];
    float4 power = Power[slab];
    float4 shape = Shape[slab];
    float4 cap = Cap[slab];		// a nuclear mushroom's, else nothing
    float4 vt = 0;
    DEN d = 0;
    if (volume.w > 0.5) {
        PresOut[id] = 0;
    } else {
        // the blast fronts move the smoke as they pass (shockShift)
        float shockKept = 1, stir = 0;
        float3 back = cell - Vel.Load(int4(id, 0)).xyz * dt - shockShift(cell, slab, dt, shockKept, stir);
        vt = Vel.SampleLevel(Linear, uvwOf(back, slab), 0);
        DEN carried = Den.SampleLevel(Linear, uvwOf(back, slab), 0);
        DEN settled = Den.SampleLevel(Linear, uvwOf(back + float3(0, 0, Step.z * dt), slab), 0);	// dust settles
        d.x = carried.x;
        d.y = settled.y;
#ifdef AIR
        // Soot thins out in the open air; a missile's trail (told by its brightness, over a
        // puff's) at its own rate, quicker low down, so a Rocket Buggy salvo's twenty trails
        // do not hang over the field as a tan haze for seconds.
        float trail = saturate((carried.z / max(carried.x, 0.01) - 1.02) / 0.2);
        float keep = lerp(Keep.w, Wind.z, trail);
        d.z = carried.z * keep;
        d.w = settled.w * Keep.z;
        d.x *= keep;
#endif
        d *= shockKept;	// every channel, so a cell's grey goes with its soot
        if (stir > 0) vt.xyz += shockStir(cell, stir, dt, volume.z);
        // Heat lifts, fading out under the ceiling, where a rising cap stops and spreads.
        float lift = Forces.x * vt.w * saturate((shape.z - cell.z) / max(shape.z * 0.3, 1));
#ifdef AIR
        lift *= 1 - trail;	// a nozzle's flame lights its trail, it does not lift it
#endif
        vt.z += dt * (lift - Forces.y * d.y);	// soot is light; dust and dirt are what weigh
#ifndef AIR
        // Cold dust is heavier than the air round it (Thin.y a slab's own weight on it): a fallen
        // building's slumps and runs out along the ground as a gravity current, and a shell's thrown
        // dirt falls back out of the air instead of standing as fingers over the crater.
        vt.z -= dt * Thin[slab].y * d.y * (1 - saturate(vt.w));
#endif
        float3 grad = 0.5 * float3(w(c + int3(1, 0, 0), slab) - w(c - int3(1, 0, 0), slab),
            w(c + int3(0, 1, 0), slab) - w(c - int3(0, 1, 0), slab),
            w(c + int3(0, 0, 1), slab) - w(c - int3(0, 0, 1), slab));
        float length2 = dot(grad, grad);
        if (length2 > 1e-10)
            vt.xyz += dt * Forces.z * cross(grad * rsqrt(length2), Curl.Load(int4(id, 0)).xyz);
        vt.xyz /= 1 + Forces.w * length(vt.xyz) * dt;
        vt.xy += (Wind.xy / volume.z - vt.xy) * saturate(dt * 0.35 * saturate(cell.z / 6.0));
#ifdef AIR
        // A trail's smoke is stirred by a slow eddy of its own, about a cell a second, so a Scud
        // Storm's columns billow and spread as they age instead of standing as straight pillars.
        if (trail > 0) {
            float3 q = cell / 5 + float3(0, 0, -Step.y * 0.3);
            float3 eddy = float3(valueNoise(q, 31), valueNoise(q + 7.1, 37), valueNoise(q + 3.3, 41)) - 0.5;
            vt.xyz += eddy * (Wind.w * trail * dt);
        }
#endif
        vt.xyz *= Keep.y;
#ifdef AIR
        vt.w *= lerp(Keep.x, Wind.z * Wind.z * Wind.z, trail);	// a trail's heat is its nozzle's, gone in a few frames
#else
        vt.w *= Keep.x * Thin[slab].w;	// a gun's fireball cools in a few frames (Thin.w under 1 for its slab only)
#endif
        d.y *= Keep.z;
#ifndef AIR
        d.x *= Thin[slab].x;	// a building's smoke thins out as it spreads
#endif
    }
    float2 offset = cell.xy - blast.xy;
    float rho = length(offset);
    float2 radial = rho > 0.01 ? offset / rho : float2(0, 0);
    float layer = skirtLayer(cell, slab);
    // The ground skirt, and the dust it lifts.
    if (power.x > 0) {
        float reach = 1 - smoothstep(blast.w * 3, blast.w * 7, rho);
        float3 flow = float3(skirtFlow(cell, slab), 0) + jitterAt(id) * power.x * 0.2;
        vt.xyz = lerp(vt.xyz, flow, 0.6 * layer * reach);
    }
    // (thinning to nothing past the footprint: a building's footprint of a dozen cells laid it as a
    // disc with a hard round rim)
    if (power.w > 0 && rho < blast.w * 1.6)
        d.y = max(d.y, power.w * layer * (1 - smoothstep(blast.w, blast.w * 1.6, rho)) * (0.5 + valueNoise(cell / 2, slab + 5)));
    // A shock front: a ring of dust racing out along the ground, far past the fireball, slowing
    // as it goes (Motion: front radius, its speed, its dust).  Buildings in its way stop it.
    float4 motion = Motion[slab];
    if (motion.z > 0) {
        float across = (rho - motion.y) / 3;
        // a nuclear surge stands taller at its front, a rolling wall of dust that still runs
        // between the buildings rather than over them
        float wall = cap.x > 0 ? 1 - smoothstep(1.5, 4.5, cell.z - groundAt((int2)cell.xy, slab)) : layer;
        float band = exp(-across * across) * wall;
        vt.xy = lerp(vt.xy, radial * motion.z * (1 + 0.3 * jitterAt(id).x), 0.5 * band);
        d.y = max(d.y, motion.w * band * (0.4 + valueNoise(cell / 2.5, slab + 9)));
    }
    // The vortex ring: a torus round the core, up through the middle, out over the top, down
    // outside the rim and in underneath; it rolls the cap and draws the ground air inward.
    if (shape.x > 0) {
        float h = cell.z - (cap.x > 0 ? cap.y : blast.z);	// a mushroom's ring rolls inside its cap
        float dr = rho - shape.y;
        float q = sqrt(dr * dr + h * h);
        float tube = max(shape.y * 0.5, 1.5);
        float k = exp(-q * q / (tube * tube));
        float3 swirl = float3(radial * h, -dr) * (shape.x / max(q, 0.75));
        swirl.z += motion.x;
        vt.xyz = lerp(vt.xyz, swirl, 0.3 * k);
        // a bomb's cap is the ring: soot and heat fed into the tube while it climbs, so it reads as
        // a torus (a nuclear cap has its own shape below)
        if (motion.x > 0.3 && cap.x <= 0) {
            float tubeCore = exp(-q * q / (tube * tube * 0.35));
            d.x = max(d.x, 1.2 * tubeCore * (0.4 + 1.2 * valueNoise(cell / 2.5 + Step.y * 0.3, slab + 3)));
            vt.w = max(vt.w, 0.9 * tubeCore);
        }
    }
    // The stem: air drawn in along the ground under the cap and up the axis into it.
    if (shape.w > 0 && cap.x <= 0) {
        float ring = max(shape.y, 2);
        float ground = layer * (1 - smoothstep(ring * 1.5, ring * 3, rho));
        float axis = exp(-rho * rho / (ring * ring * 0.2)) * (cell.z < blast.z ? 1 : 0);
        float3 stem = float3(-radial * shape.w * 0.6, 0) * ground + float3(0, 0, shape.w) * axis;
        vt.xyz = lerp(vt.xyz, stem, 0.15 * max(ground, axis));
    }
    // The hot core, lumpy from the start so the rising cap breaks into billows.
    if ((power.y > 0 || power.z > 0) && cap.x <= 0) {
        float r = length(cell - blast.xyz);
        if (r < blast.w) {
            float k = sqrt(1 - r / blast.w);
            d.x = max(d.x, power.y * k * (0.35 + 1.3 * valueNoise(cell / 2.5, slab)));
            vt.w = max(vt.w, power.z * k * (0.6 + 0.8 * valueNoise(cell / 3 + 11, slab)));
        }
    }
)HLSL";

// A nuclear blast's mushroom (Cap, Stem; reference/nuclear-real.jpg): the cap a ball of fire on the
// ground that rises, flattens and widens into a dome whose rim curls under, its surface lumped into
// billows that boil slowly; the stem a column of dust from the ground up into it, flared at its
// foot.  The cells are drawn towards that shape at Stem.w a second and never set to it, so the
// flow still rolls and smears them.  Left to the flow alone the cap spread thin and the noise ate
// it inside twelve seconds, while the dust it dropped buried the base.  Soot that leaves the cap
// and dust that leaves the stem thin out (Spill.x, CapSpillHalfLife; slower while another blast's
// front has loosened the cap, so what it pushed off drifts aside), so the shape stays a mushroom and the surge is
// a ring that passes: the stem's dust, heavy, fell out of it and spread over the whole plateau.  The heat is the cap's, hottest in its middle
// and its lumps, so the fireball glows from inside and its rim burns red.
const char *const MushroomPass = R"HLSL(
    if (cap.x > 0 && rho < cap.z * 1.6 + 3) {
        float4 stem = Stem[slab];
        float h = cell.z - cap.y;
        float lumps = valueNoise(cell / 3.2 + float3(0, 0, -Step.y * 0.25), slab + 21) * 0.65
            + valueNoise(cell / 1.6 + float3(0, 0, -Step.y * 0.4), slab + 23) * 0.35;
        // a ball at first, a dome later: flatter underneath, the rim hanging lower than the middle
        float flat = saturate((0.95 - cap.w / cap.z) / 0.53);
        float rim = saturate(rho / cap.z);
        float below = cap.w * (lerp(1.0, 0.5, flat) + 0.7 * flat * rim * rim * rim);
        float e = length(float2(rho / cap.z, h / (h < 0 ? below : cap.w)));
        float surface = e - 0.8 * (lumps - 0.5);	// cauliflower: the surface pushed in and out
        float inCap = 1 - smoothstep(0.88, 1.0, surface);
        float overGround = cell.z - groundAt((int2)cell.xy, slab);
        float stemR = stem.y * (1 + 0.8 * exp(-overGround / 2)) * (0.8 + 0.4 * lumps);
        float inStem = (1 - smoothstep(0.6, 1.0, rho / stemR)) * (1 - smoothstep(stem.z - 2, stem.z + 1, cell.z));
        float rate = saturate(stem.w * dt);
        // no soot laid on the ground itself: the skirt carried the fireball's foot out over the
        // terrain as a brown wash
        float soot = cap.x * inCap * smoothstep(0.5, 2.5, overGround), dust = stem.x * inStem;
        if (soot > d.x) d.x += (soot - d.x) * rate;
        if (dust > d.y) d.y += (dust - d.y) * rate;
        d.x *= lerp(Spill[slab].x, 1, saturate((1.3 - surface) * 3));
        d.y *= lerp(Spill[slab].x, 1, saturate(inStem * 3));
        // white-yellow in the middle, orange-red at the rim, the lumps hotter than the folds between
        vt.w = max(vt.w, power.z * inCap * (1 - 0.6 * saturate(e)) * (0.5 + 1.0 * lumps));
        vt.z = lerp(vt.z, shape.w, 0.15 * inStem);
    } else if (cap.x > 0) d *= Spill[slab].x;
)HLSL";

const char *const AdvectPassEnd = R"HLSL(
    // The timeline's emitters: flame jets, pops, sparks, dirt jets, the burning hull.
    uint count = (uint)volume.y;
    for (uint i = 0; i < count; ++i) {
        uint base = (slab * PUFFS + i) * 3;
        float4 where = Puffs[base];
        float4 motion = Puffs[base + 1];
        float4 heat = Puffs[base + 2];
        float3 away = cell - where.xyz;
        float r2 = dot(away, away) / (where.w * where.w);
        if (r2 >= 1) continue;
        float k = (1 - r2) * (1 - r2);
        d.x += motion.w * k * dt;
        d.y += heat.y * k * dt;
        vt.w = max(vt.w, heat.x * k);
        vt.xyz = lerp(vt.xyz, motion.xyz, saturate(k * heat.z));
    }
#ifdef AIR
    // The cells under a particle are drawn towards the smoke it stands for at Step.w a second, so
    // the volume follows the particles without being pinned to them: what a wake pushes out of a
    // puff comes back over a fraction of a second, not on the next step.
    {
        uint2 range = TileRange[((c.z / 8) * (GY / 8) + c.y / 8) * (GX / 8) + c.x / 8];
        float rate = saturate(Step.w * dt);
        for (uint j = range.x; j < range.x + range.y; ++j) {
            uint index = TileIndex[j];
            float4 where = Sources[index * 2], what = Sources[index * 2 + 1];
            float3 away = cell - where.xyz;
            float r2 = dot(away, away) / (where.w * where.w);
            if (r2 >= 1) continue;
            float k = (1 - r2) * (1 - r2);
            float soot = what.x * k, dust = what.y * k;
            if (soot > d.x) { d.x += (soot - d.x) * rate; d.z += (soot * what.z - d.z) * rate; }
            if (dust > d.y) { d.y += (dust - d.y) * rate; d.w += (dust * what.z - d.w) * rate; }
            vt.w = max(vt.w, what.w * k);	// a nozzle's flame
        }
    }
#endif
    applyMovers(vt, d, cell, slab, dt);
#ifdef AIR
    // A missile's trail is a cell or two across, so its density is several times a puff's for the
    // same opacity; held to 4 like a blast's, a Scud's trail coming down could not be seen.  The
    // brightness channels are held by the same factor, or a capped cell lost its trail's white.
    float2 held = min(d.xy, 8);
    d.zw *= held / max(d.xy, 1e-4);
    d.xy = held;
#else
    d = min(d, 4);
#endif
    d *= lerp(0.92, 1, walls(cell));	// smoke that drifts to the walls thins out there
    // Open sides and top: what reaches them is gone.  The ground lets nothing through and keeps
    // a little of the dust that lands on it.
    if (c.x == 0 || c.y == 0 || c.x == GX - 1 || c.y == GY - 1 || c.z == GZ - 1) { d = 0; vt.w = 0; }
    // The terrain and the buildings: nothing inside them, and no flow into their faces.
    if (solid(c, slab)) { vt = 0; d = 0; }
    else {
        vt.xyz = noPenetration(vt.xyz, c, slab);
#ifdef AIR
        if (solid(c - int3(0, 0, 1), slab)) d.y *= 0.97;
#else
        // a slab's own share of the dust on the ground kept a step (Thin.z): at 0.97 a step dust
        // touching the ground halved in 0.4 s, so a collapse's ground roll died where it was laid
        if (solid(c - int3(0, 0, 1), slab)) d.y *= Thin[slab].z;
#endif
    }
    VelOut[id] = vt;
    DenOut[id] = d;
}
)HLSL";

const char *const DivergencePass = R"HLSL(
Texture3D<float4> Vel : register(t0);
RWTexture3D<float> DivOut : register(u0);
float3 v(int3 c, uint slab) { return Vel.Load(at(c, slab)).xyz; }
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    if (solid(c, slab)) { DivOut[id] = 0; return; }
    // a solid neighbour is a wall: it mirrors the flow, so none crosses its face
    float3 here = v(c, slab);
    float east = solid(c + int3(1, 0, 0), slab) ? -here.x : v(c + int3(1, 0, 0), slab).x;
    float west = solid(c - int3(1, 0, 0), slab) ? -here.x : v(c - int3(1, 0, 0), slab).x;
    float north = solid(c + int3(0, 1, 0), slab) ? -here.y : v(c + int3(0, 1, 0), slab).y;
    float south = solid(c - int3(0, 1, 0), slab) ? -here.y : v(c - int3(0, 1, 0), slab).y;
    float below = solid(c - int3(0, 0, 1), slab) ? -here.z : v(c - int3(0, 0, 1), slab).z;
    // The skirt's footprint is the one place the flow may diverge, what skirtFlow carries, so the
    // projection keeps the outflow instead of cancelling it.
    float source = 0;
    if (Power[slab].x > 0 && length(float2(c.xy) - Blasts[slab].xy) < Blasts[slab].w)
        source = 2 * Power[slab].x / Blasts[slab].w * skirtLayer(float3(c), slab);
    DivOut[id] = 0.5 * (east - west + north - south + v(c + int3(0, 0, 1), slab).z - below) - source;
}
)HLSL";

// Open air outside the sides and top holds no pressure; the ground mirrors the cell above it.
const char *const PressureHelpers = R"HLSL(
Texture3D<float> Pres : register(t0);
float p(int3 c, uint slab, float here)
{
    if (c.z < 0) return here;
    if (any(c < int3(0, 0, 0)) || any(c >= int3(GX, GY, GZ))) return 0;
    if (solid(c, slab)) return here;	// a wall: no pressure across it
    return Pres.Load(int4(c + int3(0, 0, slab * GZ), 0));
}
)HLSL";

const char *const JacobiPass = R"HLSL(
Texture3D<float> Div : register(t1);
RWTexture3D<float> PresOut : register(u0);
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float here = Pres.Load(int4(id, 0));
    float sum = p(c + int3(1, 0, 0), slab, here) + p(c - int3(1, 0, 0), slab, here)
        + p(c + int3(0, 1, 0), slab, here) + p(c - int3(0, 1, 0), slab, here)
        + p(c + int3(0, 0, 1), slab, here) + p(c - int3(0, 0, 1), slab, here);
    PresOut[id] = (sum - Div.Load(int4(id, 0))) / 6;
}
)HLSL";

const char *const ProjectPass = R"HLSL(
Texture3D<float4> Vel : register(t1);
RWTexture3D<float4> VelOut : register(u0);
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float here = Pres.Load(int4(id, 0));
    float3 grad = 0.5 * float3(p(c + int3(1, 0, 0), slab, here) - p(c - int3(1, 0, 0), slab, here),
        p(c + int3(0, 1, 0), slab, here) - p(c - int3(0, 1, 0), slab, here),
        p(c + int3(0, 0, 1), slab, here) - p(c - int3(0, 0, 1), slab, here));
    float4 vt = Vel.Load(int4(id, 0));
    vt.xyz = noPenetration(vt.xyz - grad, c, slab);
    if (solid(c, slab)) vt = 0;
    VelOut[id] = vt;
}
)HLSL";

// The air's box follows the camera in whole cells: what it held moves the other way by Motion[0]
// cells, and what comes in at the far side is empty.
const char *const ScrollPass = R"HLSL(
Texture3D<float4> Vel : register(t0);
Texture3D<DEN> Den : register(t1);
RWTexture3D<float4> VelOut : register(u0);
RWTexture3D<DEN> DenOut : register(u1);
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    int3 from = int3(id) + (int3)Motion[0].xyz;
    bool inside = all(from >= int3(0, 0, 0)) && all(from < int3(GX, GY, GZ));
    VelOut[id] = inside ? Vel.Load(int4(from, 0)) : 0;
    DenOut[id] = inside ? Den.Load(int4(from, 0)) : 0;
}
)HLSL";

// Sunlight reaching each cell through the soot and dust between it and the sun, with a share that
// went through more thinly, for the light scattered more than once.
const char *const LightPass = R"HLSL(
Texture3D<DEN> Den : register(t0);
RWTexture3D<float> LightOut : register(u0);
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float tau = 0;
    [loop] for (int k = 1; k <= 16; ++k) {
        float3 q = float3(c) + SunToward.xyz * (k * 1.5);
        if (any(q < float3(-0.5, -0.5, -0.5)) || any(q > float3(GX - 0.5, GY - 0.5, GZ - 0.5))) break;
        DEN dd = Den.SampleLevel(Linear, uvwOf(q, slab), 0);
        tau += dd.x + dd.y;
    }
    tau *= 1.5 * SunToward.w * volume.z;
    LightOut[id] = exp(-tau) * 0.75 + exp(-tau * 0.25) * 0.25;
}
)HLSL";

const char *const MarchPass = R"HLSL(
Texture2D<float> SceneDepth : register(t0);
Texture3D<DEN> Den : register(t1);
Texture3D<float> Light : register(t2);
Texture3D<float4> Vel : register(t3);
Texture3D<float> Noise : register(t4);
SamplerState Linear : register(s0);
SamplerState Wrap : register(s1);
cbuffer Draw : register(b0)
{
    row_major float4x4 WorldFromClip;
    float4 Camera;
    float4 Viewport;
    float4 Box;
    float4 Slab;
    float4 Soot;
    float4 Dust;
    float4 SunColor;
    float4 Ambient;
    float4 SunDir;
    float4 Blast;
    float4 Effects;
    float4 Extra;
    float4 ClipLow;		// the world box the march keeps to, inside the grid's: where the smoke can be
    float4 ClipHigh;
    float4 Look;		// the light the shade side keeps, the fine billows' depth, the sun's share, how far thin smoke is spared
};
float3 worldAt(float2 pixel, float depth)
{
    float2 uv = (pixel - Viewport.xy) / max(Viewport.zw, float2(1, 1));
    float4 world = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1), WorldFromClip);
    float w = abs(world.w) < 0.000001 ? (world.w < 0 ? -0.000001 : 0.000001) : world.w;
    return world.xyz / w;
}
// Hot soot from deep red through orange to white-yellow, and dark below the glowing heat.
float3 fireColour(float heat)
{
    float3 colour = lerp(float3(0.6, 0.07, 0.02), float3(1.0, 0.42, 0.08), smoothstep(0.55, 0.95, heat));
    colour = lerp(colour, float3(1.0, 0.88, 0.62), smoothstep(0.95, 1.5, heat));
    // hotter than a shell's core, a tank's plume and a nuke's first second: overexposed near-white,
    // as the photo of a tank going up is
    colour = lerp(colour, float3(1.0, 0.97, 0.9), smoothstep(1.6, 2.6, heat));
    return colour * smoothstep(0.5, 1.0, heat) * (1 + 2 * smoothstep(1.1, 1.8, heat) + 2 * smoothstep(1.6, 2.6, heat));
}
float4 main(float4 position : SV_Position) : SV_Target
{
    float3 eye = Camera.xyz;
    float3 ray = worldAt(position.xy, SceneDepth.Load(int3(position.xy, 0))) - eye;
    float sceneDistance = length(ray);
    ray /= max(sceneDistance, 0.0001);
    float3 inverse = 1 / (abs(ray) > 1e-6 ? ray : 1e-6);
    float3 t0 = (ClipLow.xyz - eye) * inverse, t1 = (ClipHigh.xyz - eye) * inverse;
    float3 nearest = min(t0, t1), farthest = max(t0, t1);
    float enter = max(max(nearest.x, nearest.y), max(nearest.z, 0));
    float leave = min(min(min(farthest.x, farthest.y), farthest.z), sceneDistance);
    if (leave <= enter) discard;
    float span = leave - enter;
    uint steps = (uint)clamp(ceil(span / (Box.w * Slab.z)), 1, 128);
    float stepLength = span / steps;
    float jitter = frac(52.9829189 * frac(dot(position.xy, float2(0.06711056, 0.00583715)) + Camera.w * 7.31));
    float cosine = dot(ray, SunDir.xyz);
    float g = 0.35;
    float phase = lerp(1, (1 - g * g) / pow(abs(1 + g * g - 2 * g * cosine), 1.5), 0.6);
    float3 light = SunColor.rgb + Ambient.rgb;
    // the light's hue at the day's brightness; a night's dim moonlight is not lifted to it, or the
    // smoke and the trails stood out of the dark ground self-lit pale blue
    float3 tint = light / max(dot(light, float3(0.299, 0.587, 0.114)), 0.75);
    float3 color = 0;
    float transmittance = 1;
    float slabBase = Slab.x * GZ;
    [loop] for (uint i = 0; i < steps; ++i) {
        float3 cell = (eye + ray * (enter + (i + jitter) * stepLength) - Box.xyz) / Box.w - 0.5;
        float3 fromBlast = cell - Blast.xyz;
        float near2 = dot(fromBlast, fromBlast);
        // the flash: light with nothing to absorb it, a tall glow over the blast for its first frames
        if (Effects.x > 0) {
            float wide = Extra.z, tall = Extra.z * Extra.y;
            float3 f = cell - Blast.xyz - float3(0, 0, tall * 0.5);
            float falloff = (f.x * f.x + f.y * f.y) / (wide * wide) + f.z * f.z / (tall * tall);
            color += transmittance * float3(1.0, 0.9, 0.7) * Effects.x * exp(-falloff) * stepLength / Box.w;
        }
        float3 clamped = clamp(cell, float3(0, 0, 0), float3(GX - 1, GY - 1, GZ - 1));
        float3 uvw = float3((clamped.x + 0.5) / GX, (clamped.y + 0.5) / GY, (clamped.z + 0.5 + slabBase) / (GZ * SLABS));
        DEN dd = Den.SampleLevel(Linear, uvw, 0);
        // a nuclear cap's condensation collar: a white skirt round the stem just under the cap,
        // sloping down outwards, for a few seconds
        float collar = 0;
        if (Effects.w > 0) {
            float dr = length(fromBlast.xy) - Extra.x;
            float dz = cell.z - Extra.w + 0.2 * dr;
            collar = Effects.w * exp(-dr * dr / max(Extra.x * Extra.x * 0.18, 1) - dz * dz * 0.5);
        }
        if (dd.x + dd.y < 0.004 && Effects.z <= 0 && collar < 0.004) {	// empty air costs one sample
#ifdef AIR
            ++i;	// and the air's, most of its box, is crossed two steps at a time
#endif
            continue;
        }
#ifndef AIR
        // Where a plume is two or three cells across, the trilinear cells came out as a staircase,
        // flat-topped blocks with square notches.  The lookup is pushed about by up to a cell along
        // noise that drifts upwards, so an edge breaks into ragged, flickering tongues instead.
        {
            float3 q = cell / 6 + float3(Slab.x * 0.37, 0, -Camera.w * 0.6);
            float3 warp = float3(Noise.SampleLevel(Wrap, q, 0), Noise.SampleLevel(Wrap, q + 0.31, 0),
                Noise.SampleLevel(Wrap, q + 0.67, 0)) - 0.5;
            clamped = clamp(cell + warp * 2, float3(0, 0, 0), float3(GX - 1, GY - 1, GZ - 1));
            uvw = float3((clamped.x + 0.5) / GX, (clamped.y + 0.5) / GY, (clamped.z + 0.5 + slabBase) / (GZ * SLABS));
            dd = Den.SampleLevel(Linear, uvw, 0);
        }
#endif
        float n = Noise.SampleLevel(Wrap, cell / 9 + float3(Slab.x * 0.37, 0, -Camera.w * 0.04), 0);
        // the condensation shell: white, thin, over the upper half, for a second or two
        float shell = 0;
        if (Effects.z > 0) {
            float r = sqrt(near2);
            float across = (r - Effects.y) / max(Effects.y * 0.1, 0.5);
            shell = Effects.z * exp(-across * across) * saturate(fromBlast.z / Effects.y + 0.4) * (0.5 + n);
        }
        shell += collar * (0.2 + 1.4 * n);	// as white as the shell, broken up by the billows
        float total = dd.x + dd.y + shell;
        if (total < 0.004) continue;
        // billows: the surface is eroded by the noise, the inside left whole
#ifdef AIR
        float eroded = total * saturate(0.35 + 1.1 * n);	// the air's smoke is thin: the noise shapes it, it does not eat it
        // A missile's trail is a cell or two across, so the noise at nine cells cut it into white
        // patches: it keeps most of its smoke, only its edge billows with a noise twice as fine.
        float trail = saturate((dd.z / max(dd.x, 0.01) - 1.02) / 0.2);
        if (trail > 0) {
            // lumped, a cauliflower edge, not an even tube (the coarse noise pushes the fine about)
            float fine = Noise.SampleLevel(Wrap, cell / 4 + float3(0.43, 0.17, -Camera.w * 0.08), 0);
            eroded = lerp(eroded, total * saturate(0.3 + 1.2 * (fine + 0.4 * (n - 0.5))), trail);
        }
#else
        float eroded = max(total - (1 - n) * 0.45 * (1 - saturate(total * 1.2)), 0);
        // a nuclear fireball is thin on purpose, so its middle shows: the noise shapes it into
        // lumps rather than eating it (taken away, it was gone at two seconds)
        if (Look.w > 0) eroded = lerp(eroded, total * saturate(0.3 + 1.2 * n), Look.w);
        // a nuclear cap's cauliflower: a hard surface where its smoke passes a threshold, the
        // threshold pushed about by finer billows, ten units across, than its cells can hold.  Eaten
        // softly, the cap was a blurred beige ball lit the same all over.
        if (Look.y > 0) {
            float fine = Noise.SampleLevel(Wrap, cell / 3.5 + float3(Slab.x * 0.53, 0.21, -Camera.w * 0.03), 0);
            // the soot only: the thin dust of the surge and the stem's foot keeps the soft edge
            eroded = eroded * (1 - dd.x / total) + dd.x * lerp(1, smoothstep(0.3, 0.55, dd.x + (fine - 0.5) * 0.6), Look.y);
        }
#endif
        float sigma = eroded * walls(cell) * Slab.y * Slab.w;
        if (sigma <= 0) continue;
        float sun = Light.SampleLevel(Linear, uvw, 0);
        float heat = Vel.SampleLevel(Linear, uvw, 0).w;
        float height = saturate(cell.z / 24);
#ifdef AIR
        // each cell's own grey, carried with its soot from the particles that made it; a trail's is its
        // grey over TrailTag (1.25, over 0.5 of span): a missile's plume white, a rocket's trail grey,
        // with less sky in their shade, so the sunlit side is white and the far side shows the shadow
        float3 albedo = (Soot.rgb * dd.z + Dust.rgb * dd.w) / total;
        // (weighed in only by the tag itself: a trail mixed with a puff's smoke, between 1.02 and the
        // tag, would take the trail's darkest grey and draw a dark seam between them)
        float ratio = dd.z / max(dd.x, 0.01), trailGrey = saturate((ratio - 1.25) / 0.5);
        albedo = lerp(albedo, (0.3 + 0.65 * trailGrey) * (dd.x / total) + Dust.rgb * dd.w / total, saturate((ratio - 1.02) / 0.23));
        float shade = Look.x * (1 - 0.45 * trail);
#else
        float3 albedo = (Soot.rgb * dd.x + Dust.rgb * dd.y + float3(0.92, 0.93, 0.95) * shell) / total;
        float shade = Look.x;
#endif
        float3 lit = albedo * tint * (shade + 0.12 * height + Look.z * sun * phase);
        float3 source = (lit + fireColour(heat) * Soot.w * (dd.x / total)) * sigma;
        float through = exp(-sigma * stepLength);
        color += transmittance * (source - source * through) / sigma;
        transmittance *= through;
        if (transmittance < 0.01) break;
    }
    return float4(color, 1 - transmittance);
}
)HLSL";

// The fire tier's own programs (the Prelude's Fire arrays are theirs; every program declares them,
// so the blast fronts after them sit at one offset); density is soot and fuel, and the fuel is never
// drawn.

// A fire's box fades only at its sides and its top: the building it burns on sits in its middle.
const char *const FireWalls = R"HLSL(
float fireWalls(float3 cell)
{
    float2 inside = min(cell.xy, float2(GX - 1, GY - 1) - cell.xy);
    return smoothstep(0, 2, inside.x) * smoothstep(0, 2, inside.y) * smoothstep(0, 6, GZ - 1 - cell.z);
}
)HLSL";

// The fuel that burns a second in a cell: none under the ignition, and no more than the air reaching
// it lets burn, however rich the cell, so a fuel-rich core burns slowly and sooty inside its flame.
// (Cut off to nothing past the rich mark, as a first try had it, a burning surface's own cells never lit.)
const char *const FireHelpers = R"HLSL(
float burnRate(float fuel, float heat, uint slab)
{
    float4 f = Fire[slab];
    return f.x * fuel / (1 + fuel / Fire2[slab].x) * smoothstep(f.w - 0.05, f.w + 0.05, heat);
}
)HLSL";

const char *const FireAdvectPass = R"HLSL(
Texture3D<float4> Vel : register(t0);
Texture3D<DEN> Den : register(t1);
Texture3D<float4> Curl : register(t2);
RWTexture3D<float4> VelOut : register(u0);
RWTexture3D<DEN> DenOut : register(u1);
RWTexture3D<float> PresOut : register(u2);
float w(int3 c, uint slab) { return Curl.Load(at(c, slab)).w; }
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float dt = Step.x;
    float3 cell = float3(c);
    float4 fire = Fire[slab], fire2 = Fire2[slab];
    float4 vt = 0;
    DEN d = 0;
    if (volume.w > 0.5) {
        PresOut[id] = 0;
    } else {
        // a blast front blows the flame and its smoke over as it passes (shockShift), the fuel too
        float shockKept = 1, stir = 0;
        float3 back = cell - Vel.Load(int4(id, 0)).xyz * dt - shockShift(cell, slab, dt, shockKept, stir);
        vt = Vel.SampleLevel(Linear, uvwOf(back, slab), 0);
        d = Den.SampleLevel(Linear, uvwOf(back, slab), 0);
        // The smoke climbs at a plume's speed while it is warm, about 20 units a second over the
        // cells' own flow: a plume a few cells wide keeps its momentum, which the cells' trilinear
        // advection smears into the still air round it, and without this the soot stalled at the
        // tips and the wind laid it along the ground.
        float climb = 20 / volume.z * saturate(vt.w / 0.15);
        d.x = Den.SampleLevel(Linear, uvwOf(back - float3(0, 0, climb * dt), slab), 0).x;
        d *= shockKept;
        if (stir > 0) vt.xyz += shockStir(cell, stir, dt, volume.z);
        vt.z += dt * Fire3[slab].x * vt.w;	// hot gas rises
        float3 grad = 0.5 * float3(w(c + int3(1, 0, 0), slab) - w(c - int3(1, 0, 0), slab),
            w(c + int3(0, 1, 0), slab) - w(c - int3(0, 1, 0), slab),
            w(c + int3(0, 0, 1), slab) - w(c - int3(0, 0, 1), slab));
        float length2 = dot(grad, grad);
        if (length2 > 1e-10)
            vt.xyz += dt * Forces.z * cross(grad * rsqrt(length2), Curl.Load(int4(id, 0)).xyz);
        vt.xyz /= 1 + Forces.w * length(vt.xyz) * dt;
        // the eddies the cells are too coarse to hold, where the gas is hot: a curl-free kick on a
        // noise rising with the flame, so the flame sheet folds and its top breaks into licks
        // (on eddies a third of the site's flame across, so a tall flame folds on its own scale)
        if (vt.w > 0.1) {
            float3 q = cell * Fire3[slab].w + float3(0, 0, -Step.y * 2.5);
            float3 kick = float3(valueNoise(q, 51), valueNoise(q + 5.7, 53), valueNoise(q + 2.9, 57)) - 0.5;
            vt.xyz += kick * (Fire3[slab].y * saturate(vt.w) * dt);
        }
        // the wind from the flame's foot up, so a flame leans from where it burns
        float overGround = cell.z - groundAt((int2)cell.xy, slab);
        vt.xy += (Wind.xy / volume.z - vt.xy) * saturate(dt * 0.35 * saturate(overGround / 2));
        vt.xyz *= Keep.y;
        d.x *= Thin[slab].x;
    }
    // the fuel the burning surfaces give off, as hot as they are, so it lights where it leaves them
    uint count = (uint)volume.y;
    for (uint i = 0; i < count; ++i) {
        uint base = (slab * PUFFS + i) * 3;
        float4 where = Puffs[base];
        float4 motion = Puffs[base + 1];
        float4 heat = Puffs[base + 2];
        float3 away = cell - where.xyz;
        float r2 = dot(away, away) / (where.w * where.w);
        if (r2 >= 1) continue;
        float k = (1 - r2) * (1 - r2);
        // a burning edge gives its gas off in patches that come and go, not as one smooth puff: the
        // patches are what break the flame into tongues with dark gaps between them
        // (at 1.3 cells a patch was as wide as the puff, so it only dimmed the whole flame)
        float patch = valueNoise(cell / 0.7 + float3(i * 7.3, i * 3.1, -Step.y * 4), slab * 31 + i);
        patch = 0.1 + 1.8 * patch * patch;
        d.x += motion.w * k * dt * patch;
        d.y += heat.y * k * dt * patch;
        // the surface's heat stands flat over the puff, not peaked in its middle: a peak hot enough to
        // light the puff's edge glowed as a round ball of its own
        vt.w = max(vt.w, heat.x * saturate(2.5 * k));
        vt.xyz = lerp(vt.xyz, motion.xyz, saturate(k * heat.z));
    }
    // It burns: the fuel turns into heat and soot, the soot burns away where it is hottest (what is
    // left past the flame's tip is its smoke), and the heat goes as the hot soot radiates it, T^4
    // against the air's, and as the flame mixes with the air round it.
    float burn = min(burnRate(d.y, vt.w, slab), d.y / max(dt, 1e-4));
    d.y -= burn * dt;
    vt.w += fire.y * burn * dt;
    d.x += fire.z * burn * dt * saturate(d.y / fire2.x + 0.25);
    // The fuel the flame carries past its tip, where the gas has cooled under the ignition, never
    // burns: a building's fire is short of air, and its unburned pyrolysate condenses into the dark
    // smoke that leaves the flame tips.
    float quench = d.y * min(Fire3[slab].z * (1 - smoothstep(fire.w - 0.2, fire.w, vt.w)) * dt, 1);
    d.y -= quench;
    d.x += fire.z * quench;
    // soot burns away only where it is hot and has air: in a fuel-rich core it survives
    d.x *= exp(-fire2.z * dt * smoothstep(0.65, 0.95, vt.w) * saturate(1.5 - d.y / fire2.x));
    float t = 0.2 + 0.8 * vt.w, t2 = t * t;
    vt.w -= dt * fire2.y * (1 - exp(-2 * d.x)) * max(t2 * t2 - 0.0016, 0);
    // Mixing with the air round it cools the narrow, fast flame quickly and the wide plume over it
    // slowly (a plume's excess heat falls off as its height to the -5/3, so most of the fall is near
    // the flame): the smoke stays warm enough to climb off the tips instead of stalling at them.
    vt.w = clamp(vt.w * pow(Keep.x, pow(clamp(vt.w, 0.1, 1), 0.7)), 0, 2.2);
    // units push the flame and its smoke; the dust a rotor lifts is no fuel
    float fuel = d.y;
    applyMovers(vt, d, cell, slab, dt);
    d.y = fuel;
    d = min(d, 4);
    // no cell outruns 24 cells a second, and nothing undefined survives a step: one bad cell spread
    // through the slab in a few steps
    // (NaN fails every comparison, so the exponent is tested: FXC may not fold that away)
    float speed = length(vt.xyz);
    if ((asuint(speed) & 0x7f800000) == 0x7f800000) vt.xyz = 0;
    else if (speed > 24) vt.xyz *= 24 / speed;
    float rim = fireWalls(cell);
    d *= lerp(0.9, 1, rim);
    vt.w *= lerp(0.9, 1, rim);
    if (c.x == 0 || c.y == 0 || c.x == GX - 1 || c.y == GY - 1 || c.z == GZ - 1) { d = 0; vt.w = 0; }
    if (solid(c, slab)) { vt = 0; d = 0; }
    else vt.xyz = noPenetration(vt.xyz, c, slab);
    VelOut[id] = vt;
    DenOut[id] = d;
}
)HLSL";

// The divergence, with what burns as a source: the burning gas expands and pushes the flame outward.
const char *const FireDivergencePass = R"HLSL(
Texture3D<float4> Vel : register(t0);
Texture3D<DEN> Den : register(t1);
RWTexture3D<float> DivOut : register(u0);
float3 v(int3 c, uint slab) { return Vel.Load(at(c, slab)).xyz; }
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    if (solid(c, slab)) { DivOut[id] = 0; return; }
    float3 here = v(c, slab);
    float east = solid(c + int3(1, 0, 0), slab) ? -here.x : v(c + int3(1, 0, 0), slab).x;
    float west = solid(c - int3(1, 0, 0), slab) ? -here.x : v(c - int3(1, 0, 0), slab).x;
    float north = solid(c + int3(0, 1, 0), slab) ? -here.y : v(c + int3(0, 1, 0), slab).y;
    float south = solid(c - int3(0, 1, 0), slab) ? -here.y : v(c - int3(0, 1, 0), slab).y;
    float below = solid(c - int3(0, 0, 1), slab) ? -here.z : v(c - int3(0, 0, 1), slab).z;
    // held to two units of fuel a second: at the rich mark's 10 the outflow outran 8 sweeps of the
    // pressure, the flow blew up and the whole slab went to NaN (black, then nothing)
    float source = Fire2[slab].w * min(burnRate(Den.Load(int4(id, 0)).y, Vel.Load(int4(id, 0)).w, slab), 2);
    DivOut[id] = 0.5 * (east - west + north - south + v(c + int3(0, 0, 1), slab).z - below) - source;
}
)HLSL";

// The sun through the soot only: the fuel is a clear gas.
const char *const FireLightPass = R"HLSL(
Texture3D<DEN> Den : register(t0);
RWTexture3D<float> LightOut : register(u0);
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float tau = 0;
    [loop] for (int k = 1; k <= 16; ++k) {
        float3 q = float3(c) + SunToward.xyz * (k * 1.5);
        if (any(q < float3(-0.5, -0.5, -0.5)) || any(q > float3(GX - 0.5, GY - 0.5, GZ - 0.5))) break;
        tau += Den.SampleLevel(Linear, uvwOf(q, slab), 0).x;
    }
    tau *= 1.5 * SunToward.w * volume.z;
    LightOut[id] = exp(-tau) * 0.75 + exp(-tau * 0.25) * 0.25;
}
)HLSL";

// A fire's march.  Hot soot glows as a black body does (Kirchhoff: it gives off what it absorbs times
// the black body's light, Blackbody holding the colour and the brightness against 1500 K), the
// reaction zone adds a faint blue where the fuel burns, and the flame lights its own smoke from below.
const char *const FireMarchPass = R"HLSL(
Texture2D<float> SceneDepth : register(t0);
Texture3D<DEN> Den : register(t1);
Texture3D<float> Light : register(t2);
Texture3D<float4> Vel : register(t3);
Texture3D<float> Noise : register(t4);
Texture1D<float4> Blackbody : register(t5);
SamplerState Linear : register(s0);
SamplerState Wrap : register(s1);
cbuffer Draw : register(b0)
{
    row_major float4x4 WorldFromClip;
    float4 Camera;
    float4 Viewport;
    float4 Box;
    float4 Slab;
    float4 Soot;
    float4 Dust;
    float4 SunColor;
    float4 Ambient;
    float4 SunDir;
    float4 Blast;
    float4 Effects;
    float4 Extra;
    float4 ClipLow;
    float4 ClipHigh;
    float4 Look;
    float4 FireLook;
    float4 FireTint;
};
float3 worldAt(float2 pixel, float depth)
{
    float2 uv = (pixel - Viewport.xy) / max(Viewport.zw, float2(1, 1));
    float4 world = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1), WorldFromClip);
    float w = abs(world.w) < 0.000001 ? (world.w < 0 ? -0.000001 : 0.000001) : world.w;
    return world.xyz / w;
}
float3 glowOf(float heat)
{
    float kelvin = 300 + 1200 * heat;
    if (kelvin < 800) return 0;
    float4 b = Blackbody.SampleLevel(Linear, saturate((kelvin - 800) / 2200), 0);
    return b.rgb * pow(max(b.a, 0), FireLook.w);
}
float3 uvwAt(float3 cell)
{
    float3 c = clamp(cell, float3(0, 0, 0), float3(GX - 1, GY - 1, GZ - 1));
    return float3((c.x + 0.5) / GX, (c.y + 0.5) / GY, (c.z + 0.5 + Slab.x * GZ) / (GZ * SLABS));
}
float4 main(float4 position : SV_Position) : SV_Target
{
    float3 eye = Camera.xyz;
    float3 ray = worldAt(position.xy, SceneDepth.Load(int3(position.xy, 0))) - eye;
    float sceneDistance = length(ray);
    ray /= max(sceneDistance, 0.0001);
    float3 inverse = 1 / (abs(ray) > 1e-6 ? ray : 1e-6);
    float3 t0 = (ClipLow.xyz - eye) * inverse, t1 = (ClipHigh.xyz - eye) * inverse;
    float3 nearest = min(t0, t1), farthest = max(t0, t1);
    float enter = max(max(nearest.x, nearest.y), max(nearest.z, 0));
    float leave = min(min(min(farthest.x, farthest.y), farthest.z), sceneDistance);
    if (leave <= enter) discard;
    float span = leave - enter;
    uint steps = (uint)clamp(ceil(span / (Box.w * Slab.z)), 1, 128);
    float stepLength = span / steps;
    float jitter = frac(52.9829189 * frac(dot(position.xy, float2(0.06711056, 0.00583715)) + Camera.w * 7.31));
    float cosine = dot(ray, SunDir.xyz);
    float g = 0.35;
    float phase = lerp(1, (1 - g * g) / pow(abs(1 + g * g - 2 * g * cosine), 1.5), 0.6);
    float3 light = SunColor.rgb + Ambient.rgb;
    float3 tint = light / max(dot(light, float3(0.299, 0.587, 0.114)), 0.75);
    float3 color = 0;
    float transmittance = 1;
    [loop] for (uint i = 0; i < steps; ++i) {
        float3 cell = (eye + ray * (enter + (i + jitter) * stepLength) - Box.xyz) / Box.w - 0.5;
        DEN dd = Den.SampleLevel(Linear, uvwAt(cell), 0);
        if (dd.x < 0.004 && dd.y < 0.004) continue;	// empty air costs one sample
        // ragged, flickering tongues rather than the cells' staircase, as the blasts' march does
        float3 q = cell / 4 + float3(Slab.x * 0.37, 0, -Camera.w * 0.9);
        float3 warp = float3(Noise.SampleLevel(Wrap, q, 0), Noise.SampleLevel(Wrap, q + 0.31, 0),
            Noise.SampleLevel(Wrap, q + 0.67, 0)) - 0.5;
        float3 uvw = uvwAt(cell + warp * 1.5);
        dd = Den.SampleLevel(Linear, uvw, 0);
        float heat = Vel.SampleLevel(Linear, uvw, 0).w;
        float rim = fireWalls(cell) * Slab.y;
        // the reaction zone, a clear gas: light with nothing to absorb it
        float react = dd.y / (1 + dd.y / FireLook.z) * smoothstep(FireLook.y - 0.05, FireLook.y + 0.05, heat);
        color += transmittance * FireTint.rgb * react * rim * stepLength / Box.w;
        float n = Noise.SampleLevel(Wrap, cell / 7 + float3(Slab.x * 0.37, 0, -Camera.w * 0.05), 0);
        float soot = max(dd.x - (1 - n) * 0.12 * (1 - saturate(dd.x * 1.2)), 0);
        // The tongues.  A flame's burning sheet is far thinner than a cell and wrinkles in the eddies
        // the cells cannot hold, so its heat varies under the cell's mean; that variation is drawn
        // here, on noise stretched up the flow and climbing with it (Extra: its scale in world units
        // and its rise).  The hot base holds together, the cooler upper flame breaks into tongues
        // with clear gaps, and a black body's light falls so steeply with its heat that a gap goes dark.
        float3 world = Box.xyz + (cell + 0.5) * Box.w;
        float3 tq = float3(world.xy * Extra.x, world.z * Extra.y - Camera.w * Extra.z) + Slab.x * 0.37;
        float tn = Noise.SampleLevel(Wrap, tq, 0) * 0.7 + Noise.SampleLevel(Wrap, tq * 2.3 + 0.41, 0) * 0.3;
        // (the cooler the gas, the higher up the flame it is, and the sparser its tongues)
        float tip = 1 - smoothstep(0.8, 1.5, heat);
        float tongue = saturate((tn - lerp(0.2, 0.55, tip)) * 3);
        float broken = smoothstep(0.3, 0.6, heat) * lerp(0.75, 1, tip);
        heat *= lerp(1, 0.2 + 0.95 * tongue, broken);
        soot *= lerp(1, 0.1 + 0.9 * tongue, broken);
        float sigma = soot * rim * Slab.w;
        if (sigma <= 0) continue;
        float sun = Light.SampleLevel(Linear, uvw, 0);
        float height = saturate(cell.z / 24);
        float3 lit = Soot.rgb * tint * (Look.x + 0.12 * height + Look.z * sun * phase);
        // lit from the flame under it
        if (FireTint.w > 0) lit += Soot.rgb * glowOf(Vel.SampleLevel(Linear, uvwAt(cell - float3(0, 0, 2)), 0).w) * FireTint.w;
        float3 source = (lit + glowOf(heat) * FireLook.x) * sigma;
        float through = exp(-sigma * stepLength);
        color += transmittance * (source - source * through) / sigma;
        transmittance *= through;
        if (transmittance < 0.01) break;
    }
    return float4(color, 1 - transmittance);
}
)HLSL";

enum Pass { CurlProgram, AdvectProgram, DivergenceProgram, JacobiProgram, ProjectProgram, LightProgram, ScrollProgram,
	ProgramCount };

// The air round the camera: every smoke and dust system no blast took, its particles drawn into one
// grid that follows the view.  Its grid and how many particles it takes a step are the setting's;
// past that the particles farthest from the camera are left out, never drawn as billboards.
// The air's pressure needs fewer sweeps than a blast's: nothing in it is pushed as hard.
struct AirShape { unsigned gx, gy, gz; float cell; unsigned particles; unsigned sweeps; float stepCells; };
const AirShape AirShapes[3] = { { 8, 8, 8, 1.0f, 0, 0, 1.0f }, { 96, 96, 24, 12.0f, 4096, 6, 1.5f },
	{ 160, 160, 40, 7.2f, 12288, 8, 1.25f } };
const unsigned AirTileEntries = 27;		// brick list room a particle: a sphere of at most 8 cells reaches 27
const float AirExtinction = 0.04f;		// per world unit at density 1: the air's smoke is thinner than a blast's
// seconds, once no particle holds it up: a damaged building's SmokeFaction* puffs live 2 to 5.3 s
// and StructureDamagedSmoke's 1.5 s, and at 7 the air kept them on screen for twice as long again
const float AirSootHalfLife = 0.75f;	// (halved from 3 and twice again at the owner's word: a fast game)
// The air's dust thins on its own clock, apart from a blast's DustHalfLife: a tan lobe of dust stood
// beside a fallen War Factory long after its smoke had gone at the shared 6 s (2, halved again)
const float AirDustHalfLife = 1.0f;
const float AirTakeUp = 8.0f;			// a second: how fast the cells under a particle reach its smoke
const float AirCoverage = 0.7f;			// a billboard's opacity at its centre, against its alpha
// A sphere's falloff, the trilinear blur of a few cells and the noise take most of the opacity a
// particle's density was worked out for; this gives it back (by eye, on volair's dust and smoke).
const float AirGain = 4.0f;
// and a missile's ribbon of jet smoke, a cell or two wide: less in the air, where a Rocket Buggy
// salvo's twenty trails stood as a white wall at 1.8 and as a tan haze over the field at 0.7, more
// in the tall air, where one trail coming down alone is seen across and its cells are wider
// (1.8 in the tall air made a Tomahawk's arc over Heartland Shield a thick white arch that hung on)
const float StreakGain[2] = { 0.45f, 0.75f };
// A trail's cells carry their grey in the brightness channel over TrailTag (TrailTag + TrailTagSpan
// times a grey of 0 to 1), which is what tells them from a puff's cells in the passes (AdvectPass and
// MarchPass hold the same two numbers), and the march takes their albedo from that grey.  A fixed 1.7
// times the puff's albedo (TrailBright, and 2.4 before it) left a Scud Storm's columns as even bright
// pillars that read as beams of light; the photos' plumes are white where the sun is on them and grey
// on their shadow side.
const float TrailTag = 1.25f, TrailTagSpan = 0.5f;
// A rocket's trail is grey and wispy, after the RPG-7 photo: its particle's own colour times this, and
// a little thicker than its ribbon's opacity asks, since a ribbon a unit wide spreads over cells seven
// units across (at 0.55 with EA's additive fire line hidden the trail was gone)
const float RocketGrey = 0.55f, RocketGain = 1.6f;
const int RibbonSmoothPoints = 4;		// a rocket ribbon's points averaged either side (stepAir)
const float RocketFlameLead = 0.5f;		// a rocket's flame laid this many frames' travel ahead of its logic position, at the drawn tail
// A ballistic or cruise missile's plume (the Scud's, the nuclear missile's, the Tomahawk's) opens out
// wider with age than a rocket's trail, and is thicker, so it shades itself and its edge billows.
const float PlumeSpread = 3.0f, PlumeGain = 1.6f;
// The flame at the nozzle: a small hot ball at each rocket's tail, read off its drawable
// (gatherNozzles), and a trail cell's heat dies within a few hundredths of a second (AdvectPass), so
// only the nozzle glows, short and bright (fireColour is orange at 1.2 and white-yellow at 1.8).  Laid
// on the ribbon's newest points it burned where the ribbon starts, a rocket's length behind it.
const float NozzleHeat[2] = { 1.6f, 1.8f };	// a rocket's, a missile's
const float NozzleSoot = 6.0f;			// the smoke that glows there (at 2 it did not show)
const float NozzleGlow = 2.0f;			// the air's flame against the scene's white (a blast's soot[3])
// A trail's smoke half-life in the air and the tall air (the brightness tells its cells): a Rocket
// Buggy salvo's goes in a couple of seconds, a Scud Storm's columns hang on as rocket smoke does.
// (a Tomahawk's and a Scud's jet smoke lives 2 s, a Rocket Buggy's 1.2 s, so at 3 a Tomahawk's arc
// over Heartland Shield was on screen for 5 to 6 s where stock's is gone in 3; a cell holds what its
// particle laid while it lives, so the half-life is what follows the particle's 2 s: at 1.5 the arc
// still showed 4.5 s after the launch; 0.5 halved again, the owner's third halving)
const float TrailHalfLife[2] = { 0.25f, 0.25f };
// and so a trail's particle lays less the older it is (by (1 - age)^TrailAgeFade), so its cells sink
// with it over its life and are gone soon after it: at a fixed rate the arc showed 5 s, at 1.5 about
// 3.5 s after the launch on film, and at 3 a trail is down to a tenth by half its particle's life
const float TrailAgeFade = 3.0f;
// A trail's particle stands for more smoke the older it is, spread wider: the end of a column
// farthest from the missile is up to this much wider than its head, so it opens out as it ages.
const float TrailSpread = 1.5f;
const float TrailEddy = 0.25f;			// cells/s^2: the stir a trail's smoke takes (about a cell a second once the drag holds it)
// The tall air: a second grid round the camera, as wide as the air's and a thousand units tall at
// twice its cell, for the systems that climb out of the air's top, a nuclear missile's or a Scud
// Storm's trail coming down from high up, a Scud's arc.  It runs only while one of them is in it.
// The air's 30 or 40 cells hold a column to about 200 units over the ground; a trail that reached
// past that kept its billboards whole and, nearer the line, lost the part over the top.
const AirShape TallShapes[3] = { { 8, 8, 8, 1.0f, 0, 0, 1.0f }, { 48, 48, 48, 24.0f, 2048, 6, 1.5f },
	{ 96, 96, 96, 12.0f, 4096, 8, 1.25f } };
const float AirTallOver = 30.0f;		// units over the air's top less its rim: a trail with a particle that high goes to the tall air
const float AirTrailOver = 150.0f;		// units over the top: any other system with a particle that high keeps its billboards

// A mesh of a building's model as a box: along the building's heading and across it round its
// position, half sizes, bottom and top over its ground.  World units in a Solid, cells in a Volume.
struct Part { float along, across, halfAlong, halfAcross, bottom, top; };
const unsigned MaxParts = 6;

struct Volume {
	bool live = false;
	bool reset = false;
	Kind kind = Shell;
	float scale = 1;					// a shell's size against the smallest
	float origin[3] = {};
	float cell = 1;
	float blast[3] = {};				// the detonation, in cells
	float ground = 0;					// the terrain's height under it, in cells
	// a tank's hull or a building's footprint: its heading, then its half length, half width and
	// height in cells
	float hull[5] = { 1, 0, 5, 3.3f, 3.3f };
	bool onHull = false;				// a shell that burst on a vehicle
	ObjectID building = INVALID_ID;		// a structure blast's building, left out of its floor once it has fallen
	bool standing = false;				// a damaged stage blowing out of a building that stays up
	Part parts[MaxParts] = {};			// the building's meshes, in cells; none, its footprint stands in
	unsigned partCount = 0;
	unsigned seed = 0;
	int born = 0;
	int life = 0;						// logic frames
	unsigned puffs = 0;
	int floorFrame = -1000;				// when its terrain and buildings were last laid into the grid
	int site = -1;						// a fire's site in `fires`
	bool frozen = false;				// a fire off the screen: not stepped this frame
	int placed = 0;						// when a fire's box was last laid
	// A nuclear cap's hold on its shape (VolumeShock::capHold) and how far other blasts' fronts have
	// moved its middle; under 0.05 the cap is let go, free smoke.
	float hold = 1;
	float capPushed = 0;
};

struct Tier {
	TierShape shape = {};
	bool air = false;
	ID3D11ComputeShader *compute[ProgramCount] = {};
	ID3D11PixelShader *march = nullptr;
	ID3D11Buffer *simConstants = nullptr;
	Field velocity[2], density[2], pressure[2], curl, divergence, light;
	ID3D11Texture2D *floor = nullptr;	// each slab's solid height a column, slabs stacked in y
	ID3D11ShaderResourceView *floorView = nullptr;
	unsigned current = 0;		// which density holds the last step's result
};

struct State {
	ID3D11Device *device = nullptr;
	bool refused = false;
	bool tallRefused = false;	// the device would not make the tall air: what climbs out of the air keeps its billboards
	bool fireRefused = false;	// nor the fires': they keep their billboards
	Tier tiers[3];
	ID3D11Texture1D *blackbody = nullptr;	// the fires' colour and brightness from 800 to 3000 K
	ID3D11ShaderResourceView *blackbodyView = nullptr;
	int lastFireStep = -1;
	Tier air, tall;
	// the particles and tile lists of the air ([0]) and the tall air ([1])
	ID3D11Buffer *sources[2] = {}, *tileRange[2] = {}, *tileIndex[2] = {};
	ID3D11ShaderResourceView *sourcesView[2] = {}, *tileRangeView[2] = {}, *tileIndexView[2] = {};
	ID3D11VertexShader *fullscreen = nullptr;
	ID3D11SamplerState *linear = nullptr, *wrap = nullptr;
	ID3D11BlendState *premultiplied = nullptr;
	ID3D11RasterizerState *rasterizer = nullptr;
	ID3D11Buffer *drawConstants = nullptr;
	ID3D11Texture3D *noise = nullptr;
	ID3D11ShaderResourceView *noiseView = nullptr;
	int lastStepFrame = -1;
	unsigned drawn = 0;
};

State state;
Volume volumes[MaxVolumes];
SimConstants sim;
// Every system a blast took: a volume draws its smoke, or it was dropped; never billboards.
std::unordered_set<int> takenSystems;
// When each system was first asked about, to join it to a blast that started beside it.
std::unordered_map<int, int> firstSeen;
struct TemplateKind { bool trigger; Kind kind; float scale; };
std::unordered_map<const ParticleSystemTemplate *, TemplateKind> templateKinds;
unsigned claimedCount = 0, joinedCount = 0, droppedCount = 0;
int lastReport = 0;
// The air: the systems it took, its box, and where in the world its smoke can be, a box a second
// for the last twenty, so the march keeps to the smoke and not the whole grid.
std::unordered_set<int> airSystems;
std::unordered_set<int> airOutside;		// air systems with no particle in the box this step: billboards
std::unordered_set<int> tallSystems;	// air systems that climbed out of the air's top: the tall air draws them, for good
struct AirLook { bool smoke; bool dust; bool scrap; };
std::unordered_map<const ParticleSystemTemplate *, AirLook> airLooks;
Volume airVolume, tallVolume;
struct WorldBox { float low[3], high[3]; int frame; };
std::vector<WorldBox> airBoxes, tallBoxes;
unsigned airFed = 0, airLeftOut = 0;
// The units pushing the air this logic frame, nearest the camera first (MoverPass), and the blast
// fronts still running out (shockShift): each with the slot of the volume it started with, if any,
// and whether Gather has seen its place is not in the drawn player's shroud (until then it pushes
// nothing, so smoke never shows an unscouted blast).
enum MoverKind { MoverVehicle, MoverJet, MoverRotor, MoverWalker };
struct Mover { float m[4][4]; float distance; };
std::vector<Mover> movers;
struct Shock { float at[3]; VolumeShock::ShockClass c; int born; int owner, ownerBorn; bool checked; };
std::vector<Shock> shocks;
// The fronts this step uploads (fillShocks), in upload order, with their state now.
struct Front { const Shock *shock; float age, radius, width, speed; };
std::vector<Front> fronts;
unsigned moverPeak = 0;
// Every ground vehicle near the camera, standing or moving, this logic frame and the one before,
// so a tank dying now finds its hull: where it stands, its heading and its template's box.
struct Hull { float x, y, heading, major, minor, tall; };
std::vector<Hull> hulls, lastHulls;
// Where a tank's blast started since the last Gather, for its death sparks (ReforgedTankDeathSparks
// in ParticleSystemReforged.ini).  A system cannot be made in Takes, which runs inside the particle
// manager's own loop, so Gather makes them next frame, outside the logic window.
std::vector<Coord3D> pendingSparks;
struct Nozzle { float x, y, z; bool big; int frames; };	// a rocket's tail in flight (gatherNozzles)
std::vector<Nozzle> nozzles;
// The same for a shell striking armour (TankStruckSmoke, which FX_DamageTankStruck turns away from
// the attacker: `along` is the shot's heading).
struct PendingHit { Coord3D at; float along[2]; };
std::vector<PendingHit> pendingHits;
// The hits whose sparks we throw, for eight logic frames: the game's own sparks near one are hidden.
struct OwnedHit { Coord3D at; int frame; };
std::vector<OwnedHit> ownedHits;
// Each live shot's systems (its trigger flame and the FX that joined it), by slot, so a blast that
// takes the slab gives them their billboards back.
std::unordered_map<unsigned, std::vector<int>> gunSystems;
// The structures the smoke flows round, read by Gather every two seconds outside the logic
// thread's window (the render runs inside it, where walking the object list is a counted read).
// A structure goes in once the player whose view is drawn has seen it, so smoke parting round
// nothing does not give away an unscouted base.
// The list before the last read is kept too, so a building that died just after a read still has its
// footprint when its death's smoke asks for it.
// Each also keeps the boxes of its model's biggest meshes (readParts, Part).
// `burns`: a building whose fires the fire slabs take; a wall or a junk prop (CarJunk01 is a
// STRUCTURE DEFENSIVE_WALL) keeps the game's own flame.
struct Solid { float x, y, top, major, minor, angle, tall; bool box, burns; ObjectID id; Part parts[MaxParts]; unsigned partCount; };
std::vector<Solid> solids, lastSolids;
int solidsFrame = -100000;
// The fires: their sites (VolumeFireCore), the systems whose billboards a fire slab draws, the
// templates that are fires, and the objects they hang on that moved in the last second (a wreck
// that drives off keeps its billboards).
VolumeFire::Fires fires;
std::unordered_set<int> fireTaken;
std::unordered_set<unsigned> fireEmitting;	// systems the last fire step laid fuel for: only these give up their billboards
// The surface each burning building or wreck burns on (readSurface), by ObjectID.
// Its up-facing triangles too, nine floats each, which layFloor lays into the fire's slab as the
// floor its flames stand on.
struct SurfacePoint { float x, y, z, nx, ny, nz; };
struct FireSurface { unsigned key = 0; std::vector<SurfacePoint> points; std::vector<float> roofs; };
std::unordered_map<unsigned, FireSurface> fireSurfaces;
// (a past-the-cap model keeps its first roofs and loses the rest of its floor: the stock buildings
// measured here have under 1200 surface points in all)
const unsigned MaxSurfacePoints = 4096, MaxRoofTriangles = 16384;
// Each fire slab's floor as layFloor last laid it, in cells over the slab's bottom, a column a cell.
std::vector<float> fireFloors[MaxSlabs];
// The surface points each fire system's fuel comes off, picked once per surface and bone (a search
// over thousands of points every step for every emitter would cost a millisecond a frame).
struct SurfacePick { unsigned key; float bone[3]; unsigned count; SurfacePoint points[4]; };
std::unordered_map<unsigned, SurfacePick> surfacePicks;
int surfacesFrame = -100000;
std::unordered_map<const ParticleSystemTemplate *, bool> fireTemplates;
std::unordered_map<unsigned, Coord3D> fireAnchors;
std::unordered_set<unsigned> movingObjects;
int anchorsFrame = -100000;
unsigned fireClaims = 0;
// A site's key is the object its systems hang on, or for the art's fire bones (hung on the drawable)
// DrawableKey with the drawable's ID: which object that drawable is can only be asked outside the
// logic window, so Gather does (fireOwners) and a site waits a frame for its slab.
const unsigned DrawableKey = 0x80000000u;
std::unordered_map<unsigned, unsigned> fireOwners;
std::unordered_set<unsigned> pendingOwners;
unsigned ownerOf(unsigned key)
{
	if (!(key & DrawableKey)) return key;
	auto found = fireOwners.find(key);
	return found == fireOwners.end() ? 0 : found->second;
}

unsigned tierOf(unsigned slot) { return slot >= TierShapes[2].first ? 2 : slot >= TierShapes[1].first ? 1 : 0; }
unsigned slabOf(unsigned slot) { return slot - TierShapes[tierOf(slot)].first; }

void releaseState();
void pruneSets();
void boxOf(const Volume &volume, const TierShape &shape, float low[3], float high[3]);
void addShock(const Coord3D &where, const VolumeShock::ShockClass &c, int now, int owner = -1);
void fillMovers();
void fillShocks(int now);
float shockMask(const float low[3], const float high[3], float cell, int slot);
float joinRadius(const Volume &volume);
const Solid *structureAt(const Coord3D &where, ObjectID id);
void clearFires();

// The setting, read every time: Off, Low or High from the options menu, or -volsmoke for the run.
// A change drops every volume, program and texture, and forgets every system taken, so with Off
// the next frame draws the billboards again, and with a new level the atlases are made again at
// its size the next time a blast asks.
bool requested()
{
	const int want = TheGlobalData ? (std::min)((std::max)(TheGlobalData->m_volumeSmoke, 0), 2) : 0;
	if (want != level) {
		releaseState();
		takenSystems.clear();
		firstSeen.clear();
		airSystems.clear();
		airOutside.clear();
		tallSystems.clear();
		movers.clear();
		shocks.clear();
		fronts.clear();
		pendingSparks.clear();
		pendingHits.clear();
		ownedHits.clear();
		gunSystems.clear();
		hulls.clear();
		lastHulls.clear();
		solids.clear();
		lastSolids.clear();
		solidsFrame = -100000;
		lastReport = 0;
		clearFires();
		level = want;
		TierShapes[0].slabs = Qualities[level].slabs[0];
		TierShapes[0].gz = Qualities[level].smallHeight;
		TierShapes[1].slabs = Qualities[level].slabs[1];
		TierShapes[1].first = TierShapes[0].slabs;
		TierShapes[2] = { Qualities[level].fireSide, Qualities[level].fireSide, Qualities[level].fireHeight,
			Qualities[level].fireSlabs, TierShapes[1].first + TierShapes[1].slabs };
		DEBUG_LOG(("VOLSMOKE: setting %s\n", level == 0 ? "Off" : level == 1 ? "Low" : "High"));
	}
	return level > 0;
}

void dropTier(Tier &tier)
{
	for (ID3D11ComputeShader *&program : tier.compute) drop(program);
	drop(tier.march);
	drop(tier.simConstants);
	for (unsigned i = 0; i < 2; ++i) {
		drop(tier.velocity[i]);
		drop(tier.density[i]);
		drop(tier.pressure[i]);
	}
	drop(tier.curl);
	drop(tier.divergence);
	drop(tier.light);
	drop(tier.floorView);
	drop(tier.floor);
	tier = Tier();
}

// the air's (g 0) or the tall air's (g 1) grid and particle lists
void releaseAir(unsigned g)
{
	dropTier(g ? state.tall : state.air);
	drop(state.sourcesView[g]);
	drop(state.tileRangeView[g]);
	drop(state.tileIndexView[g]);
	drop(state.sources[g]);
	drop(state.tileRange[g]);
	drop(state.tileIndex[g]);
}

void releaseState()
{
	for (Tier &tier : state.tiers) dropTier(tier);
	drop(state.blackbodyView);
	drop(state.blackbody);
	releaseAir(0);
	releaseAir(1);
	drop(state.fullscreen);
	drop(state.linear);
	drop(state.wrap);
	drop(state.premultiplied);
	drop(state.rasterizer);
	drop(state.drawConstants);
	drop(state.noiseView);
	drop(state.noise);
	state = State();
	fireTaken.clear();	// their slabs are gone: the billboards draw until a new slab is warm
	for (Volume &volume : volumes) volume = Volume();
	airVolume = Volume();
	tallVolume = Volume();
	airBoxes.clear();
	tallBoxes.clear();
}

bool compile(DX11BackendClass *backend, const std::string &source, const char *name, const char *profile,
	std::vector<unsigned char> &code)
{
	if (backend->Compile_Program(source, name, profile, code)) return true;
	DEBUG_LOG(("VOLSMOKE: %s did not compile: %s\n", name, backend->First_Compiler_Error()));
	return false;
}

// Billowy tiling noise, two octaves of inverted cellular distance over hashed feature points.
std::vector<unsigned char> noiseTexels()
{
	auto hash = [](unsigned x, unsigned y, unsigned z, unsigned salt) {
		unsigned h = x * 73856093u ^ y * 19349663u ^ z * 83492791u ^ salt * 2654435761u;
		h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
		return h;
	};
	auto cellular = [&](float x, float y, float z, unsigned cells, unsigned salt) {
		const float fx = x * cells, fy = y * cells, fz = z * cells;
		const int ix = int(fx), iy = int(fy), iz = int(fz);
		float best = 4;
		for (int dz = -1; dz <= 1; ++dz)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) {
					const unsigned cx = unsigned(ix + dx + int(cells)) % cells, cy = unsigned(iy + dy + int(cells)) % cells,
						cz = unsigned(iz + dz + int(cells)) % cells;
					const unsigned h = hash(cx, cy, cz, salt);
					const float px = ix + dx + (h & 255) / 255.0f, py = iy + dy + ((h >> 8) & 255) / 255.0f,
						pz = iz + dz + ((h >> 16) & 255) / 255.0f;
					const float distance = (px - fx) * (px - fx) + (py - fy) * (py - fy) + (pz - fz) * (pz - fz);
					best = (std::min)(best, distance);
				}
		return 1.0f - (std::min)(std::sqrt(best), 1.0f);
	};
	std::vector<unsigned char> texels(NoiseSide * NoiseSide * NoiseSide);
	for (unsigned z = 0; z < NoiseSide; ++z)
		for (unsigned y = 0; y < NoiseSide; ++y)
			for (unsigned x = 0; x < NoiseSide; ++x) {
				const float u = (x + 0.5f) / NoiseSide, v = (y + 0.5f) / NoiseSide, w = (z + 0.5f) / NoiseSide;
				const float value = cellular(u, v, w, 4, 1) * 0.65f + cellular(u, v, w, 8, 2) * 0.35f;
				texels[(z * NoiseSide + y) * NoiseSide + x] = (unsigned char)(std::min)(255.0f, value * 300.0f);
			}
	return texels;
}

bool formatsTaken(ID3D11Device *device)
{
	const UINT wanted = D3D11_FORMAT_SUPPORT_TEXTURE3D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE
		| D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
	for (DXGI_FORMAT format : { VelocityFormat, DensityFormat, ScalarFormat }) {
		UINT support = 0;
		if (FAILED(device->CheckFormatSupport(format, &support)) || (support & wanted) != wanted) return false;
	}
	return true;
}

// A buffer of `count` structures the CPU rewrites every step, and its view.
bool makeStructured(ID3D11Device *device, unsigned count, unsigned stride, ID3D11Buffer *&buffer,
	ID3D11ShaderResourceView *&view)
{
	D3D11_BUFFER_DESC description = {};
	description.ByteWidth = count * stride;
	description.Usage = D3D11_USAGE_DYNAMIC;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	description.StructureByteStride = stride;
	D3D11_SHADER_RESOURCE_VIEW_DESC shown = {};
	shown.Format = DXGI_FORMAT_UNKNOWN;
	shown.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	shown.Buffer.NumElements = count;
	return SUCCEEDED(device->CreateBuffer(&description, nullptr, &buffer))
		&& SUCCEEDED(device->CreateShaderResourceView(buffer, &shown, &view));
}

bool makeTier(DX11BackendClass *backend, ID3D11Device *device, Tier &tier, const TierShape &shape, bool air, bool fire = false)
{
	tier.shape = shape;
	tier.air = air;
	const std::string header = std::string(air ? "#define AIR 1\n" : "") + "#define GX " + std::to_string(shape.gx)
		+ "\n#define GY " + std::to_string(shape.gy) + "\n#define GZ " + std::to_string(shape.gz) + "\n#define SLABS "
		+ std::to_string(shape.slabs) + "\n" + Grid;
	const std::string prelude = header + Prelude;
	static const char *const names[ProgramCount] = { "volsmoke curl", "volsmoke advect", "volsmoke divergence",
		"volsmoke jacobi", "volsmoke project", "volsmoke light", "volsmoke scroll" };
	static const char *const fireNames[ProgramCount] = { "volfire curl", "volfire advect", "volfire divergence",
		"volfire jacobi", "volfire project", "volfire light", "volfire scroll" };
	const std::string sources[ProgramCount] = {
		prelude + CurlPass,
		fire ? prelude + MoverPass + FireWalls + FireHelpers + FireAdvectPass : prelude + MoverPass + AdvectPass + MushroomPass + AdvectPassEnd,
		fire ? prelude + FireHelpers + FireDivergencePass : prelude + DivergencePass,
		prelude + PressureHelpers + JacobiPass, prelude + PressureHelpers + ProjectPass,
		prelude + (fire ? FireLightPass : LightPass), prelude + ScrollPass };
	std::vector<unsigned char> code;
	bool made = true;
	for (unsigned pass = 0; made && pass < ProgramCount; ++pass)
		if (pass != ScrollProgram || air)	// only the air moves with the camera
			made = compile(backend, sources[pass], (fire ? fireNames : names)[pass], "cs_5_0", code)
				&& SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &tier.compute[pass]));
	made = made && compile(backend, fire ? header + FireWalls + FireMarchPass : header + MarchPass, fire ? "volfire march" : "volsmoke march",
			"ps_5_0", code)
		&& SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &tier.march));
	const DXGI_FORMAT density = air ? VelocityFormat : DensityFormat;	// the air's four channels
	for (unsigned i = 0; made && i < 2; ++i)
		made = make(device, tier.velocity[i], VelocityFormat, shape) && make(device, tier.density[i], density, shape)
			&& make(device, tier.pressure[i], ScalarFormat, shape);
	made = made && make(device, tier.curl, VelocityFormat, shape) && make(device, tier.divergence, ScalarFormat, shape)
		&& make(device, tier.light, ScalarFormat, shape);
	D3D11_TEXTURE2D_DESC floor = {};
	floor.Width = shape.gx;
	floor.Height = shape.gy * shape.slabs;
	floor.MipLevels = floor.ArraySize = 1;
	floor.Format = DXGI_FORMAT_R32_FLOAT;
	floor.SampleDesc.Count = 1;
	floor.Usage = D3D11_USAGE_DEFAULT;
	floor.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	made = made && SUCCEEDED(device->CreateTexture2D(&floor, nullptr, &tier.floor))
		&& SUCCEEDED(device->CreateShaderResourceView(tier.floor, nullptr, &tier.floorView));
	D3D11_BUFFER_DESC buffer = {};
	buffer.Usage = D3D11_USAGE_DYNAMIC;
	buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buffer.ByteWidth = sizeof(SimConstants);
	return made && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &tier.simConstants));
}

bool makeState(DX11BackendClass *backend, ID3D11Device *device)
{
	if (state.device == device) return !state.refused;
	const double started = DX11Resource_Milliseconds_Now();
	releaseState();
	state.device = device;
	bool made = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0 && formatsTaken(device);
	for (unsigned t = 0; made && t < 2; ++t) made = makeTier(backend, device, state.tiers[t], TierShapes[t], false);
	const AirShape &air = AirShapes[level];
	auto makeAir = [&](unsigned g) {
		const AirShape &grid = g ? TallShapes[level] : air;
		return makeTier(backend, device, g ? state.tall : state.air, { grid.gx, grid.gy, grid.gz, 1, 0 }, true)
			&& makeStructured(device, grid.particles * 2, 16, state.sources[g], state.sourcesView[g])
			&& makeStructured(device, (grid.gx / 8) * (grid.gy / 8) * (grid.gz / 8), 8, state.tileRange[g], state.tileRangeView[g])
			&& makeStructured(device, grid.particles * AirTileEntries, 4, state.tileIndex[g], state.tileIndexView[g]);
	};
	made = made && makeAir(0);
	// the tall air is a nicety: a device short of its 45 MB keeps the rest
	if (made && !makeAir(1)) {
		releaseAir(1);
		state.tallRefused = true;
		DEBUG_LOG(("VOLSMOKE: the device refused the tall air; trails that climb out of the air keep their billboards\n"));
	}
	// The fires are a nicety too: a device that will not make them keeps their billboards.  Their
	// black body: colour (brightest channel 1) and brightness against 1500 K, 800 to 3000 K.
	if (made && TierShapes[2].slabs) {
		float texels[64 * 4];
		float rgb[64 * 3];
		VolumeFire::blackbodyTable(rgb, 64, 800.0f, 3000.0f);
		for (unsigned i = 0; i < 64; ++i) {
			for (unsigned k = 0; k < 3; ++k) texels[i * 4 + k] = rgb[i * 3 + k];
			texels[i * 4 + 3] = VolumeFire::blackbodyLuminance(800.0f + 2200.0f * i / 63.0f);
		}
		D3D11_TEXTURE1D_DESC description = {};
		description.Width = 64;
		description.MipLevels = description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		description.Usage = D3D11_USAGE_IMMUTABLE;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		const D3D11_SUBRESOURCE_DATA data = { texels, sizeof(texels), 0 };
		const bool fire = makeTier(backend, device, state.tiers[2], TierShapes[2], false, true)
			&& SUCCEEDED(device->CreateTexture1D(&description, &data, &state.blackbody))
			&& SUCCEEDED(device->CreateShaderResourceView(state.blackbody, nullptr, &state.blackbodyView));
		if (!fire) {
			dropTier(state.tiers[2]);
			drop(state.blackbodyView);
			drop(state.blackbody);
			state.fireRefused = true;
			DEBUG_LOG(("VOLSMOKE: the device refused the fires; they keep their billboards\n"));
		}
	}
	std::vector<unsigned char> code;
	made = made && compile(backend, HazardCloud::vertexShader(), "volsmoke fullscreen", "vs_5_0", code)
		&& SUCCEEDED(device->CreateVertexShader(code.data(), code.size(), nullptr, &state.fullscreen));

	if (made) {
		const std::vector<unsigned char> texels = noiseTexels();
		D3D11_TEXTURE3D_DESC description = {};
		description.Width = description.Height = description.Depth = NoiseSide;
		description.MipLevels = 1;
		description.Format = DXGI_FORMAT_R8_UNORM;
		description.Usage = D3D11_USAGE_IMMUTABLE;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		const D3D11_SUBRESOURCE_DATA data = { texels.data(), NoiseSide, NoiseSide * NoiseSide };
		made = SUCCEEDED(device->CreateTexture3D(&description, &data, &state.noise))
			&& SUCCEEDED(device->CreateShaderResourceView(state.noise, nullptr, &state.noiseView));
	}

	D3D11_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	made = made && SUCCEEDED(device->CreateSamplerState(&sampler, &state.linear));
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	made = made && SUCCEEDED(device->CreateSamplerState(&sampler, &state.wrap));

	D3D11_BLEND_DESC blend = {};
	D3D11_RENDER_TARGET_BLEND_DESC &target = blend.RenderTarget[0];
	target.BlendEnable = TRUE;
	target.SrcBlend = target.SrcBlendAlpha = D3D11_BLEND_ONE;
	target.DestBlend = target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	made = made && SUCCEEDED(device->CreateBlendState(&blend, &state.premultiplied));

	D3D11_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	rasterizer.ScissorEnable = TRUE;
	made = made && SUCCEEDED(device->CreateRasterizerState(&rasterizer, &state.rasterizer));

	D3D11_BUFFER_DESC buffer = {};
	buffer.Usage = D3D11_USAGE_DYNAMIC;
	buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buffer.ByteWidth = sizeof(DrawConstants);
	made = made && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &state.drawConstants));

	if (!made) {
		DEBUG_LOG(("VOLSMOKE: the Direct3D 11 device refused the smoke volumes; billboards stay\n"));
		releaseState();
		state.device = device;
		state.refused = true;
		return false;
	}
	DEBUG_LOG(("VOLSMOKE: programs, %u + %u volumes (the small %u cells tall), a %ux%ux%u air at %.1f units a cell, %s and %u %ux%ux%u fires made in %.1f ms\n",
		TierShapes[0].slabs, TierShapes[1].slabs, TierShapes[0].gz, air.gx, air.gy, air.gz, air.cell,
		state.tallRefused ? "no tall air" : level == 2 ? "a 96 cell tall air at 12" : "a 48 cell tall air at 24",
		state.fireRefused ? 0u : TierShapes[2].slabs, TierShapes[2].gx, TierShapes[2].gy, TierShapes[2].gz,
		DX11Resource_Milliseconds_Now() - started));
	return true;
}

bool usable()
{
	// the Classic box is the game's own picture, as it is for the hazard clouds and GPU particles
	if (!requested() || !Direct3D11_Present_Is_Enabled() || TheGlobalData->m_classicGraphics) return false;
	DX11BackendClass *backend = Direct3D11_Backend();
	DX11DeviceClass *device = Direct3D11_Device();
	if (!backend || !device || !device->Get_Device() || !device->Get_Depth_Texture()) return false;
	return makeState(backend, device->Get_Device());
}

bool upload(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const void *data, size_t bytes)
{
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
	std::memcpy(mapped.pData, data, bytes);
	context->Unmap(buffer, 0);
	return true;
}

// The systems that start a blast, by the template names of the FX lists that spawn them.  A tank's
// is its DEATH_EXPLOSION smoke; a shell's the generic missile and shell smoke; a bomb's the carpet
// and artillery barrage puffs, the Scud's arms and the Nuke Cannon's stem; a nuclear blast's the
// mushroom of the missile, the GLA's and the MOAB.  Unknown templates, mods' included, are left
// alone.
TemplateKind kindOfTemplate(const ParticleSystemTemplate *tmplate, ParticleSystem *system)
{
	auto known = templateKinds.find(tmplate);
	if (known != templateKinds.end()) return known->second;
	const char *name = tmplate->getName().str();
	TemplateKind kind = { false, Shell, 1.0f };
	const bool alpha = system->getShaderType() == ParticleSystemInfo::ALPHA && !tmplate->isEmissive();
	// the flare starts it too: it goes off at the impact, the mushroom's first system 0.2 s later
	if ((std::strstr(name, "Mushroom") && !std::strstr(name, "NukeCannon") && !std::strstr(name, "Chemical"))
			|| !std::strcmp(name, "NukeFlare") || !std::strcmp(name, "NukeGLAFlare"))
		kind = { true, Nuclear, 1.0f };
	else if (std::strstr(name, "NukeCannonMushroomStem"))
		kind = { true, Shell, 3.0f };
	else if (alpha && (std::strstr(name, "CarpetBombExplosionPuff") || std::strstr(name, "ScudMissleLauncherExplosionArms")))
		kind = { true, Shell, 2.4f };
	else if (alpha && std::strstr(name, "ArtilleryBarrageDust"))
		kind = { true, Shell, 1.6f };
	else if (alpha && std::strstr(name, "BuggyMissileExplosionSmoke"))
		kind = { true, Shell, 1.0f };
	// a missile's launch: the Scud Storm's, the Scud Launcher's and the Tomahawk's ignition cloud, the
	// nuclear missile's
	else if (alpha && !std::strcmp(name, "ScudStormIgnitionCloud"))
		kind = { true, Launch, 1.0f };
	else if (alpha && !std::strcmp(name, "NeutronMissleLaunch"))
		kind = { true, Launch, 1.3f };
	// a building's death and its damaged stages (FX_Structure*Death, TransitionDamageFX), and
	// FX_SmallStructureDeath's
	else if (alpha && std::strstr(name, "Smoke") && (!std::strncmp(name, "StructureTransition", 19)
			|| !std::strcmp(name, "StructureExplosionSmoke")))
		kind = { true, Structure, 1.0f };
	// a building's damage and death smoke is DEATH_EXPLOSION too; buildings are not tanks
	else if (alpha && system->getPriority() == DEATH_EXPLOSION && std::strstr(name, "Smoke") && !std::strstr(name, "Structure"))
		kind = { true, TankDeath, 1.0f };
	// a tank gun's shot: its flame, which the FX turns along the barrel (the Battlemaster's and the
	// Scorpion's smaller gun, WeaponFX_GenericTankGunNoTracerSmall, at three quarters)
	else if (std::strstr(name, "TankMuzzleFlashFlame"))
		kind = { true, Muzzle, 1.0f };
	else if (std::strstr(name, "TankSmallMuzzleFlashFlame"))
		kind = { true, Muzzle, 0.75f };
	templateKinds[tmplate] = kind;
	return kind;
}

float smoothstep01(float x, float low, float high)
{
	const float u = (std::min)((std::max)((x - low) / (high - low), 0.0f), 1.0f);
	return u * u * (3 - 2 * u);
}

float hashUnit(unsigned seed, unsigned salt)
{
	unsigned h = seed * 2654435761u ^ salt * 2246822519u;
	h ^= h >> 15; h *= 0x5bd1e995u; h ^= h >> 13;
	return (h & 0xffff) / 65535.0f;
}

// One emitter: cell position and radius; the velocity it pushes the flow to, its soot a second;
// its heat, its dust a second, how hard it pushes.
struct Emitter { float x, y, z, radius, vx, vy, vz, soot, heat, dust, push; };

void emit(unsigned slab, unsigned &count, const Emitter &e)
{
	if (count >= MaxPuffs) return;
	float *puff = sim.Puffs[(slab * MaxPuffs + count++) * 3];
	const float values[12] = { e.x, e.y, e.z, e.radius, e.vx, e.vy, e.vz, e.soot, e.heat, e.dust, e.push, 0 };
	std::memcpy(puff, values, sizeof(values));
}

// A fire's slab this step: an emitter a burning system of its site (VolumeFireCore), laying fuel
// for the heat its stock flame stands for, puffing at its pool's frequency, pushed out of the
// building's meshes when its bone is inside one; and its constants, in this slab's cells.
void scheduleFire(const Volume &volume, unsigned slab, DrawConstants *draw)
{
	const int now = TheGameLogic ? (int)TheGameLogic->getFrame() : volume.born;
	const float seconds = (float)now / LOGICFRAMES_PER_SECOND, cell = volume.cell;
	const TierShape &shape = TierShapes[2];
	static std::vector<VolumeFire::Emitter> list;
	list.clear();
	unsigned object = 0;
	if (volume.site >= 0 && volume.site < (int)fires.sites.size()) {
		fires.emitters(volume.site, now, LOGICFRAMES_PER_SECOND, list);
		object = ownerOf(fires.sites[volume.site].object);
	}
	VolumeFire::PartBox boxes[MaxParts];
	unsigned boxCount = 0;
	if (const Solid *building = object ? structureAt(Coord3D(), (ObjectID)object) : nullptr) {
		const float c = std::cos(building->angle), s = std::sin(building->angle), base = building->top - building->tall;
		for (unsigned i = 0; i < building->partCount; ++i) {
			const Part &p = building->parts[i];
			boxes[boxCount++] = { building->x + p.along * c - p.across * s, building->y + p.along * s + p.across * c, c, s,
				p.halfAlong, p.halfAcross, base + p.bottom, base + p.top };
		}
		if (!boxCount)
			boxes[boxCount++] = { building->x, building->y, c, s, building->major, building->box ? building->minor : building->major,
				base, building->top };
	}
	// what layFloor makes solid of a fire's own building: its footprint, to its lowest mesh
	const Solid *own = object ? structureAt(Coord3D(), (ObjectID)object) : nullptr;
	float footprintTop = -1e9f;
	if (own)
		for (unsigned i = 0; i < own->partCount; ++i)
			if (own->parts[i].bottom < 4.0f)
				footprintTop = footprintTop < -1e8f ? own->parts[i].top : (std::min)(footprintTop, own->parts[i].top);
	if (own && footprintTop > -1e8f) footprintTop += own->top - own->tall;
	auto onFootprint = [own](float x, float y) {
		const float dx = x - own->x, dy = y - own->y, c = std::cos(own->angle), s = std::sin(own->angle);
		return own->box ? std::fabs(dx * c + dy * s) <= own->major && std::fabs(-dx * s + dy * c) <= own->minor
			: dx * dx + dy * dy <= own->major * own->major;
	};
	unsigned count = 0;
	auto surfaceFound = object ? fireSurfaces.find(object) : fireSurfaces.end();
	// buildings only: a wreck or prop sits in the height map on a slope, and its mesh points under the
	// ground left CarJunk01 without a flame; its bone does fine
	const FireSurface *surface = own && surfaceFound != fireSurfaces.end() && !surfaceFound->second.points.empty() ? &surfaceFound->second : nullptr;
	for (size_t index = 0; index < list.size(); ++index) {
		const VolumeFire::Emitter &e = list[index];
		if (surface) {
			// The fuel comes off the mesh itself: the up-facing faces and the wall tops nearest the
			// bone within the fire's reach, half a cell out along their normals, so the flame's foot
			// follows the roof, the walls and the broken edges.  None in reach: no fuel (no fire in
			// empty air), whatever the bone says.
			// Picked once a system and model: the bone is the youngest particle, which climbs between
			// bursts, and picking again as it moved redid the scan most steps and walked the foot up the wall.
			// (again when it is seen a cell lower: the first sample can be a particle that has climbed
			// off its bone, and the lowest seen is the bone)
			SurfacePick &pick = surfacePicks[e.id];
			if (pick.key != surface->key || e.z < pick.bone[2] - cell) {
				pick.key = surface->key;
				pick.bone[0] = e.x; pick.bone[1] = e.y; pick.bone[2] = e.z;
				pick.count = 0;
				const float reach = (std::max)((std::max)(e.radius * 1.5f, 2.0f * cell), 6.0f);
				float best[4] = { 1e30f, 1e30f, 1e30f, 1e30f };
				for (const SurfacePoint &p : surface->points) {
					const float dx = p.x - e.x, dy = p.y - e.y, dz = p.z - e.z, d2 = dx * dx + dy * dy + dz * dz;
					if (d2 > 9.0f * reach * reach) continue;
					// the up-facing faces first: a roof under the bone over the wall beside it
					const float score = d2 * (p.nz > 0.35f ? 1.0f : 2.0f);
					unsigned at = pick.count < 4 ? pick.count : 4;
					while (at > 0 && best[at - 1] > score) --at;
					if (at >= 4) continue;
					// a cell apart, so four points are four places
					bool close = false;
					for (unsigned k = 0; k < pick.count && !close; ++k)
						close = std::fabs(pick.points[k].x - p.x) + std::fabs(pick.points[k].y - p.y) + std::fabs(pick.points[k].z - p.z) < cell;
					if (close) continue;
					for (unsigned k = (std::min)(pick.count, 3u); k > at; --k) { pick.points[k] = pick.points[k - 1]; best[k] = best[k - 1]; }
					pick.points[at] = p;
					best[at] = score;
					pick.count = (std::min)(pick.count + 1, 4u);
				}
			}
			// The puffs are shared, so every system gets one before any gets four: four each fed only seven
			// systems of a War Factory's and left the rest burning as billboards beside the volume.
			const unsigned left = MaxPuffs > count ? MaxPuffs - count : 0, systemsLeft = unsigned(list.size() - index);
			const unsigned points = (std::min)(pick.count, (std::max)(left / systemsLeft, 1u));
			// the first `points` of its picks that are in free air, and the system's heat over those
			float kept[4][3];
			unsigned keptCount = 0;
			for (unsigned k = 0; k < pick.count && keptCount < points; ++k) {
				const SurfacePoint &p = pick.points[k];
				const float x = p.x + p.nx * 0.6f * cell, y = p.y + p.ny * 0.6f * cell, z = p.z + p.nz * 0.6f * cell;
				const float cx = (x - volume.origin[0]) / cell - 0.5f, cy = (y - volume.origin[1]) / cell - 0.5f, cz = (z - volume.origin[2]) / cell - 0.5f;
				if (cx < 2 || cy < 2 || cx > shape.gx - 3.0f || cy > shape.gy - 3.0f || cz < 0 || cz > shape.gz - 8.0f) continue;
				// in free air, as layFloor lays this slab: over the ground, and on the building's own
				// footprint over its lowest mesh (a pad or a floor inside it would be zeroed each step)
				// (the centre over the surface is enough: its 2-cell sphere reaches free cells; half a cell
				// over it skipped every puff of a car on a slope, whose mesh sinks into the height map)
				// The floor layFloor laid, the building's own mesh in it: a point under another roof is out.
				const std::vector<float> &floor = fireFloors[slab];
				if (floor.size() == shape.gx * shape.gy) {
					const unsigned column = unsigned(cy + 0.5f) * shape.gx + unsigned(cx + 0.5f);
					if ((z - volume.origin[2]) / cell < floor[column] - 0.25f) continue;
				} else {
					float floorTop = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(x, y, NULL) : volume.origin[2];
					if (footprintTop > -1e8f && onFootprint(x, y)) floorTop = (std::max)(floorTop, footprintTop);
					if (z < floorTop) continue;
				}
				kept[keptCount][0] = cx; kept[keptCount][1] = cy; kept[keptCount][2] = cz;
				++keptCount;
			}
			const float kW = e.kW * VolumeFire::puff(e, seconds) / (keptCount ? keptCount : 1);
			// two cells at least: at 1.5 the pilot's heat at the nearest cell centre fell under the
			// ignition and nothing lit
			const float radius = (std::min)((std::max)(0.7f * e.radius / cell, 2.0f), 4.0f), rate = VolumeFire::emitterRate(kW, radius, cell);
			bool laid = false;
			for (unsigned k = 0; k < keptCount; ++k) {
				const unsigned before = count;
				emit(slab, count, { kept[k][0], kept[k][1], kept[k][2], radius, 0, 0, FireExit / cell, rate * FireSurfaceSoot, FirePilot, rate, 0.15f });
				laid = laid || count > before;	// past MaxPuffs emit lays nothing
			}
			if (laid && !draw) fireEmitting.insert(e.id);
			continue;
		}
		// No mesh read (a loose fire, or the first frames): the top of the box under its bone, half a cell up,
		// so the fuel comes off the roof or the deck itself.  A bone deep in a tall mesh (a window low in
		// a wall) goes out through the nearest wall instead.  Pushed out of the top by a cell, then lit
		// above that, a War Factory's flame stood over its roof as a ball.
		float x = e.x, y = e.y, z = e.z, surface = -1e9f;
		bool deep = false;
		for (unsigned i = 0; i < boxCount; ++i) {
			const VolumeFire::PartBox &b = boxes[i];
			const float dx = e.x - b.x, dy = e.y - b.y;
			if (std::fabs(dx * b.c + dy * b.s) > b.halfAlong || std::fabs(-dx * b.s + dy * b.c) > b.halfAcross) continue;
			if (b.top <= e.z + 2.0f * cell) surface = (std::max)(surface, b.top);
			else if (b.bottom <= e.z) deep = true;
		}
		if (deep && surface < -1e8f) VolumeFire::pushOut(x, y, z, boxes, boxCount, 0.5f * cell);
		else if (surface > -1e8f) z = surface + 0.5f * cell;
		const float cx = (x - volume.origin[0]) / cell - 0.5f, cy = (y - volume.origin[1]) / cell - 0.5f, cz = (z - volume.origin[2]) / cell - 0.5f;
		const float radius = (std::min)((std::max)(e.radius / cell, 1.5f), 4.0f);	// under 1.5 cells it can miss every cell centre
		if (cx < 2 || cy < 2 || cx > shape.gx - 3.0f || cy > shape.gy - 3.0f || cz < 0 || cz > shape.gz - 8.0f) continue;
		const float kW = e.kW * VolumeFire::puff(e, seconds), rate = VolumeFire::emitterRate(kW, radius, cell);
		emit(slab, count, { cx, cy, cz, radius, 0, 0, FireExit / cell, rate * FireSurfaceSoot, FirePilot, rate, 0.15f });
		if (!draw) fireEmitting.insert(e.id);
	}
	sim.Volumes[slab][1] = (float)count;
	const float fire[4] = { FireBurnRate, VolumeFire::HeatPerFuel, FireSootYield, FireIgnition };
	const float fire2[4] = { FireRich, FireRadiation, FireSootBurn, FireExpansion };
	const float flame = volume.site >= 0 && volume.site < (int)fires.sites.size() ? (std::max)(fires.sites[volume.site].flame, 12.0f) : 18.0f;
	const float fire3[4] = { FireLift / cell, FireTurbulence * (std::min)(flame / 18.0f, 2.5f) / cell, FireQuench, cell / (0.3f * flame) };
	std::memcpy(sim.Fire[slab], fire, sizeof(fire));
	std::memcpy(sim.Fire2[slab], fire2, sizeof(fire2));
	std::memcpy(sim.Fire3[slab], fire3, sizeof(fire3));
	if (draw) {
		const float soot[4] = { 0.07f, 0.065f, 0.06f, 0 }, look[4] = { ShadeLight, 0, SunShare, 0 };	// a burning building's smoke is near black
		const float fireLook[4] = { FireGain, FireIgnition, FireRich, FireExposure };
		// the reaction zone's faint blue (CH and C2 light, not a black body's), and the flame's light on its smoke
		const float fireTint[4] = { 0.015f, 0.03f, 0.07f, Qualities[level].fireLight };
		std::memcpy(draw->Soot, soot, sizeof(soot));
		std::memset(draw->Dust, 0, sizeof(draw->Dust));
		std::memset(draw->Blast, 0, sizeof(draw->Blast));
		std::memset(draw->Effects, 0, sizeof(draw->Effects));
		// the tongues' noise (FireMarchPass): a flame across a tile, two and a half tall, rising at
		// about a flame height a second
		const float tongues[4] = { 1.0f / flame, 1.0f / (2.5f * flame), FireTongueRise / 2.5f, 0 };
		std::memcpy(draw->Extra, tongues, sizeof(tongues));
		std::memcpy(draw->Look, look, sizeof(look));
		std::memcpy(draw->FireLook, fireLook, sizeof(fireLook));
		std::memcpy(draw->FireTint, fireTint, sizeof(fireTint));
	}
}

// A ballistic jet from the blast: where it is at t, and its velocity; false once it has landed.
bool jetAt(const Volume &volume, unsigned index, float speed, float tiltLow, float tiltHigh, float t,
	float out[6])
{
	const float azimuth = (index + hashUnit(volume.seed, index * 3)) * 6.2831853f / 10.0f;
	const float tilt = tiltLow + (tiltHigh - tiltLow) * hashUnit(volume.seed, index * 3 + 1);
	// a third of them faster: the separate spikes that stand out of a dirt fountain
	const float fast = hashUnit(volume.seed, index * 3 + 2) > 0.7f ? 1.6f : 0.75f + 0.5f * hashUnit(volume.seed, index + 77);
	const float v = speed * fast;
	const float g = Gravity / volume.cell;
	const float vx = v * std::sin(tilt) * std::cos(azimuth), vy = v * std::sin(tilt) * std::sin(azimuth);
	const float vz = v * std::cos(tilt) - g * t;
	out[0] = volume.blast[0] + vx * t;
	out[1] = volume.blast[1] + vy * t;
	out[2] = volume.blast[2] + v * std::cos(tilt) * t - 0.5f * g * t * t;
	out[3] = vx; out[4] = vy; out[5] = vz;
	return out[2] > 0.5f;
}

// The timeline: the constants of one slab at t seconds after the blast.  Returns the draw's flash
// and condensation shell, and the soot's and the dust's colours, through the draw constants.
void schedule(const Volume &volume, unsigned slab, float t, DrawConstants *draw)
{
	if (volume.kind == Fire) {
		scheduleFire(volume, slab, draw);
		return;
	}
	float *blast = sim.Blasts[slab], *power = sim.Power[slab], *shape = sim.Shape[slab], *motion = sim.Motion[slab];
	for (unsigned i = 0; i < 3; ++i) blast[i] = volume.blast[i];
	blast[3] = 2.0f;
	std::memset(power, 0, 16);
	std::memset(shape, 0, 16);
	std::memset(motion, 0, 16);
	float *cap = sim.Cap[slab], *stem = sim.Stem[slab];
	std::memset(cap, 0, 16);
	std::memset(stem, 0, 16);
	shape[2] = 1000.0f;		// no ceiling
	unsigned count = 0;
	float flash = 0, shellRadius = 0, shellOpacity = 0, flashTall = 1.0f, flashWide = 3.0f;
	float collarOpacity = 0, collarRadius = 0, collarHeight = 0, shade = ShadeLight, billows = 0, sunShare = SunShare, spared = 0;
	float soot[4] = { 0.06f, 0.055f, 0.05f, 1.6f }, dust[3] = { 0.52f, 0.43f, 0.31f };
	const float bx = volume.blast[0], by = volume.blast[1], bz = volume.blast[2];

	if (volume.kind == TankDeath) {
		// After a real vehicle's (reference/vehicle-explosion.png): no flash ball and no rays.  The
		// light is the fire itself.  Everything is placed on the hull: `along` its length, `across`
		// it, `up` from the ground, in cells, with the velocities in the same frame.
		const float hx = volume.hull[0], hy = volume.hull[1], L = volume.hull[2], W = volume.hull[3], H = volume.hull[4];
		auto onHull = [&](float along, float across, float up, float radius, float va, float vc, float vz, float sootRate,
			float heat, float dustRate, float push) {
			emit(slab, count, { bx + hx * along - hy * across, by + hy * along + hx * across, bz + up, radius,
				hx * va - hy * vc, hy * va + hx * vc, vz, sootRate, heat, dustRate, push });
		};
		auto unit = [&](unsigned salt) { return hashUnit(volume.seed, salt); };
		// The dust channel carries the near-black soot masses at the base (below): it never glows (the
		// march lights fire by the soot's share of a cell alone), so laid beside the fire it stays a
		// dark mass in front of it, where soot laid there burned with the fire's heat and read as haze.
		// So the tank's dust is dark brown-black, and nothing else of it lays dust: one dust colour a
		// slab, and the jets' and the base band's dust darkened with it (pass 18) turned the fan's
		// cooled smoke and the band's ends dark on three sides of the fire from the play camera.
		// (Pale, 0.70/0.62/0.50, it was the sunlit skirt of passes 6-17.)
		dust[0] = 0.10f; dust[1] = 0.085f; dust[2] = 0.075f;
		// The base: orange flame and black soot bursting out low along the whole hull, rolling off
		// both ends and a little to the sides.  The four towards the
		// ends do not glow (heat 0.5, where fireColour starts), so the hot middle sits between dark
		// lobes, as in the photo; at 1.25 all along the base read as one bright band, and at 0.8 and
		// 0.62 the soot laid in them glowed (heat is the hottest emitter's) and read as haze.
		if (t < 0.7f) {
			const float fade = 1 - t / 0.7f;
			for (unsigned i = 0; i < 6; ++i) {
				const float s = i / 2.5f - 1 + (unit(800 + i) - 0.5f) * 0.2f;
				const float across = (unit(810 + i) - 0.5f) * W * 0.7f;
				const float radius = (std::max)(W * 0.85f, 2.2f) * (0.8f + 0.4f * unit(820 + i));
				onHull(s * L * 1.15f, across, 0.6f + 0.6f * unit(830 + i), radius, s * (8.0f + 12.0f * unit(840 + i)) * fade,
					(across < 0 ? -2.5f : 2.5f) * fade, 1.5f, 20.0f * fade, std::fabs(s) < 0.5f ? 1.25f : 0.5f, 0.0f, 0.35f);
			}
		}
		// The plume, the photo's tallest part: thirteen jets of white-hot fire and dust shot up out
		// of the hull top in the first quarter second, the middle one straight up and tallest, the
		// others leaning further out and stopping lower the further from the middle they are, so the
		// flare is one ragged column that opens upwards.  The middle one is set for about twice the
		// base band's length once the heat has lifted it on, twenty cells short of the grid's top
		// (20 on Low's 40, 36 on High's 56: hot heads rise on into the lid), and the outer ones lean
		// out up to a radian and 15 cells off the blast's axis.  Kept to 12 cells and 0.8, a High
		// plume twice as tall came out a narrow pillar.  Each lays
		// down thin smoke at a rate that goes with its speed, the same a cell along the whole jet,
		// and pushes the air only lightly.  A thick jet, a hard push or a fixed rate carried the
		// smoke along with the head and left a round ball at the top with nothing under it.  The
		// fire burns brighter for the first second (soot[3] below), so the plume flares white and
		// then hangs as grey-brown smoke and dust.
		const float reach = (std::min)((std::max)(2.2f * (2.3f * L + 1.7f * W), 12.0f), TierShapes[0].gz - 20.0f);
		// an odd count, so one jet stands straight up the middle: with fourteen the two middle ones
		// leaned apart and notched the top into a heart
		const unsigned jets = 13;	// with the base, the stem and the four dark base masses and their rims, the 28 a slab holds
		// A shorter reach (Low's, or a very small hull's on High) fans a short hull's rays less and
		// from less of its length, so its fan is as tall for its width as High's: at High's angles
		// Low's came out a round dome.  Long hulls do not use it.
		const float fanScale = 0.5f + 0.5f * reach / 36.0f;
		for (unsigned j = 0; j < jets; ++j) {
			// The tilt, azimuth, height and across-start hashes are shared by each jet and its mirror across the
			// middle, so both flanks of the fan come out alike; drawn per jet one flank grew taller and
			// thicker than the other, and on the Overlord its left read as smoke.
			const unsigned pair = (std::min)(j, jets - 1 - j);
			auto field = [&](unsigned f) {
				return unit(1000 + 16 * ((f == 1 || f == 3 || f == 4 || f == 6) ? pair : j) + f);
			};
			// a jet stops once it has slowed to a fifth of its speed: lingering at the top it heated a
			// round ball there
			const float age = t - 0.06f * field(0), slows = 0.18f;
			if (age < 0 || age > 1.5f * slows) continue;
			// Spread over the fan from one end of the hull to the other, a little bunched towards
			// the middle (side = u^1.2), with a little hash on top, so the gaps fill into one
			// column: drawn at random they bunched into four or five fingers with a ball on each.
			// The middle is the photo's column, so `centre` sets it tallest, thickest and hottest
			// and the outer jets lean shorter, thinner and orange.  The jets near the middle stand
			// nearly upright (tilt ~ side^1.1).  With every jet but the middle one tilted at least
			// 0.15 towards its own end, the two halves of the fan split apart round one thin middle
			// jet: a V, two lobes over a gap at the centre top, from the play camera a white heart.
			// Shown with the middle jet alone (a thin spike, the tallest) and all but it (two lobes).
			// The middle jet's height is not drawn, so no neighbour can stand over it; the further out
			// a jet, the more its height is left to the hash, which makes the top ragged.
			const float u = (j + 0.5f) / jets * 2 - 1;
			const float shortHull = (std::min)((std::max)((8.7f - L) / 3.7f, 0.0f), 1.0f);
			const float side = (u < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(u), 1.2f - 0.3f * shortHull), centre = 1 - std::fabs(side);
			// The lower third of a jet barely leans; between a third and three quarters of its path
			// it bends out by `crown` cells, up to three quarters of the hull's length for the outer
			// ones (side^0.5), so the base and the middle stay one mass and only the crown fans out
			// towards one and a half hulls.  Leaning the whole jet that far from the hull top split a
			// short hull's fan into a V again; bending over the last half only spread the tips,
			// which the taper had already thinned to nothing, so the crown stayed a hull wide.
			// `shortHull` is 0 for an Overlord's length (half 8.7 cells) and 1 from a Crusader's (5)
			// down, and blends the curved jets above into a fan of straight rays: they start spread
			// over nine tenths of the hull's length, lean out up to half a radian, each to about the
			// same height (the middle one a little taller), push the air a quarter as hard and the
			// outer ones thin out sooner, so the fan rises straight off the hull and its top is its
			// widest part, as in the photo.  Curved jets on a short hull spread at mid height under a
			// lone spike, and bent harder they heaped their heads into a ball on a neck; rays from
			// one point over the middle stood on a thin stem under a tulip, and at the full push
			// they carried their own smoke up into a mushroom's cap.
			const float tilt = (1 - shortHull) * 0.7f * std::pow(std::fabs(side), 1.1f) + shortHull * 0.55f * fanScale * std::fabs(side)
				+ 0.04f * (field(1) - 0.5f);
			// mirrored, so a pair leans the same way across the hull (a shared offset added to both
			// twisted the fan into an S from above)
			const float swing = (field(3) - 0.5f) * 0.9f, azimuth = side < 0 ? 3.14159265f - swing : swing;
			const float ca = std::cos(azimuth), cc = std::sin(azimuth);
			const float dz = std::cos(tilt);
			const float curvedHeight = (0.75f + 0.25f * centre * centre) * (1 - 0.3f * (1 - centre * centre * centre * centre) * field(4));
			const float coneHeight = (0.84f + 0.16f * centre * centre) * (1 - 0.12f * (1 - centre * centre * centre * centre) * field(4)) / dz;
			const float height = reach * ((1 - shortHull) * curvedHeight + shortHull * coneHeight);
			const float speed = height / slows;
			const float travelled = height * (1 - std::exp(-age / slows)), now = speed * std::exp(-age / slows);
			const float path = travelled / (0.78f * height);
			// A jet starts on the part of the hull it leans to, so the fan opens from the middle
			// and the ends rather than crossing.  A long hull's or a tall jet's lean is cut, so a
			// whole jet, head and push, ends inside 15 cells, where walls() has faded it by a few
			// percent (0.78 of its height is as far as it gets).
			const float a0 = ((0.5f + 0.4f * shortHull * fanScale) * side + (field(5) - 0.5f) * (0.4f - 0.2f * shortHull)) * L,
				c0 = (field(6) - 0.5f) * W * (0.6f - 0.3f * shortHull);
			float straight = std::sin(tilt), crown = (1 - shortHull) * (1.5f * L + 0.5f * W) * std::pow(std::fabs(side), 0.5f);
			const float lean = straight * 0.78f * height + crown;
			if (lean > 1e-3f) {
				const float k = (std::min)(1.0f, (std::max)(0.0f, 15.0f - std::sqrt(a0 * a0 + c0 * c0)) / lean);
				straight *= k;
				crown *= k;
			}
			const float bendLow = 0.3f, bendSpan = 0.45f;
			const float bendU = (path - bendLow) / bendSpan;
			const float bend = smoothstep01(path, bendLow, bendLow + bendSpan),
				bending = bendU > 0 && bendU < 1 ? 6 * bendU * (1 - bendU) / bendSpan : 0.0f;
			const float out = straight * travelled + crown * bend, outSpeed = straight * now + crown * bending * now / (0.78f * height);
			const float along = a0 + ca * out, across = c0 + cc * out, da = ca * outSpeed / (std::max)(now, 1e-3f),
				dc = cc * outSpeed / (std::max)(now, 1e-3f);
			// Smoke and heat both go with the jet's speed, hottest low down; it cools once laid.
			// No dust: the tank's dust is the dark base masses' (above); the soot is what glows (a jet
			// with as much dust as soot burned dull tan), and it cools to dark smoke.  The
			// fan of jets is what widens the plume; one jet widens only a little.  Thin jets that
			// doubled their width as they climbed stood on a thin stem under a fat top, a mushroom.
			// Wider as it climbs, then over its last stretch the jet thins, lays less and cools, so
			// it ends in a spiky streak and not a bright ball.  Heat 3.7 low in the middle jet and
			// 3.1 in the outer ones (3.1 and 2.3 on a short hull, whose fan holds more of its smoke
			// under the lighter push and burned one white shape at the long hull's heat), so the
			// whole fan burns blown-out yellow-white (fireColour past 1.6)
			// while the jets run, as the photo's does, and cools through orange once they stop.  The
			// heads keep under half of it (the taper), so the white stops short of the fan's ragged
			// edge: at 2.3 outside the fan was a white core in an orange body, and with every jet
			// at 3.2 or more to its tip whatever its taper the whole fan was one white shape, a
			// heart from the play camera.  At 1.2 the outer jets went out unseen.  A jet leaves the hull at 0.7 of its heat
			// and takes the rest over the first sixth of its path; at a third the column under the
			// crown went dark and the crown floated off the hull.  The rate goes with the reach, so Low's
			// shorter jets lay as much a cell as High's: laying the same smoke into half the length
			// heaped a white ball on the hull on Low.
			// a cone's rays stay full nearly to their heads, so its top is the widest part
			const float taper = 1 - smoothstep01(path, 0.55f + 0.25f * shortHull * (0.4f + 0.6f * centre), 1.0f);
			const float rate = 45.0f * (0.75f + 0.45f * centre) * reach / 36.0f * now / speed * taper;
			const float heat = (3.1f - 0.8f * shortHull + (0.6f + 0.2f * shortHull) * centre * centre)
				* (0.7f + 0.3f * std::pow(now / speed, 0.4f)) * (1 - (0.2f + 0.3f * shortHull * (1 - centre)) * path)
				* (0.45f + 0.55f * taper) * (0.7f - 0.25f * shortHull + (0.3f + 0.25f * shortHull) * smoothstep01(path, 0.0f, 0.15f));
			onHull(along, across, H * 0.4f + dz * travelled, (2.0f + 0.06f * travelled) * (0.35f + 0.65f * taper)
				* (0.8f + 0.2f * shortHull + (0.5f - 0.2f * shortHull) * centre * centre),
				da * now, dc * now, dz * now, rate, heat, 0.0f, 0.08f * (1 - 0.75f * shortHull));
		}
		// The stem: one wide hot puff climbing at 0.4 of the middle jet's pace,
		// so the column under the crown stays lit and the crown does not float off the hull.
		// A short hull's lives to half a second and stays low and as wide as the hull, filling the
		// fan's foot rather than standing as a neck under it.
		const float stemLife = 0.3f + 0.2f * (std::min)((std::max)((8.7f - L) / 3.7f, 0.0f), 1.0f);
		if (t < stemLife) {
			const float stemShort = (std::min)((std::max)((8.7f - L) / 3.7f, 0.0f), 1.0f), pace = 0.4f - 0.25f * stemShort;
			const float climbed = pace * reach * (1 - std::exp(-t / 0.18f)), fade = 1 - t / stemLife;
			// (on a short hull no wider than half its length and only orange-hot, or it lit the whole
			// base white and the dark soot beside it never showed)
			onHull(0, 0, H * 0.4f + climbed, (std::max)((std::max)(W * (1.2f - 0.4f * stemShort), L * 0.6f), 3.0f) * (1 + climbed / reach), 0, 0,
				pace * reach / 0.18f * std::exp(-t / 0.18f), 25.0f * reach / 36.0f * fade, 2.8f - 1.2f * stemShort, 0.0f, 0.05f);
		}
		// the default 1.6 once the flare is over; about a third of a second of white, as long as
		// the jets run, and no longer, so several tanks dying at once do not white out the view
		soot[3] = 1.6f + 4.2f * std::exp(-t / 0.22f);
		// Near-black masses banked at the base, as in the photo, in front of the fire with flame
		// showing between them: two on each long flank near the middle, thick (opaque in a few cells)
		// and laid in the dust channel, which never glows, so the base band's heat beside them does
		// not light them.  Soot lobes (passes 12-17) burned with whatever heat they sat in and read as
		// haze from the side, wherever along the hull they were put.  Two a side and off the ends, no
		// ring from the play camera (round the whole hull, pass 12, they closed into one).  Pushed out
		// slowly, swelling by half, gone in about a second: heavy, they slump onto the ground, which
		// eats the dust lying on it (GroundDustHalfLife).
		// (at 70 a second over a hull and a half for 0.85 s they closed round the wreck into a black
		// ring from the play camera and lay on as a dark disc; four masses of 0.8 the hull's width
		// at 0.35 of its length either way met into one dark band from the side)
		// Each is a round blob with clear air between it and the next: its dust peaks in the middle
		// and falls off to its edge, and a ring of hot soot round it (a second puff a third wider
		// laying a little soot at heat 0.85) glows red-orange where the blob's own dust thins out,
		// while its black middle, all dust, does not light.  With the base, jets and stem, the 28 a slab holds.
		if (t > 0.05f && t < 0.75f) {
			const float fade = 1 - (t - 0.05f) / 0.7f, swell = 1 + 0.3f * (std::min)(t / 0.4f, 1.0f);
			const float size = (std::min)((std::max)(W * 0.6f, 2.0f), 0.35f * L);
			for (unsigned k = 0; k < 4; ++k) {
				const float s = (k < 2 ? -0.5f : 0.5f) * (0.9f + 0.2f * unit(980 + k)), side = (k & 1) ? 1.0f : -1.0f;
				const float wander = 6.2831853f * unit(988 + k) + t * (2.0f + 1.5f * unit(992 + k));
				// (at 0.4 and at 0.75 to 0.95 of the hull's height they lay black on the black hull's side
				// and did not show from the side: the photo's stand against the flame, so these stand at
				// the hull's top, in front of the fan's foot)
				// (out past the flank, at 1.25 of the half width: at the hull's edge the jets' roots drew
				// them up into the fan and they never showed as blobs)
				const float along = s * L, across = side * W * (1.25f + 0.1f * unit(984 + k));
				const float up = H * (0.9f + 0.2f * unit(986 + k)) + 0.3f * std::sin(wander);
				const float radius = size * swell * (0.9f + 0.2f * unit(996 + k));
				const float va = s * 1.5f * fade + 1.0f * std::cos(wander), vc = side * 1.5f * fade;
				onHull(along, across, up, radius, va, vc, 0.8f, 0, 0, 55.0f * fade * fade, 0.3f);
				onHull(along, across, up, radius * 1.35f, va, vc, 0.8f, 6.0f * fade, 0.85f, 0, 0.1f);
			}
		}
		// the cook-off pops, two to four, each a hot orange puff out of a hatch somewhere on the hull
		const unsigned pops = 2 + unsigned(unit(900) * 2.99f);
		float when = 0.22f;	// all of them inside the first 1.75 s, as the column
		for (unsigned p = 0; p < pops; ++p) {
			when += 0.12f + 0.25f * unit(901 + p);
			const float age = t - when;
			if (age >= 0 && age < 0.15f)
				onHull((unit(910 + p) - 0.5f) * L * 1.4f, (unit(920 + p) - 0.5f) * W * 0.8f, H * 0.6f, 2.4f, 0, 0, 12.0f,
					12.0f, 1.7f, 0.0f, 0.3f);
		}
		// The hull burning: a black column off its whole length for about a second and a half, which
		// with the soot thinning (TankSootHalfLife) is gone about 2 s after the death, a quarter as
		// long as the game's own billboard column (ReforgedTankSmoke), as the owner asked for a fast
		// game (halved twice, from 0.5-3.5 s and 0.8-7 s).  At half a minute it outlived the wreck.
		if (t > 0.25f && t < 1.75f) {
			const float rise = (std::min)((t - 0.25f) / 0.38f, 1.0f), fall = t > 1.0f ? 1 - (t - 1.0f) / 0.75f : 1.0f;
			for (int end = -1; end <= 1; end += 2)
				onHull(end * L * 0.45f, 0, 0.5f, (std::max)(W * 0.9f, 2.2f), 0, 0, 12.0f, 2.7f * rise * fall, 1.0f, 0.05f, 0.25f);
		}
		// Then the wreck smoulders: a thin grey wisp off one end and a glowing ember in the hull,
		// flickering, from 1.5 s, fading out over its last second to nothing at 3.5 s.  With nothing
		// after the column the wreck went dead straight after a big blast.
		if (t > 1.5f && t < 3.5f) {
			const float in = (std::min)((t - 1.5f) / 0.25f, 1.0f), out = t > 2.5f ? 1 - (t - 2.5f) : 1.0f;
			// (at a tenth and a third of these the soot thinning took the wisp before it showed; four
			// times the pass-16 rate, since the thinning's half-life was halved twice with the rest)
			onHull(L * 0.35f, 0, 0.6f, (std::max)(W * 0.6f, 2.0f), 0, 0, 6.0f, 12.8f * in * out, 0.9f, 0.15f, 0.2f);
			const float flicker = 0.5f + 0.5f * std::sin(t * 11.0f + 6.2831853f * unit(970)) * std::sin(t * 4.3f);
			// the flicker in its size: heat holds the hottest it has seen for a while, so a flicker in
			// heat read as a slow sag, and at 1.95 the ember burned as hot as a cook-off pop
			onHull(-L * 0.2f, 0, H * 0.5f, 1.2f + 1.2f * flicker, 0, 0, 0.5f, 1.0f * in * out, 1.3f * in * out, 0, 0.05f);
		}
	} else if (volume.kind == Structure) {
		// A building coming down, or blowing out a damaged stage, laid out on its own footprint the
		// way a tank's cook-off is laid on its hull: `along` its long side, `across` it, `up` from
		// the ground, in cells.  Fire and soot burst out low over the whole footprint and roll off
		// it, a ragged plume of fire climbs out of it in seven jets spread from end to end, the
		// collapse pours pale dust out from under every wall along the ground, which runs down the
		// slopes and round the buildings beside it, and the ruin burns under a dark column for half a
		// minute.  A damaged stage blows out of the roof and the walls of a building that stays up:
		// no collapse, a little dust, no column (its own fires smoke on).  No ball, rays or ring.
		const float hx = volume.hull[0], hy = volume.hull[1], L = volume.hull[2], W = volume.hull[3], H = volume.hull[4];
		auto onFootprint = [&](float along, float across, float up, float radius, float va, float vc, float vz, float sootRate,
			float heat, float dustRate, float push) {
			emit(slab, count, { bx + hx * along - hy * across, by + hy * along + hx * across, bz + up, radius,
				hx * va - hy * vc, hy * va + hx * vc, vz, sootRate, heat, dustRate, push });
		};
		auto unit = [&](unsigned salt) { return hashUnit(volume.seed, salt); };
		const bool standing = volume.standing;
		dust[0] = 0.76f; dust[1] = 0.72f; dust[2] = 0.65f;	// pale concrete and plaster dust; at 0.66 it lay grey-brown in its own shade
		// black-grey smoke off the fire, as in the building explosion photos (at grey-brown 0.2 the
		// ruin's column stood pale; a tank's 0.06 had made a factory's fall one black box)
		soot[0] = 0.085f; soot[1] = 0.08f; soot[2] = 0.075f;
		// and lit on its sunny side, with more sky in its shade: on Winter Wolf's snow the column
		// stood pure black with no side lit at 0.26 and 0.75
		shade = 0.32f;
		sunShare = 0.95f;
		// Its meshes (readParts): the tallest first, then the biggest.  With none read, its
		// footprint stands in as one box.
		Part parts[MaxParts];
		unsigned partCount = volume.partCount;
		for (unsigned i = 0; i < partCount; ++i) parts[i] = volume.parts[i];
		if (!partCount) { parts[0] = { 0, 0, L, W, 0, H }; partCount = 1; }
		float size = 0, top = 0;
		for (unsigned i = 0; i < partCount; ++i) {
			size = (std::max)(size, (std::max)(std::fabs(parts[i].along) + parts[i].halfAlong, std::fabs(parts[i].across) + parts[i].halfAcross));
			top = (std::max)(top, parts[i].top);
		}
		// how much of the building a mesh is, by its plan area, for the jets
		auto areaOf = [&](unsigned i) { return parts[i].halfAlong * parts[i].halfAcross; };
		// the biggest after the tallest, for the bursts and the ruin's column
		unsigned big[MaxParts];
		for (unsigned i = 0; i < partCount; ++i) big[i] = i;
		std::sort(big, big + partCount, [&](unsigned a, unsigned b) { return areaOf(a) > areaOf(b); });
		// The fall's fireball, after the building explosion photos: a white-yellow ball out of every
		// mesh, bigger than the mesh, swelling and pushed out through its walls from the building's
		// middle, cooling to orange, under a sooty rim that lags it and stays dark where the core's
		// heat does not reach.  Gone in 0.6 s.  The mesh boxes once filled with dust and a dull glow
		// read as a slump, and three bursts off the roof as small fires: dust never glows.
		if (!standing && t < FireballSeconds) {
			const float fade = 1 - t / FireballSeconds, swell = 1 + 0.7f * smoothstep01(t, 0.0f, 0.5f);
			for (unsigned i = 0; i < (std::min)(partCount, 6u); ++i) {
				const Part &p = parts[i];
				const float tall = (std::max)(p.top - p.bottom, 1.0f), middle = p.bottom + tall * 0.5f;
				const float out = std::sqrt(p.along * p.along + p.across * p.across);
				const float da = out > 0.5f ? p.along / out : std::cos(6.2831853f * unit(560 + i));
				const float dc = out > 0.5f ? p.across / out : std::sin(6.2831853f * unit(560 + i));
				const float room = 0.6f * (TierShapes[0].gz - 10.0f - middle);
				const float radius = (std::min)((std::max)((std::max)(std::sqrt(p.halfAlong * p.halfAlong + p.halfAcross * p.halfAcross) * 0.7f,
					tall * 0.5f), 2.0f) * swell, (std::max)(room, 2.0f));
				const float a = p.along + da * 4.0f * t, c = p.across + dc * 4.0f * t, z = middle + 3.0f * t;
				const float heat = 2.2f * (1 - 0.55f * smoothstep01(t, 0.1f, FireballSeconds));
				// (soot 8 in the ball and 12 in a rim 1.35 times as wide round it stood as one black dome
				// over the fall, the fire hidden inside it, and pushed out at 8 the ball ran along the
				// ground as a glowing carpet)
				onFootprint(a, c, z, radius, da * 5.0f * fade, dc * 5.0f * fade, 8.0f * fade, 3.0f * fade, heat, 0, 0.35f);
				if (t > 0.12f)
					onFootprint(a, c, z + radius * 0.4f, radius * 1.15f, da * 4.0f * fade, dc * 4.0f * fade, 8.0f * fade, 2.5f * fade, 0.6f, 0, 0.2f);
			}
		}
		// A damaged stage blows fire out of the top of its tallest meshes, thrown outwards, with
		// little soot, so it is fire blowing out of a standing building and not a black cloud over
		// its roof.
		if (standing && t < 0.6f) {
			const float fade = 1 - t / 0.6f;
			for (unsigned i = 0; i < (std::min)(partCount, 2u); ++i) {
				const Part &p = parts[i];
				const float out = std::sqrt(p.along * p.along + p.across * p.across) + 1e-3f;
				// (smaller than a mesh: one of a mesh's size stood over the roof as a brown ball)
				onFootprint(p.along, p.across, p.top + 0.4f, (std::max)((std::min)(p.halfAlong, p.halfAcross) * 0.45f, 2.0f),
					p.along / out * 6.0f * fade, p.across / out * 6.0f * fade, 4.0f + 3.0f * unit(560 + i), 3.0f * fade, 1.6f, 3.0f * fade, 0.3f);
			}
		}
		// The plume, the tank's jets out of the tops of its meshes: each lays thin smoke at a rate that
		// goes with its speed and tapers to a streak, so it stands as a ragged mass of fire that cools
		// to grey-brown smoke, never a ball carried on its head.  More from the bigger meshes.
		const unsigned jets = standing ? 3 : 6;
		float areaTotal = 0;
		for (unsigned i = 0; i < partCount; ++i) areaTotal += areaOf(i);
		for (unsigned j = 0; j < jets; ++j) {
			auto field = [&](unsigned f) { return unit(600 + 16 * j + f); };
			const float slows = 0.22f, age = t - 0.08f * field(0);
			if (age < 0 || age > 1.5f * slows) continue;
			// the mesh this jet comes out of, picked by plan area
			float pick = field(7) * areaTotal;
			unsigned m = 0;
			while (m + 1 < partCount && pick > areaOf(m)) { pick -= areaOf(m); ++m; }
			const Part &p = parts[m];
			const float base = p.top + 0.4f;
			const float reach = (std::min)((std::max)(standing ? 0.8f * size : 1.4f * size + 0.6f * top, 6.0f), TierShapes[0].gz - 14.0f - base);
			if (reach < 3.0f) continue;
			const float a0 = p.along + (field(1) - 0.5f) * p.halfAlong * 1.4f, c0 = p.across + (field(2) - 0.5f) * p.halfAcross * 1.4f;
			const float outLen = std::sqrt(a0 * a0 + c0 * c0) + 1e-3f;
			const float tilt = 0.12f + 0.25f * (std::min)(outLen / (size + 1e-3f), 1.0f) + 0.08f * (field(3) - 0.5f);
			const float azimuth = std::atan2(c0, a0) + (field(4) - 0.5f) * 0.8f;
			const float ca = std::cos(azimuth), cc = std::sin(azimuth), dz = std::cos(tilt), out = std::sin(tilt);
			const float height = reach * (0.75f + 0.25f * field(5));
			const float speed = height / slows;
			const float travelled = height * (1 - std::exp(-age / slows)), now = speed * std::exp(-age / slows);
			const float path = travelled / (0.78f * height);
			const float taper = 1 - smoothstep01(path, 0.55f, 1.0f);
			// (half a tank's: a blast's smoke does not thin by itself, and six jets' worth over a
			// factory filled the middle of the frame with one dark mass)
			const float rate = 20.0f * reach / 36.0f * now / speed * taper * (standing ? 0.6f : 1.0f);
			const float heat = 2.4f * (0.7f + 0.3f * std::pow(now / speed, 0.4f)) * (1 - 0.2f * path) * (0.45f + 0.55f * taper)
				* (0.7f + 0.3f * smoothstep01(path, 0.0f, 0.15f));
			onFootprint(a0 + ca * out * travelled, c0 + cc * out * travelled, base + dz * travelled,
				(2.0f + 0.07f * travelled) * (0.4f + 0.6f * taper),
				ca * out * now, cc * out * now, dz * now, rate, heat, rate * 0.5f, 0.08f);
		}
		// The collapse: pale dust pushed out low from under every wall, two bodies a side, each sunk
		// half its radius into the ground so only a low lens of it stands over it, and the eight
		// overlap into one sheet the shape of the footprint.  Standing on the ground, the same
		// bodies stood round the building as a ring of grey balls.  A standing building drops none.
		if (!standing && t > 0.05f && t < 0.9f) {
			const float fade = 1 - (t - 0.05f) / 0.85f, pour = 1.0f;	// (to 3 s, the dust still lay round the ruin at 10; 1.8 s, halved)
			for (unsigned k = 0; k < 8; ++k) {
				const bool end = k >= 4;
				const float s = (k & 1) ? 1.0f : -1.0f, along = (k & 2) ? 0.5f : -0.5f;
				const float push = (9.0f + 5.0f * unit(700 + k)) * fade * pour, wobble = (unit(710 + k) - 0.5f) * 3.0f;
				const float radius = (std::max)((end ? W : L) * 0.7f, 3.0f);
				if (end)
					onFootprint(s * (L + 0.3f), along * W, -0.5f * radius, radius, s * push, wobble, 0.2f, 1.0f * fade * pour, 0,
						14.0f * fade * pour, 0.35f);
				else
					onFootprint(along * L, s * (W + 0.3f), -0.5f * radius, radius, wobble, s * push, 0.2f, 1.0f * fade * pour, 0,
						14.0f * fade * pour, 0.35f);
			}
		}
		// The ground roll: the collapse's dust thrown out along the ground from under the whole
		// footprint, a sheet the pressure keeps (the skirt's source), cold and heavy (RollWeight), so
		// it hugs the terrain and runs out two to three footprints before it thins.  Pushed out from
		// under the walls alone, the flow had nowhere to go but up: a push outwards on every side is a
		// source, which the pressure takes out unless it is told otherwise.
		if (!standing && t < 1.8f) {
			blast[3] = (std::max)(L, W) * 0.9f;
			// (at 14 it ran into the box's round rim as one flat disc)
			power[0] = 10.0f * (t < 0.8f ? 1.0f : 1 - (t - 0.8f) / 1.0f);
			power[3] = 1.2f * (1 - t / 1.8f);
		}
		// The ruin burning: a column off the ground its two biggest meshes stood on, leaning with the
		// wind, done by 2.25 s: the game is fast, and the owner halved it from 7.5 s and again from 4.5
		// (stock's StructureTransition*Smoke lives 6.7 s), where it had hung on for fifteen at first and
		// hidden the next fight.  Its fire goes out with it.
		if (!standing && t > 0.4f && t < 2.25f) {
			const float rise = (std::min)((t - 0.4f) / 0.4f, 1.0f), fall = t > 1.0f ? 1 - (t - 1.0f) / 1.25f : 1.0f;
			for (unsigned i = 0; i < (std::min)(partCount, 2u); ++i) {
				const Part &p = parts[big[i]];
				// (thick: at 0.8 it was a thin pale wisp beside the photos' black column)
				onFootprint(p.along, p.across, 0.6f, (std::max)((std::min)(p.halfAlong, p.halfAcross) * 0.6f, 2.0f), 0, 0, 12.0f,
					2.0f * rise * fall * (i ? 0.6f : 1.0f), fall, 0.1f, 0.25f);
			}
		}
		soot[3] = 1.6f + 2.4f * std::exp(-t / 0.3f);
	} else if (volume.kind == Launch) {
		// A missile leaving its silo or its launcher, after the launch photos: a short white-yellow
		// flash at the nozzle, then a white, sunlit cloud that rolls out sideways along the ground
		// and the pad, billowing, with a hard shadow side.  Cold exhaust and steam, so it glows only
		// in the flash and lifts only a little; gone with the trail's spirit, in about four seconds.
		auto unit = [&](unsigned salt) { return hashUnit(volume.seed, salt); };
		flash = t < 0.12f ? 6.0f * (1 - t / 0.12f) : 0.0f;
		flashWide = 2.0f;
		flashTall = 2.5f;
		soot[0] = 0.9f; soot[1] = 0.9f; soot[2] = 0.92f; soot[3] = 1.2f;
		dust[0] = 0.9f; dust[1] = 0.89f; dust[2] = 0.86f;
		shade = 0.3f;
		sunShare = 1.0f;
		// the pad's footprint: what the cloud rolls out of, along the ground (the skirt)
		blast[3] = 5.0f;
		if (t < 1.6f) {
			power[0] = 16.0f * (t < 0.6f ? 1.0f : 1 - (t - 0.6f) / 1.0f);
			power[3] = 1.6f * (1 - t / 1.6f);
		}
		// billows rolling out low from the pad, warm enough to heave up a little as they go, so the
		// cloud is lumpy rather than a flat sheet; and the exhaust column over the pad, widening
		if (t < 1.0f) {
			const float fade = 1 - t;
			for (unsigned k = 0; k < 8; ++k) {
				const float azimuth = (k + unit(800 + k)) * 6.2831853f / 8.0f, out = 3.0f + 6.0f * t * (0.7f + 0.6f * unit(810 + k));
				const float ca = std::cos(azimuth), sa = std::sin(azimuth), push = 10.0f * fade;
				emit(slab, count, { bx + ca * out, by + sa * out, bz + 1.0f, 2.5f + 2.0f * t, ca * push, sa * push, 1.0f,
					3.0f * fade, 0.35f, 4.0f * fade, 0.3f });
			}
		}
		if (t < 1.2f) {
			const float fade = 1 - t / 1.2f;
			emit(slab, count, { bx, by, bz + 2.0f + 6.0f * t, 2.0f + 2.5f * t, 0, 0, 6.0f, 4.0f * fade, 0.3f, 0, 0.2f });
		}
	} else if (volume.kind == Muzzle) {
		// A tank gun firing, after footage of real ones (reference/tank-shoot.jpg): an orange-yellow
		// cone of flame out of the muzzle, wider than the turret at its far end and gone in a tenth of
		// a second, a little grey smoke in front of it, and a dust cloud the blast blows up off
		// the ground round the gun's front, rolling out sideways along it.  Placed along the barrel's
		// own aim (hull[0..2], the muzzle bone's axis) from the muzzle (blast), in cells.
		const float ax = volume.hull[0], ay = volume.hull[1], az = volume.hull[2], height = volume.hull[4];
		const float flat = (std::max)(std::sqrt(ax * ax + ay * ay), 1e-3f), fx = ax / flat, fy = ay / flat;
		auto unit = [&](unsigned salt) { return hashUnit(volume.seed, salt); };
		auto along = [&](float d, float lift, float radius, float speed, float rise, float sootRate, float heat, float push) {
			emit(slab, count, { bx + ax * d, by + ay * d, bz + az * d + lift, radius, ax * speed, ay * speed, az * speed + rise,
				sootRate, heat, 0, push });
		};
		// the flash, small, at the muzzle tip
		blast[0] = bx + ax * 0.8f; blast[1] = by + ay * 0.8f; blast[2] = bz + az * 0.8f;
		blast[3] = 1.5f;
		// (at 6 over 3 cells for 0.05 s it was a hard white disc wider than the fireball; at 2.5 over 2,
		// 3.5 cells out, a round glow off the muzzle)
		flash = t < 0.03f ? 2.0f : 0.0f;
		flashWide = 0.9f;
		flashTall = 1.0f;
		soot[0] = 0.78f; soot[1] = 0.77f; soot[2] = 0.75f; soot[3] = 1.6f + 1.4f * std::exp(-t / 0.05f);
		dust[0] = 0.62f; dust[1] = 0.55f; dust[2] = 0.44f;	// sunlit tan
		// The fireball, as the photos have it: a cone of flame straight out of the muzzle, widening
		// forward, brightest at the tip and orange-red at its fringe.  Six hot puffs on the barrel's
		// line, the first just inside the tip, each wider than the one behind it, the hottest at the
		// muzzle, pushed on only gently so the cone stays on the gun.  Four puffs out ahead, thrown
		// fastest from the muzzle, left a gap and then a round ball.  Its heat goes in a few frames
		// (MuzzleHeatHalfLife), so it flares and is smoke by 0.15 s.  A flame glows by the soot it
		// burns in (at 8 a second it lit nothing, at 70 it left a brown ball).
		if (t < 0.1f) {
			const float grow = 0.7f + 0.3f * smoothstep01(t, 0.0f, 0.05f), fade = 1 - t / 0.1f;
			// (the glow is soot times heat along the eye's ray, so laid evenly the wide far puffs outshone
			// the thin tip and the cone read as a ball ahead of the gun: the tip lays the most and the
			// hottest, the far end somewhat less and a little cooler)
			// (about a third of a Crusader's barrel long, 12 units, and as tall as its turret at the far
			// end; at 8 units and a far end laying a quarter of the tip's soot at half its heat it read
			// smaller than the photo's and the game's own flame)
			for (unsigned k = 0; k < 6; ++k) {
				const float d = (-0.3f + 1.6f * k) * grow;
				along(d, 0, (1.3f + 0.45f * (std::max)(d, 0.0f)) * grow, (4.0f + 3.0f * k) * fade, 0.5f, 70.0f - 6.0f * k,
					(2.9f - 0.12f * k) * (0.7f + 0.3f * fade), 0.4f);
			}
		}
		// a little smoke, pushed on ahead and spreading, lightly lifted
		if (t > 0.02f && t < 0.45f) {
			const float fade = 1 - (t - 0.02f) / 0.43f;
			for (unsigned k = 0; k < 3; ++k)
				along(2.5f + 3.0f * k, 0.6f + 0.4f * k, 1.8f + 0.8f * k + 1.5f * (1 - fade), (14.0f - 3.0f * k) * fade, 1.5f, 18.0f * fade, 0, 0.35f);
		}
		// The dust: where the blast meets the ground ahead of the muzzle, thrown out low along the
		// ground in a fan from straight ahead round to the gun's sides and a little behind them, so it
		// rolls out past the tank's front corners.
		if (t < 0.3f) {
			const float fade = 1 - t / 0.3f;
			const float reach = (std::max)(height, 1.0f) * 0.8f + 1.0f;
			const float gx0 = bx + fx * reach, gy0 = by + fy * reach;
			for (unsigned k = 0; k < 7; ++k) {
				const float side = k / 3.0f - 1.0f;
				const float angle = side * 2.2f + (unit(900 + k) - 0.5f) * 0.3f;
				const float dx = fx * std::cos(angle) - fy * std::sin(angle), dy = fy * std::cos(angle) + fx * std::sin(angle);
				const float out = 2.0f + 2.0f * (1 - std::fabs(side)), speed = (22.0f + 12.0f * (1 - std::fabs(side))) * fade;
				// (low and flat: at 40 a second, lifted 0.8 cells a second, it stood up into the smoke as
				// one tan ball with it)
				emit(slab, count, { gx0 + dx * out, gy0 + dy * out, volume.ground + 0.4f, 2.2f + 2.0f * (1 - fade),
					dx * speed, dy * speed, 0.3f, 0, 0, 30.0f * fade, 0.6f });
			}
		}
	} else if (volume.kind == Shell || volume.kind == Airburst) {
		const bool ground = volume.kind == Shell;
		// a flash shorter than a frame at 30, white-orange
		flash = t < 0.03f ? 12.0f : 0.0f;
		flashWide = 3.0f;
		flashTall = 1.3f;
		// a shell striking armour flashes small and white (reference/tank-hit-*.jpg): its sparks are
		// the light (ReforgedTankHitSparks); at the ground shell's 12 over 3 cells a white oval hid the
		// tank it hit
		if (volume.onHull) { flash = t < 0.03f ? 4.0f : 0.0f; flashWide = 1.6f; flashTall = 1.0f; }
		blast[3] = 2.5f;
		soot[0] = 0.11f; soot[1] = 0.105f; soot[2] = 0.10f; soot[3] = 1.2f;
		// dark brown thrown earth: tan dust on tan ground was invisible
		dust[0] = 0.33f; dust[1] = 0.26f; dust[2] = 0.19f;
		// the skirt along the ground, or for an airburst a patch of dust under it
		if (ground && t < 0.3f) { power[0] = 30.0f * (1 - t / 0.3f); power[3] = 1.5f; }
		if (!ground && !volume.onHull && t < 0.4f) emit(slab, count, { bx, by, volume.ground + 0.5f, 4.0f, 0, 0, 0, 0, 0, 6.0f, 0 });
		// the black-grey puff in the middle, rising slowly; off a hull, barely warm, so it neither
		// glows nor climbs away as an orange ball
		if (t < 0.2f) power[1] = volume.onHull ? 1.2f : 1.6f;
		if (t < 0.3f) power[2] = volume.onHull ? 0.4f : 1.1f;
		// the dirt fountain: a hollow cone of jets 10 to 20 m up, thrown for its first second and
		// falling back under its own weight (DirtWeight); thrown for 6 s, a Rocket Buggy salvo's
		// fountains stood beside a fallen War Factory as tan fingers after everything else had gone
		if (ground && t < 1.0f) {
			const float speed = std::sqrt(2.0f * Gravity * 50.0f * volume.scale) / volume.cell;
			for (unsigned j = 0; j < 10; ++j) {
				float jet[6];
				if (jetAt(volume, j, speed, 0.12f, 0.5f, t, jet))
					emit(slab, count, { jet[0], jet[1], jet[2], 2.0f, jet[3], jet[4], jet[5], 0, 0, 160.0f, 0.35f });
			}
		}
		// a bomb's small mushroom
		if (volume.scale >= 2.0f && t > 0.3f) {
			const float rise = 1 - std::exp(-t / 3.0f);
			blast[2] = bz + 10.0f * rise;
			motion[0] = 10.0f / 3.0f * std::exp(-t / 3.0f);
			shape[0] = 7.0f * std::exp(-t / 12.0f);
			shape[1] = 2.5f + 1.5f * rise;
			shape[2] = bz + 16.0f;
			shape[3] = 2.5f * std::exp(-t / 10.0f);
		}
	} else {
		// nuclear, after real ones (reference/nuclear-real.jpg): the flash and a white dome at the
		// shock front for a second and a half; a ball of fire on the ground, yellow-white inside an
		// orange-red shell, that rises, slows, flattens and widens into a cap three to four times as
		// wide as the grey-brown stem of dust under it; a white collar round the stem for a few
		// seconds; the cap cooling to cream, lit from outside, white on top and dark underneath; and
		// a low wall of dust racing out along the ground past the base.  Sized for the play camera:
		// the cap stops about 25 cells (250 units) up, 12 cells across its radius, so the stem shows
		// under it from the game's own camera and the whole mushroom stays on screen; the surge keeps
		// its full reach.  The shape is held by MushroomPass; the old flow-only cap read as a dark
		// disc and was gone in twelve seconds, and its 40 seconds of ground dust buried the base.
		const float capZ = bz + 2.0f + 20.0f * (1 - std::exp(-t / 3.2f));
		const float capR = (std::min)(6.5f * (1 - std::exp(-t / 0.12f)) + 7.0f * (1 - std::exp(-(std::max)(t - 1.0f, 0.0f) / 5.0f))
			+ 0.04f * (std::max)(t - 20.0f, 0.0f), 16.0f);
		const float capH = capR * (0.95f - 0.57f * smoothstep01(t, 1.0f, 9.0f));
		const float fade = 1 - smoothstep01(t, 50.0f, 85.0f);
		// thin while it is fire, so its hot middle shows through the cooler rim; thick smoke once it
		// has cooled.  At 0.35 the eye saw only the rim's outer cells, and at its first heat those
		// burned the whole ball one flat white
		cap[0] = (0.2f + 1.0f * smoothstep01(t, 1.0f, 10.0f)) * fade;
		cap[1] = capZ;
		cap[2] = (std::max)(capR, 1.0f);
		cap[3] = (std::max)(capH, 1.0f);
		// a narrow stem: at 3 cells it stood half as wide as the cap, a thick brown trunk
		stem[0] = 0.9f * smoothstep01(t, 1.0f, 3.5f) * fade;
		stem[1] = 1.4f + 0.8f * smoothstep01(t, 1.2f, 8.0f);
		stem[2] = capZ - 0.4f * capH;
		stem[3] = 4.0f + 20.0f * std::exp(-t / 0.3f);	// the fireball is there at once, the rest follows
		blast[3] = (std::max)(capR, 3.5f);		// the skirt's footprint
		motion[0] = 20.0f / 3.2f * std::exp(-t / 3.2f);	// the cap's rise, which the ring carries
		// the flash is light thrown on the scene (Takes starts a pulse of the game's own dynamic
		// light); this glow round the fireball lasts a tenth of a second.  At 40 over 0.15 s it drew
		// an opaque white disc across the frame for most of a second
		flash = 3.0f * std::exp(-t / 0.04f);
		flashWide = (std::max)(capR, 4.0f);
		flashTall = 1.2f;
		// the cap's heat: a white-yellow middle for the first second, yellow-orange at two, red embers by ten
		power[2] = 1.2f * std::exp(-t / 0.6f) + 1.9f * std::exp(-t / 9.0f);
		if (t < 1.0f) { power[0] = 70.0f * std::exp(-t / 0.5f); power[3] = 0.3f; }
		// the base surge: a ring front of dust racing out along the ground, far past the fireball,
		// slowing as it goes, 42 cells (420 units) out over a 1.6 s constant.  It drops dust all
		// the way out: at 1.6 that was a brown blanket over the whole base, at 0.7 a tan one.
		if (t < 7.0f) {
			motion[1] = 42.0f * (1 - std::exp(-t / 1.6f));
			motion[2] = 42.0f / 1.6f * std::exp(-t / 1.6f) * 1.2f;
			motion[3] = 0.22f * (1 - t / 7.0f);
		}
		// the Wilson cloud: a white dome running out with the shock front, centred on the ground
		// (thin enough that the fireball and the ground show through it face on, white where the
		// eye looks along it at the rim; at 0.8 and more it was an opaque cream disc over the frame)
		// At 27 cells it wrapped the pulled-back camera and hazed the whole frame cream.
		if (t > 0.1f && t < 1.5f) {
			shellRadius = 7.0f + 11.0f * smoothstep01(t, 0.1f, 1.3f);
			shellOpacity = 0.06f * std::sin((t - 0.1f) / 1.4f * 3.14159265f);
		}
		// the condensation collar, round the stem just under the cap
		if (t > 2.5f && t < 8.5f) {
			// at 0.9 and the cap's whole radius it stood round the stem as a thick white tyre
			collarOpacity = 0.35f * std::sin((t - 2.5f) / 6.0f * 3.14159265f);
			collarRadius = capR * 0.7f;
			collarHeight = capZ - capH * 0.9f;
		}
		shape[0] = 12.0f * std::exp(-t / 30.0f);	// the ring rolling the cap's rim under
		shape[1] = capR * 0.65f;
		shape[2] = capZ;		// heat lifts nothing past the cap's middle: above, it stood the cap up into a ball
		shape[3] = 6.0f * stem[0];					// the stem's dust rising into it
		// white on its sunlit top, dark underneath: more sun and a little more sky than the rest
		// take, where at 0.26 and 0.75 the cooled cap read as one flat grey-brown
		shade = 0.34f;
		sunShare = 1.0f;
		billows = smoothstep01(t, 5.0f, 9.0f);	// once it is thick smoke: the thin fireball keeps its soft glow
		spared = 1 - smoothstep01(t, 4.0f, 8.0f);
		// dark smoke round the fire at first, cream once it is lit only by the sun, greyer late
		const float cream = smoothstep01(t, 3.0f, 12.0f), grey = smoothstep01(t, 20.0f, 50.0f);
		const float early[3] = { 0.32f, 0.22f, 0.16f }, lit[3] = { 0.88f, 0.84f, 0.78f }, late[3] = { 0.80f, 0.79f, 0.78f };
		for (unsigned i = 0; i < 3; ++i) soot[i] = early[i] + (lit[i] - early[i]) * cream + (late[i] - lit[i]) * grey;
		// the fire's gain low while the fireball is hottest, so its lumps and its cooler rim show
		// rather than one burned-out white; full once it has cooled to embers in smoke
		soot[3] = 0.9f + 1.3f * smoothstep01(t, 1.0f, 4.0f);
		dust[0] = 0.55f; dust[1] = 0.48f; dust[2] = 0.41f;	// the stem and the surge, grey-brown
		// Another blast's front has moved the cap (holdCaps): it is drawn back to its shape more slowly,
		// and once let go it is free smoke with every shape and source off.  All of them together: the
		// bomb's ring, the stem's inflow and the hot core switch on where a slab has no cap.
		if (volume.hold < 1.0f) {
			stem[3] *= volume.hold;
			cap[0] *= volume.hold;
			if (volume.hold < 0.05f) {
				for (float *field : { cap, stem, shape, power, motion }) std::memset(field, 0, 16);
				shape[2] = 1000.0f;		// no ceiling: the freed smoke keeps its lift
				collarOpacity = shellOpacity = 0;	// drawn shapes, not smoke: they go with the cap
			}
		}
	}
	sim.Volumes[slab][1] = (float)count;
	if (draw) {
		for (unsigned i = 0; i < 4; ++i) draw->Soot[i] = soot[i];
		for (unsigned i = 0; i < 3; ++i) draw->Dust[i] = dust[i];
		for (unsigned i = 0; i < 4; ++i) draw->Blast[i] = blast[i];
		draw->Effects[0] = flash;
		draw->Effects[1] = shellRadius;
		draw->Effects[2] = shellOpacity;
		draw->Effects[3] = collarOpacity;
		draw->Extra[0] = collarRadius;
		draw->Extra[1] = flashTall;
		draw->Extra[2] = flashWide;
		draw->Extra[3] = collarHeight;
		draw->Look[0] = shade;
		draw->Look[1] = billows;
		draw->Look[2] = sunShare;
		draw->Look[3] = spared;
	}
}

void unbindCompute(ID3D11DeviceContext *context)
{
	ID3D11ShaderResourceView *const views[8] = {};
	ID3D11UnorderedAccessView *const writes[3] = {};
	context->CSSetShaderResources(0, 8, views);
	context->CSSetUnorderedAccessViews(0, 3, writes, nullptr);
}

// One pass over the first `slabs` slabs; `extra` goes in from t5 (the air's particle lists).
void run(ID3D11DeviceContext *context, const Tier &tier, Pass pass, std::initializer_list<ID3D11ShaderResourceView *> reads,
	std::initializer_list<ID3D11UnorderedAccessView *> writes, unsigned slabs,
	std::initializer_list<ID3D11ShaderResourceView *> extra = {})
{
	unbindCompute(context);
	context->CSSetShaderResources(0, (UINT)reads.size(), reads.begin());
	context->CSSetShaderResources(4, 1, &tier.floorView);
	if (extra.size()) context->CSSetShaderResources(5, (UINT)extra.size(), extra.begin());
	context->CSSetUnorderedAccessViews(0, (UINT)writes.size(), writes.begin(), nullptr);
	context->CSSetShader(tier.compute[pass], nullptr, 0);
	context->Dispatch(tier.shape.gx / 8, tier.shape.gy / 8, slabs * tier.shape.gz / 8);
}

float secondsSince(const Volume &volume, int now) { return (float)(now - volume.born) / LOGICFRAMES_PER_SECOND; }

// The terrain under the box and every structure standing in it, as the top of what is solid a
// column, in cells over the box's floor.  Read on the client from the objects' own geometry; the
// logic reads nothing back.  Laid again every two seconds, as buildings fall.
void layFloor(ID3D11DeviceContext *context, const Tier &tier, unsigned slab, Volume &volume, int now)
{
	if (now - volume.floorFrame < 2 * LOGICFRAMES_PER_SECOND && !volume.reset) return;
	volume.floorFrame = now;
	const TierShape &shape = tier.shape;
	static std::vector<float> heights;
	heights.assign(shape.gx * shape.gy, 0.0f);
	const float cell = volume.cell;
	for (unsigned y = 0; y < shape.gy; ++y)
		for (unsigned x = 0; x < shape.gx; ++x) {
			const float wx = volume.origin[0] + (x + 0.5f) * cell, wy = volume.origin[1] + (y + 0.5f) * cell;
			const float ground = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(wx, wy, NULL) : 0.0f;
			heights[y * shape.gx + x] = (ground - volume.origin[2]) / cell;
		}
	const float right = volume.origin[0] + shape.gx * cell, top = volume.origin[1] + shape.gy * cell;
	// the structures Gather read outside the logic thread's window, every two seconds
	for (const Solid &solid : solids) {
		// a building whose fall this blast is: the dust pours out over the ground it stood on
		if (volume.kind == Structure && !volume.standing && solid.id == volume.building) continue;
		const bool burning = volume.kind == Fire && volume.site >= 0 && volume.site < (int)fires.sites.size()
			&& (unsigned)solid.id == ownerOf(fires.sites[volume.site].object);
		const float major = solid.major, minor = solid.minor;
		float reach = std::sqrt(major * major + minor * minor);
		for (unsigned i = 0; i < solid.partCount; ++i) {
			const Part &p = solid.parts[i];
			reach = (std::max)(reach, std::sqrt((std::fabs(p.along) + p.halfAlong) * (std::fabs(p.along) + p.halfAlong)
				+ (std::fabs(p.across) + p.halfAcross) * (std::fabs(p.across) + p.halfAcross)));
		}
		if (solid.x + reach < volume.origin[0] || solid.x - reach > right || solid.y + reach < volume.origin[1] || solid.y - reach > top)
			continue;
		const float roof = (solid.top - volume.origin[2]) / cell, base = solid.top - solid.tall;
		const float c = std::cos(solid.angle), s = std::sin(solid.angle);
		// A fire's own building, once its mesh is read, stands as its mesh does: every up-facing
		// triangle laid into the columns whose middles it covers, at its height there, so the roof,
		// a broken floor and the ground plate are the floor and the flames stand on them.  (The boxes
		// and the footprint rule below are wider and taller than the mesh: the fuel off its surface lay
		// inside them, or the building's top went missing.)
		auto surfaceFound = burning ? fireSurfaces.find((unsigned)solid.id) : fireSurfaces.end();
		if (surfaceFound != fireSurfaces.end() && !surfaceFound->second.roofs.empty()) {
			const std::vector<float> &t = surfaceFound->second.roofs;
			for (size_t i = 0; i + 8 < t.size(); i += 9) {
				float px[3], py[3], pz[3];
				for (unsigned k = 0; k < 3; ++k) {
					px[k] = (t[i + k * 3] - volume.origin[0]) / cell - 0.5f;
					py[k] = (t[i + k * 3 + 1] - volume.origin[1]) / cell - 0.5f;
					pz[k] = (t[i + k * 3 + 2] - volume.origin[2]) / cell;
				}
				const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
				if (std::fabs(area) < 1e-6f) continue;
				const int x0 = (std::max)(0, (int)std::ceil((std::min)((std::min)(px[0], px[1]), px[2])));
				const int x1 = (std::min)(int(shape.gx) - 1, (int)std::floor((std::max)((std::max)(px[0], px[1]), px[2])));
				const int y0 = (std::max)(0, (int)std::ceil((std::min)((std::min)(py[0], py[1]), py[2])));
				const int y1 = (std::min)(int(shape.gy) - 1, (int)std::floor((std::max)((std::max)(py[0], py[1]), py[2])));
				for (int y = y0; y <= y1; ++y)
					for (int x = x0; x <= x1; ++x) {
						const float w1 = ((x - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (y - py[0])) / area;
						const float w2 = ((px[1] - px[0]) * (y - py[0]) - (x - px[0]) * (py[1] - py[0])) / area;
						if (w1 < 0 || w2 < 0 || w1 + w2 > 1) continue;
						float &height = heights[y * shape.gx + x];
						height = (std::max)(height, pz[0] + w1 * (pz[1] - pz[0]) + w2 * (pz[2] - pz[0]));
					}
			}
			continue;
		}
		const int x0 = (std::max)(0, int((solid.x - reach - volume.origin[0]) / cell)), x1 = (std::min)(int(shape.gx) - 1, int((solid.x + reach - volume.origin[0]) / cell));
		const int y0 = (std::max)(0, int((solid.y - reach - volume.origin[1]) / cell)), y1 = (std::min)(int(shape.gy) - 1, int((solid.y + reach - volume.origin[1]) / cell));
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x) {
				const float dx = volume.origin[0] + (x + 0.5f) * cell - solid.x, dy = volume.origin[1] + (y + 0.5f) * cell - solid.y;
				const float along = dx * c + dy * s, side = -dx * s + dy * c;
				float &height = heights[y * shape.gx + x];
				// its meshes where it has them, so the smoke runs between a factory's stacks and round
				// a tower rather than over one box as tall as the tallest
				// (and the footprint up to its lowest one, so a building of many small walls, more than
				// the meshes kept, has no hole the smoke runs through)
				if (solid.partCount) {
					float lowest = 1e9f;
					for (unsigned i = 0; i < solid.partCount; ++i) {
						const Part &p = solid.parts[i];
						if (p.bottom >= 4.0f) continue;
						lowest = (std::min)(lowest, p.top);
						// A fire's own building stands only to its lowest mesh: its boxes are wider and
						// taller than its walls and roof, so the mesh surface its fuel comes off lay inside
						// them, where every step zeroed it, and the slab stayed empty.
						if (burning) continue;
						if (std::fabs(along - p.along) <= p.halfAlong && std::fabs(side - p.across) <= p.halfAcross)
							height = (std::max)(height, (base + p.top - volume.origin[2]) / cell);
					}
					const bool onFootprint = solid.box ? std::fabs(along) <= major && std::fabs(side) <= minor : dx * dx + dy * dy <= major * major;
					if (onFootprint && lowest < 1e8f) height = (std::max)(height, (base + lowest - volume.origin[2]) / cell);
					continue;
				}
				// (a fire's own building with no mesh kept stands nowhere: its roof's fuel points lay inside
				// a footprint laid to its top, every step zeroed them, and the slab stayed empty)
				const bool inside = solid.box ? std::fabs(along) <= major && std::fabs(side) <= minor : dx * dx + dy * dy <= major * major;
				if (inside && !burning) height = (std::max)(height, roof);
			}
	}
	// a blast on a building goes off on its roof, not inside it
	// (a launch's too: laid under a Scud Storm's silo, its billows were inside the building and only
	// a flat white sheet lay on the roof)
	if (volume.reset && volume.kind != Airburst && volume.kind != Structure && volume.kind != Fire) {
		const unsigned column = unsigned(volume.blast[1]) * shape.gx + unsigned(volume.blast[0]);
		volume.blast[2] = (std::max)(volume.blast[2], heights[column] + 0.5f);
	}
	if (volume.kind == Fire && slab < MaxSlabs) fireFloors[slab] = heights;	// scheduleFire's: no fuel laid inside the floor
	const D3D11_BOX region = { 0, slab * shape.gy, 0, shape.gx, (slab + 1) * shape.gy, 1 };
	context->UpdateSubresource(tier.floor, 0, &region, heights.data(), shape.gx * sizeof(float), 0);
}

void step(ID3D11DeviceContext *context, unsigned t, int now, float dt, float seconds, const float sunToward[3], unsigned slabs)
{
	Tier &tier = state.tiers[t];
	const bool fire = t == 2;
	if (fire) fireEmitting.clear();	// scheduleFire names them again below
	sim.Step[0] = dt;
	sim.Step[1] = seconds;
	sim.Forces[0] = Buoyancy;
	sim.Forces[1] = Weight;
	sim.Forces[2] = fire ? FireVorticity : Vorticity;
	sim.Forces[3] = Drag;
	sim.Keep[0] = std::pow(0.5f, dt / (fire ? FireHeatHalfLife : HeatHalfLife));
	sim.Keep[1] = std::pow(0.5f, dt / VelocityHalfLife);
	sim.Keep[2] = std::pow(0.5f, dt / DustHalfLife);
	sim.Keep[3] = std::pow(0.5f, dt / CapSpillHalfLife);
	for (unsigned i = 0; i < 3; ++i) sim.Sun[i] = sunToward[i];
	sim.Sun[3] = Extinction;
	sim.Wind[0] = WindX;
	sim.Wind[1] = WindY;
	fillShocks(now);
	float settle = 0;
	for (unsigned slab = 0; slab < tier.shape.slabs; ++slab) {
		const Volume &volume = volumes[tier.shape.first + slab];
		// a fire off the screen is not stepped (its cells keep what they held, a step stale at most)
		sim.Volumes[slab][0] = volume.live && !volume.frozen ? 1.0f : 0.0f;
		sim.Volumes[slab][1] = 0;
		sim.Volumes[slab][2] = volume.cell;
		sim.Volumes[slab][3] = volume.reset ? 1.0f : 0.0f;
		// A building's smoke thins with a half-life of its own: a blast's soot otherwise stays as laid,
		// and a War Factory's had piled into a dark wall across the frame by the next building's fall.
		sim.Thin[slab][0] = volume.kind == Structure ? std::pow(0.5f, dt / StructureSootHalfLife)
			: volume.kind == Fire ? std::pow(0.5f, dt / FireSootHalfLife) : 1.0f;
		// A tank's thins once its column has died down, so it is gone as soon as the game's own.
		if (volume.kind == TankDeath && volume.live && secondsSince(volume, now) > TankSootThinFrom)
			sim.Thin[slab][0] = std::pow(0.5f, dt / TankSootHalfLife);
		// a nuclear cap another blast let go of thins out as free smoke, and while it is only loosened what
		// the front pushed off it drifts aside instead of fading at the cap's own spill rate
		if (volume.kind == Nuclear && volume.hold < 0.05f) sim.Thin[slab][0] = std::pow(0.5f, dt / NukeDisperseHalfLife);
		sim.Spill[slab][0] = std::pow(0.5f, dt / (CapSpillHalfLife + (NukeDisperseHalfLife - CapSpillHalfLife) * (1 - volume.hold)));
		const bool rolls = (volume.kind == Structure && !volume.standing) || volume.kind == Launch || volume.kind == Muzzle;
		// (not a bomb's: its small mushroom's stem would fall out of it)
		// (a tank's: its dark base masses slump slowly onto the ground, which eats them; the weight goes
		// with the dust's density, and at RollWeight or 1.5 the thick masses fell flat before they showed)
		sim.Thin[slab][1] = rolls ? RollWeight : volume.kind == TankDeath ? TankMassWeight
			: volume.kind == Shell && volume.scale < 2.0f ? DirtWeight : 0.0f;
		sim.Thin[slab][2] = std::pow(0.5f, dt / (rolls ? GroundRollHalfLife : GroundDustHalfLife));
		// a fall's fireball cools on its own clock once its emitters stop, so it is gone inside a
		// second (at the shared HeatHalfLife it still burned orange at 1.3 s)
		// Its heat kept over this step on top of the tier's: a gun's fireball goes in a few frames, and
		// a fall's cools on its own clock once its emitters stop, so it is gone inside a second (at
		// the shared HeatHalfLife it still burned orange at 1.3 s).
		const bool fireballCools = volume.kind == Structure && !volume.standing && volume.live && secondsSince(volume, now) > FireballSeconds;
		sim.Thin[slab][3] = volume.kind == Muzzle ? std::pow(0.5f, dt / MuzzleHeatHalfLife)
			: fireballCools ? std::pow(0.5f, dt / FireballHalfLife) : 1.0f;
		for (unsigned i = 0; i < 3; ++i) sim.Origins[slab][i] = volume.origin[i];
		sim.Origins[slab][3] = 0;
		if (!volume.live) continue;
		{
			float low[3], high[3];
			boxOf(volume, tier.shape, low, high);
			sim.Origins[slab][3] = shockMask(low, high, volume.cell, int(tier.shape.first + slab));
		}
		layFloor(context, tier, slab, volumes[tier.shape.first + slab], now);
		schedule(volume, slab, secondsSince(volume, now), nullptr);
		// ponytail: one settling speed a tier, its last volume's cell (a gun's small cells left out,
		// so a shot does not hurry a dead tank's dust down)
		if (volume.kind != Muzzle) settle = DustSettle / volume.cell;
	}
	sim.Step[2] = settle;
	fillMovers();
	upload(context, tier.simConstants, &sim, sizeof(sim));
	context->CSSetConstantBuffers(0, 1, &tier.simConstants);
	context->CSSetSamplers(0, 1, &state.linear);

	const unsigned in = tier.current, out = 1 - tier.current;
	run(context, tier, CurlProgram, { tier.velocity[0].view }, { tier.curl.write }, slabs);
	run(context, tier, AdvectProgram, { tier.velocity[0].view, tier.density[in].view, tier.curl.view },
		{ tier.velocity[1].write, tier.density[out].write, tier.pressure[0].write }, slabs);
	// (the fire's divergence also reads the new fuel and heat, for the gas its burning releases)
	run(context, tier, DivergenceProgram, { tier.velocity[1].view, tier.density[out].view }, { tier.divergence.write }, slabs);
	for (unsigned sweep = 0; sweep < (fire ? Qualities[level].fireSweeps : Qualities[level].sweeps); ++sweep) {
		const unsigned from = sweep & 1;
		run(context, tier, JacobiProgram, { tier.pressure[from].view, tier.divergence.view }, { tier.pressure[1 - from].write }, slabs);
	}
	run(context, tier, ProjectProgram, { tier.pressure[0].view, tier.velocity[1].view }, { tier.velocity[0].write }, slabs);
	run(context, tier, LightProgram, { tier.density[out].view }, { tier.light.write }, slabs);
	unbindCompute(context);
	tier.current = out;
	for (unsigned slab = 0; slab < tier.shape.slabs; ++slab) volumes[tier.shape.first + slab].reset = false;
}

// Whether the air takes a template's systems, and as soot or dust.  Smoke and dust are the
// see-through, unlit systems drawn with a smoke, cloud, dirt or contrail texture (501 of the
// shipped systems are see-through and unlit; snow, water, leaves and debris use other textures).
// Hazard fields keep their own look.
AirLook airLookOf(const ParticleSystemTemplate *tmplate, ParticleSystem *system)
{
	auto known = airLooks.find(tmplate);
	if (known != airLooks.end()) return known->second;
	std::string texture = system->getParticleTypeName().str();
	for (char &c : texture) c = (char)std::toupper((unsigned char)c);
	const bool dusty = texture.find("DRTEXP") != std::string::npos || texture.find("AVALANCHE") != std::string::npos
		|| texture.find("DUST") != std::string::npos;
	const bool smoky = texture.find("SMOK") != std::string::npos || texture.find("CLOUD") != std::string::npos
		|| texture.find("CONTRAIL") != std::string::npos;
	const char *name = tmplate->getName().str();
	AirLook look;
	look.smoke = system->getShaderType() == ParticleSystemInfo::ALPHA && !tmplate->isEmissive()
		&& system->getPriority() != AREA_EFFECT && (dusty || smoky);
	look.dust = dusty || system->getPriority() == DUST_TRAIL || std::strstr(name, "Dust") != nullptr;
	// Flying bits and the smoke trails behind them (ReforgedDebrisTrail, ReforgedShrapnelPuff, the
	// ReforgedBlastShardPuff sizes, TankStruckDebris, TrackDebrisDirt*): the air keeps what a
	// particle stamped for seconds after it moved on, so a fast clod left its whole path behind as a
	// brown tentacle and a thin streak stood as a wide pale ray.  These keep the game's own look,
	// and so does a gun's muzzle smoke (TankMuzzleFlashSmoke and the rest): one short puff stamped
	// as a sphere into cells seven units across lay on the ground in front of the gun as a flat
	// tan disc, where the game's own puff is soft grey and drifts.
	look.scrap = system->getPriority() == DEBRIS_TRAIL || std::strstr(name, "Debris") || std::strstr(name, "Shrapnel")
		|| std::strstr(name, "Shard") || std::strstr(name, "Muzzle")
		|| !std::strncmp(name, "ReforgedRocket", 14);	// gatherNozzles' billboards (as smoke in the air the backblast puff did not show)
	airLooks[tmplate] = look;
	return look;
}

bool takeAir(ParticleSystem *system)
{
	const AirLook look = airLookOf(system->getTemplate(), system);
	if (!look.smoke || look.scrap || !usable()) return false;
	static std::unordered_set<const ParticleSystemTemplate *> logged;
	if (logged.size() < 64 && logged.insert(system->getTemplate()).second)
		DEBUG_LOG(("VOLSMOKE: the air takes '%s'%s\n", system->getTemplate()->getName().str(), system->isUsingStreak() ? ", a streak" : ""));
	// an explosion's smoke that started no blast of its own still pushes the air round it
	if (system->getPriority() == DEATH_EXPLOSION && !system->isSystemForever() && TheGameLogic) {
		Coord3D where;
		system->getPosition(&where);
		addShock(where, VolumeShock::AirExplosion, (int)TheGameLogic->getFrame());
	}
	pruneSets();
	takenSystems.insert((int)system->getSystemID());
	airSystems.insert((int)system->getSystemID());
	return true;
}

// The air's box: where the smoke of the last twenty seconds can be, inside the grid.  False when
// there is none.
bool airClip(const Volume &volume, const Tier &tier, const std::vector<WorldBox> &boxes, int now, float low[3], float high[3])
{
	float gridLow[3], gridHigh[3];
	boxOf(volume, tier.shape, gridLow, gridHigh);
	bool any = false;
	for (const WorldBox &box : boxes) {
		// what the wind and the flow carried it since, at most: the wind is 6 units a second, and
		// the air's smoke has no heat to rise with
		const float age = (float)(now - box.frame) / LOGICFRAMES_PER_SECOND;
		const float drift = 15.0f + 6.0f * age;
		for (unsigned i = 0; i < 3; ++i) {
			const float a = box.low[i] - (i == 2 ? 10.0f : drift), b = box.high[i] + (i == 2 ? 15.0f + 4.0f * age : drift);
			low[i] = any ? (std::min)(low[i], a) : a;
			high[i] = any ? (std::max)(high[i], b) : b;
		}
		any = true;
	}
	if (!any) return false;
	for (unsigned i = 0; i < 3; ++i) {
		low[i] = (std::max)(low[i], gridLow[i]);
		high[i] = (std::min)(high[i], gridHigh[i]);
		if (high[i] <= low[i]) return false;
	}
	return true;
}

// A blast front, from a blast this took or a system that starts one (VolumeShock::frontOf), `owner`
// the slot of the volume it started with (that volume born now).  One a place: a blast's many
// systems start one front, which keeps the class that pushes hardest and the first volume one of
// them started; two blasts that each started a volume keep a front each.
void addShock(const Coord3D &where, const VolumeShock::ShockClass &c, int now, int owner)
{
	for (Shock &shock : shocks) {
		const float dx = shock.at[0] - where.x, dy = shock.at[1] - where.y;
		if (now - shock.born >= 6 || dx * dx + dy * dy >= 60.0f * 60.0f || (owner >= 0 && shock.owner >= 0)) continue;
		if (c.push > shock.c.push) shock.c = c;
		if (owner >= 0) { shock.owner = owner; shock.ownerBorn = now; }
		return;
	}
	if (shocks.size() >= MaxShocks) shocks.erase(shocks.begin());
	shocks.push_back({ { where.x, where.y, where.z }, c, now, owner, now, false });
}

// A nuclear trigger that joined a live nuclear volume (a second flare inside its first seconds):
// its front, started on first sight with no volume, is that volume's, so it does not tear the cap it
// is part of apart.
void adoptFront(const Coord3D &where, int slot, int now)
{
	for (Shock &shock : shocks) {
		const float dx = shock.at[0] - where.x, dy = shock.at[1] - where.y;
		if (shock.owner < 0 && now - shock.born < 6 && dx * dx + dy * dy < 60.0f * 60.0f) {
			shock.owner = slot;
			shock.ownerBorn = volumes[slot].born;
		}
	}
}

// This step's fronts into the constants, the newest first, up to the setting's count (High 16, Low
// 8).  A front Gather has not yet checked against the shroud, or past its time, pushes nothing.
void fillShocks(int now)
{
	fronts.clear();
	const size_t cap = level == 2 ? MaxShocks : MaxShocks / 2;
	for (auto it = shocks.rbegin(); it != shocks.rend() && fronts.size() < cap; ++it) {
		const float age = float(now - it->born) / LOGICFRAMES_PER_SECOND;
		if (!it->checked || age < 0 || age > it->c.seconds) continue;
		const float radius = VolumeShock::frontRadius(it->c, age);
		fronts.push_back({ &*it, age, radius, VolumeShock::frontWidth(radius), VolumeShock::peakSpeed(it->c, age) });
	}
	sim.ShockCount[0] = (float)fronts.size();
	for (size_t i = 0; i < fronts.size(); ++i) {
		const Front &front = fronts[i];
		const Shock &shock = *front.shock;
		float *a = sim.Shocks[i * 2], *b = sim.Shocks[i * 2 + 1];
		for (unsigned k = 0; k < 3; ++k) a[k] = shock.at[k];
		a[3] = front.radius;
		b[0] = front.width;
		b[1] = front.speed;
		b[2] = VolumeShock::mixRate(shock.c, front.age);
		b[3] = VolumeShock::stirAccel(shock.c, front.age);
	}
}

// Whether a front leaves this volume alone: its own blast's (the slot's tenant then, not a later
// one), or one born beside it in the same moment (the shell that killed a tank): their timelines push
// their own smoke, and the tank's fan is laid where the shell's front would blow it away.
bool spares(const Volume &volume, int slot, const Shock &shock)
{
	if (shock.owner == slot && shock.ownerBorn == volume.born) return true;
	if (std::abs(volume.born - shock.born) > 10) return false;
	const float dx = shock.at[0] - (volume.origin[0] + (volume.blast[0] + 0.5f) * volume.cell);
	const float dy = shock.at[1] - (volume.origin[1] + (volume.blast[1] + 0.5f) * volume.cell);
	return dx * dx + dy * dy <= joinRadius(volume) * joinRadius(volume);
}

// Which of this step's fronts reach a box, a bit each (Origins.w): a front reaches it where its shell,
// three widths each side of the front and never thinner than 1.5 cells, meets the box.  `slot` is the
// volume's, or -1 for the air.
float shockMask(const float low[3], const float high[3], float cell, int slot)
{
	unsigned mask = 0;
	for (size_t i = 0; i < fronts.size(); ++i) {
		const Front &front = fronts[i];
		const Shock &shock = *front.shock;
		if (slot >= 0 && spares(volumes[slot], slot, shock)) continue;
		const float reach = 3.0f * (std::max)(front.width, 1.5f * cell);
		float nearest = 0, farthest = 0;
		for (unsigned k = 0; k < 3; ++k) {
			const float a = low[k] - shock.at[k], b = high[k] - shock.at[k];
			const float n = a > 0 ? a : b < 0 ? b : 0.0f, f = (std::max)(std::fabs(a), std::fabs(b));
			nearest += n * n;
			farthest += f * f;
		}
		const float inner = (std::max)(front.radius - reach, 0.0f), outer = front.radius + reach;
		if (nearest <= outer * outer && farthest >= inner * inner) mask |= 1u << i;
	}
	return (float)mask;
}

// An air particle inside a front this step moves with it, so the cells the air lays under it
// (AirTakeUp) follow the push instead of pulling the smoke back where it was.  A client particle:
// nothing in the logic reads where one is.  At most three quarters of an air cell a step.
void nudge(Particle *p, float dt, float cell)
{
	Coord3D &at = p->m_pos;
	float shift[3] = {};
	for (const Front &front : fronts) {
		const Shock &shock = *front.shock;
		const float d[3] = { at.x - shock.at[0], at.y - shock.at[1], at.z - shock.at[2] };
		const float r2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
		const float inner = (std::max)(front.radius - 3.0f * front.width, 0.0f), outer = front.radius + 3.0f * front.width;
		if (r2 > outer * outer || r2 < inner * inner) continue;
		const float r = std::sqrt(r2);
		const float move = front.speed * VolumeShock::profile(r, front.radius, front.width) * dt / (std::max)(r, 1.0f);
		for (unsigned k = 0; k < 3; ++k) shift[k] += d[k] * move;
	}
	const float moved = std::sqrt(shift[0] * shift[0] + shift[1] * shift[1] + shift[2] * shift[2]), most = 0.75f * cell;
	const float scale = moved > most ? most / moved : 1.0f;
	at.x += shift[0] * scale;
	at.y += shift[1] * scale;
	at.z += shift[2] * scale;
}

// How firmly each nuclear cap still holds its shape: the fronts of other blasts that pass its middle
// move it, and once moved CapRelease units it is let go for good (schedule), its volume ending
// NukeDisperseLife seconds on.  Once a logic frame, from Gather.
void holdCaps(int now)
{
	static int last = -1;
	const int frames = last < 0 || now <= last ? 1 : (std::min)(now - last, 4);
	last = now;
	for (unsigned slot = 0; slot < MaxVolumes; ++slot) {
		Volume &volume = volumes[slot];
		if (!volume.live || volume.kind != Nuclear || volume.hold < 0.05f) continue;
		const float t = secondsSince(volume, now);
		const float capZ = volume.blast[2] + 2.0f + 20.0f * (1 - std::exp(-t / 3.2f));	// schedule's
		const float middle[3] = { volume.origin[0] + (volume.blast[0] + 0.5f) * volume.cell,
			volume.origin[1] + (volume.blast[1] + 0.5f) * volume.cell, volume.origin[2] + (capZ + 0.5f) * volume.cell };
		for (const Shock &shock : shocks) {
			const float age = float(now - shock.born) / LOGICFRAMES_PER_SECOND;
			if (!shock.checked || age < 0 || age > shock.c.seconds || spares(volume, (int)slot, shock)) continue;
			const float d[3] = { middle[0] - shock.at[0], middle[1] - shock.at[1], middle[2] - shock.at[2] };
			volume.capPushed += VolumeShock::shiftAt(shock.c, age, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]),
				(float)frames / LOGICFRAMES_PER_SECOND);
		}
		volume.hold = VolumeShock::capHold(volume.capPushed);
		if (volume.hold < 0.05f) {
			volume.life = (std::min)(volume.life, now - volume.born + int(NukeDisperseLife * LOGICFRAMES_PER_SECOND));
			static unsigned released = 0;
			if (released++ < 8)
				DEBUG_LOG(("VOLSMOKE: frame %d, nuclear cap %u let go: other blasts moved it %.0f units\n", now, slot, volume.capPushed));
		}
	}
}

// The units that push the air, read from the client's drawables once a logic frame: where each
// stands, its velocity from where it stood the frame before, its kind and its template's size.
// A unit hidden by stealth or the shroud pushes nothing, or its wake would give it away.  Nothing
// here goes back to the logic.  Beside an overlapped logic tick the list stays last frame's.
void collectMovers(int now)
{
	if (LogicThread::isInsideWindow() || !TheGameClient || !TheTacticalView) return;
	Coord3D look;
	TheTacticalView->getPosition(&look);
	static std::unordered_map<DrawableID, std::pair<Coord3D, int>> seen, next;
	next.clear();
	static std::vector<Mover> found;
	found.clear();
	lastHulls.swap(hulls);
	hulls.clear();
	for (Drawable *drawable = TheGameClient->firstDrawable(); drawable; drawable = drawable->getNextDrawable()) {
		const bool aircraft = drawable->isKindOf(KINDOF_AIRCRAFT), walker = drawable->isKindOf(KINDOF_INFANTRY);
		if (!aircraft && !walker && !drawable->isKindOf(KINDOF_VEHICLE)) continue;
		if (drawable->isDrawableEffectivelyHidden() || drawable->getFullyObscuredByShroud()) continue;
		const Coord3D at = *drawable->getPosition();
		const DrawableID id = drawable->getID();
		float vel[3] = {};
		const auto last = seen.find(id);
		if (last != seen.end() && now > last->second.second && now - last->second.second <= 4) {
			const float perSecond = (float)LOGICFRAMES_PER_SECOND / (now - last->second.second);
			vel[0] = (at.x - last->second.first.x) * perSecond;
			vel[1] = (at.y - last->second.first.y) * perSecond;
			vel[2] = (at.z - last->second.first.z) * perSecond;
		}
		next[id] = { at, now };
		const float dx = at.x - look.x, dy = at.y - look.y;
		if (dx * dx + dy * dy > 900.0f * 900.0f) continue;
		const float speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
		const float ground = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(at.x, at.y, NULL) : 0.0f;
		const GeometryInfo &geometry = drawable->getTemplate()->getTemplateGeometryInfo();
		const float major = geometry.getMajorRadius(), minor = geometry.getMinorRadius();
		const float tall = geometry.getMaxHeightAbovePosition();
		const float hx = std::cos(drawable->getOrientation()), hy = std::sin(drawable->getOrientation());
		Mover mover = {};
		float (&m)[4][4] = mover.m;
		for (unsigned i = 0; i < 3; ++i) m[1][i] = vel[i];
		m[0][0] = at.x;
		m[0][1] = at.y;
		m[0][2] = at.z;
		float priority = 1.0f;
		if (aircraft) {
			const float height = at.z - ground;
			if (height < 3.0f) continue;	// on the pad or the runway
			// ponytail: a helicopter is told by the Comanche's helipad flag or by flying slowly; a
			// rotor bone would give the real disc
			if (drawable->isKindOf(KINDOF_PRODUCED_AT_HELIPAD) || speed < 60.0f) {
				const float radius = (std::max)(major, 15.0f) * 1.1f;
				const float close = (std::min)(1.0f, 1.6f - height / (6.0f * radius));
				if (close <= 0.0f) continue;
				m[0][2] = at.z + tall * 0.8f;
				m[0][3] = radius;
				m[1][3] = MoverRotor;
				m[2][2] = height;
				m[3][1] = close;
				priority = 0.25f;	// the one that shows most: kept before the rest
			} else {
				m[0][3] = (std::max)(major * 0.8f, 12.0f);
				m[1][3] = MoverJet;
				m[2][2] = height;
				m[3][1] = 1.0f;
			}
		} else if (walker) {
			if (speed < 8.0f) continue;
			m[0][3] = 8.0f;
			m[1][3] = MoverWalker;
		} else {
			hulls.push_back({ at.x, at.y, drawable->getOrientation(), major,
				geometry.getGeomType() == GEOMETRY_BOX ? minor : major * 0.7f, tall });
			if (speed < 6.0f) continue;
			m[0][2] = (std::min)(at.z, ground + 2.0f);
			m[0][3] = (std::max)(major, 8.0f);
			m[1][3] = MoverVehicle;
			m[2][0] = hx;
			m[2][1] = hy;
			m[2][2] = (std::max)(geometry.getGeomType() == GEOMETRY_BOX ? minor : major * 0.7f, 6.0f);
			m[2][3] = (std::max)(tall, 8.0f);
			m[3][0] = (id & 1) ? 1.0f : -1.0f;	// which way its wake turns, the same every frame
			m[3][1] = speed;
		}
		mover.distance = (dx * dx + dy * dy) * priority;
		found.push_back(mover);
	}
	seen.swap(next);
	// The blast fronts: past their time they go, and a new one pushes only once its place is seen not to
	// be in the shroud of the player whose view is drawn (a read outside the window; nothing is decided
	// in the logic by it).
	const Int seer = TheObserverCamera.getShroudPlayerIndex();
	for (auto it = shocks.begin(); it != shocks.end();) {
		const float age = (float)(now - it->born) / LOGICFRAMES_PER_SECOND;
		if (age > it->c.seconds) { it = shocks.erase(it); continue; }
		if (!it->checked) {
			const Coord3D at = { it->at[0], it->at[1], it->at[2] };
			if (seer >= 0 && ThePartitionManager && ThePartitionManager->getShroudStatusForPlayer(seer, &at) == CELLSHROUD_SHROUDED) {
				it = shocks.erase(it);
				continue;
			}
			it->checked = true;
		}
		++it;
	}
	holdCaps(now);
	const size_t cap = level == 2 ? MaxMovers : MaxMovers / 2;
	if (found.size() > cap)
		std::nth_element(found.begin(), found.begin() + cap, found.end(),
			[](const Mover &a, const Mover &b) { return a.distance < b.distance; });
	movers.assign(found.begin(), found.begin() + (std::min)(found.size(), cap));
	moverPeak = (std::max)(moverPeak, (unsigned)movers.size());
}

// The building's shape as its model draws it: each mesh of its render object, as a box along the
// building's heading round its own position, so a hall, a tower and a stack each stand where they
// stand.  Flat meshes (the bib, a decal) and slivers are left out, and of the rest the biggest by
// volume and the tallest are kept.  Client side: the drawable's render object, read from Gather.
void readParts(Object *object, Solid &solid)
{
	solid.partCount = 0;
	Drawable *drawable = object->getDrawable();
	if (!drawable) return;
	const float c = std::cos(solid.angle), s = std::sin(solid.angle), groundZ = solid.top - solid.tall;
	const float reach = 2.5f * (std::max)(solid.major, solid.minor) + 20.0f;
	struct Found { Part part; float volume; };
	Found found[48];
	unsigned count = 0;
	// ponytail: read again every two seconds (Gather's cadence), no cache: a model swapped for its
	// damaged one can come back at the same address.  Cache by model name if a town map hitches.
	auto take = [&](RenderObjClass *mesh) {
		if (!mesh || mesh->Is_Hidden() || count >= 48) return;
		{
			{
				AABoxClass box;
				mesh->Get_Obj_Space_Bounding_Box(box);
				const Matrix3D &m = mesh->Get_Transform();
				Vector3 centre;
				Matrix3D::Transform_Vector(m, box.Center, &centre);
				// the box's half sizes along the building's heading, across it and up
				float half[3] = {};
				for (unsigned axis = 0; axis < 3; ++axis) {
					const Vector3 edge(m[0][axis] * (&box.Extent.X)[axis], m[1][axis] * (&box.Extent.X)[axis], m[2][axis] * (&box.Extent.X)[axis]);
					half[0] += std::fabs(edge.X * c + edge.Y * s);
					half[1] += std::fabs(-edge.X * s + edge.Y * c);
					half[2] += std::fabs(edge.Z);
				}
				const float dx = centre.X - solid.x, dy = centre.Y - solid.y;
				const Part part = { dx * c + dy * s, -dx * s + dy * c, half[0], half[1], centre.Z - half[2] - groundZ, centre.Z + half[2] - groundZ };
				const bool flat = half[2] < 1.0f, sliver = half[0] < 0.75f || half[1] < 0.75f;
				const bool stray = std::fabs(part.along) > reach || std::fabs(part.across) > reach || half[0] > reach || half[1] > reach;
				if (!flat && !sliver && !stray && part.top > 2.0f)
					found[count++] = { part, half[0] * half[1] * (part.top - (std::max)(part.bottom, 0.0f)) };
			}
		}
	};
	// every model its draw modules show
	for (DrawModule **modules = drawable->getDrawModulesNonDirty(); modules && *modules; ++modules) {
		const ObjectDrawInterface *draw = (*modules)->getObjectDrawInterface();
		RenderObjClass *model = draw ? ((W3DModelDraw *)draw)->getRenderObject() : nullptr;
		if (!model || model->Is_Hidden()) continue;
		if (model->Class_ID() == RenderObjClass::CLASSID_HLOD) {
			// the level of detail it draws now and the models hung on it, not every level's copy
			HLodClass *lod = (HLodClass *)model;
			const int level = lod->Get_LOD_Level();
			for (int i = 0; i < lod->Get_Lod_Model_Count(level); ++i) take(lod->Peek_Lod_Model(level, i));
			for (int i = 0; i < lod->Get_Additional_Model_Count(); ++i) take(lod->Peek_Additional_Model(i));
		} else if (model->Get_Num_Sub_Objects() > 0) {
			for (int i = 0; i < model->Get_Num_Sub_Objects(); ++i) {
				RenderObjClass *mesh = model->Get_Sub_Object(i);
				take(mesh);
				if (mesh) mesh->Release_Ref();
			}
		} else take(model);	// one mesh: the model itself
	}
	if (count) {
		unsigned tallest = 0;
		for (unsigned i = 1; i < count; ++i)
			if (found[i].part.top > found[tallest].part.top) tallest = i;
		std::swap(found[0], found[tallest]);
		std::sort(found + 1, found + count, [](const Found &a, const Found &b) { return a.volume > b.volume; });
		for (unsigned i = 0; i < count && solid.partCount < MaxParts; ++i) solid.parts[solid.partCount++] = found[i].part;
	}
}

// The surface a building or a wreck burns on, read off its meshes' triangles in world space: the
// middle of every face looking up (a roof, a slope, a broken floor) and the top edge of every wall,
// each with its outward normal.  A fire's fuel comes off these points and nowhere else (scheduleFire),
// so its flames stand on the mesh.  Read in Gather, outside the logic window, and kept while the
// models it is drawn with are the same ones: the key is their names and triangle counts, never a
// pointer (a damaged model came back at the same address in round 2).  FireSurface: with the globals.

template <class Visit> void forEachMesh(Drawable *drawable, Visit &&visit)
{
	for (DrawModule **modules = drawable->getDrawModulesNonDirty(); modules && *modules; ++modules) {
		const ObjectDrawInterface *draw = (*modules)->getObjectDrawInterface();
		RenderObjClass *model = draw ? ((W3DModelDraw *)draw)->getRenderObject() : nullptr;
		if (!model || model->Is_Hidden()) continue;
		if (model->Class_ID() == RenderObjClass::CLASSID_HLOD) {
			HLodClass *lod = (HLodClass *)model;
			const int level = lod->Get_LOD_Level();
			for (int i = 0; i < lod->Get_Lod_Model_Count(level); ++i) visit(lod->Peek_Lod_Model(level, i));
			for (int i = 0; i < lod->Get_Additional_Model_Count(); ++i) visit(lod->Peek_Additional_Model(i));
		} else if (model->Get_Num_Sub_Objects() > 0) {
			for (int i = 0; i < model->Get_Num_Sub_Objects(); ++i) {
				RenderObjClass *mesh = model->Get_Sub_Object(i);
				visit(mesh);
				if (mesh) mesh->Release_Ref();
			}
		} else visit(model);
	}
}

MeshModelClass *meshModelOf(RenderObjClass *object)
{
	if (!object || object->Is_Hidden() || object->Class_ID() != RenderObjClass::CLASSID_MESH) return nullptr;
	MeshModelClass *model = ((MeshClass *)object)->Peek_Model();
	// (a skin's vertices are in its bones' space, so its transform puts them in the wrong place)
	return model && !model->Get_Flag(MeshGeometryClass::SKIN) && model->Get_Vertex_Count() > 0 && model->Get_Polygon_Count() > 0
		? model : nullptr;
}

// Its models' key: their names and sizes, hashed.
unsigned surfaceKey(Drawable *drawable)
{
	unsigned key = 2166136261u;
	forEachMesh(drawable, [&](RenderObjClass *mesh) {
		MeshModelClass *model = meshModelOf(mesh);
		if (!model) return;
		for (const char *c = mesh->Get_Name(); c && *c; ++c) key = (key ^ (unsigned char)*c) * 16777619u;
		key = (key ^ (unsigned)model->Get_Polygon_Count()) * 16777619u;
	});
	return key;
}

void readSurface(Drawable *drawable, FireSurface &surface)
{
	surface.points.clear();
	surface.roofs.clear();
	forEachMesh(drawable, [&](RenderObjClass *mesh) {
		MeshModelClass *model = meshModelOf(mesh);
		if (!model) return;
		const Matrix3D &m = mesh->Get_Transform();
		const Vector3 *vertices = model->Get_Vertex_Array();
		const TriIndex *tris = model->Get_Polygon_Array();
		const int count = model->Get_Polygon_Count(), vertexCount = model->Get_Vertex_Count();
		for (int t = 0; t < count; ++t) {
			const TriIndex &tri = tris[t];
			if (tri.I >= vertexCount || tri.J >= vertexCount || tri.K >= vertexCount) continue;
			Vector3 a, b, c;
			Matrix3D::Transform_Vector(m, vertices[tri.I], &a);
			Matrix3D::Transform_Vector(m, vertices[tri.J], &b);
			Matrix3D::Transform_Vector(m, vertices[tri.K], &c);
			Vector3 n;
			Vector3::Cross_Product(b - a, c - a, &n);
			const float area2 = n.Length();
			if (area2 < 0.5f) continue;	// slivers and specks
			n /= area2;
			if (n.Z > 0.35f) {
				surface.points.push_back({ (a.X + b.X + c.X) / 3, (a.Y + b.Y + c.Y) / 3, (a.Z + b.Z + c.Z) / 3, n.X, n.Y, n.Z });
				if (surface.roofs.size() < MaxRoofTriangles * 9)
					surface.roofs.insert(surface.roofs.end(), { a.X, a.Y, a.Z, b.X, b.Y, b.Z, c.X, c.Y, c.Z });
			} else if (n.Z > -0.35f) {
				// a wall: the middle of its top edge, where the flame licks over it.  Only a triangle
				// with a level top edge has one: of a wall quad's two, the other's two highest corners
				// are a vertical edge, whose middle is halfway up the wall.
				const Vector3 *lowest = a.Z <= b.Z && a.Z <= c.Z ? &a : b.Z <= c.Z ? &b : &c;
				const float high = (std::max)((std::max)(a.Z, b.Z), c.Z);
				const Vector3 top = (a + b + c - *lowest) * 0.5f;
				if (high - top.Z < 0.25f)
					surface.points.push_back({ top.X, top.Y, top.Z, n.X, n.Y, n.Z });
			}
		}
	});
	// ponytail: kept to every k-th point past the cap; a voxel mask if a big model loses its edges
	if (surface.points.size() > MaxSurfacePoints) {
		const size_t stride = (surface.points.size() + MaxSurfacePoints - 1) / MaxSurfacePoints;
		size_t kept = 0;
		for (size_t i = 0; i < surface.points.size(); i += stride) surface.points[kept++] = surface.points[i];
		surface.points.resize(kept);
	}
}

void gatherSolids(int now)
{
	if (now - solidsFrame < 2 * LOGICFRAMES_PER_SECOND && now >= solidsFrame) return;
	solidsFrame = now;
	lastSolids.swap(solids);
	solids.clear();
	// the player whose fog is drawn: an observer may be watching through someone
	const Int seer = TheObserverCamera.getShroudPlayerIndex();
	// every grid lays its floor again from the new list
	for (Volume &volume : volumes) volume.floorFrame = -100000;
	airVolume.floorFrame = tallVolume.floorFrame = -100000;
	for (Object *object = TheGameLogic->getFirstObject(); object; object = object->getNextObject()) {
		if (!object->isKindOf(KINDOF_STRUCTURE)) continue;
		if (seer >= 0 && object->getShroudedStatus(seer) == OBJECTSHROUD_SHROUDED) continue;
		const Coord3D *at = object->getPosition();
		const GeometryInfo &geometry = object->getGeometryInfo();
		Solid solid = {};
		solid.x = at->x;
		solid.y = at->y;
		solid.top = at->z + geometry.getMaxHeightAbovePosition();
		solid.major = geometry.getMajorRadius();
		solid.minor = geometry.getMinorRadius();
		solid.angle = object->getOrientation();
		solid.tall = geometry.getMaxHeightAbovePosition();
		solid.box = geometry.getGeomType() == GEOMETRY_BOX;
		solid.burns = !object->isKindOf(KINDOF_DEFENSIVE_WALL);
		solid.id = object->getID();
		readParts(object, solid);
		solids.push_back(solid);
	}
	static unsigned partsLogged = 0;
	if (partsLogged < 1 && !solids.empty()) {
		++partsLogged;
		unsigned shaped = 0;
		for (const Solid &solid : solids) shaped += solid.partCount ? 1 : 0;
		DEBUG_LOG(("VOLSMOKE: frame %d, %u structures seen, %u with their meshes' boxes\n", now, (unsigned)solids.size(), shaped));
	}
}

void fillMovers()
{
	sim.MoverCount[0] = (float)movers.size();
	for (size_t i = 0; i < movers.size(); ++i) std::memcpy(sim.Movers[i * 4], movers[i].m, sizeof(movers[i].m));
}

// One logic frame of the air (g 0) or the tall air (g 1): it follows the camera, takes the particles
// of every system it holds (the nearest the camera first, up to the setting's count), and runs the
// same passes as a blast.  The air runs first and hands the tall air what climbed out of its top.
void stepAir(ID3D11DeviceContext *context, unsigned g, int now, float dt, float seconds, const float sunToward[3])
{
	const bool tall = g == 1;
	Tier &tier = tall ? state.tall : state.air;
	const AirShape &air = tall ? TallShapes[level] : AirShapes[level];
	const TierShape &shape = tier.shape;
	Volume &volume = tall ? tallVolume : airVolume;
	std::vector<WorldBox> &smokeBoxes = tall ? tallBoxes : airBoxes;
	std::unordered_set<int> &systems = tall ? tallSystems : airSystems;
	if (!volume.live) {
		volume = Volume();
		volume.live = volume.reset = true;
		volume.kind = Air;
		volume.cell = air.cell;
	}
	const float cell = volume.cell;

	// Where the camera looks, the box's middle, its floor eight cells under the ground there (the
	// tall air's four, at twice the cell).
	Coord3D look = {};
	if (TheTacticalView) TheTacticalView->getPosition(&look);
	const float ground = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(look.x, look.y, NULL) : 0.0f;
	const float want[3] = { look.x - shape.gx * cell * 0.5f, look.y - shape.gy * cell * 0.5f, ground - (tall ? 4.0f : 8.0f) * cell };
	float shift[3] = {};
	bool moved = false;
	for (unsigned i = 0; i < 3; ++i) {
		const float cells = std::floor((want[i] - volume.origin[i]) / cell + 0.5f);
		if (volume.reset) volume.origin[i] = want[i];
		else if (std::fabs(cells) >= (i == 2 ? 3.0f : 4.0f)) {	// in steps, so it is not moved every frame
			shift[i] = cells;
			volume.origin[i] += cells * cell;
			moved = true;
		}
	}
	std::memset(&sim, 0, sizeof(sim));
	sim.Volumes[0][0] = 1;
	sim.Volumes[0][2] = cell;
	sim.Volumes[0][3] = volume.reset ? 1.0f : 0.0f;
	fillShocks(now);	// before the particles, which the fronts move (nudge)
	if (moved) {
		for (unsigned i = 0; i < 3; ++i) sim.Motion[0][i] = shift[i];
		upload(context, tier.simConstants, &sim, sizeof(sim));
		context->CSSetConstantBuffers(0, 1, &tier.simConstants);
		const unsigned in = tier.current, out = 1 - tier.current;
		run(context, tier, ScrollProgram, { tier.velocity[0].view, tier.density[in].view },
			{ tier.velocity[1].write, tier.density[out].write }, 1);
		unbindCompute(context);
		context->CopyResource(tier.velocity[0].texture, tier.velocity[1].texture);
		// the pressure the sweeps start from belongs to the old place: start this step's from nothing
		const float zero[4] = {};
		context->ClearUnorderedAccessViewFloat(tier.pressure[0].write, zero);
		tier.current = out;
		volume.floorFrame = -100000;	// the ground under it moved
		for (unsigned i = 0; i < 3; ++i) sim.Motion[0][i] = 0;
	}
	layFloor(context, tier, 0, volume, now);

	// The particles, as spheres in cells.
	struct Source { float x, y, z, r, soot, dust, bright, distance, heat; };	// distance sorts on the CPU, heat goes to the GPU
	static std::vector<Source> found;
	found.clear();
	float low[3], high[3];
	boxOf(volume, shape, low, high);
	for (auto it = systems.begin(); it != systems.end();) {
		const int id = *it;
		ParticleSystem *system = TheParticleSystemManager->findParticleSystem((ParticleSystemID)id);
		if (!system) {
			it = systems.erase(it);
			takenSystems.erase(id);
			airSystems.erase(id);
			tallSystems.erase(id);
			airOutside.erase(id);
			continue;
		}
		++it;
		if (!tall && tallSystems.count(id)) continue;	// the tall air's now
		if (fireTaken.count(id)) continue;	// a burning building's smoke, its fire slab's soot instead
		const bool dusty = airLookOf(system->getTemplate(), system).dust;
		const float fade = system->getStealthFade();
		unsigned inside = 0, outside = 0;
		bool above = false;
		const size_t start = found.size();
		// A missile's smoke trail (a streak) is a ribbon through its particles in order, one laid a
		// frame, so a fast missile leaves them twenty units apart: as spheres they stood as a dotted
		// line of blobs.  Spheres are laid along each link as well, and the ribbon's bright jet smoke
		// is taken thicker and paler than a cloud's puff, or it read as a faint brown haze.
		const bool streak = system->isUsingStreak();
		// A missile's trail (the nuclear missile's is puffs).  A launcher's backblast and ignition
		// cloud (MissileBackblastClouds, the Patriot's) is a white puff laid whole for its particles'
		// life: taken as a trail it sank with their age and was gone before it showed.
		const char *named = system->getTemplate()->getName().str();
		const bool launchPuff = (std::strstr(named, "Backblast") || std::strstr(named, "Ignition") || std::strstr(named, "Launch"))
			&& !std::strstr(named, "Explosion");
		const bool trail = !launchPuff && (streak || system->getPriority() == WEAPON_TRAIL
			|| ((std::strstr(named, "Missile") || std::strstr(named, "Missle")) && !std::strstr(named, "Explosion")));
		// a ballistic or cruise missile's exhaust is a white billowing plume; a rocket's, a thin grey trail
		const bool plume = trail && (std::strstr(named, "Scud") || std::strstr(named, "Neutron") || std::strstr(named, "Tomahawk"));
		bool chained = false;
		// the blast fronts passing its particles move them (nudge)
		if (!fronts.empty())
			for (Particle *p = system->getFirstParticle(); p; p = p->m_systemNext) nudge(p, dt, cell);
		// A rocket's ribbon is laid straight: its points scatter off the fire line (EA's MissileExhaust,
		// which feeds it, throws each point sideways at up to 0.2 a frame), so the ribbon drew a regular
		// sine wiggle a few units wide where the rocket itself flies within a unit of a straight line
		// (logged off the drawables).  Each point is taken as the mean of its neighbours along the
		// ribbon, fewer towards its ends, so the head stays on the rocket.
		static std::vector<Coord3D> straight;
		straight.clear();
		const bool straighten = trail && streak && !plume;
		if (straighten) {
			static std::vector<Coord3D> raw;
			raw.clear();
			for (Particle *p = system->getFirstParticle(); p; p = p->m_systemNext) raw.push_back(*p->getPosition());
			const int count = (int)raw.size();
			for (int i = 0; i < count; ++i) {
				const int reach = (std::min)((std::min)(i, count - 1 - i), RibbonSmoothPoints);
				Coord3D mean = { 0, 0, 0 };
				for (int k = i - reach; k <= i + reach; ++k) { mean.x += raw[k].x; mean.y += raw[k].y; mean.z += raw[k].z; }
				const float n = 2.0f * reach + 1.0f;
				straight.push_back({ mean.x / n, mean.y / n, mean.z / n });
			}
		}
		unsigned index = 0;
		for (Particle *p = system->getFirstParticle(); p; p = p->m_systemNext, ++index) {
			const float alpha = (std::min)(p->getAlpha() * fade * AirCoverage, 0.95f);
			if (alpha < 0.02f) { chained = false; continue; }
			const Coord3D *at = straighten ? &straight[index] : p->getPosition();
			// at most 8 cells, so a sphere reaches 27 bricks at most (AirTileEntries)
			// (a ribbon's spheres are laid along its links too, so it may go thinner without breaking up).
			// A trail's particle is a puff of its own size, hashed from the particle, wider the older it
			// is: a Scud Storm's columns stood as straight pillars of one width all the way up.
			float puff = 1.0f, thick = 1.0f;
			if (trail) {
				// the particle's address is the same for its whole life (a client-side look, nothing to replay)
				const unsigned h = (p->getPersonality() ^ unsigned(reinterpret_cast<uintptr_t>(p) >> 4)) * 2654435761u;
				const float age = p->m_lifetime ? (std::min)((std::max)(1.0f - (float)p->getLifetimeLeft() / (float)p->m_lifetime, 0.0f), 1.0f) : 0.0f;
				puff = (0.65f + 0.8f * ((h >> 8) & 255) / 255.0f) * (1.0f + (plume ? PlumeSpread : TrailSpread) * age);
				thick = (0.55f + 0.9f * ((h >> 16) & 255) / 255.0f) * std::pow(1.0f - age, TrailAgeFade) * (plume ? PlumeGain : RocketGain);
			}
			// (a straightened rocket ribbon at 0.6 of a cell passed between the cells' middles and laid
			// next to nothing)
			const float radius = (std::min)((std::max)(p->getSize() * 0.6f, cell * (straighten ? 0.9f : streak ? 0.6f : 0.8f)) * puff, cell * 8.0f);
			// inside the box less its faded rim
			const float rim = 4.0f * cell;
			if (at->x < low[0] + rim || at->x > high[0] - rim || at->y < low[1] + rim || at->y > high[1] - rim
					|| at->z < low[2] || at->z > high[2] - rim) {
				++outside;
				// a trail climbing out of the top goes to the tall air; anything else only once well
				// over it keeps its billboards, so a burning building's column that tops the box by a
				// little stays in the air as the box steps with the camera
				above = above || at->z > high[2] + (trail && !state.tallRefused ? AirTallOver - rim : AirTrailOver);
				chained = false;
				continue;
			}
			++inside;
			// the density whose optical depth through the sphere's middle is the particle's opacity
			const float density = AirGain * (streak ? StreakGain[g] : 1.0f) * thick * -std::log(1.0f - alpha) / (2.0f * radius * AirExtinction);
			const RGBColor *colour = p->getColor();
			const float luma = (std::min)(1.0f, 0.3f * colour->red + 0.59f * colour->green + 0.11f * colour->blue);
			// a trail's cells carry their grey above TrailTag, which is what tells them in the passes
			// (a launch puff is tagged white too: a puff's own grey is at most the air's 0.5 albedo)
			const float grey = launchPuff ? TrailTag + TrailTagSpan : trail ? TrailTag + TrailTagSpan * (plume ? 1.0f : luma * RocketGrey) : luma;
			const float dx = at->x - look.x, dy = at->y - look.y;
			const Source here = { (at->x - low[0]) / cell - 0.5f, (at->y - low[1]) / cell - 0.5f, (at->z - low[2]) / cell - 0.5f,
				radius / cell, dusty ? 0.0f : density, dusty ? density * 0.6f : 0.0f, grey, dx * dx + dy * dy, 0.0f };
			if (chained) {
				const Source &last = found.back();
				const float gap = std::sqrt((here.x - last.x) * (here.x - last.x) + (here.y - last.y) * (here.y - last.y)
					+ (here.z - last.z) * (here.z - last.z));
				const unsigned links = (std::min)(unsigned(gap / (0.6f * (std::min)(here.r, last.r))), 12u);
				const Source from = last;
				for (unsigned k = 1; k < links; ++k) {
					const float u = (float)k / links;
					found.push_back({ from.x + (here.x - from.x) * u, from.y + (here.y - from.y) * u, from.z + (here.z - from.z) * u,
						from.r + (here.r - from.r) * u, from.soot + (here.soot - from.soot) * u, from.dust + (here.dust - from.dust) * u,
						from.bright + (here.bright - from.bright) * u, from.distance + (here.distance - from.distance) * u,
						from.heat + (here.heat - from.heat) * u });
				}
			}
			found.push_back(here);
			chained = streak;
		}
		// A system with no particle in the box (a plume past the screen's edge zoomed out, a column
		// over a cliff) keeps its billboards until the box comes to it: nothing else would draw it.
		// One that climbs out of the air's top goes to the tall air whole, its part inside too, and
		// stays there: a nuclear missile's trail coming down from a thousand units up drew in the
		// air as a short stub under nothing, a Scud's arc lost its top.  Past the tall air's own top
		// a trail's highest part is left out (the play camera looks down on it from under there).
		if (above && !tall && !(trail && !state.tallRefused)) {
			found.resize(start);
			airOutside.insert(id);
		} else if (above && !tall) {
			found.resize(start);
			tallSystems.insert(id);
			airOutside.erase(id);
			static unsigned tallLogged = 0;
			if (tallLogged++ < 24)
				DEBUG_LOG(("VOLSMOKE: frame %d, '%s' climbed out of the air: the tall air takes it\n", now,
					system->getTemplate()->getName().str()));
		} else if (outside && !inside) airOutside.insert(id);
		else airOutside.erase(id);
	}
	// The nozzle flames, at the tail of each rocket in the box (gatherNozzles): a small hot ball of
	// trail smoke, so its heat dies with the trail's in a few frames once the rocket has moved on and
	// it glows only where the rocket is.  A rocket's first frames burn bigger, the launch flash.
	for (const Nozzle &nozzle : nozzles) {
		if (nozzle.x < low[0] + 4 * cell || nozzle.x > high[0] - 4 * cell || nozzle.y < low[1] + 4 * cell || nozzle.y > high[1] - 4 * cell
				|| nozzle.z < low[2] || nozzle.z > high[2] - 4 * cell)
			continue;
		const bool launch = nozzle.frames < 3;
		const float dx = nozzle.x - look.x, dy = nozzle.y - look.y;
		found.push_back({ (nozzle.x - low[0]) / cell - 0.5f, (nozzle.y - low[1]) / cell - 0.5f, (nozzle.z - low[2]) / cell - 0.5f,
			(nozzle.big ? 1.5f : 1.1f) * (launch ? 1.4f : 1.0f), NozzleSoot, 0.0f, TrailTag + TrailTagSpan, dx * dx + dy * dy,
			NozzleHeat[nozzle.big ? 1 : 0] + (launch ? 0.5f : 0.0f) });
	}
	const size_t cap = air.particles;
	if (found.size() > cap) {
		airLeftOut += unsigned(found.size() - cap);
		std::nth_element(found.begin(), found.begin() + cap, found.end(),
			[](const Source &a, const Source &b) { return a.distance < b.distance; });
		found.resize(cap);
	}
	airFed += unsigned(found.size());

	// Each 8-cell brick's list of the spheres that reach it, counted, then filled.
	const unsigned tilesX = shape.gx / 8, tilesY = shape.gy / 8, tilesZ = shape.gz / 8, tiles = tilesX * tilesY * tilesZ;
	const unsigned room = air.particles * AirTileEntries;
	static std::vector<unsigned> ranges, index, fill;
	ranges.assign(tiles * 2, 0);
	auto each = [&](const Source &s, auto &&visit) {
		auto span = [](float at, float r, unsigned count, unsigned &a, unsigned &b) {
			a = (unsigned)(std::max)(0, int((at - r) / 8)); b = (unsigned)(std::min)(int(count) - 1, int((at + r) / 8));
		};
		unsigned x0, x1, y0, y1, z0, z1;
		span(s.x, s.r, tilesX, x0, x1);
		span(s.y, s.r, tilesY, y0, y1);
		span(s.z, s.r, tilesZ, z0, z1);
		for (unsigned z = z0; z <= z1; ++z)
			for (unsigned y = y0; y <= y1; ++y)
				for (unsigned x = x0; x <= x1; ++x) visit((z * tilesY + y) * tilesX + x);
	};
	unsigned entries = 0;
	for (const Source &s : found) each(s, [&](unsigned t) { ++ranges[t * 2 + 1]; });
	for (unsigned t = 0; t < tiles; ++t) {
		const unsigned count = (std::min)(ranges[t * 2 + 1], room - (std::min)(entries, room));
		ranges[t * 2] = entries;
		ranges[t * 2 + 1] = count;
		entries += count;
	}
	index.assign((std::max)(entries, 1u), 0);
	fill.assign(tiles, 0);
	for (unsigned n = 0; n < found.size(); ++n)
		each(found[n], [&](unsigned t) { if (fill[t] < ranges[t * 2 + 1]) index[ranges[t * 2] + fill[t]++] = n; });
	static std::vector<float> packed;
	packed.assign((std::max)(found.size(), (size_t)1) * 8, 0.0f);
	for (size_t n = 0; n < found.size(); ++n) {
		const Source &s = found[n];
		const float values[8] = { s.x, s.y, s.z, s.r, s.soot, s.dust, s.bright, s.heat };
		std::memcpy(&packed[n * 8], values, sizeof(values));
	}
	upload(context, state.sources[g], packed.data(), packed.size() * sizeof(float));
	upload(context, state.tileRange[g], ranges.data(), ranges.size() * sizeof(unsigned));
	upload(context, state.tileIndex[g], index.data(), index.size() * sizeof(unsigned));

	// Where its smoke is, a box a second: the particles, and what the rotors lift and the blast
	// fronts push out of them.
	auto grow = [&](const float a[3], const float b[3]) {
		if (smokeBoxes.empty() || now - smokeBoxes.back().frame >= LOGICFRAMES_PER_SECOND) {
			WorldBox box;
			for (unsigned i = 0; i < 3; ++i) { box.low[i] = 1e9f; box.high[i] = -1e9f; }
			box.frame = now;
			smokeBoxes.push_back(box);
		}
		WorldBox &box = smokeBoxes.back();
		for (unsigned i = 0; i < 3; ++i) {
			box.low[i] = (std::min)(box.low[i], a[i]);
			box.high[i] = (std::max)(box.high[i], b[i]);
		}
	};
	for (const Source &s : found) {
		const float centre[3] = { low[0] + (s.x + 0.5f) * cell, low[1] + (s.y + 0.5f) * cell, low[2] + (s.z + 0.5f) * cell };
		const float a[3] = { centre[0] - s.r * cell, centre[1] - s.r * cell, centre[2] - s.r * cell };
		const float b[3] = { centre[0] + s.r * cell, centre[1] + s.r * cell, centre[2] + s.r * cell };
		grow(a, b);
	}
	if (!smokeBoxes.empty()) {	// only where the air already holds smoke
		for (const Mover &mover : movers) {
			const float (&m)[4][4] = mover.m;
			if (int(m[1][3] + 0.5f) != MoverRotor) continue;
			const float reach = m[0][3] * 3.0f;
			const float a[3] = { m[0][0] - reach, m[0][1] - reach, m[0][2] - m[2][2] };
			const float b[3] = { m[0][0] + reach, m[0][1] + reach, m[0][2] + 20.0f };
			grow(a, b);
		}
		// a front carries smoke out to its shell's far side
		for (const Front &front : fronts) {
			const float reach = front.radius + 3.0f * front.width;
			const float a[3] = { front.shock->at[0] - reach, front.shock->at[1] - reach, front.shock->at[2] - 20.0f };
			const float b[3] = { front.shock->at[0] + reach, front.shock->at[1] + reach, front.shock->at[2] + reach };
			grow(a, b);
		}
	}
	// two soot half-lives on, what is left of a box's smoke is under a quarter and fading
	while (!smokeBoxes.empty() && now - smokeBoxes.front().frame > int(2 * AirSootHalfLife * LOGICFRAMES_PER_SECOND))
		smokeBoxes.erase(smokeBoxes.begin());

	sim.Step[0] = dt;
	sim.Step[1] = seconds;
	sim.Step[2] = DustSettle / cell;
	sim.Step[3] = AirTakeUp;
	sim.Forces[0] = Buoyancy;
	sim.Forces[1] = Weight;
	sim.Forces[2] = Vorticity;
	sim.Forces[3] = Drag;
	sim.Keep[0] = std::pow(0.5f, dt / HeatHalfLife);
	sim.Keep[1] = std::pow(0.5f, dt / VelocityHalfLife);
	sim.Keep[2] = std::pow(0.5f, dt / AirDustHalfLife);
	sim.Keep[3] = std::pow(0.5f, dt / AirSootHalfLife);
	for (unsigned i = 0; i < 3; ++i) sim.Sun[i] = sunToward[i];
	sim.Sun[3] = AirExtinction;
	sim.Wind[0] = WindX;
	sim.Wind[1] = WindY;
	sim.Wind[2] = std::pow(0.5f, dt / TrailHalfLife[g]);	// a trail's cells (AdvectPass)
	sim.Wind[3] = TrailEddy;
	sim.Shape[0][2] = 1000.0f;	// no ceiling
	for (unsigned i = 0; i < 3; ++i) sim.Origins[0][i] = volume.origin[i];
	{
		float gridLow[3], gridHigh[3];
		boxOf(volume, shape, gridLow, gridHigh);
		sim.Origins[0][3] = shockMask(gridLow, gridHigh, cell, -1);
	}
	fillMovers();
	upload(context, tier.simConstants, &sim, sizeof(sim));
	context->CSSetConstantBuffers(0, 1, &tier.simConstants);
	context->CSSetSamplers(0, 1, &state.linear);

	const unsigned in = tier.current, out = 1 - tier.current;
	run(context, tier, CurlProgram, { tier.velocity[0].view }, { tier.curl.write }, 1);
	run(context, tier, AdvectProgram, { tier.velocity[0].view, tier.density[in].view, tier.curl.view },
		{ tier.velocity[1].write, tier.density[out].write, tier.pressure[0].write }, 1,
		{ state.sourcesView[g], state.tileRangeView[g], state.tileIndexView[g] });
	run(context, tier, DivergenceProgram, { tier.velocity[1].view }, { tier.divergence.write }, 1);
	for (unsigned sweep = 0; sweep < air.sweeps; ++sweep) {
		const unsigned from = sweep & 1;
		run(context, tier, JacobiProgram, { tier.pressure[from].view, tier.divergence.view }, { tier.pressure[1 - from].write }, 1);
	}
	run(context, tier, ProjectProgram, { tier.pressure[0].view, tier.velocity[1].view }, { tier.velocity[0].write }, 1);
	run(context, tier, LightProgram, { tier.density[out].view }, { tier.light.write }, 1);
	unbindCompute(context);
	tier.current = out;
	volume.reset = false;
}

// A volume's world box, its whole grid.
void boxOf(const Volume &volume, const TierShape &shape, float low[3], float high[3])
{
	const unsigned sides[3] = { shape.gx, shape.gy, shape.gz };
	for (unsigned i = 0; i < 3; ++i) {
		low[i] = volume.origin[i];
		high[i] = volume.origin[i] + sides[i] * volume.cell;
	}
}

// A world box's pixels in the camera's viewport, false when none of it is in front of the camera.
bool screenRect(const float boxLow[3], const float boxHigh[3], const float viewProjection[16],
	const HazardCloud::Camera &camera, D3D11_RECT &rect)
{
	float low[2] = { 1e9f, 1e9f }, high[2] = { -1e9f, -1e9f };
	bool behind = false;
	for (unsigned corner = 0; corner < 8; ++corner) {
		const float world[4] = { (corner & 1) ? boxHigh[0] : boxLow[0], (corner & 2) ? boxHigh[1] : boxLow[1],
			(corner & 4) ? boxHigh[2] : boxLow[2], 1 };
		float clip[4] = {};
		for (unsigned column = 0; column < 4; ++column)
			for (unsigned k = 0; k < 4; ++k) clip[column] += world[k] * viewProjection[k * 4 + column];
		if (clip[3] <= 1.0f) { behind = true; break; }
		for (unsigned axis = 0; axis < 2; ++axis) {
			low[axis] = (std::min)(low[axis], clip[axis] / clip[3]);
			high[axis] = (std::max)(high[axis], clip[axis] / clip[3]);
		}
	}
	const LONG left = LONG(camera.x), top = LONG(camera.y);
	const LONG right = LONG(camera.x + camera.width), bottom = LONG(camera.y + camera.height);
	if (behind) {
		rect = { left, top, right, bottom };
		return true;
	}
	if (low[0] > 1 || high[0] < -1 || low[1] > 1 || high[1] < -1) return false;
	rect.left = (std::max)(left, LONG(camera.x + (low[0] * 0.5f + 0.5f) * camera.width) - 1);
	rect.right = (std::min)(right, LONG(camera.x + (high[0] * 0.5f + 0.5f) * camera.width) + 2);
	rect.top = (std::max)(top, LONG(camera.y + (0.5f - high[1] * 0.5f) * camera.height) - 1);
	rect.bottom = (std::min)(bottom, LONG(camera.y + (0.5f - low[1] * 0.5f) * camera.height) + 2);
	return rect.right > rect.left && rect.bottom > rect.top;
}

// How long each kind lives, in seconds, how big its cell is, how far and how long after its blast
// another system of the same FX joins it, and which of those it takes.
// A shell's lived 12 s: an Overlord shell landing on a fallen War Factory's rubble (the shell volume
// at its middle a third of a second after the fall) left its dirt and dust skirt as tan lobes beside
// the column for 6 s and more, its dust laid so thick that the halved DustHalfLife still left it opaque.
// Its stock puff (BuggyMissileExplosionSmoke) lives 3.3 s.
// A shell's or a rocket's (scale under 2) lives 4 s, a bomb's 12 for its small mushroom.  A building's
// fall 3.5, its ground roll the half second past the old 3.  A launch's 4, its cloud's roll.
float lifeOf(Kind kind, float scale)
{
	return kind == TankDeath ? 4.25f : kind == Structure ? 3.5f : kind == Nuclear ? 90.0f : kind == Launch ? 4.0f
		: kind == Muzzle ? MuzzleLife : kind == Shell && scale < 2.0f ? 4.0f : 12.0f;
}
// (a gun's at 1.5 units, its 48 cells 72 units across: the dust round the tank's front and the fireball's whole reach)
float cellOf(Kind kind, float scale)
{
	return kind == TankDeath ? 3.0f : kind == Nuclear ? 10.0f : kind == Launch ? 4.0f * scale : kind == Muzzle ? 1.5f * scale : 2.0f * scale;
}
float joinRadius(const Volume &volume)
{
	if (volume.kind == Structure) return (std::max)(volume.hull[2], volume.hull[3]) * volume.cell + 40.0f;
	// a gun's own FX all go off at its muzzle bone, the ground ring under it
	return volume.kind == Nuclear ? 500.0f : volume.kind == TankDeath ? 60.0f : volume.kind == Muzzle ? 14.0f : 30.0f * volume.scale;
}
// A tank's volume past 1.75 s holds only its smoulder, so it gives its slab up before any new blast does.
// A gun's shot gives its slab up before anything.
int rankOf(const Volume &volume)
{
	if (volume.kind == Muzzle) return -2;
	if (volume.kind == TankDeath && volume.live && TheGameLogic
			&& 4 * ((int)TheGameLogic->getFrame() - volume.born) > 7 * LOGICFRAMES_PER_SECOND)
		return -1;
	return volume.kind == TankDeath || volume.kind == Structure ? 2 : volume.scale >= 2.0f || volume.kind == Launch ? 1 : 0;
}

// The structure Gather saw, this read or the one before: the one named, or with none named the one
// whose middle `where` is (a death FX goes off at the building's own position; a footprint test
// laid a building's fall on its neighbour).  None, null.
const Solid *structureAt(const Coord3D &where, ObjectID id)
{
	const Solid *found = nullptr;
	float nearest = 1e30f;
	for (const std::vector<Solid> *list : { &solids, &lastSolids })
		for (const Solid &solid : *list) {
			if (id != INVALID_ID) {
				if (solid.id == id) return &solid;
				continue;
			}
			const float dx = where.x - solid.x, dy = where.y - solid.y, d2 = dx * dx + dy * dy;
			const float middle = 0.3f * (solid.box ? (std::min)(solid.major, solid.minor) : solid.major) + 2.0f;
			if (d2 <= middle * middle && d2 < nearest) { nearest = d2; found = &solid; }
		}
	return found;
}

// The vehicle Gather saw nearest `where`, this logic frame or the one before: with `onTop`, one
// whose box `where` is over; else one standing within its own length of it (a death FX goes
// off at the hull's middle, give or take).  None, null.
const Hull *hullAt(const Coord3D &where, bool onTop)
{
	const Hull *found = nullptr;
	float nearest = 1e30f;
	for (const std::vector<Hull> *list : { &hulls, &lastHulls })
		for (const Hull &hull : *list) {
			const float dx = where.x - hull.x, dy = where.y - hull.y, d2 = dx * dx + dy * dy;
			const float c = std::cos(hull.heading), s = std::sin(hull.heading);
			const bool beside = onTop ? std::fabs(dx * c + dy * s) <= hull.major + 2.0f && std::fabs(-dx * s + dy * c) <= hull.minor + 2.0f
				: d2 < (hull.major + 8.0f) * (hull.major + 8.0f);
			if (beside && d2 < nearest) { nearest = d2; found = &hull; }
		}
	return found;
}

void pruneSets()
{
	// ponytail: both grow until 4096, then forget the systems that are gone
	if (takenSystems.size() > 4096)
		for (auto it = takenSystems.begin(); it != takenSystems.end();)
			it = TheParticleSystemManager->findParticleSystem((ParticleSystemID)*it) ? std::next(it) : takenSystems.erase(it);
	if (firstSeen.size() > 4096)
		for (auto it = firstSeen.begin(); it != firstSeen.end();)
			it = TheParticleSystemManager->findParticleSystem((ParticleSystemID)it->first) ? std::next(it) : firstSeen.erase(it);
}

// ---- The fires: a burning building's or wreck's stock fire systems drawn as a burning volume ----

void clearFires()
{
	fires.clear();
	fireTaken.clear();
	fireAnchors.clear();
	movingObjects.clear();
	fireOwners.clear();
	pendingOwners.clear();
	fireEmitting.clear();
	fireSurfaces.clear();
	surfacePicks.clear();
	surfacesFrame = -100000;
	anchorsFrame = -100000;
	state.lastFireStep = -1;
}

bool isFire(ParticleSystem *system)
{
	const ParticleSystemTemplate *tmplate = system->getTemplate();
	auto known = fireTemplates.find(tmplate);
	if (known != fireTemplates.end()) return known->second;
	const bool fire = VolumeFire::classOf(tmplate->getName().str()) == VolumeFire::Building;
	fireTemplates[tmplate] = fire;
	return fire;
}

// The fire slab a site has, else MaxVolumes.
unsigned fireSlotOf(int site)
{
	const TierShape &shape = TierShapes[2];
	for (unsigned slot = shape.first; slot < shape.first + shape.slabs; ++slot)
		if (volumes[slot].live && volumes[slot].kind == Fire && volumes[slot].site == site) return slot;
	return MaxVolumes;
}

// Its systems' billboards back, the next frame (the slab is going).
void releaseFireSystems(int site)
{
	for (auto it = fireTaken.begin(); it != fireTaken.end();)
		it = fires.siteOf((unsigned)*it) == site ? fireTaken.erase(it) : std::next(it);
}

// Where a site's middle is, and how far its camera is from it, for which sites keep a slab.
float fireDistance(int site)
{
	const VolumeFire::Site &s = fires.sites[site];
	Coord3D look = {};
	if (TheTacticalView) TheTacticalView->getPosition(&look);
	const float dx = (s.low[0] + s.high[0]) * 0.5f - look.x, dy = (s.low[1] + s.high[1]) * 0.5f - look.y;
	return dx * dx + dy * dy;
}

// Lay a site's slab over it: over its building's footprint and meshes when Gather saw the building,
// else round its fires, with room over the tallest for the flame and its smoke to rise twice as high
// again.  The cell is what that box needs, 1.5 to 8 units.
void placeFire(Volume &volume, int site, int now, int born)
{
	const VolumeFire::Site &s = fires.sites[site];
	const TierShape &shape = TierShapes[2];
	const unsigned owner = ownerOf(s.object);
	const Solid *building = owner ? structureAt(Coord3D(), (ObjectID)owner) : nullptr;
	const float flame = (std::max)(s.flame, 18.0f);
	// round its fires, not the whole building: the cells are what the box needs, and a War
	// Factory's footprint and meshes at 32 cells made cells of 6.5 units, wider than its flames
	// (layFloor still lays the building's meshes into it as solid)
	const float cx = (s.low[0] + s.high[0]) * 0.5f, cy = (s.low[1] + s.high[1]) * 0.5f;
	const float half = (std::max)(s.high[0] - s.low[0], s.high[1] - s.low[1]) * 0.5f + 6.0f;
	float floorZ = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(cx, cy, NULL) : s.low[2];
	float top = s.high[2];
	if (building) {
		floorZ = building->top - building->tall;
		top = (std::max)(top, building->top);
	}
	const float across = 2.0f * (half + 0.6f * flame);
	float cell = across / (shape.gx - 4.0f);
	cell = (std::max)(cell, ((std::max)(top, floorZ) - floorZ + 2.5f * flame + 6.0f) / (shape.gz - 8.0f));
	cell = (std::min)((std::max)(cell, 1.5f), 8.0f);
	volume = Volume();
	volume.kind = Fire;
	volume.live = volume.reset = true;
	volume.cell = cell;
	volume.site = site;
	volume.born = born;
	volume.placed = now;
	volume.life = 0x3fffffff;	// as long as its fires burn (gatherFires sets the end)
	const float side = shape.gx * cell;
	volume.origin[0] = cx - side * 0.5f;
	volume.origin[1] = cy - side * 0.5f;
	float lowest = floorZ;
	if (TheTerrainRenderObject)
		for (int j = 0; j <= 6; ++j)
			for (int i = 0; i <= 6; ++i)
				lowest = (std::min)(lowest, TheTerrainRenderObject->getHeightMapHeight(volume.origin[0] + side * i / 6.0f,
					volume.origin[1] + side * j / 6.0f, NULL));
	volume.origin[2] = (std::max)(lowest, floorZ - 3.0f * cell) - cell;
	volume.seed = unsigned(int(cx) * 73856093) ^ unsigned(int(cy) * 19349663) ^ unsigned(now * 83492791);
	if (fireClaims++ < 16)
		DEBUG_LOG(("VOLSMOKE: frame %d, fire slab for %s %u at (%.0f, %.0f), %.0f units across at %.1f a cell, flame %.0f\n", now,
			building ? "building" : s.object ? "object" : "loose fires", s.object, cx, cy, side, cell, flame));
}

// A fire system this frame (Takes, inside the particle loop: client state only).  Its flame, as its
// live particles draw it, goes to its site; the site gets a slab if one is free or held by a site
// farther from the camera; its billboards stop once the slab has run FireWarmSeconds under them.
bool takeFire(ParticleSystem *system)
{
	const int id = (int)system->getSystemID();
	const unsigned drawable = (unsigned)system->getAttachedDrawable();
	unsigned object = system->getAttachedObject() != INVALID_ID ? (unsigned)system->getAttachedObject() : 0u;
	if (!object && drawable != (unsigned)INVALID_DRAWABLE_ID) {
		// the art's fire bones: one site with the body's own fires once Gather has said whose drawable it is
		const unsigned key = DrawableKey | drawable;
		auto found = fireOwners.find(key);
		if (found == fireOwners.end()) {
			pendingOwners.insert(key);
			fireTaken.erase(id);
			return false;
		}
		object = found->second ? found->second : key;
	}
	const unsigned owner = ownerOf(object);
	// ponytail: a wreck's or a prop's small fire (CarJunk01, a STRUCTURE DEFENSIVE_WALL) keeps the
	// game's flame, which reads well at its size; since the tongues it burned out to nothing in its
	// 1.5-unit cells.  Give it its own turbulence scale if wrecks are to burn in the volume again.
	const Solid *own = owner ? structureAt(Coord3D(), (ObjectID)owner) : nullptr;
	const bool building = own && own->burns;
	if (!usable() || state.fireRefused || !TierShapes[2].slabs || !TheGameLogic || !building || movingObjects.count(owner)) {
		fireTaken.erase(id);
		return false;
	}
	const int now = (int)TheGameLogic->getFrame();
	Particle *young = nullptr;
	float highZ = -1e9f, biggest = 0;
	for (Particle *p = system->getFirstParticle(); p; p = p->m_systemNext) {
		young = p;	// the list runs oldest to youngest
		highZ = (std::max)(highZ, p->getPosition()->z);
		biggest = (std::max)(biggest, p->getSize());
	}
	int site = -1;
	if (young) {
		const Coord3D at = *young->getPosition();
		const ParticleSystemInfo::EmissionVolumeType type = system->getEmisionVolumeType();
		const float radius = type == ParticleSystemInfo::SPHERE ? system->m_emissionVolume.sphere.radius
			: type == ParticleSystemInfo::CYLINDER ? system->m_emissionVolume.cylinder.radius : 0.0f;
		const VolumeFire::Sample sample = { (unsigned)id, object, at.x, at.y, at.z, (highZ - at.z) + biggest * 0.5f,
			(std::max)(2.0f * radius + biggest * 0.3f, 2.0f) };
		site = fires.sample(sample, now);
	} else site = fires.siteOf((unsigned)id);	// between bursts: what it last drew
	if (site < 0) {
		fireTaken.erase(id);
		return false;
	}
	unsigned slot = fireSlotOf(site);
	// A new site waits two frames for its slab, so every bone of its building is in it.
	if (slot == MaxVolumes && now - fires.sites[site].born < 2) {
		fireTaken.erase(id);
		return false;
	}
	if (slot == MaxVolumes) {
		const TierShape &shape = TierShapes[2];
		for (unsigned s = shape.first; s < shape.first + shape.slabs && slot == MaxVolumes; ++s)
			if (!volumes[s].live) slot = s;
		if (slot == MaxVolumes) {
			// none free: the farthest site gives its slab up to one a quarter nearer
			float farthest = -1;
			for (unsigned s = shape.first; s < shape.first + shape.slabs; ++s) {
				const float d = fireDistance(volumes[s].site);
				if (d > farthest) { farthest = d; slot = s; }
			}
			if (slot == MaxVolumes || fireDistance(site) * 1.6f > farthest) {
				fireTaken.erase(id);
				return false;
			}
			releaseFireSystems(volumes[slot].site);
		}
		placeFire(volumes[slot], site, now, now);
	}
	// warm, and one of the emitters the last step laid fuel for (the 24 biggest, inside the box)
	if (now - volumes[slot].born < (int)(FireWarmSeconds * LOGICFRAMES_PER_SECOND) || !fireEmitting.count((unsigned)id)) {
		fireTaken.erase(id);
		return false;
	}
	fireTaken.insert(id);
	return true;
}

// A burning building's own smoke (SmolderingSmoke on its Smoke bones), this frame (Takes): hidden
// while a warm fire slab burns on the building, whose soot off the flame tips is the column now;
// the air's copy of it is gone too (stepAir skips fireTaken).  Given back the frame the slab goes.
bool hideFireSmoke(ParticleSystem *system)
{
	const int id = (int)system->getSystemID();
	if (std::strncmp(system->getTemplate()->getName().str(), "SmolderingSmoke", 15) || !TheGameLogic) return false;
	unsigned object = system->getAttachedObject() != INVALID_ID ? (unsigned)system->getAttachedObject() : 0u;
	if (!object && system->getAttachedDrawable() != INVALID_DRAWABLE_ID) {
		auto found = fireOwners.find(DrawableKey | (unsigned)system->getAttachedDrawable());
		if (found != fireOwners.end()) object = found->second ? found->second : found->first;
	}
	const int now = (int)TheGameLogic->getFrame();
	const TierShape &shape = TierShapes[2];
	bool hide = false;
	for (unsigned slot = shape.first; object && slot < shape.first + shape.slabs && !hide; ++slot) {
		const Volume &volume = volumes[slot];
		hide = volume.live && volume.kind == Fire && volume.site >= 0 && volume.site < (int)fires.sites.size()
			&& fires.sites[volume.site].object == object && fires.sites[volume.site].fuelEnd < 0
			&& now - volume.born >= (int)(FireWarmSeconds * LOGICFRAMES_PER_SECOND);
		// and only while it burns some of the site's fuel: a slab whose every system kept its billboard
		// has no soot either, and the building would smoke not at all
		bool burning = false;
		for (auto it = fireEmitting.begin(); hide && !burning && it != fireEmitting.end(); ++it)
			burning = fires.siteOf(*it) == volume.site;
		hide = hide && burning;
	}
	if (hide) fireTaken.insert(id);
	else fireTaken.erase(id);
	return hide;
}

// Once a logic frame, outside the logic window (Gather): systems that are gone leave their sites, a
// site whose last one went burns out, a wreck that drives off gives its slab up, and every fire
// slab's end follows its site's.
void gatherFires(int now)
{
	if (!TheParticleSystemManager) return;
	for (unsigned id : fires.systemIds())
		if (!TheParticleSystemManager->findParticleSystem((ParticleSystemID)id)) {
			fires.forget(id, now);
			fireTaken.erase((int)id);
		}
	// and the smoke systems hideFireSmoke hid, which no site knows
	for (auto it = fireTaken.begin(); it != fireTaken.end();)
		it = TheParticleSystemManager->findParticleSystem((ParticleSystemID)*it) ? std::next(it) : fireTaken.erase(it);
	// whose drawable each art-hung fire burns on
	if (fireOwners.size() > 4096) fireOwners.clear();	// ponytail: drawables gone long ago; asked again
	for (unsigned key : pendingOwners) {
		Drawable *drawable = TheGameClient ? TheGameClient->findDrawableByID((DrawableID)(key & ~DrawableKey)) : nullptr;
		const Object *object = drawable ? drawable->getObject() : nullptr;
		fireOwners[key] = object ? (unsigned)object->getID() : 0u;
	}
	pendingOwners.clear();
	if (now - anchorsFrame >= LOGICFRAMES_PER_SECOND || now < anchorsFrame) {
		anchorsFrame = now;
		std::unordered_map<unsigned, Coord3D> seen;
		for (const VolumeFire::Site &s : fires.sites) {
			const unsigned owner = ownerOf(s.object);
			if (!s.live || !owner || seen.count(owner)) continue;
			const Object *object = TheGameLogic->findObjectByID((ObjectID)owner);
			if (!object) continue;
			const Coord3D at = *object->getPosition();
			auto last = fireAnchors.find(owner);
			if (last != fireAnchors.end()) {
				const float dx = at.x - last->second.x, dy = at.y - last->second.y;
				if (dx * dx + dy * dy > 4.0f) movingObjects.insert(owner);
				else movingObjects.erase(owner);
			}
			seen[owner] = at;
		}
		fireAnchors.swap(seen);
	}
	// freed first, so a slab never outlives its site into the next one given the same index
	fires.prune(now, LOGICFRAMES_PER_SECOND);
	const TierShape &shape = TierShapes[2];
	for (unsigned slot = shape.first; slot < shape.first + shape.slabs; ++slot) {
		Volume &volume = volumes[slot];
		if (!volume.live || volume.kind != Fire) continue;
		const VolumeFire::Site *s = volume.site >= 0 && volume.site < (int)fires.sites.size() ? &fires.sites[volume.site] : nullptr;
		if (!s || !s->live || (ownerOf(s->object) && movingObjects.count(ownerOf(s->object)))) {
			releaseFireSystems(volume.site);
			volume.live = false;
			continue;
		}
		volume.life = s->fuelEnd >= 0 ? s->fuelEnd - volume.born + (int)(VolumeFire::BurnoutSeconds * LOGICFRAMES_PER_SECOND) : 0x3fffffff;
		// The game stopped drawing its fires (a stealthed building's): so does the volume.
		if (!fires.fresh(volume.site, now, LOGICFRAMES_PER_SECOND)) {
			releaseFireSystems(volume.site);
			volume.live = false;
			continue;
		}
		// A building catches fire bone by bone as it is hurt: a fire that has spread past its slab's
		// box gets a box over all of it, at most every two seconds and while the cell can still grow
		// (a carrier deck's fires spread wider than the widest box), and warms up again under its
		// billboards so the fire does not blink out while the new sim lights.
		float low[3], high[3];
		boxOf(volume, shape, low, high);
		const float rim = 2.0f * volume.cell;
		if (s->fuelEnd < 0 && volume.cell < 7.99f && now - volume.placed > 2 * LOGICFRAMES_PER_SECOND
				&& (s->low[0] < low[0] + rim || s->low[1] < low[1] + rim || s->high[0] > high[0] - rim
					|| s->high[1] > high[1] - rim || s->high[2] > high[2] - 4.0f * volume.cell)) {
			releaseFireSystems(volume.site);
			placeFire(volume, volume.site, now, now);
		}
	}
	// The meshes each burning building or wreck burns on: read for a new one at once, then looked at
	// every two seconds and read again only when its models changed (a damaged stage swaps them).
	const bool due = now - surfacesFrame >= 2 * LOGICFRAMES_PER_SECOND || now < surfacesFrame;
	if (due) surfacesFrame = now;
	std::unordered_set<unsigned> burning;
	for (unsigned slot = shape.first; slot < shape.first + shape.slabs; ++slot) {
		const Volume &volume = volumes[slot];
		if (!volume.live || volume.kind != Fire || volume.site < 0 || volume.site >= (int)fires.sites.size()) continue;
		const unsigned owner = ownerOf(fires.sites[volume.site].object);
		if (!owner) continue;
		burning.insert(owner);
		const bool known = fireSurfaces.count(owner) != 0;
		if (known && !due) continue;
		Object *object = TheGameLogic->findObjectByID((ObjectID)owner);
		Drawable *drawable = object ? object->getDrawable() : nullptr;
		if (!drawable) continue;
		const double started = DX11Resource_Milliseconds_Now();
		const unsigned key = surfaceKey(drawable);
		FireSurface &surface = fireSurfaces[owner];
		if (known && surface.key == key) continue;
		surface.key = key;
		readSurface(drawable, surface);
		volumes[slot].floorFrame = -100000;	// its floor is the new mesh's, the next step
		static unsigned surfacesLogged = 0;
		if (surfacesLogged++ < 12)
			DEBUG_LOG(("VOLSMOKE: frame %d, fire surface of object %u (%s): %u points in %.2f ms\n", now, owner,
				object->getTemplate()->getName().str(), (unsigned)surface.points.size(), DX11Resource_Milliseconds_Now() - started));
	}
	if (due)
		for (auto it = fireSurfaces.begin(); it != fireSurfaces.end();)
			it = burning.count(it->first) ? std::next(it) : fireSurfaces.erase(it);
	if (surfacePicks.size() > 4096) surfacePicks.clear();	// picked again on the next step
}

} // namespace

namespace VolumeSmoke {

bool Hides(ParticleSystem *system)
{
	if (!requested() || !system || TheGlobalData->m_classicGraphics) return false;
	// the air reads only the CPU's particles, so an air system's GPU overflow keeps drawing
	const int id = (int)system->getSystemID();
	return (takenSystems.count(id) != 0 && !airSystems.count(id)) || fireTaken.count(id) != 0;
}

// A tank gun's own FX systems (WeaponFX_GenericTankGunNoTracer and the rest), the Heroic ones too.
bool tankMuzzle(const char *name) { return std::strstr(name, "TankMuzzle") || std::strstr(name, "TankSmallMuzzle"); }

// A tank gun's shot, its flame system at the muzzle bone turned along the barrel: a volume on a free
// small-tier slab, at most MuzzleCap at once.  False, and the shot keeps the game's own flash, when
// none is free: a shot never takes a slab from a blast.
bool startMuzzle(ParticleSystem *system, const Coord3D &where, float gunScale, int now)
{
	const TierShape &shape = TierShapes[0];
	unsigned guns = 0, slot = shape.first + shape.slabs;
	for (unsigned i = shape.first; i < shape.first + shape.slabs; ++i) {
		if (volumes[i].live) guns += volumes[i].kind == Muzzle ? 1 : 0;
		else if (slot == shape.first + shape.slabs) slot = i;
	}
	if (slot == shape.first + shape.slabs || guns >= MuzzleCap[level]) return false;
	// a gun high off the ground (the Spectre's howitzer fires the same FX from its orbit) would blow its
	// shot outside the box and draw nothing in place of its flash
	const Real ground = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(where.x, where.y, NULL) : where.z - 10.0f;
	if (where.z - ground > 25.0f) return false;
	// the barrel's aim, the muzzle bone's x axis (W3DModelDraw::handleWeaponFireFX)
	const Matrix3D &bone = *system->getLocalTransform();
	float aim[3] = { bone[0][0], bone[1][0], bone[2][0] };
	const float length = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
	const float flat = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1]);
	if (length < 1e-3f || flat < 0.5f * length) return false;	// not a barrel's axis (a gun FX turned on its side)
	for (float &axis : aim) axis /= length;
	// the tank firing: its muzzle stands a barrel's length ahead of its middle (29 units on a Crusader,
	// past hullAt's reach), so asked from 15 units back along the barrel
	const float back = 15.0f / (flat / length);
	const Coord3D middle = { where.x - aim[0] * back, where.y - aim[1] * back, where.z };
	const Hull *hull = hullAt(middle, false);
	// an Overlord's gun throws a bigger blast than a Crusader's, by its hull's half length (15 the Crusader's)
	const float scale = gunScale * (std::min)((std::max)((hull ? hull->major : 15.0f) / 15.0f, 0.85f), 1.3f);
	Volume &volume = volumes[slot];
	volume = Volume();
	volume.kind = Muzzle;
	volume.live = true;
	volume.reset = true;
	volume.scale = scale;
	volume.cell = cellOf(Muzzle, scale);
	const float cell = volume.cell;
	// the box a little ahead of the gun, round the fireball and the dust in front of the tank
	const float fx = aim[0] / (flat / length), fy = aim[1] / (flat / length);
	volume.origin[0] = where.x + fx * 8.0f * cell - shape.gx * cell * 0.5f;
	volume.origin[1] = where.y + fy * 8.0f * cell - shape.gy * cell * 0.5f;
	volume.origin[2] = (std::min)(where.z, (Real)ground) - 4.0f * cell;
	for (unsigned i = 0; i < 3; ++i) volume.blast[i] = ((&where.x)[i] - volume.origin[i]) / cell;
	volume.ground = ((float)ground - volume.origin[2]) / cell;
	for (unsigned i = 0; i < 3; ++i) volume.hull[i] = aim[i];
	volume.hull[3] = (hull ? hull->major : 15.0f) / cell;
	volume.hull[4] = (std::max)(where.z - (float)ground, 0.0f) / cell;
	volume.seed = unsigned(int(where.x) * 73856093) ^ unsigned(int(where.y) * 19349663) ^ unsigned(now * 83492791);
	volume.born = now;
	volume.life = int(MuzzleLife * LOGICFRAMES_PER_SECOND);
	pruneSets();
	takenSystems.insert((int)system->getSystemID());
	gunSystems[slot] = { (int)system->getSystemID() };
	static unsigned gunsLogged = 0;
	if (gunsLogged++ < 8)
		DEBUG_LOG(("VOLSMOKE: frame %d, gun volume %u for '%s' at (%.0f, %.0f, %.0f), aim (%.2f, %.2f, %.2f), %.2f a cell, %s\n", now, slot,
			system->getTemplate()->getName().str(), where.x, where.y, where.z, aim[0], aim[1], aim[2], cell, hull ? "hull found" : "no hull"));
	return true;
}

bool Takes(ParticleSystem *system)
{
	if (!requested() || !system || TheGlobalData->m_classicGraphics) return false;
	// A system that starts a blast front of its own on first sight, whatever it draws as: the Scud's
	// warheads (all additive, so they start no volume), the stock shock rings, a nuke's flare even when
	// it joins the nuke before it.  A volume's own front is added with its volume, and merges with this.
	{
		static std::unordered_map<const ParticleSystemTemplate *, const VolumeShock::ShockClass *> frontTemplates;
		const ParticleSystemTemplate *tmplate = system->getTemplate();
		auto known = frontTemplates.find(tmplate);
		if (known == frontTemplates.end()) known = frontTemplates.emplace(tmplate, VolumeShock::frontOf(tmplate->getName().str())).first;
		// (a system hung on an object or a drawable has a local position, so only loose ones)
		if (known->second && TheGameLogic && usable() && system->getAttachedObject() == INVALID_ID
				&& system->getAttachedDrawable() == INVALID_DRAWABLE_ID
				&& firstSeen.emplace((int)system->getSystemID(), (int)TheGameLogic->getFrame()).second) {
			Coord3D where;
			system->getPosition(&where);
			addShock(where, *known->second, (int)TheGameLogic->getFrame());
		}
	}
	// a building's or a wreck's fire burns in a fire slab, or keeps its billboards; never the air's
	if (isFire(system)) return takeFire(system);
	if (hideFireSmoke(system)) return true;
	const int id = (int)system->getSystemID();
	// A shell striking armour: its smoke, turned away from the attacker, says where and from which
	// side; Gather throws the sparks (ReforgedTankHitSparks) off the hull there, outside this loop.
	// (Direct3D 11 only, as every volume is; a bomb truck's disguise plays it unturned, and throws none)
	if (TheGameLogic && !std::strcmp(system->getTemplate()->getName().str(), "TankStruckSmoke") && usable()
			&& firstSeen.emplace(id, (int)TheGameLogic->getFrame()).second) {
		const Matrix3D &turn = *system->getLocalTransform();
		PendingHit hit;
		system->getPosition(&hit.at);
		hit.along[0] = turn[0][0];
		hit.along[1] = turn[1][0];
		if (pendingHits.size() < 64 && !(hit.along[0] == 1.0f && hit.along[1] == 0.0f)) {
			pendingHits.push_back(hit);
			ownedHits.push_back({ hit.at, (int)TheGameLogic->getFrame() });
		}
	}
	// The game's own sparks and lens flare of a hit whose sparks we throw, and of the shell that made
	// it (TankStruckSparks, the Lenzflares, in the same frame or the next and within 25 units: both go
	// off in the logic frame that struck): stacked on ours they burst over the turret and all round the
	// tank as fireworks.  ponytail: by place and frame, not by victim, so a machine gun's hit (the same
	// template) on a tank parked within 25 units in that frame loses its sparks too; key on the victim
	// if that shows.
	const char *own0 = system->getTemplate()->getName().str();
	if (TheGameLogic && !ownedHits.empty() && (!std::strcmp(own0, "TankStruckSparks") || std::strstr(own0, "TankStruckLenzflare"))) {
		const int seen = firstSeen.emplace(id, (int)TheGameLogic->getFrame()).first->second;
		Coord3D where;
		system->getPosition(&where);
		for (const OwnedHit &hit : ownedHits) {
			const float dx = where.x - hit.at.x, dy = where.y - hit.at.y;
			if (std::abs(seen - hit.frame) > 1 || dx * dx + dy * dy > 25.0f * 25.0f) continue;
			pruneSets();
			takenSystems.insert(id);
			return true;
		}
	}
	if (takenSystems.count(id)) return !airOutside.count(id);
	// A rocket's glow and its launch flash are the air's nozzle flame now (gatherNozzles): the lens
	// flare on its exhaust (MissileLenzflare and the rest) burned as two orange dots fifty pixels
	// behind the rocket, and the launch flash (MissileBackblast, the Stinger's and the TOW's) as a
	// yellow-white blob as big as the soldier.
	{
		const char *name = system->getTemplate()->getName().str();
		const bool flare = std::strstr(name, "MissileLenzflare") || std::strstr(name, "MissileExhaustLenzflare");
		const bool flash = std::strstr(name, "MissileBackblast") && !std::strstr(name, "Clouds");
		// and EA's fire line, the additive streak behind a rocket (MissileExhaust and its kin): with
		// its points thrown sideways each frame it was the bright white rope with the sine wiggle; the
		// air's grey ribbon, fed from it and laid straight, is the trail now
		const bool fireLine = system->isUsingStreak() && system->getShaderType() == ParticleSystemInfo::ADDITIVE
			&& std::strstr(name, "MissileExhaust") && !std::strstr(name, "Scud") && !std::strstr(name, "Tomahawk") && !std::strstr(name, "Neutron");
		// and the white ribbon fed from it, whose points it throws off the line (ReforgedRocketTrail
		// rides the rocket in its place)
		const bool ribbon = !std::strcmp(name, "MissileExhaustTrail");
		if ((flare || flash || fireLine || ribbon) && usable()) {
			pruneSets();
			takenSystems.insert(id);
			return true;
		}
	}
	// A blast is told from loose billboard systems; trails, streaks, a building's smoke and every
	// other smoke or dust go to the air.
	// A loose ground-aligned system is let as far as a tank's blast: the stock ring a tank's death
	// lays on the ground (MammothTankExplosionShockwave), which the photo has none of.
	// The smoke trail behind a plate or a shard thrown off a dying tank stood as a thick grey arc
	// over the plume, where the photo has a few faint straight streaks at most: one that starts
	// beside a tank's blast, in its first half second, is hidden with the blast's own systems.
	// Only trails on thrown scrap (hung on a debris object) and the vehicle shrapnel's; a shell's
	// shard trails landing beside the tank keep theirs.  Its own position is local to the plate it
	// rides on, so its first particle says where it is.
	const bool trail = system->getAttachedObject() != INVALID_ID || std::strstr(system->getTemplate()->getName().str(), "Shrapnel");
	Particle *first = system->isUsingStreak() && trail ? system->getFirstParticle() : nullptr;
	if (first && airLookOf(system->getTemplate(), system).scrap && TheGameLogic) {
		const int now = (int)TheGameLogic->getFrame();
		const int seen = firstSeen.emplace(id, now).first->second;
		const Coord3D where = *first->getPosition();
		for (const Volume &volume : volumes) {
			if (!volume.live || volume.kind != TankDeath || seen < volume.born - 10 || seen > volume.born + 30) continue;
			const float dx = where.x - (volume.origin[0] + (volume.blast[0] + 0.5f) * volume.cell);
			const float dy = where.y - (volume.origin[1] + (volume.blast[1] + 0.5f) * volume.cell);
			if (dx * dx + dy * dy > joinRadius(volume) * joinRadius(volume)) continue;
			pruneSets();
			takenSystems.insert(id);
			return true;
		}
	}
	// A building's damaged stage blows out of systems hung on the building itself (TransitionDamageFX):
	// those are taken as if they lay loose at its middle, once the player whose view is drawn has
	// seen the building (Gather's list), so a blast never outlines an unscouted base.
	const char *own = system->getTemplate()->getName().str();
	const bool structural = !std::strncmp(own, "Structure", 9) || std::strstr(own, "ReforgedStructure")
		|| std::strstr(own, "HotCoreStructure");
	const Solid *onBuilding = structural && system->getAttachedObject() != INVALID_ID && !system->isSystemForever()
		&& !system->isUsingStreak() ? structureAt(Coord3D(), system->getAttachedObject()) : nullptr;
	const bool loose = (!system->isUsingStreak() && system->getVolumeParticleDepth() <= 1
		&& system->getAttachedObject() == INVALID_ID && system->getAttachedDrawable() == INVALID_DRAWABLE_ID
		&& system->getPriority() != AREA_EFFECT) || onBuilding;
	const bool flat = loose && !system->shouldBillboard();
	// and a nuke's own flat rings (NukeRing's orange fire disc on the ground, its blast wave) as
	// far as its blast: under the volume's fireball the ring lay round it as a flat orange plate;
	// and a building's flat shock rings as far as its blast (the owner wants no ring)
	const bool nukeFlat = flat && std::strstr(own, "Nuke") && !std::strstr(own, "NukeCannon");
	const bool structFlat = flat && structural;
	// a tank gun's FX go to its shot's volume, its flat ring on the ground (TankMuzzleWave) too
	const bool gun = tankMuzzle(own), gunFlat = flat && gun;
	// A building's fall's white shard burst (ReforgedBlastShards* and the puffs on each shard) stood
	// over its fireball as a firework of fuzzy dots: hidden for a fall the volume draws, while it is
	// young and beside it.  Elsewhere, a tank's say, the shards keep their look.
	if (std::strstr(own, "BlastShard") && TheGameLogic) {
		Coord3D where;
		system->getPosition(&where);
		if (Particle *first = system->getFirstParticle()) where = *first->getPosition();
		const int now = (int)TheGameLogic->getFrame();
		for (const Volume &volume : volumes) {
			if (!volume.live || volume.kind != Structure || volume.standing || now - volume.born > (int)(1.5f * LOGICFRAMES_PER_SECOND)) continue;
			const float dx = where.x - (volume.origin[0] + (volume.blast[0] + 0.5f) * volume.cell);
			const float dy = where.y - (volume.origin[1] + (volume.blast[1] + 0.5f) * volume.cell);
			if (dx * dx + dy * dy > joinRadius(volume) * joinRadius(volume)) continue;
			pruneSets();
			takenSystems.insert(id);
			return true;
		}
	}
	if (!loose || (flat && system->getPriority() != DEATH_EXPLOSION && !nukeFlat && !structFlat && !gunFlat)) return takeAir(system);
	// flying bits stay the game's own here too: a blast that took them drew nothing in their place
	if (airLookOf(system->getTemplate(), system).scrap && !gun) return false;
	const int now = (int)TheGameLogic->getFrame();
	const int seen = firstSeen.emplace(id, now).first->second;
	Coord3D where;
	system->getPosition(&where);
	if (onBuilding) where = { onBuilding->x, onBuilding->y, onBuilding->top - onBuilding->tall };	// its own is local to the building

	// Another system of a blast already taken: it joins it if it started beside it, at its start.
	// A shell's takes smoke, dust and dirt and leaves the fire cards; a tank's and a nuclear
	// blast's take everything, as the sim draws their fire.  A system that starts a blast of its own joins only one of its own kind
	// (the nuke's several mushroom systems, a tank's two death smokes), so a tank dying under the
	// shell that killed it still gets a tank's smoke.  A tank's stays open for a second and a half:
	// the Overlord's and the Paladin's wrecks set off a second death smoke 35 to 40 frames on,
	// which started a second tank blast in the first one's fire and burned a white ball on the hull.
	const TemplateKind kind = kindOfTemplate(system->getTemplate(), system);
	const bool alpha = system->getShaderType() == ParticleSystemInfo::ALPHA && !system->getTemplate()->isEmissive();
	// A building's blast lies on its footprint.  A death smoke that goes off inside a footprint with
	// no vehicle there is the building's too (FX_SmallStructureDeath fires the Mammoth's).
	Kind kindNow = kind.kind;
	const Solid *building = onBuilding;
	// (only the smoke that FX fires: a jet or a tank dying beside a building is not its fall)
	const bool mammoth = !std::strcmp(own, "MammothTankSubExplosionSmoke");
	if (kind.trigger && !building && (kindNow == Structure || (kindNow == TankDeath && mammoth && !hullAt(where, false))))
		building = structureAt(where, INVALID_ID);
	if (kindNow == TankDeath && building && !onBuilding) kindNow = Structure;
	for (const Volume &volume : volumes) {
		if (!volume.live || volume.kind == Fire || seen < volume.born - 10
				|| seen > volume.born + (volume.kind == Nuclear ? 180 : volume.kind == TankDeath || volume.kind == Structure ? 90 : 30)) continue;	// a nuke's stem comes 2 s after its flare
		// a gun's shot takes its own FX only, and they join nothing else (a tank firing beside a dead
		// one keeps its shot)
		if ((volume.kind == Muzzle) != gun) continue;
		if (flat && !(volume.kind == TankDeath && system->getPriority() == DEATH_EXPLOSION) && !(nukeFlat && volume.kind == Nuclear)
			&& !(structFlat && volume.kind == Structure) && !(gunFlat && volume.kind == Muzzle)) continue;
		// A building's blast takes its own FX's systems, and the fire cards (not the smoke) of the
		// shells landing on it in its first second and a half: an Overlord's BuggyMissileExplosion
		// fireballs floated over the cloud as separate flat sprites.
		if (volume.kind == Structure && !structural && system->getPriority() != DEATH_EXPLOSION && (alpha || seen > volume.born + 45)) continue;
		if ((volume.kind == Shell || volume.kind == Airburst || volume.kind == Launch) && !alpha) continue;	// a launch's flare is the nozzle's
		// a nuclear blast's fire cards are its own FX's only, not a shell's that lands in its reach
		const char *joiner = system->getTemplate()->getName().str();
		if (volume.kind == Nuclear && !alpha && !std::strstr(joiner, "Nuke") && !std::strstr(joiner, "MOAB")) continue;
		// Sparks stay the game's own billboards, a dead tank's own (ReforgedTankDeathSparks) and the
		// shells' still hitting it (TankStruckSparks): taken, the fire swallowed the only hot sparks
		// near it.  A building's fall nearby would take them too.
		if ((volume.kind == TankDeath || volume.kind == Structure) && !alpha && std::strstr(joiner, "Spark")) continue;
		if (system->isSystemForever()) continue;
		// a shot's flame starts its own volume: the Overlord's second barrel fires 18 frames after the
		// first, a few units from it, and joined to the first shot's it drew nothing
		if (kind.trigger && kindNow == Muzzle) continue;
		if (kind.trigger && (kindNow == TankDeath) != (volume.kind == TankDeath)) continue;
		if (kind.trigger && (kindNow == Nuclear) != (volume.kind == Nuclear)) continue;
		if (kind.trigger && (kindNow == Structure) != (volume.kind == Structure)) continue;
		if (kind.trigger && (kindNow == Launch) != (volume.kind == Launch)) continue;
		// A building falling a second after its last damaged stage blew is a blast of its own: its
		// loose death FX go to the fall, the stage's systems hung on the building to the stage.  The
		// rubble stage's systems, hung on the building in the frame its death FX goes off, join the
		// fall (and a stage made from them that frame gives way to it, below).
		if (volume.kind == Structure && volume.standing != (onBuilding != nullptr)
				&& !(onBuilding && !volume.standing && volume.building == onBuilding->id && std::abs(seen - volume.born) <= 30)) continue;
		const float bx = volume.origin[0] + (volume.blast[0] + 0.5f) * volume.cell;
		const float by = volume.origin[1] + (volume.blast[1] + 0.5f) * volume.cell;
		const float dx = where.x - bx, dy = where.y - by, reach = joinRadius(volume);
		if (dx * dx + dy * dy > reach * reach) continue;
		pruneSets();
		takenSystems.insert(id);
		if (volume.kind == Muzzle) gunSystems[unsigned(&volume - volumes)].push_back(id);
		if (volume.kind == Nuclear && kind.trigger) adoptFront(where, int(&volume - volumes), now);
		++joinedCount;
		return true;
	}

	if (flat || !kind.trigger || system->isSystemForever() || seen < now - 5 || !usable()) return takeAir(system);
	// (on its first frame only: started a few frames late, after the game's own flash had drawn, a
	// shot flashed twice)
	if (kindNow == Muzzle) return seen == now && startMuzzle(system, where, kind.scale, now);

	const Real ground = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(where.x, where.y, NULL) : where.z;
	Kind type = kindNow;
	// A shell bursting high, or on a vehicle's hull rather than the ground, throws no dirt: on a
	// tank the dirt fountain stood over it as brown fingers.
	const bool onHull = type == Shell && hullAt(where, true);
	if (type == Shell && (where.z - ground > 25.0f || onHull)) type = Airburst;
	const unsigned tier = type == Nuclear ? 1 : 0;
	const TierShape &shape = TierShapes[tier];
	Volume fresh;
	fresh.kind = type;
	fresh.onHull = onHull;
	fresh.scale = kind.scale;
	// a Scud Storm's silo throws the big cloud; a Scud Launcher's or a Tomahawk's truck (the same
	// ScudStormIgnitionCloud) a smaller one
	if (type == Launch && kind.scale < 1.2f && !structureAt(where, INVALID_ID)) fresh.scale = 0.6f;
	const int rank = rankOf(fresh);

	// A free slab, else the one of the lowest rank whose blast is oldest; a new blast of a lower
	// rank than every live one is dropped instead.  Either way no billboards.
	unsigned slot = shape.first;
	while (slot < shape.first + shape.slabs && volumes[slot].live) ++slot;
	if (slot == shape.first + shape.slabs) {
		slot = shape.first;
		for (unsigned i = shape.first + 1; i < shape.first + shape.slabs; ++i)
			if (rankOf(volumes[i]) < rankOf(volumes[slot])
					|| (rankOf(volumes[i]) == rankOf(volumes[slot]) && volumes[i].born < volumes[slot].born))
				slot = i;
		++droppedCount;
		if (rankOf(volumes[slot]) > rank) {
			if (type == Structure) DEBUG_LOG(("VOLSMOKE: frame %d, a structure's blast dropped: every slab held a higher rank\n", now));
			pruneSets();
			takenSystems.insert(id);
			return true;
		}
	}

	Volume &volume = volumes[slot];
	// a shot whose slab this takes gets its billboards back, for what is left of them
	if (volume.live && volume.kind == Muzzle) {
		auto shot = gunSystems.find(slot);
		if (shot != gunSystems.end()) {
			for (int taken : shot->second) takenSystems.erase(taken);
			gunSystems.erase(shot);
		}
	}
	volume = fresh;
	volume.live = true;
	volume.reset = true;
	volume.cell = cellOf(type, volume.scale);
	// A building's box is about two and a half footprints across, so the cell goes with its size: 3
	// units for an American power plant, about 6.5 for a War Factory or a Particle Cannon.  None
	// seen (a building this player has not scouted), a 40 unit one at a hashed heading.
	const float extent = building ? (building->box ? (std::max)(building->major, building->minor) : building->major) : 40.0f;
	if (type == Structure) {
		// (at 0.105 and 8 the box held a ground roll to one and a half footprints)
		volume.cell = (std::min)((std::max)(extent * 0.125f, 3.0f), 9.5f);
		if (building) where = { building->x, building->y, building->top - building->tall };
	}
	const float sideX = shape.gx * volume.cell, sideY = shape.gy * volume.cell;
	volume.origin[0] = where.x - sideX * 0.5f;
	volume.origin[1] = where.y - sideY * 0.5f;
	// The box's floor under the lowest ground it covers, so smoke can run down into a valley; the
	// terrain itself is laid into the grid as solid (layFloor).
	float lowest = (std::min)(where.z, ground);
	if (TheTerrainRenderObject)
		for (int j = 0; j <= 6; ++j)
			for (int i = 0; i <= 6; ++i)
				lowest = (std::min)(lowest, TheTerrainRenderObject->getHeightMapHeight(
					volume.origin[0] + sideX * i / 6.0f, volume.origin[1] + sideY * j / 6.0f, NULL));
	// no more than four cells under the blast's own ground, so a box over a valley keeps headroom
	volume.origin[2] = (std::max)(lowest, (std::min)(where.z, ground) - 3.0f * volume.cell) - volume.cell;
	volume.blast[0] = (shape.gx - 1) * 0.5f;
	volume.blast[1] = (shape.gy - 1) * 0.5f;
	// at the blast's height, but never under the ground: the ground turns its push
	const float groundCell = (ground - volume.origin[2]) / volume.cell;
	volume.ground = groundCell;
	volume.blast[2] = type == Airburst ? (where.z - volume.origin[2]) / volume.cell - 0.5f : groundCell + 0.5f;
	volume.seed = unsigned(int(where.x) * 73856093) ^ unsigned(int(where.y) * 19349663) ^ unsigned(now * 83492791);
	// A tank's blast lies on its hull: the vehicle standing where its death FX went off, from the
	// list Gather read.  None found (hidden, or not a vehicle), a mid-sized tank at a hashed heading.
	if (type == TankDeath) {
		const Hull *found = hullAt(where, false);
		const float heading = found ? found->heading : hashUnit(volume.seed, 7) * 6.2831853f;
		const float cell = volume.cell;
		volume.hull[0] = std::cos(heading);
		volume.hull[1] = std::sin(heading);
		// kept inside what the 48-cell grid holds once the flame has rolled off the ends
		volume.hull[2] = (std::min)((std::max)((found ? found->major : 15.0f) / cell, 2.0f), 12.0f);
		volume.hull[3] = (std::min)((std::max)((found ? found->minor : 10.0f) / cell, 1.5f), 8.0f);
		volume.hull[4] = (std::min)((std::max)((found ? found->tall : 10.0f) / cell, 1.5f), 8.0f);
		static unsigned hullsLogged = 0;
		if (hullsLogged++ < 8)
			DEBUG_LOG(("VOLSMOKE: frame %d, tank hull %s, %.0f by %.0f by %.0f units, heading %.2f\n", now,
				found ? "found" : "not found", volume.hull[2] * 2 * cell, volume.hull[3] * 2 * cell, volume.hull[4] * cell, heading));
		// its sparks, started by Gather outside the particle loop this runs in
		pendingSparks.push_back({ where.x, where.y, (std::min)(where.z, ground) + volume.hull[4] * cell * 0.8f });
	}
	if (type == Structure) {
		const float cell = volume.cell, heading = building ? building->angle : hashUnit(volume.seed, 7) * 6.2831853f;
		volume.hull[0] = std::cos(heading);
		volume.hull[1] = std::sin(heading);
		volume.hull[2] = (std::min)((std::max)((building ? building->major : 40.0f) / cell, 2.0f), 14.0f);
		volume.hull[3] = (std::min)((std::max)((building ? (building->box ? building->minor : building->major) : 40.0f) / cell, 2.0f), 14.0f);
		volume.hull[4] = (std::min)((std::max)((building ? building->tall : 30.0f) / cell, 1.5f), 14.0f);
		volume.building = building ? building->id : INVALID_ID;
		volume.standing = onBuilding != nullptr;
		volume.partCount = building ? building->partCount : 0;
		for (unsigned i = 0; i < volume.partCount; ++i) {
			const Part &p = building->parts[i];
			volume.parts[i] = { p.along / cell, p.across / cell, (std::max)(p.halfAlong / cell, 0.5f), (std::max)(p.halfAcross / cell, 0.5f),
				(std::max)(p.bottom, 0.0f) / cell, (std::min)(p.top / cell, TierShapes[0].gz - 12.0f) };
		}
		// a stage the rubble systems made in the same moment is this fall's
		if (!volume.standing && building)
			for (Volume &other : volumes)
				if (&other != &volume && other.live && other.kind == Structure && other.standing && other.building == building->id
						&& now - other.born <= 2)
					other.live = false;
		static unsigned structuresLogged = 0;
		if (structuresLogged++ < 24 || !volume.standing)	// every fall, so a film can tell a missed one
			DEBUG_LOG(("VOLSMOKE: frame %d, structure %s for '%s', %.0f by %.0f by %.0f units at %.1f a cell, %s, %u meshes, the first %.0f by %.0f, %.0f to %.0f up\n", now,
				building ? "found" : "not found", own, volume.hull[2] * 2 * cell, volume.hull[3] * 2 * cell, volume.hull[4] * cell, cell,
				volume.standing ? "a damaged stage" : "its fall", volume.partCount,
				volume.partCount ? volume.parts[0].halfAlong * 2 * cell : 0.0f, volume.partCount ? volume.parts[0].halfAcross * 2 * cell : 0.0f,
				volume.partCount ? volume.parts[0].bottom * cell : 0.0f, volume.partCount ? volume.parts[0].top * cell : 0.0f));
	}
	volume.born = now;
	volume.life = int((type == Structure && volume.standing ? 2.0f : lifeOf(type, volume.scale)) * LOGICFRAMES_PER_SECOND);
	// its front through the air round it, in world units a second.  A tank's has none: a sphere
	// pushing out from the hull's middle blew its own fire into a dome and the dust round it into
	// rings, where a real one bursts out along the hull and up (schedule).
	if (type == Nuclear) {
		addShock(where, VolumeShock::Nuclear, now, (int)slot);
		// The flash: a pulse of the game's own dynamic light, so the ground, the buildings and the
		// smoke round the blast light up white and fall back over 0.3 s.  A
		// display light, like an FX list's LightPulse: nothing of it reaches the logic.  Not for the
		// MOAB, a fuel-air bomb, whose volume starts on its stem half a second after its blast.
		if (TheDisplay && !std::strstr(own, "MOAB")) {
			const Coord3D at = { where.x, where.y, (Real)ground + 60.0f };
			// well over 1, so it still shows on sunlit ground, where the light is clamped
			const RGBColor white = { 2.0f, 1.9f, 1.7f };
			TheDisplay->createLightPulse(&at, &white, 250.0f, 450.0f, 0, LOGICFRAMES_PER_SECOND * 3 / 10);
		}
	}
	else if (type == Structure) addShock(where, volume.standing ? VolumeShock::StructureStage : VolumeShock::StructureFall, now, (int)slot);
	else if (type != TankDeath && type != Launch)
		addShock(where, kind.scale >= 3.0f ? VolumeShock::NukeCannon : VolumeShock::shell(kind.scale), now, (int)slot);
	pruneSets();
	takenSystems.insert(id);
	if (claimedCount++ < 8)
		DEBUG_LOG(("VOLSMOKE: frame %d, %s volume %u for '%s' at (%.0f, %.0f, %.0f), %.0f units wide\n", now,
			type == TankDeath ? "tank" : type == Shell ? "shell" : type == Airburst ? "airburst" : type == Structure ? "structure"
				: type == Launch ? "launch" : "nuclear", slot,
			system->getTemplate()->getName().str(), where.x, where.y, where.z, sideX));
	return true;
}

// A building's fall throws its stock debris (OCL_LargeStructureDebris and the rest, GenericDebris
// objects), which flies with no trail: each chunk seen beside a fall in its first DebrisDustSeconds
// gets a short dust trail of its own, a client particle system riding its drawable.  Only with the
// setting on (Gather), so Off keeps the game's own look; nothing goes back to the logic.
void attachDebrisDust(int now)
{
	if (!TheGameClient || !TheParticleSystemManager) return;
	static std::unordered_set<DrawableID> dusted;
	const Volume *falls[MaxVolumes];
	unsigned fallCount = 0;
	for (const Volume &volume : volumes)
		if (volume.live && volume.kind == Structure && !volume.standing
				&& now - volume.born <= (int)(DebrisDustSeconds * LOGICFRAMES_PER_SECOND))
			falls[fallCount++] = &volume;
	if (!fallCount) {
		if (dusted.size() > 4096) dusted.clear();
		return;
	}
	const ParticleSystemTemplate *trail = TheParticleSystemManager->findTemplate("ReforgedStructureDebrisDust");
	if (!trail) return;
	for (Drawable *drawable = TheGameClient->firstDrawable(); drawable; drawable = drawable->getNextDrawable()) {
		// (not a tank's plates thrown beside a fall: they carry ReforgedDebrisTrail, a tank is no building)
		if (!drawable->getTemplate() || drawable->getTemplate()->getName() != "GenericDebris" || dusted.count(drawable->getID())
				|| drawable->getFullyObscuredByShroud() || hullAt(*drawable->getPosition(), false)) continue;
		const Coord3D *at = drawable->getPosition();
		for (unsigned f = 0; f < fallCount; ++f) {
			const Volume &volume = *falls[f];
			const float dx = at->x - (volume.origin[0] + (volume.blast[0] + 0.5f) * volume.cell);
			const float dy = at->y - (volume.origin[1] + (volume.blast[1] + 0.5f) * volume.cell);
			if (dx * dx + dy * dy > joinRadius(volume) * joinRadius(volume)) continue;
			dusted.insert(drawable->getID());
			if (ParticleSystem *system = TheParticleSystemManager->createParticleSystem(trail)) system->attachToDrawable(drawable);
			static unsigned dustLogged = 0;
			if (dustLogged++ < 4) DEBUG_LOG(("VOLSMOKE: frame %d, a dust trail on a fallen building's debris\n", now));
			break;
		}
	}
}

// The rockets and missiles in flight, read off their drawables once a logic frame: where the tail
// is and which way it points, for the air's nozzle flame (stepAir), which the stock lens flare drew
// fifty pixels behind the rocket where its exhaust ribbon starts.  A small rocket fired near the
// ground also gets a white backblast puff behind its launcher on its second frame, when its heading
// is known (the Tunnel Defender's has no launch FX at all, and the Tank Hunter's and the Missile
// Defender's is a yellow-white flash).  Client particles only; nothing goes back to the logic.
void gatherNozzles(int now)
{
	if (!TheGameClient) return;
	static std::unordered_map<DrawableID, std::pair<Coord3D, int>> last, next;
	next.clear();
	nozzles.clear();
	if (!TheParticleSystemManager) return;
	const ParticleSystemTemplate *puff = TheParticleSystemManager->findTemplate("ReforgedRocketBackblastPuff");
	const ParticleSystemTemplate *trail = TheParticleSystemManager->findTemplate("ReforgedRocketTrail");
	const ParticleSystemTemplate *flame = TheParticleSystemManager->findTemplate("ReforgedRocketFlame");
	for (Drawable *drawable = TheGameClient->firstDrawable(); drawable; drawable = drawable->getNextDrawable()) {
		if (!drawable->isKindOf(KINDOF_PROJECTILE) || !drawable->getTemplate()) continue;
		const char *name = drawable->getTemplate()->getName().str();
		if (!std::strstr(name, "Missile") && !std::strstr(name, "Missle") && !std::strstr(name, "Rocket")) continue;
		if (drawable->isDrawableEffectivelyHidden() || drawable->getFullyObscuredByShroud()) continue;
		const Coord3D at = *drawable->getPosition();
		auto seen = last.find(drawable->getID());
		const int frames = seen == last.end() ? 0 : seen->second.second + 1;
		next[drawable->getID()] = { at, frames };
		const bool big = std::strstr(name, "Scud") || std::strstr(name, "Neutron") || std::strstr(name, "Tomahawk") || std::strstr(name, "Nuke");
		// A small rocket's trail and flame are client billboards riding the rocket itself, after the
		// RPG-7 photo: a short bright flame at the tail and a thin grey wisp laid straight along its
		// path.  The air's cells, seven units across, could draw neither: a ribbon a unit wide laid
		// next to nothing, and the flame came out as an orange ball a rocket's length behind.
		if (seen == last.end()) {
			if (!big && trail)
				if (ParticleSystem *system = TheParticleSystemManager->createParticleSystem(trail)) system->attachToDrawable(drawable);
			continue;
		}
		const float dx = at.x - seen->second.first.x, dy = at.y - seen->second.first.y, dz = at.z - seen->second.first.z;
		const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (length < 0.5f) continue;
		const float ux = dx / length, uy = dy / length, uz = dz / length;
		if (big) nozzles.push_back({ at.x - ux * 8.0f, at.y - uy * 8.0f, at.z - uz * 8.0f, big, frames });
		// The flame is laid afresh each frame where the rocket is drawn: a system hung on the rocket
		// emits where the rocket stood when the particles last moved, a frame and a half behind the
		// drawn model, the gap EA's flare showed too.
		else if (flame) {
			const Coord3D where = { at.x + dx * RocketFlameLead, at.y + dy * RocketFlameLead, at.z + dz * RocketFlameLead };
			if (ParticleSystem *system = TheParticleSystemManager->createParticleSystem(flame)) system->setPosition(&where);
		}
		const float ground = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(at.x, at.y, NULL) : at.z;
		if (frames == 1 && !big && puff && at.z - ground < 40.0f)
			if (ParticleSystem *system = TheParticleSystemManager->createParticleSystem(puff)) {
				// behind the launcher, which is where the rocket was a frame ago less its tube
				const Coord3D where = { seen->second.first.x - ux * 10.0f, seen->second.first.y - uy * 10.0f, seen->second.first.z - uz * 10.0f };
				system->setPosition(&where);
			}
	}
	last.swap(next);
}

void Gather()
{
	if (!requested() || !TheGameLogic || TheGlobalData->m_classicGraphics || LogicThread::isInsideWindow()) return;
	const int now = (int)TheGameLogic->getFrame();
	static int gathered = -1;
	if (gathered == now) return;
	gathered = now;
	gatherSolids(now);	// every two seconds, so a blast that starts finds its buildings
	gatherFires(now);
	// every frame, idle or not, so a tank that dies now finds its hull; never where no volume can be
	if (Direct3D11_Present_Is_Enabled()) collectMovers(now);
	// A dead tank's hot sparks: client particles only, the client's random stream, no logic state.
	if (!pendingSparks.empty() && TheParticleSystemManager) {
		const ParticleSystemTemplate *sparks = TheParticleSystemManager->findTemplate("ReforgedTankDeathSparks");
		static unsigned sparksLogged = 0;
		if (sparksLogged++ < 4)
			DEBUG_LOG(("VOLSMOKE: frame %d, %u tank death spark burst(s), template %s\n", now, (unsigned)pendingSparks.size(),
				sparks ? "found" : "missing"));
		if (sparks)
			for (const Coord3D &at : pendingSparks)
				if (ParticleSystem *system = TheParticleSystemManager->createParticleSystem(sparks))
					system->setPosition(&at);
		pendingSparks.clear();
	}
	attachDebrisDust(now);
	gatherNozzles(now);
	ownedHits.erase(std::remove_if(ownedHits.begin(), ownedHits.end(), [now](const OwnedHit &hit) { return hit.frame < now - 8; }),
		ownedHits.end());
	// A shell striking armour: one dense burst of sparks out of the point where it struck, in a cone
	// round the face's outward normal (the system's x axis), over a white core.  The hit itself goes off
	// at the vehicle's middle, so the point is where the shot's line from the attacker meets the hull's
	// box.  Two systems a hit, plus a tail system on each of its 16 to 20 sparks for a quarter second.
	if (!pendingHits.empty() && TheParticleSystemManager) {
		const ParticleSystemTemplate *sparks = TheParticleSystemManager->findTemplate("ReforgedTankHitSparks");
		const ParticleSystemTemplate *core = TheParticleSystemManager->findTemplate("ReforgedTankHitSparkCore");
		static unsigned hitsLogged = 0;
		if (hitsLogged++ < 4)
			DEBUG_LOG(("VOLSMOKE: frame %d, %u armour hit spark shower(s), template %s\n", now, (unsigned)pendingHits.size(),
				sparks ? "found" : "missing"));
		if (sparks)
			for (const PendingHit &hit : pendingHits) {
				const float length = std::sqrt(hit.along[0] * hit.along[0] + hit.along[1] * hit.along[1]);
				if (length < 1e-3f) continue;
				const float dx = hit.along[0] / length, dy = hit.along[1] / length;
				Coord3D at = hit.at;
				float nx = -dx, ny = -dy;
				if (const Hull *hull = hullAt(hit.at, true)) {
					const float c = std::cos(hull->heading), s = std::sin(hull->heading);
					const float du = dx * c + dy * s, dv = -dx * s + dy * c;
					const float tu = std::fabs(du) > 1e-3f ? hull->major / std::fabs(du) : 1e30f;
					const float tv = std::fabs(dv) > 1e-3f ? hull->minor / std::fabs(dv) : 1e30f;
					const float reach = (std::min)(tu, tv);
					at.x = hull->x - dx * reach;
					at.y = hull->y - dy * reach;
					if (tu < tv) { nx = du > 0 ? -c : c; ny = du > 0 ? -s : s; }
					else { nx = dv > 0 ? s : -s; ny = dv > 0 ? -c : c; }
				} else {
					at.x -= dx * 6.0f;
					at.y -= dy * 6.0f;
				}
				Matrix3D turn(1);
				turn.Rotate_Z(std::atan2(ny, nx));
				for (const ParticleSystemTemplate *made : { sparks, core })
					if (made)
						if (ParticleSystem *system = TheParticleSystemManager->createParticleSystem(made)) {
							system->setLocalTransform(&turn);
							system->setPosition(&at);
						}
			}
		pendingHits.clear();
	}
}

void Render(const HazardCloud::Camera &camera)
{
	if (!requested() || !usable()) return;
	DX11BackendClass *backend = Direct3D11_Backend();
	DX11DeviceClass *device = Direct3D11_Device();
	// a mirror or the sun's map: the main view only
	ID3D11RenderTargetView *scene = backend->Scene_Target();
	if (!scene || !camera.width || !camera.height) return;
	// The camera's matrices, as HazardCloud's layers take them from it.
	float viewProjection[16], eyeRows[16];
	HazardCloud::multiply(camera.view, camera.projection, viewProjection);

	const int now = (int)TheGameLogic->getFrame();
	unsigned live = 0, slabs[3] = {}, fireLive = 0;
	for (unsigned slot = 0; slot < MaxVolumes; ++slot) {
		Volume &volume = volumes[slot];
		if (!volume.live) continue;
		if (now - volume.born > volume.life || now < volume.born) {
			if (volume.kind == Fire) releaseFireSystems(volume.site);
			volume.live = false;
			continue;
		}
		++live;
		fireLive += volume.kind == Fire ? 1 : 0;
		slabs[tierOf(slot)] = slabOf(slot) + 1;
		// a fire whose box, grown by its own size each way, is off the screen is not stepped
		if (volume.kind == Fire) {
			float low[3], high[3];
			boxOf(volume, TierShapes[2], low, high);
			for (unsigned i = 0; i < 3; ++i) {
				const float grow = high[i] - low[i];
				low[i] -= grow;
				high[i] += grow;
			}
			D3D11_RECT ignored;
			volume.frozen = !volume.reset && !screenRect(low, high, viewProjection, camera, ignored);
		}
	}
	// the air runs while it holds a system, and after, while its smoke can still be there
	const bool airActive = !airSystems.empty() || !airBoxes.empty() || !tallBoxes.empty();
	if (now - lastReport >= 10 * LOGICFRAMES_PER_SECOND && (live || claimedCount || airActive)) {
		lastReport = now;
		DEBUG_LOG(("VOLSMOKE: frame %d, %u volumes live, %u blasts, %u systems joined, %u dropped so far, %u draws; "
			"air %u systems (%u tall), %u particles fed, %u left out; %u units pushing, %u at most; %u fires burning, %u fire systems drawn\n",
			now, live, claimedCount, joinedCount, droppedCount, state.drawn, (unsigned)airSystems.size(), (unsigned)tallSystems.size(),
			airFed, airLeftOut, (unsigned)movers.size(), moverPeak, fireLive, (unsigned)fireTaken.size()));
	}
	if (!live && !airActive) {
		state.lastStepFrame = now;
		return;
	}

	const TimeOfDay time = TheGlobalData->m_timeOfDay;
	const GlobalData::TerrainLighting &sun = TheGlobalData->m_terrainObjectsLighting[time][0];
	float toward[3] = { -sun.lightPos.x, -sun.lightPos.y, -sun.lightPos.z };
	const float length = std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
	for (float &axis : toward) axis = length > 1e-4f ? axis / length : 0.0f;
	if (length <= 1e-4f) toward[2] = 1;
	const float seconds = (float)now / LOGICFRAMES_PER_SECOND;
	static int lightLogged = -1;
	if (lightLogged != (int)time) {
		lightLogged = (int)time;
		const float luma = 0.299f * (sun.diffuse.red + sun.ambient.red) + 0.587f * (sun.diffuse.green + sun.ambient.green)
			+ 0.114f * (sun.diffuse.blue + sun.ambient.blue);
		DEBUG_LOG(("VOLSMOKE: the smoke's light is %.2f bright (under 0.75 it is drawn dimmer)\n", luma));
	}

	ID3D11DeviceContext *context = device->Get_Context();
	D3D11_VIEWPORT savedViewport;
	UINT viewportCount = 1;
	context->RSGetViewports(&viewportCount, &savedViewport);

	if (state.lastStepFrame != now) {
		const int frames = state.lastStepFrame < 0 ? 1 : (std::min)((std::max)(now - state.lastStepFrame, 1), 4);
		for (unsigned t = 0; t < 2; ++t)
			if (slabs[t]) step(context, t, now, (float)frames / LOGICFRAMES_PER_SECOND, seconds, toward, slabs[t]);
		// the fires every logic frame on High, every other on Low
		if (slabs[2] && (state.lastFireStep < 0 || now - state.lastFireStep >= (int)Qualities[level].fireEvery || now < state.lastFireStep)) {
			const int fireFrames = state.lastFireStep < 0 || now < state.lastFireStep ? 1 : (std::min)(now - state.lastFireStep, 4);
			step(context, 2, now, (float)fireFrames / LOGICFRAMES_PER_SECOND, seconds, toward, slabs[2]);
			state.lastFireStep = now;
		}
		if (airActive) stepAir(context, 0, now, (float)frames / LOGICFRAMES_PER_SECOND, seconds, toward);
		else airVolume.live = false;	// the next system starts it from an empty grid
		// after the air, which hands it what climbed out of the air's top this step
		if (!tallSystems.empty() || !tallBoxes.empty()) stepAir(context, 1, now, (float)frames / LOGICFRAMES_PER_SECOND, seconds, toward);
		else tallVolume.live = false;
		state.lastStepFrame = now;
	}

	DrawConstants draw = {};
	if (!HazardCloud::inverse(viewProjection, draw.WorldFromClip) || !HazardCloud::inverse(camera.view, eyeRows)) {
		context->RSSetViewports(viewportCount, &savedViewport);
		backend->Forget_Bindings();
		return;
	}
	const float depthRange = (std::max)(camera.maxZ - camera.minZ, 1e-6f);
	for (unsigned column = 0; column < 4; ++column) {
		const float row = draw.WorldFromClip[8 + column] / depthRange;
		draw.WorldFromClip[12 + column] -= camera.minZ * row;
		draw.WorldFromClip[8 + column] = row;
	}
	for (unsigned i = 0; i < 3; ++i) draw.Camera[i] = eyeRows[12 + i];
	draw.Camera[3] = seconds;
	draw.Viewport[0] = (float)camera.x + 0.5f;
	draw.Viewport[1] = (float)camera.y + 0.5f;
	draw.Viewport[2] = (float)camera.width;
	draw.Viewport[3] = (float)camera.height;
	for (unsigned i = 0; i < 3; ++i) {
		draw.SunDir[i] = toward[i];
		draw.SunColor[i] = (&sun.diffuse.red)[i];
		draw.Ambient[i] = (&sun.ambient.red)[i];
	}

	// Far box first, so the nearer one lies over it.
	unsigned order[MaxVolumes];
	float distance[MaxVolumes];
	D3D11_RECT rects[MaxVolumes];
	unsigned shown = 0;
	for (unsigned slot = 0; slot < MaxVolumes; ++slot) {
		const Volume &volume = volumes[slot];
		const TierShape &shape = TierShapes[tierOf(slot)];
		// a slab claimed after this frame's step still holds its last tenant
		if (!volume.live || volume.reset) continue;
		float low[3], high[3];
		boxOf(volume, shape, low, high);
		if (!screenRect(low, high, viewProjection, camera, rects[slot])) continue;
		float squared = 0;
		const unsigned sides[3] = { shape.gx, shape.gy, shape.gz };
		for (unsigned i = 0; i < 3; ++i) {
			const float centre = volume.origin[i] + volume.cell * sides[i] * 0.5f - draw.Camera[i];
			squared += centre * centre;
		}
		distance[slot] = squared;
		order[shown++] = slot;
	}
	std::sort(order, order + shown, [&](unsigned a, unsigned b) { return distance[a] > distance[b]; });
	// the air round everything, drawn first, under the blasts in it: the tall air, then the air
	float airLow[2][3], airHigh[2][3];
	D3D11_RECT airRect[2];
	bool airShown[2] = {};
	for (unsigned g = 0; g < 2; ++g) {
		const Volume &grid = g ? tallVolume : airVolume;
		airShown[g] = airActive && grid.live && !grid.reset
			&& airClip(grid, g ? state.tall : state.air, g ? tallBoxes : airBoxes, now, airLow[g], airHigh[g])
			&& screenRect(airLow[g], airHigh[g], viewProjection, camera, airRect[g]);
	}

	if (shown || airShown[0] || airShown[1]) {
		ID3D11DepthStencilView *sceneDepth = backend->Scene_Depth_Target();
		backend->Resolve_Scene_Depth();
		ID3D11ShaderResourceView *depth = device->Get_Depth_Texture();
		const D3D11_VIEWPORT sceneViewport = { float(camera.x), float(camera.y), float(camera.width),
			float(camera.height), camera.minZ, camera.maxZ };
		ID3D11SamplerState *const samplers[2] = { state.linear, state.wrap };
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(state.fullscreen, nullptr, 0);
		context->RSSetState(state.rasterizer);
		context->RSSetViewports(1, &sceneViewport);
		context->OMSetDepthStencilState(nullptr, 0);
		context->OMSetRenderTargets(1, &scene, nullptr);
		context->OMSetBlendState(state.premultiplied, nullptr, 0xffffffff);
		context->PSSetSamplers(0, 2, samplers);
		context->PSSetConstantBuffers(0, 1, &state.drawConstants);
		// the budget: past three blasts on screen each one is marched in longer steps, past six fires each fire
		// (a gun's shot counts for nothing here: it is gone in a second and a half, and six of them in a
		// tank fight would coarsen the dead tanks' smoke beside them)
		unsigned firesShown = 0, gunsShown = 0;
		for (unsigned n = 0; n < shown; ++n) {
			firesShown += volumes[order[n]].kind == Fire ? 1 : 0;
			gunsShown += volumes[order[n]].kind == Muzzle ? 1 : 0;
		}
		const float stepCells = Qualities[level].stepCells * (std::max)(1.0f, (shown - firesShown - gunsShown) / 3.0f);
		// (a fire's cells are half a blast's or less, so its march takes longer steps in them)
		const float fireStepCells = 1.5f * Qualities[level].stepCells * (std::max)(1.0f, firesShown / 6.0f);
		for (unsigned g = 2; g-- > 0;) {
			if (!airShown[g]) continue;
			const Tier &tier = g ? state.tall : state.air;
			const Volume &grid = g ? tallVolume : airVolume;
			ID3D11ShaderResourceView *const views[5] = { depth, tier.density[tier.current].view, tier.light.view,
				tier.velocity[0].view, state.noiseView };
			context->PSSetShaderResources(0, 5, views);
			context->PSSetShader(tier.march, nullptr, 0);
			for (unsigned i = 0; i < 3; ++i) {
				draw.Box[i] = grid.origin[i];
				draw.ClipLow[i] = airLow[g][i];
				draw.ClipHigh[i] = airHigh[g][i];
			}
			draw.Box[3] = grid.cell;
			// the particles' own colour times what their textures average to, by eye against the billboards
			const float soot[4] = { 0.5f, 0.49f, 0.48f, NozzleGlow }, dust[4] = { 0.8f, 0.66f, 0.48f, 0 };
			std::memcpy(draw.Soot, soot, sizeof(soot));
			std::memcpy(draw.Dust, dust, sizeof(dust));
			std::memset(draw.Blast, 0, sizeof(draw.Blast));
			std::memset(draw.Effects, 0, sizeof(draw.Effects));
			std::memset(draw.Extra, 0, sizeof(draw.Extra));
			const float look[4] = { ShadeLight, 0, SunShare, 0 };
			std::memcpy(draw.Look, look, sizeof(look));
			const float slab[4] = { 0, 1, (g ? TallShapes : AirShapes)[level].stepCells, AirExtinction };
			std::memcpy(draw.Slab, slab, sizeof(slab));
			upload(context, state.drawConstants, &draw, sizeof(draw));
			context->RSSetScissorRects(1, &airRect[g]);
			context->Draw(3, 0);
			++state.drawn;
		}
		int boundTier = -1;
		for (unsigned n = 0; n < shown; ++n) {
			const unsigned slot = order[n];
			const Volume &volume = volumes[slot];
			const int t = (int)tierOf(slot);
			if (t != boundTier) {
				const Tier &tier = state.tiers[t];
				ID3D11ShaderResourceView *const views[6] = { depth, tier.density[tier.current].view, tier.light.view,
					tier.velocity[0].view, state.noiseView, t == 2 ? state.blackbodyView : nullptr };
				context->PSSetShaderResources(0, 6, views);
				context->PSSetShader(tier.march, nullptr, 0);
				boundTier = t;
			}
			boxOf(volume, TierShapes[t], draw.ClipLow, draw.ClipHigh);
			for (unsigned i = 0; i < 3; ++i) draw.Box[i] = volume.origin[i];
			draw.Box[3] = volume.cell;
			const float t0 = secondsSince(volume, now);
			schedule(volume, slabOf(slot), t0, &draw);
			const float left = (float)(volume.life - (now - volume.born)) / LOGICFRAMES_PER_SECOND;
			draw.Slab[0] = (float)slabOf(slot);
			const bool shortLived = volume.kind == TankDeath || volume.kind == Structure || volume.kind == Launch
				|| (volume.kind == Shell && volume.scale < 2.0f);
			draw.Slab[1] = (std::min)(1.0f, (std::max)(0.0f, left / (shortLived ? ShortEndFade
				: volume.kind == Fire ? FireEndFade : volume.kind == Muzzle ? MuzzleEndFade : 3.0f)));
			// a fire comes up over its warm-up, under the billboards it then takes from
			if (volume.kind == Fire) draw.Slab[1] *= (std::min)(1.0f, t0 / FireWarmSeconds);
			draw.Slab[2] = volume.kind == Fire ? fireStepCells : stepCells;
			draw.Slab[3] = Extinction;
			upload(context, state.drawConstants, &draw, sizeof(draw));
			context->RSSetScissorRects(1, &rects[slot]);
			context->Draw(3, 0);
			++state.drawn;
		}
		ID3D11ShaderResourceView *const none[6] = {};
		context->PSSetShaderResources(0, 6, none);
		context->OMSetRenderTargets(1, &scene, sceneDepth);
	}
	context->RSSetViewports(viewportCount, &savedViewport);
	backend->Forget_Bindings();
}

void Reset()
{
	pendingSparks.clear();	// before the early return: a burst queued as the setting went Off
	nozzles.clear();
	pendingHits.clear();
	ownedHits.clear();
	gunSystems.clear();
	clearFires();			// the system IDs start again from 1
	if (!requested()) return;
	for (Volume &volume : volumes) volume = Volume();
	airVolume = Volume();
	tallVolume = Volume();
	airBoxes.clear();
	tallBoxes.clear();
	airSystems.clear();
	airOutside.clear();
	tallSystems.clear();
	movers.clear();
	shocks.clear();
	fronts.clear();
	solids.clear();
	lastSolids.clear();
	solidsFrame = -100000;
	takenSystems.clear();
	firstSeen.clear();
	state.lastStepFrame = -1;
	lastReport = 0;
}

void ReleaseDevice()
{
	releaseState();
}

} // namespace VolumeSmoke
