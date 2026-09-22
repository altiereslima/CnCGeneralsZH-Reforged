#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import argparse
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import traceback

EXPECTED_UPSTREAM_HEAD = "e378d932218e1d6a9cb10d3b41cab4e0a5061547"

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

# Casos de teste que falham no upstream puro, sem o patch PT-BR. Conferido em
# e378d932 (v2.1.0) com o test_gameengine recompilado sem o patch: desde o build x64,
# o gerador de mapas aleatórios cobre mais de 1/5 do mapa de teste com rocha.
# Só estes nomes são tolerados; qualquer outra falha, crash ou timeout derruba o build.
KNOWN_UPSTREAM_TEST_FAILURES = {
    "the_ground_is_textured_by_what_the_ground_is_doing",
}

CORE_LOCALE_FILES=["Generals.str","Language.ini"]
MEDIA_LOCALE_FILES=[
    "Movies/EA_LOGO.BIK","Movies/EA_LOGO640.BIK",
    "Movies/sizzle_review.bik","Movies/sizzle_review640.bik",
    "Art/Textures/defeated.dds","Art/Textures/gameover.dds",
    "Art/Textures/GameOver.tga","Art/Textures/victorious.dds",
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

def detect_package_media(pkg):
    loc=pkg/"payload/GeneralsMD/Code/Data/PortugueseBrazil"
    miss=[x for x in CORE_LOCALE_FILES if not (loc/x).is_file()]
    if miss:
        raise RuntimeError("pacote PT-BR sem arquivos essenciais: "+", ".join(miss))
    present=[x for x in MEDIA_LOCALE_FILES if (loc/x).is_file()]
    if present and len(present)!=len(MEDIA_LOCALE_FILES):
        missing=[x for x in MEDIA_LOCALE_FILES if x not in present]
        raise RuntimeError("pacote de mídia PT-BR parcial: faltando "+", ".join(missing))
    return len(present)==len(MEDIA_LOCALE_FILES)

def check_run_output(repo,require_media):
    run_dir=repo/"GeneralsMD/Run"
    exe=run_dir/"generals.exe"
    loc=run_dir/"Data/PortugueseBrazil"
    miss_core=[x for x in CORE_LOCALE_FILES if not (loc/x).is_file()]
    miss_media=[x for x in MEDIA_LOCALE_FILES if not (loc/x).is_file()]
    return {
        "generals_exe":str(exe),
        "generals_exe_exists":exe.is_file(),
        "locale_dir":str(loc),
        "core_missing":miss_core,
        "media_required":require_media,
        "media_missing":miss_media,
        "media_status":"COMPLETE" if not miss_media else ("OPTIONAL_NOT_INCLUDED" if not require_media else "MISSING_REQUIRED"),
        "pass":exe.is_file() and not miss_core and (not require_media or not miss_media),
    }

def unexpected_test_failures(output):
    """Devolve (casos conhecidos que falharam, problemas) a partir da saída do ctest."""
    if "The following tests FAILED:" not in output:
        return [], ["ctest falhou sem listar os testes que falharam"]
    summary=output.split("The following tests FAILED:")[-1]
    failed=re.findall(r"(?m)^\s*\d+ - (\S+) \(([^)]*)\)\s*$", summary)
    known=[]
    problems=[]
    for name,status in failed:
        if status!="Failed":
            problems.append(f"{name}: {status}")
            continue
        m=re.search(rf"(?m)^.*Test\s+#\d+: {re.escape(name)} \..*$", output)
        if not m:
            problems.append(f"{name}: saída não encontrada")
            continue
        rest=output[m.end():]
        end=re.search(r"(?m)^\s*Start\s+\d+: |^\d+% tests passed", rest)
        block=rest[:end.start()] if end else rest
        tail=" / ".join([x.strip() for x in block.splitlines() if x.strip()][-4:])
        # Sem o resumo do harness o binário não chegou ao fim (crash no meio), ou é
        # um smoke test fora do harness.
        if not re.search(r"(?m)^\d+ tests, \d+ checks, \d+ failed\s*$", block):
            problems.append(f"{name}: terminou sem o resumo do harness | {tail}")
            continue
        cases=re.findall(r"(?m)^FAIL (\S+) \(\d+\)\s*$", block)
        extra=[c for c in cases if c not in KNOWN_UPSTREAM_TEST_FAILURES]
        if not cases or extra:
            problems.append(f"{name}: "+(", ".join(extra) or "falhou sem caso identificado")+f" | {tail}")
            continue
        known.extend(cases)
    if not failed:
        problems.append("ctest falhou, mas nenhum teste aparece na lista de falhas")
    return sorted(set(known)), problems

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

        media_present=detect_package_media(pkg)
        result["localized_media_in_package"]=media_present

        result_dir.mkdir(parents=True,exist_ok=True)
        logs=result_dir/"logs"
        logs.mkdir(exist_ok=True)

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

        verify_cmd=[sys.executable,str(pkg/"tools/verify_patched_fixture.py"),str(repo)]
        if not media_present:
            verify_cmd.append("--allow-missing-media")
        run(verify_cmd,log=logs/"02_verify_patched_source.log")
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
                raise RuntimeError("build real é Windows x64 somente. Use --preflight-only fora do Windows.")

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
                     "-G", "Visual Studio 17 2022", "-A", "x64"],
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
                cp = run([ctest, "--test-dir", build_dir, "-C", args.config, "--output-on-failure"],
                         log=logs/"07_ctest.log", check=False)
                if cp.returncode == 0:
                    result["steps"]["ctest"] = "PASS"
                else:
                    known, problems = unexpected_test_failures(cp.stdout or "")
                    if problems:
                        raise RuntimeError("ctest falhou:\n" + "\n".join(problems))
                    result["steps"]["ctest"] = "PASS_WITH_KNOWN_UPSTREAM_FAILURES"
                    result["known_upstream_test_failures"] = known
                    print("::warning::falhas conhecidas do upstream toleradas: " + ", ".join(known))

            output = check_run_output(repo,require_media=media_present)
            result["run_output"] = output
            if not output["pass"]:
                raise RuntimeError("build terminou, mas generals.exe ou o payload PT-BR não apareceu em GeneralsMD/Run")
            result["steps"]["run_output"] = "PASS"
            result["status"] = "PASS"

    except Exception as exc:
        result["status"] = "FAIL"
        result["error"] = str(exc)
        result["traceback"] = traceback.format_exc()
        # No Actions, cada linha vira anotação no resumo do run: o log completo só
        # abre para quem está logado, as anotações aparecem para qualquer um.
        if os.environ.get("GITHUB_ACTIONS"):
            for line in str(exc).splitlines()[:10]:
                print(f"::error::{line}")
        raise
    finally:
        result["finished"] = now()
        result_path.parent.mkdir(parents=True, exist_ok=True)
        result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"[stage15] resultado: {result_path}")
        print(f"[stage15] status: {result.get('status','UNKNOWN')}")

if __name__ == "__main__":
    main()
