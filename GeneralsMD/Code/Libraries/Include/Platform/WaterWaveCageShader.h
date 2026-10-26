// Axisymmetric wave cages, adapted from Jeschke et al., CGF 2020, equations 2–3.
// The fitted radii live beside terrain height in a per-water-level texture.
#pragma once
#include <string>
namespace WaterWaveCageShader {
inline std::string Source(bool vertex) {
    return std::string(vertex ? R"HLSL(
Texture2D WaveCageTerrain : register(t3);
SamplerState WaveCageSampler : register(s3);
float4 waveCageTexel(float2 uv) {return WaveCageTerrain.SampleLevel(WaveCageSampler,uv,0);}
)HLSL" : R"HLSL(
float4 waveCageTexel(float2 uv) {return Texture2.SampleLevel(Sampler2,uv,0);}
)HLSL")+R"HLSL(
struct WaveCage {float3 normal;float2 radii;float support;};
float waveCageHeight(float2 p) {
    return waveCageTexel((p+TerrainParameters.xx+5.0)/max(TerrainParameters.y,1.0)).r*TerrainParameters.z;
}
WaveCage waveCage(float2 p,float level) {
    WaveCage c=(WaveCage)0;c.normal=float3(0,0,1);
    if(TerrainParameters.w<0.5 || OpticalParameters.w<0.5 || WaterParameters.y>0.5) return c;
    float4 packed=waveCageTexel((p+TerrainParameters.xx+5.0)/max(TerrainParameters.y,1.0));
    c.support=packed.a;
    if(c.support<=0.0) return c;
    float2 gradient=float2(waveCageHeight(p+float2(10,0))-waveCageHeight(p-float2(10,0)),
        waveCageHeight(p+float2(0,10))-waveCageHeight(p-float2(0,10)))/20.0;
    c.normal=normalize(float3(-gradient,1));
    float depth=level-packed.r*TerrainParameters.z;
    c.radii=packed.gb*fftVerticalBound;
    // Interpolation must not restore a normal radius on the actual waterline.
    c.radii.y=min(c.radii.y,abs(depth)*c.normal.z);
    return c;
}
float3 waveCageUnit(float3 eta) {
    float lengthEta=length(eta);
    return eta*(lengthEta>0.00001?tanh(lengthEta/fftVerticalBound)/lengthEta:1.0/fftVerticalBound);
}
float3 waveCageMap(WaveCage c,float3 eta) {
    float3 u=waveCageUnit(eta);
    return c.radii.x*u+(c.radii.y-c.radii.x)*c.normal*dot(c.normal,u);
}
// Complete chain rule, including spatially changing terrain axes and fitted radii.
float3 waveCageDerivative(WaveCage c,float3 eta,float3 etaQ,float3 normalQ,float2 radiiQ) {
    float L=length(eta),t=tanh(L/fftVerticalBound);
    float f=L>0.00001?t/L:1.0/fftVerticalBound;
    float3 u=eta*f;
    float coefficient=L>0.00001?((1.0-t*t)/fftVerticalBound-f)/(L*L):0.0;
    float3 uQ=f*etaQ+coefficient*eta*dot(eta,etaQ);
    return radiiQ.x*u+c.radii.x*uQ+(radiiQ.y-radiiQ.x)*c.normal*dot(c.normal,u)
        +(c.radii.y-c.radii.x)*(normalQ*dot(c.normal,u)+c.normal*(dot(normalQ,u)+dot(c.normal,uQ)));
}
// Finite directional fitting bounds the cage; this final local correction handles
// bilinear terrain curvature between fitted nodes. Restrict it to the shore mapping.
float3 waveCageFloor(float2 p,float level,float3 delta) {
    float floorHeight=waveCageHeight(p+delta.xy)-level+0.035;
    delta.z=min(fftVerticalBound,max(delta.z,floorHeight));
    return delta;
}
)HLSL"+(vertex?std::string():std::string(R"HLSL(
struct WaveCageSurface {float3 displacement,derivativeX,derivativeY;};
float2 waveCageGradient(float value,float2 dx,float2 dy) {
    float determinant=dx.x*dy.y-dx.y*dy.x;
    return abs(determinant)>0.000001?float2(ddx(value)*dy.y-ddy(value)*dx.y,
        ddy(value)*dx.x-ddx(value)*dy.x)/determinant:float2(0,0);
}
WaveCageSurface waveCageSurface(WaveCage c,float2 p,float level,float3 eta,float3 etaX,float3 etaY,
    float3 obstacle,float2 parameterDx,float2 parameterDy) {
    float2 nx=waveCageGradient(c.normal.x,parameterDx,parameterDy);
    float2 ny=waveCageGradient(c.normal.y,parameterDx,parameterDy);
    float2 nz=waveCageGradient(c.normal.z,parameterDx,parameterDy);
    float2 rt=waveCageGradient(c.radii.x,parameterDx,parameterDy);
    float2 rn=waveCageGradient(c.radii.y,parameterDx,parameterDy);
    WaveCageSurface result;
    float3 mapped=waveCageMap(c,eta);
    result.displacement=mapped*obstacle.x;
    result.derivativeX=waveCageDerivative(c,eta,etaX,float3(nx.x,ny.x,nz.x),float2(rt.x,rn.x))*obstacle.x+mapped*obstacle.y;
    result.derivativeY=waveCageDerivative(c,eta,etaY,float3(nx.y,ny.y,nz.y),float2(rt.y,rn.y))*obstacle.x+mapped*obstacle.z;
    float floorHeight=c.support>0.0 && obstacle.x>0.0?
        waveCageHeight(p+result.displacement.xy)-level+0.035:-fftVerticalBound;
    float2 floorSlope=waveCageGradient(floorHeight,parameterDx,parameterDy);
    if(floorHeight>result.displacement.z) {
        result.displacement.z=min(fftVerticalBound,floorHeight);
        result.derivativeX.z=floorHeight<fftVerticalBound?floorSlope.x:0;
        result.derivativeY.z=floorHeight<fftVerticalBound?floorSlope.y:0;
    }
    return result;
}
)HLSL"));
}
}
