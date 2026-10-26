// Render-only contact with the actual posed mesh. Does not represent a closed solid.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace WaterHullVisual {
constexpr unsigned MaxVertices=64, MaxTriangles=96, RegistersPerHull=88;
constexpr unsigned MaxSourceTriangles=8192;
// Keep every intermediate float product finite. Native vessel local coordinates
// are hundreds of units; this is a numerical bound, not a clipping distance.
constexpr float MaxCoordinate=1.0e12f;
struct Point2 { float x,y; };
struct Point3 { float x,y,z; };
struct Packet { float data[RegistersPerHull][4]; float metadata[4]; };
static_assert(sizeof(Packet)==(RegistersPerHull+1)*4*sizeof(float),"packed packet");

inline bool Finite(float x) { return std::isfinite(x) && std::fabs(x)<=MaxCoordinate; }
inline bool Finite(Point3 p) { return Finite(p.x)&&Finite(p.y)&&Finite(p.z); }
inline float PackIndices(unsigned a,unsigned b,unsigned c) { return float(a+64*b+4096*c); }
inline void UnpackIndices(unsigned p,unsigned& a,unsigned& b,unsigned& c) {
    a=p%64; b=(p/64)%64; c=p/4096;
}
inline bool Integer(float x,unsigned maximum) {
    return std::isfinite(x)&&x>=0&&x<=float(maximum)&&x==std::floor(x);
}
inline bool Valid(const Packet& p) {
    if(p.metadata[3]!=1 || !Integer(p.metadata[0],MaxVertices) ||
        !Integer(p.metadata[1],MaxTriangles) || !std::isfinite(p.metadata[2]) || p.metadata[2]<0) return false;
    const unsigned nv=unsigned(p.metadata[0]),nt=unsigned(p.metadata[1]);
    if((nv==0)!=(nt==0)) return false;
    for(unsigned i=0;i<nv;++i) if(!Finite({p.data[i][0],p.data[i][1],p.data[i][2]})) return false;
    for(unsigned i=0;i<nt;++i) {
        const float packed=p.data[MaxVertices+i/4][i%4];
        if(!Integer(packed,262143)) return false;
        unsigned a,b,c;UnpackIndices(unsigned(packed),a,b,c);
        if(a>=nv||b>=nv||c>=nv) return false;
    }
    return true;
}
inline bool RefreshRadius(Packet& p) {
    double radius2=0;
    for(unsigned i=0;i<unsigned(p.metadata[0]);++i) {
        const double x=p.data[i][0],y=p.data[i][1];
        radius2=(std::max)(radius2,x*x+y*y);
    }
    const double radius=std::sqrt(radius2);
    p.metadata[2]=float(radius+(std::max)(1.0e-4,radius*2.0e-6));
    if(p.metadata[0]==0) p.metadata[2]=0;
    return std::isfinite(p.metadata[2]);
}

// Complete source traversal is required even after false. Finish fails atomically:
// no truncated packet can be mistaken for a complete contour. Zero-area faces
// carry no surface and are skipped; vertices weld only by exact float equality.
class Builder {
    Packet packet_{};
    unsigned vertices_=0,triangles_=0,source_=0;
    float lo_,hi_;
    bool valid_;
    unsigned Vertex(Point3 p) {
        for(unsigned i=0;i<vertices_;++i)
            if(packet_.data[i][0]==p.x&&packet_.data[i][1]==p.y&&packet_.data[i][2]==p.z) return i;
        if(vertices_==MaxVertices) { valid_=false; return 0; }
        const unsigned i=vertices_++;
        packet_.data[i][0]=p.x==0?0:p.x; packet_.data[i][1]=p.y==0?0:p.y; packet_.data[i][2]=p.z==0?0:p.z;
        return i;
    }
public:
    Builder(float minZ,float maxZ):lo_(minZ),hi_(maxZ),valid_(Finite(minZ)&&Finite(maxZ)&&minZ<=maxZ) {}
    bool AddTriangle(const Point3& a,const Point3& b,const Point3& c) {
        if(!valid_) return false;
        if(++source_>MaxSourceTriangles||!Finite(a)||!Finite(b)||!Finite(c)) {valid_=false;return false;}
        // Reject only faces wholly outside the full reachable interval. Do not
        // clip positions: the fragment performs the actual-height intersection.
        if((a.z<lo_&&b.z<lo_&&c.z<lo_)||(a.z>hi_&&b.z>hi_&&c.z>hi_)) return true;
        const double ux=double(b.x)-a.x,uy=double(b.y)-a.y,uz=double(b.z)-a.z;
        const double vx=double(c.x)-a.x,vy=double(c.y)-a.y,vz=double(c.z)-a.z;
        if(uy*vz-uz*vy==0 && uz*vx-ux*vz==0 && ux*vy-uy*vx==0) return true;
        if(triangles_==MaxTriangles) {valid_=false;return false;}
        const unsigned ia=Vertex(a),ib=Vertex(b),ic=Vertex(c);
        if(!valid_) return false;
        packet_.data[MaxVertices+triangles_/4][triangles_%4]=PackIndices(ia,ib,ic);++triangles_;
        return true;
    }
    bool AddIndexedTriangle(const Point3* positions,std::size_t count,unsigned a,unsigned b,unsigned c) {
        if(!positions||a>=count||b>=count||c>=count) {valid_=false;return false;}
        return AddTriangle(positions[a],positions[b],positions[c]);
    }
    bool Finish(Packet& out) const {
        Packet result{};
        if(!valid_) {out=result;return false;}
        result=packet_; result.metadata[0]=float(vertices_);result.metadata[1]=float(triangles_);result.metadata[3]=1;
        if(!RefreshRadius(result)) {out=Packet{};return false;}
        out=result;return true;
    }
};

// Alias-safe translation from cached object origin to the existing contact frame.
// Recompute the bound after translation; never translate a cached packet in place
// unless that cached packet is deliberately being replaced by the caller.
inline bool Reframe(const Packet& origin,float centerX,float centerY,Packet& out) {
    Packet p{};
    if(!Valid(origin)||!Finite(centerX)||!Finite(centerY)) {out=p;return false;}
    p=origin;
    for(unsigned i=0;i<unsigned(p.metadata[0]);++i) {
        const double x=double(p.data[i][0])-centerX,y=double(p.data[i][1])-centerY;
        if(std::fabs(x)>MaxCoordinate||std::fabs(y)>MaxCoordinate) {out=Packet{};return false;}
        p.data[i][0]=float(x);p.data[i][1]=float(y);
    }
    if(!RefreshRadius(p)) {out=Packet{};return false;}
    out=p;return true;
}

struct Result {
    bool hasSegment=false,coplanar=false;
    float distance=1.0e6f;
    Point2 closest{},positive{},negative{};
    float minY=0,maxY=0;
    unsigned segmentCount=0;
    // Span midpoint gives a symmetric longitudinal role anchor. It need not lie
    // on a segment if disjoint surfaces share an extreme X; closest always does.
    float positiveMinY=0,positiveMaxY=0,negativeMinY=0,negativeMaxY=0;
};
inline Point3 Vertex(const Packet& p,unsigned i) {return {p.data[i][0],p.data[i][1],p.data[i][2]};}
inline bool Equal(Point2 a,Point2 b) {return a.x==b.x&&a.y==b.y;}
inline bool Less(Point2 a,Point2 b) {return a.x<b.x||(a.x==b.x&&a.y<b.y);}
inline void EndPoint(Result& r,Point2 p) {
    if(p.x<r.negative.x) {r.negative=p;r.negativeMinY=r.negativeMaxY=p.y;}
    else if(p.x==r.negative.x) {r.negativeMinY=(std::min)(r.negativeMinY,p.y);r.negativeMaxY=(std::max)(r.negativeMaxY,p.y);}
    if(p.x>r.positive.x) {r.positive=p;r.positiveMinY=r.positiveMaxY=p.y;}
    else if(p.x==r.positive.x) {r.positiveMinY=(std::min)(r.positiveMinY,p.y);r.positiveMaxY=(std::max)(r.positiveMaxY,p.y);}
    r.negative.y=(r.negativeMinY+r.negativeMaxY)*0.5f;
    r.positive.y=(r.positiveMinY+r.positiveMaxY)*0.5f;
    r.minY=(std::min)(r.minY,p.y);r.maxY=(std::max)(r.maxY,p.y);
}
inline void Segment(Result& r,Point2 q,Point2 a,Point2 b) {
    const float dx=b.x-a.x,dy=b.y-a.y,den=dx*dx+dy*dy;
    const float t=den>0?(std::max)(0.0f,(std::min)(1.0f,((q.x-a.x)*dx+(q.y-a.y)*dy)/den)):0;
    const Point2 closest={a.x+t*dx,a.y+t*dy};
    const float ex=q.x-closest.x,ey=q.y-closest.y,d=std::sqrt(ex*ex+ey*ey);
    if(!r.hasSegment) {r.closest=closest;r.distance=d;r.positive=r.negative=a;r.minY=r.maxY=a.y;
        r.positiveMinY=r.positiveMaxY=r.negativeMinY=r.negativeMaxY=a.y;}
    else if(d<r.distance||(d==r.distance&&Less(closest,r.closest))) {r.closest=closest;r.distance=d;}
    EndPoint(r,a);EndPoint(r,b);r.hasSegment=true;++r.segmentCount;
}
// Exact plane convention (no +/-epsilon slab). Fully coplanar faces are flagged
// and skipped: their tessellation diagonals are not physical contact edges.
// A lone tangent vertex is an actual point intersection and uses a zero segment.
// Result is unsigned and includes all selected rendered surfaces, not a claim of
// exterior topology. Query height must be inside the builder's complete interval.
inline Result Query(const Packet& p,Point3 q) {
    Result r;
    if(!Valid(p)||!Finite(q)) return r;
    for(unsigned i=0;i<unsigned(p.metadata[1]);++i) {
        unsigned ia,ib,ic;UnpackIndices(unsigned(p.data[MaxVertices+i/4][i%4]),ia,ib,ic);
        const Point3 v[3]={Vertex(p,ia),Vertex(p,ib),Vertex(p,ic)};
        if(v[0].z==q.z&&v[1].z==q.z&&v[2].z==q.z) {r.coplanar=true;continue;}
        Point2 hit[3];unsigned count=0;
        for(unsigned e=0;e<3;++e) {
            const Point3 a=v[e],b=v[(e+1)%3];const float az=a.z-q.z,bz=b.z-q.z;
            Point2 h{};bool have=false;
            if(az==0) {h={a.x,a.y};have=true;}
            else if((az<0&&bz>0)||(az>0&&bz<0)) {
                const float t=az/(az-bz);h={a.x+t*(b.x-a.x),a.y+t*(b.y-a.y)};have=true;
            }
            if(have) {bool duplicate=false;for(unsigned k=0;k<count;++k) duplicate=duplicate||Equal(hit[k],h);if(!duplicate) hit[count++]=h;}
        }
        if(count==1) Segment(r,{q.x,q.y},hit[0],hit[0]);
        else if(count==2) Segment(r,{q.x,q.y},hit[0],hit[1]);
        else if(count>2) r.coplanar=true; // defensive ambiguity; never emit an internal diagonal
    }
    return r;
}

// The backend declares the three bounded pixel-only arrays and VisualHullMeta.
// Test harnesses may declare them with the same names before appending Source().
inline const char* Source() { return R"hlsl(
float4 WaterVisualRegister(int index) {
    if(index<256) return VisualHullBlock0[index];
    if(index<512) return VisualHullBlock1[index-256];
    return VisualHullBlock2[index-512];
}
struct WaterVisualResult {
    float hasSegment, coplanar, distance;
    float2 closest, positive, negative;
    float minY, maxY;
    int segmentCount;
    float positiveMinY,positiveMaxY,negativeMinY,negativeMaxY;
};
WaterVisualResult WaterVisualEmpty() {
    WaterVisualResult r;
    r.hasSegment=0; r.coplanar=0; r.distance=1e6;
    r.closest=0; r.positive=0; r.negative=0;
    r.minY=0; r.maxY=0; r.segmentCount=0;
    r.positiveMinY=0;r.positiveMaxY=0;r.negativeMinY=0;r.negativeMaxY=0;
    return r;
}
bool WaterVisualLess(float2 a,float2 b) { return a.x<b.x || (a.x==b.x && a.y<b.y); }
void WaterVisualEndpoint(inout WaterVisualResult r,float2 p) {
    if(p.x<r.negative.x) {r.negative=p;r.negativeMinY=p.y;r.negativeMaxY=p.y;}
    else if(p.x==r.negative.x) {r.negativeMinY=min(r.negativeMinY,p.y);r.negativeMaxY=max(r.negativeMaxY,p.y);}
    if(p.x>r.positive.x) {r.positive=p;r.positiveMinY=p.y;r.positiveMaxY=p.y;}
    else if(p.x==r.positive.x) {r.positiveMinY=min(r.positiveMinY,p.y);r.positiveMaxY=max(r.positiveMaxY,p.y);}
    r.negative.y=(r.negativeMinY+r.negativeMaxY)*0.5;
    r.positive.y=(r.positiveMinY+r.positiveMaxY)*0.5;
    r.minY=min(r.minY,p.y); r.maxY=max(r.maxY,p.y);
}
void WaterVisualSegment(inout WaterVisualResult r,float2 q,float2 a,float2 b) {
    float2 d=b-a; float den=dot(d,d);
    float t=den>0?saturate(dot(q-a,d)/den):0;
    float2 closest=a+t*d; float distance=length(q-closest);
    if(r.hasSegment<0.5) {
        r.closest=closest; r.distance=distance; r.positive=a; r.negative=a; r.minY=a.y; r.maxY=a.y;
        r.positiveMinY=a.y;r.positiveMaxY=a.y;r.negativeMinY=a.y;r.negativeMaxY=a.y;
    } else if(distance<r.distance || (distance==r.distance && WaterVisualLess(closest,r.closest))) {
        r.closest=closest; r.distance=distance;
    }
    WaterVisualEndpoint(r,a); WaterVisualEndpoint(r,b); r.hasSegment=1; r.segmentCount++;
}
WaterVisualResult WaterVisualQuery(int hull,float3 q) {
    WaterVisualResult r=WaterVisualEmpty();
    if(hull<0 || hull>=8 || !all(isfinite(q)) || any(abs(q)>1e12)) return r;
    float4 meta=VisualHullMeta[hull];
    if(!all(isfinite(meta)) || meta.w!=1 || meta.x<0 || meta.x>64 || meta.y<0 || meta.y>96 ||
        meta.x!=floor(meta.x) || meta.y!=floor(meta.y) || meta.z<0 || ((meta.x==0)!=(meta.y==0))) return r;
    int nv=(int)meta.x, nt=(int)meta.y, base=hull*88;
    [loop] for(int i=0;i<nt;++i) {
        float packed=WaterVisualRegister(base+64+i/4)[i%4];
        if(!isfinite(packed) || packed<0 || packed>262143 || packed!=floor(packed)) return WaterVisualEmpty();
        int indices=(int)packed;
        int ia=indices%64, ib=(indices/64)%64, ic=indices/4096;
        if(ia>=nv || ib>=nv || ic>=nv) return WaterVisualEmpty();
        float3 v[3];
        v[0]=WaterVisualRegister(base+ia).xyz;
        v[1]=WaterVisualRegister(base+ib).xyz;
        v[2]=WaterVisualRegister(base+ic).xyz;
        if(!all(isfinite(v[0])) || !all(isfinite(v[1])) || !all(isfinite(v[2])) ||
            any(abs(v[0])>1e12) || any(abs(v[1])>1e12) || any(abs(v[2])>1e12)) return WaterVisualEmpty();
        if(v[0].z==q.z && v[1].z==q.z && v[2].z==q.z) { r.coplanar=1; continue; }
        float2 hit[3]; int count=0;
        [unroll] for(int e=0;e<3;++e) {
            float3 a=v[e],b=v[(e+1)%3]; float az=a.z-q.z,bz=b.z-q.z;
            float2 h=0; bool have=false;
            if(az==0) {h=a.xy;have=true;}
            else if((az<0 && bz>0) || (az>0 && bz<0)) {
                float t=az/(az-bz); h=a.xy+t*(b.xy-a.xy); have=true;
            }
            if(have) {
                bool duplicate=false;
                [unroll] for(int k=0;k<3;++k) if(k<count) duplicate=duplicate || all(hit[k]==h);
                if(!duplicate) {hit[count]=h;count++;}
            }
        }
        if(count==1) WaterVisualSegment(r,q.xy,hit[0],hit[0]);
        else if(count==2) WaterVisualSegment(r,q.xy,hit[0],hit[1]);
        else if(count>2) r.coplanar=1;
    }
    return r;
}
)hlsl"; }
}
