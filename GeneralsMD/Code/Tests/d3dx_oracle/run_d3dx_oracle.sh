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
# Compares d3dxportable.h against Microsoft's own D3DXVec4Transform, by running the DLL's machine
# code on this Mac.  d3dx_oracle.cpp explains the mechanism; this file says what it is worth.
#
# =============================================================================================
# WHAT THIS PROVES, AND WHAT IT DOES NOT
#
# It proves: on a million inputs plus the golden table, d3dxportable.h built for x86_64 computes
# the same bits as the scalar and non-Intel SSE bodies of d3dx9_43.dll 9.29.952.3111 x64.  It
# also measures how often the GenuineIntel body disagrees with them, and fails if that body
# differs anywhere except lane x of the Bezier basis.  That is the comparison with the real
# implementation that every other check in this project stands in for.  It compares against
# Microsoft's instructions, not against a reading of them.
#
# It does NOT prove:
#
#   - Which body a given Windows machine runs.  That is read from the DLL's dispatch code, not
#     observed.  Running the dispatch would mean running DllMain, the registry and CPUID, none of
#     which this does.
#   - Anything about arm64.  Both sides here are x86_64 code under Rosetta.  The arm64 half is
#     Tests/arch_diff's d3dx section, which prints a fingerprint over the SAME sweep.  The chain
#     only holds while this oracle and arch_diff agree on that fingerprint.  This script prints
#     it; compare it with arch_diff's d3dx/sweep-fingerprint row.
#   - That Rosetta executes SSE exactly as silicon does.  For finite IEEE add and multiply there
#     is one correct answer and the sweep contains no NaN or infinity.  Where the architectures
#     do differ (the NaN that inf*0 generates is 0xFFC00000 on x86 and 0x7FC00000 on arm64), this
#     oracle is silent by design.
#   - The MXCSR a real game process runs with.  Defaults here: no flush-to-zero, no
#     denormals-are-zero.  If the Windows game ran with either, denormal results would differ.
#     The sweep's inputs are normal numbers, but that has not been checked against a running game.
#   - The game's use of the result.  BezFwdIterator and DumbProjectileBehavior are not run.
#     What this pins is the function they call.
#
# A green run is "the arithmetic matches two of the DLL's three bodies".  It is not "matches
# Windows": an Intel Windows machine runs the third, and this oracle shows it disagreeing.
# =============================================================================================
#
# Needs Microsoft's DLL, which this repository does not and should not carry.  Every Steam install
# of the game ships it, in _CommonRedist/DirectX/Jun2010/Jun2010_d3dx9_43_x64.cab, and this script
# takes the cab or the extracted DLL:
#
#   ZH_D3DX9_X64=/path/to/Jun2010_d3dx9_43_x64.cab ctest -R d3dx_oracle --output-on-failure
#   run_d3dx_oracle.sh <source-dir> <work-dir> [cab-or-dll]
#
# Skips (77) without it, or without an x86_64 toolchain and Rosetta.  Fails on a DLL whose sha256
# is not the one the RVAs were read from, because different code would sit at those addresses.

set -uo pipefail

SKIP=77
EXPECTED_SHA256=84b900dbd7fa978d6e0caee26fc54f2f61d92c9c75d10b35f00e3e82cd1d67b4

here="$(cd "$(dirname "$0")" && pwd)"
code_root="${1:-$(cd "$here/../.." && pwd)}"
work="${2:-${TMPDIR:-/tmp}/zhr-d3dx-oracle}"
source="${3:-${ZH_D3DX9_X64:-}}"

say()  { echo "[d3dx-oracle] $1"; }
skip() { echo "[d3dx-oracle] SKIP: $1"; exit $SKIP; }
fail() { echo "[d3dx-oracle] ERROR: $1"; exit 1; }

[ -n "$source" ] || skip "no DLL supplied.  Set ZH_D3DX9_X64 to a Steam install's
    _CommonRedist/DirectX/Jun2010/Jun2010_d3dx9_43_x64.cab (or the d3dx9_43.dll inside it)."
[ -e "$source" ] || fail "ZH_D3DX9_X64 names $source, which does not exist."

command -v clang++ >/dev/null 2>&1 || skip "no clang++ on PATH"
mkdir -p "$work"
printf 'int main(void){return 0;}\n' > "$work/canary.c"
clang -arch x86_64 "$work/canary.c" -o "$work/canary_x86" 2>/dev/null \
  || skip "this clang cannot target x86_64"
"$work/canary_x86" >/dev/null 2>&1 \
  || skip "x86_64 binaries do not run here (Rosetta 2 is not installed)"

dll="$source"
case "$source" in
  *.cab|*.CAB)
    rm -rf "$work/cab" && mkdir -p "$work/cab"
    tar -xf "$source" -C "$work/cab" d3dx9_43.dll 2>/dev/null \
      || fail "could not extract d3dx9_43.dll from $source (bsdtar reads cabinet files)"
    dll="$work/cab/d3dx9_43.dll"
    ;;
esac

actual=$(shasum -a 256 "$dll" | awk '{print $1}')
[ "$actual" = "$EXPECTED_SHA256" ] || fail "$dll is not d3dx9_43.dll x64 9.29.952.3111.
    sha256 $actual
    wanted $EXPECTED_SHA256
    d3dx_oracle.cpp's RVAs were read from that one build and would point into other code here."

WW3D2="$code_root/Libraries/Source/WWVegas/WW3D2"
clang++ -arch x86_64 -O2 -std=c++17 -ffp-contract=off -I"$WW3D2" -I"$code_root/Tests" \
  "$here/d3dx_oracle.cpp" -o "$work/d3dx_oracle" 2>"$work/build.log" || {
    echo "[d3dx-oracle] ERROR: building the oracle failed:"
    sed 's/^/    /' "$work/build.log" | head -20
    exit 1
  }

say "running d3dx9_43.dll's own D3DXVec4Transform bodies under Rosetta"
"$work/d3dx_oracle" "$dll"
status=$?
[ $status -eq 0 ] && say "read the header of this file before calling that \"matches Windows\""
exit $status
