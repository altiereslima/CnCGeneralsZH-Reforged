#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import argparse
import datetime as dt
import json
import os
import shutil
import subprocess
import sys
import traceback

EXPECTED_UPSTREAM_HEAD = "46eb93120cc43d49230de5f8100538a147f7bbd0"

PATCHED_SOURCE_FILES = [
    "GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h",
    "GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp",
    "GeneralsMD/Code/GameEngine/Source/GameClient/GameText.cpp",
    "GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp",
    "GeneralsMD/Code/Data/Patch.str",
    "GeneralsMD/Code/GameEngine/Source/GameClient/GlobalLanguage.cpp",
    "GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp",
    "GeneralsMD/Code/GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp",
    "GeneralsMD/Code/CMakeLists.txt",
]

def now():
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")

def run(cmd, *, cwd=None, log=None, check=True):
    cmd = [str(x) for x in cmd]
    print("[stage15]", " ".join(f'"{x}"' if " " in x else x for x in cmd))
    cp = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if log:
        Path(log).write_text(cp.stdout or "", encoding="utf-8", errors="replace")
    if cp.stdout:
        print(cp.stdout, end="" if cp.stdout.endswith("\n") else "\n")
    if check and cp.returncode != 0:
        raise RuntimeError(f"comando falhou ({cp.returncode}): {' '.join(cmd)}")
    return cp

def find_cmake(explicit=None):
    if explicit:
        p = Path(explicit)
        if p.is_file():
            return p
        raise RuntimeError(f"cmake não encontrado em: {p}")

    w = shutil.which("cmake")
    if w:
        return Path(w)

    if os.name == "nt":
        editions = ["Community", "Professional", "Enterprise", "BuildTools"]
        roots = [
            Path(os.environ.get("ProgramFiles", r"C:\Program Files")),
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")),
        ]
        for root in roots:
            for ed in editions:
                p = root / "Microsoft Visual Studio" / "2022" / ed / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe"
                if p.is_file():
                    return p
    raise RuntimeError("cmake não encontrado. Instale Visual Studio 2022 Desktop C++ ou coloque cmake no PATH.")

def find_ctest(cmake):
    name = "ctest.exe" if os.name == "nt" else "ctest"
    sibling = Path(cmake).with_name(name)
    if sibling.is_file():
        return sibling
    w = shutil.which("ctest")
    if w:
        return Path(w)
    raise RuntimeError("ctest não encontrado junto do cmake nem no PATH.")

def git_info(repo):
    info = {"available": False, "is_repo": False, "head": None, "dirty": None}
    git = shutil.which("git")
    if not git:
        return info
    info["available"] = True
    cp = subprocess.run([git, "-C", str(repo), "rev-parse", "--is-inside-work-tree"],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if cp.returncode != 0 or cp.stdout.strip() != "true":
        return info
    info["is_repo"] = True
    cp = subprocess.run([git, "-C", str(repo), "rev-parse", "HEAD"],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if cp.returncode == 0:
        info["head"] = cp.stdout.strip()
    cp = subprocess.run([git, "-C", str(repo), "status", "--porcelain"],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if cp.returncode == 0:
        info["dirty"] = bool(cp.stdout.strip())
    return info

def source_state(repo):
    h = repo / "GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h"
    if not h.is_file():
        raise RuntimeError("checkout inválido: GlobalData.h não encontrado")
    text = h.read_text(encoding="utf-8-sig")
    if "TEXT_LANGUAGE_PORTUGUESE_BRAZIL" in text:
        return "patched"
    if "TEXT_LANGUAGE_TURKISH" in text and "TEXT_LANGUAGE_COUNT" in text:
        return "clean"
    return "unknown"

def backup_sources(repo, out_root):
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = out_root / f"backup_{stamp}"
    for rel in PATCHED_SOURCE_FILES:
        src = repo / rel
        if src.is_file():
            dst = backup / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
    manifest = {
        "created": now(),
        "source_repo": str(repo),
        "files": [x for x in PATCHED_SOURCE_FILES if (repo/x).is_file()],
    }
    backup.mkdir(parents=True, exist_ok=True)
    (backup/"MANIFEST.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    return backup

def check_run_output(repo):
    run_dir = repo / "GeneralsMD" / "Run"
    exe = run_dir / "generals.exe"
    loc = run_dir / "Data" / "PortugueseBrazil"
    required = [
        "Generals.str",
        "Language.ini",
        "Movies/EA_LOGO.BIK",
        "Movies/EA_LOGO640.BIK",
        "Movies/sizzle_review.bik",
        "Movies/sizzle_review640.bik",
        "Art/Textures/defeated.dds",
        "Art/Textures/gameover.dds",
        "Art/Textures/GameOver.tga",
        "Art/Textures/victorious.dds",
    ]
    missing = [x for x in required if not (loc/x).is_file()]
    return {
        "generals_exe": str(exe),
        "generals_exe_exists": exe.is_file(),
        "locale_dir": str(loc),
        "locale_missing": missing,
        "pass": exe.is_file() and not missing,
    }

def main():
    ap = argparse.ArgumentParser(description="Zero Hour Reforged PT-BR Stage 15 Windows runner")
    ap.add_argument("repo", help="raiz do checkout CnCGeneralsZH-Reforged")
    ap.add_argument("--config", default="Release", choices=["Release","RelWithDebInfo","Debug"])
    ap.add_argument("--build-dir", default=None, help="diretório de build (padrão: <repo>\\build)")
    ap.add_argument("--cmake", default=None, help="caminho explícito para cmake.exe")
    ap.add_argument("--clean", action="store_true", help="apaga o diretório de build antes de configurar")
    ap.add_argument("--skip-tests", action="store_true")
    ap.add_argument("--allow-dirty", action="store_true", help="permite checkout Git com alterações locais")
    ap.add_argument("--preflight-only", action="store_true", help="aplica/verifica o patch, mas não compila; funciona fora do Windows")
    ap.add_argument("--result", default=None, help="caminho do JSON de resultado")
    args = ap.parse_args()

    pkg = Path(__file__).resolve().parents[1]
    repo = Path(args.repo).resolve()
    if not (repo/"GeneralsMD/Code").is_dir():
        raise SystemExit("STAGE15: aponte para a raiz do checkout CnCGeneralsZH-Reforged")

    result_dir = repo / "PTBR_STAGE15_RESULTS"
    result_dir.mkdir(parents=True, exist_ok=True)
    logs = result_dir / "logs"
    logs.mkdir(exist_ok=True)
    result_path = Path(args.result).resolve() if args.result else result_dir/"result.json"

    result = {
        "stage": 15,
        "started": now(),
        "repo": str(repo),
        "expected_upstream_head": EXPECTED_UPSTREAM_HEAD,
        "preflight_only": args.preflight_only,
        "config": args.config,
        "steps": {},
    }

    try:
        gi = git_info(repo)
        result["git"] = gi
        if gi["is_repo"] and gi["dirty"] and not args.allow_dirty:
            raise RuntimeError("checkout Git possui alterações locais. Use --allow-dirty somente se souber o que está fazendo.")
        if gi["head"] and gi["head"] != EXPECTED_UPSTREAM_HEAD:
            result["git"]["head_warning"] = "HEAD difere do snapshot validado; os validators de anchors decidirão se o patch ainda é compatível."

        state = source_state(repo)
        result["source_state_before"] = state

        if state == "clean":
            backup = backup_sources(repo, result_dir)
            result["backup"] = str(backup)
            run([sys.executable, str(pkg/"tools/apply_stage12.py"), str(repo)],
                log=logs/"01_apply_stage12.log")
            result["steps"]["apply_patch"] = "PASS"
        elif state == "patched":
            result["steps"]["apply_patch"] = "SKIPPED_ALREADY_PATCHED"
        else:
            raise RuntimeError("estado do fonte não reconhecido; não é seguro aplicar o patch")

        run([sys.executable, str(pkg/"tools/verify_patched_fixture.py"), str(repo)],
            log=logs/"02_verify_patched_source.log")
        result["steps"]["verify_patched_source"] = "PASS"

        run([sys.executable, str(pkg/"tools/compile_preflight.py"), str(repo)],
            log=logs/"03_compile_preflight.log")
        result["steps"]["compile_preflight"] = "PASS"

        run([sys.executable, str(pkg/"tools/check_patch_label_coverage.py"), str(repo)],
            log=logs/"04_patch_label_coverage.log")
        result["steps"]["patch_label_coverage"] = "PASS"

        if args.preflight_only:
            result["steps"]["configure"] = "SKIPPED_PREFLIGHT_ONLY"
            result["steps"]["build"] = "SKIPPED_PREFLIGHT_ONLY"
            result["steps"]["ctest"] = "SKIPPED_PREFLIGHT_ONLY"
            result["steps"]["run_output"] = "SKIPPED_PREFLIGHT_ONLY"
            result["status"] = "PASS_PREFLIGHT_ONLY"
        else:
            if os.name != "nt":
                raise RuntimeError("build real é Win32/Windows somente. Use --preflight-only fora do Windows.")

            cmake = find_cmake(args.cmake)
            ctest = find_ctest(cmake)
            result["cmake"] = str(cmake)
            result["ctest"] = str(ctest)

            build_dir = Path(args.build_dir).resolve() if args.build_dir else repo/"build"
            result["build_dir"] = str(build_dir)

            if args.clean and build_dir.exists():
                shutil.rmtree(build_dir)
                result["steps"]["clean_build_dir"] = "PASS"

            if not (build_dir/"CMakeCache.txt").is_file():
                run([cmake, "-S", repo/"GeneralsMD/Code", "-B", build_dir,
                     "-G", "Visual Studio 17 2022", "-A", "Win32"],
                    log=logs/"05_cmake_configure.log")
                result["steps"]["configure"] = "PASS"
            else:
                result["steps"]["configure"] = "SKIPPED_CACHE_EXISTS"

            run([cmake, "--build", build_dir, "--config", args.config],
                log=logs/"06_build.log")
            result["steps"]["build"] = "PASS"

            if args.skip_tests:
                result["steps"]["ctest"] = "SKIPPED_BY_USER"
            else:
                run([ctest, "--test-dir", build_dir, "-C", args.config, "--output-on-failure"],
                    log=logs/"07_ctest.log")
                result["steps"]["ctest"] = "PASS"

            output = check_run_output(repo)
            result["run_output"] = output
            if not output["pass"]:
                raise RuntimeError("build terminou, mas generals.exe ou o payload PT-BR não apareceu em GeneralsMD/Run")
            result["steps"]["run_output"] = "PASS"
            result["status"] = "PASS"

    except Exception as exc:
        result["status"] = "FAIL"
        result["error"] = str(exc)
        result["traceback"] = traceback.format_exc()
        raise
    finally:
        result["finished"] = now()
        result_path.parent.mkdir(parents=True, exist_ok=True)
        result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"[stage15] resultado: {result_path}")
        print(f"[stage15] status: {result.get('status','UNKNOWN')}")

if __name__ == "__main__":
    main()
