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
# Builds and tests the tree on Linux, in containers, from a Mac: {gcc, clang} x {arm64, amd64} in
# Release, and a fifth row, arm64 gcc in Debug.  Configure, build and ctest for each, then one table,
# then an exit status that is nonzero if any row did not pass.
#
# The Debug row is there because a Debug build is a different program: DEBUG_LOG, DEBUG_ASSERTCRASH
# and MEMORYPOOL_DEBUG compile in, reference symbols a Release build never names, and run checks a
# Release build never runs.  B1's tests linked in Release and not in Debug for exactly that reason,
# and nothing here saw it until a Debug build was tried by hand.  gcc, because it is the stricter of
# the two here (eager vtables, fortify).  arm64, because it is native and so the fast one.
#
# =============================================================================================
# WHAT THIS PROVES, AND WHAT IT DOES NOT
#
# It proves: the tree configures, builds and passes ctest under glibc and libstdc++, with GCC and
# with Clang, on both Linux architectures. That is where the non-Windows code stops leaning on
# whatever Apple's headers happened to include, and where GCC's stricter reading of the standard
# gets a say.
#
# It does NOT prove:
#   - Anything about Windows. No MSVC, no Windows SDK.
#   - Anything about a Linux the game runs on. Ubuntu 24.04's toolchain and glibc only. The
#     container has no display, no GPU and no sound, and nothing here launches the game.
#   - Anything the host filesystem hides.  The bind mount of the Mac's volume is case-insensitive
#     even inside the container, so each row builds from a copy on the container's own
#     case-sensitive filesystem, and checks that a wrongly-cased name really fails to resolve
#     before it trusts that copy.  Without the copy, this check could not see include-case bugs.
#   - A real amd64 machine. The amd64 rows run under Rosetta inside OrbStack's VM. That is the
#     same translation layer E3 relies on, and it has the same blind spots.
#   - Anything ctest does not cover. Targets kept out of the default build (EXCLUDE_FROM_ALL,
#     ZH_NOT_YET_PORTABLE) are not built at all, and a DISABLED test is not a passing one.
#   - Parity with macOS by itself. The table shows each row's disabled and skipped tests so the
#     comparison can be made. arch_differential skips on Linux by design (it needs Darwin's
#     -arch and Rosetta), and d3dx_oracle skips unless ZH_D3DX9_X64 is set, which it never is
#     here.
#
# It fails, rather than passes, when it cannot run: no docker, no daemon, or vendored sources
# missing. A check that cannot run and says "ok" is the failure this project has hit five times.
# =============================================================================================
#
#   linux-check.sh                           all five rows
#   linux-check.sh arm64/gcc amd64/clang/Debug  only the named rows: arch/compiler[/config], with
#                                               arm64|amd64, gcc|clang, Release (default)|Debug
#
# Logs go to $ZH_LINUX_CHECK_WORK (default ${TMPDIR:-/tmp}/zhr-linux-check), one per row.
# Run Tools/vendor.sh on the host first: the repository is mounted read-only and the vendored
# sources live inside it.

set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
work="${ZH_LINUX_CHECK_WORK:-${TMPDIR:-/tmp}/zhr-linux-check}"
mkdir -p "$work"

say()  { echo "[linux-check] $1"; }
fail() { echo "[linux-check] ERROR: $1"; exit 2; }

# --- can this machine do the job at all? Not being able to is a failure, not a skip. ----------
command -v docker >/dev/null 2>&1 || fail "docker is not on PATH. This check cannot run, so it has not passed."
docker info >/dev/null 2>&1 || fail "docker is installed but its daemon does not answer (is OrbStack running?)."

# --- one run at a time, machine-wide -----------------------------------------------------------
# Every row builds the whole tree inside a container, and OrbStack's disk image grows on the host
# with each one.  On 2026-09-26 three checkouts ran four rows each at once, filled a 460 GB disk
# and stopped the Docker daemon.  So runs queue on one lock, at a fixed path every checkout shares.
# A second run waits and says who it is waiting for; a lock whose owner has died is taken over.
# mkdir is the lock because it is atomic everywhere and needs no flock(1), which macOS lacks.
LOCK=/tmp/zhr-linux-check.lock
while ! mkdir "$LOCK" 2>/dev/null; do
  holder=$(cat "$LOCK/owner" 2>/dev/null)
  holder_pid=${holder%% *}
  if [ -n "$holder_pid" ] && ! kill -0 "$holder_pid" 2>/dev/null; then
    say "taking over a stale lock from a run that is no longer alive ($holder)"
    rm -rf "$LOCK"
    continue
  fi
  say "waiting for another linux-check run to finish: ${holder:-owner not yet recorded}"
  sleep 20
done
echo "$$ $(date '+%Y-%m-%d %H:%M:%S') $root ${*:-all rows}" > "$LOCK/owner"
trap 'rm -rf "$LOCK"' EXIT

# --- room to work -----------------------------------------------------------------------------
# A row needs a few GB inside OrbStack's disk image, which lives on this volume.  Below the floor,
# refuse rather than find out halfway through by stopping the Docker daemon.
MIN_FREE_GB=${ZH_LINUX_CHECK_MIN_FREE_GB:-15}
free_gb=$(df -Pk "$work" | awk 'NR==2 { printf "%d", $4 / 1048576 }')
say "free disk: ${free_gb} GB (floor ${MIN_FREE_GB} GB)"
[ "$free_gb" -ge "$MIN_FREE_GB" ] || fail "only ${free_gb} GB free, below the ${MIN_FREE_GB} GB floor.  Free space first; this run would fill the disk and stop the Docker daemon."

# The vendored sources are fetched, not tracked, and an empty directory builds nothing and says so
# cheerfully. Check the ones configure needs; a missing one is the host's job to fix.
for d in GeneralsMD/Code/Libraries/Source/Compression/ZLib GeneralsMD/Code/Libraries/Source/GameSpy; do
  [ -n "$(ls -A "$root/$d" 2>/dev/null | grep -v '^\.gitignore$')" ] \
    || fail "$d is empty. Run GeneralsMD/Code/Tools/vendor.sh on the host first."
done

# The file list for the containers' copy, computed here, where git works.  Inside the container it
# does not: a worktree's .git is a file naming the main repository's .git/worktrees/<name> by a
# host path the container cannot see.  Tracked files by git's names, minus any deleted in the
# working tree, then untracked files (the vendored sources) by the disk's.
filelist="$work/files.list"
git -C "$root" ls-files -z --cached -- GeneralsMD/Code > "$work/cached.list" \
  && git -C "$root" ls-files -z --deleted -- GeneralsMD/Code > "$work/deleted.list" \
  && git -C "$root" ls-files -z --others -- GeneralsMD/Code > "$work/others.list" \
  || fail "git could not list the tree at $root"
python3 - "$work" <<'PY' || fail "could not build the file list"
import sys, os
w = sys.argv[1]
read = lambda n: [p for p in open(os.path.join(w, n), 'rb').read().split(b'\0') if p]
deleted = set(read('deleted.list'))
paths = [p for p in read('cached.list') if p not in deleted] + read('others.list')
open(os.path.join(w, 'files.list'), 'wb').write(b'\0'.join(paths) + b'\0')
print(f"[linux-check] copying {len(paths)} files into each container ({len(deleted)} deleted, skipped)")
PY
[ "$(tr -cd '\0' < "$filelist" | wc -c)" -gt 1000 ] || fail "the file list is nearly empty; refusing to test nothing"

rows=("$@")
[ ${#rows[@]} -gt 0 ] || rows=(arm64/gcc arm64/clang amd64/gcc amd64/clang arm64/gcc/Debug)

# --- one image per architecture, built on first use --------------------------------------------
# The X11, Wayland, EGL, DRM and Vulkan headers are SDL3's: without them its configure stops with
# "could not find X11 or Wayland development libraries".  spirv-tools is for test_shader_sdl, which
# validates every generated program's SPIR-V with spirv-val and says SKIPPED where there is none.  The image tag carries a hash of this text,
# so changing the list builds a new image rather than silently reusing an old one.
DOCKERFILE='FROM ubuntu:24.04
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
      cmake ninja-build g++ clang python3 git pkg-config \
      libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev \
      libxtst-dev libwayland-dev libxkbcommon-dev wayland-protocols libegl-dev libdrm-dev \
      libgbm-dev libvulkan-dev spirv-tools >/dev/null && rm -rf /var/lib/apt/lists/*'
IMAGE_HASH=$(printf '%s' "$DOCKERFILE" | shasum | cut -c1-12)

ensure_image() { # arch
  local image="zhr-linux-check:ubuntu24.04-$1-$IMAGE_HASH"
  if ! docker image inspect "$image" >/dev/null 2>&1; then
    say "building $image (first run with this package list)" >&2
    printf '%s\n' "$DOCKERFILE" | docker build -q --platform "linux/$1" -t "$image" - >/dev/null \
      || fail "could not build $image"
    remove_superseded_images >&2
  fi
  echo "$image"
}

# Images this script built for an older package list.  Only this repository's name, never a
# `docker system prune`: other projects keep images on the same daemon.  The build cache is left
# alone for the same reason - Docker cannot prune the cache of one image's builds without the rest.
# And only images more than a day old: two branches with different package lists can be in use on
# one machine at once, and removing the other branch's fresh image makes each rebuild the other's
# forever.  (Found by testing this function: it would have removed an image another run was
# using at that moment - Docker refused, but the next run to start would have rebuilt it.)
remove_superseded_images() {
  local old
  old=$(docker image ls --filter until=24h --format '{{.Repository}}:{{.Tag}}' 'zhr-linux-check' \
    | grep -v -- "-$IMAGE_HASH\$")
  [ -z "$old" ] && return 0
  say "removing superseded images: $(echo $old)"
  docker image rm $old >/dev/null || say "note: could not remove every superseded image"
}

# --- one row: configure, build, ctest inside a throwaway container ----------------------------
# The container prints @@ marker lines; everything else is the log.
run_row() { # arch compiler config logfile
  local arch="$1" compiler="$2" config="$3" log="$4" image cc cxx
  image=$(ensure_image "$arch") || exit 2
  case "$compiler" in
    gcc)   cc=gcc   cxx=g++ ;;
    clang) cc=clang cxx=clang++ ;;
    *) fail "unknown compiler $compiler" ;;
  esac
  # -i: without it docker does not forward stdin, bash reads an empty script, and the row does
  # nothing at all.  The first version of this script had exactly that bug; @@ran below catches it.
  docker run -i --rm --platform "linux/$arch" -v "$root":/src:ro -v "$work":/lists:ro \
    -e CC="$cc" -e CXX="$cxx" -e CONFIG="$config" "$image" \
    bash -s > "$log" 2>&1 <<'INSIDE'
set -u
# The repository arrives through a bind mount of the Mac's volume, which stays case-INSENSITIVE
# inside the container (measured: Vector.H and vector.h are one inode there).  A real Linux is
# case-sensitive, so build from a copy on the container's own filesystem.
#
# And copy TRACKED files under git's names, not the disk's.  On a case-folding host a file can be
# buff.h on disk and BUFF.H in the index - a reset of a staged case-only rename leaves exactly that,
# and it happened while this script was written - and a copy of the disk would build a tree that
# git does not hold.  Untracked files (the vendored sources) have no index name and keep the disk's.
#
# The control: git stores WWLib's header as Vector.H, so vector.h must NOT resolve in the copy.  If
# it does, this row is not testing Linux and says so.
mkdir -p /tmp/src
cd /src && xargs -0 cp --parents -t /tmp/src < /lists/files.list || { echo "@@copyfailed"; exit 0; }
cd /
if [ -e /tmp/src/GeneralsMD/Code/Libraries/Source/WWVegas/WWLib/vector.h ]; then
  echo "@@casefold yes"
  exit 0
fi
echo "@@ran $(uname -m) $($CXX --version | head -1), case-sensitive copy"
B=/tmp/build
cmake -S /tmp/src/GeneralsMD/Code -B $B -G Ninja -DCMAKE_BUILD_TYPE=$CONFIG
echo "@@configure $?"
[ -e $B/build.ninja ] || exit 0
cmake --build $B -- -k 0 2>&1 | tee /tmp/build.log
status=${PIPESTATUS[0]}
echo "@@build $status $(grep -c '^FAILED:' /tmp/build.log)"
grep '^FAILED:' /tmp/build.log | sed 's/^/@@failed /'
cd $B && ctest --output-on-failure 2>&1 | tee /tmp/ctest.log
echo "@@ctest ${PIPESTATUS[0]}"
sed -n 's/^.*tests passed, \([0-9]*\) tests failed out of \([0-9]*\).*$/@@counts \1 \2/p' /tmp/ctest.log
grep -E 'Not Run \(Disabled\)' /tmp/ctest.log | sed -E 's/^.*Test +#[0-9]+: ([^ ]+).*/@@disabled \1/'
grep -E '\*\*\*Skipped' /tmp/ctest.log | sed -E 's/^.*Test +#[0-9]+: ([^ ]+).*/@@skipped \1/'
grep -E '\*\*\*(Failed|Not Run)( |$)|\*\*\*Exception' /tmp/ctest.log | grep -v Disabled \
  | sed -E 's/^.*Test +#[0-9]+: ([^ ]+).*/@@testfail \1/'
INSIDE
}

field() { sed -n "s/^@@$2 //p" "$1" | head -1; }
list()  { sed -n "s/^@@$2 //p" "$1" | tr '\n' ' ' | sed 's/ $//'; }

overall=0
table=()
for row in "${rows[@]}"; do
  IFS=/ read -r arch compiler config <<<"$row"
  config="${config:-Release}"
  case "$arch" in arm64|amd64) ;; *) fail "unknown architecture in '$row' (arm64|amd64)";; esac
  case "$config" in Release|Debug) ;; *) fail "unknown configuration in '$row' (Release|Debug)";; esac
  suffix=""; [ "$config" = Release ] || suffix="-$config"
  log="$work/$arch-$compiler$suffix.log"
  say "linux/$arch $compiler $config ... (log: $log)"
  start=$(date +%s)
  run_row "$arch" "$compiler" "$config" "$log"
  secs=$(( $(date +%s) - start ))

  cfg=$(field "$log" configure); cfg=${cfg:-none}
  read -r bstat bfailed <<<"$(field "$log" build)"; bstat=${bstat:-none}; bfailed=${bfailed:-?}
  ctest_status=$(field "$log" ctest); ctest_status=${ctest_status:-none}
  read -r tfailed ttotal <<<"$(field "$log" counts)"; tfailed=${tfailed:-?}; ttotal=${ttotal:-?}
  disabled=$(list "$log" disabled); skipped=$(list "$log" skipped); testfail=$(list "$log" testfail)

  ran=$(field "$log" ran)
  verdict=PASS
  if [ "$cfg" != 0 ] || [ "$bstat" != 0 ] || [ "$ctest_status" != 0 ]; then verdict=FAIL; overall=1; fi
  # A row that ran nothing, or ran ctest over nothing, is a failure however the fields read.
  if [ -n "$(field "$log" casefold)" ]; then
    say "linux/$arch $compiler: the source copy is case-insensitive, so this row cannot test Linux"
  fi
  if [ -z "$ran" ]; then verdict=FAIL; overall=1; say "linux/$arch $compiler: the container ran nothing"; fi
  if [ "$ttotal" = "?" ] || [ "$ttotal" = 0 ]; then verdict=FAIL; overall=1; fi
  if [ "$ttotal" != "?" ] && [ "$tfailed" != "?" ]; then passed="$((ttotal - tfailed))/$ttotal"; else passed="-"; fi
  table+=("$(printf '%-12s %-6s %-8s %-9s %-15s %-8s %-5s %4ss' "linux/$arch" "$compiler" "$config" \
      "$( [ "$cfg" = 0 ] && echo ok || echo "FAIL($cfg)")" \
      "$( [ "$bstat" = 0 ] && echo ok || echo "FAIL $bfailed edges")" \
      "$passed" "$verdict" "$secs")")
  table+=("$(printf '             ran: %s' "${ran:-NOTHING}")")
  table+=("$(printf '             failing: %s' "${testfail:-none}")")
  table+=("$(printf '             disabled: %s' "${disabled:-none}")")
  table+=("$(printf '             skipped: %s' "${skipped:-none}")")
done

echo
printf '%-12s %-6s %-8s %-9s %-15s %-8s %-5s %5s\n' platform cc config configure build ctest verdict time
for line in "${table[@]}"; do echo "$line"; done
echo
if [ $overall -eq 0 ]; then
  say "all rows passed - and read the header of this file before calling that more than Linux"
else
  say "FAIL: at least one row did not configure, build or pass ctest. Logs: $work"
fi
exit $overall
