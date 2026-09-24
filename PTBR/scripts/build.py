#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
import argparse, datetime as dt, json, os, shutil, subprocess, sys, traceback

CORE=["Generals.str","Language.ini"]
MEDIA=[
    "Movies/EA_LOGO.BIK","Movies/EA_LOGO640.BIK",
    "Movies/sizzle_review.bik","Movies/sizzle_review640.bik",
    "Art/Textures/defeated.dds","Art/Textures/gameover.dds",
    "Art/Textures/GameOver.tga","Art/Textures/victorious.dds",
]

def run(cmd,cwd=None,log=None):
    cmd=[str(x) for x in cmd]
    print("[ptbr]"," ".join(cmd))
    cp=subprocess.run(cmd,cwd=cwd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if log:
        Path(log).write_text(cp.stdout or "",encoding="utf-8",errors="replace")
    if cp.stdout:
        print(cp.stdout,end="" if cp.stdout.endswith("\n") else "\n")
    if cp.returncode:
        raise RuntimeError(f"command failed ({cp.returncode}): {' '.join(cmd)}")
    return cp

def git_info(repo):
    git=shutil.which("git")
    if not git:
        return {"is_repo":False}
    cp=subprocess.run([git,"-C",str(repo),"rev-parse","--is-inside-work-tree"],
                      text=True,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
    if cp.returncode or cp.stdout.strip()!="true":
        return {"is_repo":False}
    head=subprocess.check_output([git,"-C",str(repo),"rev-parse","HEAD"],text=True).strip()
    dirty=bool(subprocess.check_output([git,"-C",str(repo),"status","--porcelain"],text=True).strip())
    return {"is_repo":True,"head":head,"dirty":dirty}

def media_present(package):
    loc=package/"Data/PortugueseBrazil"
    missing=[x for x in CORE if not (loc/x).is_file()]
    if missing:
        raise RuntimeError("PT-BR package is missing: "+", ".join(missing))
    found=[x for x in MEDIA if (loc/x).is_file()]
    if found and len(found)!=len(MEDIA):
        raise RuntimeError("PT-BR media set is partial")
    return len(found)==len(MEDIA)

def find_cmake(explicit=None):
    if explicit:
        p=Path(explicit)
        if p.is_file(): return p
        raise RuntimeError(f"cmake not found: {p}")
    w=shutil.which("cmake")
    if w: return Path(w)
    if os.name=="nt":
        roots=[Path(os.environ.get("ProgramFiles",r"C:\Program Files")),
               Path(os.environ.get("ProgramFiles(x86)",r"C:\Program Files (x86)"))]
        for root in roots:
            for ed in ["Community","Professional","Enterprise","BuildTools"]:
                p=root/"Microsoft Visual Studio"/"2022"/ed/"Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
                if p.is_file(): return p
    raise RuntimeError("cmake was not found")

def find_ctest(cmake):
    name="ctest.exe" if os.name=="nt" else "ctest"
    p=Path(cmake).with_name(name)
    if p.is_file(): return p
    w=shutil.which("ctest")
    if w: return Path(w)
    raise RuntimeError("ctest was not found")

def check_output(repo,require_media):
    run_dir=repo/"GeneralsMD/Run"
    exe=run_dir/"generals.exe"
    loc=run_dir/"Data/PortugueseBrazil"
    missing_core=[x for x in CORE if not (loc/x).is_file()]
    missing_media=[x for x in MEDIA if not (loc/x).is_file()]
    ok=exe.is_file() and not missing_core and (not require_media or not missing_media)
    return {"pass":ok,"exe":str(exe),"missing_core":missing_core,
            "media_required":require_media,"missing_media":missing_media}

def main():
    ap=argparse.ArgumentParser(description="Build Zero Hour Reforged with PT-BR integration")
    ap.add_argument("repo",nargs="?",default=".")
    ap.add_argument("--config",default="Release",choices=["Release","RelWithDebInfo","Debug"])
    ap.add_argument("--build-dir",default=None)
    ap.add_argument("--cmake",default=None)
    ap.add_argument("--clean",action="store_true")
    ap.add_argument("--skip-tests",action="store_true")
    ap.add_argument("--allow-dirty",action="store_true")
    ap.add_argument("--preflight-only",action="store_true")
    args=ap.parse_args()

    repo=Path(args.repo).resolve()
    package=Path(__file__).resolve().parents[1]
    results=repo/"PTBR_BUILD_RESULTS"
    result_path=results/"result.json"
    result={"started":dt.datetime.now().astimezone().isoformat(timespec="seconds"),
            "repo":str(repo),"config":args.config,"steps":{}}

    try:
        gi=git_info(repo)
        result["git"]=gi
        if gi.get("is_repo") and gi.get("dirty") and not args.allow_dirty:
            raise RuntimeError("checkout has local changes; use --allow-dirty only when intentional")

        has_media=media_present(package)
        result["localized_media_in_package"]=has_media
        results.mkdir(parents=True,exist_ok=True)
        logs=results/"logs"; logs.mkdir(exist_ok=True)

        scripts=package/"scripts"
        run([sys.executable,scripts/"apply.py",repo],log=logs/"apply.log")
        result["steps"]["apply"]="PASS"

        validate=[sys.executable,scripts/"validate.py","--repo",repo]
        if not has_media:
            validate.append("--allow-missing-media")
        run(validate,log=logs/"validate.log")
        result["steps"]["validate"]="PASS"

        if args.preflight_only:
            result["status"]="PASS_PREFLIGHT_ONLY"
            result["steps"]["configure"]="SKIPPED"
            result["steps"]["build"]="SKIPPED"
            result["steps"]["ctest"]="SKIPPED"
            return

        if os.name!="nt":
            raise RuntimeError("real build is Windows/Win32 only")

        cmake=find_cmake(args.cmake)
        ctest=find_ctest(cmake)
        build=Path(args.build_dir).resolve() if args.build_dir else repo/"build"

        if args.clean and build.exists():
            shutil.rmtree(build)

        run([cmake,"-S",repo/"GeneralsMD/Code","-B",build,
             "-G","Visual Studio 17 2022","-A","Win32"],log=logs/"configure.log")
        result["steps"]["configure"]="PASS"

        run([cmake,"--build",build,"--config",args.config],log=logs/"build.log")
        result["steps"]["build"]="PASS"

        if args.skip_tests:
            result["steps"]["ctest"]="SKIPPED"
        else:
            run([ctest,"--test-dir",build,"-C",args.config,"--output-on-failure"],
                log=logs/"ctest.log")
            result["steps"]["ctest"]="PASS"

        out=check_output(repo,has_media)
        result["output"]=out
        if not out["pass"]:
            raise RuntimeError("build completed but expected PT-BR runtime output is incomplete")
        result["status"]="PASS"
    except Exception as exc:
        result["status"]="FAIL"
        result["error"]=str(exc)
        result["traceback"]=traceback.format_exc()
        raise
    finally:
        result["finished"]=dt.datetime.now().astimezone().isoformat(timespec="seconds")
        result_path.parent.mkdir(parents=True,exist_ok=True)
        result_path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
        print("PT-BR result:",result_path)
        print("PT-BR status:",result["status"])

if __name__=="__main__":
    main()
