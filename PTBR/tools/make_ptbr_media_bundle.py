#!/usr/bin/env python3
from pathlib import Path
import argparse, hashlib, zipfile

MEDIA_FILES = [
    "Movies/EA_LOGO.BIK",
    "Movies/EA_LOGO640.BIK",
    "Movies/sizzle_review.bik",
    "Movies/sizzle_review640.bik",
    "Art/Textures/defeated.dds",
    "Art/Textures/gameover.dds",
    "Art/Textures/GameOver.tga",
    "Art/Textures/victorious.dds",
]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("locale_dir")
    ap.add_argument("output")
    args=ap.parse_args()
    src=Path(args.locale_dir).resolve()
    out=Path(args.output).resolve()
    missing=[x for x in MEDIA_FILES if not (src/x).is_file()]
    if missing:
        raise SystemExit("arquivos ausentes: "+", ".join(missing))
    out.parent.mkdir(parents=True,exist_ok=True)
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for rel in MEDIA_FILES:
            z.write(src/rel,rel)
    print(out)
    print("SHA256:",hashlib.sha256(out.read_bytes()).hexdigest())

if __name__=="__main__":
    main()
