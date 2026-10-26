#pragma once

#include "Platform/WaterTerrainCulling.h"
#include <limits>

namespace WaterTerrainCullingTest
{
using WaterProjectedMesh::Mesh;
using WaterProjectedMesh::Point;

inline Mesh Triangle(Point a, Point b, Point c)
{
    Mesh mesh;
    for (Point point : {a, b, c})
    {
        WaterProjectedMesh::Vertex vertex;
        vertex.world = point;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2};
    return mesh;
}

inline bool Cull(Mesh &mesh, const std::vector<uint8_t> &heights,
                 double vertical = 0, double horizontal = 0)
{
    return WaterTerrainCulling::CullDryTriangles(
        mesh, heights.data(), 9, 9, 0, 1, 1, 10, vertical, horizontal);
}

inline void Check()
{
    std::vector<uint8_t> heights(81, 30);
    const auto original = Triangle({1.2, 1.2}, {6.8, 1.2}, {1.2, 6.8});
    auto dry = original;
    CHECK(Cull(dry, heights));
    CHECK(dry.indices.empty() && dry.vertices.empty());

    // Every triangle endpoint is high; the interior grid corner contains a small pond.
    heights[3 * 9 + 3] = 0;
    auto pond = original;
    CHECK(Cull(pond, heights));
    CHECK(pond.indices == original.indices && pond.vertices.size() == 3);

    // A wet corner outside the triangle but inside a crossed terrain cell must also retain it.
    heights.assign(81, 30);
    heights[2 * 9 + 2] = 0;
    auto cell = Triangle({2.2, 2.2}, {2.8, 2.2}, {2.2, 2.8});
    CHECK(Cull(cell, heights));
    CHECK(cell.indices.size() == 3);

    heights.assign(81, 30);
    heights[2 * 9 + 4] = 0;
    const auto shifted = Triangle({2.2, 2.2}, {2.8, 2.2}, {2.2, 2.8});
    auto still = shifted;
    CHECK(Cull(still, heights));
    CHECK(still.indices.empty());

    auto displaced = shifted;
    CHECK(Cull(displaced, heights, 0, 1));
    CHECK(displaced.indices.size() == 3);

    heights.assign(81, 11);
    auto crest = shifted;
    CHECK(Cull(crest, heights, 1));
    CHECK(crest.indices.size() == 3);

    auto below = shifted;
    CHECK(Cull(below, heights, .5));
    CHECK(below.indices.empty());

    heights.assign(81, 30);
    auto outside = Triangle({-2, 2}, {-1, 2}, {-2, 3});
    CHECK(Cull(outside, heights));
    CHECK(outside.indices.size() == 3); // no assumption about terrain beyond the map

    // Remove an unused vertex and a buried triangle while preserving winding and metadata.
    Mesh mixed = original;
    for (const auto &vertex : shifted.vertices)
        mixed.vertices.push_back(vertex);
    mixed.indices = {3, 5, 4, 0, 1, 2};
    heights[2 * 9 + 2] = 0;

    // Move the first triangle into a separate dry region.
    mixed.vertices[0].world = {5.1, 5.1};
    mixed.vertices[1].world = {6.1, 5.1};
    mixed.vertices[2].world = {5.1, 6.1};
    mixed.vertices[3].boundary = 2;
    mixed.vertices[3].knot = 7;
    mixed.vertices[3].edgeParameter = .25;
    mixed.vertices[4].collar = 0;
    mixed.vertices[5].transition = 0;
    mixed.vertices[5].lattice = 42;
    mixed.collars.resize(1);
    mixed.collars[0].fade = .5;
    mixed.transitions.resize(1);
    mixed.transitions[0].analytic = .75;
    mixed.cells = 12;
    mixed.vertices.push_back(mixed.vertices[0]);
    CHECK(Cull(mixed, heights));
    CHECK(mixed.vertices.size() == 3 && mixed.indices == std::vector<uint16_t>({0, 2, 1}));
    CHECK(mixed.vertices[0].boundary == 2 && mixed.vertices[0].knot == 7 &&
          mixed.vertices[0].edgeParameter == .25);
    CHECK(mixed.vertices[1].collar == 0 && mixed.vertices[2].transition == 0 &&
          mixed.vertices[2].lattice == 42);
    CHECK(mixed.collars.size() == 1 && mixed.collars[0].fade == .5 &&
          mixed.transitions.size() == 1 && mixed.transitions[0].analytic == .75 &&
          mixed.cells == 12);

    mixed.vertices.push_back(mixed.vertices[0]);
    CHECK(WaterTerrainCulling::CompactVertices(mixed));
    CHECK(mixed.vertices.size() == 3);

    // A single bad later triangle cannot commit removal of an earlier valid triangle.
    auto invalid = original;
    invalid.indices.insert(invalid.indices.end(), {0, 1, 99});
    const auto badIndices = invalid.indices;
    CHECK(!Cull(invalid, heights));
    CHECK(invalid.indices == badIndices && invalid.vertices.size() == 3);
    CHECK(!WaterTerrainCulling::CompactVertices(invalid));
    CHECK(invalid.indices == badIndices);

    invalid = original;
    invalid.vertices[2].world.x = std::numeric_limits<double>::quiet_NaN();
    CHECK(!Cull(invalid, heights));
    CHECK(invalid.indices == original.indices && invalid.vertices.size() == 3);

    invalid = original;
    CHECK(!Cull(invalid, heights, -1));
    CHECK(invalid.indices == original.indices);
    CHECK(!WaterTerrainCulling::CullDryTriangles(invalid, nullptr, 9, 9, 0, 1, 1, 10, 0, 0));
    CHECK(!WaterTerrainCulling::CullDryTriangles(
        invalid, heights.data(), 9, 9, 0, 0, 1, 10, 0, 0));
    CHECK(!WaterTerrainCulling::CullDryTriangles(
        invalid, heights.data(), 9, 9, 5, 1, 1, 10, 0, 0));
    CHECK(invalid.indices == original.indices && invalid.vertices.size() == 3);

    // Very broad triangles retain their original coverage when a bounded query declines them.
    std::vector<uint8_t> large(65 * 65, 30);
    auto broad = Triangle({1, 1}, {63, 1}, {1, 63});
    CHECK(WaterTerrainCulling::CullDryTriangles(
        broad, large.data(), 65, 65, 0, 1, 1, 10, 0, 0));
    CHECK(broad.indices.size() == 3);

    // Map border offset, terrain spacing and encoded height scale participate in the proof.
    auto scaled = Triangle({-.4, -.4}, {.4, -.4}, {-.4, .4});
    CHECK(WaterTerrainCulling::CullDryTriangles(
        scaled, heights.data(), 9, 9, 2, 2, 2, 40, 0, 0));
    CHECK(scaled.indices.size() == 3); // low sample at encoded (2,2), world (0,0)

    // A camera above a bilinear saddle can still be below one terrain triangulation.
    heights.assign(81, 0);
    heights[2 * 9 + 3] = 20;
    heights[3 * 9 + 2] = 20;
    CHECK(!WaterTerrainCulling::CameraAboveTerrain(
        heights.data(), 9, 9, 0, 1, 1, 2.5, 2.5, 15));
    CHECK(!WaterTerrainCulling::CameraAboveTerrain(
        heights.data(), 9, 9, 0, 1, 1, 2.5, 2.5, 20));
    CHECK(WaterTerrainCulling::CameraAboveTerrain(
        heights.data(), 9, 9, 0, 1, 1, 2.5, 2.5, 21));
    CHECK(!WaterTerrainCulling::CameraAboveTerrain(
        heights.data(), 9, 9, 0, 1, 1, -.1, 2.5, 30));
    CHECK(!WaterTerrainCulling::CameraAboveTerrain(
        heights.data(), 9, 9, 0, 1, 1, 8.1, 2.5, 30));
    CHECK(!WaterTerrainCulling::CameraAboveTerrain(
        heights.data(), 9, 9, 0, 1, 1, 2.5, 2.5, std::numeric_limits<double>::quiet_NaN()));
    CHECK(!WaterTerrainCulling::CameraAboveTerrain(nullptr, 9, 9, 0, 1, 1, 2.5, 2.5, 30));
    CHECK(!WaterTerrainCulling::CameraAboveTerrain(
        heights.data(), 9, 9, 0, 0, 1, 2.5, 2.5, 30));
    CHECK(WaterTerrainCulling::CameraAboveTerrain(
        heights.data(), 9, 9, 2, 2, 2, 1, 1, 41));
}
}
