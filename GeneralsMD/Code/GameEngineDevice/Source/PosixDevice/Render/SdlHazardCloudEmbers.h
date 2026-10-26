// Small, procedural burning particles for the isolated cloud preview only.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

class SdlGpuFrame;
struct SdlTarget;
struct SDL_GPUTexture;
namespace HazardCloud { struct FieldState; }

namespace SdlHazardCloudEmbers {

// Record after the cloud composite, before a heat-distortion scene capture.
// The view/projection is the engine's row-major world-to-clip matrix; viewport
// contains x, y, width, height, min depth and max depth in native GPU form.
// Resources are created only when an active nuclear field first needs them.
bool prepare(SdlGpuFrame *frame, const SdlTarget &target,
             const HazardCloud::FieldState *fields, unsigned fieldCount);

bool render(SdlGpuFrame *frame, const SdlTarget &target, SDL_GPUTexture *sceneDepth,
            const float viewProjection[16], const float viewport[6],
            const HazardCloud::FieldState *fields, unsigned fieldCount,
            float timeSeconds, SDL_GPUTexture *terrainTexture = nullptr,
            const bool *terrainEnabled = nullptr);
bool validationPassed();
// Call after flushing and waiting for the GPU, before destroying its device.
void shutdown();
// Forget handles only when their owning device was already destroyed.
void abandon();

}
