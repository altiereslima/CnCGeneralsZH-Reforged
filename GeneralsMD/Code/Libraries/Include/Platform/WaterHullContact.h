/* Current-mesh contact footprint within reachable crests. Never gameplay/collision geometry. */
#pragma once
#include "Platform/WaterHullModel.h"
#include "Platform/WaterFftGeometry.h"

namespace WaterHullContact {
// Foam need not use the whole theoretical crest envelope. Keep its contour close to the
// visible lower hull while respecting the same floor clearance and displacement bound.
constexpr float ContactCrestCap=3.0f;
inline float ReachableCrest(float terrainDepth,float terrainAttenuation=1.0f) {
    return (std::min)(ContactCrestCap,WaterFftGeometry::MaxVerticalDisplacement*
        WaterFftGeometry::SafeDamping(terrainAttenuation,terrainDepth));
}

// Inputs use the current displayed vessel frame, with Z relative to mean water. Clip
// the actual triangle to the reachable slab before XY projection. Vertices inside the
// slab matter: only slicing its two endpoint planes would miss protrusions between them.
// The caller retains the independent mean-water contour for damping and wave deformation.
inline bool ProjectTriangle(WaterHullModel::Point3 a,WaterHullModel::Point3 b,
    WaterHullModel::Point3 c,float crestRise,std::vector<WaterHullModel::Point>& points) {
    using WaterHullModel::Point3;
    if(!std::isfinite(crestRise)||crestRise<0||crestRise>ContactCrestCap) return false;
    Point3 input[8]={a,b,c},output[8];unsigned count=3;
    for(unsigned i=0;i<count;++i)
        if(!std::isfinite(input[i].x)||!std::isfinite(input[i].y)||!std::isfinite(input[i].z)) return false;
    for(unsigned plane=0;plane<2 && count;++plane) {
        unsigned clipped=0;
        for(unsigned i=0;i<count;++i) {
            const Point3 u=input[i],v=input[(i+1)%count];
            const double du=plane?double(crestRise)-u.z:u.z,dv=plane?double(crestRise)-v.z:v.z;
            if(du>=0) {
                if(clipped==8) return false;
                output[clipped++]=u;
            }
            if((du<0 && dv>0)||(du>0 && dv<0)) {
                if(clipped==8) return false;
                const double t=du/(du-dv);
                output[clipped++]={float(u.x+(double(v.x)-u.x)*t),float(u.y+(double(v.y)-u.y)*t),
                    plane?crestRise:0.0f};
            }
        }
        count=clipped;
        for(unsigned i=0;i<count;++i) input[i]=output[i];
    }
    if(points.size()>WaterHullModel::MaxSlicePoints || count>WaterHullModel::MaxSlicePoints-points.size()) return false;
    for(unsigned i=0;i<count;++i) points.push_back({input[i].x,input[i].y});
    return true;
}
}
