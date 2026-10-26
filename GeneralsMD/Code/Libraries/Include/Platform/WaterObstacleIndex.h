/* Render-only broad phase. Exact shore/structure damping remains with the caller. */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace WaterObstacleIndex {
struct Influence { float x,y,radius; };
struct Bounds { float minX,minY,maxX,maxY; };
struct Range { const uint32_t *indices=nullptr;size_t count=0;bool indexed=false; };
class Grid {
public:
    static constexpr double CellSize=64.0;
    static constexpr size_t MaxCells=16384,MaxReferences=262144,MaxObstacles=65536;
    void Clear() { active=false;offsets.clear();references.clear(); }
    bool Build(const std::vector<Influence> &obstacles,Bounds bounds) {
        Clear();
        int lastX,lastY;
        if (obstacles.size()>MaxObstacles || bounds.minX>bounds.maxX || bounds.minY>bounds.maxY ||
            !Cell(bounds.minX,firstX) || !Cell(bounds.minY,firstY) || !Cell(bounds.maxX,lastX) || !Cell(bounds.maxY,lastY)) return false;
        const int64_t width64=int64_t(lastX)-firstX+1,height64=int64_t(lastY)-firstY+1;
        if (width64<=0 || height64<=0 || width64>int64_t(MaxCells) || height64>int64_t(MaxCells) || width64*height64>int64_t(MaxCells)) return false;
        width=int(width64);height=int(height64);
        struct Span { int x0,y0,x1,y1; };
        std::vector<Span> spans;spans.reserve(obstacles.size());
        std::vector<uint32_t> counts(size_t(width)*height,0);
        size_t total=0;
        for (const Influence &o:obstacles) {
            // The exact narrow phase rejects outside float(radius+10), including its rounding.
            // Outward nextafter makes points exactly on a cell boundary conservative too.
            const float reach=o.radius+10.0f;
            if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.radius) || o.radius<0 || !std::isfinite(reach)) return false;
            int x0,y0,x1,y1;
            if (!Cell(std::nextafter(double(o.x)-reach,-std::numeric_limits<double>::infinity()),x0) ||
                !Cell(std::nextafter(double(o.y)-reach,-std::numeric_limits<double>::infinity()),y0) ||
                !Cell(std::nextafter(double(o.x)+reach,std::numeric_limits<double>::infinity()),x1) ||
                !Cell(std::nextafter(double(o.y)+reach,std::numeric_limits<double>::infinity()),y1)) return false;
            x0=(std::max)(x0,firstX);y0=(std::max)(y0,firstY);
            x1=(std::min)(x1,lastX);y1=(std::min)(y1,lastY);
            const size_t entries=x0<=x1 && y0<=y1 ? size_t(int64_t(x1)-x0+1)*size_t(int64_t(y1)-y0+1):0;
            if (entries>MaxReferences-total) return false;
            total+=entries;spans.push_back({x0,y0,x1,y1});
            if (entries) for (int y=y0;;++y) {
                for (int x=x0;;++x) { ++counts[size_t(y-firstY)*width+(x-firstX)];if (x==x1) break; }
                if (y==y1) break;
            }
        }
        offsets.resize(counts.size()+1);offsets[0]=0;
        for (size_t i=0;i<counts.size();++i) offsets[i+1]=offsets[i]+counts[i];
        references.resize(total);std::vector<uint32_t> cursor(offsets.begin(),offsets.end()-1);
        // Iterating original indices preserves exact caller order within every bin.
        for (size_t i=0;i<spans.size();++i) {
            const Span &s=spans[i];if (s.x0>s.x1 || s.y0>s.y1) continue;
            for (int y=s.y0;;++y) {
                for (int x=s.x0;;++x) { references[cursor[size_t(y-firstY)*width+(x-firstX)]++]=uint32_t(i);if (x==s.x1) break; }
                if (y==s.y1) break;
            }
        }
        active=true;return true;
    }
    Range Query(float x,float y) const {
        int cx,cy;
        if (!active || !Cell(x,cx) || !Cell(y,cy)) return {};
        const int64_t ix=int64_t(cx)-firstX,iy=int64_t(cy)-firstY;
        if (ix<0 || iy<0 || ix>=width || iy>=height) return {};
        const size_t n=size_t(iy)*width+size_t(ix),count=offsets[n+1]-offsets[n];
        return {count?references.data()+offsets[n]:nullptr,count,true};
    }
    size_t ReferenceCount() const { return references.size(); }
    size_t CellCount() const { return offsets.empty()?0:offsets.size()-1; }
private:
    static bool Cell(double coordinate,int &cell) {
        if (!std::isfinite(coordinate)) return false;
        const double value=std::floor(coordinate/CellSize);
        if (value<double((std::numeric_limits<int>::min)()) || value>double((std::numeric_limits<int>::max)())) return false;
        cell=int(value);return true;
    }
    bool active=false;
    int firstX=0,firstY=0,width=0,height=0;
    std::vector<uint32_t> offsets,references;
};
}
