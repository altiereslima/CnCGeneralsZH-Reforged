#!/usr/bin/env python3
from pathlib import Path
import argparse, hashlib, zipfile

PATHS = [
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader",
]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("repo")
    ap.add_argument("output")
    args=ap.parse_args()
    repo=Path(args.repo).resolve()
    out=Path(args.output).resolve()
    missing=[x for x in PATHS if not (repo/x).is_dir()]
    if missing:
        raise SystemExit("diretórios ausentes: "+", ".join(missing))
    out.parent.mkdir(parents=True,exist_ok=True)
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for rel in PATHS:
            for p in sorted((repo/rel).rglob("*")):
                if p.is_file():
                    z.write(p,p.relative_to(repo).as_posix())
    print(out)
    print("SHA256:",hashlib.sha256(out.read_bytes()).hexdigest())

if __name__=="__main__":
    main()
