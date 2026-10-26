/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

// Render-only, tileable material data. No external assets, global RNG state, or map mutations.
// R: sand grain/ridges, G: rounded pebble colour, B: pebble relief, A: broad rock variation.
// These are material signals, not a replacement albedo: the shader retains the map's terrain tint
// and selects sand/gravel coverage from the water setting and the actual submerged terrain slope.
namespace WaterBedMaterial {
static constexpr float TileWorldSize = 48.0f;

inline uint32_t Hash(int x, int y, uint32_t seed)
{
    uint32_t n = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ seed;
    n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
    return n;
}
inline float Unit(uint32_t n) { return float(n >> 8) / 16777216.0f; }
inline float Saturate(float v) { return (std::max)(0.0f, (std::min)(1.0f, v)); }
inline float Smooth(float v) { v = Saturate(v); return v*v*(3.0f-2.0f*v); }
inline int Wrap(int n, int period) { return (n % period + period) % period; }

inline uint32_t Texel(int x, int y, int side)
{
    if (side <= 0) return 0;
    x = Wrap(x, side); y = Wrap(y, side);
    const float u = (x + 0.5f) / side, v = (y + 0.5f) / side;
    constexpr float tau = 6.28318530718f;
    const float grain = Unit(Hash(x, y, 0x81a3u));
    const float ridges = std::sin(tau * (v*15.0f + 0.22f*std::sin(tau*u*3.0f)
        + 0.08f*std::sin(tau*(v*2.0f+u))));
    const float sand = 0.55f + 0.10f*ridges + 0.12f*(grain-0.5f);

    constexpr int cells = 24;
    const float px = u*cells, py = v*cells;
    const int ix = int(std::floor(px)), iy = int(std::floor(py));
    float relief = 0.0f, pebble = sand*0.72f;
    for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) {
        const uint32_t hash = Hash(Wrap(ix+i, cells), Wrap(iy+j, cells), 0x93467u);
        const float cx = ix+i+0.2f+0.6f*Unit(hash);
        const float cy = iy+j+0.2f+0.6f*Unit(Hash(int(hash), 7, 91));
        const float radius = 0.25f+0.20f*Unit(Hash(int(hash), 11, 13));
        const float dx = (px-cx)/radius, dy = (py-cy)/(radius*0.78f);
        const float dome = std::sqrt(Saturate(1.0f-dx*dx-dy*dy));
        if (dome > relief) {
            relief = dome;
            const float colour = 0.30f+0.47f*Unit(Hash(int(hash), 23, 1));
            pebble = colour*(0.56f+0.44f*dome) + 0.06f*(grain-0.5f);
        }
    }
    const float rock = Saturate(0.5f + 0.23f*std::sin(tau*(u*3.0f+v*2.0f))
        + 0.16f*std::sin(tau*(u*7.0f-v*5.0f)) + 0.08f*std::cos(tau*(u*11.0f+v*13.0f)));
    auto byte = [](float value) { return uint32_t(Saturate(value)*255.0f+0.5f); };
    return (byte(rock)<<24) | (byte(sand)<<16) | (byte(pebble)<<8) | byte(relief);
}
}
