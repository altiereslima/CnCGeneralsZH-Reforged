// Explosion smoke as a fluid, behind -volsmoke, on the Direct3D 11 frame only.  Distributed under
// the repository's GPL-3.0 terms; see LICENSE.md.
//
// Three kinds of blast, each told from the particle systems its FX spawns and each played from a
// timeline of its own, modelled on how the real thing looks:
//
// A tank's death is an ammunition cook-off.  A flame jet vents up through the turret ring, two to
// four smaller pops follow over a few seconds, each an orange puff with sparks, and then the hull
// burns: a thick black column for half a minute that rises, leans with the wind and thins, over
// a small tan skirt of dust at the ground.
//
// A shell or a bomb on the ground flashes for a frame, throws a hollow cone of dirt jets ten to
// twenty metres up that fall back inside three to six seconds, leaves a black-grey puff in the
// middle that rises slowly, and pushes a skirt of dust out along the ground.  Bombs are the same
// at a larger cell with a small mushroom on top; an airburst has no dirt, only the puff and a
// patch of dust under it.
//
// A nuclear-class blast (the missile, the GLA's, the MOAB) has a volume of its own, much bigger:
// a white flash, a fireball that rises fast and slows, a brief white condensation shell, a dust
// skirt racing out ahead, and then a toroidal cap rolling round a narrow stem fed by air drawn in
// along the ground, red-brown early, grey later, spreading flat once it stops rising.
//
// The sim is the same for all of them: a slab of a shared 3D atlas per blast, soot and dust in
// two channels (the dust settles under gravity, the soot rises with the heat), and each logic
// frame six compute passes: the curl, a semi-Lagrangian advection with buoyancy that fades under
// a ceiling (so a cap stops and spreads), vorticity confinement, speed-squared drag and the wind,
// the blast's forcing (the ground skirt, the vortex ring, the inflow stem, the lumpy hot core and
// the emitters the timeline places), the divergence, 14 Jacobi sweeps, the projection and a sun
// light march per cell.  The frame raymarches each box against the scene depth, far box first,
// with the heat glowing from deep red to white, a flash and a condensation shell drawn from the
// timeline, and a billowy noise eroding the surface so the cap reads as cauliflower.
//
// Nothing here is read by the logic or draws a random number: every variation is an integer hash.
// The D3D9 and SDL3 paths, -headless and every run without the switch keep the billboards, and
// with the switch off Takes, Hides and Render return before touching anything.  With it on, every
// system of a blast this takes draws no billboards, its GPU overflow particles included; the tank
// and the shell keep their additive fire and flash cards, the nuclear blast has none.
#include "Common/GlobalData.h"
#include "Common/Debug.h"
#include "Common/EarlyCommandLine.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
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

// Two atlases: the small one for tanks and shells, the big one for nuclear blasts.
struct TierShape { unsigned gx, gy, gz, slabs, first; };
const TierShape TierShapes[2] = { { 48, 48, 40, 24, 0 }, { 96, 96, 80, 2, 24 } };
const unsigned MaxVolumes = 26;		// both tiers' slabs, the small tier's first
const unsigned MaxSlabs = 24;		// the constant arrays' length, the same in both tiers
const unsigned MaxPuffs = 24;
const unsigned JacobiSweeps = 14;		// even, so the pressure ends where it started
const unsigned NoiseSide = 32;

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
const float Extinction = 0.16f;			// per world unit at density 1
const float StepCells = 0.6f;			// march step with up to three volumes on screen

const DXGI_FORMAT VelocityFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;	// xyz cells/s, w heat
const DXGI_FORMAT DensityFormat = DXGI_FORMAT_R16G16_FLOAT;			// soot, dust
const DXGI_FORMAT ScalarFormat = DXGI_FORMAT_R16_FLOAT;

enum Kind { TankDeath, Shell, Airburst, Nuclear };

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
	float Puffs[MaxSlabs * MaxPuffs * 3][4];
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
	float Effects[4];		// flash, condensation shell radius in cells and opacity, unused
};

// What every program starts with, after the tier's GX, GY, GZ and SLABS: the fade that keeps smoke
// off the box's walls, round about the column's axis so no square corner shows.
const char *const Grid = R"HLSL(
float walls(float3 cell)
{
    float2 across = (cell.xy - float2(GX - 1, GY - 1) * 0.5) / (GX * 0.5 - 1);
    return (1 - smoothstep(0.6, 1.0, length(across))) * smoothstep(0, 10, GZ - 1 - cell.z);
}
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
#define PUFFS 24
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
    float4 Puffs[VOLS * PUFFS * 3];
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
float3 jitterAt(uint3 id)
{
    uint h = hash(uint3(id.xy, id.z + (uint)(Step.y * 30) * 4099u));
    return float3(h & 1023, (h >> 10) & 1023, (h >> 20) & 1023) / 511.5 - 1;
}
// The skirt: a thin outflow along the ground from under the blast, the core's footprint swelling
// evenly and the 1/r sheet outside it, lobed by a hash of the slab, as no real blast pushes evenly.
float skirtLayer(float3 cell) { return 1 - smoothstep(1.5, 4.0, cell.z); }
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

// The literal is cut in two: MSVC refuses one longer than 16 KB (C2026).
const char *const AdvectPass = R"HLSL(
Texture3D<float4> Vel : register(t0);
Texture3D<float2> Den : register(t1);
Texture3D<float4> Curl : register(t2);
RWTexture3D<float4> VelOut : register(u0);
RWTexture3D<float2> DenOut : register(u1);
RWTexture3D<float> PresOut : register(u2);
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
    float4 vt = 0;
    float2 d = 0;
    if (volume.w > 0.5) {
        PresOut[id] = 0;
    } else {
        float3 back = cell - Vel.Load(int4(id, 0)).xyz * dt;
        vt = Vel.SampleLevel(Linear, uvwOf(back, slab), 0);
        d.x = Den.SampleLevel(Linear, uvwOf(back, slab), 0).x;
        d.y = Den.SampleLevel(Linear, uvwOf(back + float3(0, 0, Step.z * dt), slab), 0).y;	// dust settles
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
    // The ground skirt, and the dust it lifts.
    if (power.x > 0) {
        float layer = skirtLayer(cell);
        float reach = 1 - smoothstep(blast.w * 3, blast.w * 7, rho);
        float3 flow = float3(skirtFlow(cell, slab), 0) + jitterAt(id) * power.x * 0.2;
        vt.xyz = lerp(vt.xyz, flow, 0.6 * layer * reach);
    }
    if (power.w > 0 && rho < blast.w * 1.6)
        d.y = max(d.y, power.w * skirtLayer(cell) * (0.5 + valueNoise(cell / 2, slab + 5)));
    // The vortex ring: a torus round the core, up through the middle, out over the top, down
    // outside the rim and in underneath; it rolls the cap and draws the ground air inward.
    if (shape.x > 0) {
        float h = cell.z - blast.z;
        float dr = rho - shape.y;
        float q = sqrt(dr * dr + h * h);
        float tube = max(shape.y * 0.5, 1.5);
        float k = exp(-q * q / (tube * tube));
        float3 swirl = float3(radial * h, -dr) * (shape.x / max(q, 0.75));
        swirl.z += Motion[slab].x;
        vt.xyz = lerp(vt.xyz, swirl, 0.3 * k);
    }
    // The stem: air drawn in along the ground under the cap and up the axis into it.
    if (shape.w > 0) {
        float ring = max(shape.y, 2);
        float ground = skirtLayer(cell) * (1 - smoothstep(ring * 1.5, ring * 3, rho));
        float axis = exp(-rho * rho / (ring * ring * 0.2)) * (cell.z < blast.z ? 1 : 0);
        float3 stem = float3(-radial * shape.w * 0.6, 0) * ground + float3(0, 0, shape.w) * axis;
        vt.xyz = lerp(vt.xyz, stem, 0.15 * max(ground, axis));
    }
    // The hot core, lumpy from the start so the rising cap breaks into billows.
    if (power.y > 0 || power.z > 0) {
        float r = length(cell - blast.xyz);
        if (r < blast.w) {
            float k = sqrt(1 - r / blast.w);
            d.x = max(d.x, power.y * k * (0.35 + 1.3 * valueNoise(cell / 2.5, slab)));
            vt.w = max(vt.w, power.z * k * (0.6 + 0.8 * valueNoise(cell / 3 + 11, slab)));
        }
    }
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
    d = min(d, 4);
    d *= lerp(0.92, 1, walls(cell));	// smoke that drifts to the walls thins out there
    // Open sides and top: what reaches them is gone.  The ground lets nothing through and keeps
    // a little of the dust that lands on it.
    if (c.x == 0 || c.y == 0 || c.x == GX - 1 || c.y == GY - 1 || c.z == GZ - 1) { d = 0; vt.w = 0; }
    if (c.z == 0) { vt.z = max(vt.z, 0); d.y *= 0.97; }
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
    float below = c.z == 0 ? -v(c, slab).z : v(c - int3(0, 0, 1), slab).z;
    // The skirt's footprint is the one place the flow may diverge, what skirtFlow carries, so the
    // projection keeps the outflow instead of cancelling it.
    float source = 0;
    if (Power[slab].x > 0 && length(float2(c.xy) - Blasts[slab].xy) < Blasts[slab].w)
        source = 2 * Power[slab].x / Blasts[slab].w * skirtLayer(float3(c));
    DivOut[id] = 0.5 * (v(c + int3(1, 0, 0), slab).x - v(c - int3(1, 0, 0), slab).x
        + v(c + int3(0, 1, 0), slab).y - v(c - int3(0, 1, 0), slab).y
        + v(c + int3(0, 0, 1), slab).z - below) - source;
}
)HLSL";

// Open air outside the sides and top holds no pressure; the ground mirrors the cell above it.
const char *const PressureHelpers = R"HLSL(
Texture3D<float> Pres : register(t0);
float p(int3 c, uint slab, float here)
{
    if (c.z < 0) return here;
    if (any(c < int3(0, 0, 0)) || any(c >= int3(GX, GY, GZ))) return 0;
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
    vt.xyz -= grad;
    if (c.z == 0) vt.z = max(vt.z, 0);
    VelOut[id] = vt;
}
)HLSL";

// Sunlight reaching each cell through the soot and dust between it and the sun, with a share that
// went through more thinly, for the light scattered more than once.
const char *const LightPass = R"HLSL(
Texture3D<float2> Den : register(t0);
RWTexture3D<float> LightOut : register(u0);
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float tau = 0;
    [loop] for (int k = 1; k <= 16; ++k) {
        float3 q = float3(c) + SunToward.xyz * (k * 1.5);
        if (any(q < float3(-0.5, -0.5, -0.5)) || any(q > float3(GX - 0.5, GY - 0.5, GZ - 0.5))) break;
        float2 dd = Den.SampleLevel(Linear, uvwOf(q, slab), 0);
        tau += dd.x + dd.y;
    }
    tau *= 1.5 * SunToward.w * volume.z;
    LightOut[id] = exp(-tau) * 0.75 + exp(-tau * 0.25) * 0.25;
}
)HLSL";

const char *const MarchPass = R"HLSL(
Texture2D<float> SceneDepth : register(t0);
Texture3D<float2> Den : register(t1);
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
    return colour * smoothstep(0.5, 1.0, heat) * (1 + 2 * smoothstep(1.1, 1.8, heat));
}
float4 main(float4 position : SV_Position) : SV_Target
{
    float3 eye = Camera.xyz;
    float3 ray = worldAt(position.xy, SceneDepth.Load(int3(position.xy, 0))) - eye;
    float sceneDistance = length(ray);
    ray /= max(sceneDistance, 0.0001);
    float3 size = float3(GX, GY, GZ) * Box.w;
    float3 inverse = 1 / (abs(ray) > 1e-6 ? ray : 1e-6);
    float3 t0 = (Box.xyz - eye) * inverse, t1 = (Box.xyz + size - eye) * inverse;
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
        // the flash: light with nothing to absorb it, round the core for the first frames
        if (Effects.x > 0)
            color += transmittance * float3(1.0, 0.86, 0.62) * Effects.x * exp(-near2 / (Blast.w * Blast.w * 2))
                * stepLength / Box.w;
        float3 clamped = clamp(cell, float3(0, 0, 0), float3(GX - 1, GY - 1, GZ - 1));
        float3 uvw = float3((clamped.x + 0.5) / GX, (clamped.y + 0.5) / GY, (clamped.z + 0.5 + slabBase) / (GZ * SLABS));
        float2 dd = Den.SampleLevel(Linear, uvw, 0);
        float n = Noise.SampleLevel(Wrap, cell / 9 + float3(Slab.x * 0.37, 0, -Camera.w * 0.04), 0);
        // the condensation shell: white, thin, over the upper half, for a second or two
        float shell = 0;
        if (Effects.z > 0) {
            float r = sqrt(near2);
            float across = (r - Effects.y) / max(Effects.y * 0.1, 0.5);
            shell = Effects.z * exp(-across * across) * saturate(fromBlast.z / Effects.y + 0.4) * (0.5 + n);
        }
        float total = dd.x + dd.y + shell;
        if (total < 0.004) continue;
        // billows: the surface is eroded by the noise, the inside left whole
        float eroded = max(total - (1 - n) * 0.45 * (1 - saturate(total * 1.2)), 0);
        float sigma = eroded * walls(cell) * Slab.y * Slab.w;
        if (sigma <= 0) continue;
        float sun = Light.SampleLevel(Linear, uvw, 0);
        float heat = Vel.SampleLevel(Linear, uvw, 0).w;
        float height = saturate(cell.z / 24);
        float3 albedo = (Soot.rgb * dd.x + Dust.rgb * dd.y + float3(0.92, 0.93, 0.95) * shell) / total;
        float3 lit = albedo * tint * (0.26 + 0.12 * height + 0.75 * sun * phase);
        float3 source = (lit + fireColour(heat) * Soot.w * (dd.x / total)) * sigma;
        float through = exp(-sigma * stepLength);
        color += transmittance * (source - source * through) / sigma;
        transmittance *= through;
        if (transmittance < 0.01) break;
    }
    return float4(color, 1 - transmittance);
}
)HLSL";

enum Pass { CurlProgram, AdvectProgram, DivergenceProgram, JacobiProgram, ProjectProgram, LightProgram, ProgramCount };

struct Volume {
	bool live = false;
	bool reset = false;
	Kind kind = Shell;
	float scale = 1;					// a shell's size against the smallest
	float origin[3] = {};
	float cell = 1;
	float blast[3] = {};				// the detonation, in cells
	unsigned seed = 0;
	int born = 0;
	int life = 0;						// logic frames
	unsigned puffs = 0;
};

struct Tier {
	TierShape shape = {};
	ID3D11ComputeShader *compute[ProgramCount] = {};
	ID3D11PixelShader *march = nullptr;
	ID3D11Buffer *simConstants = nullptr;
	Field velocity[2], density[2], pressure[2], curl, divergence, light;
	unsigned current = 0;		// which density holds the last step's result
};

struct State {
	ID3D11Device *device = nullptr;
	bool refused = false;
	Tier tiers[2];
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

bool requested()
{
	static const bool on = findEarlyCommandLineOption(L"-volsmoke") != NULL;
	return on;
}

unsigned tierOf(unsigned slot) { return slot >= TierShapes[1].first ? 1 : 0; }
unsigned slabOf(unsigned slot) { return slot - TierShapes[tierOf(slot)].first; }

void releaseState()
{
	for (Tier &tier : state.tiers) {
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
	}
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

bool makeTier(DX11BackendClass *backend, ID3D11Device *device, Tier &tier, const TierShape &shape)
{
	tier.shape = shape;
	const std::string header = "#define GX " + std::to_string(shape.gx) + "\n#define GY " + std::to_string(shape.gy)
		+ "\n#define GZ " + std::to_string(shape.gz) + "\n#define SLABS " + std::to_string(shape.slabs) + "\n" + Grid;
	const std::string prelude = header + Prelude;
	static const char *const names[ProgramCount] = { "volsmoke curl", "volsmoke advect", "volsmoke divergence",
		"volsmoke jacobi", "volsmoke project", "volsmoke light" };
	const std::string sources[ProgramCount] = {
		prelude + CurlPass, prelude + AdvectPass + AdvectPassEnd, prelude + DivergencePass,
		prelude + PressureHelpers + JacobiPass, prelude + PressureHelpers + ProjectPass, prelude + LightPass };
	std::vector<unsigned char> code;
	bool made = true;
	for (unsigned pass = 0; made && pass < ProgramCount; ++pass)
		made = compile(backend, sources[pass], names[pass], "cs_5_0", code)
			&& SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &tier.compute[pass]));
	made = made && compile(backend, header + MarchPass, "volsmoke march", "ps_5_0", code)
		&& SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &tier.march));
	for (unsigned i = 0; made && i < 2; ++i)
		made = make(device, tier.velocity[i], VelocityFormat, shape) && make(device, tier.density[i], DensityFormat, shape)
			&& make(device, tier.pressure[i], ScalarFormat, shape);
	made = made && make(device, tier.curl, VelocityFormat, shape) && make(device, tier.divergence, ScalarFormat, shape)
		&& make(device, tier.light, ScalarFormat, shape);
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
	for (unsigned t = 0; made && t < 2; ++t) made = makeTier(backend, device, state.tiers[t], TierShapes[t]);
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
	DEBUG_LOG(("VOLSMOKE: programs and %u + %u volumes made in %.1f ms\n", TierShapes[0].slabs, TierShapes[1].slabs,
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
const bool ShellsOn = false, NuclearOn = false;		// the kinds that are done

TemplateKind kindOfTemplate(const ParticleSystemTemplate *tmplate, ParticleSystem *system)
{
	auto known = templateKinds.find(tmplate);
	if (known != templateKinds.end()) return known->second;
	const char *name = tmplate->getName().str();
	TemplateKind kind = { false, Shell, 1.0f };
	const bool alpha = system->getShaderType() == ParticleSystemInfo::ALPHA && !tmplate->isEmissive();
	if (!ShellsOn && !NuclearOn) {
		if (alpha && system->getPriority() == DEATH_EXPLOSION && std::strstr(name, "Smoke"))
			kind = { true, TankDeath, 1.0f };
	} else if (NuclearOn && std::strstr(name, "Mushroom") && !std::strstr(name, "NukeCannon"))
		kind = { true, Nuclear, 1.0f };
	else if (std::strstr(name, "NukeCannonMushroomStem"))
		kind = { true, Shell, 3.0f };
	else if (alpha && (std::strstr(name, "CarpetBombExplosionPuff") || std::strstr(name, "ScudMissleLauncherExplosionArms")))
		kind = { true, Shell, 2.4f };
	else if (alpha && std::strstr(name, "ArtilleryBarrageDust"))
		kind = { true, Shell, 1.6f };
	else if (alpha && std::strstr(name, "BuggyMissileExplosionSmoke"))
		kind = { true, Shell, 1.0f };
	else if (alpha && system->getPriority() == DEATH_EXPLOSION && std::strstr(name, "Smoke"))
		kind = { true, TankDeath, 1.0f };
	templateKinds[tmplate] = kind;
	return kind;
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
	shape[2] = 1000.0f;		// no ceiling
	unsigned count = 0;
	float flash = 0, shellRadius = 0, shellOpacity = 0;
	float soot[4] = { 0.06f, 0.055f, 0.05f, 1.6f }, dust[3] = { 0.52f, 0.43f, 0.31f };
	const float bx = volume.blast[0], by = volume.blast[1], bz = volume.blast[2];

	if (volume.kind == TankDeath) {
		// the hull's own small blast and dust skirt
		blast[3] = 2.2f;
		if (t < 0.25f) { power[0] = 18.0f * (1 - t / 0.25f); power[3] = 0.6f; }
		if (t < 0.4f) power[1] = 1.6f;
		if (t < 0.6f) power[2] = 1.3f;
		// the flame jet through the turret ring
		if (t < 1.1f)
			emit(slab, count, { bx, by, bz + 1.5f, 1.6f, 0, 0, 35.0f, 0.6f, 1.7f, 0, 0.5f });
		// the cook-off pops, two to four, each an orange puff with sparks
		const unsigned pops = 2 + unsigned(hashUnit(volume.seed, 900) * 2.99f);
		float when = 0.6f;
		for (unsigned p = 0; p < pops; ++p) {
			when += 0.5f + 1.0f * hashUnit(volume.seed, 901 + p);
			const float age = t - when;
			if (age < 0 || age > 0.9f) continue;
			const float ox = (hashUnit(volume.seed, 910 + p) - 0.5f) * 2.5f, oy = (hashUnit(volume.seed, 920 + p) - 0.5f) * 2.5f;
			if (age < 0.2f) emit(slab, count, { bx + ox, by + oy, bz + 1.0f, 2.8f, 0, 0, 12.0f, 12.0f, 1.6f, 0, 0.3f });
			Volume sparks = volume;
			sparks.seed = volume.seed + 31 * (p + 1);
			sparks.blast[0] += ox;
			sparks.blast[1] += oy;
			for (unsigned s = 0; s < 4; ++s) {
				float jet[6];
				if (jetAt(sparks, s, 30.0f, 0.3f, 1.1f, age, jet) && age < 0.7f)
					emit(slab, count, { jet[0], jet[1], jet[2], 1.0f, jet[3], jet[4], jet[5], 0.3f, 1.5f, 0, 0.2f });
			}
		}
		// the hull burning: a thick black column for half a minute
		if (t > 0.2f && t < 28.0f) {
			const float rise = (std::min)((t - 0.2f) / 1.0f, 1.0f), fall = t > 18.0f ? 1 - (t - 18.0f) / 10.0f : 1.0f;
			emit(slab, count, { bx, by, bz + 0.5f, 3.0f, 0, 0, 12.0f, 4.5f * rise * fall, 1.0f, 0.05f, 0.25f });
		}
	} else if (volume.kind == Shell || volume.kind == Airburst) {
		const bool ground = volume.kind == Shell;
		flash = t < 0.05f ? 10.0f : 0.0f;
		blast[3] = 2.0f;
		soot[0] = 0.10f; soot[1] = 0.095f; soot[2] = 0.09f; soot[3] = 1.2f;
		// the skirt along the ground, or for an airburst a patch of dust under it
		if (ground && t < 0.3f) { power[0] = 30.0f * (1 - t / 0.3f); power[3] = 0.9f; }
		if (!ground && t < 0.4f) emit(slab, count, { bx, by, 1.0f, 4.0f, 0, 0, 0, 0, 0, 2.5f, 0 });
		// the black-grey puff in the middle, rising slowly
		if (t < 0.15f) power[1] = 1.1f;
		if (t < 0.3f) power[2] = 1.1f;
		// the dirt fountain: a hollow cone of jets 10 to 20 m up, falling back in 3 to 6 s
		if (ground && t < 6.0f) {
			const float speed = std::sqrt(2.0f * Gravity * 50.0f * volume.scale) / volume.cell;
			for (unsigned j = 0; j < 10; ++j) {
				float jet[6];
				if (jetAt(volume, j, speed, 0.12f, 0.5f, t, jet))
					emit(slab, count, { jet[0], jet[1], jet[2], 1.3f, jet[3], jet[4], jet[5], 0, 0, 5.0f, 0.35f });
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
		// nuclear: the flash, the fireball rising fast and slowing, the condensation shell, the
		// skirt racing out, then the rolling cap on its stem, spreading once it stops
		const float rise = 1 - std::exp(-t / 7.0f);
		const float radius = 5.0f + 5.0f * (1 - std::exp(-t / 3.0f));
		blast[2] = bz + 42.0f * rise;
		blast[3] = radius;
		motion[0] = 42.0f / 7.0f * std::exp(-t / 7.0f);
		flash = t < 0.06f ? 40.0f : 40.0f * std::exp(-(t - 0.06f) / 0.15f);
		if (t < 4.0f) power[1] = 1.3f;
		power[2] = 2.0f * std::exp(-t / 5.0f);
		if (t < 4.0f) { power[0] = 70.0f * std::exp(-t / 1.5f); power[3] = 1.2f; }
		if (t > 0.3f && t < 2.2f) {
			shellRadius = radius * 1.7f;
			shellOpacity = 1.2f * std::sin((t - 0.3f) / 1.9f * 3.14159265f);
		}
		if (t > 0.8f) {
			shape[0] = 18.0f * std::exp(-t / 30.0f);
			shape[1] = radius * (0.9f + 0.03f * (std::max)(t - 10.0f, 0.0f));
			shape[1] = (std::min)(shape[1], 30.0f);
		}
		shape[2] = bz + 46.0f;
		if (t > 1.5f) shape[3] = 8.0f * std::exp(-t / 40.0f);
		// the ground dust the stem draws up
		if (t > 0.5f && t < 40.0f)
			for (unsigned k = 0; k < 5; ++k) {
				const float a = k * 1.2566371f, r = k == 0 ? 0.0f : 6.0f;
				emit(slab, count, { bx + r * std::cos(a), by + r * std::sin(a), 1.0f, 4.0f, 0, 0, 0, 0, 0, 1.5f, 0 });
			}
		const float grey = (std::min)(t / 25.0f, 1.0f);
		soot[0] = 0.42f + (0.70f - 0.42f) * grey;
		soot[1] = 0.24f + (0.68f - 0.24f) * grey;
		soot[2] = 0.14f + (0.65f - 0.14f) * grey;
		soot[3] = 2.2f;
		dust[0] = 0.50f; dust[1] = 0.38f; dust[2] = 0.26f;
	}
	sim.Volumes[slab][1] = (float)count;
	if (draw) {
		for (unsigned i = 0; i < 4; ++i) draw->Soot[i] = soot[i];
		for (unsigned i = 0; i < 3; ++i) draw->Dust[i] = dust[i];
		for (unsigned i = 0; i < 4; ++i) draw->Blast[i] = blast[i];
		draw->Effects[0] = flash;
		draw->Effects[1] = shellRadius;
		draw->Effects[2] = shellOpacity;
	}
}

void unbindCompute(ID3D11DeviceContext *context)
{
	ID3D11ShaderResourceView *const views[4] = {};
	ID3D11UnorderedAccessView *const writes[3] = {};
	context->CSSetShaderResources(0, 4, views);
	context->CSSetUnorderedAccessViews(0, 3, writes, nullptr);
}

void run(ID3D11DeviceContext *context, const Tier &tier, Pass pass, std::initializer_list<ID3D11ShaderResourceView *> reads,
	std::initializer_list<ID3D11UnorderedAccessView *> writes, unsigned slabs)
{
	unbindCompute(context);
	context->CSSetShaderResources(0, (UINT)reads.size(), reads.begin());
	context->CSSetUnorderedAccessViews(0, (UINT)writes.size(), writes.begin(), nullptr);
	context->CSSetShader(tier.compute[pass], nullptr, 0);
	context->Dispatch(tier.shape.gx / 8, tier.shape.gy / 8, slabs * tier.shape.gz / 8);
}

float secondsSince(const Volume &volume, int now) { return (float)(now - volume.born) / LOGICFRAMES_PER_SECOND; }

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
		if (!volume.live) continue;
		schedule(volume, slab, secondsSince(volume, now), nullptr);
		settle = DustSettle / volume.cell;	// ponytail: one settling speed a tier, its last volume's cell
	}
	sim.Step[2] = settle;
	upload(context, tier.simConstants, &sim, sizeof(sim));
	context->CSSetConstantBuffers(0, 1, &tier.simConstants);
	context->CSSetSamplers(0, 1, &state.linear);

	const unsigned in = tier.current, out = 1 - tier.current;
	run(context, tier, CurlProgram, { tier.velocity[0].view }, { tier.curl.write }, slabs);
	run(context, tier, AdvectProgram, { tier.velocity[0].view, tier.density[in].view, tier.curl.view },
		{ tier.velocity[1].write, tier.density[out].write, tier.pressure[0].write }, slabs);
	run(context, tier, DivergenceProgram, { tier.velocity[1].view }, { tier.divergence.write }, slabs);
	for (unsigned sweep = 0; sweep < JacobiSweeps; ++sweep) {
		const unsigned from = sweep & 1;
		run(context, tier, JacobiProgram, { tier.pressure[from].view, tier.divergence.view }, { tier.pressure[1 - from].write }, slabs);
	}
	run(context, tier, ProjectProgram, { tier.pressure[0].view, tier.velocity[1].view }, { tier.velocity[0].write }, slabs);
	run(context, tier, LightProgram, { tier.density[out].view }, { tier.light.write }, slabs);
	unbindCompute(context);
	tier.current = out;
	for (unsigned slab = 0; slab < tier.shape.slabs; ++slab) volumes[tier.shape.first + slab].reset = false;
}

// The box's pixels in the camera's viewport, false when none of it is in front of the camera.
bool screenRect(const Volume &volume, const TierShape &shape, const float viewProjection[16],
	const HazardCloud::Camera &camera, D3D11_RECT &rect)
{
	float low[2] = { 1e9f, 1e9f }, high[2] = { -1e9f, -1e9f };
	bool behind = false;
	for (unsigned corner = 0; corner < 8; ++corner) {
		const float world[4] = { volume.origin[0] + ((corner & 1) ? shape.gx : 0) * volume.cell,
			volume.origin[1] + ((corner & 2) ? shape.gy : 0) * volume.cell,
			volume.origin[2] + ((corner & 4) ? shape.gz : 0) * volume.cell, 1 };
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
float lifeOf(Kind kind) { return kind == TankDeath ? 36.0f : kind == Nuclear ? 90.0f : 12.0f; }
float cellOf(Kind kind, float scale) { return kind == TankDeath ? 3.0f : kind == Nuclear ? 10.0f : 2.0f * scale; }
float joinRadius(const Volume &volume) { return volume.kind == Nuclear ? 500.0f : volume.kind == TankDeath ? 60.0f : 30.0f * volume.scale; }
int rankOf(const Volume &volume) { return volume.kind == TankDeath ? 2 : volume.scale >= 2.0f ? 1 : 0; }

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
	return takenSystems.count((int)system->getSystemID()) != 0;
}

bool Takes(ParticleSystem *system)
{
	if (!requested() || !system || TheGlobalData->m_classicGraphics) return false;
	const int id = (int)system->getSystemID();
	if (takenSystems.count(id)) return true;
	if (!system->shouldBillboard() || system->isUsingStreak() || system->getVolumeParticleDepth() > 1
			|| system->getAttachedObject() != INVALID_ID || system->getAttachedDrawable() != INVALID_DRAWABLE_ID
			|| system->getPriority() == AREA_EFFECT)
		return false;
	const int now = (int)TheGameLogic->getFrame();
	const int seen = firstSeen.emplace(id, now).first->second;
	Coord3D where;
	system->getPosition(&where);

	// Another system of a blast already taken: it joins it if it started beside it, at its start.
	// A tank's and a shell's take smoke, dust and dirt and leave the fire cards; a nuclear blast
	// takes everything.  A system that starts a blast of its own joins only one of its own kind
	// (the nuke's several mushroom systems, a tank's two death smokes), so a tank dying under the
	// shell that killed it still gets a tank's smoke.
	const TemplateKind kind = kindOfTemplate(system->getTemplate(), system);
	const bool alpha = system->getShaderType() == ParticleSystemInfo::ALPHA && !system->getTemplate()->isEmissive();
	for (const Volume &volume : volumes) {
		if (!volume.live || seen < volume.born - 10 || seen > volume.born + (volume.kind == Nuclear ? 120 : 30)) continue;
		if (volume.kind != Nuclear && !alpha) continue;
		if (kind.trigger && (kind.kind == TankDeath) != (volume.kind == TankDeath)) continue;
		if (kind.trigger && (kind.kind == Nuclear) != (volume.kind == Nuclear)) continue;
		const float bx = volume.origin[0] + (volume.blast[0] + 0.5f) * volume.cell;
		const float by = volume.origin[1] + (volume.blast[1] + 0.5f) * volume.cell;
		const float dx = where.x - bx, dy = where.y - by, reach = joinRadius(volume);
		if (dx * dx + dy * dy > reach * reach) continue;
		pruneSets();
		takenSystems.insert(id);
		++joinedCount;
		return true;
	}

	if (!kind.trigger || system->isSystemForever() || seen < now - 5 || !usable()) return false;

	const Real ground = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(where.x, where.y, NULL) : where.z;
	Kind type = kind.kind;
	if (type == Shell && where.z - ground > 25.0f) type = Airburst;
	const unsigned tier = type == Nuclear ? 1 : 0;
	const TierShape &shape = TierShapes[tier];
	Volume fresh;
	fresh.kind = type;
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
	const float sideX = shape.gx * volume.cell, sideY = shape.gy * volume.cell;
	volume.origin[0] = where.x - sideX * 0.5f;
	volume.origin[1] = where.y - sideY * 0.5f;
	volume.origin[2] = (std::min)(where.z, ground) - volume.cell * 1.5f;
	volume.blast[0] = (shape.gx - 1) * 0.5f;
	volume.blast[1] = (shape.gy - 1) * 0.5f;
	// at the blast's height, but never under the ground's first cell: the ground turns its push
	volume.blast[2] = type == Airburst ? (where.z - volume.origin[2]) / volume.cell - 0.5f : 1.5f;
	volume.seed = unsigned(int(where.x) * 73856093) ^ unsigned(int(where.y) * 19349663) ^ unsigned(now * 83492791);
	volume.born = now;
	volume.life = int(lifeOf(type) * LOGICFRAMES_PER_SECOND);
	pruneSets();
	takenSystems.insert(id);
	if (claimedCount++ < 8)
		DEBUG_LOG(("VOLSMOKE: frame %d, %s volume %u for '%s' at (%.0f, %.0f, %.0f), %.0f units wide\n", now,
			type == TankDeath ? "tank" : type == Shell ? "shell" : type == Airburst ? "airburst" : "nuclear", slot,
			system->getTemplate()->getName().str(), where.x, where.y, where.z, sideX));
	return true;
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
	if (now - lastReport >= 10 * LOGICFRAMES_PER_SECOND && (live || claimedCount)) {
		lastReport = now;
		DEBUG_LOG(("VOLSMOKE: frame %d, %u volumes live, %u blasts, %u systems joined, %u dropped so far, %u draws\n",
			now, live, claimedCount, joinedCount, droppedCount, state.drawn));
	}
	if (!live) {
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
		if (!volume.live || volume.reset || !screenRect(volume, shape, viewProjection, camera, rects[slot])) continue;
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

	if (shown) {
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
		const float stepCells = StepCells * (std::max)(1.0f, shown / 3.0f);
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
