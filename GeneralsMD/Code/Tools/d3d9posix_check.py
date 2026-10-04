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
"""Check Platform/D3D9Posix.h (and D3D9PosixMath.h) against MinGW-w64's d3d9.h (decision 7, phase A1).

The POSIX header is written from Direct3D 9's published values, not copied from any header.  This
proves it agrees: it reads the POSIX header, writes a translation unit that includes MinGW-w64's
d3d9.h and then the POSIX header inside a namespace, and static_asserts

  - every enumerator: the same value as MinGW's;
  - every object-like #define with a value, and the function-like ones on sample arguments: the same
    value as MinGW's, captured before the POSIX header redefines them;
  - every structure: the same size, and every field at the same offset;
  - every interface identifier: the same sixteen bytes as MinGW's DEFINE_GUID line.  MinGW declares
    these as plain consts, which a static_assert cannot read, so they are compared as numbers read
    out of the two headers.

It then compiles that with x86_64-w64-mingw32-g++, once as it is and once with a false assertion
added, which must fail - so a pass means the assertions ran.  MinGW-w64's headers are only compiled
against here, never copied: they are Wine's work, under the LGPL.

  d3d9posix_check.py [--keep FILE]      exit 0 when every check holds
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CODE = os.path.dirname(HERE)
PLATFORM = os.path.join(CODE, 'Libraries', 'Include', 'Platform')
# D3D9Posix.h, and the vector and matrix it includes from a header of their own for D3DX's sake.
HEADERS = [os.path.join(PLATFORM, 'D3D9Posix.h'), os.path.join(PLATFORM, 'D3D9PosixMath.h')]
COMPILER = 'x86_64-w64-mingw32-g++'

# Function-like macros, and the arguments to compare them on.
FUNCTION_MACROS = {
    'D3DCOLOR_ARGB': ['(0x12, 0x34, 0x56, 0x78)', '(0x1ff, 0x100, 0x2ff, 0x3ff)'],
    'D3DCOLOR_RGBA': ['(0x12, 0x34, 0x56, 0x78)'],
    'D3DCOLOR_XRGB': ['(0x12, 0x34, 0x56)'],
    'D3DTS_WORLDMATRIX': ['(0)', '(3)'],
    'D3DFVF_TEXCOORDSIZE1': ['(0)', '(1)', '(7)'],
    'D3DFVF_TEXCOORDSIZE2': ['(0)', '(1)', '(7)'],
    'D3DFVF_TEXCOORDSIZE3': ['(0)', '(1)', '(7)'],
    'D3DFVF_TEXCOORDSIZE4': ['(0)', '(1)', '(7)'],
    'D3DPS_VERSION': ['(1, 1)', '(2, 0)', '(3, 0)'],
    'D3DVS_VERSION': ['(1, 1)', '(2, 0)'],
}
# Function-like macros that expand to a structure's initializer: the structure, and the fields to
# compare, on an empty argument list.
STRUCT_MACROS = {
    'D3DDECL_END': ('D3DVERTEXELEMENT9', ['Stream', 'Offset', 'Type', 'Method', 'Usage', 'UsageIndex']),
}
# The POSIX header's own names, which D3D9 has no counterpart for.
OWN = re.compile(r'^(D3D9POSIX_|PLATFORM_D3D9POSIX)')


def members(block):
    """An enumeration's members, split at the commas outside parentheses."""
    parts, depth, start = [], 0, 0
    for at, char in enumerate(block):
        depth += {'(': 1, ')': -1}.get(char, 0)
        if char == ',' and depth == 0:
            parts.append(block[start:at])
            start = at + 1
    parts.append(block[start:])
    return [part.strip() for part in parts if part.strip()]


def parse(text):
    body = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    body = re.sub(r'//[^\n]*', '', body)
    macros = [m.group(1) for m in re.finditer(r'^\s*#define\s+(\w+)[ \t]+\S', body, re.M)
              if not OWN.match(m.group(1))]
    # Nothing is skipped without a word: a function-like macro has to have sample arguments listed
    # in FUNCTION_MACROS, and an enumerator has to have its value written out.
    unchecked = [f'function-like macro {m.group(1)} has no samples in FUNCTION_MACROS'
                 for m in re.finditer(r'^\s*#define\s+(\w+)\(', body, re.M)
                 if not OWN.match(m.group(1)) and m.group(1) not in FUNCTION_MACROS
                 and m.group(1) not in STRUCT_MACROS]
    # *_FORCE_DWORD only makes an enumeration four bytes wide, and the renderer never names one.
    # MinGW-w64 (Wine) gives some 0xffffffff where Microsoft's SDK gives 0x7fffffff, so their values
    # are not compared; the four-byte width is asserted in the header.
    enumerators = []
    for block in re.finditer(r'\benum\s+(\w+)\s*\{(.*?)\}', body, re.S):
        for member in members(block.group(2)):
            name = re.match(r'\w+', member).group(0)
            if '=' not in member:
                unchecked.append(f'enumerator {name} in {block.group(1)} has no value written out')
            elif not name.endswith('_FORCE_DWORD'):
                enumerators.append(name)
    structs = {}
    for head in re.finditer(r'\bstruct\s+(\w+)\s*\{', body):
        name = head.group(1)
        depth, at = 1, head.end()
        while depth:
            depth += {'{': 1, '}': -1}.get(body[at], 0)
            at += 1
        if name.startswith('D3D9Posix'):
            continue
        inner = re.sub(r'\bunion\s*\{|\bstruct\s*\{|\}\s*;|\}', ' ', body[head.end():at - 1])
        fields = []
        for decl in inner.split(';'):
            decl = decl.strip()
            if not decl:
                continue
            declared = re.match(r'^(?:const\s+)?(?:unsigned\s+)?\w+(?:\s*\*)?\s+(.*)$', decl, re.S)
            if not declared:
                continue
            for part in declared.group(1).split(','):
                field = re.match(r'\s*\*?\s*(\w+)', part)
                if field:
                    fields.append(field.group(1))
        structs[name] = fields
    return macros, enumerators, structs, unchecked


def unit(macros, enumerators, structs, control):
    lines = ['// Written by Tools/d3d9posix_check.py; compiled by MinGW-w64, never built.',
             '#include <stddef.h>', '#include <stdint.h>', '#include <d3d9.h>', '',
             'namespace mingw {']
    for name in macros:
        lines.append(f'constexpr long long captured_{name} = (long long)({name});')
    samples = []
    for name, args in FUNCTION_MACROS.items():
        for i, arg in enumerate(args):
            lines.append(f'constexpr long long captured_{name}_{i} = (long long)({name}{arg});')
            samples.append((name, i, arg))
    for name, (struct, _) in STRUCT_MACROS.items():
        lines.append(f'constexpr ::{struct} captured_{name} = {name}();')
    lines.append('}')
    for name in macros + list(FUNCTION_MACROS) + list(STRUCT_MACROS):
        lines.append(f'#undef {name}')
    lines += ['', '#define D3D9POSIX_CHECKER', '#include "Platform/RenderTypes.h"',
              'namespace posix {', '#include "Platform/D3D9Posix.h"', '}', '']
    for name in enumerators:
        lines.append(f'static_assert((long long)posix::{name} == (long long)::{name}, "{name}");')
    for name in macros:
        lines.append(f'static_assert((long long)({name}) == mingw::captured_{name}, "{name}");')
    for name, i, arg in samples:
        lines.append(f'static_assert((long long)({name}{arg}) == mingw::captured_{name}_{i}, "{name}{arg}");')
    for name, (struct, fields) in STRUCT_MACROS.items():
        lines.append(f'constexpr ::{struct} ours_{name} = {name}();')
        for field in fields:
            lines.append(f'static_assert(ours_{name}.{field} == mingw::captured_{name}.{field}, "{name}().{field}");')
    for name, fields in structs.items():
        lines.append(f'static_assert(sizeof(posix::{name}) == sizeof(::{name}), "sizeof({name})");')
        for field in fields:
            lines.append(f'static_assert(offsetof(posix::{name}, {field}) == offsetof(::{name}, {field}), '
                         f'"{name}::{field}");')
    if control:
        lines.append('static_assert((long long)posix::D3DRS_ZENABLE == (long long)::D3DRS_FILLMODE, "control");')
    return '\n'.join(lines) + '\n'


def guids(text):
    """name -> the GUID's eleven numbers, from D3D9Posix.h's definitions or MinGW's DEFINE_GUIDs."""
    found = {}
    for match in re.finditer(r'constexpr\s+D3D9PosixGuid\s+(\w+)\s*=\s*\{(.*?)\};', text, re.S):
        found[match.group(1)] = [int(n, 0) for n in re.findall(r'0x[0-9a-fA-F]+|\d+', match.group(2))]
    for match in re.finditer(r'DEFINE_GUID\(\s*(\w+)\s*,(.*?)\);', text, re.S):
        found[match.group(1)] = [int(n, 0) for n in re.findall(r'0x[0-9a-fA-F]+|\d+', match.group(2))]
    return found


def mingw_d3d9():
    """The path of the d3d9.h MinGW-w64 compiles against."""
    result = subprocess.run([COMPILER, '-x', 'c++', '-M', '-'], input='#include <d3d9.h>\n',
                            capture_output=True, text=True)
    for word in result.stdout.replace('\\\n', ' ').split():
        if word.endswith('/d3d9.h'):
            return word
    return None


def compile_unit(source):
    with tempfile.NamedTemporaryFile('w', suffix='.cpp', delete=False) as handle:
        handle.write(source)
        path = handle.name
    try:
        result = subprocess.run([COMPILER, '-std=c++17', '-fsyntax-only', '-w', '-I',
                                 os.path.join(CODE, 'Libraries', 'Include'), path],
                                capture_output=True, text=True)
    finally:
        os.unlink(path)
    return result.returncode, result.stderr


def main():
    text = ''.join(open(h).read() for h in HEADERS)
    macros, enumerators, structs, unchecked = parse(text)
    ours = guids(text)
    theirs_path = mingw_d3d9()
    theirs = guids(open(theirs_path).read()) if theirs_path else {}
    for name, value in sorted(ours.items()):
        if len(value) != 11:
            unchecked.append(f'{name} does not read as eleven numbers')
        elif name not in theirs:
            unchecked.append(f'{name} is not in MinGW-w64\'s d3d9.h ({theirs_path})')
        elif theirs[name] != value:
            unchecked.append(f'{name} differs from MinGW-w64\'s: {value} against {theirs[name]}')
    if unchecked:
        print(f'd3d9posix_check: FAIL - {len(unchecked)} names that disagree or cannot be compared:')
        for line in unchecked:
            print(f'  {line}')
        return 1
    source = unit(macros, enumerators, structs, control=False)
    if '--keep' in sys.argv:
        open(sys.argv[sys.argv.index('--keep') + 1], 'w').write(source)
    fields = sum(len(f) for f in structs.values())
    code, errors = compile_unit(source)
    if code != 0:
        failed = sorted(set(re.findall(r'static assertion failed: "?([^"\n]+)', errors)))
        other = [l for l in errors.splitlines() if ' error: ' in l and 'static assertion failed' not in l]
        print(f'd3d9posix_check: FAIL - {len(failed)} disagree with MinGW-w64, {len(other)} other errors:')
        for name in failed[:60]:
            print(f'  {name}')
        for line in other[:20]:
            print(f'  {line}')
        return 1
    control, _ = compile_unit(unit(macros, enumerators, structs, control=True))
    if control == 0:
        print('d3d9posix_check: FAIL - the false control assertion did not fire, so nothing was checked')
        return 1
    print(f'd3d9posix_check: {len(enumerators)} enumerators, {len(macros)} macros, '
          f'{sum(len(a) for a in FUNCTION_MACROS.values())} macro samples, {len(STRUCT_MACROS)} initializer '
          f'macro, {len(structs)} structures '
          f'({fields} field offsets) and {len(ours)} interface identifiers agree with MinGW-w64\'s '
          f'd3d9.h; the control fails as it should')
    return 0


if __name__ == '__main__':
    sys.exit(main())
