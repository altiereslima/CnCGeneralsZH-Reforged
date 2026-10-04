/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// W3DSmoothMotion.h //////////////////////////////////////////////////////////////////////////////
// R1, smooth motion: a render object shown between its last two logic states.
//
// The logic ticks at 30 Hz, and the client moves a model's render object only on the render frame
// that follows a tick (W3DView::update draws drawables only when W3D's clock moved, W3DDisplay.cpp).
// On a 144 Hz panel four frames in five showed the same transforms under a camera that moves every
// frame.  With SmoothMotion on, W3DDisplay::draw keeps each model's last two transforms and, just
// before the scene renders, sets the render object to a blend of them by the fraction of a tick
// that has passed; right after the render it puts the logic transform back, so picking and the
// logic (ParticleUplinkCannonUpdate reads live bones) see exactly what they saw before.  The picture
// is one logic tick behind (33 ms at 30 Hz) and the game never changes.
//
// Header-only on purpose: W3DModelDraw includes it on every platform, and nothing new has to be
// listed in a build.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DSMOOTHMOTION_H_
#define __W3DSMOOTHMOTION_H_

#include "matrix3d.h"
#include "quat.h"
#include "vector3.h"

#include <math.h>

/// This render pass's blend (W3DDisplay.cpp): whether it is on (SmoothMotion, and not headless), and how
/// far between the last two logic ticks the picture is.  W3DView's camera lock reads them too.
extern bool TheSmoothMotionActive;
extern float TheSmoothMotionAlpha;

/// Why a model is shown at its logic transform instead of a blend, one count each per logic tick.
enum SmoothMotionSnap
{
	SMOOTH_BLENDED = 0,			///< not a snap: blended
	SMOOTH_SNAP_FIRST,			///< no earlier transform: a new drawable, or its first tick seen
	SMOOTH_SNAP_NEW_MODEL,		///< a new render object (a model state change, a replaced drawable)
	SMOOTH_SNAP_HIDDEN,			///< hidden in either tick: containment, parachutes, garrison, stealth
	SMOOTH_SNAP_FRAME_GAP,		///< more than one tick between captures (catch-up, or not seen)
	SMOOTH_SNAP_DISTANCE,		///< moved further than SMOOTH_SNAP_DISTANCE in one tick
	SMOOTH_SNAP_ROTATION,		///< turned further than SMOOTH_SNAP_ROTATION in one tick
	SMOOTH_SNAP_NOT_RIGID,		///< a matrix that is not rotation times scale
	SMOOTH_SNAP_MARKED,			///< Drawable::markMotionDiscontinuity (a teleport the logic named)
	SMOOTH_SNAP_COUNT
};

enum
{
	SMOOTH_SNAP_DISTANCE_UNITS = 60,		///< world units per tick; a fast jet moves about 7
	SMOOTH_SNAP_ROTATION_DEGREES = 120		///< degrees per tick
};

inline const char *SmoothMotion_SnapName(SmoothMotionSnap s)
{
	static const char *NAMES[SMOOTH_SNAP_COUNT] = { "blended", "first", "new model", "hidden", "frame gap", "distance",
		"rotation", "not rigid", "marked" };
	return (s >= 0 && s < SMOOTH_SNAP_COUNT) ? NAMES[s] : "?";
}

/// Per-tick counts of how each captured model was treated, for the snap rules' tuning.
inline unsigned long long *SmoothMotion_Counts()
{
	static unsigned long long counts[SMOOTH_SNAP_COUNT] = { 0 };
	return counts;
}

/// The basis of a 3x4 transform split into a pure rotation and its three column scales.  FALSE when
/// the columns are not orthogonal (shear), or a scale is zero, which a blend must not touch.
inline bool SmoothMotion_Split(const Matrix3D &m, Matrix3D &rotation, float scale[3])
{
	Vector3 axis[3] = { m.Get_X_Vector(), m.Get_Y_Vector(), m.Get_Z_Vector() };
	for (int i = 0; i < 3; ++i) {
		scale[i] = axis[i].Length();
		if (!(scale[i] > 1.0e-6f)) return false;
		axis[i] /= scale[i];
	}
	const float TOLERANCE = 1.0e-3f;
	if (fabsf(Vector3::Dot_Product(axis[0], axis[1])) > TOLERANCE || fabsf(Vector3::Dot_Product(axis[0], axis[2])) > TOLERANCE
		|| fabsf(Vector3::Dot_Product(axis[1], axis[2])) > TOLERANCE) {
		return false;
	}
	rotation.Make_Identity();
	for (int r = 0; r < 3; ++r) {
		rotation[r][0] = axis[0][r];
		rotation[r][1] = axis[1][r];
		rotation[r][2] = axis[2][r];
	}
	return true;
}

/// The blend of two logic transforms at `alpha` (0 = prev, 1 = cur) into `out`, or the reason not to
/// (then `out` is `cur`).  Translation is lerped, the rotation slerped, each column's scale lerped.
inline SmoothMotionSnap SmoothMotion_Blend(const Matrix3D &prev, const Matrix3D &cur, float alpha, Matrix3D &out)
{
	out = cur;
	const Vector3 from = prev.Get_Translation(), to = cur.Get_Translation();
	if ((to - from).Length() > (float)SMOOTH_SNAP_DISTANCE_UNITS) {
		return SMOOTH_SNAP_DISTANCE;
	}
	Matrix3D prev_rotation, cur_rotation;
	float prev_scale[3], cur_scale[3];
	if (!SmoothMotion_Split(prev, prev_rotation, prev_scale) || !SmoothMotion_Split(cur, cur_rotation, cur_scale)) {
		return SMOOTH_SNAP_NOT_RIGID;
	}
	Quaternion qa = Build_Quaternion(prev_rotation), qb = Build_Quaternion(cur_rotation);
	float dot = qa.X * qb.X + qa.Y * qb.Y + qa.Z * qb.Z + qa.W * qb.W;
	if (dot < 0.0f) dot = -dot;
	if (dot > 1.0f) dot = 1.0f;
	const float turned_degrees = 2.0f * acosf(dot) * (180.0f / 3.14159265358979f);
	if (turned_degrees > (float)SMOOTH_SNAP_ROTATION_DEGREES) {
		return SMOOTH_SNAP_ROTATION;
	}
	if (alpha <= 0.0f) { out = prev; return SMOOTH_BLENDED; }
	if (alpha >= 1.0f) { out = cur; return SMOOTH_BLENDED; }
	Quaternion q;
	Slerp(q, qa, qb, alpha);
	Matrix3D rotation;
	Build_Matrix3D(q, rotation);
	out.Make_Identity();
	for (int c = 0; c < 3; ++c) {
		const float s = prev_scale[c] + (cur_scale[c] - prev_scale[c]) * alpha;
		for (int r = 0; r < 3; ++r) out[r][c] = rotation[r][c] * s;
	}
	out.Set_Translation(from + (to - from) * alpha);
	return SMOOTH_BLENDED;
}

/// One render object's last two logic transforms, and whether a blend is on it now.
struct SmoothMotionTrack
{
	Matrix3D Prev;
	Matrix3D Cur;
	const void *Model;			///< the render object Cur was taken from; another one is a new model
	unsigned Frame;				///< the client frame Cur was taken on
	bool HavePrev;
	bool CurHidden;
	bool PrevHidden;
	bool Applied;				///< the render object holds a blend, to be put back
	SmoothMotionSnap Snap;		///< this tick's verdict

	SmoothMotionTrack() : Model(0), Frame(0), HavePrev(false), CurHidden(false), PrevHidden(false), Applied(false),
		Snap(SMOOTH_SNAP_FIRST) { Prev.Make_Identity(); Cur.Make_Identity(); }

	/// On the first render frame after a tick: the transform the model holds now becomes Cur.  The
	/// verdict for the whole tick is decided here and counted once.
	void capture(const Matrix3D &now, const void *model, bool hidden, unsigned frame, bool marked)
	{
		const bool same_model = model == Model && Model != 0;
		const bool next_frame = same_model && frame == Frame + 1;
		PrevHidden = CurHidden;
		Prev = Cur;
		Cur = now;
		CurHidden = hidden;
		HavePrev = same_model;
		if (Model == 0) Snap = SMOOTH_SNAP_FIRST;
		else if (!same_model) Snap = SMOOTH_SNAP_NEW_MODEL;
		else if (marked) Snap = SMOOTH_SNAP_MARKED;
		else if (hidden || PrevHidden) Snap = SMOOTH_SNAP_HIDDEN;
		else if (!next_frame) Snap = SMOOTH_SNAP_FRAME_GAP;
		else {
			Matrix3D ignored;
			Snap = SmoothMotion_Blend(Prev, Cur, 0.5f, ignored);
		}
		Model = model;
		Frame = frame;
		++SmoothMotion_Counts()[Snap];
	}
};

#endif // __W3DSMOOTHMOTION_H_
