#pragma once
// Sun capture is independent of per-pixel lighting and of the renderer's API identity.
#include <string>
struct IDirect3DDevice9;
namespace SunShadow {
bool Begin(IDirect3DDevice9 *device, unsigned size);
// End restores the scene and reports whether this capture can be used.
bool End(IDirect3DDevice9 *device);
void Clear(IDirect3DDevice9 *device);
// Five floats per caster: world xyz, radius and optical depth. False retains particle blob shadows.
bool FillSmoke(IDirect3DDevice9 *device, const float *casters, unsigned count, float strength);
bool CasterEffects(IDirect3DDevice9 *device);
void SetCasterEffect(IDirect3DDevice9 *device, unsigned effect, const float parameters[4]);
// Coverage belongs to the queued draw, not to the caller's temporary device state.
// Only shadow-pass effects are captured; cosmetic scene effects keep their own path.
struct CasterEffect {
    unsigned Effect = 0;
    float Parameters[4] = {};
    bool operator==(const CasterEffect &other) const {
        if (Effect != other.Effect) return false;
        if (Effect) for (unsigned i = 0; i < 4; ++i)
            if (Parameters[i] != other.Parameters[i]) return false;
        return true;
    }
};
CasterEffect GetCasterEffect(IDirect3DDevice9 *device);
class CasterEffectScope {
    IDirect3DDevice9 *Device;
    CasterEffect Saved;
    bool Active;
public:
    CasterEffectScope(IDirect3DDevice9 *device, const CasterEffect &effect)
        : Device(device), Saved(GetCasterEffect(device)), Active(Saved.Effect || effect.Effect) {
        if (Active) SetCasterEffect(Device, effect.Effect, effect.Parameters);
    }
    ~CasterEffectScope() { if (Active) SetCasterEffect(Device, Saved.Effect, Saved.Parameters); }
    CasterEffectScope(const CasterEffectScope &) = delete;
    CasterEffectScope &operator=(const CasterEffectScope &) = delete;
};
void SetSmokeGlow(IDirect3DDevice9 *device, bool glow);
// True only when the backend can receive this completed capture.
bool Parameters(IDirect3DDevice9 *device, float bias, float strength, float widest,
    float narrowest, float texelsPerGap, float unitsPerDepth, float skyFill);
// Whether the current draw's coordinates match the captured scene.
bool ReceivesView(IDirect3DDevice9 *device);
// Scene ownership survives temporary camera-space draw state, but never another view.
bool OwnsSceneShadows(IDirect3DDevice9 *device);
bool EnterScene(IDirect3DDevice9 *device, bool eligible);
void LeaveScene(IDirect3DDevice9 *device, bool eligible);
class SceneScope {
    IDirect3DDevice9 *Device;
    bool Eligible;
public:
    SceneScope(IDirect3DDevice9 *device, bool eligible) : Device(device), Eligible(EnterScene(device, eligible)) {}
    ~SceneScope() { LeaveScene(Device, Eligible); }
    SceneScope(const SceneScope &) = delete;
    SceneScope &operator=(const SceneScope &) = delete;
};
void ExcludeReceivers(IDirect3DDevice9 *device, bool exclude);
// Retain exclusion metadata when a render queue outlives its caller's scope.
bool ReceiversExcluded(IDirect3DDevice9 *device);
class ReceiverExclusion {
    IDirect3DDevice9 *Device;
public:
    explicit ReceiverExclusion(IDirect3DDevice9 *device, bool exclude = true) : Device(exclude ? device : nullptr) { ExcludeReceivers(Device, true); }
    ~ReceiverExclusion() { ExcludeReceivers(Device, false); }
    ReceiverExclusion(const ReceiverExclusion &) = delete;
    ReceiverExclusion &operator=(const ReceiverExclusion &) = delete;
};
std::string Report(IDirect3DDevice9 *device);
}
