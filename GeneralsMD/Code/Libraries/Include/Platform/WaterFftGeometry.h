/* Render-only metadata for replacing an analytic ocean interior with GPU FFT displacement. */
#pragma once
#include "Platform/WaterProjectedMesh.h"
#include "Platform/WaterSurfaceModel.h"
#include <cmath>

namespace WaterFftGeometry {
constexpr float MaxVerticalDisplacement=WaterSurfaceModel::OceanMaxDisplacement;
constexpr float MaxHorizontalDisplacement=8.0f;
constexpr float FloorClearance=0.15f;
constexpr double InnerWorldCollar=WaterProjectedMesh::CollarWidth;
constexpr double OuterWorldCollar=InnerWorldCollar*2.0;
constexpr double InnerCellCollar=2.0;
constexpr double OuterCellCollar=4.0;

inline float SafeDamping(float damping,float terrainDepth) {
    if (!std::isfinite(damping) || !std::isfinite(terrainDepth)) return 0;
    const float floor=(terrainDepth-FloorClearance)/MaxVerticalDisplacement;
    return (std::max)(0.0f,(std::min)(1.0f,(std::min)(damping,floor)));
}
inline double SmoothRange(double value,double low,double high) {
    const double t=(std::max)(0.0,(std::min)(1.0,(value-low)/(high-low)));
    return t*t*(3.0-2.0*t);
}

// A cell cut by an authored boundary has diameter sqrt(2) in lattice coordinates.
// Keeping the first two cells analytic preserves every boundary knot, centre transition,
// and source anchor. The fade then changes continuously with camera motion and resize.
// Edge lines are transformed algebraically, so an authored endpoint at the horizon does
// not cause a divide-by-zero or disable the entire ocean for one camera frame.
class SeamFade {
    struct Edge { double x,y,c,worldScale,screenX,screenY,screenC,screenScale; };
    Edge edges[4];int count=0;
    WaterProjectedMesh::Projection projection{};
    bool ready=false;
public:
    bool Prepare(const WaterProjectedMesh::Point corners[4],
        const WaterProjectedMesh::Projection &source,WaterProjectedMesh::Resolution resolution) {
        count=0;ready=false;projection=source;
        if (resolution.columns<2 || resolution.rows<2) return false;
        const double stepX=2*(1+source.margin)/resolution.columns;
        const double stepY=2*(1+source.margin)/resolution.rows;
        if (!(stepX>0) || !(stepY>0) || !std::isfinite(stepX) || !std::isfinite(stepY)) return false;
        for (int i=0;i<4;++i) {
            auto a=corners[i],b=corners[(i+1)%4];
            if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
            Edge edge{};edge.x=a.y-b.y;edge.y=b.x-a.x;edge.c=a.x*b.y-b.x*a.y;
            const double length=std::sqrt(edge.x*edge.x+edge.y*edge.y);
            if (!std::isfinite(length)) return false;
            if (length<1e-12) continue;
            edge.worldScale=1.0/length;
            const double *m=source.inverse;
            edge.screenX=edge.x*m[0]+edge.y*m[3]+edge.c*m[6];
            edge.screenY=edge.x*m[1]+edge.y*m[4]+edge.c*m[7];
            edge.screenC=edge.x*m[2]+edge.y*m[5]+edge.c*m[8];
            const double screenLength=std::sqrt(edge.screenX*edge.screenX*stepX*stepX+
                edge.screenY*edge.screenY*stepY*stepY);
            if (!std::isfinite(screenLength) || !std::isfinite(edge.screenC)) return false;
            // A world edge lying on the horizon has no finite projected line and cannot cut a
            // visible finite lattice cell. Its world-space collar still remains active.
            edge.screenScale=screenLength>1e-12?1.0/screenLength:0.0;
            edges[count++]=edge;
        }
        ready=count>=3;return ready;
    }
    float Weight(WaterProjectedMesh::Point point,float spectralSupport) const {
        if (!ready || !std::isfinite(spectralSupport) || spectralSupport<=0) return 0;
        WaterProjectedMesh::Point screen;
        if (!projection.Transform(point,screen)) return 0;
        double worldDistance=OuterWorldCollar,cellDistance=OuterCellCollar;
        for (int i=0;i<count;++i) {
            const Edge &e=edges[i];
            const double world=std::abs(e.x*point.x+e.y*point.y+e.c)*e.worldScale;
            const double cells=e.screenScale>0?
                std::abs(e.screenX*screen.x+e.screenY*screen.y+e.screenC)*e.screenScale:OuterCellCollar;
            if (!std::isfinite(world) || !std::isfinite(cells)) return 0;
            worldDistance=(std::min)(worldDistance,world);cellDistance=(std::min)(cellDistance,cells);
        }
        return float((std::min)(1.0f,spectralSupport)*SmoothRange(worldDistance,InnerWorldCollar,OuterWorldCollar)*
            SmoothRange(cellDistance,InnerCellCollar,OuterCellCollar));
    }
};
}
