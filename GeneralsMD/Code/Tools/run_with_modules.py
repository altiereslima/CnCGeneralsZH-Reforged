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
"""Run a Python tool's check only where the modules it imports are installed.

    python run_with_modules.py numpy,PIL Tools/order_cameos.py selfcheck

A check whose script needs third-party modules (numpy, Pillow) would fail with an ImportError on a
machine that lacks them, which says nothing about the code under test.  This looks for each module
first.  If any is missing it says which, on stdout so ctest's log keeps it, and exits 77, which the
test registers as its skip code: the run shows the test as skipped, by name, with the reason.  If all
are there it runs the script as __main__ with the remaining arguments, exactly as `python script ...`
would.
"""

import importlib.util
import runpy
import sys

SKIP = 77


def main():
    if len(sys.argv) < 3:
        print("usage: run_with_modules.py <module,module,...> <script> [arguments...]", file=sys.stderr)
        return 2
    modules = [name for name in sys.argv[1].split(",") if name]
    missing = [name for name in modules if importlib.util.find_spec(name) is None]
    script = sys.argv[2]
    if missing:
        print("skip: %s needs %s, which %s lacks" % (script, " and ".join(missing), sys.executable))
        return SKIP
    sys.argv = sys.argv[2:]
    runpy.run_path(script, run_name="__main__")
    return 0


if __name__ == "__main__":
    sys.exit(main())
