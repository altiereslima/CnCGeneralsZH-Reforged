#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

OLD_TOOLTIP = (
    'TOOLTIP:Language\n'
    '"The language of menus, orders, briefings and subtitles. Voices and videos stay as installed. Takes effect the next time the game starts."\n'
    'END\n'
)
NEW_TOOLTIP = (
    'TOOLTIP:Language\n'
    '"The language of menus, orders, briefings, subtitles and localized videos. Voices stay as installed. Takes effect the next time the game starts."\n'
    'END\n'
)

def replace_once(path, old, new):
    text=path.read_text(encoding="utf-8-sig")
    n=text.count(old)
    if n!=1:
        raise SystemExit(f"STAGE12: {path}: esperado 1 bloco, encontrado {n}")
    path.write_text(text.replace(old,new,1),encoding="utf-8",newline="")

def main():
    repo=Path(sys.argv[1] if len(sys.argv)>1 else ".").resolve()
    pkg=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(Path(__file__).parent))

    import validate_upstream_checkout
    validate_upstream_checkout.validate(repo)

    import apply_stage11
    old_argv=sys.argv[:]
    try:
        sys.argv=[str(Path(__file__)),str(repo)]
        apply_stage11.main()
    finally:
        sys.argv=old_argv

    patch=repo/"GeneralsMD/Code/Data/Patch.str"
    replace_once(patch, OLD_TOOLTIP, NEW_TOOLTIP)

    src=pkg/"payload/GeneralsMD/Code/Data/PortugueseBrazil"
    dst=repo/"GeneralsMD/Code/Data/PortugueseBrazil"
    shutil.copytree(src,dst,dirs_exist_ok=True)

    print("STAGE12 APPLY PASS")
    print("Upstream anchors validated before patching.")
    print("English/PT-BR language tooltip now matches localized-video behavior.")

if __name__=="__main__":
    main()
