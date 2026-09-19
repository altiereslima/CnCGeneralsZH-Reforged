#!/usr/bin/env python3
from pathlib import Path
import sys

def labels(path):
    lines=path.read_text(encoding="utf-8-sig").splitlines()
    out=[]
    i=0
    while i<len(lines):
        s=lines[i].strip()
        if not s or s.startswith("//"):
            i+=1
            continue
        if i+2>=len(lines) or not lines[i+1].strip().startswith('"') or lines[i+2].strip()!="END":
            raise RuntimeError(f"invalid STR block at {s}")
        out.append(s)
        i+=3
    return out

def main():
    repo=Path(sys.argv[1] if len(sys.argv)>1 else ".").resolve()
    pkg=Path(__file__).resolve().parents[1]
    patch=repo/"GeneralsMD/Code/Data/Patch.str"
    ptbr=pkg/"payload/GeneralsMD/Code/Data/PortugueseBrazil/Generals.str"
    p=set(labels(patch))
    t=set(labels(ptbr))
    missing=sorted(p-t)
    print(f"Patch.str labels: {len(p)}")
    print(f"PT-BR coverage: {len(p)-len(missing)}/{len(p)}")
    if missing:
        print("Missing:")
        for x in missing: print(x)
        raise SystemExit(1)
    print("PATCH LABEL COVERAGE PASS")

if __name__=="__main__":
    main()
