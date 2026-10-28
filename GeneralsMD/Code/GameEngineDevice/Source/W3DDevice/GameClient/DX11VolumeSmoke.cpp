// Explosion smoke as a fluid, behind the VolumeSmoke setting (Off, Low, High; -volsmoke for one
// run), on the Direct3D 11 frame only.  Distributed under
// the repository's GPL-3.0 terms; see LICENSE.md.
//
// Four kinds of blast, each told from the particle systems its FX spawns and each played from a
// timeline of its own, modelled on how the real thing looks:
//
// A tank's death is an ammunition cook-off, laid out on the dying hull's own box (its template's
// length, width and height and its heading, read from the drawable).  Orange flame and black soot
// burst out low along the hull and roll along the ground off both ends over a skirt of pale dust,
// a ragged plume of fire, white in the middle and orange at its edges, fans up out of the hull
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
// A building's death is laid out on its footprint, read from the structure list Gather keeps, and
// sized by it: fire and soot burst out low over the whole footprint, a ragged plume of fire climbs
// out of it and cools to smoke, the collapse pours pale dust out from under every wall along the
// ground and round the buildings beside it, and the ruin burns under a dark column for half a
// minute.  A damaged stage blows out of the roof and walls of a building that stays standing.
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
// Nothing here is read by the logic or draws a random number: every variation is an integer hash.
// The D3D9 and SDL3 paths, -headless and every run with the setting Off keep the billboards, and
// with it Off Takes, Hides and Render return before touching anything.  With it on, every
// system of a blast this takes draws no billboards, its GPU overflow particles included; the
// shell keeps its additive fire and flash cards, the tank and the nuclear blast have none, and
// the tank's ground ring and the smoke trails behind its flying scrap are taken with them.  The
// scrap itself, and every gun's muzzle smoke, stay the game's own.
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
#include "W3DDevice/GameClient/VolumeSmoke.h"
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
struct TierShape { unsigned gx, gy, gz, slabs, first; };
TierShape TierShapes[2] = { { 48, 48, 40, 24, 0 }, { 96, 96, 80, 2, 24 } };
const unsigned MaxVolumes = 26;		// both tiers' slabs at High, the small tier's first
const unsigned MaxSlabs = 24;		// the constant arrays' length, the same in both tiers
const unsigned MaxPuffs = 28;	// a tank's 28 (base, jets, stem, soot, skirt); PUFFS in the HLSL is the same
const unsigned MaxMovers = 16;		// High's; Low takes the nearest 8
const unsigned NoiseSide = 32;

// The VolumeSmoke setting, Low and High (Off draws nothing).  Low keeps High's grids, since every
// timeline constant is written in cells, and halves what a frame pays instead: half as many
// volumes at once, 8 pressure sweeps for 14, a march step half as long again.
// ponytail: a coarser Low grid needs the timelines' cell constants scaled with it; do that if
// Low still costs too much on a weak card.
// High's small grid is 56 cells tall where Low's is 40, so a tank's plume has room to climb to
// twice its base's width under the grid's top (Low keeps the shorter plume and the cheaper grid).
struct Quality { unsigned slabs[2]; unsigned sweeps; float stepCells; unsigned smallHeight; };
const Quality Qualities[3] = { { { 0, 0 }, 0, 0.0f, 40 }, { { 12, 1 }, 8, 0.9f, 40 }, { { 24, 2 }, 14, 0.6f, 56 } };	// sweeps even, so the pressure ends where it started
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
const float DustHalfLife = 6.0f;
const float CapSpillHalfLife = 1.0f;	// a nuclear cap's soot once the flow has carried it out of the cap
const float ShadeLight = 0.26f;			// the light smoke keeps where the sun does not reach it
const float SunShare = 0.75f;			// and what the sun adds where it does
const float Extinction = 0.16f;			// per world unit at density 1

const DXGI_FORMAT VelocityFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;	// xyz cells/s, w heat
const DXGI_FORMAT DensityFormat = DXGI_FORMAT_R16G16_FLOAT;			// soot, dust
const DXGI_FORMAT ScalarFormat = DXGI_FORMAT_R16_FLOAT;

enum Kind { TankDeath, Shell, Airburst, Nuclear, Air, Structure };

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
	float Origins[MaxSlabs][4];		// each slab's world corner, for the movers below
	float MoverCount[4];
	float Movers[MaxMovers * 4][4];	// the units that push the air this step, in world units (Mover)
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
    return (1 - smoothstep(0.6, 1.0, length(across))) * smoothstep(0, 10, GZ - 1 - cell.z);
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
#define SHOCK 4
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
        } else {
            // a blast's front: a shell racing outwards, pushing what it passes
            float r = length(off);
            float shell = exp(-pow((r - a.w) / max(e.w, size), 2));
            if (shell < 0.02) continue;
            vt.xyz += off / max(r, 1e-3) * (f.y / size) * shell * saturate(dt * 12);
        }
    }
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
        float3 back = cell - Vel.Load(int4(id, 0)).xyz * dt;
        vt = Vel.SampleLevel(Linear, uvwOf(back, slab), 0);
        DEN carried = Den.SampleLevel(Linear, uvwOf(back, slab), 0);
        DEN settled = Den.SampleLevel(Linear, uvwOf(back + float3(0, 0, Step.z * dt), slab), 0);	// dust settles
        d.x = carried.x;
        d.y = settled.y;
#ifdef AIR
        d.z = carried.z * Keep.w;	// soot thins out in the open air
        d.w = settled.w * Keep.z;
        d.x *= Keep.w;
#endif
        // Heat lifts, fading out under the ceiling, where a rising cap stops and spreads.
        float lift = Forces.x * vt.w * saturate((shape.z - cell.z) / max(shape.z * 0.3, 1));
        vt.z += dt * (lift - Forces.y * d.y);	// soot is light; dust and dirt are what weigh
        float3 grad = 0.5 * float3(w(c + int3(1, 0, 0), slab) - w(c - int3(1, 0, 0), slab),
            w(c + int3(0, 1, 0), slab) - w(c - int3(0, 1, 0), slab),
            w(c + int3(0, 0, 1), slab) - w(c - int3(0, 0, 1), slab));
        float length2 = dot(grad, grad);
        if (length2 > 1e-10)
            vt.xyz += dt * Forces.z * cross(grad * rsqrt(length2), Curl.Load(int4(id, 0)).xyz);
        vt.xyz /= 1 + Forces.w * length(vt.xyz) * dt;
        vt.xy += (Wind.xy / volume.z - vt.xy) * saturate(dt * 0.35 * saturate(cell.z / 6.0));
        vt.xyz *= Keep.y;
        vt.w *= Keep.x;
        d.y *= Keep.z;
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
    if (power.w > 0 && rho < blast.w * 1.6)
        d.y = max(d.y, power.w * layer * (0.5 + valueNoise(cell / 2, slab + 5)));
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
// and dust that leaves the stem thin out (Keep.w), so the shape stays a mushroom and the surge is
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
        d.x *= lerp(Keep.w, 1, saturate((1.3 - surface) * 3));
        d.y *= lerp(Keep.w, 1, saturate(inStem * 3));
        // white-yellow in the middle, orange-red at the rim, the lumps hotter than the folds between
        vt.w = max(vt.w, power.z * inCap * (1 - 0.6 * saturate(e)) * (0.5 + 1.0 * lumps));
        vt.z = lerp(vt.z, shape.w, 0.15 * inStem);
    } else if (cap.x > 0) d *= Keep.w;
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
        if (solid(c - int3(0, 0, 1), slab)) d.y *= 0.97;
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
    float3 tint = light / max(dot(light, float3(0.299, 0.587, 0.114)), 0.01);
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
        // each cell's own grey, carried with its soot from the particles that made it
        float3 albedo = (Soot.rgb * dd.z + Dust.rgb * dd.w) / total;
#else
        float3 albedo = (Soot.rgb * dd.x + Dust.rgb * dd.y + float3(0.92, 0.93, 0.95) * shell) / total;
#endif
        float3 lit = albedo * tint * (Look.x + 0.12 * height + Look.z * sun * phase);
        float3 source = (lit + fireColour(heat) * Soot.w * (dd.x / total)) * sigma;
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
const float AirSootHalfLife = 7.0f;		// seconds, once no particle holds it up
const float AirTakeUp = 8.0f;			// a second: how fast the cells under a particle reach its smoke
const float AirCoverage = 0.7f;			// a billboard's opacity at its centre, against its alpha
// A sphere's falloff, the trilinear blur of a few cells and the noise take most of the opacity a
// particle's density was worked out for; this gives it back (by eye, on volair's dust and smoke).
const float AirGain = 4.0f;
// and a missile's ribbon of jet smoke, a cell or two wide: a little less in the air, where a Rocket
// Buggy salvo's twenty trails stood as a white wall at 1.8, and more in the tall air, where one trail
// coming down alone is seen across and its cells are half again as wide
const float StreakGain[2] = { 0.7f, 1.8f };
// A missile's white jet smoke against a cloud's grey puff: the billboard is the texture's white times
// the particle's colour, unlit, so a sunlit cell needs about twice the puff's albedo to match it.  At
// 1.8 a Scud's trail came down mid grey over sunlit tan ground and could not be seen.
const float TrailBright = 2.4f;
// The tall air: a second grid round the camera, as wide as the air's and a thousand units tall at
// twice its cell, for the systems that climb out of the air's top, a nuclear missile's or a Scud
// Storm's trail coming down from high up, a Scud's arc.  It runs only while one of them is in it.
// The air's 30 or 40 cells hold a column to about 200 units over the ground; a trail that reached
// past that kept its billboards whole and, nearer the line, lost the part over the top.
const AirShape TallShapes[3] = { { 8, 8, 8, 1.0f, 0, 0, 1.0f }, { 48, 48, 48, 24.0f, 2048, 6, 1.5f },
	{ 96, 96, 96, 12.0f, 4096, 8, 1.25f } };
const float AirTallOver = 30.0f;		// units over the air's top less its rim: a trail with a particle that high goes to the tall air
const float AirTrailOver = 150.0f;		// units over the top: any other system with a particle that high keeps its billboards

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
	unsigned seed = 0;
	int born = 0;
	int life = 0;						// logic frames
	unsigned puffs = 0;
	int floorFrame = -1000;				// when its terrain and buildings were last laid into the grid
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
	Tier tiers[2];
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
// fronts still running out.
enum MoverKind { MoverVehicle, MoverJet, MoverRotor, MoverWalker, MoverShock };
struct Mover { float m[4][4]; float distance; };
std::vector<Mover> movers;
struct Shock { float at[3], speed, strength, seconds; int born; };
std::vector<Shock> shocks;
unsigned moverPeak = 0;
// Every ground vehicle near the camera, standing or moving, this logic frame and the one before,
// so a tank dying now finds its hull: where it stands, its heading and its template's box.
struct Hull { float x, y, heading, major, minor, tall; };
std::vector<Hull> hulls, lastHulls;
// The structures the smoke flows round, read by Gather every two seconds outside the logic
// thread's window (the render runs inside it, where walking the object list is a counted read).
// A structure goes in once the player whose view is drawn has seen it, so smoke parting round
// nothing does not give away an unscouted base.
// The list before the last read is kept too, so a building that died just after a read still has its
// footprint when its death's smoke asks for it.
struct Solid { float x, y, top, major, minor, angle, tall; bool box; ObjectID id; };
std::vector<Solid> solids, lastSolids;
int solidsFrame = -100000;

unsigned tierOf(unsigned slot) { return slot >= TierShapes[1].first ? 1 : 0; }
unsigned slabOf(unsigned slot) { return slot - TierShapes[tierOf(slot)].first; }

void releaseState();
void pruneSets();
void boxOf(const Volume &volume, const TierShape &shape, float low[3], float high[3]);
void addShock(const Coord3D &where, float speed, float strength, float seconds, int now);
void fillMovers();

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
		hulls.clear();
		lastHulls.clear();
		solids.clear();
		lastSolids.clear();
		solidsFrame = -100000;
		lastReport = 0;
		level = want;
		TierShapes[0].slabs = Qualities[level].slabs[0];
		TierShapes[0].gz = Qualities[level].smallHeight;
		TierShapes[1].slabs = Qualities[level].slabs[1];
		TierShapes[1].first = TierShapes[0].slabs;
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
	dropTier(state.tiers[0]);
	dropTier(state.tiers[1]);
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

bool makeTier(DX11BackendClass *backend, ID3D11Device *device, Tier &tier, const TierShape &shape, bool air)
{
	tier.shape = shape;
	tier.air = air;
	const std::string header = std::string(air ? "#define AIR 1\n" : "") + "#define GX " + std::to_string(shape.gx)
		+ "\n#define GY " + std::to_string(shape.gy) + "\n#define GZ " + std::to_string(shape.gz) + "\n#define SLABS "
		+ std::to_string(shape.slabs) + "\n" + Grid;
	const std::string prelude = header + Prelude;
	static const char *const names[ProgramCount] = { "volsmoke curl", "volsmoke advect", "volsmoke divergence",
		"volsmoke jacobi", "volsmoke project", "volsmoke light", "volsmoke scroll" };
	const std::string sources[ProgramCount] = {
		prelude + CurlPass, prelude + MoverPass + AdvectPass + MushroomPass + AdvectPassEnd, prelude + DivergencePass,
		prelude + PressureHelpers + JacobiPass, prelude + PressureHelpers + ProjectPass, prelude + LightPass,
		prelude + ScrollPass };
	std::vector<unsigned char> code;
	bool made = true;
	for (unsigned pass = 0; made && pass < ProgramCount; ++pass)
		if (pass != ScrollProgram || air)	// only the air moves with the camera
			made = compile(backend, sources[pass], names[pass], "cs_5_0", code)
				&& SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &tier.compute[pass]));
	made = made && compile(backend, header + MarchPass, "volsmoke march", "ps_5_0", code)
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
	DEBUG_LOG(("VOLSMOKE: programs, %u + %u volumes (the small %u cells tall), a %ux%ux%u air at %.1f units a cell and %s made in %.1f ms\n",
		TierShapes[0].slabs, TierShapes[1].slabs, TierShapes[0].gz, air.gx, air.gy, air.gz, air.cell,
		state.tallRefused ? "no tall air" : level == 2 ? "a 96 cell tall air at 12" : "a 48 cell tall air at 24",
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
	// a building's death and its damaged stages (FX_Structure*Death, TransitionDamageFX), and
	// FX_SmallStructureDeath's
	else if (alpha && std::strstr(name, "Smoke") && (!std::strncmp(name, "StructureTransition", 19)
			|| !std::strcmp(name, "StructureExplosionSmoke")))
		kind = { true, Structure, 1.0f };
	// a building's damage and death smoke is DEATH_EXPLOSION too; buildings are not tanks
	else if (alpha && system->getPriority() == DEATH_EXPLOSION && std::strstr(name, "Smoke") && !std::strstr(name, "Structure"))
		kind = { true, TankDeath, 1.0f };
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
		dust[0] = 0.70f; dust[1] = 0.62f; dust[2] = 0.50f;	// sunlit, fire-lit dust, warm against the tan ground
		// The base: orange flame and black soot bursting out low along the whole hull, rolling off
		// both ends and a little to the sides, held down by the dust in it.  The four towards the
		// ends burn only a dull red-brown (heat 0.8), so the hot middle sits between dark lobes,
		// as in the photo; at 1.25 all along the base read as one bright band.
		if (t < 0.7f) {
			const float fade = 1 - t / 0.7f;
			for (unsigned i = 0; i < 6; ++i) {
				const float s = i / 2.5f - 1 + (unit(800 + i) - 0.5f) * 0.2f;
				const float across = (unit(810 + i) - 0.5f) * W * 0.7f;
				const float radius = (std::max)(W * 0.85f, 2.2f) * (0.8f + 0.4f * unit(820 + i));
				onHull(s * L * 1.15f, across, 0.6f + 0.6f * unit(830 + i), radius, s * (8.0f + 12.0f * unit(840 + i)) * fade,
					(across < 0 ? -2.5f : 2.5f) * fade, 1.5f, 20.0f * fade, std::fabs(s) < 0.5f ? 1.25f : 0.8f, 8.0f * fade, 0.35f);
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
		const unsigned jets = 13;	// with the base, the stem, the soot and the skirt, the 28 a slab holds
		// A shorter reach (Low's, or a very small hull's on High) fans a short hull's rays less and
		// from less of its length, so its fan is as tall for its width as High's: at High's angles
		// Low's came out a round dome.  Long hulls do not use it.
		const float fanScale = 0.5f + 0.5f * reach / 36.0f;
		for (unsigned j = 0; j < jets; ++j) {
			auto field = [&](unsigned f) { return unit(1000 + 16 * j + f); };
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
			const float azimuth = (side < 0 ? 3.14159265f : 0.0f) + (field(3) - 0.5f) * 0.9f;
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
			// Some dust, which the sun lights pale; the soot is what glows (a jet of twice as much dust
			// burned dull tan), and with the dust it cools to grey-brown smoke rather than black.  The
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
				da * now, dc * now, dz * now, rate, heat, rate * 0.4f, 0.08f * (1 - 0.75f * shortHull));
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
				pace * reach / 0.18f * std::exp(-t / 0.18f), 25.0f * reach / 36.0f * fade, 2.8f - 1.2f * stemShort, 10.0f * reach / 36.0f * fade, 0.05f);
		}
		// the default 1.6 once the flare is over; about a third of a second of white, as long as
		// the jets run, and no longer, so several tanks dying at once do not white out the view
		soot[3] = 1.6f + 4.2f * std::exp(-t / 0.22f);
		// Dark soot hugging the hull: four thick, barely glowing puffs on its flanks, a hull's width
		// out from its axis, just over the base fire and outside the jets' roots, drifting outward
		// and a little up, so the bright core stands in red-brown smoke as in the photo and the
		// fan's top stays clear of it.  Heat 0.6 burns only a dull red (fireColour starts at 0.5).
		// Heat is the hottest emitter's, so laid half a hull's width out, inside the jets, the
		// soot burned as bright as they did and nothing dark showed.
		// 22 a second for 0.6 s: at 45 for 0.8 s a fight's three dead tanks hung under big black clouds.
		if (t < 0.6f) {
			const float fade = 1 - t / 0.6f;
			for (unsigned k = 0; k < 4; ++k) {
				const float s = (k < 2 ? -0.55f : 0.55f) * (0.85f + 0.3f * unit(980 + k)), side = (k & 1) ? 1.0f : -1.0f;
				onHull(s * L, side * W * 1.05f, H * (0.6f + 0.3f * unit(984 + k)), (std::max)(W * 0.8f, 2.2f) * (1 + t),
					s * 3.0f * fade, side * 3.0f * fade, 2.0f, 22.0f * fade, 0.6f, 3.0f * fade, 0.2f);
			}
		}
		// Pale dust lifted along the ground on both sides of the hull, pushed out low, no ring.
		if (t < 0.9f) {
			const float fade = 1 - t / 0.9f;
			for (unsigned k = 0; k < 4; ++k) {
				const float s = k < 2 ? -0.7f : 0.7f, side = (k & 1) ? 1.0f : -1.0f;
				onHull(s * L * (1.0f + 0.4f * unit(960 + k)), side * W * 1.1f, 0.5f, (std::max)(W * 0.9f, 2.5f),
					s * 4.0f * fade, side * 6.0f * fade, 0.5f, 0, 0, 10.0f * fade, 0.3f);
			}
		}
		// the cook-off pops, two to four, each a hot orange puff out of a hatch somewhere on the hull
		const unsigned pops = 2 + unsigned(unit(900) * 2.99f);
		float when = 0.9f;
		for (unsigned p = 0; p < pops; ++p) {
			when += 0.5f + 1.0f * unit(901 + p);
			const float age = t - when;
			if (age >= 0 && age < 0.2f)
				onHull((unit(910 + p) - 0.5f) * L * 1.4f, (unit(920 + p) - 0.5f) * W * 0.8f, H * 0.6f, 2.4f, 0, 0, 12.0f,
					12.0f, 1.7f, 2.0f, 0.3f);
		}
		// the hull burning: a thick black column for half a minute off its whole length
		if (t > 0.8f && t < 28.0f) {
			const float rise = (std::min)((t - 0.8f) / 1.5f, 1.0f), fall = t > 18.0f ? 1 - (t - 18.0f) / 10.0f : 1.0f;
			for (int end = -1; end <= 1; end += 2)
				onHull(end * L * 0.45f, 0, 0.5f, (std::max)(W * 0.9f, 2.2f), 0, 0, 12.0f, 2.7f * rise * fall, 1.0f, 0.05f, 0.25f);
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
		const float size = (std::max)(L, W), base = standing ? H + 0.6f : 0.6f;
		dust[0] = 0.76f; dust[1] = 0.72f; dust[2] = 0.65f;	// pale concrete and plaster dust; at 0.66 it lay grey-brown in its own shade
		// the burst: four fire-and-soot bodies over the footprint's quarters, thrown outwards along it
		if (t < 0.8f) {
			const float fade = 1 - t / 0.8f;
			for (unsigned i = 0; i < 4; ++i) {
				const float sa = (i & 1) ? 1.0f : -1.0f, sc = (i & 2) ? 1.0f : -1.0f;
				onFootprint(sa * L * (0.45f + 0.15f * unit(500 + i)), sc * W * (0.4f + 0.15f * unit(510 + i)),
					base + (standing ? 0.0f : H * 0.3f * unit(520 + i)), (std::max)((std::min)(L, W) * 0.55f, 2.5f) * (0.8f + 0.4f * unit(530 + i)),
					sa * (6.0f + 8.0f * unit(540 + i)) * fade, sc * (5.0f + 6.0f * unit(550 + i)) * fade, 3.0f + 4.0f * unit(560 + i),
					16.0f * fade, 1.35f, 3.0f * fade, 0.3f);
			}
		}
		// The plume, the tank's jets spread over the footprint: each lays thin smoke at a rate that
		// goes with its speed and tapers to a streak, so it stands as a ragged mass of fire that
		// cools to grey-brown smoke, never a ball carried on its head.
		const float reach = (std::min)((std::max)(standing ? 1.2f * size + 0.5f * H : 1.9f * size + 1.2f * H, 10.0f),
			TierShapes[0].gz - 18.0f - (standing ? H : 0.0f));
		for (unsigned j = 0; j < 7; ++j) {
			auto field = [&](unsigned f) { return unit(600 + 16 * j + f); };
			const float slows = 0.22f, age = t - 0.08f * field(0);
			if (age < 0 || age > 1.5f * slows) continue;
			const float u = (j + 0.5f) / 7 * 2 - 1, centre = 1 - std::fabs(u);
			const float a0 = (u + (field(1) - 0.5f) * 0.2f) * L * 0.75f, c0 = (field(2) - 0.5f) * W * 0.9f;
			const float tilt = 0.28f * std::fabs(u) + 0.08f * (field(3) - 0.5f);
			const float azimuth = (u < 0 ? 3.14159265f : 0.0f) + (field(4) - 0.5f) * 1.2f;
			const float ca = std::cos(azimuth), cc = std::sin(azimuth), dz = std::cos(tilt), out = std::sin(tilt);
			const float height = reach * (0.75f + 0.25f * centre * centre) * (1 - 0.25f * field(5));
			const float speed = height / slows;
			const float travelled = height * (1 - std::exp(-age / slows)), now = speed * std::exp(-age / slows);
			const float path = travelled / (0.78f * height);
			const float taper = 1 - smoothstep01(path, 0.55f, 1.0f);
			const float rate = 40.0f * reach / 36.0f * (0.75f + 0.45f * centre) * now / speed * taper;
			const float heat = 2.5f * (0.7f + 0.3f * std::pow(now / speed, 0.4f)) * (1 - 0.2f * path) * (0.45f + 0.55f * taper)
				* (0.7f + 0.3f * smoothstep01(path, 0.0f, 0.15f));
			onFootprint(a0 + ca * out * travelled, c0 + cc * out * travelled, base + dz * travelled,
				(2.2f + 0.07f * travelled) * (0.4f + 0.6f * taper) * (0.8f + 0.3f * centre),
				ca * out * now, cc * out * now, dz * now, rate, heat, rate * 0.5f, 0.08f);
		}
		// The collapse: pale dust pushed out low from under every wall, two bodies a side, each sunk
		// half its radius into the ground so only a low lens of it stands over it, and the eight
		// overlap into one sheet the shape of the footprint.  Standing on the ground, the same
		// bodies stood round the building as a ring of grey balls.
		if (t > 0.1f && t < 3.0f) {
			const float fade = 1 - (t - 0.1f) / 2.9f, pour = standing ? 0.3f : 1.0f;
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
		// the building falling into its footprint: dust dropping from its height as it comes down
		if (!standing && t > 0.2f && t < 1.6f) {
			const float progress = (t - 0.2f) / 1.4f, fade = 1 - progress;
			for (int end = -1; end <= 1; end += 2)
				onFootprint(end * L * 0.4f, 0, 1.0f + H * 0.6f * (1 - progress), (std::max)(W * 0.7f, 3.0f), 0, 0, -8.0f * fade,
					2.0f * fade, 0, 18.0f * fade, 0.3f);
		}
		// the ruin burning: a dark column off the footprint for half a minute, leaning with the wind
		if (!standing && t > 0.8f && t < 28.0f) {
			const float rise = (std::min)((t - 0.8f) / 1.5f, 1.0f), fall = t > 16.0f ? 1 - (t - 16.0f) / 12.0f : 1.0f;
			for (int end = -1; end <= 1; end += 2)
				onFootprint(end * L * 0.4f, 0, 0.6f, (std::max)(W * 0.8f, 2.5f), 0, 0, 12.0f, 3.2f * rise * fall, 1.0f, 0.1f, 0.25f);
		}
		soot[3] = 1.6f + 2.4f * std::exp(-t / 0.3f);
	} else if (volume.kind == Shell || volume.kind == Airburst) {
		const bool ground = volume.kind == Shell;
		// a flash shorter than a frame at 30, white-orange
		flash = t < 0.03f ? 12.0f : 0.0f;
		flashWide = 3.0f;
		flashTall = 1.3f;
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
		// the dirt fountain: a hollow cone of jets 10 to 20 m up, falling back in 3 to 6 s
		if (ground && t < 6.0f) {
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
		const float major = solid.major, minor = solid.minor;
		const float reach = std::sqrt(major * major + minor * minor);
		if (solid.x + reach < volume.origin[0] || solid.x - reach > right || solid.y + reach < volume.origin[1] || solid.y - reach > top)
			continue;
		const float roof = (solid.top - volume.origin[2]) / cell;
		const float c = std::cos(solid.angle), s = std::sin(solid.angle);
		const int x0 = (std::max)(0, int((solid.x - reach - volume.origin[0]) / cell)), x1 = (std::min)(int(shape.gx) - 1, int((solid.x + reach - volume.origin[0]) / cell));
		const int y0 = (std::max)(0, int((solid.y - reach - volume.origin[1]) / cell)), y1 = (std::min)(int(shape.gy) - 1, int((solid.y + reach - volume.origin[1]) / cell));
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x) {
				const float dx = volume.origin[0] + (x + 0.5f) * cell - solid.x, dy = volume.origin[1] + (y + 0.5f) * cell - solid.y;
				const float along = dx * c + dy * s, side = -dx * s + dy * c;
				const bool inside = solid.box ? std::fabs(along) <= major && std::fabs(side) <= minor : dx * dx + dy * dy <= major * major;
				if (inside) heights[y * shape.gx + x] = (std::max)(heights[y * shape.gx + x], roof);
			}
	}
	// a blast on a building goes off on its roof, not inside it
	if (volume.reset && volume.kind != Airburst && volume.kind != Structure) {
		const unsigned column = unsigned(volume.blast[1]) * shape.gx + unsigned(volume.blast[0]);
		volume.blast[2] = (std::max)(volume.blast[2], heights[column] + 0.5f);
	}
	const D3D11_BOX region = { 0, slab * shape.gy, 0, shape.gx, (slab + 1) * shape.gy, 1 };
	context->UpdateSubresource(tier.floor, 0, &region, heights.data(), shape.gx * sizeof(float), 0);
}

void step(ID3D11DeviceContext *context, unsigned t, int now, float dt, float seconds, const float sunToward[3], unsigned slabs)
{
	Tier &tier = state.tiers[t];
	sim.Step[0] = dt;
	sim.Step[1] = seconds;
	sim.Forces[0] = Buoyancy;
	sim.Forces[1] = Weight;
	sim.Forces[2] = Vorticity;
	sim.Forces[3] = Drag;
	sim.Keep[0] = std::pow(0.5f, dt / HeatHalfLife);
	sim.Keep[1] = std::pow(0.5f, dt / VelocityHalfLife);
	sim.Keep[2] = std::pow(0.5f, dt / DustHalfLife);
	sim.Keep[3] = std::pow(0.5f, dt / CapSpillHalfLife);
	for (unsigned i = 0; i < 3; ++i) sim.Sun[i] = sunToward[i];
	sim.Sun[3] = Extinction;
	sim.Wind[0] = WindX;
	sim.Wind[1] = WindY;
	float settle = 0;
	for (unsigned slab = 0; slab < tier.shape.slabs; ++slab) {
		const Volume &volume = volumes[tier.shape.first + slab];
		sim.Volumes[slab][0] = volume.live ? 1.0f : 0.0f;
		sim.Volumes[slab][1] = 0;
		sim.Volumes[slab][2] = volume.cell;
		sim.Volumes[slab][3] = volume.reset ? 1.0f : 0.0f;
		for (unsigned i = 0; i < 3; ++i) sim.Origins[slab][i] = volume.origin[i];
		if (!volume.live) continue;
		layFloor(context, tier, slab, volumes[tier.shape.first + slab], now);
		schedule(volume, slab, secondsSince(volume, now), nullptr);
		settle = DustSettle / volume.cell;	// ponytail: one settling speed a tier, its last volume's cell
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
	run(context, tier, DivergenceProgram, { tier.velocity[1].view }, { tier.divergence.write }, slabs);
	for (unsigned sweep = 0; sweep < Qualities[level].sweeps; ++sweep) {
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
		|| std::strstr(name, "Shard") || std::strstr(name, "Muzzle");
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
		addShock(where, 110.0f, 40.0f, 0.8f, (int)TheGameLogic->getFrame());
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

// A blast front, from a blast this took: it runs out at `speed` and pushes for `seconds`.  One a
// place: a blast's many systems start one front.
void addShock(const Coord3D &where, float speed, float strength, float seconds, int now)
{
	for (const Shock &shock : shocks) {
		const float dx = shock.at[0] - where.x, dy = shock.at[1] - where.y;
		if (now - shock.born < 6 && dx * dx + dy * dy < 60.0f * 60.0f) return;
	}
	if (shocks.size() >= 8) shocks.erase(shocks.begin());
	shocks.push_back({ { where.x, where.y, where.z }, speed, strength, seconds, now });
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
	for (auto it = shocks.begin(); it != shocks.end();) {
		const float age = (float)(now - it->born) / LOGICFRAMES_PER_SECOND;
		if (age > it->seconds) { it = shocks.erase(it); continue; }
		Mover mover = {};
		float (&m)[4][4] = mover.m;
		const float radius = it->speed * age;
		for (unsigned i = 0; i < 3; ++i) m[0][i] = it->at[i];
		m[0][3] = radius;
		m[1][3] = MoverShock;
		m[2][3] = 10.0f + 0.15f * radius;
		m[3][1] = it->strength * std::exp(-age / (it->seconds * 0.4f));
		mover.distance = 0;	// fronts first: they are few and short
		found.push_back(mover);
		++it;
	}
	const size_t cap = level == 2 ? MaxMovers : MaxMovers / 2;
	if (found.size() > cap)
		std::nth_element(found.begin(), found.begin() + cap, found.end(),
			[](const Mover &a, const Mover &b) { return a.distance < b.distance; });
	movers.assign(found.begin(), found.begin() + (std::min)(found.size(), cap));
	moverPeak = (std::max)(moverPeak, (unsigned)movers.size());
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
		solids.push_back({ at->x, at->y, at->z + geometry.getMaxHeightAbovePosition(), geometry.getMajorRadius(),
			geometry.getMinorRadius(), object->getOrientation(), geometry.getMaxHeightAbovePosition(),
			geometry.getGeomType() == GEOMETRY_BOX, object->getID() });
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
	struct Source { float x, y, z, r, soot, dust, bright, distance; };
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
		// a missile's trail (the nuclear missile's is puffs) and its launch cloud, both white
		const char *named = system->getTemplate()->getName().str();
		const bool trail = streak || system->getPriority() == WEAPON_TRAIL || ((std::strstr(named, "Missile") || std::strstr(named, "Missle")
			|| std::strstr(named, "Ignition")) && !std::strstr(named, "Explosion"));
		bool chained = false;
		for (Particle *p = system->getFirstParticle(); p; p = p->m_systemNext) {
			const float alpha = (std::min)(p->getAlpha() * fade * AirCoverage, 0.95f);
			if (alpha < 0.02f) { chained = false; continue; }
			const Coord3D *at = p->getPosition();
			// at most 8 cells, so a sphere reaches 27 bricks at most (AirTileEntries)
			// (a ribbon's spheres are laid along its links too, so it may go thinner without breaking up)
			const float radius = (std::min)((std::max)(p->getSize() * 0.6f, cell * (streak ? 0.6f : 0.8f)), cell * 8.0f);
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
			const float density = AirGain * (streak ? StreakGain[g] : 1.0f) * -std::log(1.0f - alpha) / (2.0f * radius * AirExtinction);
			const RGBColor *colour = p->getColor();
			const float grey = (std::min)(1.0f, 0.3f * colour->red + 0.59f * colour->green + 0.11f * colour->blue) * (trail ? TrailBright : 1.0f);
			const float dx = at->x - look.x, dy = at->y - look.y;
			const Source here = { (at->x - low[0]) / cell - 0.5f, (at->y - low[1]) / cell - 0.5f, (at->z - low[2]) / cell - 0.5f,
				radius / cell, dusty ? 0.0f : density, dusty ? density * 0.6f : 0.0f, grey, dx * dx + dy * dy };
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
						from.bright + (here.bright - from.bright) * u, from.distance + (here.distance - from.distance) * u });
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
		const float values[8] = { s.x, s.y, s.z, s.r, s.soot, s.dust, s.bright, 0 };
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
	if (!smokeBoxes.empty())	// only where the air already holds smoke
		for (const Mover &mover : movers) {
			const float (&m)[4][4] = mover.m;
			const int kind = int(m[1][3] + 0.5f);
			if (kind != MoverRotor && kind != MoverShock) continue;
			const float reach = kind == MoverRotor ? m[0][3] * 3.0f : m[0][3] + 20.0f;
			const float bottom = kind == MoverRotor ? m[0][2] - m[2][2] : m[0][2] - 20.0f;
			const float a[3] = { m[0][0] - reach, m[0][1] - reach, bottom };
			const float b[3] = { m[0][0] + reach, m[0][1] + reach, m[0][2] + 20.0f };
			grow(a, b);
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
	sim.Keep[2] = std::pow(0.5f, dt / DustHalfLife);
	sim.Keep[3] = std::pow(0.5f, dt / AirSootHalfLife);
	for (unsigned i = 0; i < 3; ++i) sim.Sun[i] = sunToward[i];
	sim.Sun[3] = AirExtinction;
	sim.Wind[0] = WindX;
	sim.Wind[1] = WindY;
	sim.Shape[0][2] = 1000.0f;	// no ceiling
	for (unsigned i = 0; i < 3; ++i) sim.Origins[0][i] = volume.origin[i];
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
float lifeOf(Kind kind) { return kind == TankDeath || kind == Structure ? 36.0f : kind == Nuclear ? 90.0f : 12.0f; }
float cellOf(Kind kind, float scale) { return kind == TankDeath ? 3.0f : kind == Nuclear ? 10.0f : 2.0f * scale; }
float joinRadius(const Volume &volume)
{
	if (volume.kind == Structure) return (std::max)(volume.hull[2], volume.hull[3]) * volume.cell + 40.0f;
	return volume.kind == Nuclear ? 500.0f : volume.kind == TankDeath ? 60.0f : 30.0f * volume.scale;
}
int rankOf(const Volume &volume) { return volume.kind == TankDeath || volume.kind == Structure ? 2 : volume.scale >= 2.0f ? 1 : 0; }

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

} // namespace

namespace VolumeSmoke {

bool Hides(ParticleSystem *system)
{
	if (!requested() || !system || TheGlobalData->m_classicGraphics) return false;
	// the air reads only the CPU's particles, so an air system's GPU overflow keeps drawing
	const int id = (int)system->getSystemID();
	return takenSystems.count(id) != 0 && !airSystems.count(id);
}

bool Takes(ParticleSystem *system)
{
	if (!requested() || !system || TheGlobalData->m_classicGraphics) return false;
	const int id = (int)system->getSystemID();
	if (takenSystems.count(id)) return !airOutside.count(id);
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
	if (!loose || (flat && system->getPriority() != DEATH_EXPLOSION && !nukeFlat && !structFlat)) return takeAir(system);
	// flying bits stay the game's own here too: a blast that took them drew nothing in their place
	if (airLookOf(system->getTemplate(), system).scrap) return false;
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
		if (!volume.live || seen < volume.born - 10
				|| seen > volume.born + (volume.kind == Nuclear ? 180 : volume.kind == TankDeath || volume.kind == Structure ? 90 : 30)) continue;	// a nuke's stem comes 2 s after its flare
		if (flat && !(volume.kind == TankDeath && system->getPriority() == DEATH_EXPLOSION) && !(nukeFlat && volume.kind == Nuclear)
			&& !(structFlat && volume.kind == Structure)) continue;
		// a building's blast takes its own FX's systems only, not the fire of the shells still
		// landing on the ruin
		if (volume.kind == Structure && !structural && system->getPriority() != DEATH_EXPLOSION) continue;
		if ((volume.kind == Shell || volume.kind == Airburst) && !alpha) continue;
		// a nuclear blast's fire cards are its own FX's only, not a shell's that lands in its reach
		const char *joiner = system->getTemplate()->getName().str();
		if (volume.kind == Nuclear && !alpha && !std::strstr(joiner, "Nuke") && !std::strstr(joiner, "MOAB")) continue;
		if (system->isSystemForever()) continue;
		if (kind.trigger && (kindNow == TankDeath) != (volume.kind == TankDeath)) continue;
		if (kind.trigger && (kindNow == Nuclear) != (volume.kind == Nuclear)) continue;
		if (kind.trigger && (kindNow == Structure) != (volume.kind == Structure)) continue;
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
		++joinedCount;
		return true;
	}

	if (flat || !kind.trigger || system->isSystemForever() || seen < now - 5 || !usable()) return takeAir(system);

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
			pruneSets();
			takenSystems.insert(id);
			return true;
		}
	}

	Volume &volume = volumes[slot];
	volume = fresh;
	volume.live = true;
	volume.reset = true;
	volume.cell = cellOf(type, kind.scale);
	// A building's box is about two and a half footprints across, so the cell goes with its size: 3
	// units for an American power plant, about 6.5 for a War Factory or a Particle Cannon.  None
	// seen (a building this player has not scouted), a 40 unit one at a hashed heading.
	const float extent = building ? (building->box ? (std::max)(building->major, building->minor) : building->major) : 40.0f;
	if (type == Structure) {
		volume.cell = (std::min)((std::max)(extent * 0.105f, 3.0f), 8.0f);
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
		// a stage the rubble systems made in the same moment is this fall's
		if (!volume.standing && building)
			for (Volume &other : volumes)
				if (&other != &volume && other.live && other.kind == Structure && other.standing && other.building == building->id
						&& now - other.born <= 2)
					other.live = false;
		static unsigned structuresLogged = 0;
		if (structuresLogged++ < 12)
			DEBUG_LOG(("VOLSMOKE: frame %d, structure %s for '%s', %.0f by %.0f by %.0f units at %.1f a cell, %s\n", now,
				building ? "found" : "not found", own, volume.hull[2] * 2 * cell, volume.hull[3] * 2 * cell, volume.hull[4] * cell, cell,
				volume.standing ? "a damaged stage" : "its fall"));
	}
	volume.born = now;
	volume.life = int((type == Structure && volume.standing ? 14.0f : lifeOf(type)) * LOGICFRAMES_PER_SECOND);
	// its front through the air round it, in world units a second.  A tank's has none: a sphere
	// pushing out from the hull's middle blew its own fire into a dome and the dust round it into
	// rings, where a real one bursts out along the hull and up (schedule).
	if (type == Nuclear) {
		addShock(where, 420.0f, 220.0f, 2.5f, now);
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
	else if (type == Structure) addShock(where, 140.0f, volume.standing ? 30.0f : 50.0f, 1.0f, now);
	else if (type != TankDeath) addShock(where, 160.0f * kind.scale, 70.0f * kind.scale, 1.0f, now);
	pruneSets();
	takenSystems.insert(id);
	if (claimedCount++ < 8)
		DEBUG_LOG(("VOLSMOKE: frame %d, %s volume %u for '%s' at (%.0f, %.0f, %.0f), %.0f units wide\n", now,
			type == TankDeath ? "tank" : type == Shell ? "shell" : type == Airburst ? "airburst" : type == Structure ? "structure" : "nuclear", slot,
			system->getTemplate()->getName().str(), where.x, where.y, where.z, sideX));
	return true;
}

void Gather()
{
	if (!requested() || !TheGameLogic || TheGlobalData->m_classicGraphics || LogicThread::isInsideWindow()) return;
	const int now = (int)TheGameLogic->getFrame();
	static int gathered = -1;
	if (gathered == now) return;
	gathered = now;
	gatherSolids(now);	// every two seconds, so a blast that starts finds its buildings
	// every frame, idle or not, so a tank that dies now finds its hull; never where no volume can be
	if (Direct3D11_Present_Is_Enabled()) collectMovers(now);
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
	unsigned live = 0, slabs[2] = {};
	for (unsigned slot = 0; slot < MaxVolumes; ++slot) {
		Volume &volume = volumes[slot];
		if (!volume.live) continue;
		if (now - volume.born > volume.life || now < volume.born) {
			volume.live = false;
			continue;
		}
		++live;
		slabs[tierOf(slot)] = slabOf(slot) + 1;
	}
	// the air runs while it holds a system, and after, while its smoke can still be there
	const bool airActive = !airSystems.empty() || !airBoxes.empty() || !tallBoxes.empty();
	if (now - lastReport >= 10 * LOGICFRAMES_PER_SECOND && (live || claimedCount || airActive)) {
		lastReport = now;
		DEBUG_LOG(("VOLSMOKE: frame %d, %u volumes live, %u blasts, %u systems joined, %u dropped so far, %u draws; "
			"air %u systems (%u tall), %u particles fed, %u left out; %u units pushing, %u at most\n", now, live, claimedCount,
			joinedCount, droppedCount, state.drawn, (unsigned)airSystems.size(), (unsigned)tallSystems.size(), airFed, airLeftOut,
			(unsigned)movers.size(), moverPeak));
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

	ID3D11DeviceContext *context = device->Get_Context();
	D3D11_VIEWPORT savedViewport;
	UINT viewportCount = 1;
	context->RSGetViewports(&viewportCount, &savedViewport);

	if (state.lastStepFrame != now) {
		const int frames = state.lastStepFrame < 0 ? 1 : (std::min)((std::max)(now - state.lastStepFrame, 1), 4);
		for (unsigned t = 0; t < 2; ++t)
			if (slabs[t]) step(context, t, now, (float)frames / LOGICFRAMES_PER_SECOND, seconds, toward, slabs[t]);
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
		// the budget: past three volumes on screen each one is marched in longer steps
		const float stepCells = Qualities[level].stepCells * (std::max)(1.0f, shown / 3.0f);
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
			const float soot[4] = { 0.5f, 0.49f, 0.48f, 0 }, dust[4] = { 0.8f, 0.66f, 0.48f, 0 };
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
				ID3D11ShaderResourceView *const views[5] = { depth, tier.density[tier.current].view, tier.light.view,
					tier.velocity[0].view, state.noiseView };
				context->PSSetShaderResources(0, 5, views);
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
			draw.Slab[1] = (std::min)(1.0f, (std::max)(0.0f, left / 3.0f));
			draw.Slab[2] = stepCells;
			draw.Slab[3] = Extinction;
			upload(context, state.drawConstants, &draw, sizeof(draw));
			context->RSSetScissorRects(1, &rects[slot]);
			context->Draw(3, 0);
			++state.drawn;
		}
		ID3D11ShaderResourceView *const none[5] = {};
		context->PSSetShaderResources(0, 5, none);
		context->OMSetRenderTargets(1, &scene, sceneDepth);
	}
	context->RSSetViewports(viewportCount, &savedViewport);
	backend->Forget_Bindings();
}

void Reset()
{
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
