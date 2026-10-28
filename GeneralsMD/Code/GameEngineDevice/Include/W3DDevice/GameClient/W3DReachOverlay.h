// The ground a defence can hit (GameClient/ReachOverlay.h), drawn on the terrain.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __W3DREACHOVERLAY_H__
#define __W3DREACHOVERLAY_H__

#include "Lib/BaseType.h"
#include <vector>

class CameraClass;
struct ReachOverlayField;

/** The look, in world units and alphas, read by both renderers.  The Direct3D 11 program takes them
	* through its constants, so tuning one needs no new program in the shader cache. */
namespace ReachLook
{
	const Real BorderWidth = 1.5f;		///< the line on the inside of the edge
	const Real BandWidth = 10.0f;			///< the stronger wash just inside the edge fades out over this
	const Real FillAlpha = 0.10f;			///< the whole area
	const Real BandAlpha = 0.14f;			///< added at the edge, nought BandWidth in
	const Real LineAlpha = 0.70f;
	const Real LineWhite = 0.35f;			///< how far the line goes from the owner's colour toward white (Direct3D 11)
}

/** This frame's fields on the ground.  Called from the terrain pass once the ground, the roads, the
	* bridges and the tracks are down and before the shroud and any model: the shroud darkens it with
	* the ground, every unit and building stands on it, and the interface is drawn long after. */
void W3DReachOverlay_Draw( CameraClass &camera );

/** Lets go of every texture and Direct3D 11 object the overlay holds, for the terrain going away or a
	* device reset; the next draw makes them again. */
void W3DReachOverlay_Release( void );

/** The Direct3D 11 pass (DX11ReachOverlay.cpp, Windows only).  False when it cannot draw at all, and
	* the fixed-function path draws instead.  Makes its program on the first terrain frame, fields or
	* not, so the first defence selected does not pay for the compile. */
Bool DX11ReachOverlay_Draw( CameraClass &camera, const std::vector< const ReachOverlayField * > &fields );
void DX11ReachOverlay_Release( void );

#endif
