/* Render-only vessel motion. No gameplay position, velocity or water level is modified. */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace WaterVesselMotion {
constexpr float MaxSpeed=250.0f;
constexpr float SmoothingSeconds=0.15f;
constexpr float MaxHistorySeconds=0.5f;
constexpr float MinimumTeleportDistance=32.0f;
struct Point { float x=0,y=0; };
struct State {
    Point position,velocity;
    float level=0,phase=1;
    std::uint32_t frame=0;
    bool initialized=false;
};
inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
inline Point LimitVelocity(Point velocity) {
    if (!Finite(velocity)) return {};
    const double speed=std::sqrt(double(velocity.x)*velocity.x+double(velocity.y)*velocity.y);
    if (speed>MaxSpeed) {
        const double scale=MaxSpeed/speed;
        velocity.x=float(velocity.x*scale);velocity.y=float(velocity.y*scale);
    }
    return velocity;
}
inline void Anchor(State &state,Point position,float level,std::uint32_t frame,Point velocity={},float phase=1) {
    state.position=position;state.level=level;state.frame=frame;state.phase=phase;
    state.velocity=LimitVelocity(velocity);state.initialized=true;
}

// The displayed transform is authoritative after the first observation, including scripted
// movement and braking. Sample once per simulation frame, so extra renders/reflections and
// pause do not create artificial acceleration. Callers own one state per stable model/object ID.
// An interpolated transform represents frame-1+phase; an unblended transform uses phase=1.
// First-render phase varies with render cadence and must be included in the elapsed sample time.
inline Point Update(State &state,Point position,float level,std::uint32_t frame,
                    bool frozen,Point initialPhysicsVelocity,float framesPerSecond,float samplePhase=1) {
    if (!Finite(position) || !std::isfinite(level) || !std::isfinite(framesPerSecond) || framesPerSecond<=0 ||
        !std::isfinite(samplePhase)) {
        state=State();return {};
    }
    samplePhase=(std::max)(0.0f,(std::min)(1.0f,samplePhase));
    if (!state.initialized) {
        Anchor(state,position,level,frame,initialPhysicsVelocity,samplePhase);return state.velocity;
    }
    if (level!=state.level || frame<state.frame) {
        Anchor(state,position,level,frame,{},samplePhase);return state.velocity;
    }
    if (frozen || frame==state.frame) return state.velocity;
    const double seconds=(double(frame-state.frame)+double(samplePhase)-state.phase)/framesPerSecond;
    const double dx=double(position.x)-state.position.x,dy=double(position.y)-state.position.y;
    const double distance=std::sqrt(dx*dx+dy*dy);
    // A fully displayed tick followed by alpha=0 has the same sample time. Preserve its
    // velocity only if the transform also agrees; a changed transform is a discontinuity.
    if (seconds<=0) {
        Anchor(state,position,level,frame,distance==0?state.velocity:Point{},samplePhase);
        return state.velocity;
    }
    const double teleportDistance=(std::max)(double(MinimumTeleportDistance),double(MaxSpeed)*seconds);
    if (distance>teleportDistance) {
        Anchor(state,position,level,frame,{},samplePhase);return state.velocity;
    }
    if (seconds>MaxHistorySeconds) {
        // Visibility can leave a cached hull unobserved. Its old displacement is no longer
        // a useful velocity estimate, but finite current physics still gives a safe seed.
        Anchor(state,position,level,frame,initialPhysicsVelocity,samplePhase);return state.velocity;
    }
    const Point measured=LimitVelocity({float(dx/seconds),float(dy/seconds)});
    const float blend=float(1.0-std::exp(-seconds/SmoothingSeconds));
    const Point velocity=LimitVelocity({state.velocity.x+(measured.x-state.velocity.x)*blend,
        state.velocity.y+(measured.y-state.velocity.y)*blend});
    Anchor(state,position,level,frame,velocity,samplePhase);
    return state.velocity;
}
}
