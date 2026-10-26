/* Surface shading of bounded, world-space foam history. No gameplay state is read here. */
#pragma once
namespace WaterFoamShading {
inline const char *HistorySamplingSource() {
    return R"HLSL(
float4 waterFoamHistoryTile(float2 uv,uint tile) {
    if(((uint)(FoamHistoryParameters.x+0.5)&4u)==0u)
        return tile==0u?Texture14.SampleLevel(Sampler14,uv,0):float4(0,0,0,0);
    uint width,height;Texture14.GetDimensions(width,height);
    float2 inset=0.5/float2(max(width,1u),max(height/2u,1u));
    // Clamp in the logical tile before packing. Sampler clamp alone would blend
    // shore/history bytes with direction bytes at the middle of the physical texture.
    float2 bounded=clamp(uv,inset,1.0-inset);
    return Texture14.SampleLevel(Sampler14,float2(bounded.x,(bounded.y+tile)*0.5),0);
}
)HLSL";
}
// Separate from history/noise so the material and actual reflection filtering can be tested
// with controlled incident radiance. Texture1/Sampler1 are the existing planar reflection.
inline const char *MaterialSource() {
    return R"HLSL(
float3 waterFoamPlanarTap(float3 sky,float2 uv,float levelCoverage) {
    // This material runs conditionally at foam pixels. Explicit mip zero avoids relying on
    // implicit derivatives inside that branch, including its divergent shoreline boundary.
    float3 rgb=Texture1.SampleLevel(Sampler1,saturate(uv),0.0).rgb;
    float2 border=min(uv,1.0-uv);
    float coverage=saturate(levelCoverage)*saturate(min(border.x,border.y)*30.0)
        *saturate(max(rgb.r,max(rgb.g,rgb.b))*64.0);
    // Decode each incident sample before filtering. Black clear/off-map taps contribute the
    // rough sky instead of making a dark border around the reflected cliff or bridge.
    return lerp(sky,pow(rgb,2.2),coverage);
}
float3 waterFoamPlanarReflection(float3 sky,float2 uv,float2 distortion,
    float2 reflectionScale,float roughness,float levelCoverage) {
    float2 radius=saturate(roughness)*0.008/max(reflectionScale,float2(0.001,0.001));
    float2 centre=uv+distortion;
    return waterFoamPlanarTap(sky,centre,levelCoverage)*0.4
        +(waterFoamPlanarTap(sky,centre+float2(radius.x,0),levelCoverage)
         +waterFoamPlanarTap(sky,centre-float2(radius.x,0),levelCoverage)
         +waterFoamPlanarTap(sky,centre+float2(0,radius.y),levelCoverage)
         +waterFoamPlanarTap(sky,centre-float2(0,radius.y),levelCoverage))*0.15;
}
float3 waterFoamRoughMaterial(float3 diffuse,float3 incidentReflection,
    float rawFresnel,float viewCosine,float roughness) {
    float mu=saturate(viewCosine),r=saturate(roughness);
    float k=r*r*0.5;
    float visibility=mu/max(mu*(1.0-k)+k,0.0001);
    // The incident reflection has no Fresnel factor yet. Apply the existing interface
    // Fresnel once, exchanging diffuse and reflected light rather than adding white energy.
    return lerp(diffuse,incidentReflection,saturate(rawFresnel)*visibility);
}
float3 waterFoamLayer(float3 clearSurface,float3 crestDiffuse,float3 vesselMaterial,
    float foam,float vesselFoam) {
    // This is the existing coverage composition, shared with the regression fixture. With
    // no vessel foam, both the clear surface and the generic crest material remain unchanged.
    float vesselShare=saturate(vesselFoam/max(foam,0.0001));
    float3 foamRadiance=lerp(crestDiffuse,vesselMaterial,vesselShare);
    float foamCoverage=saturate(max(foam*0.88,vesselFoam));
    return lerp(clearSurface,foamRadiance,foamCoverage);
}
// Attribute only the extra generic-material weight produced by the coastal source.
// Both totals already include identical hull masks and crest/vessel max operations.
float waterFoamCoastWeight(float foam,float withoutCoast,float vesselFoam) {
    if(withoutCoast>=foam) return 0.0;
    float generic=saturate(max(foam*0.88,vesselFoam))
        *(1.0-saturate(vesselFoam/max(foam,0.0001)));
    float other=saturate(max(withoutCoast*0.88,vesselFoam))
        *(1.0-saturate(vesselFoam/max(withoutCoast,0.0001)));
    return max(0.0,generic-other);
}
float3 waterFoamCoastLayer(float3 clearSurface,float3 crestDiffuse,float3 vesselMaterial,
    float3 coastMaterial,float foam,float vesselFoam,float coastWeight) {
    float3 original=waterFoamLayer(clearSurface,crestDiffuse,vesselMaterial,foam,vesselFoam);
    if(coastWeight<=0.0) return original;
    // Reassign an existing material weight: no change to total coverage, clear-water
    // transmission or vessel share, and no extra white emission.
    return original+coastWeight*(coastMaterial-crestDiffuse);
}
)HLSL";
}
inline const char *Source() {
    return R"HLSL(
// Terrain lighting is authored in the legacy display-colour space. Combine its actual
// ambient and visible Lambertian sun first, then decode once for linear surface composition.
float3 waterFoamIrradiance(float3 normal,float3 light,float3 ambientRGB,float3 sunRGB,float visibility) {
    float3 illumination=ambientRGB+sunRGB*max(dot(normal,light),0.0)*visibility;
    return pow(max(illumination,float3(0,0,0)),2.2);
}
// Additional authored global lights illuminate foam as they illuminate terrain. Only the main
// sun has a scene shadow visibility term; sum the legacy display-space light once, then decode.
float3 waterFoamIrradiance(float3 normal,float3 light,float3 ambientRGB,float3 sunRGB,float visibility,
    float3 fillDirection1,float3 fillColour1,float3 fillDirection2,float3 fillColour2) {
    float3 illumination=ambientRGB+sunRGB*max(dot(normal,light),0.0)*visibility;
    illumination+=fillColour1*max(dot(normal,fillDirection1),0.0);
    illumination+=fillColour2*max(dot(normal,fillDirection2),0.0);
    return pow(max(illumination,float3(0,0,0)),2.2);
}
float3 waterFoamReplaceAmbient(float3 authoredIrradiance,float3 ambientRGB,float3 skyIrradiance,float offshore) {
    // Retain the authored direct/fill/shadow increment exactly. Its ambient is replaced
    // offshore, never added again; zero offshore is the exact legacy material endpoint.
    if(offshore<=0.0) return authoredIrradiance;
    float3 ambient=pow(max(ambientRGB,float3(0,0,0)),2.2);
    return authoredIrradiance+(skyIrradiance-ambient)*saturate(offshore);
}
// Cubic bilinear value noise is concentrated around .5, not uniformly distributed. Its CDF
// turns a threshold into area coverage rather than silently increasing the amount of foam.
// Calibrated from 300000 independent uniform corner/coordinate samples, symmetrized about .5.
float waterFoamNoiseRank(float value) {
    static const float distribution[33]={
        0.0,0.0021533,0.0086700,0.0195933,0.0352533,0.0551733,0.0794133,0.1083450,
        0.1404633,0.1768117,0.2163183,0.2588083,0.3039783,0.3511833,0.3998867,0.4491767,
        0.5,0.5508233,0.6001133,0.6488167,0.6960217,0.7411917,0.7836817,0.8231883,
        0.8595367,0.8916550,0.9205867,0.9448267,0.9647467,0.9804067,0.9913300,0.9978467,1.0};
    float scaled=saturate(value)*32.0;
    int index=min((int)scaled,31);
    return lerp(distribution[index],distribution[index+1],scaled-index);
}
// Integrate a bounded linear filter over uniform ranks. Near the two ends, adjusting the
// cutoff by the inverse triangular area preserves coverage instead of adding an opacity floor.
float waterFoamRankCoverage(float rank,float coverage,float width) {
    coverage=saturate(coverage);
    width=clamp(width,0.005,0.5);
    float cutoff=coverage;
    if(coverage<width) cutoff=-width+sqrt(4.0*width*coverage);
    else if(coverage>1.0-width) cutoff=1.0+width-sqrt(4.0*width*(1.0-coverage));
    return saturate((cutoff-rank+width)/(2.0*width));
}
// Filter each physical scale independently. A subpixel pore converges to its mean coverage;
// it must not remain a bright binary speck while larger foam remnants are still resolved.
float waterFoamCoverageBand(float2 q,float coverage,float footprint,float cellSize,float minimumWidth) {
    coverage=saturate(coverage);
    if(coverage<=0.0 || coverage>=1.0) return coverage;
    float ratio=max(footprint,0.0)/cellSize;
    float unresolved=smoothstep(0.5,1.5,ratio);
    if(unresolved>=1.0) return coverage;
    float rank=waterFoamNoiseRank(oceanNoise(q/cellSize));
    float width=clamp(ratio*0.5,minimumWidth,0.5);
    return lerp(waterFoamRankCoverage(rank,coverage,width),coverage,unresolved);
}
float waterFoamOpticalDepth(float coverage) {
    // Avoid cancellation in log(1-c) at nearly empty, softly fading trail edges.
    // Below .01, omitted terms are bounded by c^4/(4*(1-c)); relative error is
    // less than 2.53e-7. This also avoids the GPU log subtraction error near c=.001.
    return coverage<0.01 ? coverage*(1.0+coverage*(0.5+coverage/3.0)) : -log(1.0-coverage);
}
float waterFoamOpticalCoverage(float depth) {
    return depth<0.01 ? depth*(1.0+depth*(-0.5+depth*(1.0/6.0-depth/24.0))) : 1.0-exp(-depth);
}
float waterFoamFreshMean(float depth) {
    // Average *coverage*, not density, for a uniform multiplier in [.15,1.85]. The
    // small-depth expansion uses its independent first four moments; E[m]=1.
    if(depth<0.05) return depth*(1.0+depth*(-0.6204166667+depth*(0.2870833333-depth*0.1062250521)));
    return 1.0-(exp(-0.15*depth)-exp(-1.85*depth))/(1.7*depth);
}
float2 waterFoamChurnPosition(float2 p,float time) {
    float2 q=float2(dot(p,float2(0.8,0.6)),dot(p,float2(-0.6,0.8)))+float2(17.31,-8.67);
    // Each shear has determinant one, so composing them stretches connected detail
    // without accumulating/compressing its mean density. The displacement is bounded;
    // this is local procedural churn, not transport by a simulated velocity field.
    q.x+=0.70*sin(q.y*0.70+time*0.65);
    q.y+=0.65*sin(q.x*0.63-time*0.47);
    return q;
}
// A fresh hull collision deposits clusters, not a uniformly translucent waterline.
// Redistribute its existing coverage at a fixed four-unit scale. The rank filter
// preserves expected coverage and converges to that mean when the clusters are
// unresolved. Zero source stays zero; this cannot create foam outside contact.
float waterFoamContactPatches(float2 p,float coverage,float footprint,float time) {
    coverage=saturate(coverage);
    if(coverage<=0.0 || coverage>=1.0) return coverage;
    float2 q=waterFoamChurnPosition(p/4.0,time*0.35)*4.0;
    // Split area and within-patch coverage: large islands contain smaller pores
    // instead of becoming smooth opaque blobs. Filter both scales independently.
    float area=sqrt(coverage),withinPatch=coverage/area;
    float resolvedFootprint=max(footprint,0.0)*1.7;
    float2 fine=float2(dot(q,float2(0.28,0.96)),dot(q,float2(-0.96,0.28)))+float2(-31.7,23.2);
    float patches=waterFoamCoverageBand(q,area,resolvedFootprint,4.0,0.18)
        *waterFoamCoverageBand(fine,withinPatch,resolvedFootprint,1.75,0.18);
    // Keep a dilute film between dense clusters. Pure binary islands read as beads
    // and can saturate the downstream pore response. Both terms have the same mean,
    // so this controls texture contrast without adding foam or extending its support.
    return lerp(coverage,patches,0.65);
}
float waterFoamOldCoverage(float2 p,float coverage,float footprint,float aged) {
    // Retain the original runtime lerp at the old-age endpoint. Replacing aged with a
    // literal one allows different cancellation/folding and changes existing pixel bits.
    float patchArea=lerp(1.0,sqrt(coverage),aged);
    float withinPatch=coverage/max(patchArea,0.00001);
    float2 coarse=float2(dot(p,float2(0.8,0.6)),dot(p,float2(-0.6,0.8)))+float2(17.31,-8.67);
    float2 fine=float2(dot(p,float2(0.28,0.96)),dot(p,float2(-0.96,0.28)))+float2(-31.7,23.2);
    return waterFoamCoverageBand(coarse,patchArea,footprint,1.0,0.08)
        *waterFoamCoverageBand(fine,withinPatch,footprint,0.35,0.01);
}
// The world-space history remains fixed around the recorded path. Fresh foam has smoothly
// evolving optical density rather than opaque ranked islands. Aging hands it to the existing
// smaller remnants; neither source emission nor the older material is changed here.
float waterFoamMicroCoverage(float2 p,float coverage,float footprint,float ageSeconds,float time) {
    coverage=saturate(coverage);
    if(coverage<=0.0 || coverage>=1.0) return coverage;
    float aged=smoothstep(2.0,8.0,max(ageSeconds,0.0));
    if(aged>=1.0) return waterFoamOldCoverage(p,coverage,footprint,aged); // exact old endpoint
    float depth=waterFoamOpticalDepth(coverage),mean=waterFoamFreshMean(depth);
    // 1.7 bounds the composed shear's stretch (Frobenius norm), including its most
    // compressed direction. Unresolved detail approaches its analytic coverage average.
    float unresolved=smoothstep(0.5,1.5,max(footprint,0.0)*1.7/4.0);
    float fresh=mean;
    if(unresolved<1.0) {
        float rank=waterFoamNoiseRank(oceanNoise(waterFoamChurnPosition(p,time)/4.0));
        fresh=lerp(waterFoamOpticalCoverage(depth*lerp(0.15,1.85,rank)),mean,unresolved);
    }
    if(aged<=0.0) return fresh;
    return lerp(fresh,waterFoamOldCoverage(p,coverage,footprint,1.0),aged);
}
float waterFoamMicroCoverage(float2 p,float coverage,float footprint,float ageSeconds) {
    return waterFoamMicroCoverage(p,coverage,footprint,ageSeconds,0.0);
}
float waterFoamMicroCoverage(float2 p,float coverage,float footprint) {
    return waterFoamMicroCoverage(p,coverage,footprint,0.0);
}
// Redistribute retained history coverage into resolved remnants, rather than multiplying
// it by another opacity mask. The 1.5-second fresh-churn lifetime sets the onset; by four
// such lifetimes (six seconds), the old trail is fully fragmented. Fresh contact and newly
// emitted stern foam keep their exact previous endpoint.
float waterFoamHistoryRemnants(float2 p,float coverage,float footprint,float ageSeconds,float time) {
    if(coverage<=0.0 || coverage>=1.0 || ageSeconds<=1.5) return coverage;
    // Use fixed physical scales. Crossfading them avoids the rapid world-phase swimming
    // caused by dividing world coordinates by a time-varying remnant size. Six world units
    // is the existing small-vessel reference width; twelve keeps older carrier remnants
    // visible at RTS distances without adding a vessel-size/heading channel to the atlas.
    float resolvedFootprint=max(footprint,0.0)*1.7;
    if(resolvedFootprint>=18.0) return coverage;
    float2 q=waterFoamChurnPosition(p/6.0,time*0.35)*6.0;
    // Split the covered area and within-patch coverage. Sparse old history must not
    // turn into opaque white islands: each remnant still transmits underlying water.
    // Their product keeps the same expected coverage while opening larger water gaps.
    float area=sqrt(coverage),withinPatch=coverage/area;
    float small=waterFoamCoverageBand(q,area,resolvedFootprint,6.0,0.08);
    float large=waterFoamCoverageBand(q+float2(43.7,-71.3),area,resolvedFootprint,12.0,0.08);
    float patches=lerp(small,large,smoothstep(3.0,9.0,ageSeconds))*withinPatch;
    // Each independently filtered rank band targets the original local coverage. This
    // conserves its uniform-rank expectation, not every finite patch's exact pixel sum.
    return lerp(coverage,patches,smoothstep(1.5,6.0,ageSeconds));
}
// These moments come from the recorded path. At crossing paths the axial tensor loses
// coherence, so no arbitrary heading is imposed. Filtering is opacity-weighted, as for age.
float4 waterFoamRecordedDirection(float4 packed) {
    if(packed.a<=0.0001) return 0;
    float2 moment=clamp(2.0*packed.rg/packed.a-1.0,-1.0,1.0);
    float coherence=length(moment);
    float confidence=smoothstep(0.20,0.65,coherence)*smoothstep(0.02,0.08,packed.a);
    if(confidence<=0.0) return 0;
    float angle=0.5*atan2(moment.y,moment.x);
    float width=clamp(packed.b/packed.a*32.0,0.0,32.0);
    return float4(cos(angle),sin(angle),clamp(width,3.0,12.0),confidence);
}
float waterFoamStreakTap(float2 p,float area,float footprint,float ageSeconds,float time) {
    float2 q=waterFoamChurnPosition(p/4.0,time*0.35)*4.0;
    return lerp(waterFoamCoverageBand(q,area,footprint,4.0,0.08),
        waterFoamCoverageBand(q+float2(43.7,-71.3),area,footprint,8.0,0.08),
        smoothstep(3.0,9.0,ageSeconds));
}
float waterFoamHistoryStreaks(float2 p,float coverage,float footprint,float ageSeconds,
    float time,float4 direction,float isotropic) {
    if(coverage<=0.0 || coverage>=1.0 || ageSeconds<=1.5 || direction.w<=0.0) return isotropic;
    float resolvedFootprint=max(footprint,0.0)*1.7;
    float area=sqrt(coverage),withinPatch=coverage/area;
    // Convolve calibrated *coverage* at fixed world positions. Rotating world coordinates
    // by a varying heading would rephase the noise during turns. Symmetric offsets also
    // make the arbitrary sign of an axial eigenvector immaterial. No velocity is invented.
    float2 offset=direction.xy*direction.z;
    float patches=waterFoamStreakTap(p,area,resolvedFootprint,ageSeconds,time)*0.30;
    patches+=(waterFoamStreakTap(p-offset*0.5,area,resolvedFootprint,ageSeconds,time)
        +waterFoamStreakTap(p+offset*0.5,area,resolvedFootprint,ageSeconds,time))*0.225;
    patches+=(waterFoamStreakTap(p-offset,area,resolvedFootprint,ageSeconds,time)
        +waterFoamStreakTap(p+offset,area,resolvedFootprint,ageSeconds,time))*0.125;
    float streaks=lerp(coverage,patches*withinPatch,smoothstep(1.5,6.0,ageSeconds));
    return lerp(isotropic,streaks,direction.w);
}
// Returns coverage, normalized age, and fresh churn. The atlas stores coverage-weighted
// moments, so bilinear filtering cannot invent an old edge around newly deposited foam.
float3 waterFoamHistory(float2 p,float level,float time,float footprint) {
    uint channels=(uint)(FoamHistoryParameters.x+0.5);
    if((channels&1u)==0u || abs(level-FoamHistoryParameters.y)>0.1) return 0;
    float2 uv=(p-FoamHistoryArea.xy)*FoamHistoryArea.zw;
    if(any(uv<0.0) || any(uv>1.0)) return 0;
    float3 sample=waterFoamHistoryTile(uv,0u).rgb;
    if(sample.x<0.0001) return 0;
    float elapsed=max(0.0,time-FoamHistoryParameters.w);
    float age=saturate(sample.y/sample.x+elapsed/24.0);
    float churn=saturate(sample.z/sample.x)*exp(-elapsed/1.5);
    // Coarse coverage already spreads and decays in world space. Fine breakup evolves at
    // that same fixed position: old trails do not rotate with a vessel or retain a solid stamp.
    float2 drift=float2(0.15,-0.08)*time;
    float broad=oceanNoise((p+drift)*0.065);
    float fine=oceanNoise((p-drift*0.7)*0.26+float2(broad*0.7,0));
    float resolved=1.0/(1.0+footprint*0.20);
    float structure=lerp(broad,broad*0.62+fine*0.38,resolved);
    float threshold=lerp(0.20,0.67,age);
    float broken=smoothstep(threshold-0.10,threshold+0.14,structure);
    float coverage=sample.x*exp(-elapsed/9.0)*lerp(0.38+0.62*broken,broken,smoothstep(0.05,0.65,age));
    float envelope=coverage;
    coverage=waterFoamHistoryRemnants(p,coverage,footprint,age*24.0,time);
    if((channels&4u)!=0u && age*24.0>1.5 && envelope>0.0 && footprint*1.7<18.0) {
        float4 direction=waterFoamRecordedDirection(waterFoamHistoryTile(uv,1u));
        coverage=waterFoamHistoryStreaks(p,envelope,footprint,age*24.0,time,direction,coverage);
    }
    // Fade the atlas perimeter; clipping a world window must not create a rectangular foam edge.
    float2 border=min(uv,1.0-uv)/max(FoamHistoryArea.zw,float2(0.000001,0.000001));
    coverage*=smoothstep(0.0,max(8.0,FoamHistoryParameters.z*3.0),min(border.x,border.y));
    return float3(saturate(coverage),age,churn*sample.x);
}
)HLSL";
}
}
