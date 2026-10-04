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

/*
** zh_fopen and its siblings: the C runtime's file calls, for code that opens files outside the
** engine's LocalFileSystem - the debug log, MemoryPools.ini, the preferences, the save and replay
** writers - many of which run before any file system exists (C1, decision D4).
**
** On Windows each one is the C runtime call it names, verbatim, and compiles to exactly what the
** call site compiled to before.
**
** Off Windows each one first hands the path to PosixPath_Resolve (posixpath.h), so the engine's
** spelling - backslashes, whatever case - reaches a file that exists in a different case on a
** case-sensitive volume.
**
** These are not shims in the MSVCCompat.h sense, which give a name Windows has its POSIX
** equivalent and change nothing else.  They add behaviour off Windows, and a call site that says
** zh_fopen says so; borrowing the standard names would hide it, and would also change every fopen in
** the tree, vendored code included.  Each keeps its own platform's C runtime semantics apart from the
** path: rename, for one, replaces an existing target on POSIX and fails on Windows, as it always has.
**
** Intent, off Windows: a call that can create its file - an fopen mode starting "w" or "a", an open
** with O_CREAT, the target of a rename, a new directory - needs the path's directories to exist and
** keeps the engine's spelling for a new last component; every other call needs the whole path to
** exist.
*/

#ifndef ZHIO_H
#define ZHIO_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(_WIN32)

#include <direct.h>
#include <io.h>

inline FILE * zh_fopen(const char * path, const char * mode) { return fopen(path, mode); }
inline int zh_open(const char * path, int flags, int permissions) { return _open(path, flags, permissions); }
inline int zh_access(const char * path, int mode) { return _access(path, mode); }
inline int zh_remove(const char * path) { return remove(path); }
inline int zh_unlink(const char * path) { return _unlink(path); }
inline int zh_rename(const char * from, const char * to) { return rename(from, to); }
inline int zh_mkdir(const char * path) { return _mkdir(path); }
inline int zh_stat(const char * path, struct stat * status) { return stat(path, status); }

#else

// Each returns what the C runtime call returns, and sets errno to ENOENT when the path does not
// resolve.
FILE * zh_fopen(const char * path, const char * mode);
int zh_open(const char * path, int flags, int permissions);
int zh_access(const char * path, int mode);
int zh_remove(const char * path);
// One file: unlike zh_remove, never a directory, as DeleteFile and _unlink refuse one.
int zh_unlink(const char * path);
int zh_rename(const char * from, const char * to);
// One directory, with permissions 0777 less the process's umask, as Windows' _mkdir takes none.
int zh_mkdir(const char * path);
// stat, following symbolic links, of a path that must exist.
int zh_stat(const char * path, struct stat * status);

// read(), as Windows' _read reads a file opened _O_TEXT (decision D6): each "\r\n" becomes "\n", a
// lone '\r' stays, and the file position still counts the file's bytes, so a seek back by one after a
// '\n' lands on that '\n'.  A '\r' at the end of what was read is settled by reading one byte more,
// and putting it back when it is not the '\n'.  Ctrl-Z does not end the file: nothing the game ships
// holds one in a text file.  Returns the bytes stored, which can be fewer than read, or read()'s
// 0 or -1.
int zh_read_text(int handle, void * buffer, unsigned bytes);

#endif

#endif
