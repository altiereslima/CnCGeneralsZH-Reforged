/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "Platform/WaterFftGeometry.h"
#include "Platform/WaterOpticalContext.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Jeschke et al., Making Procedural Water Waves Boundary-Aware (2020),
// https://pub.ist.ac.at/~chafner/JeschkeWaveCages.pdf, Eqs. (2)-(4), Appendix B.
// This bounded CPU adaptation uses equal tangential radii: M=rt I+(rn-rt)nn^T.
// Thus terrain normals alone define the frame; no interpolated tangent frame is
// needed. Cage fitting uses finitely sampled directions, not an exact collision
// solver. The paper's final terrain projection remains necessary on curved
// terrain, and vertex safety alone does not guarantee triangle-interior safety.
namespace WaterWaveCage {
constexpr float MaxRadius=WaterFftGeometry::MaxVerticalDisplacement<WaterFftGeometry::MaxHorizontalDisplacement?
    WaterFftGeometry::MaxVerticalDisplacement:WaterFftGeometry::MaxHorizontalDisplacement;
constexpr float TangentTolerance=.15f;
constexpr float CoastFullRange=16.0f;
constexpr float CoastRange=32.0f;
constexpr unsigned FitIterations=8;
constexpr unsigned SmoothingIterations=24;
constexpr float SmoothingStrength=16.0f;

struct Vec3 { float x=0,y=0,z=0; };
struct TerrainSample { float height=0;Vec3 normal{0,0,1}; };
struct Radii { float tangent=MaxRadius,normal=MaxRadius; };
struct Field {
    std::vector<float> tangent,normal,support;
    std::vector<uint32_t> texels;
};
inline uint32_t Texel(const Field&field,size_t n){return field.texels[n];}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Unit(Vec3 n){
    const double length=std::sqrt(double(n.x)*n.x+double(n.y)*n.y+double(n.z)*n.z);
    if(!std::isfinite(length)||length<=0)return {0,0,1};
    return {float(n.x/length),float(n.y/length),float(n.z/length)};
}
inline float ClampRadius(float r){return (std::max)(0.0f,(std::min)(MaxRadius,r));}
inline float SmoothRange(float a,float b,float value){
    const float t=(std::max)(0.0f,(std::min)(1.0f,(value-a)/(b-a)));
    return t*t*(3-2*t);
}

// The grid coordinates and clamped edge samples match the terrain texture. The
// normal uses world offsets +/- spacing, deliberately not a cell-local bilinear
// derivative; the renderer must use the same stencil when constructing M.
inline float Height(const uint8_t*heights,int width,int height,int border,
                    float spacing,float heightScale,float worldX,float worldY){
    const float gx=(std::max)(0.0f,(std::min)(float(width-1),worldX/spacing+border));
    const float gy=(std::max)(0.0f,(std::min)(float(height-1),worldY/spacing+border));
    const int x=int(gx),y=int(gy),x1=(std::min)(x+1,width-1),y1=(std::min)(y+1,height-1);
    const float tx=gx-x,ty=gy-y;
    const float a=heights[size_t(y)*width+x]*(1-tx)+heights[size_t(y)*width+x1]*tx;
    const float b=heights[size_t(y1)*width+x]*(1-tx)+heights[size_t(y1)*width+x1]*tx;
    return (a*(1-ty)+b*ty)*heightScale;
}
inline TerrainSample SampleTerrain(const uint8_t*heights,int width,int height,int border,
                                  float spacing,float heightScale,float worldX,float worldY){
    TerrainSample sample;
    if(!heights||width<2||height<2||!std::isfinite(spacing)||spacing<=0||
       !std::isfinite(heightScale)||heightScale<=0||!std::isfinite(worldX)||!std::isfinite(worldY))return sample;
    sample.height=Height(heights,width,height,border,spacing,heightScale,worldX,worldY);
    const float dx=(Height(heights,width,height,border,spacing,heightScale,worldX+spacing,worldY)-
                    Height(heights,width,height,border,spacing,heightScale,worldX-spacing,worldY))/(2*spacing);
    const float dy=(Height(heights,width,height,border,spacing,heightScale,worldX,worldY+spacing)-
                    Height(heights,width,height,border,spacing,heightScale,worldX,worldY-spacing))/(2*spacing);
    sample.normal=Unit({-dx,-dy,1});return sample;
}

// Eq. (2) with s=1/MaxRadius, followed by Eq. (3). This scale gives the
// unrestricted cage unit small-signal gain. Open water bypasses Map entirely,
// because tanh is not the identity for finite inputs. No wave phase is changed.
inline Vec3 Map(Vec3 eta,Vec3 normal,float tangentRadius,float normalRadius){
    const double length=std::sqrt(double(eta.x)*eta.x+double(eta.y)*eta.y+double(eta.z)*eta.z);
    if(!std::isfinite(length)||length==0||!std::isfinite(tangentRadius)||!std::isfinite(normalRadius))return {};
    normal=Unit(normal);const float rt=ClampRadius(tangentRadius),rn=ClampRadius(normalRadius);
    const double scale=std::tanh(length/MaxRadius)/length;
    const Vec3 bounded{float(eta.x*scale),float(eta.y*scale),float(eta.z*scale)};
    const float along=Dot(normal,bounded)*(rn-rt);
    return {rt*bounded.x+along*normal.x,rt*bounded.y+along*normal.y,rt*bounded.z+along*normal.z};
}

namespace Detail {
inline Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
// Four radial shells include the disk interior. The 16-point tangent rim and
// 18 sphere directions are a bounded fitting approximation, not paper constants.
struct Directions {
    std::array<Vec3,16> rim;
    std::array<Vec3,18> sphere;
    Directions(){
        constexpr float pi=3.14159265358979323846f;
        for(size_t i=0;i<rim.size();++i){const float a=2*pi*float(i)/rim.size();rim[i]={std::cos(a),std::sin(a),0};}
        sphere[0]={0,0,1};sphere[1]={0,0,-1};
        const float z=1/std::sqrt(3.0f),r=std::sqrt(1-z*z);
        for(int side=0;side<2;++side)for(int i=0;i<8;++i){
            const float a=2*pi*(i+.5f*side)/8;
            sphere[2+side*8+i]={r*std::cos(a),r*std::sin(a),side?z:-z};
        }
    }
};
inline const Directions& Samples(){static const Directions samples;return samples;}
struct Matrix {float xx=0,yy=0,zz=0,xy=0,xz=0,yz=0;};
inline Matrix Tensor(Vec3 n,float rt,float rn){
    const float d=rn-rt;return {rt+d*n.x*n.x,rt+d*n.y*n.y,rt+d*n.z*n.z,d*n.x*n.y,d*n.x*n.z,d*n.y*n.z};
}
inline float Difference(Matrix a,Matrix b){
    return (a.xx-b.xx)*(a.xx-b.xx)+(a.yy-b.yy)*(a.yy-b.yy)+(a.zz-b.zz)*(a.zz-b.zz)+
        2*((a.xy-b.xy)*(a.xy-b.xy)+(a.xz-b.xz)*(a.xz-b.xz)+(a.yz-b.yz)*(a.yz-b.yz));
}
inline void Add(Matrix&a,Matrix b){a.xx+=b.xx;a.yy+=b.yy;a.zz+=b.zz;a.xy+=b.xy;a.xz+=b.xz;a.yz+=b.yz;}
inline float NormalComponent(Matrix a,Vec3 n){
    return a.xx*n.x*n.x+a.yy*n.y*n.y+a.zz*n.z*n.z+2*(a.xy*n.x*n.y+a.xz*n.x*n.z+a.yz*n.y*n.z);
}
template<class Function>inline void Neighbours(size_t i,int width,int height,Function visit){
    const int x=int(i%width),y=int(i/width);
    if(x>0)visit(i-1);if(x+1<width)visit(i+1);if(y>0)visit(i-width);if(y+1<height)visit(i+width);
}
}

// Tangential radii grow first with the paper's small allowed penetration. The
// normal radius receives no additional allowance; if the tangent disk already
// intersects curved terrain its normal radius conservatively remains zero.
// Dry cages use the same tests with the terrain/air side reversed.
inline Radii Fit(const uint8_t*heights,int width,int height,int border,float spacing,
                 float heightScale,float level,float worldX,float worldY,TerrainSample terrain){
    const Vec3 n=terrain.normal;
    const Vec3 a=Unit(Detail::Cross(std::abs(n.z)<.9f?Vec3{0,0,1}:Vec3{0,1,0},n));
    const Vec3 b=Detail::Cross(n,a);
    const float depth=level-terrain.height,side=depth>=0?1.0f:-1.0f;
    const auto allowed=[&](Vec3 u,float rt,float rn,float tolerance){
        const Vec3 p{rt*(a.x*u.x+b.x*u.y)+rn*n.x*u.z,
                     rt*(a.y*u.x+b.y*u.y)+rn*n.y*u.z,
                     rt*(a.z*u.x+b.z*u.y)+rn*n.z*u.z};
        for(int shell=1;shell<=4;++shell){
            const float f=shell*.25f;
            const float floor=Height(heights,width,height,border,spacing,heightScale,worldX+p.x*f,worldY+p.y*f);
            if(side*(level+p.z*f-floor)<-tolerance-1e-5f)return false;
        }
        return true;
    };
    const auto tangentFits=[&](float rt){
        for(Vec3 u:Detail::Samples().rim)if(!allowed(u,rt,0,TangentTolerance))return false;
        return true;
    };
    float rt=MaxRadius;
    if(!tangentFits(rt)){
        float low=0,high=rt;for(unsigned i=0;i<FitIterations;++i){const float mid=(low+high)*.5f;if(tangentFits(mid))low=mid;else high=mid;}rt=low;
    }
    // Plane clearance is an additional conservative bound, also applied after
    // texture interpolation by the shader so rn is exactly zero at the waterline.
    float rn=(std::min)(MaxRadius,std::abs(depth)*n.z);
    const auto normalFits=[&](float radius){
        for(Vec3 u:Detail::Samples().rim)if(!allowed(u,rt,0,0))return false;
        for(Vec3 u:Detail::Samples().sphere)if(!allowed(u,rt,radius,0))return false;
        return true;
    };
    if(rn>0&&!normalFits(rn)){
        float low=0,high=rn;for(unsigned i=0;i<FitIterations;++i){const float mid=(low+high)*.5f;if(normalFits(mid))low=mid;else high=mid;}rn=low;
    }
    return {rt,rn};
}

inline bool Build(const uint8_t*heights,int width,int height,int border,float spacing,
                  float heightScale,float level,const WaterOpticalContext::Field&shore,Field&result){
    result=Field{};
    if(!heights||width<2||height<2||width>WaterOpticalContext::MaxDimension||height>WaterOpticalContext::MaxDimension||
       size_t(width)*height>WaterOpticalContext::MaxCells||border<0||border>(width-2)/2||border>(height-2)/2||
       !std::isfinite(spacing)||spacing<=0||!std::isfinite(heightScale)||heightScale<=0||
       !std::isfinite(level)||!std::isfinite(255*heightScale))return false;
    const size_t count=size_t(width)*height;
    if(shore.physicalShore.size()!=count||shore.exposure.size()!=count)return false;
    result.tangent.assign(count,MaxRadius);result.normal.assign(count,MaxRadius);result.support.assign(count,0);
    result.texels.resize(count);std::vector<Vec3> normals(count,Vec3{0,0,1});std::vector<size_t> active;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const size_t i=size_t(y)*width+x;
        const float distance=shore.physicalShore[i]*(WaterOpticalContext::PhysicalShoreRange/255);
        const float depth=level-heights[i]*heightScale;
        // These support thresholds are renderer choices, not paper parameters.
        // The exact exposed-water classification excludes pools and polygon-only
        // boundaries; its guarded dry extension supports shoreline interpolation.
        const float support=(shore.exposure[i]/255.0f)*(1-SmoothRange(CoastFullRange,CoastRange,distance))*
            (1-SmoothRange(MaxRadius,2*MaxRadius,std::abs(depth)));
        if(support<=0)continue;
        const float worldX=(x-border)*spacing,worldY=(y-border)*spacing;
        const auto terrain=SampleTerrain(heights,width,height,border,spacing,heightScale,worldX,worldY);
        const auto radii=Fit(heights,width,height,border,spacing,heightScale,level,worldX,worldY,terrain);
        normals[i]=terrain.normal;result.tangent[i]=radii.tangent;result.normal[i]=radii.normal;
        result.support[i]=support;active.push_back(i);
    }
    const auto rawTangent=result.tangent,rawNormal=result.normal;
    std::vector<float> alpha(count,1),nextTangent=result.tangent,nextNormal=result.normal;
    for(size_t i:active){
        const auto a=Detail::Tensor(normals[i],rawTangent[i],rawNormal[i]);float variation=0;unsigned neighbours=0;
        Detail::Neighbours(i,width,height,[&](size_t j){variation+=Detail::Difference(a,Detail::Tensor(normals[j],rawTangent[j],rawNormal[j]));++neighbours;});
        // Appendix B leaves alpha unspecified. This scale-invariant choice keeps
        // already smooth cages close to their fit and smooths abrupt cage changes.
        alpha[i]=1/(1+SmoothingStrength*variation/(neighbours*MaxRadius*MaxRadius));
    }
    for(unsigned iteration=0;iteration<SmoothingIterations;++iteration){
        for(size_t i:active){
            Detail::Matrix average;unsigned neighbours=0;
            Detail::Neighbours(i,width,height,[&](size_t j){Detail::Add(average,Detail::Tensor(normals[j],result.tangent[j],result.normal[j]));++neighbours;});
            // Exact isotropic-direction least squares restricted to r1=r2:
            // rn=n^T avg(M) n; rt=(trace(avg(M))-rn)/2. Component constraints
            // prevent smoothing from enlarging the original fitted ellipsoid.
            const float rn=Detail::NormalComponent(average,normals[i])/neighbours;
            const float rt=((average.xx+average.yy+average.zz)/neighbours-rn)*.5f;
            nextTangent[i]=(std::min)(rawTangent[i],(std::max)(0.0f,(1-alpha[i])*rt+alpha[i]*rawTangent[i]));
            nextNormal[i]=(std::min)(rawNormal[i],(std::max)(0.0f,(1-alpha[i])*rn+alpha[i]*rawNormal[i]));
        }
        result.tangent.swap(nextTangent);result.normal.swap(nextNormal);
    }
    const auto radiusByte=[](float value){return uint8_t((std::max)(0.0f,(std::min)(255.0f,std::floor(value/MaxRadius*255))));};
    for(size_t i=0;i<count;++i){
        // Radii round down: quantization cannot enlarge a fitted cage. In the
        // engine's A8R8G8B8 upload, shader RGBA = terrain, rt, rn, support.
        const uint32_t a=uint8_t(result.support[i]*255+.5f);
        result.texels[i]=(a<<24)|(uint32_t(heights[i])<<16)|(uint32_t(radiusByte(result.tangent[i]))<<8)|radiusByte(result.normal[i]);
    }
    return true;
}
}
