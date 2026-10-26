/* Reuse hull slices only while their source mesh poses remain geometrically close. */
#pragma once
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace WaterHullPoseCache {
constexpr unsigned MaxMeshes=128;
// This bounds mesh-vertex movement, NOT slice-endpoint movement: a nearly tangent
// triangle can have an ill-conditioned plane intersection even at the exact pose.
constexpr double MaxVertexMovement=0.01;
struct Mesh {
    const void *identity=nullptr,*vertexArray=nullptr,*triangleArray=nullptr;
    unsigned vertexCount=0,triangleCount=0;
    bool crossesMean=false;
    double supportRadius=0;
    float transform[3][4] = {}; // model-local vertex -> horizontal vessel frame/water-relative Z
};

inline double SupportRadius(const float center[3],const float extent[3]) {
    double squared=0;
    for(unsigned i=0;i<3;++i) {
        if(!std::isfinite(center[i]) || !std::isfinite(extent[i]) || extent[i]<0)
            return std::numeric_limits<double>::infinity();
        // Include the offset of the AABB from the mesh origin. Get_Bounding_Box reconstructs
        // float center/extent from min/max; pad its rounding conservatively before the norm.
        const double reach=(std::abs(double(center[i]))+double(extent[i]))*
            (1.0+4.0*std::numeric_limits<float>::epsilon());
        squared+=reach*reach;
    }
    return std::nextafter(std::sqrt(squared),std::numeric_limits<double>::infinity());
}
inline bool Valid(const Mesh &mesh) {
    if(!mesh.identity || !mesh.vertexArray || !mesh.triangleArray || !mesh.vertexCount ||
        !mesh.triangleCount || !std::isfinite(mesh.supportRadius) || mesh.supportRadius<0) return false;
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<4;++j)
        if(!std::isfinite(mesh.transform[i][j])) return false;
    return true;
}
inline bool SameGeometry(const Mesh &a,const Mesh &b) {
    return a.identity==b.identity && a.vertexArray==b.vertexArray && a.triangleArray==b.triangleArray &&
        a.vertexCount==b.vertexCount && a.triangleCount==b.triangleCount &&
        a.crossesMean==b.crossesMean && a.supportRadius==b.supportRadius;
}
inline double VertexMovementBound(const Mesh &sliced,const Mesh &current) {
    if(!Valid(sliced) || !Valid(current)) return std::numeric_limits<double>::infinity();
    double linearSquared=0,translationSquared=0;
    for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;j<3;++j) {
            const double delta=double(current.transform[i][j])-sliced.transform[i][j];
            linearSquared+=delta*delta;
        }
        const double delta=double(current.transform[i][3])-sliced.transform[i][3];
        translationSquared+=delta*delta;
    }
    if(linearSquared==0 && translationSquared==0) return 0; // common stationary/pause case
    const double radius=(sliced.supportRadius>current.supportRadius?sliced.supportRadius:current.supportRadius);
    // |(A-A0)v+(t-t0)| <= ||A-A0||F*R + |t-t0|, for every mesh vertex |v|<=R.
    return std::nextafter(std::sqrt(linearSquared)*radius+std::sqrt(translationSquared),
        std::numeric_limits<double>::infinity());
}
class Cache {
    std::vector<Mesh> sliced;
    double slicedCrest=0;
    bool initialized=false;
public:
    template<class T> bool Reusable(const std::vector<T> &current,double crest,
        double maximumMovement=MaxVertexMovement) const {
        if(!initialized || current.size()>MaxMeshes || current.size()!=sliced.size() ||
            !std::isfinite(crest) || crest<0 || !std::isfinite(maximumMovement) || maximumMovement<0) return false;
        const double crestMovement=std::abs(crest-slicedCrest);
        if(crestMovement>maximumMovement) return false;
        for(std::size_t i=0;i<current.size();++i) {
            if(!SameGeometry(sliced[i],current[i]) ||
                VertexMovementBound(sliced[i],current[i])+crestMovement>maximumMovement) return false;
        }
        return true;
    }
    template<class T> bool Store(const std::vector<T> &current,double crest) {
        if(current.size()>MaxMeshes || !std::isfinite(crest) || crest<0) { Clear();return false; }
        for(const Mesh &mesh:current) if(!Valid(mesh)) { Clear();return false; }
        // Only called after slicing, never on a hit: small pose changes accumulate against
        // this snapshot. Explicit reserve keeps capacity bounded rather than geometric growth.
        if(sliced.capacity()<current.size()) sliced.reserve(current.size());
        sliced.resize(current.size());
        for(std::size_t i=0;i<current.size();++i) sliced[i]=current[i];
        slicedCrest=crest;initialized=true;return true;
    }
    void Clear() { sliced.clear();slicedCrest=0;initialized=false; }
    std::size_t Size() const { return sliced.size(); }
};
}
