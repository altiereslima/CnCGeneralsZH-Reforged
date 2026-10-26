/* Bounded render-only refinement of projected water near a submitted vessel's bow. */
#pragma once
#include "Platform/WaterProjectedMesh.h"
#include "Platform/WaterHullModel.h"
#include "Platform/WaterFftGeometry.h"
#include "Platform/WaterVesselWaves.h"
#include <array>
#include <map>
#include <limits>

namespace WaterBowRefinement {
using Point=WaterProjectedMesh::Point;
struct Logical { int x=0,y=0; bool operator<(const Logical &b) const { return x<b.x || (x==b.x && y<b.y); } };
struct Node { Logical logical;Point screen{},world{};int source=-1,parent=-1; };
struct Triangle { unsigned a=0,b=0,c=0;double gate=1; };
struct Topology { std::vector<Node> nodes;std::vector<Triangle> triangles; };
constexpr unsigned MaxRegions=3,RegionVertices=2730,RegionTriangles=5460;
constexpr unsigned MaxAddedVertices=MaxRegions*RegionVertices,MaxAddedTriangles=MaxRegions*RegionTriangles;
constexpr unsigned MaxTopologyNodes=10000,MaxTopologyTriangles=19000;
constexpr double CoreRadius=4,WindowRadius=6,AdmissionSeconds=.25;
inline double Saturate(double x) { return (std::max)(0.0,(std::min)(1.0,x)); }
inline double Smooth(double a,double b,double x) { const double t=Saturate((x-a)/(b-a));return t*t*(3-2*t); }
inline bool Finite(Point p) { return std::isfinite(p.x)&&std::isfinite(p.y); }

// Red subdivision with shared edge samples also represents green stitches: a zero edge
// collapses to its canonical logical endpoint. Children target their CURRENT parents, so
// topology birth/death converges even after a nonlinear GPU displacement/sample operation.
// Output is atomic on rejection; independent callers may stress every edge separately.
template<class Weight> bool Subdivide(const Topology &input,const WaterProjectedMesh::Projection &projection,
    Weight weight,Topology &output,unsigned levels=2,bool lockBoundary=true) {
    if(levels>2 || input.nodes.size()>MaxTopologyNodes || input.triangles.size()>MaxTopologyTriangles) return false;
    for(const Node &n:input.nodes) if(!Finite(n.world)||!Finite(n.screen)) return false;
    for(const Triangle &t:input.triangles) if(t.a>=input.nodes.size()||t.b>=input.nodes.size()||t.c>=input.nodes.size()||
        !std::isfinite(t.gate)||t.gate<0||t.gate>1) return false;
    Topology current=input;
    std::vector<Point> targets;targets.reserve(MaxTopologyNodes);for(const Node &n:input.nodes) targets.push_back(n.screen);
    for(unsigned level=1;level<=levels;++level) {
        if(current.triangles.size()>MaxTopologyTriangles/4) return false;
        struct Edge { unsigned lo=0,hi=0,count=0,node=0;double gate=1,amount=0; };
        using Key=std::array<int,4>;std::map<Key,Edge> edges;
        auto edgeKey=[&](unsigned a,unsigned b) {
            if(current.nodes[b].logical<current.nodes[a].logical) std::swap(a,b);
            const Logical x=current.nodes[a].logical,y=current.nodes[b].logical;
            return Key{{x.x,x.y,y.x,y.y}};
        };
        for(const Triangle &t:current.triangles) {
            const unsigned p[3]={t.a,t.b,t.c};
            for(unsigned e=0;e<3;++e) {
                unsigned a=p[e],b=p[(e+1)%3];if(current.nodes[b].logical<current.nodes[a].logical) std::swap(a,b);
                Edge &edge=edges[edgeKey(a,b)];edge.lo=a;edge.hi=b;++edge.count;edge.gate=(std::min)(edge.gate,t.gate);
            }
        }
        if(current.nodes.size()+edges.size()>MaxTopologyNodes) return false;
        current.nodes.reserve(current.nodes.size()+edges.size());
        for(auto &entry:edges) {
            Edge &e=entry.second;const Node lo=current.nodes[e.lo],hi=current.nodes[e.hi];
            Node node;node.logical={(lo.logical.x+hi.logical.x)/2,(lo.logical.y+hi.logical.y)/2};
            const Point target={(targets[e.lo].x+targets[e.hi].x)*.5,(targets[e.lo].y+targets[e.hi].y)*.5};
            Point finalWorld;if(!projection.Transform(target,finalWorld,true)) return false;
            const double demand=weight(level,node.logical,finalWorld);
            if(!std::isfinite(demand)||demand<0||demand>1) return false;
            e.amount=(lockBoundary&&e.count==1)?0:demand*e.gate;
            if(e.amount==0 || (lo.screen.x==hi.screen.x && lo.screen.y==hi.screen.y)) {
                node.screen=lo.screen;node.world=lo.world;node.parent=int(e.lo);
            } else {
                node.screen={lo.screen.x+(hi.screen.x-lo.screen.x)*(.5*e.amount),lo.screen.y+(hi.screen.y-lo.screen.y)*(.5*e.amount)};
                if(!projection.Transform(node.screen,node.world,true)) return false;
            }
            e.node=unsigned(current.nodes.size());current.nodes.push_back(node);targets.push_back(target);
        }
        std::vector<Triangle> triangles;triangles.reserve(current.triangles.size()*4);
        for(const Triangle &t:current.triangles) {
            const Edge &ab=edges.at(edgeKey(t.a,t.b)),&bc=edges.at(edgeKey(t.b,t.c)),&ca=edges.at(edgeKey(t.c,t.a));
            // Every parent edge must open before its children gain detail. If just one edge
            // collapses, a degenerate child's distinct logical edge can coincide with another
            // physical edge; independently splitting it would break nonlinear displaced seams.
            // The minimum fades all such coincident descendants to exact aliases continuously.
            const double gate=t.gate*(std::min)({ab.amount,bc.amount,ca.amount});
            triangles.push_back({t.a,ab.node,ca.node,gate});triangles.push_back({ab.node,t.b,bc.node,gate});
            triangles.push_back({ca.node,bc.node,t.c,gate});triangles.push_back({ab.node,bc.node,ca.node,gate});
        }
        current.triangles.swap(triangles);
    }
    output=std::move(current);return true;
}

struct Bow {
    unsigned id=0;double level=0;
    WaterBowShoulder::Contour contour{};
    // origin follows the contact contour support; centre probes the exterior ridge
    // for floor clearance and visibility.
    Point origin{},centre{},forward{},hullCentre{};
    double sigmaAlong=0,sigmaAcross=0,reach=0;
};
inline bool MakeBow(unsigned id,const WaterHullModel::Hull &mean,const WaterHullModel::Hull &contact,double level,Bow &out) {
    if(!std::isfinite(level)) return false;
    const auto &h=contact.data[0][3]>=3&&contact.data[0][3]<=12?contact:mean;
    const double speed=std::hypot(h.data[2][0],h.data[2][1]);if(!(speed>double(.3f))) return false;
    const auto pressure=WaterVesselWaves::BowPressure(h.data[1][3],float(speed));
    Bow b;b.id=id;b.level=level;
    if(!WaterBowShoulder::Prepare(h,level,pressure.along,pressure.amplitude,b.contour))return false;
    b.hullCentre={b.contour.centre.x,b.contour.centre.y};
    b.forward={h.data[2][0]/speed,h.data[2][1]/speed};
    const auto origin=WaterBowShoulder::ToWorld(b.contour,b.contour.supportCentre);
    b.origin={origin.x,origin.y};
    // Narrowest ridge scale in every orientation, including turning/broadside motion.
    b.sigmaAlong=b.sigmaAcross=b.contour.width;b.reach=b.contour.reach;
    b.centre={b.origin.x+WaterBowShoulder::ShoulderOffset*b.contour.width*b.forward.x,
        b.origin.y+WaterBowShoulder::ShoulderOffset*b.contour.width*b.forward.y};
    if(!Finite(b.centre)||b.sigmaAlong<=0||b.sigmaAcross<=0||b.reach<=0) return false;
    out=b;return true;
}
struct View { WaterProjectedMesh::Projection projection;WaterProjectedMesh::Resolution resolution;double width=0,height=0;double verticalClip[3]={}; };
struct Prepared { Bow bow;Point screen{},cell{};double desiredDepth=0,heightGate=0; };
inline bool PrepareBow(const Bow &bow,const View &view,Prepared &out,double safeAmplitude=1) {
    if(!std::isfinite(view.width)||!std::isfinite(view.height)||view.width<=0||view.height<=0||
        view.resolution.columns<2||view.resolution.rows<2||!std::isfinite(safeAmplitude)||
        !Finite(bow.origin)||!Finite(bow.centre)||!Finite(bow.forward)||!Finite(bow.hullCentre)||
        !std::isfinite(bow.reach)||bow.reach<=0||
        !std::isfinite(bow.sigmaAlong)||bow.sigmaAlong<=0||!std::isfinite(bow.sigmaAcross)||bow.sigmaAcross<=0||
        !std::isfinite(bow.contour.amplitude)||bow.contour.amplitude<0) return false;
    Prepared p;p.bow=bow;if(!view.projection.Transform(bow.centre,p.screen)) return false;
    const double *m=view.projection.matrix,w=m[6]*bow.centre.x+m[7]*bow.centre.y+m[8];if(w<=1e-9) return false;
    const double j[4]={(m[0]-p.screen.x*m[6])/w*view.width*.5,(m[1]-p.screen.x*m[7])/w*view.width*.5,
        (m[3]-p.screen.y*m[6])/w*view.height*.5,(m[4]-p.screen.y*m[7])/w*view.height*.5};
    const Point a={bow.sigmaAlong*(j[0]*bow.forward.x+j[1]*bow.forward.y),bow.sigmaAlong*(j[2]*bow.forward.x+j[3]*bow.forward.y)};
    const Point b={bow.sigmaAcross*(-j[0]*bow.forward.y+j[1]*bow.forward.x),bow.sigmaAcross*(-j[2]*bow.forward.y+j[3]*bow.forward.x)};
    const double xx=a.x*a.x+b.x*b.x,yy=a.y*a.y+b.y*b.y,xy=a.x*a.y+b.x*b.y;
    const double major=.5*(xx+yy+std::hypot(xx-yy,2*xy)),det=(a.x*b.y-a.y*b.x)*(a.x*b.y-a.y*b.x);
    const double minor=std::sqrt((std::max)(0.0,det/(std::max)(major,1e-20)));
    const double limit=1+view.projection.margin,step=(std::max)(limit*view.width/view.resolution.columns,limit*view.height/view.resolution.rows);
    p.desiredDepth=(std::max)(0.0,(std::min)(2.0,std::log2(3*step/(std::max)(1.665109222*minor,1e-9))));
    const double vzx=(view.verticalClip[0]-p.screen.x*view.verticalClip[2])/w*view.width*.5;
    const double vzy=(view.verticalClip[1]-p.screen.y*view.verticalClip[2])/w*view.height*.5;
    const double bound=WaterFftGeometry::MaxVerticalDisplacement;
    const double rawProbe=WaterBowShoulder::Evaluate(bow.contour,{bow.centre.x,bow.centre.y}).value;
    const double height=bound*std::tanh(rawProbe/bound)*Saturate(safeAmplitude);
    p.heightGate=Smooth(.25,1,std::hypot(vzx,vzy)*height);
    // Center the fixed detail window on the contour support. Its inverse
    // horizontal map can lie on either side of that anchor.
    Point anchorScreen;if(!view.projection.Transform(bow.origin,anchorScreen)) return false;
    p.cell={(anchorScreen.x/limit+1)*view.resolution.columns*.5,(anchorScreen.y/limit+1)*view.resolution.rows*.5};
    if(!Finite(p.cell)||!std::isfinite(p.desiredDepth)||!std::isfinite(p.heightGate)) return false;
    out=p;return true;
}
inline double Demand(const Prepared &p,unsigned level,Logical at,Point world,double admission=1) {
    if(level<1||level>2||!Finite(world)) return 0;
    // Signed distance and support-plane retreat are 1-Lipschitz. Expand the
    // inverse image by the bounded FFT displacement without sampling it on the CPU.
    const double radius=WaterFftGeometry::MaxHorizontalDisplacement;
    const double radial=(std::max)(0.0,std::hypot(world.x-p.bow.hullCentre.x,world.y-p.bow.hullCentre.y)-radius);
    const auto lower=WaterBowShoulder::InverseProfileLowerBounds(p.bow.contour,{world.x,world.y},radius);
    const double physical=(1-Smooth(WaterBowShoulder::ShoulderFadeStart,WaterBowShoulder::ShoulderFadeEnd,lower.x))*
        (1-Smooth(WaterBowShoulder::ShoulderFadeStart,WaterBowShoulder::ShoulderFadeEnd,lower.y))*
        (1-Smooth(p.bow.reach-30,p.bow.reach,radial));
    const double cellDistance=(std::max)(std::abs(at.x*.25-p.cell.x),std::abs(at.y*.25-p.cell.y));
    const double window=1-Smooth(3,CoreRadius,cellDistance);
    return Smooth(0,1,p.desiredDepth-double(level-1))*p.heightGate*physical*window*Saturate(admission);
}
// Exact authored patch identity; no camera coordinate or transient draw index participates.
struct PatchKey { std::array<uint32_t,12> words{};uintptr_t owner=0;unsigned part=0;
    bool operator==(const PatchKey &b) const { return words==b.words&&owner==b.owner&&part==b.part; } };
inline PatchKey MakePatchKey(const Point corners[4],float level,uintptr_t owner=0,unsigned part=0) {
    PatchKey key;key.owner=owner;key.part=part;for(unsigned i=0;i<4;++i) {
        const auto p=WaterProjectedMesh::SampleKey(float(corners[i].x),float(corners[i].y),level);
        key.words[i*3]=p.x;key.words[i*3+1]=p.y;key.words[i*3+2]=p.z;
    }return key;
}
struct Active { Prepared prepared;double amount=0; };
class Admissions {
public:
    struct Slot { bool occupied=false,seen=false;PatchKey patch;Bow bow;double amount=0,previous=0; };
private:
    std::array<Slot,MaxRegions> slots{};double lastTime=0,step=0;bool initialized=false;
public:
    void Clear() { slots={};lastTime=step=0;initialized=false; }
    void Begin(double time) {
        if(!std::isfinite(time)) { Clear();return; }
        if(initialized&&time<lastTime) Clear();
        step=initialized?(std::min)(1.0,(time-lastTime)/AdmissionSeconds):0;lastTime=time;initialized=true;
        for(auto &s:slots) { s.seen=false;s.previous=s.amount;s.amount=(std::max)(0.0,s.amount-step); }
    }
    // The base keeps its existing budget. A separate fixed global reserve owns local detail;
    // this bound ensures its three regions always fit each draw's 16-bit vertex addressing.
    static size_t WorstBaseVertices() {
        return WaterProjectedMesh::VertexBound(192,128,4u*(WaterSurfaceModel::MeshCells+1));
    }
    bool Request(const PatchKey &patch,const Bow &bow,size_t patchBudget) {
        unsigned samePatch=0;Slot *empty=nullptr;
        for(auto &s:slots) {
            if(!s.occupied) { if(!empty) empty=&s;continue; }
            if(s.patch==patch) ++samePatch;
            if(s.patch==patch&&s.bow.id==bow.id) {
                // A teleport cannot move a live refinement window to unrelated geometry.
                // Retire the old window, then admit the new location from zero.
                if(std::hypot(s.bow.centre.x-bow.centre.x,s.bow.centre.y-bow.centre.y)>64) return false;
                if(!s.seen) s.amount=(std::min)(1.0,s.previous+step);s.seen=true;s.bow=bow;return true;
            }
        }
        if(!empty || patchBudget==0 || patchBudget>65535 ||
            (std::min)(patchBudget,WorstBaseVertices())+size_t(samePatch+1)*RegionVertices>65535) return false;
        empty->occupied=true;empty->seen=true;empty->patch=patch;empty->bow=bow;empty->amount=0;return true;
    }
    void End() { for(auto &s:slots) if(s.occupied&&!s.seen&&s.amount==0) s=Slot{}; }
    const std::array<Slot,MaxRegions> &Slots() const { return slots; }
};

// Protection goes to zero before any authored FFT collar, clipped-cell topology or frustum
// boundary can change eligibility. It is deliberately more conservative than FFT's seam fade.
class Protection {
    struct Line { WaterProjectedMesh::Plane world,screen;double worldScale=0,screenScale=0; };
    std::vector<Line> lines;WaterProjectedMesh::Projection projection{};
public:
    bool Prepare(const Point corners[4],const View &view) {
        lines.clear();projection=view.projection;
        const double sx=2*(1+projection.margin)/view.resolution.columns,sy=2*(1+projection.margin)/view.resolution.rows;
        auto add=[&](WaterProjectedMesh::Plane plane,bool world) {
            Line e;e.world=plane;e.worldScale=world?1/(std::max)(1e-20,std::hypot(plane.a,plane.b)):0;
            const double *m=projection.inverse;
            e.screen={plane.a*m[0]+plane.b*m[3]+plane.c*m[6],plane.a*m[1]+plane.b*m[4]+plane.c*m[7],plane.a*m[2]+plane.b*m[5]+plane.c*m[8]};
            const double length=std::hypot(e.screen.a*sx,e.screen.b*sy);e.screenScale=length>1e-12?1/length:0;lines.push_back(e);
        };
        for(unsigned i=0;i<4;++i) { const Point a=corners[i],b=corners[(i+1)%4];
            if(std::hypot(b.x-a.x,b.y-a.y)>1e-12) add({a.y-b.y,b.x-a.x,a.x*b.y-b.x*a.y},true); }
        add({projection.depth.a,projection.depth.b,projection.depth.c-projection.nearDistance},false);
        add({-projection.depth.a,-projection.depth.b,projection.farDistance-projection.depth.c},false);
        return true;
    }
    double Gate(Point world,Point screen,const View &view) const {
        double gate=1;
        for(const Line &e:lines) {
            if(e.worldScale>0) gate=(std::min)(gate,Smooth(24,36,std::abs(WaterProjectedMesh::Distance(e.world,world))*e.worldScale));
            if(e.screenScale>0) gate=(std::min)(gate,Smooth(4,6,std::abs(WaterProjectedMesh::Distance(e.screen,screen))*e.screenScale));
        }
        const double limit=1+projection.margin;
        gate=(std::min)(gate,Smooth(2,4,(limit-std::abs(screen.x))*view.resolution.columns/(2*limit)));
        gate=(std::min)(gate,Smooth(2,4,(limit-std::abs(screen.y))*view.resolution.rows/(2*limit)));
        return gate;
    }
};
// One extraction path is shared by admission and actual tessellation. Screen-visible hulls
// alone are insufficient: another same-level polygon must not spend a region's reservation.
inline bool Collect(const WaterProjectedMesh::Mesh &mesh,const View &view,const Point corners[4],
    const std::vector<Active> &active,Topology &base,std::vector<unsigned> &sourceTriangles) {
    if(view.resolution.columns<2||view.resolution.rows<2||mesh.indices.size()%3||active.size()>MaxRegions) return false;
    Protection protection;protection.Prepare(corners,view);
    base.nodes.clear();base.triangles.clear();sourceTriangles.clear();
    std::vector<int> nodeFor(mesh.vertices.size(),-1);
    auto eligible=[](const WaterProjectedMesh::Vertex &v) { return v.lattice>=0&&v.boundary<0&&v.knot<0&&v.transition<0&&v.collar<0; };
    for(size_t i=0;i<mesh.indices.size();i+=3) {
        const unsigned ids[3]={mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]};
        for(unsigned id:ids) if(id>=mesh.vertices.size()) return false;
        if(!eligible(mesh.vertices[ids[0]])||!eligible(mesh.vertices[ids[1]])||!eligible(mesh.vertices[ids[2]])) continue;
        int column=view.resolution.columns,row=view.resolution.rows;
        for(unsigned id:ids) { const int slot=mesh.vertices[id].lattice;column=(std::min)(column,slot%(view.resolution.columns+1));row=(std::min)(row,slot/(view.resolution.columns+1)); }
        bool selected=false;
        for(const Active &a:active) if(a.amount>0 && a.prepared.heightGate>0 && a.prepared.desiredDepth>0 &&
            column>=std::floor(a.prepared.cell.x-WindowRadius)&&column<=std::floor(a.prepared.cell.x+WindowRadius)&&
            row>=std::floor(a.prepared.cell.y-WindowRadius)&&row<=std::floor(a.prepared.cell.y+WindowRadius)) selected=true;
        if(!selected) continue;
        Triangle t;unsigned *target[3]={&t.a,&t.b,&t.c};
        for(unsigned k=0;k<3;++k) {
            const unsigned id=ids[k];const auto &v=mesh.vertices[id];
            if(nodeFor[id]<0) {
                Node n;n.source=int(id);n.world=v.world;n.logical={4*(v.lattice%(view.resolution.columns+1)),4*(v.lattice/(view.resolution.columns+1))};
                const double limit=1+view.projection.margin;
                n.screen={limit*(double(n.logical.x)*.5/view.resolution.columns-1),limit*(double(n.logical.y)*.5/view.resolution.rows-1)};
                nodeFor[id]=int(base.nodes.size());base.nodes.push_back(n);
            }
            *target[k]=unsigned(nodeFor[id]);const Node &n=base.nodes[*target[k]];
            t.gate=(std::min)(t.gate,protection.Gate(n.world,n.screen,view));
        }
        if(base.triangles.size()>=active.size()*13u*13u*2u) return false;
        base.triangles.push_back(t);sourceTriangles.push_back(unsigned(i/3));
    }
    return true;
}
inline bool HasRefinableDemand(const WaterProjectedMesh::Mesh &mesh,const View &view,const Point corners[4],const Prepared &prepared) {
    if(!Finite(prepared.cell)||!std::isfinite(prepared.desiredDepth)||!std::isfinite(prepared.heightGate)||
        prepared.desiredDepth<=0||prepared.heightGate<=0) return false;
    Topology base;std::vector<unsigned> source;
    if(!Collect(mesh,view,corners,{{prepared,1}},base,source)) return false;
    struct Edge { unsigned count=0;double gate=1; };
    std::map<std::pair<unsigned,unsigned>,Edge> edges;
    for(const Triangle &t:base.triangles) {
        const unsigned p[3]={t.a,t.b,t.c};
        for(unsigned i=0;i<3;++i) {
            const auto key=std::minmax(p[i],p[(i+1)%3]);auto &edge=edges[{key.first,key.second}];
            ++edge.count;edge.gate=(std::min)(edge.gate,t.gate);
        }
    }
    for(const auto &entry:edges) {
        if(entry.second.count!=2||entry.second.gate<=0) continue;
        const Node &a=base.nodes[entry.first.first],&b=base.nodes[entry.first.second];
        const Point screen={(a.screen.x+b.screen.x)*.5,(a.screen.y+b.screen.y)*.5};Point world;
        if(!view.projection.Transform(screen,world,true)) continue;
        if(Demand(prepared,1,{(a.logical.x+b.logical.x)/2,(a.logical.y+b.logical.y)/2},world)>0) return true;
    }
    return false;
}
struct Stats { size_t regions=0,addedVertices=0,addedTriangles=0,sourceTriangles=0;bool rejected=false; };
inline bool Refine(WaterProjectedMesh::Mesh &mesh,const View &view,const Point corners[4],
    const std::vector<Active> &active,size_t budget,Stats &stats,
    size_t addedVertexBudget=MaxAddedVertices,size_t addedTriangleBudget=MaxAddedTriangles) {
    stats={};if(active.empty()) return true;
    if(view.resolution.columns<2||view.resolution.rows<2||!std::isfinite(view.width)||!std::isfinite(view.height)||
        view.width<=0||view.height<=0) { stats.rejected=true;return false; }
    if(active.size()>MaxRegions||mesh.vertices.size()>budget||mesh.indices.size()%3) { stats.rejected=true;return false; }
    for(const Active &a:active) {
        const Prepared &p=a.prepared;
        if(!std::isfinite(a.amount)||a.amount<0||a.amount>1||!Finite(p.screen)||!Finite(p.cell)||
            !std::isfinite(p.desiredDepth)||p.desiredDepth<0||p.desiredDepth>2||
            !std::isfinite(p.heightGate)||p.heightGate<0||p.heightGate>1||
            !Finite(p.bow.origin)||!Finite(p.bow.centre)||!Finite(p.bow.forward)||
            !std::isfinite(p.bow.sigmaAlong)||p.bow.sigmaAlong<=0||!std::isfinite(p.bow.sigmaAcross)||p.bow.sigmaAcross<=0||
            !std::isfinite(p.bow.contour.amplitude)||p.bow.contour.amplitude<0||!Finite(p.bow.hullCentre)||!std::isfinite(p.bow.reach)||p.bow.reach<=0) {
            stats.rejected=true;return false;
        }
    }
    for(uint16_t index:mesh.indices) if(index>=mesh.vertices.size()) { stats.rejected=true;return false; }
    Topology base;std::vector<unsigned> sourceTriangles;
    if(!Collect(mesh,view,corners,active,base,sourceTriangles)) { stats.rejected=true;return false; }
    if(base.triangles.empty()) return true;
    if(base.triangles.size()>active.size()*13u*13u*2u) { stats.rejected=true;return false; }
    Topology refined;
    const auto demand=[&](unsigned level,Logical logical,Point world) {
        double result=0;for(const Active &a:active) result=(std::max)(result,Demand(a.prepared,level,logical,world,a.amount));return result;
    };
    if(!Subdivide(base,view.projection,demand,refined,2,true)) { stats.rejected=true;return false; }
    std::vector<WaterProjectedMesh::Vertex> added;added.reserve(active.size()*RegionVertices);
    std::vector<uint16_t> mapped(refined.nodes.size());
    for(size_t i=0;i<refined.nodes.size();++i) {
        const Node &n=refined.nodes[i];
        if(n.source>=0) mapped[i]=uint16_t(n.source);
        else if(n.parent>=0) mapped[i]=mapped[size_t(n.parent)];
        else {
            const size_t next=mesh.vertices.size()+added.size();
            if(next>=budget||next>=65535||added.size()>=(std::min)(size_t(active.size()*RegionVertices),addedVertexBudget)) { stats.rejected=true;return false; }
            WaterProjectedMesh::Vertex v;v.world=n.world;mapped[i]=uint16_t(next);added.push_back(v);
        }
    }
    // No real new sample means exact original indices, attributes and shader work remain.
    if(added.empty()) return true;
    std::vector<uint16_t> replacement;replacement.reserve(refined.triangles.size()*3);
    for(const Triangle &t:refined.triangles) {
        const uint16_t a=mapped[t.a],b=mapped[t.b],c=mapped[t.c];
        if(a==b||b==c||c==a) continue;
        replacement.insert(replacement.end(),{a,b,c});
    }
    const size_t addedTriangles=replacement.size()/3-sourceTriangles.size();
    if(addedTriangles>(std::min)(size_t(active.size()*RegionTriangles),addedTriangleBudget)) { stats.rejected=true;return false; }
    std::vector<uint16_t> indices;indices.reserve(mesh.indices.size()+addedTriangles*3);
    size_t selected=0;
    for(size_t i=0;i<mesh.indices.size()/3;++i) {
        if(selected<sourceTriangles.size()&&sourceTriangles[selected]==i) { ++selected;continue; }
        indices.insert(indices.end(),mesh.indices.begin()+i*3,mesh.indices.begin()+i*3+3);
    }
    indices.insert(indices.end(),replacement.begin(),replacement.end());
    mesh.vertices.insert(mesh.vertices.end(),added.begin(),added.end());mesh.indices.swap(indices);
    stats.regions=active.size();stats.sourceTriangles=sourceTriangles.size();stats.addedVertices=added.size();stats.addedTriangles=addedTriangles;return true;
}

}
