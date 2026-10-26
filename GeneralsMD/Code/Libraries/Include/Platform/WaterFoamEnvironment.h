/* Diffuse illumination of vessel foam by the existing procedural ocean sky. */
#pragma once
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace WaterFoamEnvironment {
constexpr unsigned SampleCount=65536;
constexpr unsigned BinCount=48;
struct Vector { double x,y,z; };
struct Rgb { double r,g,b; };
// Each row is integral(L_channel(w) * w / pi) over one angular bin.
struct Moment { double rgb[3][3]; };
typedef std::array<Moment,BinCount> Table;

inline double Dot(Vector a,Vector b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float Saturate(float x) {return x<0?0:(x>1?1:x);}
inline float Frac(float x) {return x-std::floor(x);}
inline float Smooth(float a,float b,float x) {x=Saturate((x-a)/(b-a));return x*x*(3-2*x);}
inline float Lerp(float a,float b,float t) {return a+(b-a)*t;}
inline float Hash(float x,float y) {
	float qx=Frac(x*0.1031f),qy=Frac(y*0.1031f),qz=qx;
	const float d=qx*(qy+33.33f)+qy*(qz+33.33f)+qz*(qx+33.33f);
	qx+=d;qy+=d;qz+=d;
	return Frac((qx+qy)*qz);
}
inline float Noise(float x,float y) {
	const float ix=std::floor(x),iy=std::floor(y);
	x-=ix;y-=iy;x=x*x*(3-2*x);y=y*y*(3-2*y);
	return Lerp(Lerp(Hash(ix,iy),Hash(ix+1,iy),x),Lerp(Hash(ix,iy+1),Hash(ix+1,iy+1),x),y);
}
inline Rgb CanonicalSky(Vector direction) {
	// Float arithmetic mirrors oceanSky's shader operations. Its sun-relative frame is
	// fixed: +X along the sun's azimuth, +Y toward the cloud bank. Exclude only the
	// explicit solar halo: the authored direct light already supplies that source.
	const float x=float(direction.x),y=float(direction.y),z=float(direction.z);
	const float horizon=std::pow(1-Saturate(z),4.0f),denominator=0.45f+std::fabs(z);
	const float px=x/denominator,py=y/denominator;
	float cloud=Noise(px*2.7f+7.8f,py*2.7f+2.3f)*0.7f+Noise(px*6.1f+0.4f,py*6.1f+13.7f)*0.3f;
	const float inverseCloudLength=1/std::sqrt(1+0.85f*0.85f);
	const float bank=std::pow(Saturate((y+z*0.85f)*inverseCloudLength),48.0f);
	cloud=Smooth(0.46f,0.73f,cloud)*(0.04f+1.5f*bank);
	const float horizonBlend=Smooth(-0.12f,0.18f,z);
	return {Lerp(0.001f,Lerp(0.016f,0.023f,horizon)+cloud*1.85f,horizonBlend),
		Lerp(0.005f,Lerp(0.055f,0.070f,horizon)+cloud*1.96f,horizonBlend),
		Lerp(0.010f,Lerp(0.110f,0.130f,horizon)+cloud*2.04f,horizonBlend)};
}

namespace Detail {
struct Sample {Vector direction;Rgb radiance;double azimuth;};
struct Group {std::vector<unsigned> indices;Moment moment={};double loss=0;};
inline void Summarize(Group &group,const std::vector<Sample> &samples) {
	group.moment=Moment{};
	double luminance=0;Vector moment={0,0,0};
	for(unsigned index:group.indices) {
		const Sample &sample=samples[index];
		const double colour[3]={sample.radiance.r,sample.radiance.g,sample.radiance.b};
		const double direction[3]={sample.direction.x,sample.direction.y,sample.direction.z};
		for(unsigned channel=0;channel<3;++channel)for(unsigned axis=0;axis<3;++axis)
			group.moment.rgb[channel][axis]+=colour[channel]*direction[axis];
		const double value=colour[0]*0.2126+colour[1]*0.7152+colour[2]*0.0722;
		luminance+=value;moment.x+=value*direction[0];moment.y+=value*direction[1];moment.z+=value*direction[2];
	}
	// Bright angular structure gets finer bins. This bound is used only for allocation;
	// every RGB channel retains its own direction moment for the final integral.
	group.loss=luminance-std::sqrt(Dot(moment,moment));
}
inline bool Split(const Group &group,const std::vector<Sample> &samples,bool azimuth,Group &a,Group &b) {
	double low=1e30,high=-1e30;
	for(unsigned index:group.indices) {
		const double value=azimuth?samples[index].azimuth:samples[index].direction.z;
		if(value<low)low=value;if(value>high)high=value;
	}
	const double middle=(low+high)*0.5;
	a.indices.reserve(group.indices.size()/2+1);b.indices.reserve(group.indices.size()/2+1);
	for(unsigned index:group.indices)
		((azimuth?samples[index].azimuth:samples[index].direction.z)<middle?a:b).indices.push_back(index);
	if(a.indices.empty() || b.indices.empty())return false;
	Summarize(a,samples);Summarize(b,samples);return true;
}
}

template<class Sampler> inline Table Build(Sampler sampleRadiance) {
	// Bounded startup work only. These temporary samples/groups are discarded after
	// generating the immutable shader constants. Sampler must return finite nonnegative
	// radiance; constant/HDR test environments use this same integration path.
	const double pi=3.14159265358979323846,golden=pi*(3-std::sqrt(5.0));
	std::vector<Detail::Sample> samples;samples.reserve(SampleCount);
	std::vector<Detail::Group> groups(8);groups.reserve(BinCount);
	for(unsigned i=0;i<SampleCount;++i) {
		const double z=1-2*(i+0.5)/SampleCount,r=std::sqrt(1-z*z),phase=i*golden;
		const Vector direction={r*std::cos(phase),r*std::sin(phase),z};
		double azimuth=std::atan2(direction.y,direction.x);if(azimuth<0)azimuth+=2*pi;
		samples.push_back({direction,sampleRadiance(direction),azimuth});
		unsigned quadrant=unsigned(azimuth*2/pi);if(quadrant>3)quadrant=3;
		groups[(z>=0?4:0)+quadrant].indices.push_back(i);
	}
	for(Detail::Group &group:groups)Detail::Summarize(group,samples);
	while(groups.size()<BinCount) {
		unsigned selected=0;bool found=false;
		for(unsigned i=0;i<groups.size();++i)if(groups[i].indices.size()>1 && (!found || groups[i].loss>groups[selected].loss)) {
			selected=i;found=true;
		}
		if(!found)break;
		Detail::Group phiA,phiB,zA,zB;
		const bool phi=Detail::Split(groups[selected],samples,true,phiA,phiB),
			z=Detail::Split(groups[selected],samples,false,zA,zB);
		if(!phi && !z)break;
		if(phi && (!z || phiA.loss+phiB.loss<zA.loss+zB.loss)) {
			groups[selected]=std::move(phiA);groups.push_back(std::move(phiB));
		} else {groups[selected]=std::move(zA);groups.push_back(std::move(zB));}
	}
	Table result={};
	for(unsigned i=0;i<groups.size();++i)for(unsigned channel=0;channel<3;++channel)for(unsigned axis=0;axis<3;++axis)
		result[i].rgb[channel][axis]=groups[i].moment.rgb[channel][axis]*(4.0/SampleCount);
	return result;
}
inline const Table &CachedTable() {
	static const Table table=Build([](Vector direction){return CanonicalSky(direction);});
	return table;
}
inline Rgb Evaluate(const Table &table,Vector normal) {
	// normal is unit length. For each channel, max(n . integral(L*w),0) is no
	// greater than integral(L*max(n.w,0)). Thus these bins remain positive and
	// conservatively lose energy at partially visible bins, including inverted normals.
	// Finite-bin underestimation is intentional; do not renormalize or tint it away.
	double value[3]={0,0,0};
	for(const Moment &moment:table)for(unsigned channel=0;channel<3;++channel) {
		const double *m=moment.rgb[channel],cosine=normal.x*m[0]+normal.y*m[1]+normal.z*m[2];
		if(cosine>0)value[channel]+=cosine;
	}
	return {value[0],value[1],value[2]};
}
inline Rgb Evaluate(Vector normal,Vector light,double daylight) {
	const double x=light.x+0.00001,y=light.y,inverseLength=1/std::sqrt(x*x+y*y);
	const Vector canonical={normal.x*x*inverseLength+normal.y*y*inverseLength,
		-normal.x*y*inverseLength+normal.y*x*inverseLength,normal.z};
	Rgb result=Evaluate(CachedTable(),canonical);
	const double scale=0.06+0.94*(daylight<0?0:(daylight>1?1:daylight));
	result.r*=scale;result.g*=scale;result.b*=scale;return result;
}
inline const std::string &HlslSource() {
	static const std::string source=[]() {
		std::string result="\n// Positive per-channel moments of the canonical sky, E/pi; solar halo excluded.\n"
			"static const float3 waterFoamSkyMoments[144]={\n";
		char line[160];const Table &table=CachedTable();
		for(unsigned i=0;i<BinCount;++i)for(unsigned channel=0;channel<3;++channel) {
			const double *m=table[i].rgb[channel];
			std::snprintf(line,sizeof(line),"float3(%.9g,%.9g,%.9g)%s\n",m[0],m[1],m[2],i+1==BinCount && channel==2?"":",");
			result+=line;
		}
		result+=R"HLSL(};
float3 waterFoamSkyIrradiance(float3 normal,float3 light,float daylight) {
    float2 axis=normalize(light.xy+float2(0.00001,0));
    float3 n=float3(dot(normal.xy,axis),dot(normal.xy,float2(-axis.y,axis.x)),normal.z);
    float3 value=0;
    [unroll] for(int i=0;i<48;++i) {
        value+=max(float3(dot(n,waterFoamSkyMoments[i*3]),
            dot(n,waterFoamSkyMoments[i*3+1]),dot(n,waterFoamSkyMoments[i*3+2])),float3(0,0,0));
    }
    return value*(0.06+0.94*saturate(daylight));
}
)HLSL";
		return result;
	}();
	return source;
}
}
