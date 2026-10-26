/*
** The Modern water on Direct3D 11: the text its programs are made from.  The draw itself is
** DX11BackendClass's (dx11water.cpp holds that half of the class), because a water draw is one of
** the backend's draws with fifteen stages, four constant blocks and, over open sea, a displaced
** vertex half and the GPU ocean bound beside what the generators give it.
**
** Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
*/

#ifndef DX11WATER_H
#define DX11WATER_H

#include "ffvertex.h"
#include "Platform/WaterHullVisualLayout.h"

#include <string>

// The registers W3DWater sets for a Modern draw, c0 to the last hull packet.
const unsigned DX11_WATER_REGISTERS = WaterHullVisualLayout::PixelRegisterCount;
// The stages the Modern text samples, t0 to t14; the backend's own four are the first of them.
const unsigned DX11_WATER_STAGES = 15;
// The ocean field's passes: the spectrum at the time asked for, one butterfly, the final sign.
const unsigned DX11_WATER_OCEAN_PASSES = 3;

// The fixed-function vertex program for this state, wrapped so the ocean's maps displace it
// (WaterFftSampling::WrapVertex).  False for a lit description, which the water never draws with.
bool DX11Water_Ocean_Vertex_Program(const VertexPipelineDescription & description, std::string & hlsl);

// One of the ocean's pixel passes (WaterFftSpectrum.h), with the SDL GPU register spaces taken out
// for shader model 5, and the full-screen triangle every pass draws.
std::string DX11Water_Ocean_Pass_Program(unsigned pass);
const char * DX11Water_Ocean_Pass_Vertex_Program();

#endif // DX11WATER_H
