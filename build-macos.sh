#!/usr/bin/env bash
#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Build Zero Hour Reforged on macOS, from a fresh clone, in one command. build.bat's arguments:
#
#   ./build-macos.sh                    check the toolchain, fetch, configure, build Release, make the app
#   ./build-macos.sh Debug              another configuration (Release|RelWithDebInfo|Debug)
#   ./build-macos.sh Release test       the same, with ctest before the app
#   ./build-macos.sh Release generals   build a single target, and nothing else
#   ./build-macos.sh clean              delete build-mac/ and start again
#
# Needs the Xcode command line tools and Homebrew's cmake and ninja; it says what to install when one is
# missing. The result is build-mac/Zero Hour Reforged.app. GeneralsMD/Code/Tools/build-posix.sh does
# the work, shared with build-linux.sh.

exec "$(dirname "$0")/GeneralsMD/Code/Tools/build-posix.sh" macos "$@"
