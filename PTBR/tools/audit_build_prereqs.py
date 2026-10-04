#!/usr/bin/env python3
from pathlib import Path
import argparse, json

REQUIRED = [
    # Public/generated dependency trees.
    "GeneralsMD/Code/Libraries/Source/Compression/ZLib/adler32.c",
    "GeneralsMD/Code/Libraries/Source/Compression/ZLib/zlib.h",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Huff.cpp",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Lz.cpp",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Lzhl.cpp",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/_huff.h",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/_lz.h",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/_lzhl.h",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/lzhl.h",
    "GeneralsMD/Code/Libraries/Source/GameSpy/CMakeLists.txt",
    "GeneralsMD/Code/Libraries/DirectX/Include/d3d8.h",
    "GeneralsMD/Code/Libraries/DirectX/Include/d3dxmath.h",
    "GeneralsMD/Code/Libraries/Source/litehtml/CMakeLists.txt",
    "GeneralsMD/Code/Libraries/Source/nanosvg/nanosvg.h",
    "GeneralsMD/Code/Libraries/Source/nanosvg/nanosvgrast.h",

    # Tracked backends used instead of the proprietary Miles/Bink SDKs (x64 build).
    "GeneralsMD/Code/Libraries/Source/WWVegas/Bink/ffmpeg/bink_ffmpeg.cpp",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Bink/include/bink.h",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Miles6/xaudio2/miles_xaudio2.cpp",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Miles6/include/MSS/MSS.h",
    "GeneralsMD/Code/Libraries/Source/FFmpeg/dist/include/libavcodec/avcodec.h",
    "GeneralsMD/Code/Libraries/Source/FFmpeg/dist/lib/avcodec.lib",
    "GeneralsMD/Code/Libraries/Source/FFmpeg/dist/bin/avcodec-62.dll",

    # Core build entry points.
    "GeneralsMD/Code/CMakeLists.txt",
    "GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h",
]

# Dependências que precisam do patch do fork: o CMake confere a mesma marca e para sem ela, com uma
# mensagem que fala do vendor.ps1 do upstream, não do instalador daqui.
PATCHED = [
    ("GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/_lz.h", "Zero Hour Reforged: altered"),
    ("GeneralsMD/Code/Libraries/Source/litehtml/include/litehtml/document.h", "master_parsed"),
]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--repo",required=True)
    args=ap.parse_args()
    repo=Path(args.repo).resolve()
    missing=[x for x in REQUIRED if not (repo/x).is_file()]
    unpatched=[path for path,marker in PATCHED
               if (repo/path).is_file() and marker not in (repo/path).read_text(encoding="utf-8",errors="replace")]
    out={
        "status":"PASS" if not missing and not unpatched else "FAIL",
        "checked":len(REQUIRED),
        "missing":missing,
        "unpatched":unpatched,
    }
    print(json.dumps(out,indent=2))
    if missing or unpatched:
        raise SystemExit(1)

if __name__=="__main__":
    main()
