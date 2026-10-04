#!/usr/bin/env python3
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
# license-headers.py: the licence notices on the files the macOS/Linux port adds or changes (NOTICE.md).
#
# The rules, decided by the port's copyright holder on 2026-09-27:
#   - a file EA released and the port modified keeps EA's header byte for byte, and gets MOD_LINE under it
#     (GPLv3 section 5(a); EA's additional term "modified versions ... must be marked as such");
#   - a file the port added carries the port's header (PORT_C / PORT_HASH), never EA's: EA did not write it.
#     PORT_C has exactly the 17 lines of EA's block, so replacing one with the other moves no line number;
#   - a file the port added that holds EA's code (DERIVED_EA) keeps or gets EA's header plus a line naming
#     the EA file it came from;
#   - a file the port added that adapts upstream code (ADAPTED_UPSTREAM) gets the port's header plus a line
#     crediting the upstream file and its author;
#   - no header names an Electronic Arts trademark: EA's section 7 terms forbid distributing a modification
#     "using any Electronic Arts trademark".
#
# "Added" and "modified" are against the fork point: git merge-base HEAD upstream/main, or --base.
#
# Usage: license-headers.py --check | --apply [--base <ref>]
#   --check  lists every file that breaks a rule; exit 1 if any does
#   --apply  fixes them in the working tree (bytes in, bytes out: line endings and encodings are kept)

import argparse
import os
import subprocess
import sys

HOLDER = "İlyas Akın"
HOLDER_ASCII = "Ilyas Akin"  # only for the few legacy CP1252 files, which cannot encode İ or ı

GPL_LINES = [
	"This program is free software: you can redistribute it and/or modify",
	"it under the terms of the GNU General Public License as published by",
	"the Free Software Foundation, either version 3 of the License, or",
	"(at your option) any later version.",
	"",
	"This program is distributed in the hope that it will be useful,",
	"but WITHOUT ANY WARRANTY; without even the implied warranty of",
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the",
	"GNU General Public License for more details.",
	"",
	"You should have received a copy of the GNU General Public License",
	"along with this program.  If not, see <http://www.gnu.org/licenses/>.",
]
BODY = ["Copyright 2026 " + HOLDER, "Additional terms under GNU GPL section 7 apply: see LICENSE.md.", ""] + GPL_LINES
PORT_C = ["/*"] + [("**\t" + l) if l else "**" for l in BODY] + ["*/"]
PORT_HASH = [("#\t" + l) if l else "#" for l in BODY]
assert len(PORT_C) == 17

MOD_MARK = "for the macOS/Linux port"
def mod_line(holder):
	return "// Modified 2026 by " + holder + " " + MOD_MARK + "; see NOTICE.md and the git history."

# Port files that hold EA's code (the provenance audit of 2026-09-27): path -> (how, EA source files).
CODE = "GeneralsMD/Code/"
DERIVED_EA = {
	CODE + "GameEngine/Source/GameLogic/Map/TerrainHeightSampling.cpp": ("moved", ["GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp", "GameEngineDevice/Include/W3DDevice/GameClient/BaseHeightMap.h"]),
	CODE + "GameEngine/Source/Common/MapObject.cpp": ("moved", ["GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp"]),
	CODE + "GameEngine/Source/GameLogic/Map/WorldHeightMapData.cpp": ("moved", ["GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp"]),
	CODE + "GameEngine/Include/GameLogic/WorldHeightMapData.h": ("moved", ["GameEngineDevice/Include/W3DDevice/GameClient/WorldHeightMap.h"]),
	CODE + "GameEngine/Source/GameLogic/Map/WaterGridMotion.cpp": ("moved", ["GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp"]),
	CODE + "GameEngine/Include/GameLogic/WaterGridMotion.h": ("partly", ["GameEngineDevice/Include/W3DDevice/GameClient/W3DWater.h"]),
	CODE + "Main/PosixMain.cpp": ("partly", ["Main/WinMain.cpp"]),
	CODE + "GameEngine/Source/Common/System/WWDownloadRegistryPosix.cpp": ("partly", ["Libraries/Source/WWVegas/WWDownload/urlBuilder.cpp"]),
	CODE + "GameEngineDevice/Source/SdlDevice/GameClient/SdlMouse.cpp": ("partly", ["GameEngineDevice/Source/Win32Device/GameClient/Win32Mouse.cpp"]),
	CODE + "GameEngineDevice/Source/PosixDevice/Common/PosixLocalFileSystem.cpp": ("partly", ["GameEngineDevice/Source/Win32Device/Common/Win32LocalFileSystem.cpp"]),
	CODE + "GameEngine/Source/GameClient/GUI/IMEManagerPosix.cpp": ("partly", ["GameEngine/Source/GameClient/GUI/IMEManager.cpp"]),
	CODE + "GameEngine/Source/GameNetwork/WOLBrowser/WebBrowserPosix.cpp": ("partly", ["GameEngine/Source/GameNetwork/WOLBrowser/WebBrowser.cpp"]),
	CODE + "Tests/fs_oracle.cpp": ("partly", ["GameEngineDevice/Source/Win32Device/Common/Win32LocalFileSystem.cpp"]),
}
# Port files that adapt code from upstream (every source below is Olcay Seygan's, by git log).
UPSTREAM_AUTHOR = "Olcay Seygan"
ADAPTED_UPSTREAM = {
	CODE + "Libraries/Source/WWVegas/Miles6/miniaudio/miles_miniaudio.cpp": ["Libraries/Source/WWVegas/Miles6/xaudio2/miles_xaudio2.cpp"],
	CODE + "GameEngineDevice/Source/PosixDevice/Render/SdlConstants.h": ["Libraries/Source/WWVegas/WW3D2/dx11backend.h"],
	CODE + "Libraries/Source/WWVegas/WW3D2/dx11runtime_posix.cpp": ["Libraries/Source/WWVegas/WW3D2/dx11runtime.cpp"],
	CODE + "Libraries/Source/WWVegas/WW3D2/d3dx9posix.cpp": ["Libraries/Source/WWVegas/WW3D2/d3dx9runtime.cpp"],
	CODE + "GameEngineDevice/Source/PosixDevice/Render/PosixDevice9Draw.cpp": ["Libraries/Source/WWVegas/WW3D2/dx11backend.cpp"],
	CODE + "Tests/shader_cases.h": ["Tests/test_ffvertex.cpp", "Tests/test_ffshadercompile.cpp"],
	CODE + "Tools/vendor.sh": ["Tools/vendor.ps1"],
	CODE + "Tools/ffmpeg-build-posix.sh": ["Tools/ffmpeg-build.sh"],
}
# Added files that take no header: generated data a test compares whole, and patches of third-party code
# (whose own licences govern them; their added lines are marked "Zero Hour Reforged:").
SKIP = {
	CODE + "BuildFingerprint.manifest",
	CODE + "Tests/binkvideo_golden.inc",
	CODE + "Tests/glyph_rasteriser_golden.inc",
	CODE + "Tests/w3d_view/shaders/model_shaders.h",
}
# Added folders of third-party build output, committed as they came out of their build: their own licence
# (the LICENSE.txt beside them) governs every file, and ours would be wrong on any of them.  FFmpeg's dist/
# came from upstream and is not an added file; dist-arm64/ is the same build for ARM64.
SKIP_DIRS = (
	CODE + "Libraries/Source/FFmpeg/dist-arm64/",
)
C_EXT = {"c", "cc", "cpp", "cxx", "h", "hh", "hpp", "inc", "m", "mm", "metal", "frag", "vert", "hlsl"}
HASH_EXT = {"sh", "py", "ps1", "cmake"}


def git(*args):
	return subprocess.run(["git"] + list(args), check=True, capture_output=True, text=True).stdout


def comment_style(path, data):
	name = os.path.basename(path)
	ext = name.rsplit(".", 1)[-1].lower() if "." in name else ""
	if path.endswith(".patch") or path in SKIP or path.startswith(SKIP_DIRS):
		return None
	if ext in C_EXT:
		return "c"
	if ext in HASH_EXT or (ext == "" and data.startswith(b"#!")):
		return "hash"
	return None


def split_lines(data):
	"""-> (bom, lines without their endings, the file's line ending, whether the last line ends)"""
	bom = b""
	if data.startswith(b"\xef\xbb\xbf"):
		bom, data = data[:3], data[3:]
	eol = b"\r\n" if b"\r\n" in data[:4096] else b"\n"
	ends = data.endswith(b"\n")
	lines = data.split(b"\n")
	if ends:
		lines = lines[:-1]
	if eol == b"\r\n":
		lines = [l[:-1] if l.endswith(b"\r") else l for l in lines]
	return bom, lines, eol, ends


def join_lines(bom, lines, eol, ends):
	return bom + eol.join(lines) + (eol if ends else b"")


def ea_block(lines):
	"""-> (start, end) of EA's /* ... */ header block in the first 30 lines, or None"""
	if not lines or lines[0].strip() != b"/*":
		return None
	for k in range(1, min(30, len(lines))):
		if lines[k].strip() == b"*/":
			text = b"\n".join(lines[:k + 1])
			if b"Electronic Arts" in text and b"General Public License" in text:
				return (0, k)
			return None
	return None


def port_block_end(lines, style):
	"""-> index of the port header's last line if the file starts with it (after a shebang), else None"""
	first = 1 if lines and lines[0].startswith(b"#!") else 0
	want = [l.encode() for l in (PORT_C if style == "c" else PORT_HASH)]
	if lines[first:first + len(want)] == want:
		return first + len(want) - 1
	return None


def holder_for(data):
	try:
		data.decode("utf-8")
		return HOLDER
	except UnicodeDecodeError:
		return HOLDER_ASCII


def src_list(srcs):
	return " and ".join(CODE + s for s in srcs)


def derived_line(path, holder):
	how, srcs = DERIVED_EA[path]
	if how == "moved":
		return "// Modified 2026 by " + holder + " " + MOD_MARK + ": moved here from " + src_list(srcs) + "; see NOTICE.md and the git history."
	return ("// Modified 2026 by " + holder + " " + MOD_MARK + ": parts come from " + src_list(srcs)
		+ ", the rest is new code, copyright 2026 " + holder + "; see NOTICE.md and the git history.")


def upstream_line(path, style):
	lead = "//" if style == "c" else "#"
	return lead + " Portions adapted from " + src_list(ADAPTED_UPSTREAM[path]) + " by " + UPSTREAM_AUTHOR + " (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later."


def fix_modified(path, data):
	"""An EA file the port changed: EA's block stays, MOD_LINE goes under it. -> new bytes, or None if fine."""
	bom, lines, eol, ends = split_lines(data)
	block = ea_block(lines)
	if block is None:
		return None
	line = mod_line(holder_for(data)).encode("utf-8")
	after = block[1] + 1
	if any(MOD_MARK.encode() in l for l in lines[after:after + 3]):
		return None
	return join_lines(bom, lines[:after] + [line] + lines[after:], eol, ends)


def fix_added(path, data, ea_template):
	"""A file the port added. -> (new bytes or None, a problem the tool cannot fix or None)"""
	style = comment_style(path, data)
	if style is None:
		return None, None
	bom, lines, eol, ends = split_lines(data)
	holder = holder_for(data)
	block = ea_block(lines)
	if path in DERIVED_EA:
		want = derived_line(path, holder).encode("utf-8")
		if block is None:
			if style != "c":
				return None, "derived from EA but not a C-style file"
			lines = ea_template + lines
			block = (0, len(ea_template) - 1)
		after = block[1] + 1
		if lines[after:after + 1] == [want]:
			return None, None
		lines = [l for i, l in enumerate(lines) if not (after <= i < after + 3 and MOD_MARK.encode() in l)]
		return join_lines(bom, lines[:after] + [want] + lines[after:], eol, ends), None
	header = [l.encode("utf-8") for l in (PORT_C if style == "c" else PORT_HASH)]
	if holder != HOLDER:
		header = [l.replace(HOLDER.encode("utf-8"), HOLDER_ASCII.encode()) for l in header]
	credit = [upstream_line(path, style).encode("utf-8")] if path in ADAPTED_UPSTREAM else []
	if block is not None:
		if style != "c":
			return None, "EA block in a non-C file"
		new = lines[:block[0]] + header + credit + lines[block[1] + 1:]
	else:
		end = port_block_end(lines, style)
		if end is not None:
			if not credit or lines[end + 1:end + 2] == credit:
				return None, None
			new = lines[:end + 1] + credit + lines[end + 1:]
		else:
			first = 0
			if lines and lines[0].startswith(b"#!"):
				first = 1
				if style == "hash" and len(lines) > 1 and b"coding" in lines[1] and lines[1].startswith(b"#"):
					first = 2
			new = lines[:first] + header + credit + lines[first:]
	return join_lines(bom, new, eol, ends), None


def main():
	ap = argparse.ArgumentParser()
	mode = ap.add_mutually_exclusive_group(required=True)
	mode.add_argument("--check", action="store_true")
	mode.add_argument("--apply", action="store_true")
	ap.add_argument("--base")
	args = ap.parse_args()
	root = git("rev-parse", "--show-toplevel").strip()
	os.chdir(root)
	base = args.base or git("merge-base", "HEAD", "upstream/main").strip()

	tmpl_src = git("show", base + ":" + CODE + "GameEngineDevice/Source/Win32Device/Common/Win32LocalFileSystem.cpp")
	tmpl_lines = split_lines(tmpl_src.encode("utf-8"))[1]
	tmpl = tmpl_lines[:ea_block(tmpl_lines)[1] + 1]

	changes, problems = [], []
	for row in git("diff", "--name-status", "-M", base, "--").splitlines():
		st, *paths = row.split("\t")
		path = paths[-1]
		if not os.path.isfile(path):
			continue
		data = open(path, "rb").read()
		if st == "A":
			new, problem = fix_added(path, data, tmpl)
		elif st == "M" or st.startswith("R"):
			new, problem = fix_modified(path, data), None
		else:
			continue
		if problem:
			problems.append(path + ": " + problem)
		if new is not None and new != data:
			changes.append((path, new))
	for path in list(DERIVED_EA) + list(ADAPTED_UPSTREAM):
		if not os.path.isfile(path):
			problems.append(path + ": named in the tool's tables but not in the tree")

	for path, new in changes:
		if args.apply:
			open(path, "wb").write(new)
		else:
			print("needs its notice: " + path)
	for p in problems:
		print("PROBLEM: " + p)
	print(("fixed " if args.apply else "") + "%d file(s); %d problem(s)" % (len(changes), len(problems)))
	return 1 if problems or (changes and args.check) else 0


if __name__ == "__main__":
	sys.exit(main())
