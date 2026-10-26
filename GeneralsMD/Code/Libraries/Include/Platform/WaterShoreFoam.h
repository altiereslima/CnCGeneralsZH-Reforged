/* Render-only coastal breaking source. No fluid solver or gameplay height changes. */
#pragma once
#include "Platform/WaterSurfaceModel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace WaterShoreFoam {
// The physical nearshore surface is the legacy analytic model where optical R is zero.
// Its first three modes carry the visible broad/medium wave motion; fine ripples remain
// in geometry and normals but cannot independently trigger this breaking source.
constexpr unsigned ModeCount=3;
struct Mode { float kx,ky,amplitude,omega,phase; };
#define WATER_SHORE_MODE(kx,ky,a,w,p) {kx,ky,a,w,p},
static constexpr Mode Modes[]={WATER_SURFACE_MODES(WATER_SHORE_MODE)};
#undef WATER_SHORE_MODE
struct Wave { float height=0,dx=0,dy=0,dt=0; };
struct Front {float mask=0,residence=0;};
struct FrontWeights {float value[ModeCount]={};};
// Exact positive crest of sin(q)+.13*sin(2q): .52*cos(q)^2+cos(q)-.26=0.
// The rising leading face runs from this crest to q=pi (q decreases with time).
// Its duration/width are phase-derived, independent of render count or terrain bands.
constexpr float FrontCrestCosine=.232009281f,FrontCrestPhase=1.336653506f;
constexpr float FrontPhaseWidth=1.804939147f,FrontArea=.991318299f;
constexpr float FrontPi=3.141592654f,FrontTau=6.283185307f;
constexpr float FrontMaximumResidence=FrontPhaseWidth/Modes[0].omega;
static_assert(ModeCount==3 && Modes[0].omega<=Modes[1].omega && Modes[0].omega<=Modes[2].omega,
    "Update the bound if the coastal modal subset changes");
struct Context {
    float depth=0,shore=0,nx=0,ny=0,slope=0,exposure=0,support=0;
};
#define WATER_SHORE_CONSTANTS(X) \
    X(MaximumRate,1.2f) X(FreshInterval,0.35f) X(MinDepth,0.15f) X(WetDepth,0.8f) \
    X(IncomingCosine,0.25f) X(CrestHeight,0.5f) X(BreakStart,0.4f) X(BreakFull,0.85f) \
    X(RockDistance,6.0f) X(SlopeStart,0.35f) X(SlopeFull,1.0f) X(ShelteredScale,0.15f)
#define WATER_SHORE_CONSTANT(name,value) constexpr float name=value;
WATER_SHORE_CONSTANTS(WATER_SHORE_CONSTANT)
#undef WATER_SHORE_CONSTANT
inline float Clamp(float x,float lo,float hi) {return (std::max)(lo,(std::min)(hi,x));}
inline float Smooth(float a,float b,float x) {float t=Clamp((x-a)/(b-a),0,1);return t*t*(3-2*t);}
// Normalized incoming modal energy is static per terrain context; the CPU history
// caches it once. Keeping the sum <=1 makes the new birth rate <= the previous rate.
inline FrontWeights IncomingWeights(float nx,float ny) {
    FrontWeights weights;const float length=std::hypot(nx,ny);
    if(!std::isfinite(length)||length<=1e-8f)return weights;
    float total=0;
    for(unsigned i=0;i<ModeCount;++i) {
        const Mode&m=Modes[i];const float k=std::hypot(m.kx,m.ky);
        weights.value[i]=m.amplitude*m.amplitude*Smooth(0,IncomingCosine,-(m.kx*(nx/length)+m.ky*(ny/length))/k);
        total+=weights.value[i];
    }
    if(total>0)for(float&w:weights.value)w/=total;
    return weights;
}
inline float FrontKernel(float sine,float cosine) {
    if(!std::isfinite(sine)||!std::isfinite(cosine)||sine<=0)return 0;
    const float u=Clamp((FrontCrestCosine-cosine)/(1+FrontCrestCosine),0,1);
    return 4*u*(1-u);
}
// Stable definite integral on one positive rising face. Factoring sin(b)-sin(a)
// avoids subtraction of two nearly equal antiderivatives for tiny pixel footprints.
inline float FrontSegment(float a,float b) {
    a=(std::max)(a,FrontCrestPhase);b=(std::min)(b,FrontPi);
    if(b<=a)return 0;
    const float width=b-a,middle=(a+b)*.5f,c=FrontCrestCosine;
    return (std::max)(0.0f,4/((1+c)*(1+c))*((c-.5f)*width
        +2*(c-1)*std::cos(middle)*std::sin(width*.5f)-.5f*std::cos(2*middle)*std::sin(width)));
}
inline float FrontFiltered(float sine,float cosine,float width) {
    if(!std::isfinite(sine)||!std::isfinite(cosine)||!std::isfinite(width)||width<0)return 0;
    // Below .001 radians the point limit differs by <.0004 even at the crest corner.
    if(width<=.001f)return FrontKernel(sine,cosine);
    // An arbitrarily coarse footprint approaches its period mean. At this bound
    // omitting its residual period changes coverage by <1/4096, independent of phase.
    if(width>=FrontTau*4096)return FrontArea/FrontTau;
    const float periods=std::floor(width/FrontTau),remainder=width-periods*FrontTau;
    float q=std::atan2(sine,cosine)+(periods-2*std::floor(periods*.5f))*FrontPi;
    q-=FrontTau*std::floor(q/FrontTau);
    const float a=q-remainder*.5f,b=q+remainder*.5f;
    const float residual=FrontSegment(a,b)+FrontSegment(a-FrontTau,b-FrontTau)+FrontSegment(a+FrontTau,b+FrontTau);
    return Clamp((periods*FrontArea+residual)/width,0,1);
}
inline Front EvaluateFront(float x,float y,float time,float nx,float ny,
                          float dxX=0,float dxY=0,float dyX=0,float dyY=0) {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(time)
        ||!std::isfinite(dxX)||!std::isfinite(dxY)||!std::isfinite(dyX)||!std::isfinite(dyY))return {};
    const FrontWeights weights=IncomingWeights(nx,ny);Front front;
    for(unsigned i=0;i<ModeCount;++i) {
        const Mode&m=Modes[i];const float q=x*m.kx+y*m.ky-time*m.omega+m.phase;
        if(!std::isfinite(q))return {};
        const float width=std::abs(m.kx*dxX+m.ky*dxY)+std::abs(m.kx*dyX+m.ky*dyY);
        const float active=weights.value[i]*FrontFiltered(std::sin(q),std::cos(q),width);
        front.mask+=active;front.residence+=active*(FrontPhaseWidth/m.omega);
    }
    if(front.mask>0)front.residence/=front.mask;
    front.mask=Clamp(front.mask,0,1);return front;
}
inline float FrontFresh(float rate,float residence) {
    return std::isfinite(rate)&&std::isfinite(residence)
        ?1-std::exp(-Clamp(rate,0,MaximumRate)*Clamp(residence,0,FrontMaximumResidence)):0;
}
inline float BlendFrontHistory(float legacy,float rate,float residence,float history,float support,float sceneWet,float terrainMatch) {
    return legacy*(1-Clamp(support,0,1)*Clamp(terrainMatch,0,1))
        +(std::max)(FrontFresh(rate,residence),Clamp(history,0,1))*Clamp(sceneWet,0,1)*Clamp(terrainMatch,0,1);
}
inline Wave Evaluate(float x,float y,float time) {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(time))return {};
    Wave value;
    for(unsigned i=0;i<ModeCount;++i) {
        const Mode &m=Modes[i];const float p=x*m.kx+y*m.ky-time*m.omega+m.phase;
        if(!std::isfinite(p))return {};
        const float s=std::sin(p),c=std::cos(p);
        const float derivative=m.amplitude*(c+0.26f*(c*c-s*s));
        value.height+=m.amplitude*(s+0.26f*s*c);
        value.dx+=derivative*m.kx;value.dy+=derivative*m.ky;value.dt-=derivative*m.omega;
    }
    return value;
}
inline bool Finite(const Context &c,const Wave &w) {
    return std::isfinite(c.depth)&&std::isfinite(c.shore)&&std::isfinite(c.nx)&&std::isfinite(c.ny)
        &&std::isfinite(c.slope)&&std::isfinite(c.exposure)&&std::isfinite(c.support)
        &&std::isfinite(w.height)&&std::isfinite(w.dx)&&std::isfinite(w.dy)&&std::isfinite(w.dt);
}
inline float Rate(const Context &c,const Wave &w) {
    if(!Finite(c,w)||c.depth<=MinDepth||c.support<=0||w.height<=0)return 0;
    const float normalLength=std::hypot(c.nx,c.ny),waveSlope=std::hypot(w.dx,w.dy);
    if(!std::isfinite(normalLength)||!std::isfinite(waveSlope)||normalLength<=1e-8f||waveSlope<=1e-8f||std::abs(w.dt)<=1e-8f)return 0;
    // Phase travel is -h_t*grad(h)/|grad(h)|^2. Positive flux approaches the coast,
    // since the context normal points from high terrain toward open water.
    const float cosine=(w.dt>0?1.0f:-1.0f)*((w.dx/waveSlope)*(c.nx/normalLength)+(w.dy/waveSlope)*(c.ny/normalLength));
    const float incoming=Smooth(0,IncomingCosine,cosine);
    const float attenuation=WaterSurfaceModel::ShoreDamping(c.depth);
    const float beach=Smooth(BreakStart,BreakFull,2*w.height/(std::max)(c.depth+0.5f,1.0f))
        *(1-attenuation*attenuation);
    const float rock=1-Smooth(0,RockDistance,(std::max)(0.0f,c.shore));
    const float steep=Smooth(SlopeStart,SlopeFull,c.slope);
    return MaximumRate*Clamp(c.support,0,1)*Smooth(MinDepth,WetDepth,c.depth)*incoming
        *(ShelteredScale+(1-ShelteredScale)*Clamp(c.exposure,0,1))
        *Clamp(beach+(rock-beach)*steep,0,1)*Smooth(0,CrestHeight,w.height);
}
// A fallback terrain position is not evidence that the original scene pixel hit terrain.
// This eligibility applies to replacement as well as emission, so missing/clear depth
// preserves the previous intersection rim even during a zero-rate wave trough.
inline bool SceneDepthEligible(float available,float rawDepth) {
    return std::isfinite(available)&&available>0.5f&&std::isfinite(rawDepth)
        &&rawDepth>=0.0f&&rawDepth<0.999999f;
}
inline float Fresh(float rate) {
    return std::isfinite(rate)?1-std::exp(-Clamp(rate,0,MaximumRate)*FreshInterval):0;
}
// Rate already includes static support. Preserve object/river/missing-context fallback
// independently of source activity; a wave trough can then genuinely remove the terrain rim.
inline float Blend(float legacy,float rate,float support,float sceneWet,float terrainMatch) {
    return legacy*(1-Clamp(support,0,1)*Clamp(terrainMatch,0,1))
        +Fresh(rate)*Clamp(sceneWet,0,1)*Clamp(terrainMatch,0,1);
}
inline float BlendHistory(float legacy,float rate,float history,float support,float sceneWet,float terrainMatch) {
    return legacy*(1-Clamp(support,0,1)*Clamp(terrainMatch,0,1))
        +(std::max)(Fresh(rate),Clamp(history,0,1))*Clamp(sceneWet,0,1)*Clamp(terrainMatch,0,1);
}
inline std::string HistorySource() {
    return R"HLSL(
float waterShoreHistory(float2 p,float level,float time) {
    // x bit0 retains vessel RGB; bit1 independently advertises coastal alpha.
    if(((uint)(FoamHistoryParameters.x+0.5)&2u)==0u || abs(level-FoamHistoryParameters.y)>.1) return 0;
    float2 uv=(p-FoamHistoryArea.xy)*FoamHistoryArea.zw;
    if(any(uv<0.0)||any(uv>1.0)) return 0;
    float coverage=saturate(waterFoamHistoryTile(uv,0u).a);
    // Effective optical-density decay between <=0.1s uploads. Age-dependent porosity
    // is held for that bounded interval; CPU history remains the authoritative state.
    float elapsed=max(0.0,time-FoamHistoryParameters.w);
    coverage=1-pow(max(1.0-coverage,1.0/65536.0),exp(-elapsed/5.0));
    float2 border=min(uv,1.0-uv)/max(FoamHistoryArea.zw,float2(.000001,.000001));
    return coverage*smoothstep(0.0,max(8.0,FoamHistoryParameters.z*3.0),min(border.x,border.y));
}
)HLSL";
}
inline std::string FrontSource() {
    char constants[512];std::snprintf(constants,sizeof(constants),
        "static const float ShoreFrontCrestCosine=%.9g,ShoreFrontCrestPhase=%.9g,ShoreFrontPhaseWidth=%.9g,ShoreFrontArea=%.9g,ShoreFrontPi=%.9g,ShoreFrontTau=%.9g,ShoreFrontMaximumResidence=%.9g;\n",
        double(FrontCrestCosine),double(FrontCrestPhase),double(FrontPhaseWidth),double(FrontArea),double(FrontPi),double(FrontTau),double(FrontMaximumResidence));
    std::string text=constants;
    text+=R"HLSL(
float waterShoreFrontKernel(float s,float c) {
    if(!isfinite(s)||!isfinite(c)||s<=0)return 0;
    float u=saturate((ShoreFrontCrestCosine-c)/(1+ShoreFrontCrestCosine));return 4*u*(1-u);
}
float waterShoreFrontSegment(float a,float b) {
    a=max(a,ShoreFrontCrestPhase);b=min(b,ShoreFrontPi);if(b<=a)return 0;
    float width=b-a,middle=(a+b)*.5,c=ShoreFrontCrestCosine;
    return max(0.0,4/((1+c)*(1+c))*((c-.5)*width
        +2*(c-1)*cos(middle)*sin(width*.5)-.5*cos(2*middle)*sin(width)));
}
float waterShoreFrontFiltered(float s,float c,float width) {
    if(!isfinite(s)||!isfinite(c)||!isfinite(width)||width<0)return 0;
    if(width<=.001)return waterShoreFrontKernel(s,c);
    if(width>=ShoreFrontTau*4096)return ShoreFrontArea/ShoreFrontTau;
    float periods=floor(width/ShoreFrontTau),remainder=width-periods*ShoreFrontTau;
    float q=atan2(s,c)+(periods-2*floor(periods*.5))*ShoreFrontPi;
    q-=ShoreFrontTau*floor(q/ShoreFrontTau);
    float a=q-remainder*.5,b=q+remainder*.5;
    float residual=waterShoreFrontSegment(a,b)+waterShoreFrontSegment(a-ShoreFrontTau,b-ShoreFrontTau)
        +waterShoreFrontSegment(a+ShoreFrontTau,b+ShoreFrontTau);
    return saturate((periods*ShoreFrontArea+residual)/width);
}
float waterShoreFrontFresh(float rate,float residence) {
    return isfinite(rate)&&isfinite(residence)?1-exp(-clamp(rate,0,ShoreMaximumRate)*clamp(residence,0,ShoreFrontMaximumResidence)):0;
}
float waterShoreBlendFrontHistory(float legacy,float rate,float residence,float history,float support,float sceneWet,float terrainMatch) {
    return legacy*(1-saturate(support)*saturate(terrainMatch))
        +max(waterShoreFrontFresh(rate,residence),saturate(history))*saturate(sceneWet)*saturate(terrainMatch);
}
float4 waterShoreWaveFront(float2 p,float time,float2 outward,float2 parameterDx,float2 parameterDy,out float2 front) {
    front=0;
    if(!all(isfinite(p))||!isfinite(time)||!all(isfinite(outward))
        ||!all(isfinite(parameterDx))||!all(isfinite(parameterDy)))return 0;
    float normalLength=length(outward);if(!isfinite(normalLength)||normalLength<=1e-8)return waterShoreWave(p,time);
    float2 normal=outward/normalLength;float total=0;float4 wave=0;
)HLSL";
    for(unsigned i=0;i<ModeCount;++i) {
        const Mode&m=Modes[i];char line[1400];
        std::snprintf(line,sizeof(line),
            "{float2 k=float2(%.9g,%.9g);float q=p.x*k.x+p.y*k.y-time*%.9g+%.9g;if(!isfinite(q)){front=0;return 0;}"
            "float s=sin(q),c=cos(q),d=%.9g*(c+.26*(c*c-s*s));wave+=float4(%.9g*(s+.26*s*c),d*k,-d*%.9g);"
            "float weight=%.9g*smoothstep(0,ShoreIncomingCosine,-dot(k,normal)/%.9g);"
            "float width=abs(dot(k,parameterDx))+abs(dot(k,parameterDy));float active=weight*waterShoreFrontFiltered(s,c,width);"
            "front+=active*float2(1,ShoreFrontPhaseWidth/%.9g);total+=weight;}\n",
            double(m.kx),double(m.ky),double(m.omega),double(m.phase),double(m.amplitude),double(m.amplitude),double(m.omega),
            double(m.amplitude*m.amplitude),double(std::hypot(m.kx,m.ky)),double(m.omega));
        text+=line;
    }
    text+="if(front.x>0)front.y/=front.x;if(total>0)front.x=saturate(front.x/total);return wave;\n}\n";
    return text;
}
inline std::string Source() {
    std::string text;
#define WATER_SHORE_HLSL(name,value) text+="static const float Shore" #name "=" #value ";\n";
    WATER_SHORE_CONSTANTS(WATER_SHORE_HLSL)
#undef WATER_SHORE_HLSL
    text+="float4 waterShoreWave(float2 p,float time) {\nif(!all(isfinite(p))||!isfinite(time)) return 0;\nfloat4 w=0;\n";
    for(unsigned i=0;i<ModeCount;++i) {
        const Mode &m=Modes[i];char line[700];
        std::snprintf(line,sizeof(line),
            "{float q=p.x*%.9g+p.y*%.9g-time*%.9g+%.9g;if(!isfinite(q))return 0;float s=sin(q),c=cos(q);float d=%.9g*(c+.26*(c*c-s*s));w+=float4(%.9g*(s+.26*s*c),d*%.9g,d*%.9g,-d*%.9g);}\n",
            double(m.kx),double(m.ky),double(m.omega),double(m.phase),double(m.amplitude),double(m.amplitude),double(m.kx),double(m.ky),double(m.omega));
        text+=line;
    }
    text+=R"HLSL(return w;
}
float waterShoreRate(float4 wave,float depth,float shore,float2 outward,float terrainSlope,float exposure,float support) {
    if(!all(isfinite(wave))||!all(isfinite(float4(depth,shore,outward)))
        ||!all(isfinite(float3(terrainSlope,exposure,support)))
        ||depth<=ShoreMinDepth||support<=0||wave.x<=0) return 0;
    float normalLength=length(outward),waveSlope=length(wave.yz);
    if(!isfinite(normalLength)||!isfinite(waveSlope)||normalLength<=1e-8||waveSlope<=1e-8||abs(wave.w)<=1e-8) return 0;
    float cosine=sign(wave.w)*dot(wave.yz/waveSlope,outward/normalLength);
    float incoming=smoothstep(0,ShoreIncomingCosine,cosine);
    float attenuation=saturate(depth/9.0);attenuation=attenuation*attenuation*(3-2*attenuation);
    float beach=smoothstep(ShoreBreakStart,ShoreBreakFull,2*wave.x/max(depth+.5,1))*(1-attenuation*attenuation);
    float rock=1-smoothstep(0,ShoreRockDistance,max(0,shore));
    float steep=smoothstep(ShoreSlopeStart,ShoreSlopeFull,terrainSlope);
    return ShoreMaximumRate*saturate(support)*smoothstep(ShoreMinDepth,ShoreWetDepth,depth)*incoming
        *lerp(ShoreShelteredScale,1,saturate(exposure))*saturate(lerp(beach,rock,steep))
        *smoothstep(0,ShoreCrestHeight,wave.x);
}
bool waterShoreSceneDepth(float available,float rawDepth) {
    return isfinite(available)&&available>0.5&&isfinite(rawDepth)
        &&rawDepth>=0.0&&rawDepth<0.999999;
}
float waterShoreFresh(float rate) {return isfinite(rate)?1-exp(-clamp(rate,0,ShoreMaximumRate)*ShoreFreshInterval):0;}
float waterShoreBlendHistory(float legacy,float rate,float history,float support,float sceneWet,float terrainMatch) {
    return legacy*(1-saturate(support)*saturate(terrainMatch))
        +max(waterShoreFresh(rate),saturate(history))*saturate(sceneWet)*saturate(terrainMatch);
}
float waterShoreBlend(float legacy,float rate,float support,float sceneWet,float terrainMatch) {
    return legacy*(1-saturate(support)*saturate(terrainMatch))
        +waterShoreFresh(rate)*saturate(sceneWet)*saturate(terrainMatch);
}
)HLSL";
    return text+FrontSource();
}
#undef WATER_SHORE_CONSTANTS
}
