#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import argparse
import hashlib
import shutil
import tempfile
import zipfile

REQUIRED = [
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Huff.cpp",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Lz.cpp",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Lzhl.cpp",
]
HEADER_DIR = "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader"

def sha256(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024),b""): h.update(chunk)
    return h.hexdigest()

def safe_extract(zf,dst):
    root=dst.resolve()
    for i in zf.infolist():
        t=(dst/i.filename).resolve()
        try: t.relative_to(root)
        except ValueError: raise RuntimeError(f"entrada insegura: {i.filename}")
        if i.filename.startswith(("/", "\\")): raise RuntimeError(f"entrada absoluta: {i.filename}")
    zf.extractall(dst)

def locate(root):
    for c in [root]+[p for p in root.iterdir() if p.is_dir()]:
        if all((c/x).is_file() for x in REQUIRED) and (c/HEADER_DIR).is_dir():
            return c
    raise RuntimeError("bundle LZH inválido")

def validate(root):
    miss=[x for x in REQUIRED if not (root/x).is_file()]
    hd=root/HEADER_DIR
    if not hd.is_dir() or not any(p.is_file() for p in hd.rglob("*")):
        miss.append(HEADER_DIR+"/")
    if miss: raise RuntimeError("LZH-Light incompleto: "+", ".join(miss))

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--repo",required=True)
    ap.add_argument("--archive",required=True)
    ap.add_argument("--sha256",required=True)
    args=ap.parse_args()

    repo=Path(args.repo).resolve()
    arc=Path(args.archive).resolve()
    got=sha256(arc)
    if got.lower()!=args.sha256.strip().lower():
        raise SystemExit(f"SHA-256 LZH incorreto: {got}")

    with tempfile.TemporaryDirectory(prefix="zh-lzh-") as td:
        td=Path(td)
        with zipfile.ZipFile(arc) as z: safe_extract(z,td)
        srcroot=locate(td)
        validate(srcroot)
        src=srcroot/"GeneralsMD/Code/Libraries/Source/Compression/LZHCompress"
        dst=repo/"GeneralsMD/Code/Libraries/Source/Compression/LZHCompress"
        dst.mkdir(parents=True,exist_ok=True)
        shutil.copytree(src/"CompLibSource",dst/"CompLibSource",dirs_exist_ok=True)
        shutil.copytree(src/"CompLibHeader",dst/"CompLibHeader",dirs_exist_ok=True)

    validate(repo)
    print("LZH-LIGHT INSTALL PASS")

if __name__=="__main__":
    main()
