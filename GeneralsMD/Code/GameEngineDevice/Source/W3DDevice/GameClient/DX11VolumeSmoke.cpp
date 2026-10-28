// Explosion smoke as a fluid, behind -volsmoke, on the Direct3D 11 frame only.  Distributed under
// the repository's GPL-3.0 terms; see LICENSE.md.
//
// A death's smoke system (DEATH_EXPLOSION priority, "Smoke" in the template's name: a tank's or a
// building's plume) that VolumeSmoke::Takes claims gets a box of 32 x 32 x 64 cells over the spot,
// one slab of a shared 3D atlas, and stops drawing billboards.  Its particles still live
// on the client and are the sources: each logic frame the volume is stepped, every live particle
// stamps its density, velocity and heat into the grid as a soft ball, and the grid does the rest.
// One step is six compute passes over every slab at once: the curl of the velocity, a
// semi-Lagrangian advection with buoyancy, vorticity confinement and decay, the divergence, 14
// Jacobi sweeps of the pressure, the projection that makes the flow incompressible (what rolls a
// plume over at its head), and the light, which marches each cell towards the sun through the
// density.  The frame then raymarches each box into the scene against its depth, far box first,
// lit by that light, in the colour of the particles times their texture's mean, the heat glowing
// orange while it lasts, and eroded at the edges by a tiling noise so the 32-cell grid does not
// read as blobs.
//
// Nothing here is read by the logic or draws a random number: the noise is integer hashes.  The
// D3D9 and SDL3 paths, -headless and every run without the switch keep the billboards, and with
// the switch off Takes and Render return before touching anything.  Sixteen volumes at most; a
// system that finds none keeps its billboards, one whose particles have been gone a second gives
// its volume up to a new blast, and the march gets coarser as more are on screen.
#include "Common/GlobalData.h"
#include "Common/Debug.h"
#include "Common/EarlyCommandLine.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "WW3D2/texture.h"
#include "dx11texture.h"
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

const unsigned GridX = 32, GridY = 32, GridZ = 64;
const unsigned MaxVolumes = 16;		// slabs of the atlas, 64 cells deep each
const unsigned MaxPuffs = 24;
const unsigned JacobiSweeps = 14;		// even, so the pressure ends where it started
const unsigned NoiseSide = 32;
const int LingerFrames = 5 * LOGICFRAMES_PER_SECOND;	// after the last particle: the smoke thins out
const int FadeFrames = 2 * LOGICFRAMES_PER_SECOND;

// The look, in cells and seconds: what to tune.
const float Buoyancy = 9.0f;			// cells/s^2 of lift at heat 1
const float Weight = 1.0f;				// cells/s^2 of sink at density 1
const float Vorticity = 2.4f;
const float DensityHalfLife = 5.0f;
const float HeatHalfLife = 0.8f;
const float VelocityHalfLife = 3.0f;
const float Extinction = 0.16f;			// per world unit at density 1
const float StepCells = 0.85f;			// march step with up to three volumes on screen

const DXGI_FORMAT VelocityFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;	// xyz cells/s, w heat
const DXGI_FORMAT ScalarFormat = DXGI_FORMAT_R16_FLOAT;

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

bool make(ID3D11Device *device, Field &field, DXGI_FORMAT format, unsigned depth)
{
	D3D11_TEXTURE3D_DESC description = {};
	description.Width = GridX;
	description.Height = GridY;
	description.Depth = depth;
	description.MipLevels = 1;
	description.Format = format;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	return SUCCEEDED(device->CreateTexture3D(&description, nullptr, &field.texture))
		&& SUCCEEDED(device->CreateShaderResourceView(field.texture, nullptr, &field.view))
		&& SUCCEEDED(device->CreateUnorderedAccessView(field.texture, nullptr, &field.write));
}

// The pass constants.  Volumes: x live, y puffs, z world units a cell, w one on the step that
// starts it, so the slab's last tenant is not read.  Three float4 a puff: cell position and
// radius in cells; velocity in cells/s and strength; heat and how hard it pushes the flow.
struct SimConstants {
	float Step[4];			// dt seconds, seconds, unused, unused
	float Forces[4];		// buoyancy, weight, vorticity, unused
	float Keep[4];			// density, heat, velocity kept over this step
	float Sun[4];			// towards the sun, extinction per world unit
	float Volumes[MaxVolumes][4];
	float Puffs[MaxVolumes * MaxPuffs * 3][4];
};

struct DrawConstants {
	float WorldFromClip[16];
	float Camera[4];		// position, seconds
	float Viewport[4];		// as HazardCloud's: x + 0.5, y + 0.5, width, height
	float Box[4];			// world corner, world units a cell
	float Slab[4];			// slab, fade, march step in cells, extinction
	float Albedo[4];
	float SunColor[4];
	float Ambient[4];
	float SunDir[4];		// towards the sun
};

// Every pass shares the grid constants and the clamp that keeps a sample inside its own slab.
// What every program starts with: the grid, and the fade that keeps smoke off the box's walls, round
// about the column's axis so no square corner shows and thinning out under the lid.
const char *const Grid = R"HLSL(
#define GX 32
#define GY 32
#define GZ 64
float walls(float3 cell)
{
    float2 across = (cell.xy - float2(GX - 1, GY - 1) * 0.5) / (GX * 0.5 - 1);
    return (1 - smoothstep(0.55, 1.0, length(across))) * smoothstep(0, 16, GZ - 1 - cell.z);
}
)HLSL";

const char *const Prelude = R"HLSL(
#define PUFFS 24
cbuffer Sim : register(b0)
{
    float4 Step;
    float4 Forces;
    float4 Keep;
    float4 SunToward;
    float4 Volumes[SLABS];
    float4 Puffs[SLABS * PUFFS * 3];
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
uint hash(uint3 v)
{
    uint h = v.x * 73856093u ^ v.y * 19349663u ^ v.z * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
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

const char *const AdvectPass = R"HLSL(
Texture3D<float4> Vel : register(t0);
Texture3D<float> Den : register(t1);
Texture3D<float4> Curl : register(t2);
RWTexture3D<float4> VelOut : register(u0);
RWTexture3D<float> DenOut : register(u1);
RWTexture3D<float> PresOut : register(u2);
float w(int3 c, uint slab) { return Curl.Load(at(c, slab)).w; }
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float dt = Step.x;
    float4 vt = 0;
    float d = 0;
    if (volume.w > 0.5) {
        PresOut[id] = 0;
    } else {
        float3 back = float3(c) - Vel.Load(int4(id, 0)).xyz * dt;
        vt = Vel.SampleLevel(Linear, uvwOf(back, slab), 0);
        d = Den.SampleLevel(Linear, uvwOf(back, slab), 0);
        vt.z += dt * (Forces.x * vt.w - Forces.y * d);
        float3 grad = 0.5 * float3(w(c + int3(1, 0, 0), slab) - w(c - int3(1, 0, 0), slab),
            w(c + int3(0, 1, 0), slab) - w(c - int3(0, 1, 0), slab),
            w(c + int3(0, 0, 1), slab) - w(c - int3(0, 0, 1), slab));
        float length2 = dot(grad, grad);
        if (length2 > 1e-10)
            vt.xyz += dt * Forces.z * cross(grad * rsqrt(length2), Curl.Load(int4(id, 0)).xyz);
        vt.xyz *= Keep.z;
        vt.w *= Keep.y;
        d *= Keep.x;
    }
    // The particles: soft balls of smoke, pushing the flow their own way, hot while young, with a
    // little hashed jitter in the push for the confinement to turn into eddies.
    // Overlapping puffs add up, as their billboards' opacity did.
    uint count = (uint)volume.y;
    float stamped = 0;
    for (uint i = 0; i < count; ++i) {
        uint base = (slab * PUFFS + i) * 3;
        float4 where = Puffs[base];
        float4 motion = Puffs[base + 1];
        float4 heat = Puffs[base + 2];
        float3 offset = float3(c) - where.xyz;
        float r2 = dot(offset, offset) / (where.w * where.w);
        if (r2 >= 1) continue;
        float k = (1 - r2) * (1 - r2);
        stamped += motion.w * k;
        uint h = hash(uint3(id.xy, id.z + (uint)(Step.y * 30) * 4099u));
        float3 jitter = float3(h & 1023, (h >> 10) & 1023, (h >> 20) & 1023) / 511.5 - 1;
        vt.xyz = lerp(vt.xyz, motion.xyz + jitter * 6 * heat.x, k * heat.y);
        vt.w = max(vt.w, heat.x * k);
    }
    d = max(d, min(stamped, 3));
    d *= lerp(0.92, 1, walls(float3(c)));	// smoke that drifts to the walls thins out there
    // Open sides and top: smoke that reaches them is gone.  The ground lets nothing through.
    if (c.x == 0 || c.y == 0 || c.x == GX - 1 || c.y == GY - 1 || c.z == GZ - 1) { d = 0; vt.w = 0; }
    if (c.z == 0) vt.z = max(vt.z, 0);
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
    DivOut[id] = 0.5 * (v(c + int3(1, 0, 0), slab).x - v(c - int3(1, 0, 0), slab).x
        + v(c + int3(0, 1, 0), slab).y - v(c - int3(0, 1, 0), slab).y
        + v(c + int3(0, 0, 1), slab).z - below);
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

// Sunlight reaching each cell through the smoke between it and the sun, with a share that went
// through more thinly, for the light scattered more than once.
const char *const LightPass = R"HLSL(
Texture3D<float> Den : register(t0);
RWTexture3D<float> LightOut : register(u0);
[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    CELL_HEADER
    float tau = 0;
    [loop] for (int k = 1; k <= 16; ++k) {
        float3 q = float3(c) + SunToward.xyz * (k * 1.5);
        if (any(q < float3(-0.5, -0.5, -0.5)) || any(q > float3(GX - 0.5, GY - 0.5, GZ - 0.5))) break;
        tau += Den.SampleLevel(Linear, uvwOf(q, slab), 0);
    }
    tau *= 1.5 * SunToward.w * volume.z;
    LightOut[id] = exp(-tau) * 0.75 + exp(-tau * 0.25) * 0.25;
}
)HLSL";

// The colour a billboard of the particle texture shows, its alpha-weighted mean over a 64 x 64
// grid, into the slot's entry: the particles' keyframe colour alone was cream for smoke whose
// texture is dark grey.
const char *const MeanPass = R"HLSL(
Texture2D<float4> Picture : register(t0);
SamplerState Linear : register(s0);
RWBuffer<float4> MeanOut : register(u0);
cbuffer Mean : register(b0) { uint4 Slot; };
groupshared float4 sums[64];
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 sum = 0;
    for (uint y = 0; y < 64; ++y) {
        float4 texel = Picture.SampleLevel(Linear, (float2(id.x, y) + 0.5) / 64, 0);
        sum += float4(texel.rgb * texel.a, texel.a);
    }
    sums[id.x] = sum;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32; stride > 0; stride >>= 1) {
        if (id.x < stride) sums[id.x] += sums[id.x + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (id.x == 0) MeanOut[Slot.x] = float4(sums[0].a > 0.001 ? sums[0].rgb / sums[0].a : float3(0.5, 0.5, 0.5), 1);
}
)HLSL";

const char *const MarchPass = R"HLSL(
Texture2D<float> SceneDepth : register(t0);
Texture3D<float> Den : register(t1);
Texture3D<float> Light : register(t2);
Texture3D<float4> Vel : register(t3);
Texture3D<float> Noise : register(t4);
Buffer<float4> Means : register(t5);
SamplerState Linear : register(s0);
SamplerState Wrap : register(s1);
cbuffer Draw : register(b0)
{
    row_major float4x4 WorldFromClip;
    float4 Camera;
    float4 Viewport;
    float4 Box;
    float4 Slab;
    float4 Albedo;
    float4 SunColor;
    float4 Ambient;
    float4 SunDir;
};
float3 worldAt(float2 pixel, float depth)
{
    float2 uv = (pixel - Viewport.xy) / max(Viewport.zw, float2(1, 1));
    float4 world = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1), WorldFromClip);
    float w = abs(world.w) < 0.000001 ? (world.w < 0 ? -0.000001 : 0.000001) : world.w;
    return world.xyz / w;
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
    uint steps = (uint)clamp(ceil(span / (Box.w * Slab.z)), 1, 96);
    float stepLength = span / steps;
    float jitter = frac(52.9829189 * frac(dot(position.xy, float2(0.06711056, 0.00583715)) + Camera.w * 7.31));
    float cosine = dot(ray, SunDir.xyz);
    float g = 0.35;
    float phase = lerp(1, (1 - g * g) / pow(abs(1 + g * g - 2 * g * cosine), 1.5), 0.6);
    float3 light = SunColor.rgb + Ambient.rgb;
    float3 tint = light / max(dot(light, float3(0.299, 0.587, 0.114)), 0.01);
    float3 paint = Albedo.rgb * (Albedo.w > 0.5 ? Means[(uint)Slab.x].rgb : float3(0.5, 0.5, 0.5));
    float3 color = 0;
    float transmittance = 1;
    float slabBase = Slab.x * GZ;
    [loop] for (uint i = 0; i < steps; ++i) {
        float3 cell = (eye + ray * (enter + (i + jitter) * stepLength) - Box.xyz) / Box.w - 0.5;
        float3 clamped = clamp(cell, float3(0, 0, 0), float3(GX - 1, GY - 1, GZ - 1));
        float3 uvw = float3((clamped.x + 0.5) / GX, (clamped.y + 0.5) / GY, (clamped.z + 0.5 + slabBase) / (GZ * SLABS));
        float d = Den.SampleLevel(Linear, uvw, 0);
        if (d < 0.004) continue;
        float n = Noise.SampleLevel(Wrap, cell / 13 + float3(Slab.x * 0.37, 0, -Camera.w * 0.06), 0);
        d = max(d - (1 - n) * 0.3 * (1 - saturate(d * 1.4)), 0);
        float edge = walls(cell);
        float sigma = d * edge * Slab.y * Slab.w;
        if (sigma <= 0) continue;
        float sun = Light.SampleLevel(Linear, uvw, 0);
        float heat = saturate(Vel.SampleLevel(Linear, uvw, 0).w);
        // The billboards' own colour on the sunlit side, a third of it in the smoke's own shadow:
        // the map's light sets the hue only, so the volume is no brighter than the smoke it replaced.
        float height = saturate(cell.z / 24);
        float3 lit = paint * tint * (0.26 + 0.12 * height + 0.75 * sun * phase);
        float hot = saturate((heat - 0.35) / 0.65);
        float3 glow = float3(1.0, 0.36, 0.08) * hot * hot * 3;
        float3 source = (lit + glow) * sigma;
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
	ParticleSystemID system = INVALID_PARTICLE_SYSTEM_ID;
	bool live = false;
	bool reset = false;
	float origin[3] = {};
	float cell = 1;
	float albedo[3] = { 0.6f, 0.6f, 0.6f };
	int born = 0;
	int lastSeen = 0;
	unsigned puffs = 0;
	TextureClass *picture = nullptr;	// the particle texture, held until its mean is taken
	bool meanKnown = false;
};

void dropPicture(Volume &volume)
{
	if (volume.picture) volume.picture->Release_Ref();
	volume.picture = nullptr;
}

struct State {
	ID3D11Device *device = nullptr;
	bool refused = false;
	ID3D11ComputeShader *compute[ProgramCount] = {};
	ID3D11ComputeShader *mean = nullptr;
	ID3D11Buffer *means = nullptr, *meanConstants = nullptr;
	ID3D11ShaderResourceView *meansView = nullptr;
	ID3D11UnorderedAccessView *meansWrite = nullptr;
	ID3D11VertexShader *fullscreen = nullptr;
	ID3D11PixelShader *march = nullptr;
	ID3D11SamplerState *linear = nullptr, *wrap = nullptr;
	ID3D11BlendState *premultiplied = nullptr;
	ID3D11RasterizerState *rasterizer = nullptr;
	ID3D11Buffer *simConstants = nullptr, *drawConstants = nullptr;
	Field velocity[2], density[2], pressure[2], curl, divergence, light;
	ID3D11Texture3D *noise = nullptr;
	ID3D11ShaderResourceView *noiseView = nullptr;
	unsigned current = 0;		// which density holds the last step's result
	int lastStepFrame = -1;
	unsigned drawn = 0;
};

State state;
Volume volumes[MaxVolumes];
SimConstants sim;
std::unordered_map<const ParticleSystemTemplate *, bool> templateKinds;
std::unordered_set<int> refusedSystems;	// found every volume taken: billboards for life
unsigned claimedCount = 0, refusedCount = 0;
int lastReport = 0;
float lastViewProjection[16];		// the main view's, from the last Render
bool lastViewKnown = false;

// Inside the last main view, with a margin for the plume that rises out of a blast at its edge.
bool onScreen(const Coord3D &where)
{
	if (!lastViewKnown) return false;
	const float world[4] = { where.x, where.y, where.z, 1 };
	float clip[4] = {};
	for (unsigned column = 0; column < 4; ++column)
		for (unsigned k = 0; k < 4; ++k) clip[column] += world[k] * lastViewProjection[k * 4 + column];
	return clip[3] > 1.0f && std::fabs(clip[0]) <= clip[3] * 1.3f && std::fabs(clip[1]) <= clip[3] * 1.6f;
}

bool requested()
{
	static const bool on = findEarlyCommandLineOption(L"-volsmoke") != NULL;
	return on;
}

void releaseState()
{
	for (ID3D11ComputeShader *&program : state.compute) drop(program);
	drop(state.mean);
	drop(state.meansWrite);
	drop(state.meansView);
	drop(state.means);
	drop(state.meanConstants);
	drop(state.fullscreen);
	drop(state.march);
	drop(state.linear);
	drop(state.wrap);
	drop(state.premultiplied);
	drop(state.rasterizer);
	drop(state.simConstants);
	drop(state.drawConstants);
	for (unsigned i = 0; i < 2; ++i) {
		drop(state.velocity[i]);
		drop(state.density[i]);
		drop(state.pressure[i]);
	}
	drop(state.curl);
	drop(state.divergence);
	drop(state.light);
	drop(state.noiseView);
	drop(state.noise);
	state = State();
	for (Volume &volume : volumes) {
		dropPicture(volume);
		volume = Volume();
	}
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

bool makeState(DX11BackendClass *backend, ID3D11Device *device)
{
	if (state.device == device) return !state.refused;
	const double started = DX11Resource_Milliseconds_Now();
	releaseState();
	state.device = device;
	bool made = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
	UINT support = 0;
	const UINT wanted = D3D11_FORMAT_SUPPORT_TEXTURE3D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE
		| D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
	made = made && SUCCEEDED(device->CheckFormatSupport(VelocityFormat, &support)) && (support & wanted) == wanted;
	made = made && SUCCEEDED(device->CheckFormatSupport(ScalarFormat, &support)) && (support & wanted) == wanted;

	static const char *const names[ProgramCount] = { "volsmoke curl", "volsmoke advect", "volsmoke divergence",
		"volsmoke jacobi", "volsmoke project", "volsmoke light" };
	const std::string slabs = "#define SLABS " + std::to_string(MaxVolumes) + "\n" + Grid;
	const std::string prelude = slabs + Prelude;
	const std::string sources[ProgramCount] = {
		prelude + CurlPass, prelude + AdvectPass, prelude + DivergencePass, prelude + PressureHelpers + JacobiPass,
		prelude + PressureHelpers + ProjectPass, prelude + LightPass };
	std::vector<unsigned char> code;
	for (unsigned pass = 0; made && pass < ProgramCount; ++pass)
		made = compile(backend, sources[pass], names[pass], "cs_5_0", code)
			&& SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &state.compute[pass]));
	made = made && compile(backend, MeanPass, "volsmoke mean", "cs_5_0", code)
		&& SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &state.mean));
	made = made && compile(backend, HazardCloud::vertexShader(), "volsmoke fullscreen", "vs_5_0", code)
		&& SUCCEEDED(device->CreateVertexShader(code.data(), code.size(), nullptr, &state.fullscreen));
	made = made && compile(backend, slabs + MarchPass, "volsmoke march", "ps_5_0", code)
		&& SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &state.march));

	const unsigned atlas = GridZ * MaxVolumes;
	for (unsigned i = 0; made && i < 2; ++i)
		made = make(device, state.velocity[i], VelocityFormat, atlas) && make(device, state.density[i], ScalarFormat, atlas)
			&& make(device, state.pressure[i], ScalarFormat, atlas);
	made = made && make(device, state.curl, VelocityFormat, atlas) && make(device, state.divergence, ScalarFormat, atlas)
		&& make(device, state.light, ScalarFormat, atlas);

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
	buffer.ByteWidth = sizeof(SimConstants);
	made = made && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &state.simConstants));
	buffer.ByteWidth = sizeof(DrawConstants);
	made = made && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &state.drawConstants));
	buffer.ByteWidth = 16;
	made = made && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &state.meanConstants));
	D3D11_BUFFER_DESC meanBuffer = {};
	meanBuffer.Usage = D3D11_USAGE_DEFAULT;
	meanBuffer.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	meanBuffer.ByteWidth = MaxVolumes * 16;
	made = made && SUCCEEDED(device->CreateBuffer(&meanBuffer, nullptr, &state.means));
	D3D11_SHADER_RESOURCE_VIEW_DESC meanView = {};
	meanView.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	meanView.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	meanView.Buffer.NumElements = MaxVolumes;
	made = made && SUCCEEDED(device->CreateShaderResourceView(state.means, &meanView, &state.meansView));
	D3D11_UNORDERED_ACCESS_VIEW_DESC meanWrite = {};
	meanWrite.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	meanWrite.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	meanWrite.Buffer.NumElements = MaxVolumes;
	made = made && SUCCEEDED(device->CreateUnorderedAccessView(state.means, &meanWrite, &state.meansWrite));

	if (!made) {
		DEBUG_LOG(("VOLSMOKE: the Direct3D 11 device refused the smoke volumes; billboards stay\n"));
		releaseState();
		state.device = device;
		state.refused = true;
		return false;
	}
	DEBUG_LOG(("VOLSMOKE: programs and %u volumes made in %.1f ms\n", MaxVolumes,
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

bool explosionSmokeTemplate(const ParticleSystemTemplate *tmplate)
{
	auto known = templateKinds.find(tmplate);
	if (known != templateKinds.end()) return known->second;
	const bool smoke = !tmplate->isEmissive() && std::strstr(tmplate->getName().str(), "Smoke");
	templateKinds[tmplate] = smoke;
	return smoke;
}

// The particles of a volume's system, the first MaxPuffs of them spread over the list, into its
// share of the pass constants.
void gatherPuffs(unsigned slot, Volume &volume, ParticleSystem *system)
{
	const Int count = (Int)system->getParticleCount();
	const Int stride = count > (Int)MaxPuffs ? (count + MaxPuffs - 1) / MaxPuffs : 1;
	const float fade = system->getStealthFade();
	float color[3] = {};
	unsigned taken = 0;
	Int index = 0;
	for (Particle *p = system->getFirstParticle(); p && taken < MaxPuffs; p = p->m_systemNext, ++index) {
		if (index % stride) continue;
		const Coord3D *pos = p->getPosition();
		float *puff = sim.Puffs[(slot * MaxPuffs + taken) * 3];
		puff[0] = (pos->x - volume.origin[0]) / volume.cell - 0.5f;
		puff[1] = (pos->y - volume.origin[1]) / volume.cell - 0.5f;
		puff[2] = (pos->z - volume.origin[2]) / volume.cell - 0.5f;
		// A puff that has grown past six cells (SizeRate swells some to hundreds of units) stamps
		// no wider: spreading it is the flow's job, and wider stamps fill the box to its walls.
		puff[3] = (std::min)((std::max)(p->getSize() * 0.42f / volume.cell, 1.3f), 6.0f);
		const float perSecond = (float)LOGICFRAMES_PER_SECOND / volume.cell;
		puff[4] = p->m_vel.x * perSecond;
		puff[5] = p->m_vel.y * perSecond;
		puff[6] = p->m_vel.z * perSecond;
		// As opaque through its middle as the billboard was, so a big faint puff is a thin ball and
		// not a solid one: optical depth -ln(1 - alpha) over the ball's diameter, whose (1 - r^2)^2
		// falloff averages 0.53 along a line through the centre.
		const float alpha = (std::min)(p->getAlpha() * fade, 0.95f);
		const float diameter = 2.0f * puff[3] * volume.cell;
		puff[7] = (std::min)(-std::log(1.0f - alpha) / (Extinction * diameter * 0.53f), 1.5f);
		const float life = p->m_lifetime ? (float)p->getLifetimeLeft() / (float)p->m_lifetime : 0.0f;
		puff[8] = 1.2f * life * life * life * life;	// hot only while young
		puff[9] = 0.35f;
		puff[10] = puff[11] = 0;
		const RGBColor *tint = p->getColor();
		color[0] += tint->red;
		color[1] += tint->green;
		color[2] += tint->blue;
		++taken;
	}
	volume.puffs = taken;
	const float follow = volume.reset ? 1.0f : 0.2f;	// the first step takes the particles' colour as it is
	if (taken)
		for (unsigned i = 0; i < 3; ++i)
			volume.albedo[i] += ((std::max)(color[i] / taken, 0.08f) - volume.albedo[i]) * follow;
}

void unbindCompute(ID3D11DeviceContext *context)
{
	ID3D11ShaderResourceView *const views[4] = {};
	ID3D11UnorderedAccessView *const writes[3] = {};
	context->CSSetShaderResources(0, 4, views);
	context->CSSetUnorderedAccessViews(0, 3, writes, nullptr);
}

void run(ID3D11DeviceContext *context, Pass pass, std::initializer_list<ID3D11ShaderResourceView *> reads,
	std::initializer_list<ID3D11UnorderedAccessView *> writes, unsigned slabs)
{
	unbindCompute(context);
	context->CSSetShaderResources(0, (UINT)reads.size(), reads.begin());
	context->CSSetUnorderedAccessViews(0, (UINT)writes.size(), writes.begin(), nullptr);
	context->CSSetShader(state.compute[pass], nullptr, 0);
	context->Dispatch(GridX / 8, GridY / 8, slabs * GridZ / 8);
}

// Each new volume's texture colour, once; a texture with no Direct3D 11 view keeps grey.
void takeMeans(ID3D11Device *device, ID3D11DeviceContext *context)
{
	for (unsigned slot = 0; slot < MaxVolumes; ++slot) {
		Volume &volume = volumes[slot];
		if (!volume.live || !volume.picture) continue;
		// a texture only death smoke uses was never applied, as its billboards never drew
		if (!volume.picture->Is_Initialized()) volume.picture->Init();
		IDirect3DBaseTexture9 *texture = volume.picture->Peek_D3D_Base_Texture();
		ID3D11ShaderResourceView *view = texture ? DX11Texture_Mirror(device, context, texture) : nullptr;
		if (view) {
			const unsigned constants[4] = { slot, 0, 0, 0 };
			upload(context, state.meanConstants, constants, sizeof(constants));
			unbindCompute(context);
			context->CSSetShaderResources(0, 1, &view);
			context->CSSetUnorderedAccessViews(0, 1, &state.meansWrite, nullptr);
			context->CSSetConstantBuffers(0, 1, &state.meanConstants);
			context->CSSetSamplers(0, 1, &state.linear);
			context->CSSetShader(state.mean, nullptr, 0);
			context->Dispatch(1, 1, 1);
			volume.meanKnown = true;
		}
		dropPicture(volume);
	}
	unbindCompute(context);
}

void step(ID3D11DeviceContext *context, float dt, float seconds, const float sunToward[3], unsigned slabs)
{
	sim.Step[0] = dt;
	sim.Step[1] = seconds;
	sim.Forces[0] = Buoyancy;
	sim.Forces[1] = Weight;
	sim.Forces[2] = Vorticity;
	sim.Keep[0] = std::pow(0.5f, dt / DensityHalfLife);
	sim.Keep[1] = std::pow(0.5f, dt / HeatHalfLife);
	sim.Keep[2] = std::pow(0.5f, dt / VelocityHalfLife);
	for (unsigned i = 0; i < 3; ++i) sim.Sun[i] = sunToward[i];
	sim.Sun[3] = Extinction;
	for (unsigned slot = 0; slot < MaxVolumes; ++slot) {
		const Volume &volume = volumes[slot];
		sim.Volumes[slot][0] = volume.live ? 1.0f : 0.0f;
		sim.Volumes[slot][1] = (float)volume.puffs;
		sim.Volumes[slot][2] = volume.cell;
		sim.Volumes[slot][3] = volume.reset ? 1.0f : 0.0f;
	}
	upload(context, state.simConstants, &sim, sizeof(sim));
	context->CSSetConstantBuffers(0, 1, &state.simConstants);
	context->CSSetSamplers(0, 1, &state.linear);

	const unsigned in = state.current, out = 1 - state.current;
	run(context, CurlProgram, { state.velocity[0].view }, { state.curl.write }, slabs);
	run(context, AdvectProgram, { state.velocity[0].view, state.density[in].view, state.curl.view },
		{ state.velocity[1].write, state.density[out].write, state.pressure[0].write }, slabs);
	run(context, DivergenceProgram, { state.velocity[1].view }, { state.divergence.write }, slabs);
	for (unsigned sweep = 0; sweep < JacobiSweeps; ++sweep) {
		const unsigned from = sweep & 1;
		run(context, JacobiProgram, { state.pressure[from].view, state.divergence.view }, { state.pressure[1 - from].write }, slabs);
	}
	run(context, ProjectProgram, { state.pressure[0].view, state.velocity[1].view }, { state.velocity[0].write }, slabs);
	run(context, LightProgram, { state.density[out].view }, { state.light.write }, slabs);
	unbindCompute(context);
	state.current = out;
	for (Volume &volume : volumes) volume.reset = false;
}

// The box's pixels in the camera's viewport, false when none of it is in front of the camera.
bool screenRect(const Volume &volume, const float viewProjection[16], const HazardCloud::Camera &camera, D3D11_RECT &rect)
{
	float low[2] = { 1e9f, 1e9f }, high[2] = { -1e9f, -1e9f };
	bool behind = false;
	for (unsigned corner = 0; corner < 8; ++corner) {
		const float world[4] = { volume.origin[0] + ((corner & 1) ? GridX : 0) * volume.cell,
			volume.origin[1] + ((corner & 2) ? GridY : 0) * volume.cell,
			volume.origin[2] + ((corner & 4) ? GridZ : 0) * volume.cell, 1 };
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

} // namespace

namespace VolumeSmoke {

bool Takes(ParticleSystem *system)
{
	if (!requested() || !system || TheGlobalData->m_classicGraphics) return false;
	const ParticleSystemID id = system->getSystemID();
	for (const Volume &volume : volumes)
		if (volume.live && volume.system == id) return true;
	// A death's smoke (a tank's, a building's): the shell impacts' small puffs would take every
	// volume in a tank fight and leave the plumes on billboards.
	if (system->getPriority() != DEATH_EXPLOSION
			|| system->getShaderType() != ParticleSystemInfo::ALPHA || !system->shouldBillboard() || system->isUsingStreak()
			|| system->getVolumeParticleDepth() > 1 || system->isSystemForever() || system->getGpuSampleCount() != 0
			|| system->getAttachedObject() != INVALID_ID || system->getAttachedDrawable() != INVALID_DRAWABLE_ID
			|| !explosionSmokeTemplate(system->getTemplate()))
		return false;
	if (refusedSystems.count((int)id)) return false;
	// Off screen it keeps its billboards and is asked again next frame, so a death across the map
	// does not take a volume from the one in view.
	Coord3D where;
	system->getPosition(&where);
	if (!onScreen(where)) return false;
	if (!usable()) return false;

	unsigned slot = 0;
	while (slot < MaxVolumes && volumes[slot].live) ++slot;
	// None free: the one whose particles have been gone longest gives way, a second at least, as it
	// is thin smoke by then.  A fresh blast is worth more than the end of an old one.
	const int now = (int)TheGameLogic->getFrame();
	if (slot == MaxVolumes)
		for (unsigned i = 0; i < MaxVolumes; ++i)
			if (now - volumes[i].lastSeen > LOGICFRAMES_PER_SECOND
					&& (slot == MaxVolumes || volumes[i].lastSeen < volumes[slot].lastSeen))
				slot = i;
	if (slot == MaxVolumes) {
		// ponytail: the set only grows until 512, then forgets the systems that are gone
		if (refusedSystems.size() > 512)
			for (auto it = refusedSystems.begin(); it != refusedSystems.end();)
				it = TheParticleSystemManager->findParticleSystem((ParticleSystemID)*it) ? std::next(it) : refusedSystems.erase(it);
		refusedSystems.insert((int)id);
		++refusedCount;
		return false;
	}

	// The box from what the template's particles grow to: three and a half times the full width
	// of the biggest one, twice that high, standing on the ground under the blast.
	const Real start = system->m_startSize.getMaximumValue() * system->getSizeMultiplier();
	const Real rate = system->m_sizeRate.getMaximumValue() * system->getSizeMultiplier();
	const Real damping = system->m_sizeRateDamping.getMaximumValue();
	const Real life = system->m_lifetime.getMaximumValue();
	const Real grown = damping < 0.999f ? (std::min)(rate / (1.0f - damping), rate * life) : rate * life;
	// ponytail: past 160 units a cell would be coarser than the young puffs it has to hold, so a
	// building-sized plume is cut by its box; a finer grid or a second, bigger one if that shows.
	const Real side = (std::max)(48.0f, (std::min)(160.0f, (start + (std::max)(grown, 0.0f)) * 3.5f));
	const Real ground = TheTerrainRenderObject ? TheTerrainRenderObject->getHeightMapHeight(where.x, where.y, NULL) : where.z;

	Volume &volume = volumes[slot];
	dropPicture(volume);
	volume = Volume();
	if (W3DDisplay::m_assetManager)
		volume.picture = W3DDisplay::m_assetManager->Get_Texture(system->getParticleTypeName().str());
	volume.system = id;
	volume.live = true;
	volume.reset = true;
	volume.cell = side / GridX;
	volume.origin[0] = where.x - side * 0.5f;
	volume.origin[1] = where.y - side * 0.5f;
	volume.origin[2] = (std::min)(where.z, ground) - volume.cell * 1.5f;
	volume.born = volume.lastSeen = (int)TheGameLogic->getFrame();
	if (claimedCount++ == 0)
		DEBUG_LOG(("VOLSMOKE: first volume, '%s' at (%.0f, %.0f, %.0f), %.0f units wide\n",
			system->getTemplate()->getName().str(), where.x, where.y, where.z, side));
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
	// The camera's matrices, as HazardCloud's layers take them from it; Takes culls with them too.
	float viewProjection[16], eyeRows[16];
	HazardCloud::multiply(camera.view, camera.projection, viewProjection);
	std::memcpy(lastViewProjection, viewProjection, sizeof(viewProjection));
	lastViewKnown = true;

	const int now = (int)TheGameLogic->getFrame();
	unsigned live = 0, slabs = 0;
	for (unsigned slot = 0; slot < MaxVolumes; ++slot) {
		Volume &volume = volumes[slot];
		if (!volume.live) continue;
		ParticleSystem *system = TheParticleSystemManager->findParticleSystem(volume.system);
		if (system && system->getParticleCount() > 0) volume.lastSeen = now;
		if (now - volume.lastSeen > LingerFrames || now < volume.born) {
			volume.live = false;
			dropPicture(volume);
			continue;
		}
		if (state.lastStepFrame != now) {
			if (system) gatherPuffs(slot, volume, system);
			else volume.puffs = 0;
		}
		++live;
		slabs = slot + 1;
	}
	if (now - lastReport >= 10 * LOGICFRAMES_PER_SECOND && (live || claimedCount)) {
		lastReport = now;
		DEBUG_LOG(("VOLSMOKE: frame %d, %u volumes live, %u claimed and %u left on billboards so far, %u draws\n",
			now, live, claimedCount, refusedCount, state.drawn));
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

	takeMeans(device->Get_Device(), context);
	if (state.lastStepFrame != now) {
		const int frames = state.lastStepFrame < 0 ? 1 : (std::min)((std::max)(now - state.lastStepFrame, 1), 4);
		step(context, (float)frames / LOGICFRAMES_PER_SECOND, seconds, toward, slabs);
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
	for (unsigned slot = 0; slot < slabs; ++slot) {
		const Volume &volume = volumes[slot];
		// a slab claimed after this frame's step still holds its last tenant
		if (!volume.live || volume.reset || !screenRect(volume, viewProjection, camera, rects[slot])) continue;
		float squared = 0;
		for (unsigned i = 0; i < 3; ++i) {
			const float centre = volume.origin[i] + volume.cell * (i == 2 ? GridZ : GridX) * 0.5f - draw.Camera[i];
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
		ID3D11ShaderResourceView *const views[6] = { depth, state.density[state.current].view, state.light.view,
			state.velocity[0].view, state.noiseView, state.meansView };
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(state.fullscreen, nullptr, 0);
		context->RSSetState(state.rasterizer);
		context->RSSetViewports(1, &sceneViewport);
		context->OMSetDepthStencilState(nullptr, 0);
		context->OMSetRenderTargets(1, &scene, nullptr);
		context->OMSetBlendState(state.premultiplied, nullptr, 0xffffffff);
		context->PSSetShader(state.march, nullptr, 0);
		context->PSSetSamplers(0, 2, samplers);
		context->PSSetShaderResources(0, 6, views);
		context->PSSetConstantBuffers(0, 1, &state.drawConstants);
		// the budget: past three volumes on screen each one is marched in longer steps
		const float stepCells = StepCells * (std::max)(1.0f, shown / 3.0f);
		for (unsigned n = 0; n < shown; ++n) {
			const unsigned slot = order[n];
			const Volume &volume = volumes[slot];
			for (unsigned i = 0; i < 3; ++i) {
				draw.Box[i] = volume.origin[i];
				draw.Albedo[i] = volume.albedo[i];
			}
			draw.Box[3] = volume.cell;
			draw.Albedo[3] = volume.meanKnown ? 1.0f : 0.0f;
			const int quiet = now - volume.lastSeen;
			draw.Slab[0] = (float)slot;
			draw.Slab[1] = quiet > LingerFrames - FadeFrames
				? (std::max)(0.0f, (float)(LingerFrames - quiet) / FadeFrames) : 1.0f;
			draw.Slab[2] = stepCells;
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
	if (!requested()) return;
	for (Volume &volume : volumes) {
		dropPicture(volume);
		volume = Volume();
	}
	refusedSystems.clear();
	state.lastStepFrame = -1;
	lastReport = 0;
}

void ReleaseDevice()
{
	releaseState();
}

} // namespace VolumeSmoke
