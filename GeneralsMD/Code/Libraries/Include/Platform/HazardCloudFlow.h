// Backend-neutral persistent planar smoke transport for hazard clouds.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

namespace HazardCloudFlow {

// World coordinates and world units per second. The caller supplies the current
// velocity, not displacement since the last rendered frame. Body order need not
// be stable. Z and height retain body geometry; the flow simulation is planar.
struct Body {
    float x, y, z;
    float vx, vy;
    float radius, height;
};

// Each field owns independent grids and integration history. No graphics device
// or game-state dependency is needed to create, advance, or destroy a flow.
class Flow {
public:
    Flow();
    ~Flow();
    Flow(const Flow &) = delete;
    Flow &operator=(const Flow &) = delete;

    // Covers the texture edges, with cells sampled at their centres. Setting a
    // domain resets all flow. reset() preserves the domain and clears history.
    void setDomain(float minX, float minY, float maxX, float maxY);
    void reset();

    // Fixed 1/60-second steps with a capped catch-up interval. Empty input heals
    // the existing wake; zero delta freezes it. Invalid bodies are ignored and
    // reported by valid(); infantry velocities above the limit are warps.
    // At most the first 32 supplied bodies are considered.
    void step(float deltaSeconds, const Body *bodies, unsigned count);

    unsigned width() const;
    unsigned height() const;
    float domainMinX() const;
    float domainMinY() const;
    float domainMaxX() const;
    float domainMaxY() const;

    // Row-major RGBA32F, X varies fastest. R/G contain accumulated world
    // displacement; sample smoke noise at world.xy - RG. B/A stay zero so flow
    // does not carve holes or rings. The shader applies height falloff. Values
    // stay valid until this instance's next step/reset/setDomain or destruction.
    // Displacement magnitude is bounded to 18 units.
    const float *data() const;

    // Render diagnostics only, never authoritative gameplay state.
    bool valid() const;
    float maxDisturbance() const; // Displacement magnitude divided by 18 units.
    unsigned activeBodies() const;

private:
    struct Impl;
    Impl *impl;
};

} // namespace HazardCloudFlow
