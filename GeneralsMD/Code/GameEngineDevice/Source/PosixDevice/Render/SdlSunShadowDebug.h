#pragma once
#include <cstring>

// Fault validation only, at resource/program/pipeline creation, never the cached
// draw path. Only tests name a point; the game never does.
inline const char *&Sdl_Sun_Shadow_Fault_Point()
{
    static const char *point = nullptr;
    return point;
}

inline bool Sdl_Sun_Shadow_Fault(const char *point)
{
    const char *requested = Sdl_Sun_Shadow_Fault_Point();
    return requested && std::strcmp(requested, point) == 0;
}
