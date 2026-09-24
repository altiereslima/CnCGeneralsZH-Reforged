#!/usr/bin/env python3
from pathlib import Path
import argparse, hashlib, shutil, tempfile, zipfile

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

def sha256(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024), b""):
            h.update(chunk)
    return h.hexdigest()

def safe_extract(zf,dst):
    root=dst.resolve()
    for info in zf.infolist():
        target=(dst/info.filename).resolve()
        try:
            target.relative_to(root)
        except ValueError:
            raise RuntimeError(f"entrada insegura no ZIP: {info.filename}")
        if info.filename.startswith(("/", "\\")):
            raise RuntimeError(f"entrada absoluta no ZIP: {info.filename}")
    zf.extractall(dst)

def find_root(extracted):
    candidates=[extracted]+[x for x in extracted.iterdir() if x.is_dir()]
    for c in candidates:
        if all((c/f).is_file() for f in MEDIA_FILES):
            return c
        nested=c/"PortugueseBrazil"
        if nested.is_dir() and all((nested/f).is_file() for f in MEDIA_FILES):
            return nested
    raise RuntimeError("bundle de mídia PT-BR não possui o layout esperado")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--package",required=True)
    ap.add_argument("--archive",required=True)
    ap.add_argument("--sha256",required=True)
    args=ap.parse_args()

    package=Path(args.package).resolve()
    archive=Path(args.archive).resolve()
    if not (package/"Data/PortugueseBrazil/Generals.str").is_file():
        raise SystemExit("pacote PTBR inválido")

    got=sha256(archive)
    if got.lower()!=args.sha256.strip().lower():
        raise SystemExit(f"SHA-256 da mídia incorreto: esperado {args.sha256}, obtido {got}")

    with tempfile.TemporaryDirectory(prefix="zh-ptbr-media-") as td:
        td=Path(td)
        with zipfile.ZipFile(archive) as z:
            safe_extract(z,td)
        src=find_root(td)
        dst=package/"Data/PortugueseBrazil"
        for rel in MEDIA_FILES:
            s=src/rel
            d=dst/rel
            d.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(s,d)

    missing=[x for x in MEDIA_FILES if not (package/"Data/PortugueseBrazil"/x).is_file()]
    if missing:
        raise RuntimeError("mídia ausente após instalação: "+", ".join(missing))
    print("PT-BR MEDIA INSTALL PASS")
    print("SHA256:",got)

if __name__=="__main__":
    main()
