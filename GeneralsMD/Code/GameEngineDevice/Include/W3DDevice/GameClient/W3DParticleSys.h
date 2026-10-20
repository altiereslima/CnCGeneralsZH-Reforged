/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DParticleSys.h /////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DParticleSys_H_
#define __W3DParticleSys_H_

#include "GameClient/ParticleSys.h"
#include "WW3D2/pointgr.h"
#include "WW3D2/streak.h"
#include "WW3D2/rinfo.h"
#include "WWLib/bittype.h"

#include <vector>

//=============================================================================
/** W3D implementation of the game display which is responsible for creating
  * all interaction with the screen and updating the display 
	*/
class W3DParticleSystemManager : public ParticleSystemManager
{

public:
	W3DParticleSystemManager();
	~W3DParticleSystemManager();

	virtual void reset();
	virtual void doParticles(RenderInfoClass &rinfo);
	virtual void queueParticleRender();
	///< returns the number of particles shown on screen per frame
	virtual Int getOnScreenParticleCount() { return m_onScreenParticleCount; }
	/// at the render's sync point: the systems' stealth fades and the fields' fades, which read the logic
	void takeReadsForRender( void );

private:
	enum { MAX_POINTS_PER_GROUP = 512 };

	PointGroupClass *m_pointGroup;							///< the point group that contains all of the particles
	StreakLineClass *m_streakLine;							///< the streak class that contains all of the streaks
	ShareBufferClass<Vector3> *m_posBuffer;			///< array of particle positions
	ShareBufferClass<Vector4> *m_RGBABuffer;		///< array of particle color and alpha
	ShareBufferClass<float> *m_sizeBuffer;			///< array of particle sizes
	ShareBufferClass<uint8> *m_angleBuffer;			///< array of particle orientations
	Bool m_readyToRender;											///< if true, it is OK to render

public:
	/// One sorted billboard system's share of a frame's fill: reserved on the render thread, written by
	/// one job, inserted into the sorting pool on the render thread again, in system list order.
	struct BillboardFill
	{
		ParticleSystem *												system;
		PointGroupClass::SortingBillboardRange	range;
		TextureClass *													texture;
		ShaderClass															shader;
		Int																			capacity;					///< quads reserved: the system's particles, at most MAX_POINTS_PER_GROUP
		UnsignedInt															fieldIncrement;		///< 1 if every drawn particle counts against the field budget
		Real																		fade;							///< ParticleSystem::getStealthFade, above zero
		Int																			drawn;						///< written by the job
		UnsignedInt															pastLimit;				///< written by the job: on-screen particles past capacity
		Bool																		glow;							///< written by the job: the fire's glow is in the normals, not the colour
	};

	/// One particle the sun's smoke map can take, as the last particle update left it.
	struct SunMapCaster
	{
		Real							x, y, z, radius, opticalDepth;
		ParticleSystem *	system;
	};

	/// Every particle of a billboarded alpha system that is drawn at all, with the optical depth its
	/// alpha and its system's stealth fade give it, in system list order.  Walked once a particle
	/// update; fillSmokeMap culls it to its own sun every pass.
	const std::vector<SunMapCaster> &getSunMapCasters( void );

private:
	/// A system with particles that the local player sees at all, and how much of it.
	struct SystemView
	{
		ParticleSystem *	system;
		Real							fade;				///< ParticleSystem::getStealthFade, above zero
	};

	void refreshFrameView( void );

	std::vector<BillboardFill> m_billboardFills;	///< kept across frames so the fill does not allocate

	/// Nothing moves a particle between two particle updates and nothing but the logic changes a
	/// stealth fade, and both run once a logic frame, so the walk over every system is taken once a
	/// logic frame and every pass drawn until the next one reads this.  The counts and the next system
	/// id catch anything that adds or takes a system or a particle away in between; a reset clears it.
	/// That only holds while every add and remove of a system or a particle moves m_particleCount,
	/// m_particleSystemCount or m_uniqueSystemID, and while the stealth look, the opacity and the
	/// attachment that decide a fade change in the logic alone, or in doDrawModule before a pass's first
	/// refresh.  Anything else that changes one of them mid-frame is not seen until the next logic frame.
	std::vector<SystemView>		m_frameView;
	std::vector<ParticleSystem *>	m_fieldSystems;		///< the contamination fields' systems, empty ones too
	std::vector<SunMapCaster>	m_sunMapCasters;
	Bool											m_frameViewValid;
	Bool											m_sunMapCastersValid;
	UnsignedInt								m_frameViewLogicFrame;
	UnsignedInt								m_frameViewFrame;
	UnsignedInt								m_frameViewParticles;
	UnsignedInt								m_frameViewSystems;
	ParticleSystemID					m_frameViewNextID;
};

#endif  // end __W3DParticleSys_H_
