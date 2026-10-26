#pragma once
// Render-only admission for replacing authored .wak surf. No texture-name inference.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace WaterShoreOverlay {
constexpr unsigned MaxRegions = 128;
struct Quad { float xy[8] = {}; float level = 0; };
struct View {
    std::uintptr_t colour = 0, depth = 0;
    std::uint64_t capture = 0;
    unsigned width = 0, height = 0;
    float worldFromClip[16] = {}, viewport[4] = {};
};
inline bool SameView(const View &a, const View &b) {
    for(float f:a.worldFromClip) if(!std::isfinite(f)) return false;
    for(float f:a.viewport) if(!std::isfinite(f)) return false;
    if(a.viewport[2]<=0||a.viewport[3]<=0) return false;
    return a.colour && a.depth && a.capture && a.width && a.height &&
        a.colour==b.colour && a.depth==b.depth && a.capture==b.capture &&
        a.width==b.width && a.height==b.height &&
        std::memcmp(a.worldFromClip,b.worldFromClip,sizeof(a.worldFromClip))==0 &&
        std::memcmp(a.viewport,b.viewport,sizeof(a.viewport))==0;
}
inline double Cross(const Quad &q, unsigned a, unsigned b, double x, double y) {
    return (double(q.xy[b*2])-q.xy[a*2])*(y-q.xy[a*2+1])-
        (double(q.xy[b*2+1])-q.xy[a*2+1])*(x-q.xy[a*2]);
}
inline bool Convex(const Quad &q) {
    if(!std::isfinite(q.level)) return false;
    for(float f:q.xy) if(!std::isfinite(f)) return false;
    double orientation=0;
    for(unsigned i=0;i<4;++i) {
        const double c=Cross(q,i,(i+1)%4,q.xy[((i+2)%4)*2],q.xy[((i+2)%4)*2+1]);
        if(std::abs(c)<1e-8 || (orientation!=0 && c*orientation<=0)) return false;
        orientation=c;
    }
    return true;
}
inline bool Contains(const Quad &patch,const Quad &track) {
    if(!Convex(patch)||!Convex(track)||patch.level!=track.level) return false;
    const double orientation=Cross(patch,0,1,patch.xy[4],patch.xy[5]);
    for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j) {
        const double c=Cross(patch,i,(i+1)%4,track.xy[j*2],track.xy[j*2+1]);
        if((orientation>0 && c<0)||(orientation<0 && c>0)) return false;
    }
    return true;
}
inline float Replacement(float depthAvailable,float rawDepth,float offshore,float support,float terrainDifference) {
    if(!std::isfinite(depthAvailable)||depthAvailable<=.5f||!std::isfinite(rawDepth)||
        rawDepth<0||rawDepth>=.999999f||!std::isfinite(offshore)||
        !std::isfinite(support)||!std::isfinite(terrainDifference)) return 0;
    float x=(std::abs(terrainDifference)-.5f);x=x<0?0:x>1?1:x;
    return 1-x*x*(3-2*x);
}
// Append to the ordinary fixed-function D3D11 combiner, then retarget exactly once.
// The original pixel function runs unchanged (including its fog, shroud and alpha test).
inline bool WrapPixel(std::string &source) {
    const std::string fields="    float4 AlphaReference;";
    const std::string entry="float4 main(Input input) : SV_Target";
    const std::size_t at=source.find(fields),main=source.find(entry);
    if(at==std::string::npos||main==std::string::npos) return false;
    source.replace(main,entry.size(),"float4 shoreOriginalPixel(Input input) : SV_Target");
    source.insert(at+fields.size(),R"HLSL(
    row_major float4x4 ShoreWorldFromClip;
    float4 ShoreViewport;
    float4 ShoreTerrain;
    float4 ShoreScene;
    float4 ShoreSurface;
)HLSL");
    source+=R"HLSL(
Texture2D Texture4 : register(t4);
SamplerState Sampler4 : register(s4);
Texture2D Texture6 : register(t6);
SamplerState Sampler6 : register(s6);
bool shoreUnproject(float2 pixel,float d,out float3 world) {
    float2 uv=(pixel-ShoreViewport.xy)/max(ShoreViewport.zw,1.0.xx);
    float4 p=mul(float4(uv*float2(2,-2)+float2(-1,1),d,1),ShoreWorldFromClip);
    world=0;
    if(!all(isfinite(p))||abs(p.w)<1e-8) return false;
    world=p.xyz/p.w;
    return all(isfinite(world));
}
float shoreReplacement(float2 originalPixel) {
    if(!isfinite(ShoreScene.w)||ShoreScene.w<=.5||ShoreTerrain.w<.5||ShoreSurface.z<.5) return 0;
    float2 first=max(.5.xx,ShoreViewport.xy);
    float2 last=min(1.0/ShoreScene.yz-.5,ShoreViewport.xy+ShoreViewport.zw-1);
    float2 pixel=clamp(floor(originalPixel)+.5,first,last);
    float d=Texture6.SampleLevel(Sampler6,pixel*ShoreScene.yz,0).r;
    if(!isfinite(d)||d<0||d>=.999999) return 0;
    float3 scene,a,b;
    if(!shoreUnproject(pixel,d,scene)||!shoreUnproject(pixel,0,a)||!shoreUnproject(pixel,.5,b)) return 0;
    float dz=b.z-a.z;
    if(abs(dz)<1e-8) return 0;
    // Reconstruct the actual elevated sprite plane. SV_Position.z includes legacy Z bias.
    float2 plane=a.xy+(b.xy-a.xy)*((ShoreSurface.y-a.z)/dz);
    if(!all(isfinite(plane))) return 0;
    float2 uv=(plane+ShoreTerrain.xx+5)/max(ShoreTerrain.y,1);
    if(any(uv<0)||any(uv>1)) return 0;
    float4 coast=Texture4.SampleLevel(Sampler4,uv,0);
    if(!all(isfinite(coast))) return 0;
    float2 groundUV=(scene.xy+ShoreTerrain.xx+5)/max(ShoreTerrain.y,1);
    if(any(groundUV<0)||any(groundUV>1)) return 0;
    float ground=Texture2.SampleLevel(Sampler2,groundUV,0).r*ShoreTerrain.z;
    if(!isfinite(ground)) return 0;
    // The accepted modern patch owns this authored surf quad. R only blends wave
    // models, and A only locates fresh breaking; neither limits that ownership.
    // Keeping either here leaves old travelling sprites outside the source band.
    // Actual terrain/depth checks still preserve objects and unavailable resources.
    return 1-smoothstep(.5,1.5,abs(scene.z-ground));
}
float4 main(Input input) : SV_Target {
    float4 result=shoreOriginalPixel(input);
    result.a*=1-shoreReplacement(input.Position.xy);
    return result;
}
)HLSL";
    return true;
}
} // namespace WaterShoreOverlay

#ifndef _WIN32
class IDirect3DDevice9;
// Scope is one actual WaterRender call. Null tags clear pending metadata immediately.
void PosixWaterShoreBegin(IDirect3DDevice9 *,bool enabled);
void PosixWaterShoreEnd(IDirect3DDevice9 *);
void PosixWaterShorePatch(IDirect3DDevice9 *,const WaterShoreOverlay::Quad *);
void PosixWaterShoreTrack(IDirect3DDevice9 *,const WaterShoreOverlay::Quad *);
namespace WaterShoreOverlay {
struct Scope {
    IDirect3DDevice9 *device;
    Scope(IDirect3DDevice9 *d,bool enabled):device(d){PosixWaterShoreBegin(d,enabled);}
    ~Scope(){PosixWaterShoreEnd(device);}
    Scope(const Scope &)=delete;
    Scope &operator=(const Scope &)=delete;
};
}
#else
// The Direct3D 11 backend draws the Modern water without the shore overlay's second pass yet: the
// coast keeps the foam the surface shader draws itself.
struct IDirect3DDevice9;
inline void PosixWaterShorePatch(IDirect3DDevice9 *,const WaterShoreOverlay::Quad *) {}
inline void PosixWaterShoreTrack(IDirect3DDevice9 *,const WaterShoreOverlay::Quad *) {}
namespace WaterShoreOverlay {
struct Scope {
    Scope(IDirect3DDevice9 *,bool) {}
    Scope(const Scope &)=delete;
    Scope &operator=(const Scope &)=delete;
};
}
#endif
