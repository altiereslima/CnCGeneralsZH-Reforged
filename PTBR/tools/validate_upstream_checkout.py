#!/usr/bin/env python3
from pathlib import Path
import json
import sys

EXPECTED_SNAPSHOT = {
    "head_commit": "46eb93120cc43d49230de5f8100538a147f7bbd0",
    "files": {
        "GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h": "db4a9baf5db5ef27f160eb989ec54e207a8f4841",
        "GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp": "366c120a6af8d21fa9ccc85339206224f2bcb791",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GameText.cpp": "de5aa27ae27cd311081d7abd9c1ddf18372f7d5a",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp": "5a7b19ed7103d9a77c6539ce0462a050aec7c1a3",
        "GeneralsMD/Code/Data/Patch.str": "66aad744bfde6267eff5f4456fc13e296071f41d",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GlobalLanguage.cpp": "115d28f739fdd8f061a2622ecf4f685dd620f174",
        "GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp": "b41dc411edf6034177bef575112742f6f92137fb",
        "GeneralsMD/Code/GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp": "23f711877eb038a8f3b2dba5b27644cb71048337",
        "GeneralsMD/Code/CMakeLists.txt": "36ccc49e5948902d0db8e4929ba0c26e410053a3",
        "GeneralsMD/Code/GameEngine/Include/Common/AsciiString.h": "338c83a046b1338a9b60622ead08db762ff7b41a",
        "GeneralsMD/Code/GameEngine/Include/Common/Debug.h": "708817af5576ace9079e3c8f2900b92eb727882a",
        "GeneralsMD/Code/GameEngine/Source/Common/OptionsCatalog.cpp": "1810ed19ca05df40cb3d840e20ce084dde35dd89",
    }
}

def need(text, needle, name, count=1):
    found = text.count(needle)
    if found != count:
        raise RuntimeError(f"{name}: esperado {count}, encontrado {found}")
    return found

def parse_str_labels(path):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    labels = []
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if not s or s.startswith("//"):
            i += 1
            continue
        if i + 2 >= len(lines) or not lines[i+1].strip().startswith('"') or lines[i+2].strip() != "END":
            raise RuntimeError(f"{path}: bloco STR inválido perto de {s}")
        labels.append(s)
        i += 3
    return labels

def validate(repo):
    repo = Path(repo).resolve()
    code = repo / "GeneralsMD" / "Code"
    if not code.exists():
        raise RuntimeError("aponte para a raiz de CnCGeneralsZH-Reforged")

    result = {"repo": str(repo), "checks": {}}

    h = (code/"GameEngine/Include/Common/GlobalData.h").read_text(encoding="utf-8-sig")
    need(h,
        "\tTEXT_LANGUAGE_ENGLISH\t= 0,\n\tTEXT_LANGUAGE_TURKISH\t= 1,\n\n\tTEXT_LANGUAGE_COUNT\t\t= 2,\n",
        "GlobalData.h enum")
    result["checks"]["language_enum_anchor"] = "PASS"

    cpp = (code/"GameEngine/Source/Common/GlobalData.cpp").read_text(encoding="utf-8-sig")
    need(cpp, "\tm_textLanguage = TEXT_LANGUAGE_ENGLISH;\n", "GlobalData.cpp default")
    result["checks"]["default_language_anchor"] = "PASS"

    gt = (code/"GameEngine/Source/GameClient/GameText.cpp").read_text(encoding="utf-8-sig")
    need(gt,
        "static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =\n{\n\tNULL,\n\t\"Data\\\\Turkish\\\\Generals.str\",\n};\n",
        "GameText.cpp overlay table")
    if '"Data\\\\Patch.str"' not in gt:
        raise RuntimeError("GameText.cpp: Patch.str overlay ausente")
    if "stays English rather than turning into a key name" not in gt:
        raise RuntimeError("GameText.cpp: sem evidência do fallback de rótulos ausentes")
    result["checks"]["english_fallback_architecture"] = "PASS"

    opt = (code/"GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp").read_text(encoding="utf-8-sig")
    need(opt, 'load("Options.ini");', "OptionsMenu.cpp load", 1)
    result["checks"]["options_migration_anchor"] = "PASS"

    pc = (code/"Data/Patch.str").read_text(encoding="utf-8-sig")
    need(pc, 'GUI:Language1\n"Türkçe"\nEND\n\nTOOLTIP:Language\n', "Patch.str language")
    result["checks"]["patch_language_anchor"] = "PASS"

    gl = (code/"GameEngine/Source/GameClient/GlobalLanguage.cpp").read_text(encoding="utf-8-sig")
    need(gl, 'fname.format("Data\\\\%s\\\\Language.ini", GetRegistryLanguage().str());', "GlobalLanguage.cpp")
    result["checks"]["language_ini_anchor"] = "PASS"

    w3d = (code/"GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp").read_text(encoding="utf-8-sig")
    if w3d.count("GetRegistryLanguage().str()") != 2:
        raise RuntimeError("W3DFileSystem.cpp: esperado exatamente 2 lookups do idioma do registro")
    need(w3d, "Germany hates exploding people units", "W3D localized anchor")
    result["checks"]["w3d_localized_assets_anchor"] = "PASS"

    bink = (code/"GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp").read_text(encoding="utf-8-sig")
    if bink.count("GetRegistryLanguage().str()") != 1:
        raise RuntimeError("BinkVideoPlayer.cpp: esperado exatamente 1 lookup do idioma do registro")
    need(bink, '#define VIDEO_LANG_PATH_FORMAT "Data/%s/Movies/%s.%s"', "Bink path format definition")
    need(
        bink,
        "sprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );",
        "Bink localized lookup call"
    )
    result["checks"]["bink_localized_assets_anchor"] = "PASS"

    cmake = (code/"CMakeLists.txt").read_text(encoding="utf-8-sig")
    need(cmake, "${CMAKE_CURRENT_SOURCE_DIR}/Data/Turkish", "CMake Turkish copy")
    result["checks"]["cmake_locale_copy_anchor"] = "PASS"

    ascii_h = (code/"GameEngine/Include/Common/AsciiString.h").read_text(encoding="utf-8-sig")
    if "int compareNoCase(const char* s) const;" not in ascii_h:
        raise RuntimeError("AsciiString.h: compareNoCase(const char*) ausente")
    result["checks"]["ascii_compare_no_case_api"] = "PASS"

    debug_h = (code/"GameEngine/Include/Common/Debug.h").read_text(encoding="utf-8-sig")
    if "#define DEBUG_ASSERTLOG(c, m)" not in debug_h:
        raise RuntimeError("Debug.h: macro DEBUG_ASSERTLOG ausente")
    result["checks"]["debug_assertlog_api"] = "PASS"

    catalog = (code/"GameEngine/Source/Common/OptionsCatalog.cpp").read_text(encoding="utf-8-sig")
    if '"TextLanguage"' not in catalog or "TEXT_LANGUAGE_COUNT - 1" not in catalog:
        raise RuntimeError("OptionsCatalog.cpp: TextLanguage não acompanha TEXT_LANGUAGE_COUNT")
    result["checks"]["options_language_range"] = "PASS"

    result["status"] = "PASS"
    return result

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    result = validate(repo)
    print(json.dumps(result, ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
