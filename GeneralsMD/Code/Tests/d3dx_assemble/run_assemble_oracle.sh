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
# A3e-asm: Microsoft's own D3DXAssembleShader against FFReference's ps.1.1 assembler, on the game's four
# water programs.  assemble_oracle.cpp explains the mechanism and its one departure (three C-runtime
# sites that read the Windows thread block); this file says what it is worth.
#
# =============================================================================================
# WHAT THIS PROVES, AND WHAT IT DOES NOT
#
# It proves: for each ps.1.1 text W3DWater.cpp hands D3DXAssembleShader, the June 2010 d3dx9_43 x64
# (with the D3DCompiler_43 it loads, as on Windows) emits exactly the tokens FFRef::assemblePixelProgram
# emits, word for word, and they decode under FFReference's census.  So what a Windows machine's D3DX
# makes of those texts is pinned, and the oracle's second reading of them agrees.
#
# It does NOT prove:
#   - What the port hands its device.  The port's D3DXAssembleShader is not an assembler: it carries
#     the text to the device in a comment (decision: it stays so; the device picks a transcription by
#     the registered name).  test_ffprogram's stub check pins that the text arrives exactly.
#   - Anything about other D3DX builds.  Both DLLs are sha256-pinned; the patched sites are this
#     build's.
#   - Behaviour on error paths.  Every import the DLLs never called here is a trap, and exceptions
#     stop the run.  Valid text only.
# =============================================================================================
#
# Needs Microsoft's DLLs, which this repository does not and should not carry.  Every Steam install of
# the game ships both cabs, in _CommonRedist/DirectX/Jun2010/:
#
#   ZH_D3DX9_X64=/path/to/Jun2010_d3dx9_43_x64.cab ctest -R d3dx_assemble_oracle --output-on-failure
#   (Jun2010_D3DCompiler_43_x64.cab is taken from beside it, or from ZH_D3DCOMPILER_X64)
#
# Skips (77) without them, or without an x86_64 toolchain and Rosetta.  Refuses a Wine builtin and any
# DLL whose sha256 is not the pinned one.

set -uo pipefail

SKIP=77
D3DX_SHA256=84b900dbd7fa978d6e0caee26fc54f2f61d92c9c75d10b35f00e3e82cd1d67b4
COMPILER_SHA256=44c3a7e330b54a35a9efa015831392593aa02e7da1460be429d17c3644850e8a

here="$(cd "$(dirname "$0")" && pwd)"
code_root="${1:-$(cd "$here/../.." && pwd)}"
work="$(mktemp -d "${TMPDIR:-/tmp}/assemble-oracle.XXXXXX")" || work=""
if [ -z "$work" ] || [ ! -d "$work" ]; then
	echo "run_assemble_oracle: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
trap 'rm -rf -- "${work:?}"' EXIT
d3dx_source="${ZH_D3DX9_X64:-}"

say()  { echo "[assemble-oracle] $1"; }
skip() { echo "[assemble-oracle] SKIP: $1"; exit $SKIP; }
fail() { echo "[assemble-oracle] ERROR: $1"; exit 1; }

[ -n "$d3dx_source" ] || skip "no DLL supplied.  Set ZH_D3DX9_X64 to a Steam install's
    _CommonRedist/DirectX/Jun2010/Jun2010_d3dx9_43_x64.cab (or the d3dx9_43.dll inside it)."
[ -e "$d3dx_source" ] || fail "ZH_D3DX9_X64 names $d3dx_source, which does not exist."
compiler_source="${ZH_D3DCOMPILER_X64:-$(dirname "$d3dx_source")/Jun2010_D3DCompiler_43_x64.cab}"
[ -e "$compiler_source" ] || skip "no D3DCompiler_43: $compiler_source does not exist (set ZH_D3DCOMPILER_X64)."

command -v clang++ >/dev/null 2>&1 || skip "no clang++ on PATH"
printf 'int main(void){return 0;}\n' > "$work/canary.c"
clang -arch x86_64 "$work/canary.c" -o "$work/canary" 2>/dev/null || skip "this clang cannot target x86_64"
"$work/canary" >/dev/null 2>&1 || skip "x86_64 binaries do not run here (Rosetta 2 is not installed)"

# The sources are only read; they are hashed before and after all the same
before="$(shasum -a 256 "$d3dx_source" "$compiler_source")"

extract() {	# extract <cab or dll> <member> -> the DLL's path
	case "$1" in
		*.cab|*.CAB)
			mkdir -p "$work/cab"
			( cd "$work/cab" && tar -xf "$1" "$2" 2>/dev/null ) || fail "could not extract $2 from $1"
			echo "$work/cab/$2" ;;
		*) echo "$1" ;;
	esac
}
d3dx="$(extract "$d3dx_source" d3dx9_43.dll)"
compiler="$(extract "$compiler_source" D3DCompiler_43.dll)"
for pair in "$d3dx:$D3DX_SHA256" "$compiler:$COMPILER_SHA256"; do
	dll="${pair%:*}"; wanted="${pair##*:}"
	grep -q -a "Wine builtin DLL" "$dll" && fail "$dll is a Wine builtin, not Microsoft's"
	actual=$(shasum -a 256 "$dll" | awk '{print $1}')
	[ "$actual" = "$wanted" ] || fail "$dll is not the pinned build (sha256 $actual, wanted $wanted);
    assemble_oracle.cpp's patch sites were read from that one build."
done

clang++ -arch x86_64 -O1 -std=c++17 -I"$code_root/Tests" "$here/assemble_oracle.cpp" \
	"$code_root/Tests/ffreference/ffprogram.cpp" -o "$work/assemble_oracle" 2>"$work/build.log" || {
	echo "[assemble-oracle] ERROR: building the oracle failed:"; sed 's/^/    /' "$work/build.log" | head -20; exit 1; }

say "running Microsoft's D3DXAssembleShader (and the D3DCompiler_43 it loads) under Rosetta"
"$work/assemble_oracle" "$d3dx" "$compiler" "$code_root/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp"
status=$?
after="$(shasum -a 256 "$d3dx_source" "$compiler_source")"
[ "$before" = "$after" ] || { say "FAIL: a source cab changed while it was read"; status=1; }
exit $status
