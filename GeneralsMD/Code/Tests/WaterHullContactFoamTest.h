#pragma once
#include "Platform/WaterHullContactFoam.h"
#include <limits>

namespace WaterHullContactFoamTest {
// Independent double scalar integrand. The production primitive is never used here.
inline double Profile(double x) {
    x=std::abs(x);
    if(x<=.5)return 1;
    if(x>=1.5)return 0;
    const double u=(1.5-x);
    return u*u*(3-2*u);
}
inline double Band(double distance,double footprint) {
    const double half=(std::max)(.125,(std::min)(1.5,footprint*.5));
    // Simpson quadrature with exact interval cuts at every polynomial break. Integration
    // of each cubic piece is exact in real arithmetic and independent of its primitive.
    double cuts[6]={distance-half,distance+half,-1.5,-.5,.5,1.5};
    std::sort(cuts,cuts+6);double result=0;
    for(unsigned i=0;i<5;++i) {
        const double a=(std::max)(cuts[i],distance-half),b=(std::min)(cuts[i+1],distance+half);
        if(b>a)result+=(b-a)*(Profile(a)+4*Profile((a+b)*.5)+Profile(b))/6;
    }
    return result/(2*half);
}
inline double Response(double height,double slope) {
    if(!std::isfinite(height)||!std::isfinite(slope)||slope<0)return 0;
    // Spectral energy normalization is evaluated in double, independently from the
    // production float control flow. The reference scale is intentionally float-rounded.
    const double H=double(WaterHullContactFoam::HeightScale),S=double(WaterHullContactFoam::SlopeScale);
    const double phase=(std::max)(0.,(std::min)(1.,1+height/H));
    const double crest=(std::max)(height,0.),energy=crest*crest/(H*H)+slope*slope/(S*S);
    return (3*phase*phase-2*phase*phase*phase)*energy/(1+energy);
}
}

TEST(water_hull_contact_foam_calm_bounds_phase_and_filtered_width)
{
    namespace C=WaterHullContactFoam;namespace T=WaterHullContactFoamTest;
    double largestError=0;
    for(float h:{-12.f,-C::HeightScale,-1.f,-.01f,0.f,.01f,.4f,C::HeightScale,4.f,12.f})
    for(float slope:{0.f,.0001f,.01f,.04f,C::SlopeScale,.15f,.4f,2.f}) {
        const float response=C::Response(h,slope);
        CHECK(std::isfinite(response)&&response>=0&&response<=1);
        CHECK_NEAR(response,T::Response(h,slope),.0000003);
        for(float fp:{0.f,.1f,.25f,1.f,2.f,3.f,6.f,30.f})for(unsigned j=0;j<=100;++j) {
            const float d=j*.03125f;const float band=C::Band(d,fp),coverage=C::Coverage(d,fp,h,slope);
            largestError=(std::max)(largestError,std::abs(double(band)-T::Band(d,fp)));
            CHECK(std::isfinite(band)&&band>=0&&band<=1&&coverage>=0&&coverage<=response);
            CHECK_NEAR(band,T::Band(d,fp),.0000015);
            if(d>=3)CHECK(coverage==0);
        }
    }
    for(float fp:{0.f,.25f,1.f,2.f,3.f,100.f}) {
        double area=0;const double step=6./6000;
        for(unsigned j=0;j<6000;++j)area+=C::Band(float(std::abs(-3+(j+.5)*step)),fp)*step;
        CHECK_NEAR(area,2.,.000006); // armed against broadening a full-strength rim with AA
        CHECK(C::Coverage(0,fp,0,0)==0);
        CHECK(C::Coverage(3,fp,12,2)==0);
    }
    CHECK_NEAR(C::Response(C::HeightScale,0),.5,.0000002); // crest survives zero slope
    CHECK_NEAR(C::Response(0,C::SlopeScale),.5,.0000002);
    CHECK(C::Response(-C::HeightScale,.4f)==0); // trough is not a perpetual contact band
    CHECK(C::Coverage(1,1,C::HeightScale,0)>.24f);
    CHECK(C::Coverage(1,1,0,C::SlopeScale)>.24f);
    for(float beam:{0.f,4.f,8.f,16.f,32.f,54.f,1000.f}) {
        const double scale=1+3*(std::sqrt((std::max)(1.,(std::min)(4.,double(beam)/8.)))-1);
        for(float fp:{0.f,1.f,4.f,100.f})for(unsigned i=0;i<=192;++i) {
            const float d=i*.0625f;
            CHECK_NEAR(C::Coverage(d,fp,C::HeightScale,0,beam),
                .5*T::Band(d/scale,fp/scale),.0000015);
            if(d>=12)CHECK(C::Coverage(d,fp,12,.4f,beam)==0);
            if(beam<=8)CHECK(C::Coverage(d,fp,1,.1f,beam)==C::Coverage(d,fp,1,.1f));
        }
    }
    // Old slope-product gate drops the crest case entirely. This is a changed source,
    // not increased material brightness or widened noise; retain the nonvacuous control.
    const auto old=[](float h,float s){auto ramp=[](float a,float b,float x){float t=(std::max)(0.f,(std::min)(1.f,(x-a)/(b-a)));return t*t*(3-2*t);};return ramp(.04f,.4f,s)*ramp(-.8f,1.6f,h);};
    CHECK(old(C::HeightScale,0)==0&&C::Response(C::HeightScale,0)>.49f);
    CHECK(C::Coverage(1,1,0,C::SlopeScale)>old(0,C::SlopeScale)*.58f*8);
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        CHECK(C::Response(invalid,.1f)==0&&C::Response(1,invalid)==0);
        CHECK(C::Band(invalid,1)==0&&C::Band(1,invalid)==0);
    }
    CHECK(C::Band(-1,1)==0&&C::Band(1,-1)==0&&C::Response(1,-1)==0);
    const float huge=(std::numeric_limits<float>::max)();
    CHECK(C::Response(huge,huge)==1&&C::Band(huge,huge)==0);
    // A travelling broad/medium-size sinusoid must have calm/trough zero intervals and
    // crest response, not a permanent rim. Repeated frozen inputs are bit-identical.
    unsigned active=0,empty=0;float previous=0;double largestStep=0;
    for(unsigned i=0;i<=4096;++i) {
        const double phase=6.283185307179586*i/4096;
        const float h=C::HeightScale*float(std::sin(phase)),s=C::SlopeScale*float(std::abs(std::cos(phase)));
        const float a=C::Coverage(1,1,h,s),b=C::Coverage(1,1,h,s);
        CHECK(a==b);active+=a>.2f;empty+=a<.00001f;
        if(i)largestStep=(std::max)(largestStep,std::abs(double(a)-previous));previous=a;
    }
    CHECK(active>1000&&empty>50&&largestStep<.002);
    printf("water hull contact: independent filtered strip maximum error %.9g, phase step %.9g\n",largestError,largestStep);
}
