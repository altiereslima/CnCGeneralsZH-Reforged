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

// Decision 7 phase A1's link checkpoint: every object file of w3ddevice and ww3d2 is linked (whole
// archive) against what the POSIX game links, so each symbol they reference has to resolve.  It is
// built, never run.
//
// What it does not prove: that the game starts, or that anything draws - only that nothing the renderer
// refers to is missing off Windows.

#include "Lib/BaseType.h"
#include "Platform/RenderTypes.h"

// Main/PosixMain.cpp's, with its values: the probe cannot link that file, which holds main().
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
static char s_noAppPrefix[] = "";
char *gAppPrefix = s_noAppPrefix;

// WinMain.cpp's on Windows, and C2's to define off Windows once the game links w3ddevice: the window the
// device draws into (null under -headless) and whether it is borderless.  Not defined there yet.
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

int main()
{
	return 0;
}
