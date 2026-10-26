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

// FILE: MilesAudioManager.cpp 
/*---------------------------------------------------------------------------*/
/* EA Pacific                                                                */
/* Confidential Information	                                                 */
/* Copyright (C) 2001 - All Rights Reserved                                  */
/* DO NOT DISTRIBUTE                                                         */
/*---------------------------------------------------------------------------*/
/* Project:    RTS3                                                          */
/* File name:  MilesAudioManager.cpp                                         */
/* Created:    John K. McDonald, Jr., 3/21/2002                              */
/* Desc:       This is the implementation for the MilesAudioManager, which   */
/*						 interfaces with the Miles Sound System.                       */
/* Revision History:                                                         */
/*		7/18/2002 : Initial creation                                           */
/*---------------------------------------------------------------------------*/

#if defined(_WIN32)
#include <dsound.h>
#endif
#include "Lib/Clock.h"
#include "Lib/BaseType.h"
#include "MilesAudioDevice/MilesAudioManager.h"
#include "MilesAudioDevice/AudioMixPolicy.h"

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/AsciiString.h"
#include "Common/AudioEventInfo.h"
#include "Common/FileSystem.h"
#include "Common/GameCommon.h"
#include "Common/GameSounds.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "Common/MapObject.h"
#include "Common/ThingTemplate.h"
#include "Common/ScopedMutex.h"
#include "Common/TerrainTypes.h"

#include <algorithm>

#include "GameClient/DebugDisplay.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"

#include "Common/file.h"
#if !defined(_WIN32)
#include "Common/LocalFileSystem.h"

/* Off Windows (C4): InterlockedCompareExchange's job on PlayingAudio::m_status.  Every call site
	 casts the 4-byte PlayingStatus to (volatile long *), which is the same width under Windows' LLP64
	 and twice it under LP64 (macOS, Linux): a like-for-like stand-in would compare and swap eight bytes
	 over a four-byte member.  So this takes the member's own type and works on its own four bytes, with
	 the full barrier every Interlocked* call is.  It returns the value that was there, as that does. */
static inline PlayingStatus playingStatusCompareExchange( volatile PlayingStatus *status,
	PlayingStatus exchange, PlayingStatus comparand )
{
	static_assert( sizeof( PlayingStatus ) == sizeof( Int ), "the status is compared and swapped as its own four bytes" );
	Int expected = (Int)comparand;
	__atomic_compare_exchange_n( (volatile Int *)status, &expected, (Int)exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST );
	return (PlayingStatus)expected;
}
#endif

#ifdef _INTERNAL
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

enum { INFINITE_LOOP_COUNT = 1000000 };

// Callback functions useful for Miles ////////////////////////////////////////////////////////////
static void AILCALLBACK setSampleCompleted( HSAMPLE sampleCompleted );
static void AILCALLBACK set3DSampleCompleted( H3DSAMPLE sample3DCompleted );
static void AILCALLBACK setStreamCompleted( HSTREAM streamCompleted );

static U32 AILCALLBACK streamingFileOpen(char const *fileName, AILFILEHANDLE *file_handle);
static void AILCALLBACK streamingFileClose(AILFILEHANDLE fileHandle);
static S32 AILCALLBACK streamingFileSeek(AILFILEHANDLE fileHandle, S32 offset, U32 type);
static U32 AILCALLBACK streamingFileRead(AILFILEHANDLE fileHandle, void *buffer, U32 bytes);

//-------------------------------------------------------------------------------------------------
/* "Is this sound one of these?", asked of a sound that is already playing or already queued.
	 EA read the type straight through the info pointer at fifteen places in this file and checked
	 it for null at two others, which is the same code admitting both that the pointer can be null
	 and that nobody remembered.  It can: setEventName drops the info because it is not valid for
	 the new name, the drawable custom-ambient path clears it by hand before deleting what it points
	 at, and a delayed loop hands the same event back to the request list without one.  When the
	 sound finishes the read is an access violation at address 0x90, in a stack that is entirely
	 audio and names nothing that caused it.  A sound with no info is not music and not speech,
	 which is what every one of those fifteen questions wants to hear. */
static Bool audioIsType( const AudioEventRTS *event, AudioType type )
{
	const AudioEventInfo *info = event ? event->getAudioEventInfo() : NULL;
	return info != NULL && info->m_soundType == type;
}

//-------------------------------------------------------------------------------------------------
MilesAudioManager::MilesAudioManager() :
	m_providerCount(0),
	m_selectedProvider(PROVIDER_ERROR),
	m_selectedSpeakerType(0),
	m_lastProvider(PROVIDER_ERROR),
	m_digitalHandle(NULL),
	m_num2DSamples(0),
	m_num3DSamples(0),
	m_numStreams(0),
	m_delayFilter(NULL),
	m_clockFrame(0),
	m_clockElapsedMs(MSEC_PER_LOGICFRAME_REAL),
	m_clockMovedAt(0),
	m_legacySound(FALSE),
	m_roomAbsorption(0.0f),
	m_airScale(0.0f),
	m_askingOnArrival(false),
	m_bedHandle(AHSV_Error),
	m_bedStartedAt(0),
	m_lastReplyAtMs(0),
	m_mixLoggedAt(0),
	m_occlusionCursor(0),
	m_blockerColumns(0),
	m_blockerRows(0),
	m_blockersBuiltAt(0),
	m_binkHandle(NULL),
	m_pref3DProvider(AsciiString::TheEmptyString),
	m_prefSpeaker(AsciiString::TheEmptyString)
{
	m_audioCache = NEW AudioFileCache;
	memset(&m_mixCounts, 0, sizeof(m_mixCounts));
}

//-------------------------------------------------------------------------------------------------
MilesAudioManager::~MilesAudioManager()
{
	DEBUG_ASSERTCRASH(m_binkHandle == NULL, ("Leaked a Bink handle. Chuybregts"));
	releaseHandleForBink();
	closeDevice();
	delete m_audioCache;
	
	DEBUG_ASSERTCRASH(this == TheAudio, ("Umm...\n"));
	TheAudio = NULL;
}

//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
AudioHandle MilesAudioManager::addAudioEvent( const AudioEventRTS *eventToAdd )
{
	if (TheGlobalData->m_preloadReport) {
		if (!eventToAdd->getEventName().isEmpty()) {
			m_allEventsLoaded.insert(eventToAdd->getEventName());
		}
	}

	return AudioManager::addAudioEvent(eventToAdd);
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::audioDebugDisplay(DebugDisplayInterface *dd, void *, FILE *fp )
{
	std::list<PlayingAudio *>::iterator it;

	static char buffer[128] = { 0 };
	if (buffer[0] == 0) {
		AIL_MSS_version(buffer, 128);
	}
	
	Coord3D lookPos;
	TheTacticalView->getPosition( &lookPos );
	lookPos.z = TheTerrainLogic->getGroundHeight( lookPos.x, lookPos.y );
	const Coord3D *mikePos = TheAudio->getListenerPosition();
	Coord3D distanceVector = TheTacticalView->get3DCameraPosition();
	distanceVector.sub( mikePos );

	Int now = TheGameLogic->getFrame();
	static Int lastCheck = now;
	const Int frames = 60;
	static Int latency = 0;
	static Int worstLatency = 0;
	if( lastCheck + frames <= now )
	{
		latency = AIL_get_timer_highest_delay();
		if( latency > worstLatency )
		{
			worstLatency = latency;
		}
		lastCheck = now;
	}

	if( dd )
	{
		dd->printf("Miles Sound System version: %s    ", buffer);
		dd->printf("Memory Usage : %d/%d\n", m_audioCache->getCurrentlyUsedSize(), m_audioCache->getMaxSize());
		dd->printf("Sound: %s    ", (isOn(AudioAffect_Sound) ? "Yes" : "No"));
		dd->printf("3DSound: %s    ", (isOn(AudioAffect_Sound3D) ? "Yes" : "No"));
		dd->printf("Speech: %s    ", (isOn(AudioAffect_Speech) ? "Yes" : "No"));
		dd->printf("Music: %s\n", (isOn(AudioAffect_Music) ? "Yes" : "No"));
		dd->printf("Channels Available: ");
		dd->printf("%d Sounds    ", m_sound->getAvailableSamples());

		dd->printf("%d(%d) 3D Sounds\n", m_sound->getAvailable3DSamples(), m_available3DSamples.size() );
		dd->printf("Volume: ");
		dd->printf("Sound: %d    ", REAL_TO_INT(m_soundVolume * 100.0f));
		dd->printf("3DSound: %d    ", REAL_TO_INT(m_sound3DVolume * 100.0f));
		dd->printf("Speech: %d    ", REAL_TO_INT(m_speechVolume * 100.0f));
		dd->printf("Music: %d\n", REAL_TO_INT(m_musicVolume * 100.0f));
		dd->printf("Current 3D Provider: %s    ", 
			
		TheAudio->getProviderName(m_selectedProvider).str());
		dd->printf("Current Speaker Type: %s\n", TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType()).str());

		dd->printf( "Looking at: (%d,%d,%d) -- Microphone at: (%d,%d,%d)\n", 
				(Int)lookPos.x, (Int)lookPos.y, (Int)lookPos.z, (Int)mikePos->x, (Int)mikePos->y, (Int)mikePos->z );
		dd->printf( "Camera distance from microphone: %d -- Zoom Volume: %d%%\n", 
				(Int)distanceVector.length(), (Int)(TheAudio->getZoomVolume()*100.0f) );
		dd->printf( "Worst latency: %d -- Current latency: %d\n", worstLatency, latency );

		dd->printf("-----------------------------------------------------------\n");
		dd->printf("Playing Audio\n");
	}
	if( fp )
	{
		fprintf( fp, "Miles Sound System version: %s    ", buffer );
		fprintf( fp, "Memory Usage : %d/%d\n", m_audioCache->getCurrentlyUsedSize(), m_audioCache->getMaxSize() );
		fprintf( fp, "Sound: %s    ", (isOn(AudioAffect_Sound) ? "Yes" : "No") );
		fprintf( fp, "3DSound: %s    ", (isOn(AudioAffect_Sound3D) ? "Yes" : "No") );
		fprintf( fp, "Speech: %s    ", (isOn(AudioAffect_Speech) ? "Yes" : "No") );
		fprintf( fp, "Music: %s\n", (isOn(AudioAffect_Music) ? "Yes" : "No") );
		fprintf( fp, "Channels Available: " );
		fprintf( fp, "%d Sounds    ", m_sound->getAvailableSamples() );
		fprintf( fp, "%d 3D Sounds\n", m_sound->getAvailable3DSamples() );
		fprintf( fp, "Volume: ");
		fprintf( fp, "Sound: %d    ", REAL_TO_INT(m_soundVolume * 100.0f) );
		fprintf( fp, "3DSound: %d    ", REAL_TO_INT(m_sound3DVolume * 100.0f) );
		fprintf( fp, "Speech: %d    ", REAL_TO_INT(m_speechVolume * 100.0f) );
		fprintf( fp, "Music: %d\n", REAL_TO_INT(m_musicVolume * 100.0f) );
		fprintf( fp, "Current 3D Provider: %s    ", TheAudio->getProviderName(m_selectedProvider).str());
		fprintf( fp, "Current Speaker Type: %s\n", TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType()).str());

		fprintf( fp, "Looking at: (%d,%d,%d) -- Microphone at: (%d,%d,%d)\n", 
				(Int)lookPos.x, (Int)lookPos.y, (Int)lookPos.z, (Int)mikePos->x, (Int)mikePos->y, (Int)mikePos->z );
		fprintf( fp, "Camera distance from microphone: %d -- Zoom Volume: %d%%\n", 
				(Int)distanceVector.length(), (Int)(TheAudio->getZoomVolume()*100.0f) );

		fprintf( fp, "-----------------------------------------------------------\n" );
		fprintf( fp, "Playing Audio\n" );
	}

	PlayingAudio *playing = NULL;
	Int channel;
	Int channelCount;
	Real volume = 0.0f;
	AsciiString filenameNoSlashes;

	const Int maxChannels = 64;
	PlayingAudio *playingArray[maxChannels] = { NULL };

	// 2-D Sounds
	if( dd )
	{
		dd->printf("-----------------------------------------------------Sounds\n");
		channelCount = TheAudio->getNum2DSamples();
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}

			playingArray[AIL_sample_user_data(playing->m_sample, 0)] = playing;
		}

		for (Int i = 1; i <= maxChannels && i <= channelCount; ++i) {
			playing = playingArray[i];
			if (!playing) {
				dd->printf("%d: Silence\n", i);
				continue;
			}

			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			dd->printf("%2d: %-20s - (%s) Volume: %d (2D)\n", i, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
			playingArray[i] = NULL;
		}
	}
	if( fp )
	{
		fprintf( fp, "-----------------------------------------------------Sounds\n" );
		channelCount = TheAudio->getNum2DSamples();
		channel = 1;
		for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it )
		{
			playing = *it;
			if( !playing ) 
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind( '\\' ) + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume( playing->m_audioEventRTS );

			fprintf( fp, "%2d: %-20s - (%s) Volume: %d (2D)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT( volume ) );
		}
		for( int i = channel; i <= channelCount; ++i ) 
		{
			fprintf( fp, "%d: Silence\n", i );
		}
	}

	const Coord3D *microphonePos = TheAudio->getListenerPosition();

	// Now 3D Sounds
	if( dd )
	{
		dd->printf("--------------------------------------------------3D Sounds\n");
		channelCount = TheAudio->getNum3DSamples();
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}

			playingArray[AIL_3D_user_data(playing->m_3DSample, 0)] = playing;
		}

		for (Int i = 1; i <= maxChannels && i <= channelCount; ++i) 
		{
			playing = playingArray[i];
			if (!playing) 
			{
				dd->printf("%d: Silence\n", i);
				continue;
			}

			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);
			Real dist = -1.0f;
			const Coord3D *pos = playing->m_audioEventRTS->getPosition();
			char distStr[32];
			if( pos )
			{
				Coord3D vector = *microphonePos;
				vector.sub( pos );
				dist = vector.length();
				sprintf( distStr, "%d", REAL_TO_INT( dist ) );
			}
			else
			{
				sprintf( distStr, "???" );
			}
			char str[32];
			switch( playing->m_audioEventRTS->getOwnerType() )
			{
				case OT_Positional:
					sprintf( str, "(3D)" );
					break;
				case OT_Object:
					sprintf( str, "(3DObj)" );
					break;
				case OT_Drawable:
					sprintf( str, "(3DDraw)" );
					break;
				case OT_Dead:
					sprintf( str, "(3DDead)" );
					break;

			}

			dd->printf("%2d: %-20s - (%s) Volume: %d, Dist: %s, %s\n", 
				i, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume), distStr, str );
			playingArray[i] = NULL;
		}
	}
	if( fp )
	{
		fprintf( fp, "--------------------------------------------------3D Sounds\n" );
		channelCount = TheAudio->getNum3DSamples();
		channel = 1;
		for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) 
		{
			playing = *it;
			if( !playing ) 
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume( playing->m_audioEventRTS );
			fprintf( fp, "%2d: %-24s - (%s) Volume: %d \n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT( volume ) );
		}

		for( int i = channel; i <= channelCount; ++i ) 
		{
			fprintf( fp, "%2d: Silence\n", i );
		}
	}

	// Now Streams
	if( dd )
	{
		dd->printf("----------------------------------------------------Streams\n");
		channelCount = TheAudio->getNumStreams();
		channel = 1;
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;

			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			dd->printf("%2d: %-24s - (%s)  Volume: %d (Stream)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
		}
		
		for ( int i = channel; i <= channelCount; ++i) {
			dd->printf("%2d: Silence\n", i);
		}
		dd->printf("===========================================================\n");
	}
	if( fp )
	{
		fprintf( fp, "----------------------------------------------------Streams\n" );
		channelCount = TheAudio->getNumStreams();
		channel = 1;
		for( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it ) 
		{
			playing = *it;
			if( !playing ) 
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			fprintf( fp, "%2d: %-24s - (%s)  Volume: %d (Stream)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT( volume ) );
		}
		
		for( int i = channel; i <= channelCount; ++i ) 
		{
			fprintf( fp, "%2d: Silence\n", i );
		}
		fprintf( fp, "===========================================================\n" );
	}
}
#endif

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::init()
{
	AudioManager::init();
	m_engineSettings = *m_audioSettings;
	m_roomAbsorption = m_audioSettings->m_airAbsorptionDistance;
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("Sound has temporarily been disabled in debug builds only. jkmcd\n"));
	// for now, _DEBUG builds only should have no sound. ask jkmcd or srj about this.
	return;
#endif

	// We should now know how many samples we want to load
	openDevice();
	m_audioCache->setMaxSize(getAudioSettings()->m_maxCacheSize);

	// Now, set the file callbacks to load the streams from Biggie files
	AIL_set_file_callbacks(streamingFileOpen, streamingFileClose, streamingFileSeek, streamingFileRead);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::postProcessLoad()
{
	AudioManager::postProcessLoad();
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::reset()
{
#if defined(_DEBUG) || defined(_INTERNAL)
	dumpAllAssetsUsed();
	m_allEventsLoaded.clear();
#endif

	AudioManager::reset();
	stopAllAudioImmediately();
  removeAllAudioRequests();
	m_recentShots.clear();
	m_replyAtMs.clear();
	m_blockers.clear();
	m_blockersBuiltAt = 0;
	// the bed went with everything else; the next map, the same one again included, chooses anew
	m_roomMap.clear();
	m_bedEvent.clear();
	m_bedHandle = AHSV_Error;
  // This must come after stopAllAudioImmediately() and removeAllAudioRequests(), to ensure that
  // sounds pointing to the temporary AudioEventInfo handles are deleted before their info is deleted
  removeLevelSpecificAudioEventInfos();
}

//-------------------------------------------------------------------------------------------------
/** -wav <from> <to> [name]: record the finished mix over a range of logic frames.
	*
	* A -wav run opens no audio device (openDevice): the mix is moved by the logic clock, here, by
	* exactly 1/30 of a second of sound per logic frame, however long the frame took on the wall clock.
	* So the file is as long as the frames it spans and lines up with the picture of the same frames,
	* in a run that plays at the speed a person hears, in a -directorrecord run that crawls, and on a
	* machine with no speakers.  A sound asked for during frame N starts N/LOGICFRAMES_PER_SECOND seconds in, because the
	* requests are only played after this has mixed up to N.  Nothing is heard while it runs. */
static void updateSoundCapture( void )
{
	static Bool recording = FALSE;
	static Bool finished = FALSE;

	if (TheGlobalData->m_wavEndFrame <= 0 || TheGameLogic == NULL)
		return;

	const UnsignedInt frame = TheGameLogic->getFrame();
	AIL_ex_mix_to_frame((S32)frame);

	if (finished || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return;
	if (frame < (UnsignedInt)TheGlobalData->m_wavStartFrame)
		return;

	if (!recording)
	{
		char directory[_MAX_PATH];
		snprintf(directory, ARRAY_SIZE(directory), "%sVideos", TheGlobalData->getPath_UserData().str());
#if defined(_WIN32)
		CreateDirectoryA(directory, NULL);
#else
		TheLocalFileSystem->createDirectory(AsciiString(directory));
#endif

		char pathname[_MAX_PATH];
		snprintf(pathname, ARRAY_SIZE(pathname), "%s\\%s.wav", directory, TheGlobalData->m_wavName.str());
		if (!AIL_ex_start_capture(pathname))
		{
			finished = TRUE;
			DEBUG_LOG(("AUDIO: nothing could be recorded into %s\n", pathname));
			return;
		}

		recording = TRUE;
		DEBUG_LOG(("AUDIO: recording logic frames %d to %d into %s\n",
			TheGlobalData->m_wavStartFrame, TheGlobalData->m_wavEndFrame, pathname));
		return;
	}

	if (frame <= (UnsignedInt)TheGlobalData->m_wavEndFrame)
		return;

	finished = TRUE;
	AIL_ex_stop_capture();

	// The file's own length, counted in samples written, against the frames the capture spanned.
	S32 samples = 0;
	S32 rate = 0;
	AIL_ex_capture_length(&samples, &rate);
	const double recordedSeconds = rate > 0 ? (double)samples / (double)rate : 0.0;
	const double pictureSeconds = (double)(frame - TheGlobalData->m_wavStartFrame) / LOGICFRAMES_PER_SECONDS_REAL;
	DEBUG_LOG(("AUDIO: recorded %.3f seconds of sound (%d samples at %d Hz) for %.3f seconds of picture, "
		"%.3f adrift\n", recordedSeconds, (Int)samples, (Int)rate, pictureSeconds, recordedSeconds - pictureSeconds));
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::update()
{
	AudioManager::update();
	processCompletedAudio();
	updateSoundCapture();
	advanceAudioClock();
	applyLegacySound();
	if (TheGameLogic->isInGame() && !TheGameLogic->isLoadingMap() && m_roomMap != TheGlobalData->m_mapName)
	{
		m_roomMap = TheGlobalData->m_mapName;
		setRoomFromMap();
	}
	// the air, as far as the room has it, stretched with the sounds' ranges (AudioManager::getRangeScale)
	if (m_airScale != getRangeScale())
	{
		m_airScale = getRangeScale();
		AIL_ex_set_3D_air(m_audioSettings->m_speedOfSound, m_roomAbsorption * m_airScale);
	}
	if (TheGameLogic->isInGame() && !TheGameLogic->isLoadingMap())
	{
		buildBlockers();
	}
	updateAmbienceBed();
	setDeviceListenerPosition();
	processRequestList();
	processPlayingList();
	processFadingList();
	logMix();
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopAudio( AudioAffect which )
{
	// All we really need to do is:
	// 1) Remove the EOS callback.
	// 2) Stop the sample, (so that when we later unload it, bad stuff doesn't happen)
	// 3) Set the status to stopped, so that when we next process the playing list, we will 
	//		correctly clean up the sample.


	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				AIL_register_EOS_callback(playing->m_sample, NULL);
				AIL_stop_sample(playing->m_sample);
				playing->m_status = PS_Stopped;
			}
		}
	}

	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
				AIL_stop_3D_sample(playing->m_3DSample);
				playing->m_status = PS_Stopped;
			}
		}
	}

	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing) {
				if (audioIsType(playing->m_audioEventRTS, AT_Music)) {
					if (!BitTest(which, AudioAffect_Music)) {
						continue;
					}
				} else {
					if (!BitTest(which, AudioAffect_Speech)) {
						continue;
					}
				}
				AIL_register_stream_callback(playing->m_stream, NULL);
				AIL_pause_stream(playing->m_stream, 1);
				playing->m_status = PS_Stopped;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::pauseAudio( AudioAffect which )
{
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				AIL_stop_sample(playing->m_sample);
			}
		}
	}

	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				AIL_stop_3D_sample(playing->m_3DSample);
			}
		}
	}

	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				if (audioIsType(playing->m_audioEventRTS, AT_Music)) {
					if (!BitTest(which, AudioAffect_Music)) {
						continue;
					}
				} else {
					if (!BitTest(which, AudioAffect_Speech)) {
						continue;
					}
				}

				AIL_pause_stream(playing->m_stream, 1);
			}
		}
	}
	
	//Get rid of PLAY audio requests when pausing audio.
	std::list<AudioRequest*>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); /* empty */) 
	{
		AudioRequest *req = (*ait);
		if( req && req->m_request == AR_Play ) 
		{
			req->deleteInstance();
			ait = m_audioRequests.erase(ait);
		}
		else
		{
			ait++;
		}
	}
}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::resumeAudio( AudioAffect which )
{
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				AIL_resume_sample(playing->m_sample);
			}
		}
	}

	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				AIL_resume_3D_sample(playing->m_3DSample);
			}
		}
	}

	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				if (audioIsType(playing->m_audioEventRTS, AT_Music)) {
					if (!BitTest(which, AudioAffect_Music)) {
						continue;
					}
				} else {
					if (!BitTest(which, AudioAffect_Speech)) {
						continue;
					}
				}
				AIL_pause_stream(playing->m_stream, 0);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::pauseAmbient( Bool shouldPause )
{

}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::playAudioEvent( AudioEventRTS *event, Bool requestStop, Bool nextLoop )
{
#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("MILES (%d) - Processing play request: %d (%s)", TheGameLogic->getFrame(), event->getPlayingHandle(), event->getEventName().str()));
#endif
	if (!event) {
		return;
	}

	const AudioEventInfo *info = event->getAudioEventInfo();
	if (!info) {
		delete event;			// nothing downstream will take it now
		return;
	}
	if (!event->getIsLogicalAudio() && refuseReply(event)) {
		delete event;
		return;
	}

	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing = NULL;

	AudioHandle handleToKill = event->getHandleToKill();

	AsciiString fileToPlay = event->getFilename();
	PlayingAudio *audio = allocatePlayingAudio();
	audio->m_requestStop = requestStop;
	switch(info->m_soundType)
	{
		case AT_Music:
		case AT_Streaming:
		{
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG(("- Stream\n"));
		#endif
			
			if ((info->m_soundType == AT_Streaming) && event->getUninterruptable()) {
				stopAllSpeech();
			}

			Real curVolume = 1.0;
			if (info->m_soundType == AT_Music) {
				curVolume = m_musicVolume;
			} else {
				curVolume = m_speechVolume;
			}
			curVolume *= event->getVolume();

			Bool foundSoundToReplace = false;
			if (handleToKill) {
				for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
					playing = (*it);
					if (!playing) {
						continue;
					}

					if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill) 
					{
						//Release this streaming channel immediately because we are going to play another sound in it's place.
						stopPlayingAudio(playing);
						foundSoundToReplace = true;
						break;
					}
				}
			}

			// One track at a time.  Whatever music is still on the list and not fading out is stopped,
			// so a track started without a stop in front of it cannot play over another.
			if (info->m_soundType == AT_Music && (!handleToKill || foundSoundToReplace)) {
				for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
					playing = (*it);
					if (playing && !playing->m_fade && audioIsType(playing->m_audioEventRTS, AT_Music)) {
						stopPlayingAudio(playing);
					}
				}
			}

			HSTREAM stream;
			if (!handleToKill || foundSoundToReplace) {
				stream = AIL_open_stream(m_digitalHandle, fileToPlay.str(), 0);
			} else {
				stream = NULL;
			}

			// Put this on here, so that the audio event RTS will be cleaned up regardless.
			audio->m_audioEventRTS = event; 
			audio->m_stream = stream;
			audio->m_type = PAT_Stream;

			if (stream) {
				if ((info->m_soundType == AT_Streaming) && event->getUninterruptable()) {
					setDisallowSpeech(TRUE);
	 			}
				AIL_set_stream_volume_pan(stream, curVolume, 0.5f);
				playStream(event, stream);
				{
					CriticalSectionClass::LockClass lock(m_playingStreamsCS);
					m_playingStreams.push_back(audio);
				}
				audio = NULL;
			}
			break;
		}

		case AT_SoundEffect:
		{
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG(("- Sound"));
		#endif

			
			if (event->isPositionalAudio()) {
				// Sounds that are non-global are positional 3-D sounds. Deal with them accordingly
			#ifdef INTENSIVE_AUDIO_DEBUG
				DEBUG_LOG((" Positional"));
			#endif
				Bool foundSoundToReplace = false;
				if (handleToKill) 
				{
					for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
						playing = (*it);
						if (!playing) {
							continue;
						}

						if( playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill ) 
						{
							//Release this 3D sound channel immediately because we are going to play another sound in it's place.
							stopPlayingAudio(playing);
							foundSoundToReplace = true;
							break;
						}
					}
				}
				
				H3DSAMPLE sample3D;
				if( !handleToKill || foundSoundToReplace )
				{
					sample3D = getFirst3DSample( event );
					if( !sample3D )
					{
						//If we don't have an available sample, kill the lowest priority assuming we have one that is lower
						//than the sound we are trying to add. One possibility for strangeness is when an interrupt sound
						//that wants to kill a handle to replace it, it's possible that another request already killed it,
						//in which case we need to attempt to find another sound to kill.
						if( killLowestPrioritySoundImmediately( event ) )
						{
							sample3D = getFirst3DSample( event );
						}
					}
				} 
				else 
				{
					sample3D = NULL;
				}
				// Push it onto the list of playing things
				audio->m_audioEventRTS = event; 
				audio->m_3DSample = sample3D;
				audio->m_file = NULL;
				audio->m_type = PAT_3DSample;
				{
					CriticalSectionClass::LockClass lock(m_playing3DSoundsCS);
					m_playing3DSounds.push_back(audio);
				}

				if (sample3D) {
					audio->m_file = playSample3D(audio);
					m_sound->notifyOf3DSampleStart();
					++m_mixCounts.started3D;
					if (audio->m_file && !nextLoop) {
						duckForBlast(event, sample3D);
					}
				}

				if( !audio->m_file ) 
				{
					m_playing3DSounds.pop_back();
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Killed (no handles available)\n"));
					#endif
				} 
				else 
				{
					audio = NULL;
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Playing.\n"));
					#endif
				}
			} 
			else 
			{
				// UI sounds are always 2-D. All other sounds should be Positional
				// Unit acknowledgement, etc, falls into the UI category of sound.
				Bool foundSoundToReplace = false;
				if (handleToKill) {
					for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
						playing = (*it);
						if (!playing) {
							continue;
						}

						if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill) 
						{
							//Release this 2D sound channel immediately because we are going to play another sound in it's place.
							stopPlayingAudio(playing);
							foundSoundToReplace = true;
							break;
						}
					}
				}

				HSAMPLE sample;
				if( !handleToKill || foundSoundToReplace ) 
				{
					sample = getFirst2DSample(event);
					if( !sample )
					{
						//If we don't have an available sample, kill the lowest priority assuming we have one that is lower
						//than the sound we are trying to add. One possibility for strangeness is when an interrupt sound
						//that wants to kill a handle to replace it, it's possible that another request already killed it,
						//in which case we need to attempt to find another sound to kill.
						if( killLowestPrioritySoundImmediately( event ) )
						{
							sample = getFirst2DSample( event );
						}
					}
					} 
				else 
				{
					sample = NULL;
				}

				// Push it onto the list of playing things
				audio->m_audioEventRTS = event; 
				audio->m_sample = sample;
				audio->m_file = NULL;
				audio->m_type = PAT_Sample;
				{
					CriticalSectionClass::LockClass lock(m_playingSoundsCS);
					m_playingSounds.push_back(audio);
				}

				if (sample) {
					audio->m_file = playSample(event, sample);
					m_sound->notifyOf2DSampleStart();
				}

				if (!audio->m_file) {
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Killed (no handles available)\n"));
					#endif
					m_playingSounds.pop_back();
				} else {
					audio = NULL;
				}

				#ifdef INTENSIVE_AUDIO_DEBUG
					DEBUG_LOG((" Playing.\n"));
				#endif
			}
			break;
		}
	}

	// If we were able to successfully play audio, then we set it to NULL above. (And it will be freed
	// later. However, if audio is non-NULL at this point, then it must be freed.
	if (audio) {
		stopPlayingAudio(audio);
		releasePlayingAudio(audio);
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopAudioEvent( AudioHandle handle )
{
#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("MILES (%d) - Processing stop request: %d\n", TheGameLogic->getFrame(), handle));
#endif

	std::list<PlayingAudio *>::iterator it;
	if ( handle == AHSV_StopTheMusic || handle == AHSV_StopTheMusicFade ) {
		// Every music stream, not the first one found.  A stream that was already told to stop or fade
		// stays on this list until the end of the update, so two track changes inside one update (two
		// logic frames, or two scripts on one frame) used to find the old track a second time and leave
		// the first new one playing under the second.
		for ( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it ) {
			PlayingAudio *audio = (*it);
			if (!audio) {
				continue;
			}

			if( audioIsType(audio->m_audioEventRTS, AT_Music) )
			{
				if( handle == AHSV_StopTheMusicFade )
				{
					audio->m_fade = true;	// processPlayingList moves it across, holding both locks
				}
				else
				{
					stopPlayingAudio( audio );
				}
			}
		}
	}

	//
	// A sound between loops that have a delay is not on any playing list: startNextLoop handed its
	// event back to the request list.  Stopping it there is the only way to stop it at all, or the
	// loop outlives the thing that asked for it.  A one-shot sound effect still plays the one pass
	// it was waiting for, the same as one caught mid-play below; a loop is dropped, as one playing
	// below ends its pass, which matters since the distance delay holds nearly every world loop here.
	// A loop already heard and waiting between passes plays its decay now, as a playing one would.
	//
	std::list<AudioRequest*>::iterator ait;
	for ( ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait ) {
		AudioRequest *req = (*ait);
		if (req && req->m_usePendingEvent && req->m_pendingEvent
				&& req->m_pendingEvent->getPlayingHandle() == handle) {
			const Bool loop = (req->m_pendingEvent->getAudioEventInfo()->m_control & AC_LOOP) != 0;
			if (audioIsType(req->m_pendingEvent, AT_SoundEffect) && !loop) {
				req->m_requestStop = true;
			} else if (loop && req->m_nextLoop && req->m_pendingEvent->getDecayFilename().isNotEmpty()) {
				req->m_pendingEvent->setNextPlayPortion(PP_Decay);
				req->m_pendingEvent->setDelay(0.0f);
				req->m_requestStop = true;
			} else {
				req->deleteInstance();
				m_audioRequests.erase(ait);
			}
			return;
		}
	}

	for ( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it ) {
		PlayingAudio *audio = (*it);
		if (!audio) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
			// found it
			// Ask it to stop; the next processPlayingList sweep does the Miles side and the free.
#if defined(_WIN32)
			InterlockedCompareExchange( (volatile long *)&audio->m_status, PS_Stopping, PS_Playing );
#else
			playingStatusCompareExchange( &audio->m_status, PS_Stopping, PS_Playing );
#endif
			notifyOfAudioCompletion((UnsignedIntPtr)(audio->m_stream), PAT_Stream);
			break;
		}
	}

	for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
		PlayingAudio *audio = (*it);
		if (!audio) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
			// EA's stop for a sound effect: let this pass finish and start no further loop.  Moving
			// it straight to PS_Stopping cut death cries, build-complete barks and weapon tails off
			// the frame they were asked to stop.
			audio->m_requestStop = true;
			endLoopPassNow( audio );
			break;
		}
	}

	for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
		PlayingAudio *audio = (*it);
		if (!audio) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG((" (%s)\n", audio->m_audioEventRTS->getEventName()));
		#endif
			// What is heard is what happened a travel time ago, so a sound that started late by its
			// distance ends late by it too: a missile's flight loop runs on until its boom arrives,
			// rather than going quiet the moment it is seen to hit.  processPlayingList lets it go.
			if (audio->m_stopHeld) {
				break;
			}
			const Real travelMs = audio->m_audioEventRTS->getTravels()
				? getTravelMs(audio->m_audioEventRTS->getCurrentPosition()) : 0.0f;
			if (travelMs > 0.0f) {
				audio->m_stopHeld = true;
				audio->m_stopInMs = travelMs;
				break;
			}
			audio->m_requestStop = true;
			endLoopPassNow( audio );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A loop told to stop used to play its current pass to the end: a destroyed unit's engine hum or
	* a turret that had stopped turning went on for as long as the sample ran.  A one-shot sound keeps
	* its tail (that is what m_requestStop is for), but a loop ends the pass now, the same way the
	* stream branch above does it: completing it with m_requestStop set starts no further loop, plays
	* the decay portion if the sound has one, and otherwise leaves it for the next sweep to free. */
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::endLoopPassNow( PlayingAudio *audio )
{
	const AudioEventInfo *info = audio->m_audioEventRTS->getAudioEventInfo();
	if (!audio->isPlaying() || info == NULL || !(info->m_control & AC_LOOP)) {
		return;
	}

	// stopped first: completing it closes the file the mixer is still reading
	if (audio->m_type == PAT_Sample) {
		AIL_stop_sample(audio->m_sample);
		notifyOfAudioCompletion((UnsignedIntPtr)(audio->m_sample), PAT_Sample);
	} else if (audio->m_type == PAT_3DSample) {
		AIL_stop_3D_sample(audio->m_3DSample);
		notifyOfAudioCompletion((UnsignedIntPtr)(audio->m_3DSample), PAT_3DSample);
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::killAudioEventImmediately( AudioHandle audioEvent )
{
	//First look for it in the request list.
	std::list<AudioRequest*>::iterator ait;
	for( ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ait++ ) 
	{
		AudioRequest *req = (*ait);
		// A play request carries the event, not the handle, so the match is on the event's handle.
		// The two used to share a union and this compared a pointer against a handle instead.
		if( req && req->m_usePendingEvent && req->m_pendingEvent
				&& req->m_pendingEvent->getPlayingHandle() == audioEvent )
		{
			req->deleteInstance();
			ait = m_audioRequests.erase(ait);
			return;
		}
	}

	//Look for matching 3D sound to kill
	std::list<PlayingAudio *>::iterator it;
	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); it++ ) 
	{
		PlayingAudio *audio = (*it);
		if( !audio ) 
		{
			continue;
		}

		if( audio->m_audioEventRTS->getPlayingHandle() == audioEvent ) 
		{
			stopPlayingAudio( audio );
			releasePlayingAudioInListIfStopped( m_playing3DSounds, m_playing3DSoundsCS );
			return;
		}
	}

	//Look for matching 2D sound to kill
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); it++ ) 
	{
		PlayingAudio *audio = (*it);
		if( !audio )
		{
			continue;
		}

		if( audio->m_audioEventRTS->getPlayingHandle() == audioEvent ) 
		{
			stopPlayingAudio( audio );
			releasePlayingAudioInListIfStopped( m_playingSounds, m_playingSoundsCS );
			return;
		}
	}

	//Look for matching steaming sound to kill
	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); it++ ) 
	{
		PlayingAudio *audio = (*it);
		if( !audio )
		{
			continue;
		}

		if( audio->m_audioEventRTS->getPlayingHandle() == audioEvent ) 
		{
			stopPlayingAudio( audio );
			releasePlayingAudioInListIfStopped( m_playingStreams, m_playingStreamsCS );
			return;
		}
	}

}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::pauseAudioEvent( AudioHandle handle )
{
	// pause audio
}

//-------------------------------------------------------------------------------------------------
void *MilesAudioManager::loadFileForRead( AudioEventRTS *eventToLoadFrom )
{
	return m_audioCache->openFile(eventToLoadFrom);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::closeFile( void *fileRead )
{
	m_audioCache->closeFile(fileRead);
}


//-------------------------------------------------------------------------------------------------
PlayingAudio *MilesAudioManager::allocatePlayingAudio( void )
{
	PlayingAudio *aud = NEW PlayingAudio;	// poolify
	aud->m_status = PS_Playing;
	return aud;
}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::releaseMilesHandles( PlayingAudio *release )
{
	switch (release->m_type)
	{
		case PAT_Sample:
		{
			if (release->m_sample) {
				AIL_register_EOS_callback(release->m_sample, NULL);
				AIL_stop_sample(release->m_sample);
				m_availableSamples.push_back(release->m_sample);
				release->m_sample = 0;
			}
			break;
		}
		case PAT_3DSample:
		{
			if (release->m_3DSample) {
				AIL_register_3D_EOS_callback(release->m_3DSample, NULL);
				AIL_end_3D_sample(release->m_3DSample);	// an end, not a stop: the handle is done with
				m_available3DSamples.push_back(release->m_3DSample);
				release->m_3DSample = 0;
			}
			break;
		}
		case PAT_Stream:
		{
			if (release->m_stream) {
				AIL_register_stream_callback(release->m_stream, NULL);
				AIL_close_stream(release->m_stream);
				release->m_stream = 0;
			}
			break;
		}
	}
	release->m_type = PAT_INVALID;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::releasePlayingAudio( PlayingAudio *release )
{
	//
	// A playing event's info can be gone by the time the sound finishes: setEventName drops it
	// (it is not valid for a new name), the drawable custom-ambient path clears it by hand before
	// the info it points at is deleted, and a delayed loop hands the same event back to the
	// request list without one.  EA knew - playAudioEvent and checkForSample both test for it -
	// and then read m_soundType straight through the pointer here, which is an access violation
	// at address 0x90 in a stack that is all audio and names nothing that caused it.
	//
	// Missing info is not a reason to skip the notification: that is the count of how many sample
	// channels are in use, and losing one leaks a channel for the rest of the match until the
	// game runs out and goes quiet.  m_type already says which channel this was, and the two
	// handle tests below already do the real work - the sound type is only excluding streams,
	// and a stream holds neither handle.
	//
	// Which of the three it was has never been pinned down, and a guard that hides the cause is
	// worth less than one that names it: say the event, so the next occurrence arrives with the
	// sound's name attached instead of a register dump.
	if (release->m_file) {
		closeFile( release->m_file );
		release->m_file = NULL;
	}
	if (release->m_cleanupAudioEventRTS) {
		releaseAudioEventRTS(release->m_audioEventRTS);
	}
	delete release;
}

//-------------------------------------------------------------------------------------------------
/** Everything that has to talk to Miles to stop a sound.  Never call this holding one of the list
	* critical sections: Miles calls back into notifyOfAudioCompletion from its own thread and that
	* wants the same lock.  The two interlocked steps make the body run exactly once even if the
	* main thread and the timer thread both arrive here for the same sound. */
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopPlayingAudio( PlayingAudio *release )
{
	// Playing -> Stopping, then Stopping -> Stopped.  Whoever wins the second one does the work.
#if defined(_WIN32)
	InterlockedCompareExchange( (volatile long *)&release->m_status, PS_Stopping, PS_Playing );
#else
	playingStatusCompareExchange( &release->m_status, PS_Stopping, PS_Playing );
#endif
#if defined(_WIN32)
	const long prevStatus = InterlockedCompareExchange( (volatile long *)&release->m_status, PS_Stopped, PS_Stopping );
#else
	const long prevStatus = playingStatusCompareExchange( &release->m_status, PS_Stopped, PS_Stopping );
#endif
	if (prevStatus != PS_Stopping) {
		return;
	}

	const AudioEventInfo *info = release->m_audioEventRTS ? release->m_audioEventRTS->getAudioEventInfo() : NULL;
	if (!info)
	{
		DEBUG_LOG(("AUDIO: releasing '%s' with no event info - see stopPlayingAudio\n",
			release->m_audioEventRTS ? release->m_audioEventRTS->getEventName().str() : "(no event)"));
	}
	if (!info || info->m_soundType == AT_SoundEffect) {
		if (release->m_type == PAT_Sample) {
			if (release->m_sample) {
				m_sound->notifyOf2DSampleCompletion();
			}
		} else {
			if (release->m_3DSample) {
				m_sound->notifyOf3DSampleCompletion();
			}
		}
	}
	releaseMilesHandles(release);	// forces stop of this audio
}

//-------------------------------------------------------------------------------------------------
/** Take everything that has finished stopping off one list and free it, holding that list's lock
	* so the Miles thread is not walking it at the same moment. */
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::releasePlayingAudioInListIfStopped( std::list<PlayingAudio *> &list, CriticalSectionClass &cs )
{
	CriticalSectionClass::LockClass lock(cs);

	std::list<PlayingAudio *>::iterator it;
	for (it = list.begin(); it != list.end(); )
	{
		PlayingAudio *playing = (*it);
		if (playing == NULL || playing->m_status == PS_Stopped)
		{
			if (playing)
				releasePlayingAudio(playing);
			it = list.erase(it);
			continue;
		}
		++it;
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopAllAudioImmediately( void )
{
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	// Stop them all first, with no lock held, because that is the half that calls Miles.
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (playing)
			stopPlayingAudio(playing);
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (playing)
			stopPlayingAudio(playing);
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = (*it);
		if (playing)
			stopPlayingAudio(playing);
	}

	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
		playing = (*it);
		if (playing)
			stopPlayingAudio(playing);
	}

	// Then take them off, holding each list's lock.
	releasePlayingAudioInListIfStopped(m_playingSounds, m_playingSoundsCS);
	releasePlayingAudioInListIfStopped(m_playing3DSounds, m_playing3DSoundsCS);
	releasePlayingAudioInListIfStopped(m_playingStreams, m_playingStreamsCS);
	releasePlayingAudioInListIfStopped(m_fadingAudio, m_fadingAudioCS);


	std::list<HAUDIO>::iterator hit;
	for (hit = m_audioForcePlayed.begin(); hit != m_audioForcePlayed.end(); ++hit) {
		if (*hit) {
			AIL_quick_unload(*hit);
		}
	}

	m_audioForcePlayed.clear();
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::freeAllMilesHandles( void )
{
	// First, we need to ensure that we don't have any sample handles open. To that end, we must stop
	// all of our currently playing audio.
	stopAllAudioImmediately();

	// Walks through the available 2-D and 3-D handles and releases them
	std::list<HSAMPLE>::iterator it;
	for ( it = m_availableSamples.begin(); it != m_availableSamples.end(); /* empty */ ) {
		HSAMPLE sample = *it;
		AIL_release_sample_handle(sample);
		it = m_availableSamples.erase(it);
	}
	m_num2DSamples = 0;
	
	std::list<H3DSAMPLE>::iterator it3D;
	for ( it3D = m_available3DSamples.begin(); it3D != m_available3DSamples.end(); /* empty */ ) {
		H3DSAMPLE sample3D = *it3D;
		AIL_release_3D_sample_handle(sample3D);
		it3D = m_available3DSamples.erase(it3D);
	}
	m_num3DSamples = 0;
	m_numStreams = 0;
}

//-------------------------------------------------------------------------------------------------
HSAMPLE MilesAudioManager::getFirst2DSample( AudioEventRTS *event )
{
	if (m_availableSamples.begin() != m_availableSamples.end()) {
		HSAMPLE retSample = *m_availableSamples.begin();
		m_availableSamples.erase(m_availableSamples.begin());
		return (retSample);
	}

	// Find the first sample of lower priority than my augmented priority that is interruptable and take its handle

	return NULL;
}

//-------------------------------------------------------------------------------------------------
H3DSAMPLE MilesAudioManager::getFirst3DSample( AudioEventRTS *event )
{
	if (m_available3DSamples.begin() != m_available3DSamples.end()) {
		H3DSAMPLE retSample = *m_available3DSamples.begin();
		m_available3DSamples.erase(m_available3DSamples.begin());
		return (retSample);
	}

	// Find the first sample of lower priority than my augmented priority that is interruptable and take its handle
	return NULL;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::adjustPlayingVolume( PlayingAudio *audio )
{
	Real desiredVolume = audio->m_audioEventRTS->getVolume() * audio->m_audioEventRTS->getVolumeShift();
	Real pan;
	if (audio->m_type == PAT_Sample) {
		AIL_sample_volume_pan(audio->m_sample, NULL, &pan);
		AIL_set_sample_volume_pan(audio->m_sample, getVoiceMixedVolume(audio->m_audioEventRTS, m_soundVolume), pan);

	} else if (audio->m_type == PAT_3DSample) {
		applyOcclusion(audio);

	} else if (audio->m_type == PAT_Stream) {
		AIL_stream_volume_pan(audio->m_stream, NULL, &pan);
		if (audioIsType(audio->m_audioEventRTS, AT_Music)) {
			AIL_set_stream_volume_pan(audio->m_stream, getVoiceMixedVolume(audio->m_audioEventRTS, m_musicVolume), pan);
			
		} else {
			AIL_set_stream_volume_pan(audio->m_stream, m_speechVolume * desiredVolume, pan);
			
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopAllSpeech( void )
{
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		playing = (*it);
		if (!playing) {
			continue;
		}

		if (audioIsType(playing->m_audioEventRTS, AT_Streaming)) {
			stopPlayingAudio(playing);
		}
		++it;
	}

	releasePlayingAudioInListIfStopped(m_playingStreams, m_playingStreamsCS);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::initFilters( HSAMPLE sample, const AudioEventRTS *event )
{
	// set the sample volume
	Real volume = getVoiceMixedVolume(event, m_soundVolume);
	AIL_set_sample_volume_pan(sample, volume, 0.5f);

	// pitch shift
	Real pitchShift = event->getPitchShift();
	if (pitchShift == 0.0f) {
		DEBUG_CRASH(("Invalid Pitch shift in sound: '%s'", event->getEventName().str()) );
	} else {
		AIL_set_sample_playback_rate(sample, REAL_TO_INT(AIL_sample_playback_rate(sample) * pitchShift));
	}

	// set up delay filter, if applicable
	if (event->getDelay() > 0.0f) {
		Real value;
		value = event->getDelay();
		AIL_set_sample_processor(sample, DP_FILTER, m_delayFilter);
		AIL_set_filter_sample_preference(sample, "Mono Delay Time", &value);

		value = 0.0;
		AIL_set_filter_sample_preference(sample, "Mono Delay", &value);
		AIL_set_filter_sample_preference(sample, "Mono Delay Mix", &value);		
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::initFilters3D( PlayingAudio *audio, const Coord3D *pos )
{
	H3DSAMPLE sample = audio->m_3DSample;
	const AudioEventRTS *event = audio->m_audioEventRTS;

	// pitch shift
	Real pitchShift = event->getPitchShift();
	if (pitchShift == 0.0f) {
		DEBUG_CRASH(("Invalid Pitch shift in sound: '%s'", event->getEventName().str()) );
	} else {
		AIL_set_3D_sample_playback_rate(sample, REAL_TO_INT(AIL_3D_sample_playback_rate(sample) * pitchShift));
	}
	
	// Low pass filter: a real one now, a ceiling on the cutoff air already lowers with distance
	// (MSS.h's AIL_ex_3D_cutoff), where it used to be a cut in volume.  Every pass of the sound asks
	// again, and a hill in the way muffles it further (updateOcclusion); applyOcclusion sets both, and
	// the volume, since a new file on the handle has put its occlusion back to none.
	audio->m_offscreenOcclusion = (event->getAudioEventInfo()->m_lowPassFreq > 0 && !isOnScreen(pos))
		? 1.0f - event->getAudioEventInfo()->m_lowPassFreq : 0.0f;
	if (!audio->m_tracked) {
		updateOcclusion(audio, pos, TRUE);	// the first pass hears its hill from the start
	}
	applyOcclusion(audio);
	noteReach(audio, pos);
}

//-------------------------------------------------------------------------------------------------
/** What a battle sound starts at, for the AUDIO REACH line: where on the screen it is (the middle
	* half of the width and height, the rest of the screen, off it), its distance from the microphone,
	* its gain before the sliders (distance, the hill in the way, the zoom mix) and its low-pass cutoff
	* (air and occlusion, AIL_ex_3D_cutoff; a snow map's thinner air is not counted). */
void MilesAudioManager::noteReach( const PlayingAudio *playing, const Coord3D *pos ) const
{
	const AudioEventInfo *info = playing->m_audioEventRTS->getAudioEventInfo();
	if (info->m_soundType != AT_SoundEffect || BitTest(info->m_type, ST_VOICE | ST_GLOBAL) || isAmbientSound(info)
			|| !TheGameLogic->isInGame() || TheGameLogic->isInShellGame()) {
		return;
	}
	Int bucket = 2;
	ICoord2D screen;
	if (TheTacticalView->worldToScreenTriReturn(pos, &screen) == View::WTS_INSIDE_FRUSTUM) {
		Int originX, originY;
		TheTacticalView->getOrigin(&originX, &originY);
		const Int width = TheTacticalView->getWidth();
		const Int height = TheTacticalView->getHeight();
		bucket = abs(2 * (screen.x - originX) - width) < width / 2 && abs(2 * (screen.y - originY) - height) < height / 2 ? 0 : 1;
	}
	Coord3D toMicrophone = *pos;
	toMicrophone.sub(&m_listenerPosition);
	const Real distance = toMicrophone.length();
	const AudioSettings *settings = m_audioSettings;
	const Real occlusionGain = 1.0f - (1.0f - settings->m_occlusionVolume) * playing->m_occlusion;
	const Real gain = AIL_ex_3D_distance_gain(distance, info->m_minDistance * getRangeScale(), info->m_maxDistance * getRangeScale(),
		settings->m_rangeVolumeFade) * occlusionGain * m_zoomDetailVolume;
	const Real occlusion = max(playing->m_offscreenOcclusion, playing->m_occlusion * settings->m_occlusionStrength);
	++m_mixCounts.reachCount[bucket];
	m_mixCounts.reachDistance[bucket] += distance;
	m_mixCounts.reachGainDb[bucket] += 20.0f * log10f(max(gain, 0.001f));
	m_mixCounts.reachCutoff[bucket] += AIL_ex_3D_cutoff(distance, settings->m_airAbsorptionDistance * getRangeScale(), occlusion);
}

//-------------------------------------------------------------------------------------------------
/** A 3D sample's range, as the zoom has it (getRangeScale): set as it starts and again every pass, so
	* a loop playing while the camera zooms follows it.  Miles takes the maximum first; EA passed the
	* minimum first, and with the pair backwards every world sound played at full volume out to its
	* cut-off, so far-reaching ambient loops drowned the fighting next to the camera. */
void MilesAudioManager::setSampleDistances( H3DSAMPLE sample3D, const AudioEventRTS *event ) const
{
	const Bool global = BitTest(event->getAudioEventInfo()->m_type, ST_GLOBAL);
	const Real minRange = global ? (Real)m_audioSettings->m_globalMinRange : event->getAudioEventInfo()->m_minDistance;
	const Real maxRange = global ? (Real)m_audioSettings->m_globalMaxRange : event->getAudioEventInfo()->m_maxDistance;
	AIL_set_3D_sample_distances(sample3D, maxRange * getRangeScale(), minRange * getRangeScale());
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::nextMusicTrack( void )
{
	AsciiString trackName;
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			trackName = playing->m_audioEventRTS->getEventName();
		}
	}

	// Stop currently playing music
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);

	trackName = nextTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::prevMusicTrack( void )
{
	AsciiString trackName;
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			trackName = playing->m_audioEventRTS->getEventName();
		}
	}

	// Stop currently playing music 
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);

	trackName = prevTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isMusicPlaying( void ) const
{
	std::list<PlayingAudio *>::const_iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const
{
	if( LogicThread::defersEffects() )
	{
		for( size_t i = 0; i < m_musicLoopsAtSync.size(); ++i )
			if( m_musicLoopsAtSync[ i ].first == trackName && m_musicLoopsAtSync[ i ].second >= numberOfTimes )
				return TRUE;
		return FALSE;
	}

	LOGIC_CENSUS( CENSUS_AUDIO_READ );
	std::list<PlayingAudio *>::const_iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			if (playing->m_audioEventRTS->getEventName() == trackName) {
				if (INFINITE_LOOP_COUNT - AIL_stream_loop_count(playing->m_stream) >= numberOfTimes) {
					return TRUE;
				}
			}
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
AsciiString MilesAudioManager::getMusicTrackName( void ) const
{
	// First check the requests. If there's one there, then report that as the currently playing track.
	std::list<AudioRequest *>::const_iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		if ((*ait)->m_request != AR_Play) {
			continue;
		}

		if (!(*ait)->m_usePendingEvent) {
			continue;
		}

		if (audioIsType((*ait)->m_pendingEvent, AT_Music)) {
			return (*ait)->m_pendingEvent->getEventName();
		}
	}

	std::list<PlayingAudio *>::const_iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			return playing->m_audioEventRTS->getEventName();
		}
	}

	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::openDevice( void )
{
	if (!TheGlobalData->m_audioOn) {
		return;
	}
	
	AIL_set_redist_directory("MSS\\");
	// -wav records from a mix the logic clock moves (updateSoundCapture), so the run plays no sound
	// and needs no audio device.
	if (TheGlobalData->m_wavEndFrame > 0)
		AIL_ex_offline_mix();
	AIL_startup();
	Int retval = 0;

	// AIL_quick_startup should be replaced later with a call to actually pick which device to use, etc
	const AudioSettings *audioSettings = getAudioSettings();
	AIL_ex_set_3D_linear_falloff(audioSettings->m_rangeVolumeFade);
	AIL_ex_set_3D_air(audioSettings->m_speedOfSound, audioSettings->m_airAbsorptionDistance);
	m_selectedSpeakerType = TheAudio->translateSpeakerTypeToUnsignedInt(m_prefSpeaker);

	retval = AIL_quick_startup(audioSettings->m_useDigital, audioSettings->m_useMidi, audioSettings->m_outputRate, audioSettings->m_outputBits, audioSettings->m_outputChannels);
	
	// Quick handles tells us where to store the various devices. For now, we're only interested in the digital handle.
	AIL_quick_handles(&m_digitalHandle, NULL, NULL);
	
	if (retval) {
		buildProviderList();
	} else {
		// if we couldn't initialize any devices, turn sound off (fail silently)
		setOn( false, AudioAffect_All );
	}

	selectProvider(TheAudio->getProviderIndex(m_pref3DProvider));

	// Now that we're all done, update the cached variables so that everything is in sync.
	TheAudio->refreshCachedVariables();

	if (!isValidProvider()) {
		return;
	}

	initDelayFilter();
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::closeDevice( void )
{
	freeAllMilesHandles();
	unselectProvider();
	AIL_shutdown();
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isCurrentlyPlaying( AudioHandle handle )
{
	if( LogicThread::defersEffects() )
		return std::binary_search( m_playingAtSync.begin(), m_playingAtSync.end(), handle ) || isQueuedFromTick( handle );

	LOGIC_CENSUS( CENSUS_AUDIO_READ );
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	// if something is requested, it is also considered playing
	std::list<AudioRequest *>::iterator ait;
	AudioRequest *req = NULL;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		req = *ait;
		if (req && req->m_usePendingEvent && req->m_pendingEvent->getPlayingHandle() == handle) {
			return true;
		}
	}

	// and so is a sound the tick asked for, which waits for the end of the tick to be requested
	return isQueuedFromTick(handle);
}

//-------------------------------------------------------------------------------------------------
/** isCurrentlyPlaying's yes for every handle it would give one to, less the tick's own queue, which
	* it still asks itself; and hasMusicTrackCompleted's loop count for every music stream. */
void MilesAudioManager::captureForLogic( void )
{
	m_playingAtSync.clear();
	m_musicLoopsAtSync.clear();

	const std::list<PlayingAudio *> *lists[] = { &m_playingSounds, &m_playing3DSounds, &m_playingStreams };
	for( Int i = 0; i < 3; ++i )
	{
		for( std::list<PlayingAudio *>::const_iterator it = lists[ i ]->begin(); it != lists[ i ]->end(); ++it )
		{
			PlayingAudio *playing = *it;
			if( playing && playing->isPlaying() )
				m_playingAtSync.push_back( playing->m_audioEventRTS->getPlayingHandle() );
		}
	}
	for( std::list<AudioRequest *>::const_iterator ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait )
	{
		AudioRequest *req = *ait;
		if( req && req->m_usePendingEvent )
			m_playingAtSync.push_back( req->m_pendingEvent->getPlayingHandle() );
	}
	std::sort( m_playingAtSync.begin(), m_playingAtSync.end() );

	for( std::list<PlayingAudio *>::const_iterator it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it )
	{
		PlayingAudio *playing = *it;
		if( playing && audioIsType( playing->m_audioEventRTS, AT_Music ) )
			m_musicLoopsAtSync.push_back( std::make_pair( playing->m_audioEventRTS->getEventName(),
				(Int)( INFINITE_LOOP_COUNT - AIL_stream_loop_count( playing->m_stream ) ) ) );
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::queueAudioCompletion( UnsignedIntPtr audioCompleted, UnsignedInt flags )
{
	CriticalSectionClass::LockClass lock(m_completedAudioCS);
	CompletedAudio completed;
	completed.handle = audioCompleted;
	completed.flags = flags;
	completed.startsAtCompletion = m_audioStarts[audioCompleted];
	m_completedAudio.push_back(completed);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::noteAudioStarted( UnsignedIntPtr handle )
{
	CriticalSectionClass::LockClass lock(m_completedAudioCS);
	++m_audioStarts[handle];
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processCompletedAudio( void )
{
	std::list<CompletedAudio> completedAudio;
	{
		CriticalSectionClass::LockClass lock(m_completedAudioCS);
		completedAudio.swap(m_completedAudio);
	}

	for (std::list<CompletedAudio>::const_iterator it = completedAudio.begin(); it != completedAudio.end(); ++it) {
		Bool restartedSince;
		{
			CriticalSectionClass::LockClass lock(m_completedAudioCS);
			restartedSince = m_audioStarts[it->handle] != it->startsAtCompletion;
		}
		if (!restartedSince) {
			notifyOfAudioCompletion(it->handle, it->flags);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::notifyOfAudioCompletion( UnsignedIntPtr audioCompleted, UnsignedInt flags )
{
	PlayingAudio *playing = findPlayingAudioFrom(audioCompleted, flags);
	if (!playing) {
		DEBUG_CRASH(("Audio has completed playing, but we can't seem to find it. - jkmcd"));
		return;
	}
	
	if (getDisallowSpeech() && audioIsType(playing->m_audioEventRTS, AT_Streaming)) {
		setDisallowSpeech(FALSE);
	}

	if (playing->m_audioEventRTS->getAudioEventInfo()->m_control & AC_LOOP) {
		if (playing->m_audioEventRTS->getNextPlayPortion() == PP_Attack) {
			playing->m_audioEventRTS->setNextPlayPortion(PP_Sound);
		}
		if (playing->m_audioEventRTS->getNextPlayPortion() == PP_Sound) {
			// First, decrease the loop count.
			playing->m_audioEventRTS->decreaseLoopCount();

			// Now, try to start the next loop
			if (startNextLoop(playing)) {
				return;
			}
		}
	}

	playing->m_audioEventRTS->advanceNextPlayPortion();
	if (playing->m_audioEventRTS->getNextPlayPortion() != PP_Done) {
		// Closed and forgotten before the next portion opens, as startNextLoop does: opening it can
		// make the cache evict a file, and closeAnySamplesUsingFile would find this sound still
		// naming the one it just closed, then stop and free it, its event and its handle in the
		// middle of this call, and close that file a second time.
		closeFile(playing->m_file);
		playing->m_file = NULL;
		if (playing->m_type == PAT_Sample) {
			playing->m_file = playSample(playing->m_audioEventRTS, playing->m_sample);
			
			// If we don't have a file now, then we should drop to the stopped status so that 
			// We correctly close this handle.
			if (playing->m_file) {
				return;
			}
		} else if (playing->m_type == PAT_3DSample) {
			playing->m_file = playSample3D(playing);

			// If we don't have a file now, then we should drop to the stopped status so that 
			// We correctly close this handle.
			if (playing->m_file) {
				return;
			}
		} 
	}

	if (playing->m_type == PAT_Stream) {
		if (audioIsType(playing->m_audioEventRTS, AT_Music)) {
			playStream(playing->m_audioEventRTS, playing->m_stream);
			
			return;
		}
	}

	// Ask for the stop rather than declaring it: this runs on the Miles thread, and the half that
	// talks back to Miles and frees the object belongs to the main thread's next sweep.
#if defined(_WIN32)
	InterlockedCompareExchange( (volatile long *)&playing->m_status, PS_Stopping, PS_Playing );
#else
	playingStatusCompareExchange( &playing->m_status, PS_Stopping, PS_Playing );
#endif
}

//-------------------------------------------------------------------------------------------------
PlayingAudio *MilesAudioManager::findPlayingAudioFrom( UnsignedIntPtr audioCompleted, UnsignedInt flags )
{
	// Miles calls in here from its own timer thread when a sample finishes, so every walk of these
	// lists is guarded against the main thread taking an element out from under it.
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	if (flags == PAT_Sample) {
		HSAMPLE sample = (HSAMPLE) audioCompleted;
		CriticalSectionClass::LockClass lock(m_playingSoundsCS);
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->m_sample == sample) {
				return playing;
			}
		}
	}

	if (flags == PAT_3DSample) {
		H3DSAMPLE sample3D = (H3DSAMPLE) audioCompleted;
		CriticalSectionClass::LockClass lock(m_playing3DSoundsCS);
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->m_3DSample == sample3D) {
				return playing;
			}
		}
	}

	if (flags == PAT_Stream) {
		HSTREAM stream = (HSTREAM) audioCompleted;
		{
			CriticalSectionClass::LockClass lock(m_playingStreamsCS);
			for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
				playing = *it;
				if (playing && playing->m_stream == stream) {
					return playing;
				}
			}
		}
		// A stream that is fading has moved lists, and its callback still arrives.
		{
			CriticalSectionClass::LockClass lock(m_fadingAudioCS);
			for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
				playing = *it;
				if (playing && playing->m_stream == stream) {
					return playing;
				}
			}
		}
	}

	return NULL;
}


//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getProviderCount( void ) const
{
	return m_providerCount;
}

//-------------------------------------------------------------------------------------------------
AsciiString MilesAudioManager::getProviderName( UnsignedInt providerNum ) const
{
	if (isOn(AudioAffect_Sound3D) && providerNum < m_providerCount) {
		return m_provider3D[providerNum].name;
	}

	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getProviderIndex( AsciiString providerName ) const
{
	for (UnsignedInt i = 0; i < m_providerCount; ++i) {
		if (providerName == m_provider3D[i].name) {
			return i;
		}
	}

	return PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::selectProvider( UnsignedInt providerNdx )
{
	if (!isOn(AudioAffect_Sound3D)) 
	{
		return;
	}

	if (providerNdx == m_selectedProvider) 
	{
		return;
	}

	if (isValidProvider()) 
	{
		freeAllMilesHandles();
		unselectProvider();
	}

#if defined(_WIN32)
	/* Never runs, on Windows either: AIL_get_DirectSound_info answers NULL in both backends under this
		 manager (XAudio2's and miniaudio's), so useDolby stays FALSE.  Kept on Windows as it was; off
		 Windows there is no DirectSound to name (C4). */
	LPDIRECTSOUND lpDirectSoundInfo;
	AIL_get_DirectSound_info( NULL, (void**)&lpDirectSoundInfo, NULL );
#endif
	Bool useDolby = FALSE;
#if defined(_WIN32)
	if( lpDirectSoundInfo )
	{
		DWORD speakerConfig;
		lpDirectSoundInfo->GetSpeakerConfig( &speakerConfig );
		switch( DSSPEAKER_CONFIG( speakerConfig ) )
		{
			case DSSPEAKER_DIRECTOUT:
				m_selectedSpeakerType = AIL_3D_2_SPEAKER;
				break;
			case DSSPEAKER_MONO:     
				m_selectedSpeakerType = AIL_3D_2_SPEAKER;
				break;
			case DSSPEAKER_STEREO:   
				m_selectedSpeakerType = AIL_3D_2_SPEAKER;
				break;
			case DSSPEAKER_HEADPHONE:
				m_selectedSpeakerType = AIL_3D_HEADPHONE;
				useDolby = TRUE;
				break;
			case DSSPEAKER_QUAD:     
				m_selectedSpeakerType = AIL_3D_4_SPEAKER;
				useDolby = TRUE;
				break;
			case DSSPEAKER_SURROUND:
				m_selectedSpeakerType = AIL_3D_SURROUND;
				useDolby = TRUE;
				break;
			case DSSPEAKER_5POINT1:  
				m_selectedSpeakerType = AIL_3D_51_SPEAKER;
				useDolby = TRUE;
				break;
			case DSSPEAKER_7POINT1:  
				m_selectedSpeakerType = AIL_3D_71_SPEAKER;
				useDolby = TRUE;
				break;
		}
	}
#endif

	Bool success = FALSE;
	if( useDolby )
	{
		providerNdx = getProviderIndex( "Dolby Surround" );
	}
	else
	{
		providerNdx = getProviderIndex( "Miles Fast 2D Positional Audio" );
	}
	success = providerNdx < m_providerCount && AIL_open_3D_provider( m_provider3D[providerNdx].id ) == 0;

	//if (providerNdx < m_providerCount) 
	//{
	//	failed = AIL_open_3D_provider(m_provider3D[providerNdx].id);
	//}



	if( !success ) 
	{
		m_selectedProvider = PROVIDER_ERROR;
		// try to select a failsafe
		providerNdx = getProviderIndex( "Miles Fast 2D Positional Audio" );
		success = providerNdx < m_providerCount && AIL_open_3D_provider( m_provider3D[providerNdx].id ) == 0;
	}

	if ( success )
	{
		m_selectedProvider = providerNdx;
	
		initSamplePools();
		
		createListener();
		setSpeakerType(m_selectedSpeakerType);
		if (TheVideoPlayer) 
		{
			TheVideoPlayer->notifyVideoPlayerOfNewProvider(TRUE);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::unselectProvider( void )
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	if (TheVideoPlayer) {
		TheVideoPlayer->notifyVideoPlayerOfNewProvider(FALSE);
	}
	AIL_close_3D_listener(m_listener);
	m_listener = NULL;

	AIL_close_3D_provider(m_provider3D[m_selectedProvider].id);
	m_lastProvider = m_selectedProvider;

	m_selectedProvider = PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getSelectedProvider( void ) const
{
	return m_selectedProvider;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::setSpeakerType( UnsignedInt speakerType )
{
	if (!isValidProvider()) {
		return;
	}
	
	AIL_set_3D_speaker_type( m_provider3D[m_selectedProvider].id, speakerType );
	m_selectedSpeakerType = speakerType;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getSpeakerType( void )
{
	if (!isValidProvider()) {
		return 0;
	}

	return m_selectedSpeakerType;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getNum2DSamples( void ) const
{
	return m_num2DSamples;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getNum3DSamples( void ) const
{
	return m_num3DSamples;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getNumStreams( void ) const
{
	return m_numStreams;
}

//-------------------------------------------------------------------------------------------------
/** Every sound effect is asked this before it is queued (SoundManager::canPlayNow), and again before
	* it plays when it waited for its distance (checkForSample).  Around EA's per-sound Limit sits the
	* battle mix: a shot folded into an identical one before it, a battle sound over the budget after
	* it.  A sound logic asked for, and waits on, is left exactly as EA had it.  Replies are held back
	* where they start (playAudioEvent), since a unit's second line skips this question altogether. */
Bool MilesAudioManager::doesViolateLimit( AudioEventRTS *event ) const
{
	// Folding is decided once, when the shot is asked for.  On arrival the shot it would fold into
	// is a later one from the same gun, still on its way, and two shots a beat apart became one.
	const Bool mixed = !event->getIsLogicalAudio();
	if (mixed && !m_askingOnArrival && coalesceShot(event)) {
		return true;
	}
	if (doesViolateEventLimit(event)) {
		return true;
	}
	if (mixed && refuseOverBudget(event)) {
		return true;
	}
	if (mixed && !m_askingOnArrival) {
		noteShot(event);
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** A unit's line in the commander's ear (a 2D voice with a speaker), asked as it is about to start.
	* Clicking through a crowd, a factory emptying, a dozen units promoted in one fight each asked for
	* one, and they stacked: a unit already talking skips canPlayNow's limits altogether when its line
	* interrupts, and then played over itself.  The same unit is quiet for ReplyUnitCooldown after it
	* spoke; another waits ReplyGap after the last line began, and no more than ReplyMaxVoices sound at
	* once.  A line held back is dropped, not delayed: by then it answers nothing. */
Bool MilesAudioManager::refuseReply( AudioEventRTS *event ) const
{
	const AudioEventInfo *info = event->getAudioEventInfo();
	const UnsignedInt objectID = event->getObjectID();
	if (!BitTest(info->m_type, ST_VOICE) || event->isPositionalAudio() || objectID == 0
			|| info->m_soundType != AT_SoundEffect) {
		return false;
	}

	const AudioSettings *settings = m_audioSettings;
	const UnsignedInt now = Clock_Milliseconds();
	std::map<UnsignedInt, UnsignedInt>::const_iterator spoke = m_replyAtMs.find(objectID);
	Bool refuse = spoke != m_replyAtMs.end() && settings->m_replyUnitCooldown > 0.0f
		&& (Real)(now - spoke->second) < settings->m_replyUnitCooldown;

	Int lines = 0;
	std::list<PlayingAudio *>::const_iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		AudioEventRTS *other = (*it)->m_audioEventRTS;
		if ((*it)->isPlaying() && other->getObjectID() != 0 && other->getAudioEventInfo()
				&& BitTest(other->getAudioEventInfo()->m_type, ST_VOICE)) {
			++lines;
		}
	}
	if (settings->m_replyMaxVoices > 0 && lines >= settings->m_replyMaxVoices) {
		refuse = true;
	}
	if (lines > 0 && settings->m_replyGap > 0.0f && (Real)(now - m_lastReplyAtMs) < settings->m_replyGap) {
		refuse = true;
	}

	if (refuse) {
		++m_mixCounts.repliesHeld;
		return true;
	}
	m_replyAtMs[objectID] = now;
	m_lastReplyAtMs = now;
	++m_mixCounts.repliesLet;
	return false;
}

//-------------------------------------------------------------------------------------------------
/** The loudness a sound reaches the microphone at, 0 to 1, with the slider taken back out: what the
	* budget compares when it picks the sound that matters least. */
Real MilesAudioManager::getLoudness( AudioEventRTS *event ) const
{
	const Real slider = isAmbientSound( event->getAudioEventInfo() ) ? getAmbientVolume( TRUE ) : m_sound3DVolume;
	return getEffectiveVolume(event) / (slider > 0.0f ? slider : 1.0f);
}

//-------------------------------------------------------------------------------------------------
/** What the battle mix may fold or take the place of: a one-shot world sound that is not a voice,
	* not heard map-wide, not critical and not ambience. */
static Bool isBattleShot( const AudioEventRTS *event )
{
	const AudioEventInfo *info = event->getAudioEventInfo();
	return info != NULL && info->m_soundType == AT_SoundEffect && event->isPositionalAudio()
		&& !BitTest(info->m_type, ST_VOICE | ST_GLOBAL) && info->m_priority != AP_CRITICAL
		&& !BitTest(info->m_control, AC_LOOP) && !AudioManager::isAmbientSound(info);
}

//-------------------------------------------------------------------------------------------------
/** Twenty Rangers firing on one frame asked for twenty identical cracks from one patch of ground, and
	* the voices ran out on the first volley.  A one-shot sound asked for within CoalesceFrames logic
	* frames and CoalesceRadius units of the same sound let through just before folds into that one,
	* which plays CoalesceGainStep louder for each, at most CoalesceMaxGain.  The one let through is
	* found queued (its travel delay) or playing; once it is neither, the next one takes its place. */
Bool MilesAudioManager::coalesceShot( AudioEventRTS *event ) const
{
	const AudioSettings *settings = m_audioSettings;
	if (settings->m_coalesceRadius <= 0.0f || !isBattleShot(event)) {
		return false;
	}
	const Coord3D *pos = event->getCurrentPosition();
	if (pos == NULL) {
		return false;
	}

	const UnsignedInt frame = TheGameLogic->getFrame();
	std::vector<RecentShot>::iterator shot;
	for (shot = m_recentShots.begin(); shot != m_recentShots.end(); ++shot) {
		if (shot->handle == event->getPlayingHandle()) {
			return false;	// one let through, asked again on its way to play
		}
	}
	for (shot = m_recentShots.begin(); shot != m_recentShots.end(); ++shot) {
		if (frame - shot->frame > (UnsignedInt)settings->m_coalesceFrames || shot->name != event->getEventName()) {
			continue;
		}
		Coord3D apart = *pos;
		apart.sub(&shot->position);
		if (apart.length() > settings->m_coalesceRadius) {
			continue;
		}

		// the survivor, queued or playing
		AudioEventRTS *survivor = NULL;
		PlayingAudio *survivorPlaying = NULL;
		std::list<AudioRequest *>::const_iterator ait;
		for (ait = m_audioRequests.begin(); ait != m_audioRequests.end() && !survivor; ++ait) {
			if ((*ait)->m_usePendingEvent && (*ait)->m_pendingEvent->getPlayingHandle() == shot->handle) {
				survivor = (*ait)->m_pendingEvent;
			}
		}
		std::list<PlayingAudio *>::const_iterator it;
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end() && !survivor; ++it) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getPlayingHandle() == shot->handle) {
				survivorPlaying = *it;
				survivor = survivorPlaying->m_audioEventRTS;
			}
		}
		if (!survivor) {
			continue;
		}

		++shot->merged;
		++m_mixCounts.merged;
		survivor->setVolume(shot->volume * AudioMixPolicy::coalescedGain(shot->merged, settings->m_coalesceGainStep,
			settings->m_coalesceMaxGain));
		if (survivorPlaying) {
			const_cast<MilesAudioManager *>(this)->applyOcclusion(survivorPlaying);
		}
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** A shot let through, for the identical ones asked for after it to fold into; the list keeps only
	* the last CoalesceFrames' worth. */
void MilesAudioManager::noteShot( AudioEventRTS *event ) const
{
	const AudioSettings *settings = m_audioSettings;
	if (settings->m_coalesceRadius <= 0.0f || !isBattleShot(event) || event->getCurrentPosition() == NULL) {
		return;
	}
	const UnsignedInt frame = TheGameLogic->getFrame();
	std::vector<RecentShot>::iterator shot;
	for (shot = m_recentShots.begin(); shot != m_recentShots.end(); ) {
		if (shot->handle == event->getPlayingHandle()) {
			return;
		}
		if (frame - shot->frame > (UnsignedInt)settings->m_coalesceFrames || frame < shot->frame) {
			shot = m_recentShots.erase(shot);
			continue;
		}
		++shot;
	}
	RecentShot recent;
	recent.name = event->getEventName();
	recent.position = *event->getCurrentPosition();
	recent.frame = frame;
	recent.handle = event->getPlayingHandle();
	recent.merged = 0;
	recent.volume = event->getVolume();
	m_recentShots.push_back(recent);
}

//-------------------------------------------------------------------------------------------------
/** BattleVoiceBudget one-shot battle sounds at a time.  Past it, a new one takes the place of the one
	* that matters least (AudioMixPolicy::importance: priority, then loudness at the microphone, so the
	* quietest and farthest go first) through EA's own handle-to-kill, or is not played when it matters
	* less than every one of them.  Asked only when the sound is about to play: a sound still waiting
	* for its distance is let into the queue, since what plays by the time it arrives is not known yet.
	* Voices, ambience, loops and the 2D sounds of the interface are outside the budget, so battle noise
	* never takes their voices. */
Bool MilesAudioManager::refuseOverBudget( AudioEventRTS *event ) const
{
	const Int budget = m_audioSettings->m_battleVoiceBudget;
	if (budget <= 0 || !isBattleShot(event) || event->getDelay() >= MSEC_PER_LOGICFRAME_REAL) {
		return false;
	}

	Int count = 0;
	AudioEventRTS *victim = NULL;
	Real victimImportance = 0.0f;
	std::list<PlayingAudio *>::const_iterator it;
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		AudioEventRTS *other = (*it)->m_audioEventRTS;
		if (!(*it)->isPlaying() || !isBattleShot(other)) {
			continue;
		}
		++count;
		const Real otherImportance = AudioMixPolicy::importance(other->getAudioEventInfo()->m_priority, getLoudness(other));
		if (!victim || otherImportance < victimImportance) {
			victim = other;
			victimImportance = otherImportance;
		}
	}
	if (count < budget) {
		return false;
	}

	// EA's limit may already have picked one of the same sound to make room; that keeps the count.
	if (event->getHandleToKill() != 0) {
		return false;
	}
	if (AudioMixPolicy::importance(event->getAudioEventInfo()->m_priority, getLoudness(event)) <= victimImportance) {
		++m_mixCounts.overBudget;
		return true;
	}
	event->setHandleToKill(victim->getPlayingHandle());
	++m_mixCounts.stolen;
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Once a second of logic time in a match: what the battle mix did, for the -wav measurements. */
void MilesAudioManager::logMix( void )
{
	if (!TheGameLogic->isInGame() || TheGameLogic->isInShellGame()) {
		return;
	}
	const UnsignedInt frame = TheGameLogic->getFrame();
	if (frame < m_mixLoggedAt + LOGICFRAMES_PER_SECOND && frame >= m_mixLoggedAt) {
		return;
	}
	m_mixLoggedAt = frame;
	if (m_mixCounts.started3D == 0 && m_mixCounts.repliesLet == 0 && m_mixCounts.repliesHeld == 0 && m_mixCounts.peak3D == 0) {
		return;
	}
	DEBUG_LOG(("AUDIO MIX frame %u: %d 3D starts, peak %u voices (%u battle), %d folded, %d taken, %d over budget, "
		"%d replies let %d held, %d looks, most muffled %.2f\n", frame, m_mixCounts.started3D, m_mixCounts.peak3D,
		m_mixCounts.peakBattle, m_mixCounts.merged, m_mixCounts.stolen, m_mixCounts.overBudget, m_mixCounts.repliesLet,
		m_mixCounts.repliesHeld, m_mixCounts.looks, m_mixCounts.occludedPeak));

	// How far the microphone is from the ground at the middle of the screen's side and top and bottom
	// edges, against which the battle sounds started this second in each part of the screen.
	Int originX, originY;
	TheTacticalView->getOrigin(&originX, &originY);
	const Int width = TheTacticalView->getWidth();
	const Int height = TheTacticalView->getHeight();
	const ICoord2D edges[3] = { { originX, originY + height / 2 }, { originX + width / 2, originY }, { originX + width / 2, originY + height - 1 } };
	Real edgeDistance[3] = { -1.0f, -1.0f, -1.0f };
	for (Int i = 0; i < 3; ++i) {
		Coord3D ground;
		if (TheTacticalView->screenToTerrain(&edges[i], &ground)) {
			ground.sub(&m_listenerPosition);
			edgeDistance[i] = ground.length();
		}
	}
	const char *bucketNames[3] = { "middle", "edge", "off" };
	AsciiString reach;
	for (Int i = 0; i < 3; ++i) {
		const Int n = m_mixCounts.reachCount[i];
		AsciiString part;
		part.format(", %s %d at %.0f units %.1f dB %.0f Hz", bucketNames[i], n, n ? m_mixCounts.reachDistance[i] / n : 0.0f,
			n ? m_mixCounts.reachGainDb[i] / n : 0.0f, n ? m_mixCounts.reachCutoff[i] / n : 0.0f);
		reach.concat(part);
	}
	DEBUG_LOG(("AUDIO REACH frame %u: side %.0f, top %.0f, bottom %.0f units away, zoom out %.2f%s\n", frame,
		edgeDistance[0], edgeDistance[1], edgeDistance[2], getZoomOut(), reach.str()));
	memset(&m_mixCounts, 0, sizeof(m_mixCounts));
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::doesViolateEventLimit( AudioEventRTS *event ) const
{
	Int limit = event->getAudioEventInfo()->m_limit;
	if (limit == 0) {
		return false;
	}

	Int totalCount = 0;
	Int totalRequestCount = 0;

	std::list<PlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				if (totalCount == 0) {
					// This is the oldest audio of this type playing.
					event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				}
				++totalCount;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				if (totalCount == 0) {
					// This is the oldest audio of this type playing.
					event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				}
				++totalCount;
			}
		}
	}
	
	// Also check the request list in case we've requested to play this sound.  A sound asked again as
	// it lands (checkForSample) is on that list itself and is not one more of its kind: counted, it
	// refused itself at a Limit of 1.
	std::list<AudioRequest*>::const_iterator arIt;
	for (arIt = m_audioRequests.begin(); arIt != m_audioRequests.end(); ++arIt) {
		AudioRequest *req = (*arIt);
		if (req == NULL) {
			continue;
		}
		if( req->m_usePendingEvent && req->m_pendingEvent != event )
		{
			if( req->m_pendingEvent->getEventName() == event->getEventName() ) 
			{
				totalRequestCount++;
				totalCount++;
			}
		}
	}

	//If our event is an interrupting type, then normally we would always add it. The exception is when we have requested
	//multiple sounds in the same frame and those requests violate the limit. Because we don't have any "old" sounds to
	//remove in the case of an interrupt, we need to catch it early and prevent the sound from being added if we already
	//reached the limit
	if( event->getAudioEventInfo()->m_control & AC_INTERRUPT )
	{
		if( totalRequestCount < limit )
		{
			Int totalPlayingCount = totalCount - totalRequestCount;
			if( totalRequestCount + totalPlayingCount < limit )
			{
				//We aren't exceeding the actual limit, then clear the kill handle.
				event->setHandleToKill(0);
				return false;
			}

			//We are exceeding the limit - the kill handle will kill the
			//oldest playing sound to enforce the actual limit.
			return false;
		}
	}

	if( totalCount < limit ) 
	{
		event->setHandleToKill(0);
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isPlayingAlready( AudioEventRTS *event ) const
{
	std::list<PlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				return true;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isObjectPlayingVoice( UnsignedInt objID ) const
{
	if (objID == 0) {
		return false;
	}

	std::list<PlayingAudio *>::const_iterator it;
		// 2-D
	for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
		if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getObjectID() == objID && (*it)->m_audioEventRTS->getAudioEventInfo()->m_type & ST_VOICE) {
			return true;
		}
	}

	// 3-D
	for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
		if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getObjectID() == objID && (*it)->m_audioEventRTS->getAudioEventInfo()->m_type & ST_VOICE) {
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
/** A unit's reply is one short line, and in a fight it was lost under the guns.  It plays at the
	* louder of the effects and voice sliders, lifted again on top of that up to full scale.  Nothing
	* else is turned down for it.  EVA and briefing speech is left where the voice slider puts it. */
Real MilesAudioManager::getVoiceMixedVolume( const AudioEventRTS *event, Real sliderVolume ) const
{
	static const Real VOICE_BOOST = 1.6f;

	// A sound can reach here with no info (see audioIsType): v2.0.0 read m_type through it and a
	// looping sound's next pass died at address 0x40 on the XAudio2 service thread.
	const AudioEventInfo *info = event->getAudioEventInfo();
	const Real eventVolume = event->getVolume() * event->getVolumeShift();
	// A reply is heard in the ear, not out on the field.  A world sound flagged as voice - a soldier's
	// death cry - kept its place in the battle before this boost and keeps it now.
	if (info != NULL && (info->m_type & ST_VOICE) && !event->isPositionalAudio()) {
		return min( 1.0f, eventVolume * max( sliderVolume, m_speechVolume ) * VOICE_BOOST );
	}

	// Ambience answers to its own slider, whichever effects slider the caller passed.
	if (isAmbientSound( info )) {
		return eventVolume * getAmbientVolume( event->isPositionalAudio() );
	}

	return eventVolume * sliderVolume;
}

//-------------------------------------------------------------------------------------------------
AudioEventRTS* MilesAudioManager::findLowestPrioritySound( AudioEventRTS *event )
{
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if( priority == AP_LOWEST )
	{
		//If the event we pass in is the lowest priority, don't bother checking because
		//there is nothing lower priority than lowest.
		return NULL;
	}
	AudioEventRTS *lowestPriorityEvent = NULL;
	AudioPriority lowestPriority = priority;

	const std::list<PlayingAudio *> &playingList = event->isPositionalAudio() ? m_playing3DSounds : m_playingSounds;
	std::list<PlayingAudio *>::const_iterator it;
	for( it = playingList.begin(); it != playingList.end(); ++it )
	{
		if( !(*it)->isPlaying() )
		{
			continue;
		}
		AudioEventRTS *itEvent = (*it)->m_audioEventRTS;
		AudioPriority itPriority = itEvent->getAudioEventInfo()->m_priority;
		if( itPriority < lowestPriority )
		{
			lowestPriorityEvent = itEvent;
			lowestPriority = itPriority;
			if( lowestPriority == AP_LOWEST )
			{
				return lowestPriorityEvent;
			}
		}
	}
	return lowestPriorityEvent;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isPlayingLowerPriority( AudioEventRTS *event ) const
{
	//We don't actually want to do anything to this CONST function. Remember, we're
	//just checking to see if there is a lower priority sound.
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if( priority == AP_LOWEST )
	{
		//If the event we pass in is the lowest priority, don't bother checking because
		//there is nothing lower priority than lowest.
		return false;
	}
	std::list<PlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getAudioEventInfo()->m_priority < priority) {
				//event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				return true;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getAudioEventInfo()->m_priority < priority) {
				//event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::killLowestPrioritySoundImmediately( AudioEventRTS *event )
{
	//Actually, we want to kill the LOWEST PRIORITY SOUND, not the first "lower" priority
	//sound we find, because it could easily be 
	AudioEventRTS *lowestPriorityEvent = findLowestPrioritySound( event );
	if( lowestPriorityEvent )
	{
		std::list<PlayingAudio *>::iterator it;
		if( event->isPositionalAudio() )
		{
			for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) 
			{
				PlayingAudio *playing = (*it);
				if( !playing ) 
				{
					continue;
				}

				if( playing->isPlaying() && playing->m_audioEventRTS == lowestPriorityEvent )
				{
					//Release this 3D sound channel immediately because we are going to play another sound in it's place.
					stopPlayingAudio( playing );
					releasePlayingAudioInListIfStopped( m_playing3DSounds, m_playing3DSoundsCS );
					return TRUE;
				}
			}
		}
		else
		{
			for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it )
			{
				PlayingAudio *playing = (*it);
				if( !playing )
				{
					continue;
				}

				if( playing->isPlaying() && playing->m_audioEventRTS == lowestPriorityEvent )
				{
					//Release this 2D sound channel immediately because we are going to play another sound in it's place.
					stopPlayingAudio( playing );
					releasePlayingAudioInListIfStopped( m_playingSounds, m_playingSoundsCS );
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume)
{
	Real pan;
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			AIL_sample_volume_pan(playing->m_sample, NULL, &pan);
			AIL_set_sample_volume_pan(playing->m_sample, desiredVolume, pan);
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			AIL_set_3D_sample_volume(playing->m_3DSample, desiredVolume);
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			AIL_stream_volume_pan(playing->m_stream, NULL, &pan);
			AIL_set_stream_volume_pan(playing->m_stream, desiredVolume, pan);
		}
	}
}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::removePlayingAudio( AsciiString eventName )
{
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playingSounds, m_playingSoundsCS );

	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playing3DSounds, m_playing3DSoundsCS );

	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playingStreams, m_playingStreamsCS );
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::removeAllDisabledAudio()
{
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playingSounds, m_playingSoundsCS );

	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playing3DSounds, m_playing3DSoundsCS );

	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playingStreams, m_playingStreamsCS );
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processRequestList( void )
{
	std::list<AudioRequest*>::iterator it;
	for (it = m_audioRequests.begin(); it != m_audioRequests.end(); /* empty */) {
		AudioRequest *req = (*it);
		if (req == NULL) {
			continue;
		}

		if (!shouldProcessRequestThisFrame(req)) {
			adjustRequest(req);
			++it;
			continue;
		}

		if (!req->m_requiresCheckForSample || checkForSample(req)) {
			processRequest(req);
		}
		req->deleteInstance();
		it = m_audioRequests.erase(it);
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processPlayingList( void )
{
	// Two passes.  The first one talks to Miles and holds no lock; it only ever writes a status.
	// The second takes the finished ones off the lists, holding each list's own lock, and calls
	// nothing in Miles.  The whole point of the split is that the Miles timer thread walks these
	// same lists from notifyOfAudioCompletion.
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = (*it);
		if (!playing)
			continue;

		if (playing->m_status == PS_Stopping)
		{
			stopPlayingAudio( playing );
			continue;
		}

		if (playing->m_status == PS_Stopped)
			continue;

		if (m_volumeHasChanged)
		{
			adjustPlayingVolume(playing);
		}
	}

	// Occlusion looks along a few lines a pass, taking the playing sounds in turn (updateOcclusion).
	const UnsignedInt count3D = (UnsignedInt)m_playing3DSounds.size();
	const UnsignedInt looksThisPass = (UnsignedInt)max(m_audioSettings->m_occlusionRays, 0);
	UnsignedInt index3D = 0;
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it)
	{
		playing = (*it);
		const Bool lookAgain = (index3D++ + count3D - m_occlusionCursor % count3D) % count3D < looksThisPass;
		if (!playing)
			continue;

		if (playing->m_status == PS_Stopping)
		{
			stopPlayingAudio( playing );
			continue;
		}

		if (playing->m_status == PS_Stopped)
		{
		}
		else
		{
			if (m_volumeHasChanged)
			{
				adjustPlayingVolume(playing);
			}

			// a held stop (stopAudioEvent) goes through once its travel time has passed
			if (playing->m_stopHeld && playing->m_stopInMs > 0.0f)
			{
				playing->m_stopInMs -= m_clockElapsedMs;
				if (playing->m_stopInMs <= 0.0f)
				{
					playing->m_requestStop = true;
					endLoopPassNow( playing );
				}
			}

			const Coord3D *pos = getCurrentPositionFromEvent(playing->m_audioEventRTS);
			if (pos)
			{
				if( playing->m_audioEventRTS->isDead() )
				{
					stopAudioEvent( playing->m_audioEventRTS->getPlayingHandle() );
					continue;
				}
				else
				{
					Real volForConsideration = getEffectiveVolume(playing->m_audioEventRTS);
					// the slider is taken back out: a sound is culled by distance, not by how far a slider is down
					const Real slider = isAmbientSound( playing->m_audioEventRTS->getAudioEventInfo() ) ? getAmbientVolume( TRUE ) : m_sound3DVolume;
					volForConsideration /= (slider > 0.0f ? slider : 1.0f);
					Bool playAnyways = BitTest( playing->m_audioEventRTS->getAudioEventInfo()->m_type, ST_GLOBAL) || playing->m_audioEventRTS->getAudioEventInfo()->m_priority == AP_CRITICAL;
					if( volForConsideration < m_audioSettings->m_minVolume && !playAnyways )
					{
						// don't want to get an additional callback for this sample
						AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
						stopPlayingAudio( playing );
						continue;
					}
					// canPlayNow keeps a shrouded sound from starting in the fog, but a loop that started
					// in the open played on after its source flew into the fog: an enemy Helix or Comanche
					// could be heard, and followed, where it could not be seen.  Stopped like one out of
					// range, and the drawable starts it again once the spot is clear.
					else if( !playAnyways
						&& BitTest( playing->m_audioEventRTS->getAudioEventInfo()->m_type, ST_SHROUDED )
						&& playing->m_audioEventRTS->getAudioEventInfo()->isPermanentSound()
						&& ThePartitionManager->getShroudStatusForPlayer( TheObserverCamera.getShroudPlayerIndex(), pos ) != CELLSHROUD_CLEAR )
					{
						AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
						stopPlayingAudio( playing );
						continue;
					}
					else
					{
						updateOcclusion( playing, pos, lookAgain );
						trackVelocity( playing, pos );
						setSampleDistances( playing->m_3DSample, playing->m_audioEventRTS );
						Real x = pos->x;
						Real y = pos->y;
						Real z = pos->z;
						AIL_set_3D_position( playing->m_3DSample, x, y, z );
					}
				}
			} 
			else 
			{
				AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
				stopPlayingAudio( playing );
				continue;
			}
		}
	}

	m_occlusionCursor += looksThisPass;

	// what the AUDIO MIX line reports: the most 3D voices at once, and how many of them were battle
	UnsignedInt playing3D = 0;
	UnsignedInt playingBattle = 0;
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = (*it);
		if (playing && playing->isPlaying()) {
			++playing3D;
			const AudioEventInfo *info = playing->m_audioEventRTS->getAudioEventInfo();
			if (info && !BitTest(info->m_type, ST_VOICE | ST_GLOBAL) && !isAmbientSound(info)) {
				++playingBattle;
			}
			m_mixCounts.occludedPeak = max(m_mixCounts.occludedPeak, playing->m_occlusion);
		}
	}
	m_mixCounts.peak3D = max(m_mixCounts.peak3D, playing3D);
	m_mixCounts.peakBattle = max(m_mixCounts.peakBattle, playingBattle);

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = (*it);
		if (!playing)
			continue;

		if (playing->m_status == PS_Stopping)
		{
			stopPlayingAudio( playing );
			continue;
		}

		if (playing->m_status == PS_Stopped)
			continue;

		if (m_volumeHasChanged)
		{
			adjustPlayingVolume(playing);
		}
	}

	if (m_volumeHasChanged) {
		m_volumeHasChanged = false;
		TheVideoPlayer->setVolume(getVolume(AudioAffect_Speech));
	}

	//
	// Second pass: take the finished ones off, holding each list's own lock.  Nothing here calls
	// into Miles, which is what makes it safe to hold the lock.
	//
	releasePlayingAudioInListIfStopped(m_playingSounds, m_playingSoundsCS);
	releasePlayingAudioInListIfStopped(m_playing3DSounds, m_playing3DSoundsCS);
	releasePlayingAudioInListIfStopped(m_playingStreams, m_playingStreamsCS);

	//
	// And move anything that asked to fade across, holding both lists.
	//
	{
		CriticalSectionClass::LockClass streamLock(m_playingStreamsCS);
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
			playing = (*it);
			if (playing && playing->m_fade)
			{
				{
					CriticalSectionClass::LockClass fadeLock(m_fadingAudioCS);
					m_fadingAudio.push_back(playing);
				}
				it = m_playingStreams.erase(it);
				continue;
			}
			++it;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processFadingList( void )
{
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	// Same two passes as processPlayingList, for the same reason.
	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
		playing = *it;
		if (!playing) {
			continue;
		}

		if (playing->m_framesFaded >= getAudioSettings()->m_fadeAudioFrames) {
			stopPlayingAudio( playing );
			continue;
		}

		if (playing->m_status != PS_Playing) {
			stopPlayingAudio( playing );
			continue;
		}

		// TimeToFadeAudio is logic frames; a pass is a render frame.  In a match the audio clock says how
		// far the logic went (none while paused, so a crossfade holds with MusicManager's rise), and
		// out of one a pass counts as a frame, as it always did.
		playing->m_framesFaded += m_clockElapsedMs / MSEC_PER_LOGICFRAME_REAL;
		Real volume = getEffectiveVolume(playing->m_audioEventRTS);
		volume *= (1.0f - 1.0f * playing->m_framesFaded / getAudioSettings()->m_fadeAudioFrames);

		switch(playing->m_type)
		{
			case PAT_Sample:
			{
				AIL_set_sample_volume_pan(playing->m_sample, volume, 0.5f);
				break;
			}

			case PAT_3DSample:
			{
				// Not volume: that carries the distance falloff, which the backend applies again.
				AIL_set_3D_sample_volume(playing->m_3DSample, getVoiceMixedVolume(playing->m_audioEventRTS, m_sound3DVolume)
					* (1.0f - 1.0f * playing->m_framesFaded / getAudioSettings()->m_fadeAudioFrames));
				break;
			}
			
			case PAT_Stream:
			{
				AIL_set_stream_volume_pan(playing->m_stream, volume, 0.5f);
				break;
			}
			
		}
	}

	releasePlayingAudioInListIfStopped(m_fadingAudio, m_fadingAudioCS);
}


//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::shouldProcessRequestThisFrame( AudioRequest *req ) const
{
	if (!req->m_usePendingEvent) {
		return true;
	}

	if (req->m_pendingEvent->getDelay() < MSEC_PER_LOGICFRAME_REAL) {
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::adjustRequest( AudioRequest *req )
{
	if (!req->m_usePendingEvent) {
		return;
	}

	// A world sound counts logic time, not a frame's worth a pass: at 120 fps that ran every delay
	// twice as fast as the picture, and a sound held back by its distance has to land with what is
	// seen.  Everything else keeps a frame a pass, so the pause menu's sounds still play while the
	// logic clock stands.
	req->m_pendingEvent->decrementDelay(req->m_pendingEvent->isPositionalAudio() ? m_clockElapsedMs : MSEC_PER_LOGICFRAME_REAL);
	req->m_requiresCheckForSample = true;
}

//-------------------------------------------------------------------------------------------------
/** m_clockElapsedMs: the logic time since the last update, which is nothing on a pass drawn between
	* two logic frames and several frames' worth on a pass that caught up.  Out of a game the logic
	* clock stands still, and a pass counts as one frame, as every pass used to. */
void MilesAudioManager::advanceAudioClock( void )
{
	if (!TheGameLogic->isInGame())
	{
		m_clockElapsedMs = MSEC_PER_LOGICFRAME_REAL;
		m_clockMovedAt = Clock_Milliseconds();
		return;
	}

	// a new match starts its clock at nought again, and the first pass of it counts nothing
	const UnsignedInt frame = TheGameLogic->getFrame();
	m_clockElapsedMs = frame > m_clockFrame ? (Real)(frame - m_clockFrame) * MSEC_PER_LOGICFRAME_REAL : 0.0f;
	m_clockFrame = frame;
	if (m_clockElapsedMs > 0.0f)
		m_clockMovedAt = Clock_Milliseconds();
}

//-------------------------------------------------------------------------------------------------
/** Doppler's velocity: how far the source moved over the logic time since the last pass the clock
	* moved on.  A drawable's position is the smoothed one, read at whatever point between two logic
	* frames the pass fell, so each reading only takes the velocity half way there, which keeps a flyby's
	* pitch from warbling.  The backend takes the speed towards the microphone (AIL_ex_doppler_ratio). */
void MilesAudioManager::trackVelocity( PlayingAudio *playing, const Coord3D *pos )
{
	if (m_clockElapsedMs <= 0.0f)
	{
		// A pass between two logic frames keeps the velocity.  A clock standing longer than a few frames
		// is a pause or a network stall, where nothing moves and no pitch may hang.
		const UnsignedInt CLOCK_STALL_MS = 100;
		if (playing->m_tracked && Clock_Milliseconds() - m_clockMovedAt > CLOCK_STALL_MS)
		{
			playing->m_velocity.zero();
			AIL_set_3D_velocity_vector( playing->m_3DSample, 0.0f, 0.0f, 0.0f );
		}
		return;
	}

	if (playing->m_tracked)
	{
		const Real perSecond = MSEC_PER_SECOND / m_clockElapsedMs;
		playing->m_velocity.x += 0.5f * ((pos->x - playing->m_lastPosition.x) * perSecond - playing->m_velocity.x);
		playing->m_velocity.y += 0.5f * ((pos->y - playing->m_lastPosition.y) * perSecond - playing->m_velocity.y);
		playing->m_velocity.z += 0.5f * ((pos->z - playing->m_lastPosition.z) * perSecond - playing->m_velocity.z);
		// The faster a unit, the further its game speed is below the real machine's: a tank is drawn at
		// about half its real speed, a Raptor at a tenth.  Past DopplerFastSpeed the scale grows with the
		// speed, so a jet's pass bends as far as a helicopter's does and further.
		const Real fastSpeed = m_audioSettings->m_dopplerFastSpeed;
		const Real speed = playing->m_velocity.length();
		const Real scale = m_audioSettings->m_dopplerScale * (fastSpeed > 0.0f && speed > fastSpeed ? speed / fastSpeed : 1.0f);
		AIL_set_3D_velocity_vector( playing->m_3DSample, playing->m_velocity.x * scale, playing->m_velocity.y * scale,
			playing->m_velocity.z * scale );
	}
	playing->m_lastPosition = *pos;
	playing->m_tracked = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A world sound behind a hill or a building is muffled: the line from the microphone to it is looked
	* along (lookForOcclusion), and the deeper ground or a roof stands in it, the lower the backend's low
	* pass closes (the occlusion it already takes for EA's off-screen sounds, AIL_ex_3D_cutoff) and the
	* quieter it plays.  A sound's first pass is looked at as it starts; after that OcclusionRaysPerFrame
	* playing sounds are looked at again a pass, in turn, and the muffle moves towards what they saw over
	* OcclusionTime of logic time, so a unit walking out from behind a ridge opens up rather than
	* jumping.  Voices and sounds heard map-wide are never muffled.  Only the client's view of the
	* ground and the drawables is read. */
void MilesAudioManager::updateOcclusion( PlayingAudio *playing, const Coord3D *pos, Bool look )
{
	const AudioEventInfo *info = playing->m_audioEventRTS->getAudioEventInfo();
	if (m_audioSettings->m_occlusionStrength <= 0.0f || info == NULL || BitTest(info->m_type, ST_VOICE | ST_GLOBAL)) {
		// occlusion switched off (Legacy sound) under a sound it had muffled
		if (playing->m_occlusion > 0.0f) {
			playing->m_occlusion = 0.0f;
			applyOcclusion(playing);
		}
		return;
	}
	if (look) {
		playing->m_occlusionTarget = lookForOcclusion(pos);
	}
	const Real before = playing->m_occlusion;
	playing->m_occlusion = playing->m_tracked
		? AudioMixPolicy::approach(before, playing->m_occlusionTarget, m_clockElapsedMs, m_audioSettings->m_occlusionTime)
		: playing->m_occlusionTarget;
	if (playing->m_occlusion != before) {
		applyOcclusion(playing);
	}
}

//-------------------------------------------------------------------------------------------------
/** The low pass and the volume of a 3D sound: EA's off-screen low pass or the muffle, whichever is
	* the closer, and the sliders' volume taken down by the muffle. */
void MilesAudioManager::applyOcclusion( PlayingAudio *playing )
{
	const AudioSettings *settings = m_audioSettings;
	const Real muffle = playing->m_occlusion * settings->m_occlusionStrength;
	AIL_set_3D_sample_occlusion(playing->m_3DSample, max(playing->m_offscreenOcclusion, muffle));
	const Real gain = 1.0f - (1.0f - settings->m_occlusionVolume) * playing->m_occlusion;
	AIL_set_3D_sample_volume(playing->m_3DSample, getVoiceMixedVolume(playing->m_audioEventRTS, m_sound3DVolume) * gain);
}

//-------------------------------------------------------------------------------------------------
/** How much of the full muffle the line from the microphone to pos earns, 0 to 1. */
Real MilesAudioManager::lookForOcclusion( const Coord3D *pos ) const
{
	const Real LIFT = 10.0f;		// a sound comes off a unit, not out of the ground under it
	const Real STEP = 20.0f;		// a sample of the line every cell of the blockers' grid
	const Real MARGIN = 25.0f;	// the ground under either end is not a hill between them
	const Int MAX_SAMPLES = 48;
	++m_mixCounts.looks;
	// A sound from a building, or a microphone over one, starts on its roof: its own roof is not in the way.
	const Real sourceZ = max(pos->z, getBlockerHeight(pos->x, pos->y)) + LIFT;
	const Real listenerZ = max(m_listenerPosition.z, getBlockerHeight(m_listenerPosition.x, m_listenerPosition.y) + LIFT);
	const Real depth = AudioMixPolicy::rayObstruction(m_listenerPosition.x, m_listenerPosition.y, listenerZ,
		pos->x, pos->y, sourceZ, STEP, MARGIN, MAX_SAMPLES,
		[this]( Real x, Real y ) { return getBlockerHeight( x, y ); });
	return AudioMixPolicy::occlusionAmount(depth, m_audioSettings->m_occlusionDepth);
}

//-------------------------------------------------------------------------------------------------
static const Real BLOCKER_CELL = 20.0f;
static const Real NO_ROOF = -100000.0f;

/** The ground, or the roof over it where a building stands. */
Real MilesAudioManager::getBlockerHeight( Real x, Real y ) const
{
	const Real ground = TheTerrainLogic->getGroundHeight( x, y );
	if (m_blockers.empty()) {
		return ground;
	}
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	const Int column = (Int)floorf((x - extent.lo.x) / BLOCKER_CELL);
	const Int row = (Int)floorf((y - extent.lo.y) / BLOCKER_CELL);
	if (column < 0 || row < 0 || column >= m_blockerColumns || row >= m_blockerRows) {
		return ground;
	}
	return max(ground, m_blockers[row * m_blockerColumns + column]);
}

//-------------------------------------------------------------------------------------------------
/** The roofs, once a second of logic time: every structure the client draws, over the cells its
	* footprint covers, at its top.  Bridges are left out (a sound under one is not behind it), and so is
	* a building the local player has never seen, whose muffle would give it away. */
void MilesAudioManager::buildBlockers( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if (m_audioSettings->m_occlusionStrength <= 0.0f
			|| (!m_blockers.empty() && frame >= m_blockersBuiltAt && frame < m_blockersBuiltAt + LOGICFRAMES_PER_SECOND)) {
		return;
	}
	m_blockersBuiltAt = frame;

	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	m_blockerColumns = (Int)ceilf(extent.width() / BLOCKER_CELL);
	m_blockerRows = (Int)ceilf(extent.height() / BLOCKER_CELL);
	m_blockers.assign((size_t)(m_blockerColumns * m_blockerRows), NO_ROOF);

	const Int playerIndex = TheObserverCamera.getShroudPlayerIndex();
	for (Drawable *draw = TheGameClient->firstDrawable(); draw; draw = draw->getNextDrawable())
	{
		const ThingTemplate *thing = draw->getTemplate();
		if (!thing->isKindOf(KINDOF_STRUCTURE) || thing->isKindOf(KINDOF_BRIDGE) || thing->isKindOf(KINDOF_BRIDGE_TOWER)) {
			continue;
		}
		const Coord3D *pos = draw->getPosition();
		if (ThePartitionManager->getShroudStatusForPlayer(playerIndex, pos) == CELLSHROUD_SHROUDED) {
			continue;
		}
		const GeometryInfo &geometry = thing->getTemplateGeometryInfo();
		const Real roof = pos->z + geometry.getMaxHeightAbovePosition();
		const Real reach = geometry.getBoundingCircleRadius();
		const Real angle = draw->getOrientation();
		const Real c = cosf(angle);
		const Real s = sinf(angle);
		const Int firstColumn = max(0, (Int)floorf((pos->x - reach - extent.lo.x) / BLOCKER_CELL));
		const Int lastColumn = min(m_blockerColumns - 1, (Int)floorf((pos->x + reach - extent.lo.x) / BLOCKER_CELL));
		const Int firstRow = max(0, (Int)floorf((pos->y - reach - extent.lo.y) / BLOCKER_CELL));
		const Int lastRow = min(m_blockerRows - 1, (Int)floorf((pos->y + reach - extent.lo.y) / BLOCKER_CELL));
		for (Int row = firstRow; row <= lastRow; ++row)
		{
			for (Int column = firstColumn; column <= lastColumn; ++column)
			{
				// the cell's centre, in the building's own frame
				const Real dx = extent.lo.x + (column + 0.5f) * BLOCKER_CELL - pos->x;
				const Real dy = extent.lo.y + (row + 0.5f) * BLOCKER_CELL - pos->y;
				const Bool inside = geometry.getGeomType() == GEOMETRY_BOX
					? fabsf(dx * c + dy * s) <= geometry.getMajorRadius() && fabsf(-dx * s + dy * c) <= geometry.getMinorRadius()
					: dx * dx + dy * dy <= geometry.getMajorRadius() * geometry.getMajorRadius();
				Real &cell = m_blockers[row * m_blockerColumns + column];
				if (inside && roof > cell) {
					cell = roof;
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The processing v3.4.0 put between a sound and the speakers, as AudioSettings.ini gives it (`from`)
	* or, with `from` NULL, each stage at the value its own comment in AudioSettings.h calls none. */
static void setSoundProcessing( AudioSettings *to, const AudioSettings *from )
{
	to->m_speedOfSound = from ? from->m_speedOfSound : 0.0f;							// travel delay and doppler
	to->m_maxSoundDelay = from ? from->m_maxSoundDelay : 0.0f;
	to->m_dopplerScale = from ? from->m_dopplerScale : 0.0f;
	to->m_airAbsorptionDistance = from ? from->m_airAbsorptionDistance : 0.0f;	// the air's low pass
	to->m_reverbLevel = from ? from->m_reverbLevel : 0.0f;								// the map's room
	to->m_duckVolume = from ? from->m_duckVolume : 1.0f;
	to->m_replyUnitCooldown = from ? from->m_replyUnitCooldown : 0.0f;
	to->m_replyGap = from ? from->m_replyGap : 0.0f;
	to->m_replyMaxVoices = from ? from->m_replyMaxVoices : 0;
	to->m_battleVoiceBudget = from ? from->m_battleVoiceBudget : 0;
	to->m_coalesceRadius = from ? from->m_coalesceRadius : 0.0f;							// shot folding
	to->m_occlusionStrength = from ? from->m_occlusionStrength : 0.0f;
	to->m_occlusionVolume = from ? from->m_occlusionVolume : 1.0f;
	to->m_zoomMixDetailVolume = from ? from->m_zoomMixDetailVolume : 1.0f;
	to->m_ambienceBedSnow = from ? from->m_ambienceBedSnow : AsciiString::TheEmptyString;
	to->m_ambienceBedTown = from ? from->m_ambienceBedTown : AsciiString::TheEmptyString;
	to->m_ambienceBedDesert = from ? from->m_ambienceBedDesert : AsciiString::TheEmptyString;
	to->m_ambienceBedField = from ? from->m_ambienceBedField : AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
/** Legacy sound (LegacySound in Options.ini, on Options > Audio): retail's mix, every stage above off
	* and the backend's limiter with them, and MusicManager::engaged leaving the music to the scripts.
	* Taken up the pass after the option changes.  The room and the bed are set again from the map on
	* this same pass (setRoomFromMap), the bed playing now stopped; a sound already muffled opens up on
	* its next position update (updateOcclusion). */
void MilesAudioManager::applyLegacySound( void )
{
	if (m_legacySound == TheGlobalData->m_legacySound)
		return;
	m_legacySound = TheGlobalData->m_legacySound;
	setSoundProcessing( m_audioSettings, m_legacySound ? NULL : &m_engineSettings );
	AIL_ex_set_limiter( !m_legacySound );
	m_roomAbsorption = m_audioSettings->m_airAbsorptionDistance;
	m_airScale = 0.0f;
	removeAudioEvent( m_bedHandle );
	m_bedEvent.clear();
	m_roomMap.clear();
	DEBUG_LOG(("AUDIO: legacy sound %s: delay %.0f ms, doppler %.1f, air %.0f, room %.2f, duck %.2f, occlusion %.2f, "
		"budget %d, folding %.0f, replies %d, zoom mix %.2f, limiter %s\n", m_legacySound ? "on" : "off",
		m_audioSettings->m_maxSoundDelay, m_audioSettings->m_dopplerScale, m_audioSettings->m_airAbsorptionDistance,
		m_audioSettings->m_reverbLevel, m_audioSettings->m_duckVolume, m_audioSettings->m_occlusionStrength,
		m_audioSettings->m_battleVoiceBudget, m_audioSettings->m_coalesceRadius, m_audioSettings->m_replyMaxVoices,
		m_audioSettings->m_zoomMixDetailVolume, m_legacySound ? "off" : "on"));
}

//-------------------------------------------------------------------------------------------------
/** The room the map's world sounds send into (AIL_ex_set_reverb), from the map itself: how much of
	* its playable ground the placed structures cover.  Supply docks are left out, being on every map at
	* a third to a full percent of it.  The shipped maps measured that way run from about 0.1% (Twilight
	* Flame, Tournament Tundra) to near 3% (Tournament Urban, El Scorcho), and the room goes from open
	* ground at 0.3%, a loud late slap and a long thin tail, to a town at 1.9%, a strong early echo off
	* the walls and a brighter tail.  Snow leaves a third of the send, no echo, a short dead tail and
	* air that takes the top end off sooner.  The object list is the map file's, which the terrain
	* keeps until the next map, so nothing in logic is asked. */
void MilesAudioManager::setRoomFromMap( void )
{
	Real built = 0.0f;
	for (MapObject *mapObject = MapObject::getFirstMapObject(); mapObject; mapObject = mapObject->getNext())
	{
		const ThingTemplate *thing = mapObject->getThingTemplate();
		// waypoints, lights and other markers have no template
		if (thing && thing->isKindOf(KINDOF_STRUCTURE) && !thing->isKindOf(KINDOF_SUPPLY_SOURCE))
			built += thing->getTemplateGeometryInfo().getFootprintArea();
	}
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	const Real cover = built / (extent.width() * extent.height());

	const Real OPEN_COVER = 0.003f;
	const Real TOWN_COVER = 0.019f;
	Real town = (cover - OPEN_COVER) / (TOWN_COVER - OPEN_COVER);
	town = town < 0.0f ? 0.0f : (town > 1.0f ? 1.0f : town);

	// Open ground answers late and thin: a ridge's slap a quarter second on, then a long dark tail.  A
	// town answers at once off the walls across the street, brighter and stronger.
	Real wet = 0.14f + 0.16f * town;
	Real decay = 2.6f - 0.8f * town;
	Real damping = 0.8f - 0.5f * town;
	Real echo = 0.26f - 0.17f * town;
	Real slap = 1.2f - 0.85f * town;
	Real absorption = m_audioSettings->m_airAbsorptionDistance;
	const Bool snowy = TheGlobalData->m_weather == WEATHER_SNOWY;
	if (snowy)
	{
		// snow swallows the echo and the top end, of the sound itself as well as the room's
		const Real SNOW_SEND_SHARE = 0.35f;
		const Real SNOW_DECAY_SECONDS = 0.4f;
		const Real SNOW_AIR_SHARE = 0.6f;
		wet *= SNOW_SEND_SHARE;
		decay = SNOW_DECAY_SECONDS;
		damping = 1.0f;
		echo = 0.0f;
		slap = 0.0f;
		absorption *= SNOW_AIR_SHARE;
	}
	m_roomAbsorption = absorption;
	m_airScale = 0.0f;	// update() hands it down with the zoom's range scale
	AIL_ex_set_reverb(wet * m_audioSettings->m_reverbLevel, decay, damping, echo, slap);
	DEBUG_LOG(("AUDIO: room for %s: %.2f%% built over, town %.2f%s, send %.2f, decay %.2f s, damping %.2f, "
		"echo %d ms at %.2f, air %.0f\n", m_roomMap.str(), cover * 100.0f, town, snowy ? " in snow" : "",
		wet * m_audioSettings->m_reverbLevel, decay, damping, (Int)(echo * 1000.0f), slap, absorption));

	// The bed, from the same reading and the ground the map is painted with, sampled on a grid: snow
	// (the weather, or snow on most of the ground that is not plain dirt and rock), a town (half way to
	// the room's town or more, or streets and houses painted on a quarter of it), else desert or field,
	// whichever of the two covers more.  The shell map behind the menus keeps quiet.
	const Int GRID = 24;
	Int desert = 0, field = 0, snow = 0, urban = 0, counted = 0;
	for (Int i = 0; i < GRID; ++i)
	{
		for (Int j = 0; j < GRID; ++j)
		{
			TerrainType *tile = TheTerrainVisual->getTerrainTile(extent.lo.x + extent.width() * (i + 0.5f) / GRID,
				extent.lo.y + extent.height() * (j + 0.5f) / GRID);
			if (tile == NULL)
				continue;
			++counted;
			switch (tile->getClass())
			{
				case TERRAIN_DESERT_1: case TERRAIN_DESERT_2: case TERRAIN_DESERT_3: case TERRAIN_LIVE_DESERT:
				case TERRAIN_DRY_DESERT: case TERRAIN_SAND: case TERRAIN_ACCENT_SAND: case TERRAIN_TROPICAL_BEACH:
					++desert;
					break;
				case TERRAIN_EASTERN_EUROPE_1: case TERRAIN_EASTERN_EUROPE_2: case TERRAIN_EASTERN_EUROPE_3:
				case TERRAIN_SWISS_1: case TERRAIN_SWISS_2: case TERRAIN_SWISS_3: case TERRAIN_GRASS: case TERRAIN_FIELD:
				case TERRAIN_COBBLESTONE_GRASS: case TERRAIN_ACCENT_GRASS: case TERRAIN_BEACH_PARK: case TERRAIN_WOOD:
					++field;
					break;
				case TERRAIN_SNOW_1: case TERRAIN_SNOW_2: case TERRAIN_SNOW_3: case TERRAIN_RUGGED_SNOW: case TERRAIN_FLAT_SNOW:
					++snow;
					break;
				case TERRAIN_RESIDENTIAL: case TERRAIN_ASPHALT: case TERRAIN_CONCRETE: case TERRAIN_CHINA: case TERRAIN_URBAN:
					++urban;
					break;
				default:
					break;
			}
		}
	}
	const AudioSettings *settings = m_audioSettings;
	const char *kind;
	if (TheGameLogic->isInShellGame())
	{
		kind = "none, the shell map";
		m_bedEvent.clear();
	}
	else if (snowy || 2 * snow > desert + field + snow + urban)
	{
		kind = "snow";
		m_bedEvent = settings->m_ambienceBedSnow;
	}
	else if (town >= 0.5f || 4 * urban >= counted)
	{
		kind = "town";
		m_bedEvent = settings->m_ambienceBedTown;
	}
	else if (desert >= field)
	{
		kind = "desert";
		m_bedEvent = settings->m_ambienceBedDesert;
	}
	else
	{
		kind = "field";
		m_bedEvent = settings->m_ambienceBedField;
	}
	if (m_bedEvent.isNotEmpty() && findAudioEventInfo(m_bedEvent) == NULL)
		m_bedEvent.clear();
	m_bedHandle = AHSV_Error;
	m_bedStartedAt = 0;
	DEBUG_LOG(("AUDIO: ambience bed for %s: %s (%d desert, %d field, %d snow, %d town of %d samples), %s\n",
		m_roomMap.str(), kind, desert, field, snow, urban, counted, m_bedEvent.isEmpty() ? "none" : m_bedEvent.str()));
}

//-------------------------------------------------------------------------------------------------
/** The map's bed, a looping 2D sound under everything else: started once the match runs, started
	* again (a second on) whenever it is not playing, since as the lowest of priorities it gives up its
	* voice to anything that needs one, and set every pass to the level the camera's height asks for,
	* AmbienceBedNearVolume of its own Volume zoomed in up to AmbienceBedFarVolume zoomed out.  It
	* answers to the Ambience slider (AudioManager::isAmbientSound) and pauses with the rest. */
void MilesAudioManager::updateAmbienceBed( void )
{
	if (m_bedEvent.isEmpty() || !TheGameLogic->isInGame() || TheGameLogic->isLoadingMap())
		return;

	const AudioSettings *settings = m_audioSettings;
	const Real level = settings->m_ambienceBedNearVolume
		+ (settings->m_ambienceBedFarVolume - settings->m_ambienceBedNearVolume) * getZoomOut();

	const UnsignedInt frame = TheGameLogic->getFrame();
	if (!isCurrentlyPlaying(m_bedHandle))
	{
		if (m_bedStartedAt != 0 && frame >= m_bedStartedAt && frame < m_bedStartedAt + LOGICFRAMES_PER_SECOND)
			return;
		AudioEventRTS bed(m_bedEvent);
		getInfoForAudioEvent(&bed);
		bed.setVolume(bed.getAudioEventInfo()->m_volume * level);
		m_bedHandle = addAudioEvent(&bed);
		m_bedStartedAt = frame > 0 ? frame : 1;
		return;
	}

	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it)
	{
		PlayingAudio *playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getPlayingHandle() == m_bedHandle)
		{
			playing->m_audioEventRTS->setVolume(playing->m_audioEventRTS->getAudioEventInfo()->m_volume * level);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A big blast (DuckSounds) dips everything else for DuckTime, down to DuckVolume, as far as the blast
	* is loud at the microphone: by the square root of its distance gain, so a nuke across the map
	* still leans on the mix and one out of its range does nothing.  Asked when the blast starts, so a
	* far one ducks when it is heard rather than when it is seen; a loop that ducks does so once. */
void MilesAudioManager::duckForBlast( AudioEventRTS *event, H3DSAMPLE sample3D )
{
	const AudioSettings *settings = m_audioSettings;
	if (settings->m_duckVolume >= 1.0f
			|| std::find(settings->m_duckSounds.begin(), settings->m_duckSounds.end(), event->getEventName()) == settings->m_duckSounds.end())
		return;

	const AudioEventInfo *info = event->getAudioEventInfo();
	const Bool global = BitTest(info->m_type, ST_GLOBAL);
	Coord3D toMicrophone = *getCurrentPositionFromEvent(event);
	toMicrophone.sub(&m_listenerPosition);
	const Real distance = toMicrophone.length();
	const Real nearness = sqrtf(AIL_ex_3D_distance_gain(distance,
		(global ? (Real)settings->m_globalMinRange : info->m_minDistance) * getRangeScale(),
		(global ? (Real)settings->m_globalMaxRange : info->m_maxDistance) * getRangeScale(), settings->m_rangeVolumeFade));
	if (nearness <= 0.0f)
	{
		DEBUG_LOG(("AUDIO: duck by %s at %.0f units: out of earshot, none\n", event->getEventName().str(), distance));
		return;
	}
	const Real gain = 1.0f - (1.0f - settings->m_duckVolume) * nearness;
	const Real seconds = settings->m_duckTime / MSEC_PER_SECOND;
	AIL_ex_duck(sample3D, gain, seconds);
	// AIL_ex_duck_gain's shape: down by 0.05 s, held to a quarter of the time, back to full at its end
	const Real DUCK_ATTACK_SECONDS = 0.05f;
	const Real heldTo = 0.25f * seconds > DUCK_ATTACK_SECONDS ? 0.25f * seconds : DUCK_ATTACK_SECONDS;
	const Real halfway = 0.5f * (heldTo + seconds);
	const Real halfwayGain = AIL_ex_duck_gain(halfway, gain, seconds);
	DEBUG_LOG(("AUDIO: duck by %s at %.0f units, nearness %.2f: the rest at %.2f (%.1f dB) muffled to %.0f Hz, "
		"held to %.2f s, at %.2f s %.2f and %.0f Hz, full at %.2f s, frame %u\n",
		event->getEventName().str(), distance, nearness, gain, 20.0f * log10f(gain), AIL_ex_duck_cutoff(gain),
		heldTo, halfway, halfwayGain, AIL_ex_duck_cutoff(halfwayGain), seconds, TheGameLogic->getFrame()));
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::checkForSample( AudioRequest *req )
{
	if (!req->m_usePendingEvent) {
		return true;
	}

  if ( req->m_pendingEvent->getAudioEventInfo() == NULL )
  {
    // Fill in event info
    getInfoForAudioEvent( req->m_pendingEvent );
  }

	// A sound that waited out its distance is asked again as it lands, as EA meant it to be: its range
	// from where the microphone is now, the fog, its Limit, the channels and the battle budget, since
	// when it was asked for the voices it would join were still to come.  EA compared the info's ST_
	// bits with AT_SoundEffect here, so only a sound typed plain "world" (ST_WORLD has the same value)
	// was ever asked.  A loop's next pass is the same sound going on, not a new one, and is let through:
	// refused here it would be gone for good, with nothing to start it again.  So is a sound a script
	// asked for, which the script may be waiting on and which passed every test when it was asked.
	if (req->m_nextLoop || req->m_pendingEvent->getIsLogicalAudio()
		|| req->m_pendingEvent->getAudioEventInfo()->m_soundType != AT_SoundEffect)
  {
		return true;
	}

	m_askingOnArrival = true;
	const Bool canPlay = m_sound->canPlayNow(req->m_pendingEvent);
	m_askingOnArrival = false;
	return canPlay;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::setHardwareAccelerated(Bool accel)
{
	// Extends
	Bool retEarly = (accel == m_hardwareAccel);
	AudioManager::setHardwareAccelerated(accel);

	if (retEarly) {
		return;
	}
	
	if (m_hardwareAccel) {
		for (Int i = 0; i < MAX_HW_PROVIDERS; ++i) {
			UnsignedInt providerNdx = TheAudio->getProviderIndex(TheAudio->getAudioSettings()->m_preferred3DProvider[i]);
			TheAudio->selectProvider(providerNdx);
			if (getSelectedProvider() == providerNdx) {
				return;
			}
		}
	}

	// set it false
	AudioManager::setHardwareAccelerated(FALSE);
	UnsignedInt providerNdx = TheAudio->getProviderIndex(TheAudio->getAudioSettings()->m_preferred3DProvider[MAX_HW_PROVIDERS]);
	TheAudio->selectProvider(providerNdx);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::setSpeakerSurround(Bool surround)
{
	// Extends
	Bool retEarly = (surround == m_surroundSpeakers);
	AudioManager::setSpeakerSurround(surround);

	if (retEarly) {
		return;
	}

	UnsignedInt speakerType;
	if (m_surroundSpeakers) {
		speakerType = TheAudio->getAudioSettings()->m_defaultSpeakerType3D;
	} else {
		speakerType = TheAudio->getAudioSettings()->m_defaultSpeakerType2D;
	}

	TheAudio->setSpeakerType(speakerType);
}

//-------------------------------------------------------------------------------------------------
Real MilesAudioManager::getFileLengthMS( AsciiString strToLoad ) const
{
	if (strToLoad.isEmpty()) {
		return 0.0f;
	}

	//
	// The file's own header first, because two script conditions are built on this answer and a
	// machine with no sound device - a -headless run is one - cannot ask Miles anything.  See
	// AudioManager::getWaveFileLengthMS.  Miles still answers for whatever is not a .wav.
	//
	const Real fromHeader = getWaveFileLengthMS(strToLoad);
	if (fromHeader > 0.0f) {
		return fromHeader;
	}

	LOGIC_CENSUS( CENSUS_AUDIO_READ );	// the device, from the tick (LOGIC-THREAD.md M7)
	// Load it as a stream to get the file info without actually opening the file.
	HSTREAM stream = AIL_open_stream(m_digitalHandle, strToLoad.str(), 0);
	if (!stream) {
		return 0.0f;
	}

	long retVal;
	AIL_stream_ms_position(stream, &retVal, NULL);
	// Now close the stream
	AIL_close_stream(stream);

	return INT_TO_REAL(retVal);
}

//-------------------------------------------------------------------------------------------------
/** The cache is throwing a file away to make room, so whatever plays it stops now: its sample is
	* ended and handed back, and it forgets the file, which is freed when this returns.  It stays on its
	* list, stopped, for processPlayingList's sweep to free.
	*
	* It used to be freed here, and this runs from inside the cache's openFile, which is reached from
	* the middle of playAudioEvent, startNextLoop and notifyOfAudioCompletion, while stopAudioEvent or
	* processPlayingList are walking the 3D list and holding one of its sounds.  Crash 1275 (v3.2.0, a
	* NULL event read in stopAudioEvent's walk of the 3D list) fits that: in v3.2.0 a sound whose stop
	* was being processed opened its decay portion still naming the file it had just closed, an eviction
	* could find it by that file and free it and its list node, and the walk went on through freed
	* memory.  1bb1e81b made that sound forget its file first; this makes an eviction free nothing under
	* any caller. */
void MilesAudioManager::closeAnySamplesUsingFile( const void *fileToClose )
{
	std::list<PlayingAudio *> *lists[] = { &m_playingSounds, &m_playing3DSounds };
	for (Int i = 0; i < 2; ++i) {
		std::list<PlayingAudio *>::iterator it;
		for (it = lists[i]->begin(); it != lists[i]->end(); ++it) {
			PlayingAudio *playing = *it;
			if (playing->m_file == fileToClose) {
				stopPlayingAudio(playing);
				playing->m_file = NULL;	// freed by the cache, not to be closed again by the sweep
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::setDeviceListenerPosition( void )
{
	if (m_listener) {
		AIL_set_3D_orientation(m_listener, m_listenerOrientation.x, m_listenerOrientation.y, m_listenerOrientation.z, 0, 0, -1);

		Real x = m_listenerPosition.x;
		Real y = m_listenerPosition.y;
		Real z = m_listenerPosition.z;
		AIL_set_3D_position( m_listener, x, y, z );
	}
}

//-------------------------------------------------------------------------------------------------
const Coord3D *MilesAudioManager::getCurrentPositionFromEvent( AudioEventRTS *event )
{
	if (!event->isPositionalAudio()) {
		return NULL;
	}

	return event->getCurrentPosition();
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isOnScreen( const Coord3D *pos ) const
{
	static ICoord2D dummy;
	// WorldToScreen will return True if the point is onscreen and false if it is offscreen.
	return TheTacticalView->worldToScreen(pos, &dummy);
}

//-------------------------------------------------------------------------------------------------
Real MilesAudioManager::getEffectiveVolume(AudioEventRTS *event) const
{
	Real volume = 1.0f;
	volume *= (event->getVolume() * event->getVolumeShift());
	if (event->getAudioEventInfo()->m_soundType == AT_Music) 
	{
		volume *= m_musicVolume;
	} 
	else if (event->getAudioEventInfo()->m_soundType == AT_Streaming) 
	{
		volume *= m_speechVolume;
	} 
	else 
	{
		const Bool ambient = isAmbientSound( event->getAudioEventInfo() );
		if (event->isPositionalAudio())
		{
			volume *= ambient ? getAmbientVolume( TRUE ) : m_sound3DVolume;
			Coord3D distance = m_listenerPosition;
			const Coord3D *pos = event->getCurrentPosition();
			if (pos) 
			{
				distance.sub(pos);
				Real objMinDistance;
				Real objMaxDistance;

				if (event->getAudioEventInfo()->m_type & ST_GLOBAL) 
				{
					objMinDistance = TheAudio->getAudioSettings()->m_globalMinRange;
					objMaxDistance = TheAudio->getAudioSettings()->m_globalMaxRange;
				} 
				else 
				{
					objMinDistance = event->getAudioEventInfo()->m_minDistance;
					objMaxDistance = event->getAudioEventInfo()->m_maxDistance;
				}

				Real objDistance = distance.length();

				// `RangeVolumeFade = Yes` in AudioSettings.ini uses the linear fade EA left
				// commented out right here. The default curve is 1/distance, which never reaches
				// zero, so the hard cut at the maximum range makes a sound you are walking away
				// from stop dead instead of fading out. The linear one reaches zero exactly where
				// the cut is. Off by default: it changes how every 3D sound in the game attenuates,
				// and that is a listening decision, not a bug fix.  The curve is the audio backend's
				// own, so a sound is culled on the same curve it is heard at.
				volume *= AIL_ex_3D_distance_gain( objDistance, objMinDistance * getRangeScale(), objMaxDistance * getRangeScale(),
					TheAudio->getAudioSettings()->m_rangeVolumeFade );
			}
		} 
		else
		{
			volume *= ambient ? getAmbientVolume( FALSE ) : m_soundVolume;
		}
	}

	return volume;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::startNextLoop( PlayingAudio *looping )
{
	closeFile(looping->m_file);
	looping->m_file = NULL;

	if (!looping->isPlaying() || looping->m_requestStop) {
		return false;
	}

	if (looping->m_audioEventRTS->hasMoreLoops()) {
		// generate a new filename, and test to see whether we can play with it now
		looping->m_audioEventRTS->generateFilename();
		
		if (looping->m_audioEventRTS->getDelay() > MSEC_PER_LOGICFRAME_REAL) {
			// This pass is done, and the next waits on the request list for its delay with a copy of
			// the event.  EA handed over the event itself and kept pointing at it until the sweep, so a
			// stop that dropped the request in the same update freed it under this entry, and the next
			// walk of the list (stopAudioEvent, the sweep's stopPlayingAudio) read freed memory.
#if defined(_WIN32)
			InterlockedCompareExchange( (volatile long *)&looping->m_status, PS_Stopping, PS_Playing );
#else
			playingStatusCompareExchange( &looping->m_status, PS_Stopping, PS_Playing );
#endif


			AudioRequest *req = allocateAudioRequest(true);
			req->m_pendingEvent = MSGNEW("AudioEventRTS") AudioEventRTS(*looping->m_audioEventRTS);
			req->m_requiresCheckForSample = true;
			req->m_nextLoop = true;
			appendAudioRequest(req);
			return true;
		}

		if (looping->m_type == PAT_3DSample) {
			looping->m_file = playSample3D(looping);
		} else {
			looping->m_file = playSample(looping->m_audioEventRTS, looping->m_sample);
		}
		
		return looping->m_file != NULL;
	} 
	return false;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::playStream( AudioEventRTS *event, HSTREAM stream )
{
	// Force it to the beginning
	if (event->getAudioEventInfo()->m_soundType == AT_Music) {
		AIL_set_stream_loop_count(stream, INFINITE_LOOP_COUNT);
	}

	AIL_register_stream_callback(stream, setStreamCompleted);
	noteAudioStarted((UnsignedIntPtr) stream);	// before the start: a stream that ends at once still counts as this one
	AIL_start_stream(stream);
	if (event->getAudioEventInfo()->m_soundType == AT_Music) {
		// Need to stop/fade out the old music here.
	}
}

//-------------------------------------------------------------------------------------------------
void *MilesAudioManager::playSample( AudioEventRTS *event, HSAMPLE sample )
{
	AIL_init_sample(sample);

	// Prep any sort of filtering, etc, here
	AIL_register_EOS_callback(sample, setSampleCompleted);
	initFilters(sample, event);

	// Load the file in
	void *fileBuffer = NULL;
	fileBuffer = loadFileForRead(event);
	if (fileBuffer) {
		AIL_set_sample_file(sample, fileBuffer, 0);

		// Start playback
		noteAudioStarted((UnsignedIntPtr) sample);
		AIL_start_sample(sample);
	}

	return fileBuffer;
}

//-------------------------------------------------------------------------------------------------
void *MilesAudioManager::playSample3D( PlayingAudio *audio )
{
	AudioEventRTS *event = audio->m_audioEventRTS;
	H3DSAMPLE sample3D = audio->m_3DSample;
	const Coord3D *pos = getCurrentPositionFromEvent(event);
	if (pos) {
		// Load the file in
		void *fileBuffer = loadFileForRead(event);
		if (fileBuffer) {
			AIL_set_3D_sample_file(sample3D, fileBuffer);
			// a unit's reply is the radio in the commander's ear: no air, no doppler, no room
			AIL_ex_set_3D_sample_dry(sample3D, BitTest(event->getAudioEventInfo()->m_type, ST_VOICE));

			// Prep any sort of filtering, etc, here
			AIL_register_3D_EOS_callback(sample3D, set3DSampleCompleted);

			// Set the position values of the sample here.  Miles takes the maximum first; EA passed the
			// minimum first, and with the pair backwards every world sound played at full volume out to
			// its cut-off, so far-reaching ambient loops drowned the fighting next to the camera.
			setSampleDistances(sample3D, event);

			// Set the position of the sample here
			Real x = pos->x;
			Real y = pos->y;
			Real z = pos->z;
			AIL_set_3D_position( sample3D, x, y, z );
			initFilters3D(audio, pos);
			
			// Start playback
			noteAudioStarted((UnsignedIntPtr) sample3D);
			AIL_start_3D_sample(sample3D);
		}
		return fileBuffer;
	}

	return NULL;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::buildProviderList( void )
{
   HPROENUM next = HPROENUM_FIRST;

	 char *name;
	 UnsignedInt index = 0;
	 while (index < MAXPROVIDERS && AIL_enumerate_3D_providers(&next, &m_provider3D[index].id, &name)) {
		 m_provider3D[index].name.set(name);	// set it to the AsciiString
		 ++index;
	 }
	 
	 m_providerCount = index;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::createListener( void )
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	m_listener = AIL_open_3D_listener(m_provider3D[m_selectedProvider].id);
	// initial listener position will be (0, 0, 0)
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::initDelayFilter( void )
{
	if (m_delayFilter != NULL) {
		return;
	}

	char* filterName;
	HPROENUM enumFLTs = HPROENUM_FIRST;
	HPROVIDER currentProvider;

	while (AIL_enumerate_filters(&enumFLTs, &currentProvider, &filterName )) {  
		if (strcmp(filterName,"Mono Delay Filter") == 0) {
			m_delayFilter = currentProvider;
			break;  
		}
	}
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isValidProvider( void )
{
	return (m_selectedProvider < m_providerCount);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::initSamplePools( void )
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	int i = 0;
	for (i = 0; i < getAudioSettings()->m_sampleCount2D; ++i) {
		HSAMPLE sample = AIL_allocate_sample_handle(m_digitalHandle);
		DEBUG_ASSERTCRASH(sample, ("Couldn't get %d 2D samples\n", i + 1));
		if (sample) {
			AIL_init_sample(sample);
			AIL_set_sample_user_data(sample, 0, i + 1);
			m_availableSamples.push_back(sample);
			++m_num2DSamples;
		}
	}

	for (i = 0; i < getAudioSettings()->m_sampleCount3D; ++i) {
		H3DSAMPLE sample = AIL_allocate_3D_sample_handle(m_provider3D[m_selectedProvider].id);
		DEBUG_ASSERTCRASH(sample, ("Couldn't get %d 3D samples\n", i + 1));
		if (sample) {
			AIL_set_3D_user_data(sample, 0, i + 1);
			m_available3DSamples.push_back(sample);
			++m_num3DSamples;
		}
	}

	// Streams are basically free, so we can just allocate the appropriate number
	m_numStreams = getAudioSettings()->m_streamCount;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processRequest( AudioRequest *req )
{
	switch (req->m_request)
	{
		case AR_Play:
		{
			// hand the event over: from here it belongs to the playing list, or to playAudioEvent
			// to free if it cannot be played
			const Bool requestStop = req->m_requestStop;
			const Bool nextLoop = req->m_nextLoop;
			playAudioEvent(req->releasePendingEvent(), requestStop, nextLoop);
			break;
		}
		case AR_Pause:
		{
			pauseAudioEvent(req->m_handleToInteractOn);
			break;
		}
		case AR_Stop:
		{
			stopAudioEvent(req->m_handleToInteractOn);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void *MilesAudioManager::getHandleForBink( void )
{
	if (m_binkHandle == NULL) {
		PlayingAudio *aud = allocatePlayingAudio();
		aud->m_audioEventRTS = NEW AudioEventRTS("BinkHandle");	// poolify
		getInfoForAudioEvent(aud->m_audioEventRTS);
		aud->m_sample = getFirst2DSample(aud->m_audioEventRTS);
		aud->m_type = PAT_Sample;

		if (!aud->m_sample) {
			stopPlayingAudio(aud);
			releasePlayingAudio(aud);
			return NULL;
		}

		m_binkHandle = aud;
	}
	
	AILLPDIRECTSOUND lpDS;
	AIL_get_DirectSound_info(m_binkHandle->m_sample, &lpDS, NULL);
	return lpDS;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::releaseHandleForBink( void )
{
	if (m_binkHandle) {
		stopPlayingAudio(m_binkHandle);
		releasePlayingAudio(m_binkHandle);
		m_binkHandle = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay)
{
	if (!eventToPlay->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToPlay);
		if (!eventToPlay->getAudioEventInfo()) {
			DEBUG_CRASH(("No info for forced audio event '%s'\n", eventToPlay->getEventName().str()));
			return;
		}
	}

	switch (eventToPlay->getAudioEventInfo()->m_soundType)
	{
		case AT_Music:
			if (!isOn(AudioAffect_Music)) 
				return;
			break;
		case AT_SoundEffect:
			if (!isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D)) 
				return;
			break;
		case AT_Streaming:
			if (!isOn(AudioAffect_Speech)) 
				return;
			break;
	}
	
	AudioEventRTS event = *eventToPlay;

	event.generateFilename();
	event.generatePlayInfo();

	std::list<std::pair<AsciiString, Real> >::iterator it;
	for (it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it) {
		if (it->first == event.getEventName()) {
			event.setVolume(it->second);
			break;
		}
	}

	AsciiString fileToPlay = event.getFilename();
	HAUDIO haud = AIL_quick_load_and_play(fileToPlay.str(), 1, 0);

	// Even though the event type is not Speech, this is used only for mission briefings, so use the 
	// speech slider to adjust the volume.
	// Get the volume from the event, and pass 0.5 to play the audio in the middle. (0.0 is full left, 1.0 is full right)
	AIL_quick_set_volume(haud, event.getVolume() * getVolume(AudioAffect_Speech), 0.5);
	m_audioForcePlayed.push_back(haud);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void AILCALLBACK setSampleCompleted( HSAMPLE sampleCompleted )
{
	static_cast<MilesAudioManager *>(TheAudio)->queueAudioCompletion((UnsignedIntPtr) sampleCompleted, PAT_Sample);
}

//-------------------------------------------------------------------------------------------------
void AILCALLBACK set3DSampleCompleted( H3DSAMPLE sample3DCompleted )
{
	static_cast<MilesAudioManager *>(TheAudio)->queueAudioCompletion((UnsignedIntPtr) sample3DCompleted, PAT_3DSample);
}

//-------------------------------------------------------------------------------------------------
void AILCALLBACK setStreamCompleted( HSTREAM streamCompleted )
{
	static_cast<MilesAudioManager *>(TheAudio)->queueAudioCompletion((UnsignedIntPtr) streamCompleted, PAT_Stream);
}

//-------------------------------------------------------------------------------------------------
U32 AILCALLBACK streamingFileOpen(char const *fileName, AILFILEHANDLE *file_handle)
{
	// The handle is a File*, so it is pointer sized - the cast to a 32-bit U32 that used to be here
	// threw half of every pointer away and was guarded by a debug-only crash saying as much.
	(*file_handle) = (AILFILEHANDLE) TheFileSystem->openFile(fileName, File::READ | File::STREAMING);
	return ((*file_handle) != 0);
}

//-------------------------------------------------------------------------------------------------
void AILCALLBACK streamingFileClose(AILFILEHANDLE fileHandle)
{
	((File*) fileHandle)->close();
}

//-------------------------------------------------------------------------------------------------
S32 AILCALLBACK streamingFileSeek(AILFILEHANDLE fileHandle, S32 offset, U32 type)
{
	return ((File*) fileHandle)->seek(offset, (File::seekMode) type);
}

//-------------------------------------------------------------------------------------------------
U32 AILCALLBACK streamingFileRead(AILFILEHANDLE file_handle, void *buffer, U32 bytes)
{
	return ((File*) file_handle)->read(buffer, bytes);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AudioFileCache::AudioFileCache() : m_maxSize(0), m_currentlyUsedSize(0), m_mutexName("AudioFileCacheMutex")
{
#if defined(_WIN32)
	m_mutex = CreateMutex(NULL, FALSE, m_mutexName);
#else
	m_mutex = new std::timed_mutex;	// the process's own: Windows' name made it every process's, by accident
#endif
}

//-------------------------------------------------------------------------------------------------
AudioFileCache::~AudioFileCache()
{
	{
		ScopedMutex mut(m_mutex);

		// Free all the samples that are open.
		OpenFilesHashIt it;
		for ( it = m_openFiles.begin(); it != m_openFiles.end(); ++it ) {
			if (it->second.m_openCount > 0) {
				DEBUG_CRASH(("Sample '%s' is still playing, and we're trying to quit.\n", it->second.m_eventInfo->m_audioName.str()));
			}

			releaseOpenAudioFile(&it->second);
			// Don't erase it from the map, cause it makes this whole process way more complicated, and 
			// we're about to go away anyways.
		}
	}

#if defined(_WIN32)
	CloseHandle(m_mutex);
#else
	delete m_mutex;
#endif
}

//-------------------------------------------------------------------------------------------------
void *AudioFileCache::openFile( AudioEventRTS *eventToOpenFrom )
{
	// Protect the entire openFile function
	ScopedMutex mut(m_mutex);

	AsciiString strToFind;
	switch (eventToOpenFrom->getNextPlayPortion())
	{
		case PP_Attack:
			strToFind = eventToOpenFrom->getAttackFilename();
			break;
		case PP_Sound:
			strToFind = eventToOpenFrom->getFilename();
			break;
		case PP_Decay:
			strToFind = eventToOpenFrom->getDecayFilename();
			break;
		case PP_Done:
			return NULL;
	}

	OpenFilesHash::iterator it;
	it = m_openFiles.find(strToFind);

	if (it != m_openFiles.end()) {
		++it->second.m_openCount;
		// A cached file keeps the info of whoever opened it first, and a map's customised sound
		// shares its file with the stock one.  That info is deleted with the level
		// (removeLevelSpecificAudioEventInfos), and freeEnoughSpaceForSample went on reading the
		// priority out of it for the file's next user.  The event opening it now is alive.
		it->second.m_eventInfo = eventToOpenFrom->getAudioEventInfo();
		return it->second.m_file;
	}

	// Couldn't find the file, so actually open it.
	File *file = TheFileSystem->openFile(strToFind.str());
	if (!file) {
		DEBUG_ASSERTLOG(strToFind.isEmpty(), ("Missing Audio File: '%s'\n", strToFind.str()));
		return NULL;
	}

	UnsignedInt fileSize = file->size();
	char* buffer = file->readEntireAndClose();

	OpenAudioFile openedAudioFile;
	openedAudioFile.m_eventInfo = eventToOpenFrom->getAudioEventInfo();

	AILSOUNDINFO soundInfo;
	AIL_WAV_info(buffer, &soundInfo);

	if (eventToOpenFrom->isPositionalAudio()) {
		if (soundInfo.channels > 1) {
			DEBUG_CRASH(("Requested Positional Play of audio '%s', but it is in stereo.", strToFind.str()));
			delete [] buffer;
			return NULL;
		}
	}
	
	if (soundInfo.format == WAVE_FORMAT_IMA_ADPCM) {
		void *decompressFileBuffer;
		U32 newFileSize;
		AIL_decompress_ADPCM(&soundInfo, &decompressFileBuffer, &newFileSize);
		fileSize = newFileSize;
		openedAudioFile.m_compressed = TRUE;
		delete [] buffer;
		openedAudioFile.m_file = decompressFileBuffer;
		openedAudioFile.m_soundInfo = soundInfo;
		openedAudioFile.m_openCount = 1;
	} else if (soundInfo.format == WAVE_FORMAT_PCM) {
		openedAudioFile.m_compressed = FALSE;
		openedAudioFile.m_file = buffer;
		openedAudioFile.m_soundInfo = soundInfo;
		openedAudioFile.m_openCount = 1;
	} else {
		DEBUG_CRASH(("Unexpected compression type in '%s'\n", strToFind.str()));
		// prevent leaks
		delete [] buffer;
		return NULL;
	}

	openedAudioFile.m_fileSize = fileSize;
	m_currentlyUsedSize += openedAudioFile.m_fileSize;
	if (m_currentlyUsedSize > m_maxSize) {
		// We need to free some samples, or we're not going to be able to play this sound.
		if (!freeEnoughSpaceForSample(openedAudioFile)) {
			m_currentlyUsedSize -= openedAudioFile.m_fileSize;
			releaseOpenAudioFile(&openedAudioFile);
			return NULL;
		}
	}

	m_openFiles[strToFind] = openedAudioFile;
	return openedAudioFile.m_file;
}

//-------------------------------------------------------------------------------------------------
void AudioFileCache::closeFile( void *fileToClose )
{
	if (!fileToClose) {
		return;
	}

	// Protect the entire closeFile function
	ScopedMutex mut(m_mutex);

	OpenFilesHash::iterator it;
	for ( it = m_openFiles.begin(); it != m_openFiles.end(); ++it ) {
		if ( it->second.m_file == fileToClose ) {
			--it->second.m_openCount;
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AudioFileCache::setMaxSize( UnsignedInt size )
{
	// Protect the function, in case we're trying to use this value elsewhere.
	ScopedMutex mut(m_mutex);

	m_maxSize = size;
}

//-------------------------------------------------------------------------------------------------
void AudioFileCache::releaseOpenAudioFile( OpenAudioFile *fileToRelease )
{
	if (fileToRelease->m_openCount > 0) {
		// This thing needs to be terminated IMMEDIATELY.
		TheAudio->closeAnySamplesUsingFile(fileToRelease->m_file);
	}

	if (fileToRelease->m_file) {
		if (fileToRelease->m_compressed) {
			// Files read in via AIL_decompress_ADPCM must be freed with AIL_mem_free_lock. 
			AIL_mem_free_lock(fileToRelease->m_file);
		} else { 
			// Otherwise, we read it, we own it, blow it away.
			delete [] fileToRelease->m_file;
		}
		fileToRelease->m_file = NULL;
		fileToRelease->m_eventInfo = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
Bool AudioFileCache::freeEnoughSpaceForSample(const OpenAudioFile& sampleThatNeedsSpace)
{
	
	Int spaceRequired = m_currentlyUsedSize - m_maxSize;
	Int runningTotal = 0;

	std::list<AsciiString> filesToClose;
	// First, search for any samples that have ref counts of 0. They are low-hanging fruit, and 
	// should be considered immediately.
	OpenFilesHashIt it;
	for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
		if (it->second.m_openCount == 0) {
			// This is said low-hanging fruit.
			filesToClose.push_back(it->first);

			runningTotal += it->second.m_fileSize;

			if (runningTotal >= spaceRequired) {
				break;
			}
		}
	}

	// If we don't have enough space yet, then search through the events who have a count of 1 or more
	// and who are lower priority than this sound.
	// Mical said that at this point, sounds shouldn't care if other sounds are interruptable or not.
	// Kill any files of lower priority necessary to clear our the buffer.
	if (runningTotal < spaceRequired) {
		for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
			if (it->second.m_openCount > 0) {
				if (it->second.m_eventInfo->m_priority < sampleThatNeedsSpace.m_eventInfo->m_priority) {
					filesToClose.push_back(it->first);
					runningTotal += it->second.m_fileSize;
				
					if (runningTotal >= spaceRequired) {
						break;
					}
				}
			}
		}
	}

	// We weren't able to find enough sounds to truncate. Therefore, this sound is not going to play.
	if (runningTotal < spaceRequired) {
		return FALSE;
	}

	std::list<AsciiString>::iterator ait;
	for (ait = filesToClose.begin(); ait != filesToClose.end(); ++ait) {
		OpenFilesHashIt itToErase = m_openFiles.find(*ait);
		if (itToErase != m_openFiles.end()) {
			releaseOpenAudioFile(&itToErase->second);
			m_currentlyUsedSize -= itToErase->second.m_fileSize;
			m_openFiles.erase(itToErase);
		}
	}

	return TRUE;
}


#if defined(_DEBUG) || defined(_INTERNAL)
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::dumpAllAssetsUsed()
{
	if (!TheGlobalData->m_preloadReport) {
		return;
	}

	// Dump all the audio assets we've used.
	FILE *logfile=fopen("PreloadedAssets.txt","a+");	//append to log
	if (!logfile)
		return;

	std::list<AsciiString> missingEvents;
	std::list<AsciiString> usedFiles;

	std::list<AsciiString>::iterator lit;

	fprintf(logfile, "\nAudio Asset Report - BEGIN\n");
	{
		SetAsciiStringIt it;
		std::vector<AsciiString>::iterator asIt;
		for (it = m_allEventsLoaded.begin(); it != m_allEventsLoaded.end(); ++it) {
			AsciiString astr = *it;
			AudioEventInfo *aei = findAudioEventInfo(astr);
			if (!aei) {
				missingEvents.push_back(astr);
				continue;
			}

			for (asIt = aei->m_attackSounds.begin(); asIt != aei->m_attackSounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			for (asIt = aei->m_sounds.begin(); asIt != aei->m_sounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			for (asIt = aei->m_decaySounds.begin(); asIt != aei->m_decaySounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			if (!aei->m_filename.isEmpty()) {
				usedFiles.push_back(aei->m_filename);
			}
		}

		fprintf(logfile, "\nEvents Requested that are missing information - BEGIN\n");
		for (lit = missingEvents.begin(); lit != missingEvents.end(); ++lit) {
			fprintf(logfile, "%s\n", (*lit).str());
		}
		fprintf(logfile, "\nEvents Requested that are missing information - END\n");

		fprintf(logfile, "\nFiles Used - BEGIN\n");
		for (lit = usedFiles.begin(); lit != usedFiles.end(); ++lit) {
			fprintf(logfile, "%s\n", (*lit).str());
		}
		fprintf(logfile, "\nFiles Used - END\n");
	}
	fprintf(logfile, "\nAudio Asset Report - END\n");
	fclose(logfile);
	logfile = NULL;
}
#endif
