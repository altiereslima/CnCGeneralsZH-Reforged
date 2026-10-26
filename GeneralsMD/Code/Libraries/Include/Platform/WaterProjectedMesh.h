/* Render-only projected water lattice. No simulation or map data is modified. */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace WaterProjectedMesh {
struct Point { double x,y; };
struct Plane { double a,b,c; };
inline bool HasSingleLevel(const float levels[4]) {
    if (!std::isfinite(levels[0])) return false;
    for (int i=1;i<4;++i) if (levels[i]!=levels[0]) return false;
    return true;
}
struct Projection {
    // (clip x, clip y, clip w) = matrix * (world x, world y, 1), at the water level.
    double matrix[9], inverse[9];
    Plane depth;
    double nearDistance,farDistance,margin;
    bool Prepare() {
        const double *m=matrix;
        const double d=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
        if (!std::isfinite(d) || std::abs(d)<1e-12) return false;
        const double adj[9]={m[4]*m[8]-m[5]*m[7],m[2]*m[7]-m[1]*m[8],m[1]*m[5]-m[2]*m[4],
            m[5]*m[6]-m[3]*m[8],m[0]*m[8]-m[2]*m[6],m[2]*m[3]-m[0]*m[5],
            m[3]*m[7]-m[4]*m[6],m[1]*m[6]-m[0]*m[7],m[0]*m[4]-m[1]*m[3]};
        for (int i=0;i<9;++i) inverse[i]=adj[i]/d;
        return nearDistance>0 && farDistance>nearDistance && margin>=0 && std::isfinite(margin);
    }
    bool Transform(Point p,Point &out,bool backwards=false) const {
        const double *m=backwards?inverse:matrix;
        const double w=m[6]*p.x+m[7]*p.y+m[8];
        if (!std::isfinite(w) || std::abs(w)<1e-12) return false;
        out={(m[0]*p.x+m[1]*p.y+m[2])/w,(m[3]*p.x+m[4]*p.y+m[5])/w};
        return std::isfinite(out.x) && std::isfinite(out.y);
    }
};
struct Polygon { Point points[20]; int count=0; };
inline double Distance(const Plane &plane,Point p) { return plane.a*p.x+plane.b*p.y+plane.c; }
inline Point Intersection(Point a,Point b,const Plane &plane) {
    // Canonical endpoint order makes reversed shared polygon boundaries yield the same result.
    if (a.x>b.x || (a.x==b.x && a.y>b.y)) { const Point c=a;a=b;b=c; }
    const double da=Distance(plane,a),db=Distance(plane,b),t=da/(da-db);
    Point p={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
    // Axis clips produce exact lattice coordinates, including at a lattice corner.
    if (plane.b==0 && plane.a!=0) p.x=-plane.c/plane.a;
    if (plane.a==0 && plane.b!=0) p.y=-plane.c/plane.b;
    return p;
}
inline bool Clip(Polygon &polygon,const Plane &plane) {
    if (!polygon.count) return true;
    Polygon result;
    Point previous=polygon.points[polygon.count-1]; double previousDistance=Distance(plane,previous);
    for (int i=0;i<polygon.count;++i) {
        const Point current=polygon.points[i]; const double currentDistance=Distance(plane,current);
        if ((currentDistance>=0)!=(previousDistance>=0)) {
            if (result.count>=20) return false;
            result.points[result.count++]=Intersection(previous,current,plane);
        }
        if (currentDistance>=0) {
            if (result.count>=20) return false;
            result.points[result.count++]=current;
        }
        previous=current;previousDistance=currentDistance;
    }
    polygon=result;return true;
}
inline size_t VertexBound(int columns,int rows,size_t boundaryKnots=0) {
    // A convex boundary crosses each lattice line at most twice. Extra slack covers frustum
    // clipping corners and one centre per clipped cell; lattice interior vertices are shared.
    return size_t(columns+1)*(rows+1)+6u*(columns+rows+4)+boundaryKnots;
}
struct Resolution { int columns,rows; };
inline Resolution ChooseResolution(int width,int height,int flatPatches,size_t available,size_t boundaryKnots=0) {
    Resolution r={(std::max)(8,(std::min)(192,(width+7)/8)),(std::max)(8,(std::min)(128,(height+7)/8))};
    const size_t share=available/size_t((std::max)(1,flatPatches));
    while (VertexBound(r.columns,r.rows,boundaryKnots)>(std::min)(size_t(60000),share) && (r.columns>2 || r.rows>2)) {
        r.columns=(std::max)(2,r.columns/2);r.rows=(std::max)(2,r.rows/2);
    }
    return r;
}
constexpr double CollarWidth=12.0;
struct BoundaryCollar {
    int count=0,edge[4];double parameter[4],weight[4],fade=0;
    void Add(int index,double parameterValue,double distanceSquared) {
        if (distanceSquared>=CollarWidth*CollarWidth) return;
        double t=1-std::sqrt(distanceSquared)/CollarWidth;t=t*t*(3-2*t);
        edge[count]=index;parameter[count]=parameterValue;
        weight[count++]=t/(distanceSquared+1e-12);fade=(std::max)(fade,t);
    }
    void Normalize() {
        double sum=0;for (int i=0;i<count;++i) sum+=weight[i];
        if (sum>0) for (int i=0;i<count;++i) weight[i]*=fade/sum;
    }
};
inline BoundaryCollar CollarAt(const Point corners[4],Point p) {
    BoundaryCollar collar;
    for (int edge=0;edge<4;++edge) {
        Point a=corners[edge],b=corners[(edge+1)%4];
        if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
        const double dx=b.x-a.x,dy=b.y-a.y,len2=dx*dx+dy*dy;
        if (len2<1e-12) continue;
        const double t=(std::max)(0.0,(std::min)(1.0,((p.x-a.x)*dx+(p.y-a.y)*dy)/len2));
        const double ex=p.x-a.x-dx*t,ey=p.y-a.y-dy*t;
        collar.Add(edge,t,ex*ex+ey*ey);
    }
    collar.Normalize();return collar;
}
struct Vertex { Point world; int boundary=-1; double edgeParameter=0; int knot=-1; int transition=-1; int collar=-1; int lattice=-1; };
struct BoundaryKnot { Point world; int boundary,index; double edgeParameter; };
// A clipped cell always has the same centre fan, whether or not it currently contains an edge
// knot. As clipping vanishes, its centre approaches the regular cell's diagonal and its height
// approaches that diagonal's interpolation. The full-cell path can then omit it continuously.
struct CentreTransition { Point source[3]; double weight[3]; double analytic; };
inline bool NormalizeCentreWeights(double weight[3]) {
    // Inverse-projecting a reference corner across the horizon reverses its homogeneous sign.
    // Such a triangle does not contain the visible centre, even if its screen triangle does.
    // Only convex interpolation preserves the displacement bound; repair roundoff, not geometry.
    for (int i=0;i<3;++i)
        if (!std::isfinite(weight[i]) || weight[i]<-1e-10 || weight[i]>1+1e-10) return false;
    double sum=0;for (int i=0;i<3;++i) { weight[i]=(std::max)(0.0,(std::min)(1.0,weight[i]));sum+=weight[i]; }
    if (!(sum>0)) return false;
    for (int i=0;i<3;++i) weight[i]/=sum;
    return true;
}
inline double InterpolateCentre(const CentreTransition &transition,double analytic,const double source[3]) {
    const double linear=source[0]*transition.weight[0]+source[1]*transition.weight[1]+source[2]*transition.weight[2];
    return linear+(analytic-linear)*transition.analytic;
}
struct Mesh { std::vector<Vertex> vertices; std::vector<uint16_t> indices; std::vector<CentreTransition> transitions; std::vector<BoundaryCollar> collars; size_t cells=0; };
struct Key { uint64_t x,y; bool operator==(const Key &other) const { return x==other.x && y==other.y; } };
struct Hash { size_t operator()(const Key &key) const { return size_t(key.x^(key.y+0x9e3779b97f4a7c15ull+(key.x<<6)+(key.x>>2))); } };
struct WorldKey { uint32_t x,y,z; bool operator==(const WorldKey &o) const { return x==o.x && y==o.y && z==o.z; } };
struct WorldHash { size_t operator()(const WorldKey &k) const { return size_t(k.x)*73856093u^size_t(k.y)*19349663u^size_t(k.z)*83492791u; } };
inline WorldKey SampleKey(float x,float y,float z) {
    WorldKey k;if (x==0) x=0;if (y==0) y=0;if (z==0) z=0;
    std::memcpy(&k.x,&x,4);std::memcpy(&k.y,&y,4);std::memcpy(&k.z,&z,4);return k;
}
inline Key PointKey(Point p) {
    Key key; if (p.x==0) p.x=0; if (p.y==0) p.y=0;
    std::memcpy(&key.x,&p.x,8);std::memcpy(&key.y,&p.y,8);return key;
}
inline bool Build(const Point corners[4],const Projection &projection,Resolution resolution,Mesh &mesh,size_t budget=60000,const std::vector<BoundaryKnot> &knots={},bool useLatticeCache=true) {
    mesh.vertices.clear();mesh.indices.clear();mesh.transitions.clear();mesh.collars.clear();mesh.cells=0;
    if (resolution.columns<2 || resolution.rows<2 || VertexBound(resolution.columns,resolution.rows,knots.size())>budget || budget>65535) return false;
    // Convex water patches (including a repeated final triangle corner) are supported.
    // Concave authored geometry retains the legacy renderer instead of losing coverage.
    double orientation=0;
    for (int i=0;i<4;++i) {
        const Point a=corners[i],b=corners[(i+1)%4],c=corners[(i+2)%4];
        const double turn=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        if (!std::isfinite(turn)) return false;
        if (std::abs(turn)>1e-10) { if (orientation*turn<0) return false;orientation=turn; }
    }
    if (orientation==0) return false;
    struct ScreenKnot { Point screen; const BoundaryKnot *source; };
    std::vector<ScreenKnot> screenKnots;screenKnots.reserve(knots.size());
    for (const BoundaryKnot &k:knots) {
        Point screen;
        if (Distance(projection.depth,k.world)>=projection.nearDistance &&
            Distance(projection.depth,k.world)<=projection.farDistance && projection.Transform(k.world,screen))
            screenKnots.push_back({screen,&k});
    }
    Polygon footprint;for (int i=0;i<4;++i) footprint.points[footprint.count++]=corners[i];
    if (!Clip(footprint,{projection.depth.a,projection.depth.b,projection.depth.c-projection.nearDistance}) ||
        !Clip(footprint,{-projection.depth.a,-projection.depth.b,projection.farDistance-projection.depth.c})) return false;
    const double limit=1+projection.margin;
    for (int axis=0;axis<2;++axis) for (int sign:{-1,1}) {
        const double *m=projection.matrix;
        if (!Clip(footprint,{limit*m[6]+sign*m[axis*3],limit*m[7]+sign*m[axis*3+1],limit*m[8]+sign*m[axis*3+2]})) return false;
    }
    if (footprint.count<3) return true;
    for (int i=0;i<footprint.count;++i) {
        Point screen;if (!projection.Transform(footprint.points[i],screen)) return false;
        footprint.points[i]=screen;
    }
    double area=0;for (int i=0,j=footprint.count-1;i<footprint.count;j=i++) area+=footprint.points[j].x*footprint.points[i].y-footprint.points[i].x*footprint.points[j].y;
    if (std::abs(area)<1e-14) return true;
    if (area<0) { std::reverse(footprint.points,footprint.points+footprint.count);area=-area; }
    Plane boundary[20];
    for (int i=0,j=footprint.count-1;i<footprint.count;j=i++) {
        const Point a=footprint.points[j],b=footprint.points[i]; const double orientation=area>0?1:-1;
        boundary[i]={orientation*(a.y-b.y),orientation*(b.x-a.x),orientation*(a.x*b.y-b.x*a.y)};
    }
    mesh.vertices.reserve(VertexBound(resolution.columns,resolution.rows,knots.size()));
    mesh.indices.reserve(size_t(resolution.columns)*resolution.rows*6+18u*(resolution.columns+resolution.rows+4)+knots.size()*6);
    std::unordered_map<Key,uint16_t,Hash> known;
    known.reserve(useLatticeCache?6u*(resolution.columns+resolution.rows+4)+knots.size():VertexBound(resolution.columns,resolution.rows));
    // Integer slots replace repeated hashing for exact regular lattice corners. Every clipped
    // corner uses the same slots; genuinely new boundary intersections retain canonical keys.
    // The switch exists for an exact reference-regression comparison, not runtime quality.
    std::vector<int> lattice(useLatticeCache?size_t(resolution.columns+1)*(resolution.rows+1):0,-1);
    auto append=[&](Point screen,uint16_t &index,const BoundaryKnot *knot=nullptr,int slot=-1) {
        const int logicalSlot=slot;
        if (!useLatticeCache) slot=-1;
        const Key key=PointKey(screen);
        const auto found=slot<0?known.find(key):known.end();
        if ((slot>=0 && lattice[slot]>=0) || found!=known.end()) {
            index=slot>=0?uint16_t(lattice[slot]):found->second;
            if (knot) mesh.vertices[index]={knot->world,knot->boundary,knot->edgeParameter,knot->index};
            return true;
        }
        Vertex vertex;vertex.lattice=logicalSlot;if (!projection.Transform(screen,vertex.world,true)) return false;
        BoundaryCollar collar;
        for (int edge=0;edge<4;++edge) {
            Point a=corners[edge],b=corners[(edge+1)%4];
            if (a.x>b.x || (a.x==b.x && a.y>b.y)) { const Point swap=a;a=b;b=swap; }
            const double dx=b.x-a.x,dy=b.y-a.y,len2=dx*dx+dy*dy;
            if (len2<1e-12) continue;
            const double t=((vertex.world.x-a.x)*dx+(vertex.world.y-a.y)*dy)/len2;
            const double cross=(vertex.world.x-a.x)*dy-(vertex.world.y-a.y)*dx;
            const double bounded=(std::max)(0.0,(std::min)(1.0,t));
            const double ex=vertex.world.x-a.x-dx*bounded,ey=vertex.world.y-a.y-dy*bounded;
            collar.Add(edge,bounded,ex*ex+ey*ey);
            if (t>=-1e-8 && t<=1+1e-8 && std::abs(cross)<1e-8*(len2+1)) {
                vertex.boundary=edge;vertex.edgeParameter=(std::max)(0.0,(std::min)(1.0,t));
                // Both owners reconstruct the same authored edge in canonical world order.
                vertex.world={a.x+dx*vertex.edgeParameter,a.y+dy*vertex.edgeParameter};break;
            }
        }
        if (knot) vertex={knot->world,knot->boundary,knot->edgeParameter,knot->index};
        if (vertex.boundary<0 && collar.count) {
            collar.Normalize();vertex.collar=int(mesh.collars.size());mesh.collars.push_back(collar);
        }
        if (mesh.vertices.size()>=budget) return false;
        index=uint16_t(mesh.vertices.size());
        if (slot>=0) lattice[slot]=index;else known.emplace(key,index);
        mesh.vertices.push_back(vertex);return true;
    };
    for (int row=0;row<resolution.rows;++row) for (int column=0;column<resolution.columns;++column) {
        const double left=limit*(2.0*column/resolution.columns-1),right=limit*(2.0*(column+1)/resolution.columns-1);
        const double bottom=limit*(2.0*row/resolution.rows-1),top=limit*(2.0*(row+1)/resolution.rows-1);
        const Point centre={(left+right)*0.5,(bottom+top)*0.5}; bool outside=false,inside=true;
        for (int edge=0;edge<footprint.count;++edge) {
            const Plane &p=boundary[edge];const double distance=Distance(p,centre);
            const double extent=std::abs(p.a)*(right-left)*0.5+std::abs(p.b)*(top-bottom)*0.5;
            if (distance+extent<-1e-14) { outside=true;break; }
            if (distance-extent<=1e-14) inside=false;
        }
        if (outside) continue;
        Polygon cell;
        if (inside) { cell.count=4;cell.points[0]={left,bottom};cell.points[1]={right,bottom};cell.points[2]={right,top};cell.points[3]={left,top}; }
        else {
            cell=footprint;
            if (!Clip(cell,{1,0,-left}) || !Clip(cell,{-1,0,right}) || !Clip(cell,{0,1,-bottom}) || !Clip(cell,{0,-1,top})) return false;
        }
        if (cell.count<3) continue;
        if (inside) {
            const int a=row*(resolution.columns+1)+column;
            const int slots[4]={a,a+1,a+resolution.columns+2,a+resolution.columns+1};
            uint16_t indices[4];for (int i=0;i<4;++i) if (!append(cell.points[i],indices[i],nullptr,slots[i])) return false;
            mesh.indices.insert(mesh.indices.end(),{indices[0],indices[1],indices[2],indices[0],indices[2],indices[3]});
            ++mesh.cells;continue;
        }
        // A retained river edge can contain several displaced knots inside one projected
        // cell. Include the union of those knots and lattice intersections. A centre fan
        // makes every resulting boundary segment an actual triangle edge, even if collinear.
        struct CellPoint { Point screen;const BoundaryKnot *knot; };
        std::vector<CellPoint> polygon;polygon.reserve(cell.count+4);
        for (int i=0;i<cell.count;++i) {
            const Point a=cell.points[i],b=cell.points[(i+1)%cell.count];
            const double dx=b.x-a.x,dy=b.y-a.y,len2=dx*dx+dy*dy;
            const BoundaryKnot *atStart=nullptr;
            std::vector<std::pair<double,const ScreenKnot *>> between;
            if (!inside && len2>1e-24) for (const ScreenKnot &k:screenKnots) {
                if (k.screen.x<left-1e-12 || k.screen.x>right+1e-12 || k.screen.y<bottom-1e-12 || k.screen.y>top+1e-12) continue;
                const double t=((k.screen.x-a.x)*dx+(k.screen.y-a.y)*dy)/len2;
                const double cross=(k.screen.x-a.x)*dy-(k.screen.y-a.y)*dx;
                if (std::abs(cross)>1e-12*len2) continue;
                if (std::abs(t)<1e-12) atStart=k.source;
                else if (t>1e-12 && t<1-1e-12) between.push_back({t,&k});
            }
            polygon.push_back({atStart?Point{0,0}:a,atStart});
            if (atStart) projection.Transform(atStart->world,polygon.back().screen);
            std::sort(between.begin(),between.end(),[](const auto &a,const auto &b) { return a.first<b.first; });
            for (const auto &entry:between) polygon.push_back({entry.second->screen,entry.second->source});
        }
        auto appendClipped=[&](Point p,uint16_t &index,const BoundaryKnot *knot=nullptr) {
            const int x=p.x==left?column:(p.x==right?column+1:-1);
            const int y=p.y==bottom?row:(p.y==top?row+1:-1);
            return append(p,index,knot,x>=0 && y>=0?y*(resolution.columns+1)+x:-1);
        };
        std::vector<uint16_t> polygonIndices(polygon.size());
        for (size_t i=0;i<polygon.size();++i) if (!appendClipped(polygon[i].screen,polygonIndices[i],polygon[i].knot)) return false;
        auto triangle=[&](uint16_t ia,uint16_t ib,uint16_t ic,Point a,Point b,Point c) {
            if (std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))<1e-16) return;
            mesh.indices.push_back(ia);mesh.indices.push_back(ib);mesh.indices.push_back(ic);
        };
        // The area centroid is independent of inserted knots and duplicate clipping corners.
        // An arithmetic vertex average would jump when a knot enters or leaves this cell.
        Point clippedCentroid={0,0};double twiceArea=0;
        for (int i=0;i<cell.count;++i) {
            const Point a=cell.points[i],b=cell.points[(i+1)%cell.count];
            const double cross=a.x*b.y-b.x*a.y;
            twiceArea+=cross;clippedCentroid.x+=(a.x+b.x)*cross;clippedCentroid.y+=(a.y+b.y)*cross;
        }
        if (std::abs(twiceArea)<1e-16) continue;
        clippedCentroid.x/=3*twiceArea;clippedCentroid.y/=3*twiceArea;
        uint16_t clippedCentroidIndex;if (!appendClipped(clippedCentroid,clippedCentroidIndex)) return false;
        double clipped=1-std::abs(twiceArea)/(2*(right-left)*(top-bottom));
        clipped=(std::max)(0.0,(std::min)(1.0,clipped/0.25));
        if (clipped<1) {
            CentreTransition transition={};transition.analytic=clipped*clipped*(3-2*clipped);
            const bool lower=(clippedCentroid.x-left)/(right-left)>=(clippedCentroid.y-bottom)/(top-bottom);
            const Point screen[3]={{left,bottom},lower?Point{right,bottom}:Point{left,top},{right,top}};
            bool valid=true;for (int i=0;i<3;++i) valid=projection.Transform(screen[i],transition.source[i],true)&&valid;
            const Point a=transition.source[0],b=transition.source[1],c=transition.source[2],p=mesh.vertices[clippedCentroidIndex].world;
            const double determinant=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            if (valid && std::isfinite(determinant) && std::abs(determinant)>1e-20) {
                transition.weight[1]=((p.x-a.x)*(c.y-a.y)-(p.y-a.y)*(c.x-a.x))/determinant;
                transition.weight[2]=((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x))/determinant;
                transition.weight[0]=1-transition.weight[1]-transition.weight[2];
                if (NormalizeCentreWeights(transition.weight)) {
                    mesh.vertices[clippedCentroidIndex].transition=int(mesh.transitions.size());mesh.transitions.push_back(transition);
                }
            }
        }
        for (size_t i=0;i<polygon.size();++i) {
            const size_t next=(i+1)%polygon.size();
            triangle(clippedCentroidIndex,polygonIndices[i],polygonIndices[next],clippedCentroid,polygon[i].screen,polygon[next].screen);
        }
        ++mesh.cells;
    }
    return true;
}
}
