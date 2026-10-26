#pragma once
#include "Platform/WaterVesselWaves.h"
#include <cassert>
namespace WaterBowShoulderTest {
namespace S=WaterBowShoulder;
WaterHullModel::Hull hull(double speed=40,double angle=0) {
 WaterHullModel::Hull h;h.data[0][0]=127;h.data[0][1]=-83;h.data[0][2]=4;h.data[0][3]=5;
 h.data[1][0]=std::cos(angle);h.data[1][1]=std::sin(angle);h.data[1][2]=40;h.data[1][3]=9;
 h.data[2][0]=speed*std::cos(angle);h.data[2][1]=speed*std::sin(angle);h.data[2][3]=42;
 h.data[3][0]=-40;h.data[3][2]=40;
 double p[5][2]={{-40,-9},{20,-9},{40,0},{20,9},{-40,9}};
 for(unsigned i=0;i<5;++i){h.data[4+i/2][2*(i%2)]=p[i][0];h.data[4+i/2][2*(i%2)+1]=p[i][1];}return h;
}
// Larger analytic hull keeps the same normalized forebody shape; independent fixture
// geometry, not a claim to be the exact carrier mesh or its instantaneous waterline.
inline WaterHullModel::Hull scaledHull(double halfLength,double halfBeam,double speed=40,double angle=0) {
 auto h=hull(speed,angle);const double sx=halfLength/40,sy=halfBeam/9;
 h.data[1][2]=float(halfLength);h.data[1][3]=float(halfBeam);
 h.data[2][3]=float(std::hypot(halfLength,halfBeam));
 h.data[3][0]=float(-halfLength);h.data[3][2]=float(halfLength);
 for(unsigned i=0;i<5;++i){h.data[4+i/2][2*(i%2)]*=float(sx);h.data[4+i/2][2*(i%2)+1]*=float(sy);}return h;
}
S::Contour contour(const WaterHullModel::Hull& h) {S::Contour c;const auto p=WaterVesselWaves::BowPressure(h.data[1][3],std::hypot(h.data[2][0],h.data[2][1]));const bool prepared=S::Prepare(h,4,p.along,p.amplitude,c);assert(prepared);(void)prepared;return c;}
// Independent scalar oracle: segment distance + winding + velocity support; no production
// query/profile/gradient calls. World packet transform intentionally expanded separately.
double oracle(const WaterHullModel::Hull& h,double x,double y,double widthCap=12.) {
 const double speed=std::hypot(h.data[2][0],h.data[2][1]);if(speed<=double(.3f))return 0;
 const double dx=x-h.data[0][0],dy=y-h.data[0][1],radius=std::hypot(dx,dy),reach=h.data[2][3]+110;
 if(radius>=reach)return 0;
 const double px=dx*h.data[1][0]+dy*h.data[1][1],py=-dx*h.data[1][1]+dy*h.data[1][0];
 const double fx=(h.data[2][0]*h.data[1][0]+h.data[2][1]*h.data[1][1])/speed,fy=(-h.data[2][0]*h.data[1][1]+h.data[2][1]*h.data[1][0])/speed;
 double best=1e99,support=-1e99;bool inside=true;unsigned n=unsigned(h.data[0][3]);
 auto vertex=[&](unsigned i,unsigned d){return double(h.data[4+i/2][2*(i%2)+d]);};
 for(unsigned i=0;i<n;++i){unsigned k=(i+1)%n;double ax=vertex(i,0),ay=vertex(i,1),ex=vertex(k,0)-ax,ey=vertex(k,1)-ay;
  double t=(std::max)(0.,(std::min)(1.,((px-ax)*ex+(py-ay)*ey)/(ex*ex+ey*ey)));double qx=ax+t*ex,qy=ay+t*ey,d=(px-qx)*(px-qx)+(py-qy)*(py-qy);
  if(d<best){best=d;}inside=inside&&ex*(py-ay)-ey*(px-ax)>=0;support=(std::max)(support,ax*fx+ay*fy);}
 const double beam=h.data[1][3],width=(std::max)(3.,(std::min)(widthCap,beam*double(.45f))),length=(std::max)(9.,(std::min)(32.,6+double(1.2f)*beam));
 const double u=(std::max)(0.,(std::min)(250.,speed)-double(.3f)),amplitude=(std::max)(3.,(std::min)(8.,1.8*std::sqrt(beam)))*u*u/(u*u+26*26);
 auto profile=[](double a){a=std::abs(a);if(a>=3)return 0.;double t=(std::max)(0.,(std::min)(1.,(a-1.5)/1.5));return std::exp(-a*a)*(1-t*t*(3-2*t));};
 const double ridge=((inside?-1:1)*std::sqrt(best)-.5*width)/width,aft=(std::max)(0.,support-px*fx-py*fy)/length;
 const double t=(std::max)(0.,(std::min)(1.,(radius-(reach-30))/30));return amplitude*profile(ridge)*profile(aft)*(1-t*t*(3-2*t));
}

// Original captured PT contact slabs at frames80/201/381, not current clock poses.
inline WaterHullModel::Hull CapturedHull(unsigned sample) {
 WaterHullModel::Hull h;
 static const double meta[3][8]={
 {2850,100,1,0,-0.796630859,0.805389404,43.5699959,8.79331398},
 {2957.32373,101.627365,0.999879956,0.0154919038,-1.87758327,0.556955695,42.4093399,8.65352535},
 {3137.72559,218.210144,0.527187467,0.849749029,-2.43318248,-0.390773833,41.8714218,9.2412529},
 };
 static const double points[3][12][2]={
 {{-44.3667183,-2.73863411},{3.43643284,-7.74325371},{24.3135548,-7.98792601},{31.9316635,-6.89070606},{39.2081947,-4.29391861},{42.7732735,0.0232927334},{39.2874413,4.42194176},{32.0012093,7.06341076},{23.9288235,8.55384064},{11.167552,9.59870148},{1.27504039,9.19470978},{-44.3667183,2.95121074}},
 {{-44.2869186,-7.18103313},{-9.24202156,-8.09656334},{3.95104051,-7.42917967},{34.3899384,-4.10701752},{38.6082649,-2.49652481},{40.531765,-0.06233171},{38.6690483,2.57676816},{33.8858871,4.58143282},{16.4649582,7.49637556},{-0.0597534217,9.21048737},{-17.3822479,9.02173901},{-44.2866325,7.31158113}},
 {{-44.3046722,7.36160374},{-44.3044128,-7.56387568},{-7.98102331,-9.63191414},{-2.87246585,-9.50199986},{17.8896427,-6.07912159},{35.5311737,-2.69312334},{38.3919563,-1.40663123},{39.4381752,0.105810069},{38.3620911,1.51685262},{35.8054276,2.57435393},{-0.0340973027,7.78546333},{-20.204155,8.85059071}},
 };
 const auto &a=meta[sample%3];h.data[0][0]=float(a[0]+a[2]*a[4]-a[3]*a[5]);h.data[0][1]=float(a[1]+a[3]*a[4]+a[2]*a[5]);
 h.data[0][2]=4;h.data[0][3]=12;h.data[1][0]=float(a[2]);h.data[1][1]=float(a[3]);h.data[1][2]=float(a[6]);h.data[1][3]=float(a[7]);
 h.data[2][0]=float(40*a[2]);h.data[2][1]=float(40*a[3]);h.data[2][3]=float(std::hypot(a[6],a[7]));
 for(unsigned i=0;i<12;++i){h.data[4+i/2][2*(i%2)]=float(points[sample%3][i][0]-a[4]);h.data[4+i/2][2*(i%2)+1]=float(points[sample%3][i][1]-a[5]);}
 return h;
}
}
