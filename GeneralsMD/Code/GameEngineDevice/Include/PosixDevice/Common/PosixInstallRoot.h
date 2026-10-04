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

// PosixInstallRoot.h: where the player's Zero Hour is (P1 step 4).

/* The game's data is the player's own install (Steam, CD, First Decade, a copy from a PC), anywhere
	 on disk; off Windows no path can be assumed.  PosixMain asks here, before the engine exists:

	   1. "-root <dir>": used as given, never validated (harnesses root at farms and test folders);
	   2. Registry.ini's Zero Hour InstallPath, if it still validates (an install can move);
	   3. packaged only (an app bundle, or a Linux package, P3): the known places (~/Games, /Applications,
	      the Steam libraries, CrossOver and Whisky bottles), the first that validates;
	   4. packaged only: the player's own choice, through the chooser PosixMain passes (SDL's native
	      folder dialog; none in the Steam Deck's Game Mode, which is told to use -root instead).  An
	      invalid choice is said why and asked again; cancelling ends the start.  The chosen folder is
	      the one thing written: Registry.ini's InstallPath, so the chooser runs once.  Zero Hour whose
	      base game is nowhere the game looks (a CD or First Decade install with Generals elsewhere, or a
	      Flatpak, which sees only the folder chosen) is followed by a second question, for the original
	      Generals folder, written as Registry.ini's Generals InstallPath;
	   5. unpackaged: the executable's directory, as always.

	 Validation: Zero Hour's INIZH.big at the folder's top level, and the base game's Textures.big found
	 where Win32BIGFileSystem::init will look for it (the folder itself, ZH_Generals/, the two sibling
	 folder names, or Registry.ini's Generals InstallPath and its First Decade subfolder); and not a
	 folder inside the app bundle or an overlay.  Everything here is plain logic over the file system,
	 so every path but the dialog itself is tested (test_install_root). */

#pragma once

#if defined(_WIN32)
#error "PosixInstallRoot.h is the POSIX start-up's; WinMain roots the game at its own directory"
#endif

#include <string>
#include <vector>

enum PosixInstallCheck
{
	INSTALL_OK,
	INSTALL_NOT_A_FOLDER,
	INSTALL_NO_ZERO_HOUR,		///< no INIZH.big at its top level
	INSTALL_NO_BASE_GAME,		///< Zero Hour, but Generals' Textures.big nowhere the game looks
	INSTALL_INSIDE_THE_APP		///< a folder inside the app bundle or an overlay
};

/// Validates `folder`.  `forbidden` holds folders the install may not be in or under.  The base game is
/// also looked for through Registry.ini's Generals InstallPath when `registryFile` names one.
PosixInstallCheck PosixCheckInstallFolder( const std::string &folder, const std::vector<std::string> &forbidden );
PosixInstallCheck PosixCheckInstallFolderWith( const std::string &folder, const std::vector<std::string> &forbidden,
	const std::string &registryFile );

/// The sentence a player reads for a check that failed, naming the folder.
std::string PosixInstallCheckMessage( PosixInstallCheck check, const std::string &folder );

/// The known places, in the order they are tried, below `home`: ~/Games, /Applications, every Steam
/// library (each Steam folder's own and those its libraryfolders.vdf lists: the Steam Deck's, P3), then
/// CrossOver's and Whisky's bottles.  Candidates only: nothing checked.
std::vector<std::string> PosixKnownInstallPlaces( const std::string &home );

enum PosixInstallSource { ROOT_FROM_ARGUMENT, ROOT_FROM_REGISTRY, ROOT_FROM_KNOWN_PLACE, ROOT_FROM_CHOOSER,
	ROOT_FROM_EXECUTABLE };

/// What the chooser asks for: the Zero Hour folder, or, for a Zero Hour without its base game, the folder the
/// original Generals is installed in.
enum PosixInstallQuestion { CHOOSE_ZERO_HOUR, CHOOSE_GENERALS };

/** The player's choice: TRUE with a real folder, FALSE when they cancelled.  `why` is empty the first
	* time Zero Hour is asked for, and otherwise the reason for the question (the last choice refused, or
	* why Generals is asked for). */
typedef bool (*PosixInstallChooser)( PosixInstallQuestion question, const std::string &why, std::string &chosen,
	void *context );

struct PosixInstallRequest
{
	std::vector<std::string> arguments;		///< argv[1..]
	bool insideAppBundle;					///< packaged: a macOS app bundle, or a Linux package (P3)
	std::string executableDirectory;
	std::string home;						///< for the known places
	std::vector<std::string> forbidden;		///< the bundle and the overlays
	std::string registryFile;				///< Registry.ini's path, or "" for none
	PosixInstallChooser chooser;			///< NULL: no chooser (headless, tests of the other paths)
	void *chooserContext;
};

struct PosixInstallChoice
{
	std::string root;
	PosixInstallSource source;
	bool writeInstallPath;		///< the player chose it: PosixMain writes Registry.ini's InstallPath
	std::string generals;		///< the original Generals folder the player chose, when it was asked for
	bool writeGeneralsInstallPath;	///< PosixMain writes `generals` as Registry.ini's Generals InstallPath
	std::string problem;		///< why there is no root, when the answer is FALSE
};

/// Steps 1-5 above.  FALSE when there is no root to use (a cancelled chooser, or a bundle with none).
bool PosixChooseInstallRoot( const PosixInstallRequest &request, PosixInstallChoice &choice );
