// Explosion smoke as a simulated volume, behind -volsmoke, on the Direct3D 11 frame only
// (DX11VolumeSmoke.cpp).  Client side: it reads particle systems and draws, and nothing in the
// logic reads it back.  Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

class ParticleSystem;
namespace HazardCloud { struct Camera; }

namespace VolumeSmoke {

// True for a system whose smoke a volume draws in place of its billboards.  The first time an
// explosion smoke system is asked about it is given a volume if one is free; a system that found
// none keeps its billboards for life, so nothing changes look halfway.  False whenever -volsmoke
// is off or the device cannot take the volumes.
bool Takes(ParticleSystem *system);

// Steps every volume once per logic frame that passed and draws them into the scene, from
// doParticles between two of the backend's draws.  Nothing with -volsmoke off.
void Render(const HazardCloud::Camera &camera);

// The particle manager's reset (a match ending, a load): every volume goes, and the systems left
// on billboards are forgotten, as the next game numbers its systems from 1 again.
void Reset();

// The device is going: drop every texture and program.  Volumes start again on the next device.
void ReleaseDevice();

} // namespace VolumeSmoke
