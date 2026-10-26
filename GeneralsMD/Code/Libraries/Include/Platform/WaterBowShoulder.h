/* Render-only exterior bow shoulder. No gameplay/transport state. */
#pragma once
#include "Platform/WaterHullModel.h"
#include <string>
namespace WaterBowShoulder {
#define WATER_BOW_SHOULDER_CONSTANTS(X) \
 X(ShoulderWidthBeam,.45f) X(ShoulderWidthMin,3.0f) X(ShoulderWidthMax,12.0f) \
 X(ShoulderOffset,.5f) X(ShoulderFadeStart,1.5f) X(ShoulderFadeEnd,3.0f)
#define WATER_BOW_SHOULDER_CPP(name,value) constexpr double name=value;
WATER_BOW_SHOULDER_CONSTANTS(WATER_BOW_SHOULDER_CPP)
#undef WATER_BOW_SHOULDER_CPP
struct Point { double x=0,y=0; };
inline Point operator+(Point a,Point b) { return {a.x+b.x,a.y+b.y}; }
inline Point operator-(Point a,Point b) { return {a.x-b.x,a.y-b.y}; }
inline Point operator*(Point a,double b) { return {a.x*b,a.y*b}; }
inline double Dot(Point a,Point b) { return a.x*b.x+a.y*b.y; }
inline double Clamp(double x,double a,double b) { return (std::max)(a,(std::min)(b,x)); }
inline bool Finite(Point p) { return std::isfinite(p.x)&&std::isfinite(p.y); }
struct Field { double value=0,dx=0,dy=0; };
inline Field Multiply(Field a,Field b) { return {a.value*b.value,a.dx*b.value+b.dx*a.value,a.dy*b.value+b.dy*a.value}; }
// C1 compact Gaussian. Exact zero outside |x|>=3, so broad-phase rejection is supported.
inline Field Profile(double x,Point gradient) {
 const double a=std::abs(x);if(a>=ShoulderFadeEnd)return {};
 const double t=Clamp((a-ShoulderFadeStart)/(ShoulderFadeEnd-ShoulderFadeStart),0,1);
 const double fade=1-t*t*(3-2*t),e=std::exp(-x*x);
 const double slope=e*(-2*x*fade-6*t*(1-t)/(ShoulderFadeEnd-ShoulderFadeStart)*(x<0?-1:1));
 return {e*fade,slope*gradient.x,slope*gradient.y};
}
struct Contour {
 unsigned count=0;Point points[12]{},centre{},axis{},forward{}; // vertices/forward in hull frame
 double level=0,halfLength=0,halfBeam=0,width=0,length=0,amplitude=0,reach=0,support=0;
 Point supportCentre{}; // continuous inset-section midpoint projected onto support plane; refinement only
};
inline bool Prepare(const WaterHullModel::Hull &h,double level,double length,double amplitude,Contour &out) {
 Contour c;if(!std::isfinite(level)||!std::isfinite(length)||!std::isfinite(amplitude)||length<=0||amplitude<0)return false;
 for(const auto &r:h.data)for(float f:r)if(!std::isfinite(f))return false;
 if(h.data[0][3]<3||h.data[0][3]>12||h.data[0][3]!=std::floor(h.data[0][3])||std::abs(h.data[0][2]-level)>.1)return false;
 c.count=unsigned(h.data[0][3]);c.centre={h.data[0][0],h.data[0][1]};c.axis={h.data[1][0],h.data[1][1]};
 if(std::abs(Dot(c.axis,c.axis)-1)>.001||h.data[1][2]<0||h.data[1][3]<0||h.data[2][3]<0)return false;
 Point velocity={h.data[2][0],h.data[2][1]};const double speed=std::hypot(velocity.x,velocity.y);if(speed<=double(.3f))return false;
 c.forward={Dot(velocity,c.axis)/speed,(-velocity.x*c.axis.y+velocity.y*c.axis.x)/speed};
 c.halfLength=h.data[1][2];c.halfBeam=h.data[1][3];c.level=level;c.reach=h.data[2][3]+110;
 c.width=Clamp(c.halfBeam*ShoulderWidthBeam,ShoulderWidthMin,ShoulderWidthMax);c.length=length;c.amplitude=amplitude;
 c.support=-1e30;double back=1e30;
 for(unsigned i=0;i<c.count;++i) {
  Point p={h.data[4+i/2][2*(i%2)],h.data[4+i/2][2*(i%2)+1]};c.points[i]=p;
  const double s=Dot(p,c.forward);
  c.support=(std::max)(c.support,s);back=(std::min)(back,s);
 }
 // Packets are CCW convex. Reject malformed contours; do not silently invent a footprint.
 for(unsigned i=0;i<c.count;++i){Point e=c.points[(i+1)%c.count]-c.points[i];if(Dot(e,e)<=1e-12)return false;
  for(unsigned j=0;j<c.count;++j){Point d=c.points[j]-c.points[i];if(e.x*d.y-e.y*d.x<-.0001)return false;}}
 // A support vertex can jump across a flat bow under an arbitrarily small yaw.
 // Place only the DETAIL WINDOW at the midpoint of a continuous inset cross-section.
 // The physical field depends solely on the continuous support value, never this centre.
 const double inset=(std::min)(c.width,(c.support-back)*.5),plane=c.support-inset;
 if(!(inset>1e-8))return false;
 const Point side={-c.forward.y,c.forward.x};double low=1e30,high=-1e30;
 auto retain=[&](Point p){double y=Dot(p,side);low=(std::min)(low,y);high=(std::max)(high,y);};
 for(unsigned i=0;i<c.count;++i){Point a=c.points[i],b=c.points[(i+1)%c.count];double da=Dot(a,c.forward)-plane,db=Dot(b,c.forward)-plane;
  if(da==0)retain(a);if((da<0&&db>0)||(da>0&&db<0))retain(a+(b-a)*(da/(da-db)));}
 if(low>high)return false;
 c.supportCentre=c.forward*c.support+side*((low+high)*.5);
 out=c;return true;
}
inline Point ToLocal(const Contour &c,Point p) { p=p-c.centre;return {Dot(p,c.axis),-p.x*c.axis.y+p.y*c.axis.x}; }
inline Point ToWorld(const Contour &c,Point p) { return c.centre+Point{c.axis.x*p.x-c.axis.y*p.y,c.axis.y*p.x+c.axis.x*p.y}; }
struct Contact { double distance=0,retreat=0;Point normal{},retreatGradient{},closest{};bool valid=false; };
inline Contact QueryLocal(const Contour &c,Point p) {
 Contact q;if(c.count<3||c.count>12||!Finite(p))return q;
 double best=1e30;bool inside=true;Point selectedEdge{};
 for(unsigned i=0;i<c.count;++i) {
  Point a=c.points[i],edge=c.points[(i+1)%c.count]-a,from=p-a;
  const double square=Dot(edge,edge),t=Clamp(Dot(from,edge)/(std::max)(square,1e-12),0,1);
  const Point closest=a+edge*t,separation=p-closest;const double d=Dot(separation,separation);
  if(d<best) {best=d;q.closest=closest;selectedEdge=edge;}
  inside=inside&&(edge.x*from.y-edge.y*from.x>=0);
 }
 const double distance=std::sqrt((std::max)(best,0.0)),sign=inside?-1:1;
 q.distance=sign*distance;
 if(distance>1e-8)q.normal=(p-q.closest)*(sign/distance);
 else {const double n=std::hypot(selectedEdge.x,selectedEdge.y);q.normal={selectedEdge.y/n,-selectedEdge.x/n};}
 q.retreat=(std::max)(0.0,c.support-Dot(p,c.forward));q.retreatGradient=q.retreat>0?c.forward*(-1):Point{};q.valid=true;return q;
}
inline Field Evaluate(const Contour &c,Point world) {
 if(c.count<3||c.count>12||!Finite(world)||c.width<=0||c.length<=0||c.amplitude<=0)return {};
 const Point delta=world-c.centre;const double radius=std::hypot(delta.x,delta.y);if(radius>=c.reach)return {};
 const Point p=ToLocal(c,world);const double extent=(ShoulderFadeEnd+ShoulderOffset)*c.width;
 if(std::abs(p.x)>c.halfLength+extent||std::abs(p.y)>c.halfBeam+extent)return {};
 const Contact q=QueryLocal(c,p);if(!q.valid)return {};
 Field field=Multiply(Profile((q.distance-ShoulderOffset*c.width)/c.width,q.normal*(1/c.width)),
   Profile(q.retreat/c.length,q.retreatGradient*(1/c.length)));
 field.value*=c.amplitude;field.dx*=c.amplitude;field.dy*=c.amplitude;
 const Point gradient={c.axis.x*field.dx-c.axis.y*field.dy,c.axis.y*field.dx+c.axis.x*field.dy};field.dx=gradient.x;field.dy=gradient.y;
 const double t=Clamp((radius-(c.reach-30))/30,0,1),fade=1-t*t*(3-2*t),slope=-6*t*(1-t)/30;
 return Multiply(field,{fade,slope*delta.x/(std::max)(radius,.001),slope*delta.y/(std::max)(radius,.001)});
}
// Safe lower bounds for every registered point within horizontal radius R. The signed
// distance is 1-Lipschitz and the unit-direction support-plane retreat is 1-Lipschitz.
// The envelope uses the sample position, avoiding closest-point tie jumps inside the hull.
inline Point InverseProfileLowerBounds(const Contour &c,Point world,double radius) {
 const Point p=ToLocal(c,world);const Contact q=QueryLocal(c,p);if(!q.valid)return {1e6,1e6};
 return {(std::max)(0.0,(std::abs(q.distance-ShoulderOffset*c.width)-radius)/c.width),
  (std::max)(0.0,c.support-Dot(p,c.forward)-radius)/c.length};
}
inline std::string Source() {
 std::string s;
#define WATER_BOW_SHOULDER_HLSL(name,value) s+="static const float " #name "=" #value ";\n";
 WATER_BOW_SHOULDER_CONSTANTS(WATER_BOW_SHOULDER_HLSL)
#undef WATER_BOW_SHOULDER_HLSL
 return s+R"HLSL(
float3 vesselShoulderProfile(float3 x) {
    float a=abs(x.x);if(a>=ShoulderFadeEnd)return 0;
    float t=saturate((a-ShoulderFadeStart)/(ShoulderFadeEnd-ShoulderFadeStart));
    float fade=1.0-t*t*(3.0-2.0*t),e=exp(-x.x*x.x);
    float slope=e*(-2.0*x.x*fade-6.0*t*(1.0-t)/(ShoulderFadeEnd-ShoulderFadeStart)*(x.x<0.0?-1.0:1.0));
    return float3(e*fade,x.yz*slope);
}
// One query at the final registered XY, never a second query in the raw/stern loop.
// CCW convex packet and unit axis are provided by the existing bounded hull collector.
float3 vesselContourBow(float2 world,float level,int h) {
    int base=h*10;
    float4 position=vesselPacketRegister(base),axis=vesselPacketRegister(base+1),motion=vesselPacketRegister(base+2);
    int count=(int)position.w;float speed=length(motion.xy);
    if(count<3||count>12||abs(level-position.z)>0.1||speed<=0.3)return 0;
    float2 delta=world-position.xy;float radius=length(delta),reach=motion.w+110.0;
    if(radius>=reach)return 0;
    float width=clamp(axis.w*ShoulderWidthBeam,ShoulderWidthMin,ShoulderWidthMax);
    float2 p=float2(dot(delta,axis.xy),dot(delta,float2(-axis.y,axis.x)));
    float extent=(ShoulderFadeEnd+ShoulderOffset)*width;
    if(abs(p.x)>axis.z+extent||abs(p.y)>axis.w+extent)return 0;
    float2 forward=float2(dot(motion.xy,axis.xy),dot(motion.xy,float2(-axis.y,axis.x)))/speed;
    float best=1.0e30,support=-1.0e30;bool inside=true;
    float2 closest=0,selectedEdge=0;
    for(int j=0;j<count;++j) {
        int next=(j+1)%count;
        float4 pa=vesselPacketRegister(base+4+j/2),pb=vesselPacketRegister(base+4+next/2);
        float2 a=(j&1)!=0?pa.zw:pa.xy,b=(next&1)!=0?pb.zw:pb.xy;
        float2 edge=b-a,from=p-a;float square=max(dot(edge,edge),1.0e-12);
        float t=saturate(dot(from,edge)/square);float2 q=a+t*edge,separation=p-q;
        float d=dot(separation,separation);
        if(d<best){best=d;closest=q;selectedEdge=edge;}
        inside=inside&&(edge.x*from.y-edge.y*from.x>=0.0);support=max(support,dot(a,forward));
    }
    float distance=sqrt(max(best,0.0)),sign=inside?-1.0:1.0;
    float2 normal=distance>1.0e-8?(p-closest)*(sign/distance):float2(selectedEdge.y,-selectedEdge.x)/max(length(selectedEdge),1.0e-8);
    float3 pressure=vesselBowPressure(axis.w,speed);
    float3 ridge=vesselShoulderProfile(float3((sign*distance-ShoulderOffset*width)/width,normal/width));
    float retreat=max(0.0,support-dot(p,forward));
    float3 aft=vesselShoulderProfile(float3(retreat/pressure.x,retreat>0.0?-forward/pressure.x:float2(0,0)));
    float3 field=vwMul(ridge,aft)*pressure.z;
    field.yz=float2(axis.x*field.y-axis.y*field.z,axis.y*field.y+axis.x*field.z);
    float3 radial=float3(radius,delta/max(radius,0.001));
    return vwMul(field,vwConstant(1.0)-vwSmooth(motion.w+80.0,reach,radial));
}
)HLSL";
}
#undef WATER_BOW_SHOULDER_CONSTANTS
}
