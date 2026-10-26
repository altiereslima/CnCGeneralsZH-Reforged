/* Support for history interpolation: a sample may only see a node along a wet,
   polygon-covered segment. Original bilinear weights are never renormalized. */
#pragma once
#include "Platform/WaterShoreHistory.h"
#include "Platform/WaterOpticalContext.h"
#include <array>

namespace WaterShoreSupport {
constexpr size_t MaxTiles=4*WaterShoreHistory::MaxCells;
constexpr size_t MaxTileVertices=128,MaxVertices=16*WaterShoreHistory::MaxCells;
constexpr size_t MaxWork=8*1024*1024,MaxClipVertices=4*WaterOpticalContext::MaxPoints;
enum class AreaResult {Covered,Unsupported,OverBudget};
struct Point {double x=0,y=0;};
struct Polygon {uint32_t first=0,count=0;};
struct Tile {
    int32_t x=0,y=0;double level=0;
    std::array<float,4> heights{};
    uint32_t first=0,count=0;
    bool interior=false,wet=false;
};
inline bool Less(const Tile&a,const Tile&b) {
    if(a.level!=b.level)return a.level<b.level;
    if(a.y!=b.y)return a.y<b.y;return a.x<b.x;
}
inline bool Same(const Tile&a,const Tile&b){return a.x==b.x&&a.y==b.y&&a.level==b.level;}
// True only when a nonempty part of an edge lies in the OPEN square.
inline bool CrossesInterior(Point a,Point b) {
    double low=0,high=1;
    for(unsigned axis=0;axis<2;++axis) {
        const double p=axis?a.y:a.x,d=axis?b.y-a.y:b.x-a.x;
        if(d==0){if(p<=0||p>=1)return false;continue;}
        const double u=-p/d,v=(1-p)/d;
        low=(std::max)(low,(std::min)(u,v));high=(std::min)(high,(std::max)(u,v));
    }
    if(!(high>low))return false;
    const double t=(low+high)*.5,x=a.x+(b.x-a.x)*t,y=a.y+(b.y-a.y)*t;
    return x>0&&x<1&&y>0&&y<1;
}
class Domain {
    WaterShoreHistory::Grid grid;
    std::vector<Tile> tiles;
    std::vector<Polygon> shapes;
    std::vector<Point> vertices;
    size_t work=0;
    bool Spend(size_t amount=1){if(amount>MaxWork-work)return false;work+=amount;return true;}
    static double Height(const Tile&t,double x,double y) {
        return (t.heights[0]*(1-x)+t.heights[1]*x)*(1-y)
             +(t.heights[2]*(1-x)+t.heights[3]*x)*y;
    }
    bool PolygonSegment(const Tile&t,Point a,Point b)const {
        if(t.interior)return true;
        const double dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
        // The start is an admitted source node. This limit is < 1e-9 world units.
        if(length2<1e-24)return true;
        std::array<std::pair<double,double>,3*MaxTileVertices> spans;
        size_t spanCount=0;
        for(size_t shape=t.first;shape<size_t(t.first)+t.count;++shape) {
            const Polygon&p=shapes[shape];std::array<double,MaxTileVertices> crossings;size_t count=0;
            for(size_t i=0,j=p.count-1;i<p.count;j=i++) {
                const Point&v=vertices[p.first+i];const Point&u=vertices[p.first+j];
                const double vx=v.x-a.x,vy=v.y-a.y,ux=u.x-a.x,uy=u.y-a.y;
                double va=vx*dy-vy*dx,ua=ux*dy-uy*dx;
                // Roundoff tolerance in normalized square coordinates, not a world-sized gap.
                if(std::abs(va)<1e-13)va=0;if(std::abs(ua)<1e-13)ua=0;
                const double vp=(vx*dx+vy*dy)/length2,up=(ux*dx+uy*dy)/length2;
                if((va>0)!=(ua>0))crossings[count++]=vp+(up-vp)*(-va)/(ua-va);
                if(va==0) {
                    spans[spanCount++]={vp,vp};
                    if(ua==0)spans[spanCount++]={(std::min)(vp,up),(std::max)(vp,up)};
                }
            }
            std::sort(crossings.begin(),crossings.begin()+count);
            for(size_t i=0;i+1<count;i+=2)spans[spanCount++]={crossings[i],crossings[i+1]};
        }
        std::sort(spans.begin(),spans.begin()+spanCount);
        double covered=0;
        for(size_t i=0;i<spanCount;++i) {
            if(spans[i].second<-1e-12)continue;
            if(spans[i].first>covered+1e-12)return false;
            covered=(std::max)(covered,spans[i].second);
            if(covered>=1-1e-12)return true;
        }
        return false;
    }
    AreaResult CoversRectangle(const Tile&t,double x0,double y0,double x1,double y1,size_t &remaining)const {
        if(!std::isfinite(x0)||!std::isfinite(y0)||!std::isfinite(x1)||!std::isfinite(y1)
            ||x0<0||y0<0||x1>1||y1>1||x1<=x0||y1<=y0)return AreaResult::Unsupported;
        auto spend=[&](size_t n=1){if(n>remaining)return false;remaining-=n;return true;};
        if(!spend(5))return AreaResult::OverBudget;
        // A bilinear function's maximum on an axis-aligned rectangle is attained
        // at a corner. This certifies its WHOLE area, including diagonal saddles.
        if(!t.wet && !(Height(t,x0,y0)<t.level&&Height(t,x1,y0)<t.level
            &&Height(t,x0,y1)<t.level&&Height(t,x1,y1)<t.level))return AreaResult::Unsupported;
        if(t.interior)return AreaResult::Covered;
        const double cx=(x0+x1)*.5,cy=(y0+y1)*.5,rx=1/(x1-x0),ry=1/(y1-y0);
        for(size_t shape=t.first;shape<size_t(t.first)+t.count;++shape) {
            if(!spend())return AreaResult::OverBudget;
            const Polygon&p=shapes[shape];bool inside=false,crosses=false;
            for(size_t i=0,j=p.count-1;i<p.count;j=i++) {
                if(!spend())return AreaResult::OverBudget;
                const Point&a=vertices[p.first+j],&b=vertices[p.first+i];
                // Merely testing corners or polygon vertices misses thin cuts.
                // Every segment is tested against the OPEN rectangle interior.
                if(CrossesInterior({(a.x-x0)*rx,(a.y-y0)*ry},{(b.x-x0)*rx,(b.y-y0)*ry})) {crosses=true;break;}
                if((a.y>cy)!=(b.y>cy))inside=inside^(cx<a.x+(cy-a.y)*(b.x-a.x)/(b.y-a.y));
            }
            // With no boundary entering the connected rectangle interior, one
            // interior point fixes its membership. Single-polygon coverage is a
            // sufficient proof; union-only seams are deliberately rejected.
            if(!crosses&&inside)return AreaResult::Covered;
        }
        return AreaResult::Unsupported;
    }
    bool Allows(const Tile&t,unsigned corner,double x,double y)const {
        const Point node={double(corner&1),double(corner>>1)},target={x,y};
        if(!t.wet) {
            // A straight segment in a half-terrain-cell square has quadratic height.
            // Check its interior maximum too: wet endpoints can straddle a dry saddle.
            const double h0=t.heights[corner],h1=Height(t,x,y);
            if(!(h0<t.level&&h1<t.level))return false;
            const double hm=Height(t,(node.x+x)*.5,(node.y+y)*.5);
            const double a=2*(h1+h0-2*hm),b=h1-h0-a;
            if(a<0) {
                const double at=-b/(2*a);
                if(at>0&&at<1&&a*at*at+b*at+h0>=t.level)return false;
            }
        }
        return PolygonSegment(t,node,target);
    }
public:
    struct Stencil {
        const Domain*domain=nullptr;const Tile*tile=nullptr;double x=0,y=0;
        bool Allows(unsigned dx,unsigned dy)const{return tile&&domain->Allows(*tile,dy*2+dx,x,y);}
        AreaResult CoversRectangle(double x0,double y0,double x1,double y1,size_t &remaining)const {
            return tile?domain->CoversRectangle(*tile,x0,y0,x1,y1,remaining):AreaResult::Unsupported;
        }
    };
    Stencil At(int32_t x,int32_t y,double level,double fx,double fy)const {
        Tile key;key.x=x;key.y=y;key.level=level;
        const auto at=std::lower_bound(tiles.begin(),tiles.end(),key,Less);
        return {this,at!=tiles.end()&&Same(*at,key)?&*at:nullptr,fx,fy};
    }
    template<class HeightSampler> bool Build(const WaterShoreHistory::Grid&inputGrid,
        const std::vector<WaterShoreHistory::Cell>&cells,int width,int height,
        const std::vector<WaterOpticalContext::Polygon>&polygons,HeightSampler sampleHeight) {
        if(!WaterShoreHistory::ValidGrid(inputGrid)||cells.size()>WaterShoreHistory::MaxCells
            ||width<2||height<2||width>WaterOpticalContext::MaxDimension||height>WaterOpticalContext::MaxDimension
            ||size_t(width)*height>WaterOpticalContext::MaxCells||polygons.size()>WaterOpticalContext::MaxPoints/3)return false;
        size_t totalPoints=0;
        for(const auto&p:polygons) {
            if(p.points.size()>WaterOpticalContext::MaxPoints-totalPoints)return false;totalPoints+=p.points.size();
            for(const auto&v:p.points)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return false;
        }
        Domain next;next.grid=inputGrid;
        for(const auto&c:cells) {
            if(c.x<0||c.y<0||c.x>=2*(width-1)||c.y>=2*(height-1)||!std::isfinite(c.level))return false;
            for(int dy=-1;dy<=0;++dy)for(int dx=-1;dx<=0;++dx) {
                if(c.x+dx<0||c.y+dy<0)continue;
                Tile t;t.x=c.x+dx;t.y=c.y+dy;t.level=c.level;next.tiles.push_back(t);
            }
        }
        std::sort(next.tiles.begin(),next.tiles.end(),Less);
        next.tiles.erase(std::unique(next.tiles.begin(),next.tiles.end(),Same),next.tiles.end());
        if(next.tiles.size()>MaxTiles)return false;
        for(Tile&t:next.tiles) {
            for(unsigned i=0;i<4;++i)t.heights[i]=sampleHeight((t.x+double(i&1))*.5,(t.y+double(i>>1))*.5);
            t.wet=true;for(float h:t.heights){if(!std::isfinite(h))return false;t.wet=t.wet&&h<t.level;}
            const double ox=next.grid.originX+t.x*next.grid.spacing,oy=next.grid.originY+t.y*next.grid.spacing;
            std::vector<size_t> boundary;
            for(size_t p=0;p<polygons.size();++p) {
                if(!next.Spend())return false;
                const auto&poly=polygons[p];if(poly.points.size()<3)continue;
                bool same=true;for(const auto&v:poly.points) {
                    if(!next.Spend())return false;same=same&&std::abs(double(v.z)-t.level)<.01;
                }
                if(!same)continue;
                bool inside=false,crosses=false;
                for(size_t i=0,j=poly.points.size()-1;i<poly.points.size();j=i++) {
                    if(!next.Spend())return false;
                    const Point a={(poly.points[j].x-ox)/next.grid.spacing,(poly.points[j].y-oy)/next.grid.spacing};
                    const Point b={(poly.points[i].x-ox)/next.grid.spacing,(poly.points[i].y-oy)/next.grid.spacing};
                    crosses=crosses||CrossesInterior(a,b);
                    if((a.y>.5)!=(b.y>.5))inside=inside^(.5<a.x+(.5-a.y)*(b.x-a.x)/(b.y-a.y));
                }
                if(!crosses&&inside){t.interior=true;break;}
                if(crosses)boundary.push_back(p);
            }
            if(t.interior)continue;
            t.first=uint32_t(next.shapes.size());size_t tileVertices=0;
            for(size_t p:boundary) {
                std::vector<Point> points,scratch;
                for(const auto&v:polygons[p].points)points.push_back({(v.x-ox)/next.grid.spacing,(v.y-oy)/next.grid.spacing});
                // Clipping a concave polygon to a convex square may create boundary bridges;
                // even/odd scanline intervals still represent its interior union exactly.
                for(unsigned plane=0;plane<4&&!points.empty();++plane) {
                    scratch.clear();
                    auto distance=[&](Point v){return plane==0?v.x:plane==1?1-v.x:plane==2?v.y:1-v.y;};
                    for(size_t i=0,j=points.size()-1;i<points.size();j=i++) {
                        if(!next.Spend())return false;
                        const Point a=points[j],b=points[i];const double da=distance(a),db=distance(b);
                        if((da>=0)!=(db>=0)) {const double u=da/(da-db);scratch.push_back({a.x+(b.x-a.x)*u,a.y+(b.y-a.y)*u});}
                        if(db>=0)scratch.push_back(b);
                        if(scratch.size()>MaxClipVertices)return false;
                    }
                    points.swap(scratch);
                }
                if(points.size()<3)continue;
                if(points.size()>MaxTileVertices-tileVertices||points.size()>MaxVertices-next.vertices.size())return false;
                next.shapes.push_back({uint32_t(next.vertices.size()),uint32_t(points.size())});
                next.vertices.insert(next.vertices.end(),points.begin(),points.end());tileVertices+=points.size();++t.count;
            }
        }
        *this=std::move(next);return true;
    }
    size_t TileCount()const{return tiles.size();}
    size_t VertexCount()const{return vertices.size();}
    size_t Work()const{return work;}
};
}
