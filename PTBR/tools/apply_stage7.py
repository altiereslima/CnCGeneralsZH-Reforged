#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    pkg = Path(__file__).resolve().parents[1]

    sys.path.insert(0, str(Path(__file__).parent))
    import apply_stage6

    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage6.main()
    finally:
        sys.argv = old_argv

    src = pkg / "payload" / "GeneralsMD" / "Code" / "Data" / "PortugueseBrazil"
    dst = repo / "GeneralsMD" / "Code" / "Data" / "PortugueseBrazil"
    shutil.copytree(src, dst, dirs_exist_ok=True)

    print("STAGE07 APPLY PASS")
    print("USA + China campaign review locale installed.")

if __name__ == "__main__":
    main()
