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

// SdlGameEngine: the game engine off Windows with SDL3 underneath (C2).  PosixGameEngine (C1 (f)) has
// the file systems, the network and the browser; this adds what WinMain and its window procedure do
// on Windows:
//   - the window, which it owns: none at all under -headless, where SDL's video is never started,
//     so a headless run needs no display (E1's replay runs are on machines without one);
//   - the event pump (serviceWindowsOS), with a close request and the application's focus handled
//     as WM_CLOSE and WM_ACTIVATEAPP are;
//   - the window's title, through GameText's hook (GameClient/ApplicationWindowTitle.h);
//   - the displays, as Monitors.h's table (SdlDisplays.h), while SDL's video is up;
//   - MessageBoxWrapper's box, as SDL_ShowMessageBox over the window (SdlMessageBox.h);
//   - W3DDevice's window calls, through its hooks (W3DWindowHooks.h): a change of window mode or
//     resolution restyles, resizes and places the SDL window as W3DDisplay does ApplicationHWnd on
//     Windows, and ApplicationHWnd itself is the SDL window.
// Nothing draws into the window until A3; the device is decision 7's, made with no window under
// -headless (decision 8, refined).
//
// The factories are Win32GameEngine's, the same W3D classes (decision 8), W3DGameClient's Bink video
// player included since V1.  Audio is
// MilesAudioManager, over the Miles surface on miniaudio (C4).

#pragma once

#ifndef __SDLGAMEENGINE_H
#define __SDLGAMEENGINE_H

#include "PosixDevice/Common/PosixGameEngine.h"

struct SDL_Window;

class SdlGameEngine : public PosixGameEngine
{
public:
	/** How the window starts, as WinMain settles it before the engine exists: -headless, -win,
		* -fullscreen and -borderless over Options.ini's WindowMode, -hiddenwindow and -offscreen. */
	struct WindowRequest
	{
		Bool headless;		///< no window, and SDL's video never started
		Bool windowed;
		Bool borderless;	///< windowed, with no frame
		Bool hidden;		///< windowed and never shown: for harnesses and automated runs (-hiddenwindow)
		Bool offscreen;		///< no window at all: every frame drawn into the device's own target (-offscreen)
	};

	SdlGameEngine( const WindowRequest &request );
	virtual ~SdlGameEngine();

	using PosixGameEngine::init;
	virtual void init( int argc, char *argv[] );	///< the engine's real start (GameEngine::init( void ) does nothing)
	virtual void serviceWindowsOS( void );

protected:
	virtual GameLogic *createGameLogic( void );
	virtual GameClient *createGameClient( void );
	virtual ModuleFactory *createModuleFactory( void );
	virtual ThingFactory *createThingFactory( void );
	virtual FunctionLexicon *createFunctionLexicon( void );
	virtual Radar *createRadar( void );
	virtual ParticleSystemManager *createParticleSystemManager( void );
	virtual AudioManager *createAudioManager( void );

private:
	void createWindow( void );
	void startOffscreen( void );		///< -offscreen: SDL's video, and no window (SdlGameEngine.cpp)

	WindowRequest m_request;
	SDL_Window *m_window;
	Bool m_sdlVideoStarted;
};

/** Releases the game's window and SDL's video, after the engine is gone (PosixMain, after GameMain), as
	* WinMain destroys its window after GameMain.  The engine's destructor hands them on; the GPU device the
	* engine's teardown releases is made on SDL's video, so the video must outlive it. */
void SdlGameEngine_releaseWindow( void );

#endif // __SDLGAMEENGINE_H
