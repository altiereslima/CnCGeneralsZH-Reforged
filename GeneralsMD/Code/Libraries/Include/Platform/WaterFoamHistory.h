/* Render-only, world-space vessel foam. No gameplay state or water height is modified. */
#pragma once
#include "Platform/WaterHullModel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace WaterFoamHistory {
constexpr unsigned MaxSegments=1024,MaxEmitters=64,MaxAtlasSide=512;
constexpr double DirectionWidthScale=32.0; // maximum initial lane width is30 world units
constexpr double SampleInterval=0.25,Lifetime=24.0,MaxObservationGap=0.5;
struct Point { double x=0,y=0; };
struct Mapping {
    double originX=0,originY=0,widthWorld=0,heightWorld=0;
    unsigned widthPixels=0,heightPixels=0;
};
struct Sample {
    Point center,axis,lanes[3]; // stern, port and starboard waterline
    double width[3]={},rate[3]={};
    double level=0,time=0,halfLength=0,halfWidth=0;
};
struct Segment { Sample begin,end; std::uint64_t id=0; };
inline double Clamp(double x,double a,double b) { return (std::max)(a,(std::min)(b,x)); }
inline double Smooth(double low,double high,double x) { const double t=Clamp((x-low)/(high-low),0,1);return t*t*(3-2*t); }
inline bool Finite(Point p) { return std::isfinite(p.x)&&std::isfinite(p.y); }
inline Point Mix(Point a,Point b,double t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}; }
inline double Length(Point p) { return std::sqrt(p.x*p.x+p.y*p.y); }
inline double Distance(Point a,Point b) { return Length({a.x-b.x,a.y-b.y}); }
struct KernelIntegral { double mass=0,mean=0.5; };
// Average compact radial quartic over a uniformly deposited line, plus its weighted
// deposition coordinate. Multiplying mass by duration makes adjacent time sections
// additive: each shared endpoint contributes only its own half of the radial support.
inline KernelIntegral IntegrateKernel(double along,double perpendicularSquared,double length,double radius) {
    if(!std::isfinite(along)||!std::isfinite(perpendicularSquared)||!std::isfinite(length)||
        !std::isfinite(radius)||perpendicularSquared<0||length<0||radius<=0) return {};
    const double q=1-perpendicularSquared/(radius*radius);
    if(q<=0) return {};
    if(length<=1.0e-10) {
        const double kernel=q-along*along/(radius*radius);
        return {kernel>0?kernel*kernel:0,0.5};
    }
    const double support=radius*std::sqrt(q);
    const double low=(std::max)(-support,-along)/radius;
    const double high=(std::min)(support,length-along)/radius;
    if(low>=high) return {};
    const double mid=(low+high)*0.5,half=(high-low)*0.5,m2=mid*mid,h2=half*half;
    // Integrate around the interval midpoint. This avoids subtracting nearly equal
    // antiderivatives for very short live tails or support intervals near an endpoint.
    const double average=(std::max)(0.0,(q-m2)*(q-m2)+(-2*q+6*m2)*h2/3+h2*h2/5);
    if(average<=0) return {};
    const double first=q*q*mid-2*q*(mid*m2+mid*h2)
        +mid*m2*m2+(10.0/3)*mid*m2*h2+mid*h2*h2;
    return {Clamp((high-low)*radius/length*average,0,1),
        Clamp((along+radius*first/average)/length,0,1)};
}
inline Sample Interpolate(const Sample &a,const Sample &b,double time) {
    const double t=Clamp((time-a.time)/(b.time-a.time),0,1);Sample s=a;
    s.center=Mix(a.center,b.center,t);s.axis=Mix(a.axis,b.axis,t);s.time=time;
    for(unsigned i=0;i<3;++i) { s.lanes[i]=Mix(a.lanes[i],b.lanes[i],t);
        s.width[i]=a.width[i]+(b.width[i]-a.width[i])*t;s.rate[i]=a.rate[i]+(b.rate[i]-a.rate[i])*t; }
    return s;
}
inline bool MakeSample(const WaterHullModel::Hull &h,double time,Sample &s) {
    if(!std::isfinite(time)) return false;
    for(unsigned i=0;i<WaterHullModel::RegistersPerHull;++i)
        for(unsigned j=0;j<4;++j) if(!std::isfinite(h.data[i][j])) return false;
    if(h.data[0][3]<3 || h.data[0][3]>12 || h.data[1][2]<=0 || h.data[1][3]<=0 ||
        h.data[1][2]>4096 || h.data[1][3]>4096) return false;
    s=Sample();s.center={h.data[0][0],h.data[0][1]};s.axis={h.data[1][0],h.data[1][1]};
    const double axisLength=Length(s.axis);if(axisLength<0.5 || axisLength>1.5) return false;
    s.axis.x/=axisLength;s.axis.y/=axisLength;s.level=h.data[0][2];s.time=time;
    s.halfLength=h.data[1][2];s.halfWidth=h.data[1][3];
    const Point velocity={h.data[2][0],h.data[2][1]};const double speed=Length(velocity);
    const double forward=Smooth(-0.25,0.25,(velocity.x*s.axis.x+velocity.y*s.axis.y)/(std::max)(speed,0.001));
    const Point stern=Mix({h.data[3][2],h.data[3][3]},{h.data[3][0],h.data[3][1]},forward);
    auto world=[&](Point p) { return Point{s.center.x+s.axis.x*p.x-s.axis.y*p.y,s.center.y+s.axis.y*p.x+s.axis.x*p.y}; };
    // Intersect the actual convex contour with its central cross-section. This keeps side
    // emission on the sliced waterline instead of the gameplay collision box.
    const WaterHullModel::Polygon polygon=WaterHullModel::Unpack(h);
    double low=1.0e30,high=-1.0e30;
    for(unsigned i=0;i<polygon.count;++i) {
        const auto a=polygon.points[i],b=polygon.points[(i+1)%polygon.count];
        if(a.x==0) {low=(std::min)(low,double(a.y));high=(std::max)(high,double(a.y));}
        if((a.x<0 && b.x>0)||(a.x>0 && b.x<0)) {
            const double y=a.y+(b.y-a.y)*(-double(a.x)/(double(b.x)-a.x));
            low=(std::min)(low,y);high=(std::max)(high,y);
        }
    }
    if(low>high) return false;
    // The extremal aft vertex can jump between transom corners as the hull pitches.
    // Retained propulsion wash follows the transverse centre of the actual midship
    // section, projected to the same velocity-selected aft X; contact foam still uses
    // the exact contour. Keep the recorded path, rates and aging independent of that corner.
    s.lanes[0]=world({stern.x,(low+high)*0.5});
    s.lanes[1]=world({0,low});s.lanes[2]=world({0,high});
    // Propeller churn spreads beyond a narrow hull. The sublinear term gives small
    // boats a readable stern trail without scaling large-ship support without bound.
    s.width[0]=Clamp(s.halfWidth*0.30+std::sqrt(s.halfWidth)*0.8,2.5,30.0);
    s.width[1]=s.width[2]=Clamp(s.halfWidth*0.055,0.65,3.0);
    const double response=2.0*Smooth(0.3,12.0,speed)*(0.8+0.25*std::sqrt((std::min)(speed,250.0)));
    // The radial support area grows with width squared, while a transom source
    // grows approximately with its span. Remove one width factor from stern dose
    // above the six-world small-boat footprint; its existing response stays exact.
    // Width/spreading and side wash retain their own independent contracts.
    s.rate[0]=response*(std::min)(1.0,6.0/s.width[0]);
    s.rate[1]=s.rate[2]=response*0.22;
    return true;
}

class History {
    struct Emitter { std::uint64_t id=0,seen=0;Sample observed,anchor,moving;double next=0; };
    std::vector<Emitter> emitters;
    std::vector<Segment> segments;
    double now=0;std::uint64_t serial=0;bool initialized=false;
    void Anchor(Emitter &e,const Sample &s) {
        e.observed=e.anchor=e.moving=s;e.next=(std::floor(s.time/SampleInterval)+1)*SampleInterval;e.seen=serial;
    }
    static bool Active(const Sample &a,const Sample &b) {
        if(b.time<=a.time || Distance(a.center,b.center)<=1.0e-6) return false;
        for(unsigned i=0;i<3;++i) if(a.rate[i]+b.rate[i]>1.0e-6) return true;
        return false;
    }
    bool Record(const Sample &a,const Sample &b,std::uint64_t id) {
        if(!Active(a,b) || now-b.time>=Lifetime) return false;
        if(segments.size()==MaxSegments) segments.erase(segments.begin());
        segments.push_back({a,b,id});return true;
    }
    void RememberTail(const Emitter &e) {Record(e.anchor,e.moving,e.id);}
public:
    void Clear() { emitters.clear();segments.clear();now=0;serial=0;initialized=false; }
    double Time() const { return now; }
    size_t SegmentCount() const { return segments.size(); }
    size_t TrackedCount() const { return emitters.size(); }
    const std::vector<Segment> &Segments() const { return segments; }
    bool HasFoam() const {
        if(!segments.empty()) return true;
        for(const Emitter &e:emitters)
            if(Active(e.anchor,e.moving) && now-(e.anchor.time+e.moving.time)*0.5<Lifetime) return true;
        return false;
    }
    bool Advance(double time) {
        if(!std::isfinite(time)) {Clear();return false;}
        if(initialized && time<now) Clear();
        if(initialized && time==now) return false;
        initialized=true;now=time;++serial;
        segments.erase(std::remove_if(segments.begin(),segments.end(),[&](const Segment &s){return now-s.end.time>=Lifetime;}),segments.end());
        return true;
    }
    bool Observe(std::uint64_t id,const WaterHullModel::Hull &h,double time,bool visible=true,bool discontinuity=false,
        const Point *trueMotionPosition=nullptr) {
        if(!initialized || time>now) Advance(time);
        auto found=std::find_if(emitters.begin(),emitters.end(),[&](const Emitter &e){return e.id==id;});
        Sample sample;
        if(!visible || !std::isfinite(time) || time!=now || !MakeSample(h,time,sample) ||
            (trueMotionPosition && !Finite(*trueMotionPosition))) {
            if(found!=emitters.end()) {RememberTail(*found);emitters.erase(found);}return false;
        }
        // A slab's geometric center can shift as a stationary vessel pitches/rolls. Keep
        // the actual waterline lanes, but gate new deposition on displayed translation.
        if(trueMotionPosition) sample.center=*trueMotionPosition;
        if(found==emitters.end()) {
            if(emitters.size()==MaxEmitters) {
                auto oldest=std::min_element(emitters.begin(),emitters.end(),
                    [](const Emitter &a,const Emitter &b){return a.observed.time<b.observed.time;});
                RememberTail(*oldest);emitters.erase(oldest);
            }
            emitters.push_back(Emitter());Emitter &e=emitters.back();e.id=id;Anchor(e,sample);return false;
        }
        Emitter &e=*found;
        if(discontinuity || sample.level!=e.observed.level) {RememberTail(e);Anchor(e,sample);return false;}
        const double dt=time-e.observed.time;
        if(dt<=0) return false; // Duplicate scene/reflection draws and pause cannot emit twice.
        const double travel=Distance(sample.center,e.observed.center);
        const double declaredSpeed=Length({h.data[2][0],h.data[2][1]});
        // Explicit motion-reset flags are authoritative. The generous secondary guard allows
        // accelerated native simulation to move farther per wall-clock tick without inventing
        // a cross-map connection after a spawn/teleport or an unseen interval.
        const double reach=(std::max)((std::max)(64.0,2*(sample.halfLength+sample.halfWidth)),
            8*(std::max)(250.0,declaredSpeed)*dt);
        const double heading=sample.axis.x*e.observed.axis.x+sample.axis.y*e.observed.axis.y;
        const double sternTravel=Distance(sample.lanes[0],e.observed.lanes[0]);
        const double turnAllowance=2*sample.halfLength*std::sqrt((std::max)(0.0,2*(1-heading)));
        if(dt>MaxObservationGap || e.seen+1<serial || travel>reach || heading<0 ||
            sternTravel>travel+turnAllowance+(std::max)(16.0,sample.halfWidth*2)) {RememberTail(e);Anchor(e,sample);return false;}
        if(travel>1.0e-6) e.moving=sample;
        bool emitted=false;
        while(e.next<=time+1.0e-9) {
            const Sample next=Interpolate(e.observed,sample,e.next);
            // Judge motion over the fixed deposition interval, not a render-to-render delta.
            // Uninterpolated30Hz models are stationary on most120Hz render samples, while a
            // genuinely stopped vessel has no travel over the complete section even if its
            // filtered physics velocity has not decayed yet.
            // Do not extend dose duration after motion stops, including the final incomplete
            // section. Its already-visible tail is committed unchanged at the next boundary.
            const Sample &end=e.moving.time<next.time?e.moving:next;
            emitted=Record(e.anchor,end,id)||emitted;
            e.anchor=next;e.next+=SampleInterval;
        }
        e.observed=sample;e.seen=serial;return emitted;
    }
    // RGBA is packed as ARGB: R=opacity, G=opacity*normalized mean age,
    // B=opacity*fresh churn, A=0 reserved for a distinct shoreline source.
    // Optional second tile retains the *deposited* lane orientation, not a vessel's current
    // heading. RG hold opacity-weighted axial moments (cos2theta/sin2theta biased to0..1),
    // B the weighted initial lane width/32, A the same opacity as the first tile's R.
    // Axial moments combine opposite travel directions and lose confidence at crossings.
    bool BuildAtlas(const Mapping &map,float level,std::vector<std::uint32_t> &argb,
        std::vector<std::uint32_t> *direction=nullptr) const {
        argb.clear();
        if(direction) direction->clear();
        if(!std::isfinite(map.originX)||!std::isfinite(map.originY)||!std::isfinite(map.widthWorld)||
            !std::isfinite(map.heightWorld)||map.widthWorld<=0||map.heightWorld<=0||!std::isfinite(level)||
            map.widthPixels<1||map.heightPixels<1||map.widthPixels>MaxAtlasSide||map.heightPixels>MaxAtlasSide) return false;
        struct Cell {float density=0,age=0,fresh=0;};
        struct DirectionCell {float x=0,y=0,width=0;};
        const size_t count=size_t(map.widthPixels)*map.heightPixels;std::vector<Cell> cells(count);
        std::vector<DirectionCell> directions(direction?count:0);
        if(direction) direction->resize(count);
        argb.resize(count);const double pixelX=map.widthWorld/map.widthPixels,pixelY=map.heightWorld/map.heightPixels;
        auto rasterize=[&](const Segment &segment) {
            if(segment.begin.level!=level || now-segment.end.time>=Lifetime) return;
            const double duration=segment.end.time-segment.begin.time;
            for(unsigned lane=0;lane<3;++lane) {
                if(segment.begin.rate[lane]+segment.end.rate[lane]<=0) continue;
                const Point a=segment.begin.lanes[lane],b=segment.end.lanes[lane],line={b.x-a.x,b.y-a.y};
                const double length=Length(line);
                const Point axis=length>1.0e-10?Point{line.x/length,line.y/length}:Point{1,0};
                double ages[2],radii[2],amplitudes[2],freshness[2];
                for(unsigned end=0;end<2;++end) {
                    const Sample &sample=end?segment.end:segment.begin;
                    const double age=Clamp(now-sample.time,0,Lifetime),initial=sample.width[lane];
                    const double spread=(lane==0?0.28:0.10)+std::sqrt(initial)*(lane==0?0.12:0.06);
                    const double aged=initial+spread*age;
                    ages[end]=age/Lifetime;radii[end]=2.5*aged;
                    amplitudes[end]=sample.rate[lane]*std::exp(-age/(lane==0?7.0:3.5))
                        *(1-Smooth(20,Lifetime,age))*initial*initial/(aged*aged);
                    freshness[end]=std::exp(-age/1.5);
                }
                const double radius=(std::max)(radii[0],radii[1]);
                const int left=int(Clamp(std::floor(((std::min)(a.x,b.x)-radius-map.originX)/pixelX),0,map.widthPixels));
                const int right=int(Clamp(std::ceil(((std::max)(a.x,b.x)+radius-map.originX)/pixelX),0,map.widthPixels));
                const int bottom=int(Clamp(std::floor(((std::min)(a.y,b.y)-radius-map.originY)/pixelY),0,map.heightPixels));
                const int top=int(Clamp(std::ceil(((std::max)(a.y,b.y)+radius-map.originY)/pixelY),0,map.heightPixels));
                for(int y=bottom;y<top;++y) for(int x=left;x<right;++x) {
                    const Point p={map.originX+(x+0.5)*pixelX,map.originY+(y+0.5)*pixelY};
                    const double dx=p.x-a.x,dy=p.y-a.y,along=dx*axis.x+dy*axis.y,cross=dx*axis.y-dy*axis.x;
                    const double nearest=length>1.0e-10?Clamp(along/length,0,1):0.5;
                    const KernelIntegral kernel=IntegrateKernel(along,cross*cross,length,
                        radii[0]+(radii[1]-radii[0])*nearest);
                    if(kernel.mass<=0) continue;
                    // Endpoint decay/rates are interpolated at the kernel's mean deposition
                    // time. Age is continuous along a section; no per-pixel exponential is needed.
                    const double first=amplitudes[0]*(1-kernel.mean),last=amplitudes[1]*kernel.mean;
                    const double dose=duration*kernel.mass,density=dose*(first+last);
                    Cell &cell=cells[size_t(y)*map.widthPixels+x];
                    cell.density+=float(density);
                    cell.age+=float(dose*(first*ages[0]+last*ages[1]));
                    cell.fresh+=float(dose*(first*freshness[0]+last*freshness[1]));
                    if(direction) {
                        DirectionCell &d=directions[size_t(y)*map.widthPixels+x];
                        // A zero-length lane has no recorded path direction. Its density still
                        // contributes to the denominator, softly reducing directional confidence.
                        if(length>1.0e-6) {
                            d.x+=float(density*(axis.x*axis.x-axis.y*axis.y));
                            d.y+=float(density*(2*axis.x*axis.y));
                        }
                        d.width+=float(dose*(first*segment.begin.width[lane]+last*segment.end.width[lane]));
                    }
                }
            }
        };
        for(const Segment &segment:segments) rasterize(segment);
        // Preview only the uncommitted time interval. This closes the newest stern gap even
        // at high speed, without adding render-rate-dependent stamps or overlapping history.
        // Unobserved tails remain fixed and age normally until removal/eviction commits them.
        for(const Emitter &e:emitters)
            if(Active(e.anchor,e.moving)) rasterize({e.anchor,e.moving,e.id});
        for(size_t i=0;i<count;++i) {
            const Cell &cell=cells[i];if(cell.density<=0) {argb[i]=0;continue;}
            const double opacity=1-std::exp(-double(cell.density));
            const unsigned r=unsigned(Clamp(opacity*255+0.5,0,255));
            const unsigned g=unsigned(Clamp(opacity*cell.age/cell.density*255+0.5,0,255));
            const unsigned b=unsigned(Clamp(opacity*cell.fresh/cell.density*255+0.5,0,255));
            argb[i]=(r<<16)|(g<<8)|b;
            if(direction) {
                const DirectionCell &d=directions[i];
                const unsigned dx=unsigned(Clamp(opacity*(1+Clamp(d.x/cell.density,-1,1))*127.5+0.5,0,255));
                const unsigned dy=unsigned(Clamp(opacity*(1+Clamp(d.y/cell.density,-1,1))*127.5+0.5,0,255));
                const unsigned width=unsigned(Clamp(opacity*d.width/cell.density/DirectionWidthScale*255+0.5,0,255));
                (*direction)[i]=(r<<24)|(dx<<16)|(dy<<8)|width;
            }
        }
        return true;
    }
};
} // namespace WaterFoamHistory
