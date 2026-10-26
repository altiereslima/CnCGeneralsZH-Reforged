// Backend-neutral visual-only hazard cloud renderer.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "Lib/BaseType.h"
#include "Platform/HazardCloudFlow.h"
#include <cstdint>
#include <memory>

namespace HazardCloud {

constexpr unsigned TerrainGridSide = 129;

struct FieldState {
    Coord3D ground;
    float radius;
    float height;       // Full world-space height above the sampled ground.
    float density;
    float palette;      // 0 toxin, 1 radiation, 2 anthrax blue, 3 gamma purple.
    float formation;
    float release;
    float fringe;
};
using Body = HazardCloudFlow::Body;
struct Color { float r, g, b; };
struct Palette {
    Color main, shadow;
    float brightness;
    // 0 retains full optical density; 1 makes only the smoke invisible.
    // Formation, footprint, wakes, heat and embers remain independent.
    float transparency = 0.0f;
};

// Stable user-approved palette, independent of preview files or game options.
const Palette &defaultPalette(unsigned index);

// The scene camera a cloud is drawn from, handed over by the caller rather than
// read back from the device, whose transforms can be the last draw's until the
// next state flush. Matrices are row-major in the Direct3D layout (row vectors,
// world * view * projection); the viewport is in target pixels.
struct Camera {
    float view[16];
    float projection[16];
    unsigned x, y, width, height;
    float minZ, maxZ;
};

// Main render thread only. Owns up to two fields with independent density,
// lighting and infantry-wake history. Pipelines and transient scene targets are
// shared by all layers on the same device. No scene object is registered.
// Without a terrain grid, ground support is the field's flat ground/deck plane.
// The support model has one height per XY position. The production adapter uses
// terrain samples or an elevated plane; cliffs and crossing decks have no
// dedicated validation fixture or automatic geometry fallback yet.
class Layer {
public:
    Layer() = default;
    virtual ~Layer() = default;
    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    virtual bool initialize() = 0;
    virtual void setFields(const FieldState *fields, unsigned count) = 0;
    virtual void setBodies(const Body *bodies, unsigned count) = 0; // At most 32.
    virtual void setFlowDomain(float minX, float minY, float maxX, float maxY) = 0;
    // Copies TerrainGridSide squared world heights, row-major with increasing Y.
    // Grid nodes span ground.xy +/- (radius + fringe + 18 world units), matching
    // this field's cache domain. Refresh after changing that domain or terrain.
    // nullptr disables terrain sampling for this field and restores its plane.
    virtual void setTerrain(unsigned fieldIndex, const float *heights) = 0;
    virtual void setPalette(unsigned index, const Palette &palette) = 0;
    virtual void setInteractionEnabled(bool enabled) = 0;
    virtual bool isInteractionEnabled() const = 0;
    virtual void setHeatEnabled(bool enabled) = 0;
    virtual bool isHeatEnabled() const = 0;
    virtual void update(float timeSeconds) = 0;
    virtual void reset() = 0; // Clears source/wake history, retaining fields and allocations.

    // Call only in the main scene after opaque geometry/water. False means no
    // cloud scene draw was recorded, so the caller may draw classic effects.
    virtual bool render(const Camera &camera) = 0;
    virtual bool valid() const = 0;
    virtual unsigned activeBodies() const = 0;
    virtual float maxDisturbance() const = 0;
    virtual unsigned interactionUploads() const = 0;
    virtual unsigned renderedFrames() const = 0;

    // Density + lighting atlas allocations and local flow texture. Excludes
    // shared screen scratch targets; estimate before admitting a new layer.
    static std::uint64_t estimateCacheBytes(const FieldState *fields, unsigned count);
    virtual std::uint64_t estimatedCacheBytes() const = 0;

    // Release before the display/device is destroyed. Repeated calls are safe;
    // releasing one layer leaves all other layers' resources and history intact.
    virtual void shutdown() = 0;
};

// A renderer backend (SDL GPU, Direct3D 11) implements Layer and these entry
// points. Supported() queries the current device at call time; false includes
// calls before a usable graphics device exists. CreateLayer() returns nullptr
// when unsupported; initialize() performs resource allocation.
bool Supported();
std::unique_ptr<Layer> CreateLayer();
// The programs and targets every layer shares outlive the layers, so a new map's
// first hazard compiles nothing. Release them only when the device goes away
// (teardown, reset, resolution change), after every layer has been shut down.
void ReleaseDevice();

} // namespace HazardCloud
