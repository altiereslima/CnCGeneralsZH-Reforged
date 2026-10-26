/* Render-only vessel response. Shares the actual waterline and simulation clock with water. */
#pragma once
#include <string>
#include <algorithm>
#include <cmath>
#include "Platform/WaterBowShoulder.h"

namespace WaterVesselWaves {
// Render-only pressure response uses measured contact half-beam and velocity. Keeping these
// constants in one list makes the CPU refinement estimate and both shader stages agree.
#define WATER_VESSEL_BOW_CONSTANTS(X) \
    X(BowAlongBase,6.0f) X(BowAlongBeam,1.2f) X(BowAlongMin,9.0f) X(BowAlongMax,32.0f) \
    X(BowAcrossBase,1.5f) X(BowAcrossBeam,0.95f) X(BowAcrossMin,4.0f) X(BowAcrossMax,60.0f) \
    X(BowHeadBeam,1.8f) X(BowHeadMin,3.0f) X(BowHeadMax,8.0f) \
    X(BowRestSpeed,0.3f) X(BowSpeedScale,26.0f) X(BowSpeedMax,250.0f) X(BowCurvature,0.6f)
#define WATER_VESSEL_BOW_CONSTANT(name,value) constexpr float name=value;
WATER_VESSEL_BOW_CONSTANTS(WATER_VESSEL_BOW_CONSTANT)
#undef WATER_VESSEL_BOW_CONSTANT
struct BowPressureParameters { float along=0,across=0,amplitude=0; };
inline BowPressureParameters BowPressure(float halfBeam,float speed) {
    if(!std::isfinite(halfBeam)||!std::isfinite(speed)||halfBeam<0||speed<0) return {};
    const float u=(std::max)(0.0f,(std::min)(speed,BowSpeedMax)-BowRestSpeed);
    const float response=u*u/(u*u+BowSpeedScale*BowSpeedScale);
    BowPressureParameters p;
    p.along=(std::max)(BowAlongMin,(std::min)(BowAlongMax,BowAlongBase+BowAlongBeam*halfBeam));
    p.across=(std::max)(BowAcrossMin,(std::min)(BowAcrossMax,BowAcrossBase+BowAcrossBeam*halfBeam));
    p.amplitude=(std::max)(BowHeadMin,(std::min)(BowHeadMax,BowHeadBeam*std::sqrt(halfBeam)))*response;
    return p;
}
inline std::string BowPressureSource() {
    std::string source;
#define WATER_VESSEL_BOW_HLSL(name,value) source+="static const float " #name "=" #value ";\n";
    WATER_VESSEL_BOW_CONSTANTS(WATER_VESSEL_BOW_HLSL)
#undef WATER_VESSEL_BOW_HLSL
    return source+R"HLSL(
float3 vesselBowPressure(float halfBeam,float speed) {
    if(!isfinite(halfBeam)||!isfinite(speed)||halfBeam<0.0||speed<0.0) return 0;
    float u=max(0.0,min(speed,BowSpeedMax)-BowRestSpeed);
    float response=u*u/(u*u+BowSpeedScale*BowSpeedScale);
    return float3(clamp(BowAlongBase+BowAlongBeam*halfBeam,BowAlongMin,BowAlongMax),
        clamp(BowAcrossBase+BowAcrossBeam*halfBeam,BowAcrossMin,BowAcrossMax),
        clamp(BowHeadBeam*sqrt(halfBeam),BowHeadMin,BowHeadMax)*response);
}
)HLSL";
}
#undef WATER_VESSEL_BOW_CONSTANTS
// Reflection reaches at most 90 units from a contour; generated wakes end within 110 units.
// The collector's existing hull-radius + 122 culling margin therefore still contains the field.
inline std::string Source() {
    // A pitching shallow keel can move a mean-slice endpoint by several world units
    // in one frame. Generated packets follow the current reachable contact envelope;
    // exact mean-waterline clearance and reflection below remain independent.
    std::string source=R"HLSL(
float4 vesselPacketRegister(int index) {
    int base=(index/10)*10;
    float count=ContactHullData[base].w;
    return count>=3.0 && count<=12.0?ContactHullData[index]:HullData[index];
}
)HLSL";
    return BowPressureSource()+source+R"HLSL(
// Match WaterHullModel::Damping at the undisplaced surface parameter. The smooth nine-unit
// clearance envelope is differentiated here, rather than after interpolation across a mesh
// triangle; stronger reflected waves must not expose that triangle's constant damping slope.
float3 vesselHullDamping(float2 p,float level) {
    float3 result=float3(1,0,0);
    int hullCount=clamp((int)HullParameters.x,0,8);
    for(int h=0;h<hullCount;++h) {
        int base=h*10;
        float4 position=HullData[base],axis=HullData[base+1];
        int count=(int)position.w;
        if(count<3 || count>12 || abs(level-position.z)>0.1) continue;
        float2 delta=p-position.xy;
        float2 local=float2(dot(delta,axis.xy),dot(delta,float2(-axis.y,axis.x)));
        if(abs(local.x)>axis.z+9.0 || abs(local.y)>axis.w+9.0) continue;
        float best=1.0e30;float2 separation=0;
        bool inside=true;
        for(int j=0;j<count;++j) {
            float4 pairA=HullData[base+4+j/2];
            int next=(j+1)%count;
            float4 pairB=HullData[base+4+next/2];
            float2 a=(j&1)!=0?pairA.zw:pairA.xy,b=(next&1)!=0?pairB.zw:pairB.xy;
            float2 edge=b-a,from=local-a;
            float t=saturate(dot(from,edge)/max(dot(edge,edge),1.0e-12));
            float2 q=from-t*edge;float d=dot(q,q);
            if(d<best) {best=d;separation=q;}
            inside=inside && edge.x*from.y-edge.y*from.x>=-0.0001;
        }
        if(inside) return float3(0,0,0);
        float distance=sqrt(max(best,0.0));
        float t=saturate(distance/9.0),value=t*t*(3.0-2.0*t);
        if(value<result.x) {
            float2 n=distance>0.0?separation/distance:float2(0,0);
            float2 gradient=float2(axis.x*n.x-axis.y*n.y,axis.y*n.x+axis.x*n.y)
                *(6.0*t*(1.0-t)/9.0);
            result=float3(value,gradient);
        }
    }
    return result;
}
float3 vesselHullDamping(float2 p,float level,float offshore) {
    float3 hull=vesselHullDamping(p,level);
    return float3(1.0+(hull.x-1.0)*offshore,hull.yz*offshore);
}
// Both shader stages consume the same hull-free floor/obstacle envelope and offshore metadata.
// These are scalar+XY-gradient triples; vertex sampling supplies zero input gradients. Hull
// clearance is independent of the optional reflected/generated-wave switch.
float3 vesselApplyHullDamping(float2 p,float level,float3 freeDamping,float3 offshore) {
    if(offshore.x<=0.0 || freeDamping.x<=0.0) return freeDamping;
    float3 hull=vesselHullDamping(p,level);
    hull=float3(1.0+(hull.x-1.0)*offshore.x,hull.yz*offshore.x+(hull.x-1.0)*offshore.yz);
    return hull.x<freeDamping.x?hull:freeDamping;
}
struct VesselReflection {
    float2 samplePosition;
    float2 normal;
    float2 weightGradient;
    float weight;
    float distance;
    float corner;
};
VesselReflection vesselReflection(float2 p,float level,int h) {
    VesselReflection v=(VesselReflection)0;
    int base=h*10;
    float4 position=HullData[base],axis=HullData[base+1];
    int count=(int)position.w;
    if(count<3 || count>12 || abs(level-position.z)>0.1) return v;
    float range=clamp(axis.w*2.0,28.0,90.0);
    float2 delta=p-position.xy;
    float2 local=float2(dot(delta,axis.xy),dot(delta,float2(-axis.y,axis.x)));
    if(abs(local.x)>axis.z+range || abs(local.y)>axis.w+range) return v;
    float best=1.0e30,corner=0;float2 closest=0;
    bool inside=true;
    for(int j=0;j<count;++j) {
        float4 pairA=HullData[base+4+j/2];
        int next=(j+1)%count;
        float4 pairB=HullData[base+4+next/2];
        float2 a=(j&1)!=0?pairA.zw:pairA.xy,b=(next&1)!=0?pairB.zw:pairB.xy;
        float2 edge=b-a,from=local-a;
        float t=saturate(dot(from,edge)/max(dot(edge,edge),1.0e-12));
        float2 q=a+t*edge,separation=local-q;
        float d=dot(separation,separation);
        if(d<best) {best=d;closest=q;corner=(t<=0.0 || t>=1.0)?1.0:0.0;}
        inside=inside && edge.x*from.y-edge.y*from.x>=-0.0001;
    }
    float distance=sqrt(max(best,0.0));
    if(inside || distance<0.0001 || distance>=range) return v;
    float2 n=(local-closest)/distance;
    v.normal=float2(axis.x*n.x-axis.y*n.y,axis.y*n.x+axis.x*n.y);
    v.samplePosition=p-2.0*distance*v.normal;
    float t=distance/range,fade=1.0-t*t*(3.0-2.0*t);
    // A modest reflected copy of the actual incident field creates interference and standing
    // crests beside stationary hulls. No unrelated scrolling phase is introduced.
    v.weight=0.68*fade*fade;
    v.weightGradient=(-1.36*fade*6.0*t*(1.0-t)/range)*v.normal;
    v.distance=distance;v.corner=corner;
    return v;
}
float2 vesselReflectVector(float2 d,float2 normal) {
    return d-2.0*normal*dot(normal,d);
}
float2 vesselMirrorDerivative(float2 direction,VesselReflection v) {
    // The closest point slides along an edge, but stays fixed in a vertex's Voronoi region.
    return v.corner>0.5?-direction:vesselReflectVector(direction,v.normal);
}
float2 vesselReflectionNormalDerivative(float2 direction,VesselReflection v) {
    return v.corner>0.5?(direction-v.normal*dot(v.normal,direction))/max(v.distance,0.0001):float2(0,0);
}
float3 vesselReflectedValue(float3 d,VesselReflection v) {
    return float3(vesselReflectVector(d.xy,v.normal),d.z)*v.weight;
}
float3 vesselReflectedDerivative(float3 d,float3 dx,float3 dy,float2 direction,VesselReflection v) {
    float2 mirror=vesselMirrorDerivative(direction,v);
    float3 sampled=dx*mirror.x+dy*mirror.y;
    float2 normalDerivative=vesselReflectionNormalDerivative(direction,v);
    float2 horizontal=vesselReflectVector(sampled.xy,v.normal)
        -2.0*(normalDerivative*dot(v.normal,d.xy)+v.normal*dot(normalDerivative,d.xy));
    float3 reflected=float3(vesselReflectVector(d.xy,v.normal),d.z);
    return float3(horizontal,sampled.z)*v.weight+reflected*dot(v.weightGradient,direction);
}
// A scalar and its exact world-space gradient. The packet is shared by vertex displacement
// and pixel normals; no finite-difference texture fetches or screen-space phases are required.
float3 vwConstant(float value) {return float3(value,0,0);}
float3 vwMul(float3 a,float3 b) {return float3(a.x*b.x,a.yz*b.x+b.yz*a.x);}
float3 vwExp(float3 a) {float e=exp(a.x);return float3(e,a.yz*e);}
float3 vwSqrt(float3 a) {float root=sqrt(a.x);return float3(root,a.yz/(2.0*root));}
float3 vwSin(float3 a) {return float3(sin(a.x),cos(a.x)*a.yz);}
float3 vwCos(float3 a) {return float3(cos(a.x),-sin(a.x)*a.yz);}
float3 vwSmooth(float low,float high,float3 a) {
    float t=saturate((a.x-low)/(high-low));
    return float3(t*t*(3.0-2.0*t),a.yz*(6.0*t*(1.0-t)/(high-low)));
}
float3 vwGaussian(float3 distance,float width) {
    float3 q=distance/max(width,0.001);
    return vwExp(-vwMul(q,q));
}
// A curved pressure shoulder starts on the actual contact bow. Its wings turn aft toward
// the forebody; there is no detached forward-centred bump. The nine-unit clearance/floor
// envelope is applied later, exactly as for all other generated surface displacement.
float3 vesselBowPressureField(float3 along,float3 across,float3 parameters) {
    if(parameters.x<=0.0 || parameters.y<=0.0 || parameters.z<=0.0) return 0;
    float3 q=across/parameters.y;
    float3 shifted=along/parameters.x+
        (vwSqrt(vwConstant(1.0)+vwMul(q,q))-vwConstant(1.0))*BowCurvature;
    return vwExp(-vwMul(shifted,shifted)-vwMul(q,q))*parameters.z;
}
)HLSL"+WaterBowShoulder::Source()+R"HLSL(// Generated crest coverage follows the physical packet and its usable displacement.
// Illumination belongs to the diffuse foam material, not to the amount of emitted foam.
// The caller retains the solid-hull mask and uses the same microcoverage as hull/history foam.
float vesselGeneratedFoam(float crest,float weight,float damping) {
    return saturate(crest)*saturate(weight)*0.65*smoothstep(0.15,0.65,saturate(damping));
}
float3 vesselGeneratedWave(float2 p,float level,float time,int h,out float3 bowContribution) {
    bowContribution=0;
    int base=h*10;
    float4 position=vesselPacketRegister(base),axis=vesselPacketRegister(base+1),motion=vesselPacketRegister(base+2);
    float speed=length(motion.xy);
    if(position.w<3 || abs(level-position.z)>0.1 || speed<=0.3) return 0;
    float2 delta=p-position.xy;
    float radius=length(delta),reach=motion.w+110.0;
    if(radius>=reach) return 0;
    float moving=smoothstep(0.3,14.0,speed);
    float2 forward=motion.xy/max(speed,0.001),aft=-forward,side=float2(-forward.y,forward.x);
    float2 localVelocity=float2(dot(forward,axis.xy),dot(forward,float2(-axis.y,axis.x)));
    float4 endpoints=vesselPacketRegister(base+3);
    // Sideways drift during a sharp turn must not swap two distant endpoints in one frame.
    // Forward/reverse retain the actual stern/bow; the blend is confined to broadside travel.
    float forwardBlend=smoothstep(-0.25,0.25,localVelocity.x);
    float2 sternLocal=lerp(endpoints.zw,endpoints.xy,forwardBlend);
    float2 bowLocal=lerp(endpoints.xy,endpoints.zw,forwardBlend);
    float2 stern=position.xy+float2(axis.x*sternLocal.x-axis.y*sternLocal.y,axis.y*sternLocal.x+axis.x*sternLocal.y);
    float2 bow=position.xy+float2(axis.x*bowLocal.x-axis.y*bowLocal.y,axis.y*bowLocal.x+axis.x*bowLocal.y);
    float3 along=float3(dot(p-stern,aft),aft),across=float3(dot(p-stern,side),side);
    float lengthWake=clamp(speed*3.2+axis.w*2.0,30.0,110.0);
    float3 trail=vwMul(vwSmooth(0.0,10.0,along),vwConstant(1.0)-vwSmooth(lengthWake*0.5,lengthWake,along));
    float3 width=vwConstant(max(axis.w*0.35,1.0))+along*0.353553;
    float3 ridgeA=across-width,ridgeB=across+width;
    float ribbonWidth=clamp(axis.w*0.18+2.0,2.5,7.0);
    // Paired divergent crests create a V behind the real stern. The transverse packet remains
    // centred on the measured travel direction, including reverse movement.
    float3 arms=vwMul(vwGaussian(ridgeA,ribbonWidth),vwCos(ridgeA*0.40))
        +vwMul(vwGaussian(ridgeB,ribbonWidth),vwCos(ridgeB*0.40));
    float3 churn=vwMul(vwGaussian(across,max(axis.w*0.55,2.0)),
        vwSin(along*0.30+vwConstant(-time*1.15+motion.z*6.2831853)))*0.24;
    float3 wake=vwMul(trail,arms*1.35+churn);
    float3 bowForward=float3(dot(p-bow,forward),forward),bowAcross=float3(dot(p-bow,side),side);
    float3 bowCrest=vesselBowPressureField(bowForward,bowAcross,vesselBowPressure(axis.w,speed));
    float3 radial=float3(radius,delta/max(radius,0.001));
    float3 support=vwConstant(1.0)-vwSmooth(motion.w+80.0,reach,radial);
    bowContribution=vwMul(bowCrest,support);
    return vwMul(wake*moving+bowCrest,support);
}
float3 vesselGeneratedWave(float2 p,float level,float time,int h) {
    float3 bowContribution;
    return vesselGeneratedWave(p,level,time,h,bowContribution);
}
// Bow-only world field. Keep the stern packet and its parameterization above unchanged.
// The caller supplies the final horizontal position after every reflected XY contribution.
float3 vesselGeneratedBow(float2 p,float level,int h) {
    return vesselContourBow(p,level,h);
}
)HLSL";
}
}
