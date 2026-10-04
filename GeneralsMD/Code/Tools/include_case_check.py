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
"""Fails if any #include in the POSIX build names a file in the wrong case.

Windows and a default macOS volume fold case, so `#include "vector.h"` finds `Vector.H` there and
nothing complains. Linux does not fold case, and the same line is a missing header. The first Linux
build of this tree found 300 such includes reaching 93 files. This check finds them from macOS,
without Linux.

How: for every translation unit in compile_commands.json, follow its includes the way the compiler
searches (the includer's own directory first for "", then each -I/-iquote/-isystem directory in
order), recursing into headers inside the source tree. An include that resolves only when case is
ignored is a finding. Existence is checked against directory listings, never os.path.exists, because
the host filesystem may fold case and answer yes.

What it does NOT see:
  - Files outside compile_commands.json. Windows-only code is not in a POSIX build, so it is not
    checked until it is ported, and it is checked then.
  - Includes inside #if branches that are not taken. This reads text, not the preprocessor, so it
    checks MORE branches than a build does, not fewer, and could flag an include no build uses.
    That errs toward a false alarm, never a silent miss.
  - Macro-expanded includes (#include SOME_MACRO). There are none in the tree today.
  - Case in anything but #include: CMake source lists, data file names, fopen paths.

What it checks first: that the working tree's names are git's names. On a case-folding host a
case-only rename can exist in the index and not on disk, or the reverse. `git reset` of a staged
rename leaves exactly that, and a later `git add -A` does not notice. The audit reads the disk, so
if the disk disagrees with git it would certify a tree nobody committed. Any tracked file whose
on-disk name differs from its index name, even by case alone, fails the run before the audit
starts. Outside a git checkout this precondition is skipped and the output says so.

  include_case_check.py <compile_commands.json> <source-root>
"""
import json
import os
import re
import shlex
import subprocess
import sys

INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.M)
_listing = {}


def entries(directory):
    if directory not in _listing:
        try:
            _listing[directory] = set(os.listdir(directory))
        except OSError:
            _listing[directory] = set()
    return _listing[directory]


def walk(path, fold):
    """Resolve path component by component, exactly or ignoring case. Returns the on-disk path."""
    current = '/'
    for part in os.path.normpath(path).split('/'):
        if not part:
            continue
        if part == '..':
            current = os.path.dirname(current.rstrip('/')) or '/'
            continue
        names = entries(current)
        if part in names:
            current = os.path.join(current, part)
        elif fold:
            match = [n for n in names if n.lower() == part.lower()]
            if not match:
                return None
            current = os.path.join(current, sorted(match)[0])
        else:
            return None
    return current


def index_matches_disk(root):
    """(checked, [paths whose on-disk name differs from git's]).  checked is False outside git."""
    try:
        listing = subprocess.run(['git', '-C', root, 'ls-files', '-z'], capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return False, []
    differ = []
    for path in listing.stdout.decode('utf-8', 'replace').split('\0'):
        if path and not walk(os.path.join(root, path), False) and walk(os.path.join(root, path), True):
            differ.append(path)
    return True, differ


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    commands = json.load(open(sys.argv[1]))
    root = os.path.realpath(sys.argv[2])

    checked, differ = index_matches_disk(root)
    if differ:
        print(f'include_case_check: FAIL - {len(differ)} tracked files are named differently on disk '
              'than in git\'s index, so this checkout is not the tree git holds and auditing it would '
              'prove nothing.  Fix the checkout (git mv, or re-checkout on a case-sensitive volume):')
        for path in differ[:20]:
            print(f'  {path}')
        return 1
    if not checked:
        print('include_case_check: note - not a git checkout, so the disk is trusted as it is')
    findings = {}
    seen = set()
    resolved = 0

    def scan(path, dirs):
        nonlocal resolved
        key = (path, dirs)
        if key in seen:
            return
        seen.add(key)
        try:
            text = open(path, errors='replace').read()
        except OSError:
            return
        for m in INCLUDE.finditer(text):
            kind, name = m.group(1), m.group(2).replace('\\', '/')
            candidates = ([os.path.dirname(path)] if kind == '"' else []) + list(dirs)
            hit = next((walk(os.path.join(d, name), False) for d in candidates
                        if walk(os.path.join(d, name), False)), None)
            if hit:
                resolved += 1
                if hit.startswith(root):
                    scan(hit, dirs)
                continue
            for d in candidates:
                real = walk(os.path.join(d, name), True)
                if real:
                    if real.startswith(root):
                        line = text[:m.start(2)].count('\n') + 1
                        findings[(os.path.relpath(path, root), line)] = (name, os.path.relpath(real, root))
                        scan(real, dirs)
                    break

    for entry in commands:
        args = shlex.split(entry['command']) if 'command' in entry else entry['arguments']
        dirs = []
        for i, arg in enumerate(args):
            if arg in ('-I', '-iquote', '-isystem'):
                dirs.append(args[i + 1])
            elif arg.startswith('-I'):
                dirs.append(arg[2:])
        base = entry.get('directory', '.')
        dirs = tuple(os.path.realpath(os.path.join(base, d)) for d in dirs)
        scan(os.path.realpath(os.path.join(base, entry['file'])), dirs)

    # A check that read nothing must not report clean.
    if not commands or resolved == 0:
        print(f'include_case_check: FAIL - read {len(commands)} translation units and resolved '
              f'{resolved} includes; this check did not run')
        return 1
    if findings:
        print(f'include_case_check: FAIL - {len(findings)} includes name a file in the wrong case '
              '(Windows and macOS fold case; Linux does not):')
        for (where, line), (name, real) in sorted(findings.items()):
            print(f'  {where}:{line}: "{name}" is on disk as {real}')
        return 1
    print(f'include_case_check: ok - {len(commands)} translation units, {resolved} includes, '
          'every one spelled as it is on disk')
    return 0


if __name__ == '__main__':
    sys.exit(main())
