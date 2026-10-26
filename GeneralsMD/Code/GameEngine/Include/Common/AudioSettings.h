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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// EA Pacific
// John McDonald, Jr
// Do not distribute

#pragma once

#ifndef _AUDIOSETTINGS_H_
#define _AUDIOSETTINGS_H_

#include "Common/AsciiString.h"

#include <vector>

enum { MAX_HW_PROVIDERS = 4 };

struct AudioSettings
{
	// Every field below is filled in by AudioSettings.ini and nothing gives them a value before
	// that, so a key the shipped file does not carry arrives as whatever was on the heap. New
	// fields get their default here.
	AudioSettings() :
		m_rangeVolumeFade( FALSE ),
		m_speedOfSound( 1700.0f ),
		m_maxSoundDelay( 1000.0f ),
		m_dopplerScale( 3.0f ),
		m_dopplerFastSpeed( 100.0f ),
		m_airAbsorptionDistance( 450.0f ),
		m_reverbLevel( 1.0f ),
		m_duckVolume( 0.15f ),
		m_duckTime( 4000.0f ),
		m_replyUnitCooldown( 1500.0f ),
		m_replyGap( 400.0f ),
		m_replyMaxVoices( 2 ),
		m_battleVoiceBudget( 16 ),
		m_coalesceRadius( 80.0f ),
		m_coalesceFrames( 2 ),
		m_coalesceGainStep( 0.12f ),
		m_coalesceMaxGain( 1.5f ),
		m_occlusionStrength( 0.75f ),
		m_occlusionVolume( 0.6f ),
		m_occlusionDepth( 20.0f ),
		m_occlusionTime( 250.0f ),
		m_occlusionRays( 16 ),
		m_zoomMixNearHeight( 300.0f ),
		m_zoomMixFarHeight( 900.0f ),
		m_zoomMixDetailVolume( 0.75f ),
		m_ambienceBedNearVolume( 0.3f ),
		m_ambienceBedFarVolume( 0.8f ),
		m_ambienceBedSnow( "Amb_WinterNatureWindLoop" ),
		m_ambienceBedTown( "Amb_UrbanCityAmbientLoop" ),
		m_ambienceBedDesert( "Amb_DesertNatureWindThinLoop" ),
		m_ambienceBedField( "Amb_TemperateMeadowAmbientLoop" )
	{
		m_duckSounds.push_back( "ExplosionNeutron" );
		m_duckSounds.push_back( "ExplosionDaisyCutter" );
		m_duckSounds.push_back( "ExplosionMOAB" );
		m_duckSounds.push_back( "ExplosionMiniNuke" );
		m_duckSounds.push_back( "ParticleUplinkCannon_GroundAnnihilationSoundLoop" );
	}

	AsciiString m_audioRoot;
	AsciiString m_soundsFolder;
	AsciiString m_musicFolder;
	AsciiString m_streamingFolder;
	AsciiString m_soundsExtension;
	Bool m_useDigital;
	Bool m_useMidi;
	Int m_outputRate;
	Int m_outputBits;
	Int m_outputChannels;
	Int m_sampleCount2D;
	Int m_sampleCount3D;
	Int m_streamCount;
	Int m_globalMinRange;
	Int m_globalMaxRange;
	/** How a 3D sound loses volume with distance.
		*
		* No, the default, is what the game has always done: a 1/distance curve past the minimum range
		* and then a hard cut to silence at the maximum, so a sound you are walking away from stops
		* dead rather than fading out. Yes uses the linear fade between the two ranges that EA left
		* commented out next to it, which reaches zero exactly where the cut is.
		* `RangeVolumeFade` in AudioSettings.ini. */
	Bool m_rangeVolumeFade;

	/** Sound in air, none of which the shipped file carries, so the defaults below are what plays.
		*
		* `SpeedOfSound`, game units a second: a world sound starts late by its distance to the
		* microphone over it, never more than `MaxSoundDelay` milliseconds, and a moving one is pitched
		* by doppler. The ground is drawn at about 0.2 m a unit (a Ranger stands 13 units, a Crusader
		* is 30 long), so 343 m/s is 1700. Zero is neither. `DopplerScale` multiplies the speed doppler
		* hears, zero switching doppler alone off: 3, since a Raptor crosses the map at 35 m/s on that
		* scale and a real jet's pitch bend needs it about ten times faster. `DopplerFastSpeed`, units
		* a second: past it the scale grows with the speed, so at 100 a Raptor at 175 is heard at 5.25
		* times its speed and a tank at 30 at 3. Zero keeps one scale for everything.
		* `AirAbsorptionDistance`, game units: where air has taken the top end down from 20 kHz to 10,
		* falling with the square of the distance past it, through two stages. Zero is no air. A snow
		* map takes it at 60%, and a zoom past MaxCameraHeight stretches it with the ranges.  450: at
		* 300 a fight at the edge of the screen at EA's zoom (about 300 units off) lost all above 10 kHz
		* and one just past it (460) all above 6 kHz.
		* `ReverbLevel`: the room's send, as a share of what the map asks for. Zero is no room.
		* `DuckVolume` and `DuckTime` (milliseconds): when one of `DuckSounds` starts within earshot,
		* everything else dips to that volume and comes back over that time. 100% is no duck. */
	Real m_speedOfSound;
	Real m_maxSoundDelay;
	Real m_dopplerScale;
	Real m_dopplerFastSpeed;
	Real m_airAbsorptionDistance;
	Real m_reverbLevel;
	Real m_duckVolume;
	Real m_duckTime;
	std::vector<AsciiString> m_duckSounds;

	/** The battle mix (MilesAudioManager), none of which the shipped file carries either.
		*
		* A unit's reply in the commander's ear: the same unit says nothing again for
		* `ReplyUnitCooldown` milliseconds, another unit's line waits `ReplyGap` milliseconds after the
		* last one started, and no more than `ReplyMaxVoices` lines sound at once.  Zero switches each off.
		* EVA and the interface are never held back, and nothing in the battle takes their voices.
		*
		* `BattleVoiceBudget`: how many one-shot battle sounds play at once.  Past it a new one takes the
		* place of the least important (lowest priority, then the quietest, which is mostly the farthest),
		* or is not played if it matters less than all of them.  Loops are counted but never taken.  Zero
		* is no budget.
		*
		* `CoalesceRadius` (game units) and `CoalesceFrames` (logic frames): the same one-shot sound asked
		* for that soon after and that close to another is folded into it, which plays `CoalesceGainStep`
		* louder for each one folded in, at most `CoalesceMaxGain` times.  A zero radius folds nothing.
		*
		* Occlusion: ground or a building in the line from the microphone muffles a world sound, the low
		* pass at `OcclusionStrength` of the backend's occlusion and the volume down to `OcclusionVolume`
		* once `OcclusionDepth` units of hill stand in the line, moving over `OcclusionTime` milliseconds;
		* `OcclusionRaysPerFrame` playing sounds are looked at again each pass.  A zero strength is no
		* occlusion. */
	Real m_replyUnitCooldown;
	Real m_replyGap;
	Int m_replyMaxVoices;
	Int m_battleVoiceBudget;
	Real m_coalesceRadius;
	Int m_coalesceFrames;
	Real m_coalesceGainStep;
	Real m_coalesceMaxGain;
	Real m_occlusionStrength;
	Real m_occlusionVolume;
	Real m_occlusionDepth;
	Real m_occlusionTime;
	Int m_occlusionRays;

	/** The mix as the camera zooms out, none of which the shipped file carries.
		*
		* Between `ZoomMixNearHeight` and `ZoomMixFarHeight`, the camera's height over the ground it looks
		* at, the 3D sounds that are not ambience (weapons, engines, explosions, voices on the field) fall
		* to `ZoomMixDetailVolume` of their level, while ambience keeps its own; zoomed back in, the
		* detail returns.  100% leaves the mix as it was.  EA's own zoom bonus (`ZoomSoundVolume...`)
		* still applies on top, to both.  75%, 2.5 dB: 60% took 4.4 dB off a zoomed out
		* fight, on top of EA's 1.9.  The ranges and the air stretch with the zoom as well
		* (AudioManager::getRangeScale), which is what keeps the edges of a zoomed out screen in earshot.
		*
		* The map's ambience bed (MilesAudioManager::updateAmbienceBed): one looping 2D sound chosen
		* from the map when it starts, `AmbienceBedSnow` on a snow map, `AmbienceBedTown` on a built-up
		* one, `AmbienceBedDesert` or `AmbienceBedField` by the ground it is painted with.  It plays at
		* `AmbienceBedNearVolume` of its own Volume with the camera at the near height and rises to
		* `AmbienceBedFarVolume` at the far one, under the Ambience slider.  `NoSound`, or any name the loaded
		* audio data lacks, plays no bed on that kind of map. */
	Real m_zoomMixNearHeight;
	Real m_zoomMixFarHeight;
	Real m_zoomMixDetailVolume;
	Real m_ambienceBedNearVolume;
	Real m_ambienceBedFarVolume;
	AsciiString m_ambienceBedSnow;
	AsciiString m_ambienceBedTown;
	AsciiString m_ambienceBedDesert;
	AsciiString m_ambienceBedField;

	Int m_drawableAmbientFrames;
	Int m_fadeAudioFrames;
	UnsignedInt m_maxCacheSize;

	Real m_minVolume;		// At volumes less than this, the sample will be culled.
	
	AsciiString m_preferred3DProvider[MAX_HW_PROVIDERS + 1];

	//Defaults actually don't ever get changed!
	Real m_relative2DVolume;		//2D volume compared to 3D
	Real m_defaultSoundVolume;
	Real m_default3DSoundVolume;
	Real m_defaultSpeechVolume;
	Real m_defaultMusicVolume;
	UnsignedInt m_defaultSpeakerType2D;
	UnsignedInt m_defaultSpeakerType3D;

	//If you want to change a value, store it somewhere else (like here)
	Real m_preferredSoundVolume;
	Real m_preferred3DSoundVolume;
	Real m_preferredSpeechVolume;
	Real m_preferredMusicVolume;

	//The desired altitude of the microphone to improve panning relative to terrain.
	Real m_microphoneDesiredHeightAboveTerrain;

	//When tracing a line between the ground look-at-point and the camera, we want
	//to ensure a maximum percentage, so the microphone never goes behind the camera.
	Real m_microphoneMaxPercentageBetweenGroundAndCamera;

	//Handles changing sound volume whenever the camera is close to the microphone.
  Real m_zoomMinDistance;			//If we're closer than the minimum distance, then apply the full bonus no matter how close.
  Real m_zoomMaxDistance;			//The maximum distance from microphone we need to be before benefiting from any bonus.
	
  //NOTE: The higher this value is, the lower normal sounds will be! If you specify a sound volume value of 25%, then sounds will play
	//between 75% and 100%, not 100% to 125%!
  Real m_zoomSoundVolumePercentageAmount;	//The amount of sound volume dedicated to zooming.
};

#endif // _AUDIOSETTINGS_H_
