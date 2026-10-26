/* Render-only waterline contours. Gameplay geometry and navigation never use these hulls. */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "Platform/WaterHullContactFoam.h"

namespace WaterHullModel {
constexpr unsigned MaxHulls=8, MaxVertices=12, RegistersPerHull=10, MaxCacheEntries=64;
constexpr unsigned MaxSlicePoints=4096, MaxSliceTriangles=8192, MaxMeshes=128;
constexpr float FadeDistance=9.0f;
struct Point { float x,y; };
struct Point3 { float x,y,z; };
struct Polygon { Point points[MaxVertices]; unsigned count=0; float error=0.0f; };
struct Hull { float data[RegistersPerHull][4] = {}; };
inline float Cross(Point a,Point b,Point c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
inline float SegmentDistanceSquared(Point p,Point a,Point b) {
    const float x=b.x-a.x,y=b.y-a.y;
    const float t=(std::max)(0.0f,(std::min)(1.0f,((p.x-a.x)*x+(p.y-a.y)*y)/(std::max)(x*x+y*y,1.0e-12f)));
    const float dx=p.x-a.x-t*x,dy=p.y-a.y-t*y;
    return dx*dx+dy*dy;
}
// Coplanar triangles contribute their vertices. Tangent edges contribute their exact waterline,
// and repeated edge hits are welded by BuildPolygon. No GPU buffer is mapped or read back.
inline bool SliceTriangle(Point3 a,Point3 b,Point3 c,std::vector<Point>& points) {
    const Point3 p[3]={a,b,c};
    for (unsigned i=0;i<3;++i) {
        const Point3 &u=p[i],&v=p[(i+1)%3];
        if (!std::isfinite(u.x)||!std::isfinite(u.y)||!std::isfinite(u.z)) return false;
        if (std::abs(u.z)<0.0001f) {
            if (points.size()>=MaxSlicePoints) return false;
            points.push_back({u.x,u.y});
        }
        if ((u.z<0.0f && v.z>0.0f)||(u.z>0.0f && v.z<0.0f)) {
            if (points.size()>=MaxSlicePoints) return false;
            const float t=u.z/(u.z-v.z);
            points.push_back({u.x+(v.x-u.x)*t,u.y+(v.y-u.y)*t});
        }
    }
    return true;
}
inline Polygon BuildPolygon(std::vector<Point> points) {
    Polygon result;
    if (points.size()<3 || points.size()>MaxSlicePoints) return result;
    for (const Point& p:points) if (!std::isfinite(p.x)||!std::isfinite(p.y)) return result;
    std::sort(points.begin(),points.end(),[](Point a,Point b){return a.x<b.x || (a.x==b.x && a.y<b.y);});
    points.erase(std::unique(points.begin(),points.end(),[](Point a,Point b){return std::abs(a.x-b.x)<0.0001f && std::abs(a.y-b.y)<0.0001f;}),points.end());
    std::vector<Point> outline(points.size()*2);
    unsigned count=0;
    for (Point p:points) { while(count>=2 && Cross(outline[count-2],outline[count-1],p)<=0.00001f) --count; outline[count++]=p; }
    const unsigned lower=count;
    for (size_t i=points.size()-1;i>0;--i) { const Point p=points[i-1]; while(count>lower && Cross(outline[count-2],outline[count-1],p)<=0.00001f) --count; outline[count++]=p; }
    if (count<4) return result;
    outline.resize(count-1);
    const std::vector<Point> original=outline;
    // Preserve the largest shape features. The measured error is retained for diagnostics/tests;
    // the inner convex approximation never places an artificial solid footprint outside the mesh.
    while(outline.size()>MaxVertices) {
        size_t remove=0; float least=1.0e30f;
        for (size_t i=0;i<outline.size();++i) {
            const float error=SegmentDistanceSquared(outline[i],outline[(i+outline.size()-1)%outline.size()],outline[(i+1)%outline.size()]);
            if(error<least) { least=error;remove=i; }
        }
        outline.erase(outline.begin()+remove);
    }
    result.count=(unsigned)outline.size();
    for(unsigned i=0;i<result.count;++i) result.points[i]=outline[i];
    for(Point p:original) {
        float closest=1.0e30f;
        for(unsigned i=0;i<result.count;++i) closest=(std::min)(closest,SegmentDistanceSquared(p,result.points[i],result.points[(i+1)%result.count]));
        result.error=(std::max)(result.error,std::sqrt(closest));
    }
    return result;
}
inline float SignedDistance(Point p,const Polygon& polygon) {
    if(polygon.count<3 || polygon.count>MaxVertices) return 1.0e6f;
    float distance=1.0e30f; bool inside=true;
    for(unsigned i=0;i<polygon.count;++i) {
        const Point a=polygon.points[i],b=polygon.points[(i+1)%polygon.count];
        distance=(std::min)(distance,SegmentDistanceSquared(p,a,b));
        inside=inside && Cross(a,b,p)>=-0.0001f;
    }
    return std::sqrt(distance)*(inside?-1.0f:1.0f);
}
inline Polygon Unpack(const Hull& hull) {
    Polygon p;p.count=(unsigned)(std::max)(0.0f,(std::min)(float(MaxVertices),hull.data[0][3]));
    for(unsigned i=0;i<p.count;++i) p.points[i]={hull.data[4+i/2][(i%2)*2],hull.data[4+i/2][(i%2)*2+1]};
    return p;
}
inline Point LocalPoint(const Hull& hull,float x,float y) {
    const float dx=x-hull.data[0][0],dy=y-hull.data[0][1];
    return {dx*hull.data[1][0]+dy*hull.data[1][1],-dx*hull.data[1][1]+dy*hull.data[1][0]};
}
inline float Damping(const Hull& hull,float x,float y,float level) {
    if(std::abs(level-hull.data[0][2])>0.1f) return 1.0f;
    const Point p=LocalPoint(hull,x,y);
    if(std::abs(p.x)>hull.data[1][2]+FadeDistance || std::abs(p.y)>hull.data[1][3]+FadeDistance) return 1.0f;
    const float t=(std::max)(0.0f,(std::min)(1.0f,SignedDistance(p,Unpack(hull))/FadeDistance));
    return t*t*(3.0f-2.0f*t);
}
// The consuming water shader declares float4 HullData[MaxHulls*RegistersPerHull].
inline const char *GetHlslSource() {
    return R"HLSL(
float2 WaterHullVertex(int baseIndex,int vertex) {
    float4 pair=HullData[baseIndex+4+vertex/2];
    return (vertex&1)!=0?pair.zw:pair.xy;
}
float WaterHullSignedDistance(float2 p,int baseIndex,int count) {
    if(count<3 || count>12) return 1.0e6;
    float distanceSquared=1.0e30; bool inside=true;
    for(int i=0;i<count;++i) {
        float2 a=WaterHullVertex(baseIndex,i),b=WaterHullVertex(baseIndex,(i+1)%count);
        float2 edge=b-a,delta=p-a;
        float t=saturate(dot(delta,edge)/max(dot(edge,edge),1.0e-12));
        float2 separation=delta-t*edge;
        distanceSquared=min(distanceSquared,dot(separation,separation));
        inside=inside && edge.x*delta.y-edge.y*delta.x>=-0.0001;
    }
    return sqrt(distanceSquared)*(inside?-1.0:1.0);
}
)HLSL";
}

// Depends on the production oceanNoise function and HullParameters/HullData declarations.
// Tests call this same emitted shader, so contact and moving-wake behavior cannot drift.
inline std::string GetEffectsHlslSource() {
    // An invalid or empty contact packet falls back to the exact mean-waterline contour.
    std::string source=R"HLSL(
float4 WaterContactRegister(int index) {
    return ContactHullData[(index/10)*10].w>=3.0?ContactHullData[index]:HullData[index];
}
)HLSL";
    source=std::string(WaterHullContactFoam::Source())+source;
    source+=R"HLSL(
float WaterContactDistance(float2 p,int index,int count) {
    if(count<3 || count>12) return 1.0e6;
    float closest=1.0e30; bool inside=true;
    for(int i=0;i<count;++i) {
        int next=(i+1)%count;
        float4 pa=WaterContactRegister(index+4+i/2),pb=WaterContactRegister(index+4+next/2);
        float2 a=(i&1)!=0?pa.zw:pa.xy,b=(next&1)!=0?pb.zw:pb.xy;
        float2 edge=b-a,delta=p-a;
        float t=saturate(dot(delta,edge)/max(dot(edge,edge),1.0e-12));
        float2 separation=delta-t*edge;
        closest=min(closest,dot(separation,separation));
        inside=inside && edge.x*delta.y-edge.y*delta.x>=-0.0001;
    }
    return sqrt(closest)*(inside?-1.0:1.0);
}

// The hull contour anchors fresh contact foam. Only a short stern mixing region follows the
// current motion; the independent world-space history retains the trailing path after turns.
float3 oceanHullEffects(float2 world,float level,float time,float offshore,float waveHeight,float footprint,float waveImpact)
{
    float foam=0,turbulence=0,damping=1;
    if(offshore<=0.001) return float3(0,0,1);
    int count=clamp((int)HullParameters.x,0,8);
    for(int h=0;h<count;++h) {
        int index=h*10;
        float4 meanPosition=HullData[index],meanAxis=HullData[index+1];
        if(meanPosition.w<3 || abs(level-meanPosition.z)>0.1) continue;
        float2 meanDelta=world-meanPosition.xy;
        float2 meanLocal=float2(dot(meanDelta,meanAxis.xy),dot(meanDelta,float2(-meanAxis.y,meanAxis.x)));
        if(abs(meanLocal.x)<meanAxis.z+9.0 && abs(meanLocal.y)<meanAxis.w+9.0) {
            float t=saturate(WaterHullSignedDistance(meanLocal,index,(int)meanPosition.w)/9.0);
            damping=min(damping,t*t*(3.0-2.0*t));
        }
        float4 position=WaterContactRegister(index),axis=WaterContactRegister(index+1),motion=WaterContactRegister(index+2);
        if(position.w<3 || abs(level-position.z)>0.1) continue;
        float2 delta=world-position.xy;
        float2 local=float2(dot(delta,axis.xy),dot(delta,float2(-axis.y,axis.x)));
        float speed=length(motion.xy),moving=smoothstep(0.3,18.0,speed);
        float reach=motion.w+48.0;
        if(dot(delta,delta)>reach*reach) continue;
        float2 velocity=float2(dot(motion.xy,axis.xy),dot(motion.xy,float2(-axis.y,axis.x)));
        float2 forward=velocity/max(speed,0.001),aft=-forward,side=float2(-aft.y,aft.x);
        float blend=smoothstep(-0.25,0.25,forward.x);
        float4 endpoints=WaterContactRegister(index+3);
        float2 stern=lerp(endpoints.zw,endpoints.xy,blend),bow=lerp(endpoints.xy,endpoints.zw,blend);
        float hullDistance=1.0e6;
        if(abs(local.x)<axis.z+12.0 && abs(local.y)<axis.w+12.0) {
            hullDistance=WaterContactDistance(local,index,(int)position.w);
            float distance=max(hullDistance,0.0),aa=clamp(footprint*0.6,0.25,1.5);
            float exposed=smoothstep(-aa,0.1,hullDistance);
            float wash=oceanNoise(local*float2(0.065,0.32)+float2(-time*0.12,motion.z*9.0));
            float flecks=oceanNoise(local*0.48+float2(time*0.08,-time*0.13));
            // Incoming slope/contact can foam on an idle hull, but never emits propulsion wash.
            float collision=waterHullContactResponse(waveHeight,waveImpact);
            float sideWidth=1.0+moving*min(2.0,sqrt(max(axis.w,1.0))*0.35)+collision*1.3;
            float rim=1.0-smoothstep(sideWidth,sideWidth+aa,distance);
            float sideStreak=smoothstep(0.43,0.68,wash)*(0.2+0.8*smoothstep(0.25,0.65,flecks));
            float contact=exposed*max(rim*sideStreak*moving*0.38,
                waterHullContactCoverage(distance,footprint,waveHeight,waveImpact,axis.w));
            float outer=1.0-smoothstep(sideWidth,sideWidth+3.5,distance);
            // The leading waterline carries a concentrated bow impact, not an all-round halo.
            float bowDistance=length((local-bow)/float2(max(axis.w*0.8,3.0),max(axis.w*0.9,3.0)));
            float bowRegion=exp(-bowDistance*bowDistance*1.4);
            float bowWidth=2.0+moving*min(7.0,3.0+sqrt(max(axis.w,1.0))*0.55);
            float bowContact=exposed*(1.0-smoothstep(bowWidth,bowWidth+aa,distance))*bowRegion
                *moving*(0.45+0.55*smoothstep(0.25,0.7,flecks));
)HLSL";
    source+="            if(VisualHullMeta[h].w<0.5) {\n";
    source+=R"HLSL(            foam=max(foam,max(contact,bowContact*0.82));
            turbulence=max(turbulence,exposed*outer*(collision*0.18+moving*0.25)+bowContact*0.4);
)HLSL";
    source+="            }\n";
    source+=R"HLSL(        }
        if(speed>0.3) {
            float2 wakePoint=local-stern;
            float along=dot(wakePoint,aft),across=dot(wakePoint,side);
            float lengthWake=clamp(14.0+sqrt(max(axis.w,1.0))*3.0+speed*0.12,18.0,44.0);
            float trail=smoothstep(-1.5,2.0,along)*(1.0-smoothstep(lengthWake*0.45,lengthWake,along));
            float wakeWidth=max(1.0,axis.w*0.45)+max(along,0.0)*0.12;
            float churn=exp(-across*across/max(wakeWidth*wakeWidth,1.0));
            float structure=oceanNoise(float2(across*0.18,along*0.09-time*0.22)+motion.zz*9.0);
            float broken=smoothstep(0.26,0.72,structure);
            float threads=1.0-smoothstep(0.045,0.13,abs(oceanNoise(float2(across*0.32,along*0.07)+structure)-0.5));
            // Broad central mixing has gaps and thin irregular strands; the aged atlas continues
            // this field beyond the hull. No long rigidly rotating V-shaped foam strip is emitted.
            float wake=trail*moving*churn*(broken*0.70+threads*0.42);
            float outside=smoothstep(-0.2,0.3,hullDistance);
            foam=max(foam,wake*outside);
            turbulence=max(turbulence,trail*moving*churn*outside);
        }
    }
    return float3(saturate(foam)*offshore,saturate(turbulence)*offshore,damping);
}
)HLSL";
    return source;
}
}
