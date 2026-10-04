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
"""Fails if anything in the POSIX build still speaks wchar_t, or if any source literal holds raw
non-ASCII bytes.

WideChar is char16_t (B1).  wchar_t is four bytes on every POSIX platform and two on Windows, so any
wchar_t, L"..." or L'x' left in code that a POSIX build compiles is a place where the two platforms
hold text at different widths - and text reaches the replay CRC through Xfer::xferUnicodeString.
Most of those are compile errors once WideChar is char16_t.  Not all: an L'x' converts to char16_t
silently, and a wchar_t helper that never meets a WideChar compiles and quietly works at four bytes.
This check is what finds the silent ones, and it keeps finding them as more of the engine starts
compiling on POSIX.

Two rules:

  1. POSIX VIEW.  Every file a POSIX translation unit reaches (from compile_commands.json, following
     #includes, tracked files only - vendored libraries are not ours to change), with every branch
     that only Windows compiles removed (unifdef -U_WIN32 -U_MSC_VER ...) and comments stripped, must
     contain no L"..." or L'x' literal, no wchar_t or std::wstring, and no call to a C wide-character
     function (wcs*, wmem*, *wprintf, *wscanf, tow*, isw*, fgetw*/fputw*, mbstowcs and friends).
     Exceptions live in widechar_check_allow.txt, one per line, each with a reason.

  2. EVERY LITERAL, BOTH PLATFORMS.  No string or character literal - narrow or wide, in any branch -
     in the engine, WWVegas or Main may contain a byte above 0x7F.  MSVC reads source in the ANSI
     codepage unless /utf-8 is set, and clang reads UTF-8, so a raw non-ASCII byte in a literal is
     different code units on the two compilers - and in a UnicodeString, different CRC bytes.  Write
     it as a \\u escape.

What it cannot see: a wide character arriving at runtime from a Win32 API on Windows (that is the
Windows build's business and it is two bytes there); text built by macro concatenation of an L
prefix (there is none); and anything not tracked by git.  It reads text, not the preprocessor's
output, so a macro that hides a wchar_t would be missed - none does today.

  widechar_check.py <compile_commands.json> <source-root>
"""
import json
import os
import re
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ALLOW_FILE = os.path.join(HERE, 'widechar_check_allow.txt')

POSIX_VIEW = ['-U_WIN32', '-UWIN32', '-U_WIN64', '-U_MSC_VER', '-U_M_X64', '-U_M_AMD64', '-U_M_IX86']
SOURCE = re.compile(r'\.(h|hpp|hh|inl|c|cc|cpp|cxx)$', re.I)
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.M)
LITERAL_TREES = ('GameEngine/', 'GameEngineDevice/', 'Libraries/Source/WWVegas/', 'Libraries/Include/',
                 'Main/', 'Tests/')

PATTERNS = [
    ('wide string literal', re.compile(r'(?<![A-Za-z0-9_])L"')),
    ('wide character literal', re.compile(r"(?<![A-Za-z0-9_])L'")),
    ('wchar_t', re.compile(r'\bwchar_t\b')),
    ('std::wstring', re.compile(r'\bwstring\b')),
    ('C wide-character function', re.compile(
        r'\b(wcs[a-z_]*|_wcs[a-z_]*|wmem[a-z]+|[a-z_]*wprintf|_vsnwprintf|_snwprintf|[a-z]*wscanf'
        r'|tow(?:upper|lower)|isw[a-z]+|fgetw[a-z]*|fputw[a-z]*|getwc|putwc|btowc|wctob'
        r'|mbstowcs|wcstombs|mbrtowc|wcrtomb|mbtowc|wctomb)\s*\(')),
]


RAW_OPEN = re.compile(r'(?:u8|u|U|L)?R"([^()\\ \n]{0,16})\(')


def strip_comments(text, raws=None):
    """// and /* */ out, string and character literals kept, line count kept.

    A raw string literal - R"MSL(...)MSL", which the tree uses for shader source - is replaced by R""
    plus its newlines, because its contents are not C++: unifdef would read a #include inside one as a
    directive and its quotes as unterminated.  Its text goes into `raws`, as (line, text), so the
    non-ASCII rule still reads it."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        raw = RAW_OPEN.match(text, i) if c in 'u8ULR' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')) else None
        if raw:
            close = ')' + raw.group(1) + '"'
            end = text.find(close, raw.end())
            end = n if end < 0 else end + len(close)
            body = text[raw.end():end - len(close)]
            if raws is not None:
                raws.append((text.count('\n', 0, i) + 1, body))
            out.append('R""' + '\n' * body.count('\n'))
            i = end
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                i += 1
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            end = text.find('*/', i + 2)
            end = n if end < 0 else end + 2
            out.append('\n' * text.count('\n', i, end))
            i = end
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def literals(text):
    """(line, literal) for every string and character literal, comments already stripped."""
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1
        if c in '"\'' and not (c == "'" and i > 0 and (text[i - 1].isalnum() and text[i - 1] not in 'LuU8')):
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            yield line, text[i:j + 1]
            i = j + 1
            continue
        i += 1


def blank_literals(text):
    """Each literal's contents as spaces, its prefix and quotes kept: the token rules look at code, and
    'L' - the character - is not an L prefix.  (It was, in the first version of this check: Keyboard.cpp's
    u'L' and GameText.cpp's ('L'<<24) both came back as wide literals.)"""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in '"\'' and not (c == "'" and i > 0 and (text[i - 1].isalnum() and text[i - 1] not in 'LuU8')):
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(c + ' ' * (min(j, n) - i - 1) + (text[j] if j < n else ''))
            i = j + 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def tracked(root):
    listing = subprocess.run(['git', '-C', root, 'ls-files', '-z'], capture_output=True, check=True)
    return {os.path.realpath(os.path.join(root, p)) for p in listing.stdout.decode().split('\0') if p}


def load_allow():
    allow = []
    if os.path.exists(ALLOW_FILE):
        for raw in open(ALLOW_FILE):
            raw = raw.rstrip('\n')
            if not raw.strip() or raw.lstrip().startswith('#'):
                continue
            parts = raw.split('\t')
            if len(parts) < 3 or not parts[2].strip():
                print(f'widechar_check: FAIL - allowlist line has no reason: {raw}')
                sys.exit(1)
            allow.append((parts[0], parts[1]))
    return allow


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    commands = json.load(open(sys.argv[1]))
    root = os.path.realpath(sys.argv[2])
    ours = tracked(root)
    allow = load_allow()

    # Every tracked file a POSIX TU reaches.
    reached, stack = set(), []
    for entry in commands:
        args = shlex.split(entry['command']) if 'command' in entry else entry['arguments']
        dirs = []
        for k, arg in enumerate(args):
            if arg in ('-I', '-iquote', '-isystem'):
                dirs.append(args[k + 1])
            elif arg.startswith('-I'):
                dirs.append(arg[2:])
        base = entry.get('directory', '.')
        dirs = tuple(os.path.realpath(os.path.join(base, d)) for d in dirs)
        stack.append((os.path.realpath(os.path.join(base, entry['file'])), dirs))
    seen = set()
    while stack:
        path, dirs = stack.pop()
        if (path, dirs) in seen:
            continue
        seen.add((path, dirs))
        if path not in ours:
            continue
        reached.add(path)
        try:
            text = open(path, 'rb').read().decode('latin-1')
        except OSError:
            continue
        for m in INCLUDE.finditer(text):
            name = m.group(2).replace('\\', '/')
            candidates = ([os.path.dirname(path)] if m.group(1) == '"' else []) + list(dirs)
            for d in candidates:
                target = os.path.realpath(os.path.join(d, name))
                if os.path.isfile(target):
                    stack.append((target, dirs))
                    break

    if not reached:
        print('widechar_check: FAIL - no tracked file is reached from compile_commands.json; nothing was checked')
        return 1

    findings = []
    for path in sorted(reached):
        rel = os.path.relpath(path, root)
        if not SOURCE.search(rel):
            continue
        raw = open(path, 'rb').read().decode('latin-1').replace('\r\n', '\n')
        run = subprocess.run(['unifdef', *POSIX_VIEW], input=strip_comments(raw).encode('latin-1'),
                             capture_output=True)
        if run.returncode not in (0, 1):
            print(f'widechar_check: FAIL - unifdef could not read {rel}: {run.stderr.decode().strip()}')
            return 1
        view = blank_literals(run.stdout.decode('latin-1'))
        # unifdef deletes lines, so find each hit in the view and report the view's line.
        for lineno, line in enumerate(view.split('\n'), 1):
            for label, pattern in PATTERNS:
                for m in pattern.finditer(line):
                    token = m.group(0).rstrip('( \t')
                    if any(rel == a_path and (a_tok == '*' or a_tok == token) for a_path, a_tok in allow):
                        continue
                    findings.append((rel, lineno, label, line.strip()[:110]))

    # Rule 2: raw non-ASCII bytes inside any literal, in every tracked engine source, both platforms.
    nonascii = []
    for path in sorted(ours):
        rel = os.path.relpath(path, root)
        if not SOURCE.search(rel) or not rel.startswith(LITERAL_TREES) or rel.startswith('..'):
            continue
        raws = []
        try:
            text = strip_comments(open(path, 'rb').read().decode('latin-1'), raws)
        except OSError:
            continue
        for lineno, body in raws:
            if any(ord(ch) > 0x7F for ch in body):
                if not any(rel == a_path and a_tok == 'non-ASCII literal' for a_path, a_tok in allow):
                    nonascii.append((rel, lineno, 'R"(...)" ' + body[:50]))
        for lineno, lit in literals(text):
            if any(ord(ch) > 0x7F for ch in lit):
                if not any(rel == a_path and a_tok == 'non-ASCII literal' for a_path, a_tok in allow):
                    nonascii.append((rel, lineno, lit[:60]))

    checked = sum(1 for p in reached if SOURCE.search(p))
    if findings or nonascii:
        if findings:
            print(f'widechar_check: FAIL - {len(findings)} uses of wchar_t text in the POSIX view '
                  f'of {checked} files (WideChar is char16_t; see this script\'s header):')
            for rel, lineno, label, text in findings:
                print(f'  {rel}: view line {lineno}: {label}: {text}')
        if nonascii:
            print(f'widechar_check: FAIL - {len(nonascii)} literals hold raw non-ASCII bytes, which MSVC '
                  'and clang read as different characters; write them as \\u escapes:')
            for rel, lineno, lit in nonascii:
                print(f'  {rel}:{lineno}: {lit!r}')
        return 1
    print(f'widechar_check: ok - {checked} files in the POSIX view speak no wchar_t, and no literal '
          'in the engine holds a raw non-ASCII byte')
    return 0


if __name__ == '__main__':
    sys.exit(main())
