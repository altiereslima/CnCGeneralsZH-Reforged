// The ground a defence can hit, as the renderer draws it: a signed distance field on a world
// aligned grid for each defence shown this frame.  GameClient fills it (ReachOverlay.cpp), the
// device draws it on the terrain.  Nothing here goes back into the logic.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __REACHOVERLAY_H__
#define __REACHOVERLAY_H__

#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include <vector>

class Object;
class ThingTemplate;

/** One defence's hittable ground.  Sample (i, j) sits at world (originX + i * step, originY + j * step).
	* distance[ j * width + i ] is in world units: positive inside the ground the defence can hit, negative
	* outside, nought on the edge, clamped to +-distanceLimit, and smoothed so its edge has no grid steps.
	* The edge drawn where the field taken bilinearly crosses nought lies only in grid cells whose four
	* corners the logic's own tests passed (ReachOverlay.cpp says how), so the renderer must not grow the
	* area: draw nothing where the filtered distance is at or under nought.
	*
	* hardness, on the same grid, says what the nearest edge is: 1 where it is the end of the guns' reach
	* (a range ring, drawn as a line), 0 where something blocks it (a cliff, a building, the fog), which
	* fades in softly instead.  Taken bilinearly too. */
struct ReachOverlayField
{
	Real originX;
	Real originY;
	Real step;
	Int width;
	Int height;
	Real distanceLimit;
	std::vector< Real > distance;
	std::vector< Real > hardness;
	Color color;							///< the owner's colour, alpha ignored
	Real opacity;							///< 0..1, the whole area's strength this frame
	UnsignedInt version;			///< changes whenever distance, hardness or the grid changes, so a texture can be kept
};

/** The fields to draw this frame, in no particular order.  Empty when nothing is shown, and always
	* empty headless.  The pointers stay valid until the next GameClient update. */
void ReachOverlay_getFields( std::vector< const ReachOverlayField * > &out );

/** Work out this frame's fields: the selected defences of the local player, an ally or (to an
	* observer) anyone, or while a defence is on the placement cursor that one alone.  Nothing else is
	* ever drawn.  InGameUI::update calls it once a frame. */
void ReachOverlay_update( void );

/** Sample a standing defence's field, and its template's on the cursor at the same spot, against
	* the logic's own acquisition and fire tests with the probe Object standing on each sample point,
	* and write the mismatch counts as HEADLESS REACHPROOF lines.  Moves the probe and runs the
	* partition: a scenario's reachproof line only.  TRUE when no lit point was refused. */
Bool ReachOverlay_prove( Object *defence, Object *probe, Int samples );

#endif
