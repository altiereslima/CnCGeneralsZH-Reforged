/* Bounded camera window for render-only world-space foam. No simulation data is changed. */
#pragma once
#include "Platform/WaterFoamHistory.h"

namespace WaterFoamWindow {
struct Point { double x,y,z; };
// Rays are the view centre followed by its four NDC corners, in world space. A low
// horizon view falls back to a bounded window; ordinary RTS views tightly cover
// their water-plane footprint instead of spending most atlas texels off screen.
inline WaterFoamHistory::Mapping Choose(Point camera,const Point rays[5],double level) {
    using namespace WaterFoamHistory;
    auto hit=[&](Point ray) {
        const double t=ray.z<-0.0001?Clamp((level-camera.z)/ray.z,0,8192):4096;
        return Point{camera.x+ray.x*t,camera.y+ray.y*t,level};
    };
    const Point focus=hit(rays[0]);
    double lowX=focus.x,highX=focus.x,lowY=focus.y,highY=focus.y;
    bool bounded=camera.z>level;
    for(unsigned i=1;i<5;++i) {
        bounded=bounded && rays[i].z<-0.0001 && (level-camera.z)/rays[i].z<8192;
        const Point p=hit(rays[i]);
        lowX=(std::min)(lowX,p.x);highX=(std::max)(highX,p.x);
        lowY=(std::min)(lowY,p.y);highY=(std::max)(highY,p.y);
    }
    bounded=bounded && highX-lowX<3800 && highY-lowY<3800;
    double width=4096,height=4096,centerX=focus.x,centerY=focus.y;
    if(bounded) {
        width=std::ceil(Clamp((highX-lowX)*1.10+128,256,4096)/64)*64;
        height=std::ceil(Clamp((highY-lowY)*1.10+128,256,4096)/64)*64;
        centerX=(lowX+highX)*0.5;centerY=(lowY+highY)*0.5;
    } else {
        // A horizon centre ray has no nearby water hit. Centring the window on its
        // arbitrary far point would discard the visible water below the camera. Keep
        // the camera neighbourhood, then fit finite forward hits nearest-first. Distant
        // hits that cannot fit are omitted from this sampled window, not from history.
        constexpr double margin=128,step=64,maximumSpan=4096-2*margin-step;
        struct Hit { Point point;double distance; } hits[5];unsigned count=0;
        for(unsigned i=0;i<5;++i) {
            if(std::abs(rays[i].z)<=0.00000001) continue;
            const double t=(level-camera.z)/rays[i].z;
            const Point p={camera.x+rays[i].x*t,camera.y+rays[i].y*t,level};
            const double dx=p.x-camera.x,dy=p.y-camera.y,distance=dx*dx+dy*dy;
            if(t<0 || !std::isfinite(t) || !std::isfinite(distance)) continue;
            unsigned at=count;
            while(at>0 && hits[at-1].distance>distance) { hits[at]=hits[at-1];--at; }
            hits[at]={p,distance};++count;
        }
        lowX=highX=camera.x;lowY=highY=camera.y;
        for(unsigned i=0;i<count;++i) {
            const Point &p=hits[i].point;
            const double lx=(std::min)(lowX,p.x),hx=(std::max)(highX,p.x);
            const double ly=(std::min)(lowY,p.y),hy=(std::max)(highY,p.y);
            if(hx-lx<=maximumSpan && hy-ly<=maximumSpan) {lowX=lx;highX=hx;lowY=ly;highY=hy;}
        }
        // Bias unused space forwards, while quantized origin limits guarantee the near
        // footprint's margin even after snapping. This is still one fixed 512-square atlas.
        const double forwardLength=std::hypot(rays[0].x,rays[0].y);
        centerX=camera.x;centerY=camera.y;
        if(std::isfinite(forwardLength) && forwardLength>0.000001) {
            centerX+=1024*rays[0].x/forwardLength;centerY+=1024*rays[0].y/forwardLength;
        }
        auto origin=[&](double center,double low,double high) {
            return Clamp(std::floor((center-2048)/step)*step,
                std::ceil((high+margin-4096)/step)*step,std::floor((low-margin)/step)*step);
        };
        Mapping result;
        result.originX=origin(centerX,lowX,highX);result.originY=origin(centerY,lowY,highY);
        result.widthWorld=result.heightWorld=4096;
        result.widthPixels=result.heightPixels=MaxAtlasSide;
        return result;
    }
    Mapping result;
    result.originX=std::floor((centerX-width*0.5)/(width/64))*(width/64);
    result.originY=std::floor((centerY-height*0.5)/(height/64))*(height/64);
    result.widthWorld=width;result.heightWorld=height;
    result.widthPixels=result.heightPixels=MaxAtlasSide;
    return result;
}
}
