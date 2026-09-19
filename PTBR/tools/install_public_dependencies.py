#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import argparse
import hashlib
import os
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

ZLIB_URL = "https://zlib.net/fossils/zlib-1.1.4.tar.gz"
ZLIB_MD5 = "abc405d0bdd3ee22782d7aa20e440f08"
GAMESPY_REPO = "https://github.com/TheSuperHackers/GamespySDK.git"
GAMESPY_COMMIT = "b1b77d8f1f30d289b4b4910d305a377f706a0bf7"

ZLIB_REQUIRED = [
    "adler32.c", "compress.c", "crc32.c", "deflate.c", "gzio.c",
    "infblock.c", "infcodes.c", "inffast.c", "inflate.c", "inftrees.c",
    "infutil.c", "trees.c", "uncompr.c", "zutil.c", "zlib.h", "zconf.h",
]

def md5(path: Path) -> str:
    h=hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024), b""):
            h.update(chunk)
    return h.hexdigest()

def safe_extract_tar(archive: Path, dst: Path):
    root=dst.resolve()
    with tarfile.open(archive, "r:*") as tf:
        for m in tf.getmembers():
            target=(dst/m.name).resolve()
            try:
                target.relative_to(root)
            except ValueError:
                raise RuntimeError(f"entrada insegura no tar: {m.name}")
            if m.issym() or m.islnk():
                raise RuntimeError(f"link não permitido no tar: {m.name}")
        # Python 3.12 supports the data filter.  It also removes the Python 3.14
        # deprecation warning seen on GitHub Actions.
        tf.extractall(dst, filter="data")

def safe_extract_zip(zf: zipfile.ZipFile, dst: Path):
    root=dst.resolve()
    for info in zf.infolist():
        target=(dst/info.filename).resolve()
        try:
            target.relative_to(root)
        except ValueError:
            raise RuntimeError(f"entrada insegura no zip: {info.filename}")
        if info.filename.startswith(("/", "\\")):
            raise RuntimeError(f"entrada absoluta no zip: {info.filename}")
    zf.extractall(dst)

def download(url: str, dst: Path):
    req=urllib.request.Request(url, headers={"User-Agent":"ZH-Reforged-PTBR-CI/1.0"})
    with urllib.request.urlopen(req, timeout=60) as r, dst.open("wb") as f:
        shutil.copyfileobj(r, f)

def install_zlib(repo: Path, archive_override: Path|None):
    dst=repo/"GeneralsMD/Code/Libraries/Source/Compression/ZLib"
    if all((dst/x).is_file() for x in ZLIB_REQUIRED):
        return {"status":"PRESENT","path":str(dst)}

    with tempfile.TemporaryDirectory(prefix="zh-zlib-") as td:
        td=Path(td)
        arc=archive_override if archive_override else td/"zlib-1.1.4.tar.gz"
        if not archive_override:
            download(ZLIB_URL, arc)
        got=md5(arc)
        if got.lower()!=ZLIB_MD5:
            raise RuntimeError(f"zlib 1.1.4 MD5 inválido: {got}")
        extract=td/"extract"
        extract.mkdir()
        safe_extract_tar(arc, extract)

        roots=[p for p in extract.iterdir() if p.is_dir()]
        src=None
        for c in roots:
            if (c/"zlib.h").is_file() and (c/"adler32.c").is_file():
                src=c
                break
        if src is None:
            raise RuntimeError("arquivo zlib não contém a árvore esperada")

        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(src,dst)

    missing=[x for x in ZLIB_REQUIRED if not (dst/x).is_file()]
    if missing:
        raise RuntimeError("zlib incompleto após instalação: "+", ".join(missing))
    return {"status":"INSTALLED","path":str(dst),"md5":ZLIB_MD5}

def run(cmd, cwd=None):
    cp=subprocess.run([str(x) for x in cmd],cwd=cwd,text=True,
                      stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if cp.returncode:
        raise RuntimeError(cp.stdout)
    return cp.stdout

def validate_gamespy(path: Path):
    required=["CMakeLists.txt"]
    missing=[x for x in required if not (path/x).is_file()]
    if missing:
        raise RuntimeError("GameSpy SDK inválido: "+", ".join(missing))

def install_gamespy(repo: Path, source_override: Path|None):
    dst=repo/"GeneralsMD/Code/Libraries/Source/GameSpy"
    if (dst/"CMakeLists.txt").is_file():
        return {"status":"PRESENT","path":str(dst)}

    if source_override:
        validate_gamespy(source_override)
        if dst.exists(): shutil.rmtree(dst)
        shutil.copytree(source_override,dst)
        return {"status":"INSTALLED_FROM_OVERRIDE","path":str(dst)}

    git=shutil.which("git")
    if not git:
        raise RuntimeError("git não encontrado para instalar GameSpy SDK")

    parent=dst.parent
    parent.mkdir(parents=True,exist_ok=True)

    # Clone outside the source tree.  Do NOT move the clone itself into GameSpy:
    # on Windows, Git may leave pack/index files under .git with attributes that
    # make shutil.rmtree() fail with WinError 5.  Instead, export the pinned
    # commit with `git archive`, which contains only tracked files and no .git.
    temp_root=Path(tempfile.mkdtemp(prefix="zh-gamespy-"))
    clone=temp_root/"GamespySDK"
    archive=temp_root/"gamespy.zip"

    run([git,"clone","--no-checkout","--filter=blob:none",GAMESPY_REPO,str(clone)])
    run([git,"checkout",GAMESPY_COMMIT],cwd=clone)
    head=run([git,"rev-parse","HEAD"],cwd=clone).strip()
    if head.lower()!=GAMESPY_COMMIT.lower():
        raise RuntimeError(f"commit GameSpy inesperado: {head}")
    validate_gamespy(clone)

    run([git,"archive","--format=zip","--output",archive,GAMESPY_COMMIT],cwd=clone)

    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive) as zf:
        safe_extract_zip(zf,dst)

    validate_gamespy(dst)
    if (dst/".git").exists():
        raise RuntimeError("export GameSpy inesperadamente contém .git")

    # temp_root intentionally stays in the runner temp directory.  The hosted
    # Windows runner removes it when the job ends; avoiding in-process deletion
    # also avoids Windows file-attribute races on .git pack files.
    return {"status":"INSTALLED","path":str(dst),"commit":GAMESPY_COMMIT}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--repo",required=True)
    ap.add_argument("--zlib-archive",default=None,
                    help="fixture/offline override for zlib-1.1.4.tar.gz")
    ap.add_argument("--gamespy-source",default=None,
                    help="fixture/offline override for an already extracted GamespySDK tree")
    args=ap.parse_args()

    repo=Path(args.repo).resolve()
    if not (repo/"GeneralsMD/Code").is_dir():
        raise SystemExit("checkout Reforged inválido")

    z=install_zlib(repo, Path(args.zlib_archive).resolve() if args.zlib_archive else None)
    g=install_gamespy(repo, Path(args.gamespy_source).resolve() if args.gamespy_source else None)

    print("PUBLIC DEPENDENCIES INSTALL PASS")
    print("zlib:",z)
    print("gamespy:",g)

if __name__=="__main__":
    main()
