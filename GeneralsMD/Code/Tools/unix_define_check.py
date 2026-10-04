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
"""Fail if anything defines _UNIX: a #define in any source file under the tree, or -D_UNIX on a
compile command.

    python3 Tools/unix_define_check.py <build>/compile_commands.json <source root>

Why.  _UNIX is the switch for Westwood's abandoned 1990s UNIX port, which runs through WWVegas at
about sixty sites, and the port's rules say never to turn it on (PORTING.md, "Never
define _UNIX").  It was on anyway: the vendored GameSpy SDK's gsplatform.h defined it on Linux and
Apple, so every engine file that included a GameSpy header had it from that point, and 34 of them
did.  Tools/vendor.sh now renames the SDK's macro to GSI_UNIX.  This check keeps a definition from
coming back by any route: a vendored header, an engine header, or a compile flag.  MSVCCompat.h's
#error is the other half: it fires at compile time wherever _UNIX is defined before it is reached.

It walks the filesystem itself, rather than asking git or grep for the file list.  Every vendored
directory here holds a committed .gitignore containing `*`, so anything that honours .gitignore -
git, and this machine's grep, which is ugrep - never sees the vendored sources, and the vendored
sources are exactly where the last definition was.

ARMED CONTROL.  Before scanning the tree it plants one definition of each kind in a temporary
directory and requires that it finds both.  A check that cannot see is reported as a failure, never
as a pass.

What it cannot see: a -D_UNIX on a command line that compile_commands.json does not record (a
Windows build, a hand-run compiler), and a definition built by token pasting or spelled through
another macro.  A file with an extension not listed below is not read.
"""

import json, os, re, shlex, sys, tempfile

SOURCE = re.compile(r"\.(h|hh|hpp|hxx|inl|c|cc|cpp|cxx)$", re.I)
DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+_UNIX\b", re.M)


def defines_in_tree(root):
    hits = []
    for directory, subdirs, files in os.walk(root):
        subdirs[:] = [d for d in subdirs if d != ".git"]
        for name in files:
            if not SOURCE.search(name):
                continue
            path = os.path.join(directory, name)
            try:
                text = open(path, encoding="latin-1").read()
            except OSError:
                continue
            for m in DEFINE.finditer(text):
                hits.append("%s:%d: %s" % (path, text.count("\n", 0, m.start()) + 1,
                                           text[m.start():text.find("\n", m.start())].strip()))
    return hits


def defines_on_command_lines(compile_commands):
    hits = []
    for entry in json.load(open(compile_commands)):
        args = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
        for i, a in enumerate(args):
            flag = a[2:] if a[:2] in ("-D", "/D") else None
            if flag == "" and i + 1 < len(args):      # "-D _UNIX", as two arguments
                flag = args[i + 1]
            if flag is not None and re.match(r"_UNIX(=|$)", flag):
                hits.append("%s: %s" % (entry["file"], a if a[2:] else a + " " + flag))
    return hits


def control():
    """Plant one of each and insist on finding both."""
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "planted.h"), "w") as f:
            f.write("/* control */\n  #  define _UNIX 1\n")
        cc = os.path.join(tmp, "compile_commands.json")
        json.dump([{"directory": tmp, "file": "a.cpp", "command": "c++ -D_UNIX -c a.cpp"},
                   {"directory": tmp, "file": "b.cpp", "arguments": ["c++", "-D", "_UNIX", "-c", "b.cpp"]}],
                  open(cc, "w"))
        return len(defines_in_tree(tmp)) == 1 and len(defines_on_command_lines(cc)) == 2


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.split("\n\n")[1])
    compile_commands, root = sys.argv[1], sys.argv[2]
    if not control():
        print("FAIL: the armed control was not detected - this check cannot see, so it proves nothing")
        return 1
    hits = defines_in_tree(root) + defines_on_command_lines(compile_commands)
    if hits:
        print("FAIL: _UNIX is defined - see PORTING.md, \"Never define _UNIX\":")
        for h in hits:
            print("  " + h)
        return 1
    print("OK: control detected; no definition of _UNIX in %s or its compile commands" % root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
