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
# P3's check: Tools/linux-portable.sh makes the folder it says, from a build inside the Steam Runtime's SDK,
# and the folder's own launcher plays E1 in the Steam Runtime's platform image (glibc 2.31, a clean system).
#
#   1. the folder (no art, no archive): the launcher, generals stripped with its debuglink, the debug file,
#      the overlay, the licences, the icon (a 48 px PNG), the .desktop file, README.txt, and VERSION naming
#      this commit; generals needs nothing newer than GLIBC_2.31 and no libstdc++ symbol;
#   2. armed controls, from the same build without rebuilding: an overlay file that forces the HUD overlay
#      on (off by default in Release), and a static library on the link line that macos-app-licenses.txt does not name, are each refused;
#   3. with ZH_DATA_DIR and the platform image: E1 through the packaged launcher (replay-check.sh --package),
#      inside the platform image, no network - the recording, its playback and a second run agree.
#
# Skipped (77) off Linux, without docker, without the SDK image pulled, or without ZH_PORTABLE_CMAKE (a Linux
# CMake of 3.29 or later that runs inside the container: Kitware's release tarball).  The build is kept in
# <build>/portable-build, so after the first run it is incremental.
#
# What it cannot see: the Steam Deck itself - its GPU, gamescope, Game Mode, audio; every run is -headless.
#
# Usage: test_linux_portable.sh <build dir>

set -u
BUILD_DIR="$1"
CODE="$(cd "$(dirname "$0")/.." && pwd)"
SDK=registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest
PLATFORM=registry.gitlab.steamos.cloud/steamrt/sniper/platform:latest
[ "$(uname -s)" = Linux ] || { echo "skip: not Linux"; exit 77; }
command -v docker >/dev/null && docker info >/dev/null 2>&1 || { echo "skip: no usable docker"; exit 77; }
docker image inspect "$SDK" >/dev/null 2>&1 || { echo "skip: $SDK is not pulled"; exit 77; }
[ -n "${ZH_PORTABLE_CMAKE:-}" ] && [ -x "$ZH_PORTABLE_CMAKE" ] || { echo "skip: ZH_PORTABLE_CMAKE names no CMake"; exit 77; }

failures=0
check() {	# check <condition> <what>
	if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failures=$((failures + 1)); fi
}
T="$(mktemp -d "${TMPDIR:-/tmp}/test_linux_portable.XXXXXX")" || T=""
if [ -z "$T" ] || [ ! -d "$T" ]; then
	echo "test_linux_portable: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
trap 'rm -rf -- "$T"' EXIT
PB="$BUILD_DIR/portable-build"
OUT="$T/ZeroHourReforged-linux-x86_64"

# 1. the folder
out="$(bash "$CODE/Tools/linux-portable.sh" --build "$PB" --out "$OUT" --cmake "$ZH_PORTABLE_CMAKE" --no-art --no-tar 2>&1)"; status=$?
check '[ $status -eq 0 ]' "linux-portable.sh makes the folder (exit $status: $(printf '%s' "$out" | tail -3))"
check '[ -x "$OUT/zero-hour-reforged.sh" ] && [ -x "$OUT/bin/generals" ] && [ -s "$OUT/bin/generals.debug" ]' \
	"the launcher, generals and its debug file"
check 'readelf -S "$OUT/bin/generals" | grep -q "\.gnu_debuglink" && ! readelf -S "$OUT/bin/generals" | grep -q "\.debug_info"' \
	"generals is stripped and names its debug file"
check '[ -d "$OUT/share/zero-hour-reforged/overlay" ] && [ -n "$(ls "$OUT/share/zero-hour-reforged/overlay")" ]' "the overlay"
check '[ -z "$(find "$OUT" -name "Reforged*.big")" ]' "--no-art: no Reforged*.big in the folder"
check '[ -f "$OUT/share/zero-hour-reforged/licenses/Zero-Hour-Reforged-LICENSE.md" ] && [ -f "$OUT/share/zero-hour-reforged/licenses/SDL3-LICENSE.txt" ]' \
	"the licences"
check 'grep -q "this notice may not be removed or altered" "$OUT/share/zero-hour-reforged/licenses/LZH-Light-LICENSE.txt" && grep -q "GNU GPL option, version 2 or later" "$OUT/share/zero-hour-reforged/licenses/FreeType-OPTION.txt" && [ -f "$OUT/share/zero-hour-reforged/licenses/FreeType-GPLv2.txt" ]' \
	"LZH-Light's notice, and FreeType's GPLv2-or-later option with its GPLv2 text"
check 'python3 -c "import struct,sys; d=open(sys.argv[1],\"rb\").read(); sys.exit(0 if d[:8]==b\"\\x89PNG\\r\\n\\x1a\\n\" and struct.unpack(\">II\", d[16:24])==(48,48) else 1)" "$OUT/share/icons/hicolor/48x48/apps/zero-hour-reforged.png"' \
	"the icon, a 48 px PNG"
check 'grep -q "^Exec=" "$OUT/share/applications/zero-hour-reforged.desktop" && grep -q -- "-root" "$OUT/README.txt"' \
	"the .desktop file, and README.txt with the launch-option line"
head_commit="$(git -C "$CODE" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)"
check 'grep -q "^commit $head_commit" "$OUT/VERSION" && grep -q "^built [0-9-]*T[0-9:]*Z$" "$OUT/VERSION"' \
	"VERSION names this commit ($head_commit) and the build time"
glibc="$(objdump -T "$OUT/bin/generals" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
check '[ "$(printf "%s\n%s\n" "$glibc" GLIBC_2.31 | sort -V | tail -1)" = GLIBC_2.31 ] && ! objdump -T "$OUT/bin/generals" | grep -qE "GLIBCXX_|CXXABI_"' \
	"generals needs $glibc at most GLIBC_2.31, and no libstdc++ symbol"

# 2. armed controls: the same build, one thing wrong, --no-build
fake="$T/fake-build"
mkdir -p "$fake"
for f in generals generated ffmpeg; do ln -s "$PB/$f" "$fake/$f"; done
cp -R -L "$PB/overlay" "$fake/overlay" && printf 'ShowHudOverlay = Yes\n' > "$fake/overlay/HudOn.ini"
cp "$PB/build.ninja" "$fake/build.ninja"
out="$(bash "$CODE/Tools/linux-portable.sh" --build "$fake" --out "$T/hud-on" --no-build --no-art --no-tar 2>&1)"; status=$?
check '[ $status -ne 0 ] && printf "%s" "$out" | grep -q "may force the HUD overlay on" && [ ! -e "$T/hud-on" ]' \
	"armed: an overlay forcing the HUD on is refused, and no folder is left"
rm -f "$fake/overlay/HudOn.ini"
sed -i 's#libwwlib\.a#libwwlib.a libnotlicensed.a#' "$fake/build.ninja"
out="$(bash "$CODE/Tools/linux-portable.sh" --build "$fake" --out "$T/unlicensed" --no-build --no-art --no-tar 2>&1)"; status=$?
check '[ $status -ne 0 ] && printf "%s" "$out" | grep -q "does not cover: notlicensed"' \
	"armed: a library macos-app-licenses.txt does not name is refused"

# 3. E1 through the packaged launcher, in the platform image
if [ -n "${ZH_DATA_DIR:-}" ] && docker image inspect "$PLATFORM" >/dev/null 2>&1 && [ $failures -eq 0 ]; then
	data="$(cd "$ZH_DATA_DIR" && pwd -P)"
	mkdir -p "$T/tmp"
	cat > "$T/in-platform" <<RUNNER
#!/bin/sh
exec docker run --rm -i -u $(id -u):$(id -g) -e HOME -e ZH_USER_DATA_DIR --network none -v "$T:$T" -v "$data:$data" -w "\$PWD" $PLATFORM "\$@"
RUNNER
	chmod +x "$T/in-platform"
	e1="$(TMPDIR="$T/tmp" REPLAY_CHECK_RUNNER="$T/in-platform" bash "$CODE/Tools/replay-check.sh" --package "$OUT" --data "$data" --seeds 0 --maxframes 1200 2>&1)"
	printf '%s\n' "$e1" | grep -E "HEADLESS CRC|agree|DIVERGED|no result" | sed 's/^/  /'
	check 'printf "%s" "$e1" | grep -q "the playback and the second run agree"' \
		"E1 through the packaged launcher in $PLATFORM: the recording, its playback and a second run agree"
else
	echo "note: part 3 not run (it needs ZH_DATA_DIR, $PLATFORM pulled, and parts 1 and 2 passing)"
fi

echo "$failures failure(s)"
[ $failures -eq 0 ]
