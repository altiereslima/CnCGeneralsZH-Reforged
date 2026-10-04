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
"""Compile each of a CMake target's sources on its own and report the first real error.

    python3 Tools/syntax_sweep.py build-mac wwlib
    python3 Tools/syntax_sweep.py build-mac gameengine --files      # list names per cause
    python3 Tools/syntax_sweep.py build-mac wwmath --only lookuptable.cpp

Why this exists.  A library that does not link tells you almost nothing about how far it is from
linking: `make` stops, or stops eighty times on the same header, and a grep over the output answers
a different question than the one you asked.  Three people measured wwmath three different ways in
one afternoon and got three different tables.  Compiling each source separately with -fsyntax-only
and -ferror-limit=1 answers exactly "how many of these would compile, and what is the first thing
stopping each of the rest", which is the number worth tracking and the only one that has held up.

Two details that matter, both of which the obvious implementation gets wrong:

  * Attribute the failure to the line that says `error:`, NOT to a filename grepped out of the
    output.  clang prints an `In file included from ...` chain before the diagnostic, so grepping
    for filenames attributes every failure to whichever header happens to appear first in the
    chain.  That is how the contradictory wwmath tables happened.  The regex below anchors on
    `<path>:<line>:<col>: error:` for that reason - do not "simplify" it to a filename match.

  * -ferror-limit=1 on purpose.  We want the FIRST cause per file, because fixing it usually
    reveals a different second cause and counting all of them makes the same header look like
    twenty problems.  Re-run after each fix; the tail changes shape as you go.

Where the commands come from.  Each source is compiled with the exact command the build would run
for it, taken from the compilation database: `ninja -t compdb` in a Ninja tree (build-posix.sh's),
or compile_commands.json, which the macOS configure writes for either generator.  Only three things
are changed: -fsyntax-only and -ferror-limit=1 are added, and the output and depfile arguments
(-o, -MD, -MMD, -MT, -MF, -MQ) are removed, so a sweep never writes into the build tree.

WHAT THIS USED TO MISS, until 2026-09-25.  The first version read CXX_INCLUDES and CXX_FLAGS out of
a Makefiles tree's flags.make and put them in front of clang++ for every source.  That was blind in
three ways, and every number measured with it before that date carries them:

  * No CXX_DEFINES.  Target-specific -D flags were silently absent, so a file whose outcome depends
    on one was reported wrongly.  compression's CompressionManager.cpp was reported failing on a
    `Byte` typedef clash that the target's own -DZ_PREFIX exists to prevent.
  * C compiled as C++.  Every .c file went through clang++ with the C++ flags.  compression's
    fourteen zlib sources were reported failing on their K&R definitions.  Together with the line
    above, compression measured 13 of 28 on a day it built 28 of 28 and its self-check passed.
  * Makefiles only.  A Ninja tree has no flags.make, so the tool refused build.sh's default tree.

What it still does not see.  -fsyntax-only is not a build: no code generation, no link, so a green
sweep says nothing about undefined symbols (wwmath_selfcheck was front-end green and never linked).
It sees the one configuration the tree was configured for - a Release tree cannot tell you about a
_DEBUG-only branch.  And it reports the first error per file, never the total.
"""

import argparse, collections, json, os, re, shlex, subprocess, sys

# Absolute or relative: the build runs from its own directory and prints paths as it was given them.
ERROR_LINE = re.compile(r"^([^:\s][^:]*):(\d+):\d+: (?:fatal )?error: (.*)$")
SOURCE = re.compile(r"\.(?:cpp|CPP|c|cc|cxx)$")

# Arguments that write files: dropped along with the value that follows them.
DROP_WITH_VALUE = {"-o", "-MT", "-MF", "-MQ", "-MJ"}
DROP_ALONE = {"-MD", "-MMD"}


def compilation_database(build):
    """Every compile command in the tree, from Ninja itself or from compile_commands.json."""
    if os.path.exists(os.path.join(build, "build.ninja")):
        # Asked of ninja rather than read from a file, so it is always what ninja would run now.
        r = subprocess.run(["ninja", "-C", build, "-t", "compdb"], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("ninja -t compdb failed in %s:\n%s" % (build, r.stderr))
        return json.loads(r.stdout)
    p = os.path.join(build, "compile_commands.json")
    if os.path.exists(p):
        return json.load(open(p))
    sys.exit("%s has neither build.ninja nor compile_commands.json.  Reconfigure it; the macOS "
             "configure turns on CMAKE_EXPORT_COMPILE_COMMANDS, or pass "
             "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON." % build)


def arguments(entry):
    return entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])


def output_of(entry, args):
    if "output" in entry:
        return entry["output"]
    return args[args.index("-o") + 1] if "-o" in args else ""


def target_commands(build, target):
    """(source, argv, directory) for each source CMake compiles into this target."""
    prefix = "CMakeFiles/%s.dir/" % target
    found, targets = {}, set()
    for e in compilation_database(build):
        if not SOURCE.search(e["file"]):
            continue
        args = arguments(e)
        out = output_of(e, args)
        m = re.match(r"CMakeFiles/([^/]+)\.dir/", out)
        if m:
            targets.add(m.group(1))
        if out.startswith(prefix):
            src = e["file"] if os.path.isabs(e["file"]) else os.path.join(e["directory"], e["file"])
            found[src] = (args, e["directory"])
    if not found:
        sys.exit("no sources for target %s in %s.  Targets with sources: %s"
                 % (target, build, " ".join(sorted(targets))))
    return [(src,) + found[src] for src in sorted(found)]


def syntax_only(args):
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
        elif a in DROP_WITH_VALUE:
            skip = True
        elif a in DROP_ALONE:
            pass
        else:
            out.append(a)
    return out[:1] + ["-fsyntax-only", "-ferror-limit=1"] + out[1:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("target")
    ap.add_argument("--only", help="sweep just the sources whose name contains this")
    ap.add_argument("--files", action="store_true", help="list the failing file names per cause")
    args = ap.parse_args()

    cmds = target_commands(args.build, args.target)
    if args.only:
        cmds = [c for c in cmds if args.only in os.path.basename(c[0])]

    ok, causes = [], collections.defaultdict(list)
    for src, argv, directory in cmds:
        r = subprocess.run(syntax_only(argv), cwd=directory, capture_output=True, text=True)
        if r.returncode == 0:
            ok.append(os.path.basename(src))
            continue
        for line in r.stderr.split("\n"):
            m = ERROR_LINE.match(line)
            if m:
                where = "%s:%s" % (os.path.basename(m.group(1)), m.group(2))
                causes[(where, m.group(3))].append(os.path.basename(src))
                break
        else:
            first = (r.stderr.strip().split("\n") or ["(no diagnostic)"])[0]
            causes[("?", first[:90])].append(os.path.basename(src))

    print("%s: %d of %d compile, %d fail" % (args.target, len(ok), len(cmds), len(cmds) - len(ok)))
    if causes:
        print()
    for (where, msg), files in sorted(causes.items(), key=lambda kv: -len(kv[1])):
        print("%3d  %-28s %s" % (len(files), where, msg[:70]))
        if args.files:
            print("     " + " ".join(sorted(files)))
    return 1 if causes else 0


if __name__ == "__main__":
    sys.exit(main())
