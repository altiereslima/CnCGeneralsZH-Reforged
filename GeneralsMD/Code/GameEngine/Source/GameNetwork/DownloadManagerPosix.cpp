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

// FILE: DownloadManagerPosix.cpp /////////////////////////////////////////////////////////////////
// Desc:   GameNetwork/DownloadManager.h off Windows: no downloader.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* DownloadManager.cpp is the Windows body, over WWDownload's FTP, and is not built here.  The class
	 off Windows is the header's, inline and doing nothing; this is the one definition it needs.  Only
	 DownloadMenuInit ever creates a manager, and only on Windows, so off Windows it stays NULL. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/DownloadManager.h"

DownloadManager *TheDownloadManager = NULL;
