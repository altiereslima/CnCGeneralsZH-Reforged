#pragma once
#include <algorithm>
#include <cmath>
#include <string>

// Render-only response to the undisturbed broad/medium surface at an actual hull contact.
// This is a bounded crest/contact energy proxy, not a fluid pressure or incidence solver.
// Caller supplies the independently limited incident height and its matching slope;
// fine ripples, reflected waves and generated vessel waves must not enter this input.
namespace WaterHullContactFoam {
constexpr float HeightScale=1.6770509831248424f; // sqrt(1.50^2+.75^2), authored broad/medium RMS
constexpr float SlopeScale=.075f; // calibrated reference near their seeded .0718 slope RMS
constexpr float CoreRadius=.5f, OuterRadius=1.5f, MaxFilterRadius=1.5f;
constexpr float MaximumReach=OuterRadius+MaxFilterRadius;

inline float Response(float height,float slope) {
    if(!std::isfinite(height)||!std::isfinite(slope)||slope<0) return 0;
    const float phase=(std::max)(0.f,(std::min)(1.f,(height+HeightScale)/HeightScale));
    const float h=(std::max)(height,0.f)/HeightScale,s=slope/SlopeScale;
    const float energy=h*h+s*s;
    // The large-energy form stays finite even if a finite pathological input squares to
    // infinity; the small form avoids subtracting almost equal numbers near calm water.
    const float response=energy<=1.f ? energy/(1.f+energy) : 1.f-1.f/(1.f+energy);
    return phase*phase*(3.f-2.f*phase)*response;
}

// Odd primitive of the symmetric strip: 1 inside .5, cubic fade to 0 at 1.5.
// Its total integral is two world units. Filtering averages this profile rather than
// expanding an opaque rim, so changing footprint preserves the strip's integrated area.
inline float Primitive(float x) {
    const float a=std::abs(x);
    float integral;
    if(a<=CoreRadius) integral=a;
    else if(a>=OuterRadius) integral=1.f;
    else {const float u=a-CoreRadius;integral=CoreRadius+u-u*u*u+.5f*u*u*u*u;}
    return x<0 ? -integral : integral;
}
inline float Band(float distance,float footprint) {
    if(!std::isfinite(distance)||!std::isfinite(footprint)||distance<0||footprint<0) return 0;
    const float halfWidth=(std::max)(.125f,(std::min)(MaxFilterRadius,footprint*.5f));
    if(distance>=OuterRadius+halfWidth) return 0;
    return (std::max)(0.f,(std::min)(1.f,
        (Primitive(distance+halfWidth)-Primitive(distance-halfWidth))/(2.f*halfWidth)));
}
inline float Coverage(float distance,float footprint,float height,float slope) {
    return Band(distance,footprint)*Response(height,slope);
}
// Keep small-boat contact compact. Large waterlines spread wash over a wider strip,
// capped at four times the reference width (twelve units including the filter tail).
inline float WidthScale(float halfBeam) {
    return 1.f+3.f*(std::sqrt((std::max)(1.f,(std::min)(4.f,halfBeam*.125f)))-1.f);
}
inline float Coverage(float distance,float footprint,float height,float slope,float halfBeam) {
    if(!std::isfinite(halfBeam)||halfBeam<0) return 0;
    const float scale=WidthScale(halfBeam);
    return Coverage(distance/scale,footprint/scale,height,slope);
}
inline const char *Source() {
    return R"HLSL(
// Broad/medium incident crest/contact energy only. There is no calm, speed or noise floor.
float waterHullContactResponse(float height,float slope) {
    if(!isfinite(height)||!isfinite(slope)||slope<0.0) return 0.0;
    const float heightScale=1.6770509831248424, slopeScale=0.075;
    float phase=saturate((height+heightScale)/heightScale);
    float h=max(height,0.0)/heightScale,s=slope/slopeScale;
    float energy=h*h+s*s;
    float response=energy<=1.0 ? energy/(1.0+energy) : 1.0-1.0/(1.0+energy);
    return phase*phase*(3.0-2.0*phase)*response;
}
float waterHullContactPrimitive(float x) {
    float a=abs(x),integral;
    if(a<=0.5) integral=a;
    else if(a>=1.5) integral=1.0;
    else {float u=a-0.5;integral=0.5+u-u*u*u+0.5*u*u*u*u;}
    return x<0.0 ? -integral : integral;
}
float waterHullContactBand(float distance,float footprint) {
    if(!isfinite(distance)||!isfinite(footprint)||distance<0.0||footprint<0.0) return 0.0;
    float halfWidth=clamp(footprint*0.5,0.125,1.5);
    if(distance>=1.5+halfWidth) return 0.0;
    return saturate((waterHullContactPrimitive(distance+halfWidth)
        -waterHullContactPrimitive(distance-halfWidth))/(2.0*halfWidth));
}
float waterHullContactCoverage(float distance,float footprint,float height,float slope) {
    return waterHullContactBand(distance,footprint)*waterHullContactResponse(height,slope);
}
float waterHullContactCoverage(float distance,float footprint,float height,float slope,float halfBeam) {
    if(!isfinite(halfBeam)||halfBeam<0.0) return 0.0;
    float scale=1.0+3.0*(sqrt(clamp(halfBeam*0.125,1.0,4.0))-1.0);
    return waterHullContactCoverage(distance/scale,footprint/scale,height,slope);
}
)HLSL";
}
}
