#pragma once
#include "Platform/WaterWaveCage.h"
#include "Platform/WaterWaveCageMesh.h"
#include <limits>

namespace WaterWaveCageTest {
namespace Cage=WaterWaveCage;
namespace Opt=WaterOpticalContext;
struct Fixture {
    int width,height,border=0;float spacing=2,scale=1,level=48;
    std::vector<uint8_t> heights;Opt::Field shore;Cage::Field field;
    Fixture(int w,int h):width(w),height(h),heights(size_t(w)*h,0){}
    bool Build(){
        const float x0=-border*spacing,y0=-border*spacing;
        const float x1=(width-border-1)*spacing,y1=(height-border-1)*spacing;
        const std::vector<Opt::Polygon> polygons={{{{x0,y0,level},{x1,y0,level},{x1,y1,level},{x0,y1,level}}}};
        const bool optical=Opt::Build(heights.data(),width,height,border,spacing,scale,level,polygons,shore);
        CHECK(optical);if(!optical)return false;
        const bool cage=Cage::Build(heights.data(),width,height,border,spacing,scale,level,shore,field);CHECK(cage);return cage;
    }
    Cage::TerrainSample Sample(float x,float y)const{return Cage::SampleTerrain(heights.data(),width,height,border,spacing,scale,x,y);}
};
inline float Length(Cage::Vec3 a){return std::sqrt(Cage::Dot(a,a));}
inline void CheckMap(){
    using namespace Cage;
    const Vec3 n=Unit({1,2,3});
    for(float amplitude:{.000001f,.1f,2.f,10.f,10000.f})for(Vec3 direction:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1},Unit({1,-2,3})}){
        const Vec3 eta{direction.x*amplitude,direction.y*amplitude,direction.z*amplitude};
        const Vec3 a=Map(eta,n,MaxRadius,.3f),b=Map({-eta.x,-eta.y,-eta.z},n,MaxRadius,.3f);
        CHECK(std::abs(a.x+b.x)<1e-6f&&std::abs(a.y+b.y)<1e-6f&&std::abs(a.z+b.z)<1e-6f);
        CHECK(Length(a)<=MaxRadius+1e-5f);
        CHECK(std::abs(Dot(n,a))<=.30001f);
    }
    const auto zero=Map({},n,MaxRadius,MaxRadius);CHECK(Length(zero)==0);
    const auto tiny=Map({1e-5f,0,0},{0,0,1},MaxRadius,MaxRadius);CHECK(std::abs(tiny.x-1e-5f)<1e-10f);
    const auto finite=Map({std::numeric_limits<float>::max(),0,0},{0,0,1},MaxRadius,MaxRadius);
    CHECK(std::isfinite(finite.x)&&finite.x<=MaxRadius);
    CHECK(Length(Map({1,2,3},n,0,0))==0);
    CHECK(Length(Map({std::numeric_limits<float>::infinity(),0,0},n,MaxRadius,MaxRadius))==0);
}
inline void CheckSlope(){
    Fixture slope(33,17);
    for(int y=0;y<slope.height;++y)for(int x=0;x<slope.width;++x)slope.heights[size_t(y)*slope.width+x]=uint8_t(64-x);
    if(!slope.Build())return;
    const auto terrain=slope.Sample(32,16);const auto expected=Cage::Unit({.5f,0,1});
    CHECK(terrain.height==48&&std::abs(terrain.normal.x-expected.x)<1e-6f&&std::abs(terrain.normal.z-expected.z)<1e-6f);
    const auto fractional=slope.Sample(33,15);CHECK(std::abs(fractional.height-47.5f)<1e-6f);
    const size_t line=8*slope.width+16;
    CHECK(slope.field.support[line]==1&&slope.field.normal[line]==0&&slope.field.tangent[line]>1);
    const auto slide=Cage::Map({0,0,3},terrain.normal,slope.field.tangent[line],0);
    CHECK(std::abs(slide.x)>.1f&&std::abs(slide.z)>.1f&&std::abs(Cage::Dot(slide,terrain.normal))<1e-6f);
    // This planar terrain has an independent analytic collision oracle. Check
    // the entire phase orbit on both sides, including the actual waterline.
    for(int x=14;x<=22;++x){
        const size_t i=8*slope.width+x;const auto sample=slope.Sample(x*slope.spacing,16);
        CHECK(slope.field.support[i]>0||x<16);
        if(slope.field.support[i]==0)continue;
        const auto raw=Cage::Fit(slope.heights.data(),slope.width,slope.height,0,2,1,48,x*2.f,16,sample);
        CHECK(slope.field.tangent[i]<=raw.tangent+1e-6f&&slope.field.normal[i]<=raw.normal+1e-6f);
        for(int phase=0;phase<128;++phase){
            const float angle=phase*6.28318530718f/128;
            const auto displacement=Cage::Map({4*std::sin(angle),std::cos(angle*3),4*std::cos(angle)},sample.normal,
                slope.field.tangent[i],slope.field.normal[i]);
            const float floor=64-(x*2+displacement.x)*.5f;
            const float clearance=48+displacement.z-floor;
            CHECK(x>=16?clearance>=-1e-5f:clearance<=1e-5f);
            CHECK(Length(displacement)<=Cage::MaxRadius+1e-5f);
        }
    }
    bool smoothed=false;
    for(int x=17;x<=24;++x){
        const size_t i=8*slope.width+x;if(slope.field.support[i]==0)continue;
        const auto raw=Cage::Fit(slope.heights.data(),slope.width,slope.height,0,2,1,48,x*2.f,16,slope.Sample(x*2.f,16));
        smoothed=smoothed||slope.field.normal[i]<raw.normal-.0001f||slope.field.tangent[i]<raw.tangent-.0001f;
    }
    CHECK(smoothed);
    for(size_t i=0;i<slope.heights.size();++i){
        const auto texel=Cage::Texel(slope.field,i);
        CHECK(((texel>>16)&255)==slope.heights[i]);
        CHECK(float((texel>>8)&255)*Cage::MaxRadius/255<=slope.field.tangent[i]+1e-6f);
        CHECK(float(texel&255)*Cage::MaxRadius/255<=slope.field.normal[i]+1e-6f);
        CHECK(std::abs(float(texel>>24)/255-slope.field.support[i])<=.5f/255+1e-6f);
        CHECK(slope.field.tangent[i]>=0&&slope.field.tangent[i]<=Cage::MaxRadius);
        CHECK(slope.field.normal[i]>=0&&slope.field.normal[i]<=Cage::MaxRadius);
    }
    // At sub-grid waterline samples, interpolated radii alone need not vanish.
    // The runtime plane-clearance cap restores the exact rn=0 constraint.
    const float capped=(std::min)(1.0f,std::abs(48-terrain.height)*terrain.normal.z);CHECK(capped==0);
}
inline void CheckClassification(){
    Fixture sea(17,17);sea.level=10;if(!sea.Build())return;
    CHECK(std::all_of(sea.field.support.begin(),sea.field.support.end(),[](float value){return value==0;}));
    CHECK(std::all_of(sea.field.tangent.begin(),sea.field.tangent.end(),[](float value){return value==Cage::MaxRadius;}));
    Fixture pool(17,17);pool.level=10;
    for(int y=4;y<=12;++y)for(int x=4;x<=12;++x)if(x==4||x==12||y==4||y==12)pool.heights[size_t(y)*17+x]=12;
    if(!pool.Build())return;
    CHECK(pool.shore.exposure[8*17+8]==0&&pool.field.support[8*17+8]==0);
    CHECK(pool.shore.exposure[8*17+3]==255&&pool.field.support[8*17+3]>0);
    CHECK(pool.field.support[8*17+4]==0); // guarded dry ridge between ocean and pool
    // Failed inputs cannot leave a partially usable packed texture behind.
    Cage::Field bad;auto broken=sea.shore;broken.exposure.pop_back();
    CHECK(!Cage::Build(sea.heights.data(),17,17,0,2,1,10,broken,bad)&&bad.texels.empty());
    CHECK(!Cage::Build(nullptr,17,17,0,2,1,10,sea.shore,bad));
    CHECK(!Cage::Build(sea.heights.data(),17,17,-1,2,1,10,sea.shore,bad));
    CHECK(!Cage::Build(sea.heights.data(),17,17,0,0,1,10,sea.shore,bad));
    CHECK(!Cage::Build(sea.heights.data(),17,17,0,2,1,std::numeric_limits<float>::quiet_NaN(),sea.shore,bad));
}
inline WaterProjectedMesh::Mesh Quad(){
    WaterProjectedMesh::Mesh mesh;
    for(auto p:{WaterProjectedMesh::Point{0,0},WaterProjectedMesh::Point{2,0},WaterProjectedMesh::Point{2,2},WaterProjectedMesh::Point{0,2}}){
        WaterProjectedMesh::Vertex vertex;vertex.world=p;mesh.vertices.push_back(vertex);
    }
    mesh.indices={0,1,2,0,2,3};return mesh;
}
inline void CheckMesh(){
    using WaterWaveCageMesh::ClipRestWaterline;using Point=WaterProjectedMesh::Point;
    const auto depth=[](Point p){return p.x-1;};const auto eligible=[](Point){return true;};
    auto mesh=Quad();CHECK(ClipRestWaterline(mesh,depth,eligible));
    CHECK(mesh.vertices.size()==7&&mesh.indices.size()==9);
    int shared=-1,sharedCount=0;
    for(size_t i=0;i<mesh.vertices.size();++i){
        const auto&p=mesh.vertices[i].world;
        if(std::abs(p.x-1)<1e-8&&std::abs(p.y-1)<1e-8){shared=int(i);++sharedCount;}
    }
    CHECK(sharedCount==1);unsigned diagonalUses=0;
    for(size_t t=0;t<mesh.indices.size();t+=3)for(int c=0;c<3;++c){
        const auto a=mesh.indices[t+c],b=mesh.indices[t+(c+1)%3];
        CHECK(depth(mesh.vertices[a].world)>=0);
        if((a==shared&&b==2)||(a==2&&b==shared))++diagonalUses;
    }
    CHECK(diagonalUses==2); // both owners use the same waterline intersection
    auto protectedMesh=Quad();protectedMesh.vertices[3].boundary=0;
    CHECK(ClipRestWaterline(protectedMesh,depth,eligible));
    CHECK(protectedMesh.vertices.size()==4&&protectedMesh.indices==Quad().indices);
    auto protectedCollar=Quad();protectedCollar.vertices[3].collar=0;
    CHECK(ClipRestWaterline(protectedCollar,depth,eligible)&&protectedCollar.vertices.size()==4);
    auto protectedTransition=Quad();protectedTransition.vertices[3].transition=0;
    CHECK(ClipRestWaterline(protectedTransition,depth,eligible)&&protectedTransition.vertices.size()==4);
    auto budget=Quad();CHECK(!ClipRestWaterline(budget,depth,eligible,5));
    CHECK(budget.vertices.size()==4&&budget.indices==Quad().indices); // rollback after a partial candidate split
    auto invalid=Quad();CHECK(!ClipRestWaterline(invalid,[](Point){return std::numeric_limits<double>::quiet_NaN();},eligible));
    CHECK(invalid.vertices.size()==4&&invalid.indices==Quad().indices);
    auto dry=Quad();CHECK(ClipRestWaterline(dry,[](Point){return -1.;},eligible)&&dry.indices.empty());
    auto curved=Quad();CHECK(ClipRestWaterline(curved,[](Point p){return p.x*p.x-.9;},eligible));
    for(size_t i=4;i<curved.vertices.size();++i)CHECK(std::abs(curved.vertices[i].world.x-std::sqrt(.9))<4e-5);
}
inline void Check(){CheckMap();CheckSlope();CheckClassification();CheckMesh();}
}
