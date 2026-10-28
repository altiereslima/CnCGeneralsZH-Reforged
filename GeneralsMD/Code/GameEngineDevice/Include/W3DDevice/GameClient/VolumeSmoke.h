// Explosion smoke as a simulated volume, behind the VolumeSmoke setting, on the Direct3D 11 frame only
// (DX11VolumeSmoke.cpp).  Client side: it reads particle systems and draws, and nothing in the
// logic reads it back.  Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

class ParticleSystem;
namespace HazardCloud { struct Camera; }

namespace VolumeSmoke {

// True for a system whose billboards are not drawn: a volume draws its smoke, or its volume went
// to a newer blast and its smoke is dropped.  The first time a death's smoke system is asked
// about it is given a volume, the oldest one's if none is free, so none of them ever draws
// billboards.  False whenever the setting is Off or the device cannot take the volumes.
bool Takes(ParticleSystem *system);

// Takes without claiming anything and without touching the device: for the GPU overflow path,
// whose particles of a taken system are hidden as well.
bool Hides(ParticleSystem *system);

// Reads what the volumes need from the drawables and the structures (the units pushing the air,
// the buildings it flows round, the hull a dying tank's blast is laid out on), once per logic
// frame, from W3DDisplay::draw before the logic thread's window opens.  Nothing with the setting
// Off.
void Gather();

// Steps every volume once per logic frame that passed and draws them into the scene, from
// doParticles between two of the backend's draws.  Nothing with the setting Off.
void Render(const HazardCloud::Camera &camera);

// The particle manager's reset (a match ending, a load): every volume goes, and the systems left
// on billboards are forgotten, as the next game numbers its systems from 1 again.
void Reset();

// The device is going: drop every texture and program.  Volumes start again on the next device.
void ReleaseDevice();

} // namespace VolumeSmoke
