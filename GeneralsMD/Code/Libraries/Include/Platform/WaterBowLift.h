// Positive bow pressure freed from the mean-plane hull ramp. Render-only.
#pragma once
#include <cmath>
#include <algorithm>
#include <string>
#include "Platform/WaterFftGeometry.h"
namespace WaterBowLift {
struct Field { double value=0,dx=0,dy=0; };
// Contract: B>=0, 0<=D<=F<=1. F retains terrain/floor/obstacle clearance;
// D also contains mean-hull clearance. Gradients are with respect to mesh parameters.
inline Field Additional(Field bow,Field free,Field damping) {
    if(bow.value<=0 || free.value<=damping.value)return {};
    const double bound=WaterFftGeometry::MaxVerticalDisplacement;
    const double q=std::tanh(bow.value/bound),limited=bound*q,derivative=1-q*q;
    const double available=free.value-damping.value;
    return {available*limited,(free.dx-damping.dx)*limited+available*derivative*bow.dx,
        (free.dy-damping.dy)*limited+available*derivative*bow.dy};
}
inline double Foam(double addedHeight,double weight) {
    const double bounded=(std::max)(0.0,(std::min)(1.0,addedHeight*.32));
    return bounded*(std::max)(0.0,(std::min)(1.0,weight))*.65;
}
inline std::string Source() {return R"HLSL(
// Added height is positive and consumes only the unused F-D part of the old
// global vertical envelope. The complete height is D*L(A+B)+(F-D)*L(B).
// Both terms stay inside the existing F*V bound; ambient/stern and XY are unchanged.
float3 fftBowLift(float3 bow,float3 freeField,float3 dampingField) {
    if(bow.x<=0.0 || freeField.x<=dampingField.x)return 0;
    float q=tanh(bow.x/fftVerticalBound),limited=fftVerticalBound*q;
    float available=freeField.x-dampingField.x;
    return float3(available*limited,(freeField.yz-dampingField.yz)*limited+
        available*(1.0-q*q)*bow.yz);
}
float fftBowLiftFoam(float addedHeight,float weight) {
    // Reuse the existing .32 crest conversion and .65 coverage. The input is
    // actual extra limited height, already attenuated by F-D, never raw pressure.
    return saturate(addedHeight*0.32)*saturate(weight)*0.65;
}
)HLSL";}
}
