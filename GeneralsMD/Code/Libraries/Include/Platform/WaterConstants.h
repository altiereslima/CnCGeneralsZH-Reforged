// The Modern water's pixel constant block, built from the float4 registers W3DWater sets and the
// camera the draw is made with.  The SDL GPU device and the Direct3D 11 backend draw the same text
// (engineshader.cpp, write_modern_water).  This is the Direct3D 11 builder; field for field it is
// PosixDevice9::Build_Water_Constants, which the SDL device keeps until it can be built against it.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "Platform/ModernWaterMaterial.h"
#include "Platform/WaterFoamHistory.h"
#include "Platform/WaterHullVisualLayout.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace WaterConstants {

// The b0 block both stages read, field for field WaterFftSampling::UniformSource's cbuffer.  The
// first three are the combiner's, which the appended alpha test and fog read.
struct Block
{
	float TextureFactor[4];
	float FogColour[4];
	float AlphaReference[4];
	float WorldFromClip[16];
	float CameraPosition[4];
	float SunDirection[4];
	float SunColour[4];
	float Viewport[4];
	float ReflectionArea[4];
	float WaterParameters[4]; // animation seconds, river flag, reflection available, mirror height
	float TerrainParameters[4]; // height texture border, world size, height scale, available
	float SceneParameters[4]; // refraction available, inverse scene-target width/height, scene depth available
	float OpticalParameters[4]; // authored depth/opacity, offshore context available, flat UV0 height correction available
	float HullParameters[4]; // number of visible vessel contours, authored ambient RGB for vessel foam
	float HullData[80][4]; // eight vessels, ten float4 registers each; runtime waterline geometry
	float FftParameters[4]; // x: the dedicated vertex displacement and six sampled ocean maps are bound
	float FoamHistoryArea[4]; // world origin XY, inverse world width/height; sampled at undisplaced surface XY
	float FoamHistoryParameters[4]; // available, water level, cell world size, simulation time of atlas
	float ContactHullData[80][4]; // optional foam-only contours; count < 3 falls back to the matching mean hull
	float FoamFillLights[4][4]; // two direction/colour pairs; first direction.w enables the main light for foam
#define MODERN_WATER_CONSTANTS_UNIFORM(name) float name[4];
	MODERN_WATER_MATERIAL_UNIFORMS(MODERN_WATER_CONSTANTS_UNIFORM)
#undef MODERN_WATER_CONSTANTS_UNIFORM
};
static_assert(sizeof(Block) == WaterHullVisualLayout::VertexCommonRegisters * 16, "water uniform packing");
static_assert(offsetof(Block, MaterialColor) == 184 * 16 && offsetof(Block, MaterialParameters) == 185 * 16,
	"water material tail packing");

// The pixel half's b0: the block above and the hull meshes' metadata after it.
struct VisualBlock
{
	Block Water;
	float VisualHullMeta[WaterHullVisualLayout::MaxHulls][4];
};
static_assert(sizeof(VisualBlock) == WaterHullVisualLayout::FragmentCommonRegisters * 16,
	"fragment hull metadata packing");

// The draw's camera: view and projection in the Direct3D layout (row vectors), the viewport in
// target pixels with its depth range.
struct Camera
{
	const float *view;
	const float *projection;
	float x, y, width, height, minZ, maxZ;
};

// Visual-only inverse for reconstructing the water surface from depth. Double intermediates keep
// distant camera translations stable; this does not reach simulation or replay arithmetic.
inline bool Inverse(const float source[16], float result[16])
{
	double rows[4][8];
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 8; ++c)
			rows[r][c] = c < 4 ? source[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
	for (unsigned c = 0; c < 4; ++c) {
		unsigned pivot = c;
		for (unsigned r = c + 1; r < 4; ++r)
			if (std::fabs(rows[r][c]) > std::fabs(rows[pivot][c])) pivot = r;
		if (!std::isfinite(rows[pivot][c]) || std::fabs(rows[pivot][c]) < 1e-15) return false;
		for (unsigned k = 0; k < 8; ++k) {
			const double saved = rows[c][k]; rows[c][k] = rows[pivot][k]; rows[pivot][k] = saved;
		}
		const double scale = rows[c][c];
		for (unsigned k = 0; k < 8; ++k) rows[c][k] /= scale;
		for (unsigned r = 0; r < 4; ++r) {
			if (r == c) continue;
			const double factor = rows[r][c];
			for (unsigned k = 0; k < 8; ++k) rows[r][k] -= factor * rows[c][k];
		}
	}
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 4; ++c) result[r * 4 + c] = (float)rows[r][c + 4];
	return true;
}

inline void Multiply(const float left[16], const float right[16], float result[16])
{
	for (unsigned row = 0; row < 4; ++row)
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned index = 0; index < 4; ++index) sum += left[row * 4 + index] * right[index * 4 + column];
			result[row * 4 + column] = sum;
		}
}

// Whether c88/c89 describe a foam atlas the shader may read: history is an optional CPU-authored
// image, so a stale binding, an invalid mapping or a river draw never makes it a required input.
inline bool FoamHistoryRegisters(const float (*registers)[4])
{
	if (registers[1][1] > 0.5f || registers[89][0] <= 0.5f) return false;
	for (unsigned c = 0; c < 4; ++c)
		if (!std::isfinite(registers[88][c]) || !std::isfinite(registers[89][c])) return false;
	if (registers[88][2] <= 0 || registers[88][3] <= 0 || registers[89][2] <= 0) return false;
	const float flags = registers[89][0];
	return !(flags > 7 || std::floor(flags) != flags || (static_cast<unsigned>(flags) & 3u) == 0u);
}

// The packed format has two equal logical tiles; a malformed or stale binding must not reinterpret
// a middle row as a history/direction boundary.  Legacy atlases remain valid.
inline bool FoamHistoryAtlas(const float (*registers)[4], unsigned width, unsigned height)
{
	if (width == 0 || height == 0) return false;
	return (static_cast<unsigned>(registers[89][0]) & 4u) == 0u || (height % 2 == 0
		&& width <= WaterFoamHistory::MaxAtlasSide && height <= 2 * WaterFoamHistory::MaxAtlasSide);
}

// Everything but the combiner's three fields, which the caller's own pipeline fills.  registers is
// the WaterHullVisualLayout::PixelRegisterCount bank; history says whether the atlas at stage 14
// passed the two checks above.
inline void Fill(const float (*registers)[4], const Camera &camera, bool history, Block &water)
{
	std::memset(&water, 0, sizeof(water));
	float view_projection[16], inverse_view[16];
	Multiply(camera.view, camera.projection, view_projection);
	if (!Inverse(view_projection, water.WorldFromClip) || !Inverse(camera.view, inverse_view)) {
		std::memset(water.WorldFromClip, 0, sizeof(water.WorldFromClip));
		std::memset(inverse_view, 0, sizeof(inverse_view));
		for (unsigned i = 0; i < 4; ++i) water.WorldFromClip[i * 5] = inverse_view[i * 5] = 1.0f;
	}
	water.CameraPosition[0] = inverse_view[12];
	water.CameraPosition[1] = inverse_view[13];
	water.CameraPosition[2] = inverse_view[14];
	// Both SV_Position.z and sampled scene depth include the viewport's depth range. Compose its
	// inverse into the unprojection so custom MinZ/MaxZ use the same shader path as the normal range.
	const float depth_range = camera.maxZ - camera.minZ;
	if (depth_range > 1.0e-8f) {
		for (unsigned int column = 0; column < 4; ++column) {
			const float z_row = water.WorldFromClip[8 + column] / depth_range;
			water.WorldFromClip[12 + column] -= camera.minZ * z_row;
			water.WorldFromClip[8 + column] = z_row;
		}
	}
	water.Viewport[0] = camera.x + 0.5f;
	water.Viewport[1] = camera.y + 0.5f;
	water.Viewport[2] = camera.width;
	water.Viewport[3] = camera.height;
	std::memcpy(water.WaterParameters, registers[1], sizeof(water.WaterParameters));
	std::memcpy(water.ReflectionArea, registers[2], sizeof(water.ReflectionArea));
	std::memcpy(water.SunDirection, registers[3], sizeof(water.SunDirection));
	std::memcpy(water.SunColour, registers[4], sizeof(water.SunColour));
	std::memcpy(water.TerrainParameters, registers[5], sizeof(water.TerrainParameters));
	std::memcpy(water.OpticalParameters, registers[6], sizeof(water.OpticalParameters));
	std::memcpy(water.HullParameters, registers[7], sizeof(water.HullParameters));
	std::memcpy(water.HullData, registers[8], sizeof(water.HullData));
	std::memcpy(water.ContactHullData, registers[96], sizeof(water.ContactHullData));
	std::memcpy(water.FoamFillLights, registers[90], sizeof(water.FoamFillLights));
	std::memcpy(water.MaterialColor, registers[ModernWaterMaterial::RegisterColor], sizeof(water.MaterialColor));
	std::memcpy(water.MaterialParameters, registers[ModernWaterMaterial::RegisterParameters], sizeof(water.MaterialParameters));
	if (history) {
		std::memcpy(water.FoamHistoryArea, registers[88], sizeof(water.FoamHistoryArea));
		std::memcpy(water.FoamHistoryParameters, registers[89], sizeof(water.FoamHistoryParameters));
	}
	// River draws must never inherit a preceding ocean draw's hull field.
	if (water.WaterParameters[1] > 0.5f) water.HullParameters[0] = 0;
}

inline void FillVisual(const Block &water, const float (*registers)[4], VisualBlock &visual)
{
	visual.Water = water;
	std::memcpy(visual.VisualHullMeta, registers[WaterHullVisualLayout::MetadataRegister], sizeof(visual.VisualHullMeta));
	// River and empty hull draws must not activate a preceding ocean's packet metadata.
	if (water.HullParameters[0] <= 0) std::memset(visual.VisualHullMeta, 0, sizeof(visual.VisualHullMeta));
}

} // namespace WaterConstants
