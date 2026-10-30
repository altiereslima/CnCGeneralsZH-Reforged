#!/usr/bin/env python3
from pathlib import Path
import json
import sys

EXPECTED_SNAPSHOT = {
    "head_commit": "60069824716e17c3a2efbcf643b472095f3282ff",
    "files": {
        "GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h": "8ac70232e004e776cb6d725a87ffd09739edac9f",
        "GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp": "81c281a187d74762b1b51499292473afcb139972",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GameText.cpp": "01694f14b3ee94f66425fdde6d50a79670ba59b4",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp": "254c9265474c609e36b9327e67bbd8108665a31b",
        "GeneralsMD/Code/Data/Patch.str": "283e17ffb993b7ffd40af8a7ddbddd9794f079ea",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GlobalLanguage.cpp": "1af8f15c265e1204b74b087bab3f9d16d9821224",
        "GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp": "61b3a14c2a46b3f297a1535f04b9f581d2d26269",
        "GeneralsMD/Code/GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp": "717e59dce543150596028264a1950af357b76ed7",
        "GeneralsMD/Code/CMakeLists.txt": "551826db4ccd445b86f90e4b7b4b98b720d3e131",
        "GeneralsMD/Code/GameEngine/Include/Common/AsciiString.h": "3b58ca6b46023845e48daa5d0afc4488f2a2e066",
        "GeneralsMD/Code/GameEngine/Include/Common/Debug.h": "f912431471a640658fdd8309ac9b87bf22f15b83",
        "GeneralsMD/Code/GameEngine/Source/Common/OptionsCatalog.cpp": "b21b0c76488f1ba233753110832b10182acf21f4",
    }
}

def need(text, needle, name, count=1):
    found = text.count(needle)
    if found != count:
        raise RuntimeError(f"{name}: esperado {count}, encontrado {found}")
    return found

def unconditional(text, needle, name):
    """O needle tem de estar fora de qualquer #if: uma inicialização dentro de um bloco de build de
    desenvolvedor fica sem valor no Release, e a contagem de âncoras não percebe."""
    pos = text.find(needle)
    if pos < 0:
        raise RuntimeError(f"{name}: ausente")
    depth = 0
    for line in text[:pos].splitlines():
        s = line.strip()
        if s.startswith(("#if", "#ifdef", "#ifndef")):
            depth += 1
        elif s.startswith("#endif"):
            depth -= 1
    if depth != 0:
        raise RuntimeError(f"{name}: dentro de um bloco #if")

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
    from apply_stage2 import LANGUAGE_ENUM
    need(h, LANGUAGE_ENUM, "GlobalData.h enum")
    result["checks"]["language_enum_anchor"] = "PASS"

    cpp = (code/"GameEngine/Source/Common/GlobalData.cpp").read_text(encoding="utf-8-sig")
    need(cpp, "\tm_textLanguage = TEXT_LANGUAGE_ENGLISH;\n", "GlobalData.cpp default")
    result["checks"]["default_language_anchor"] = "PASS"

    gt = (code/"GameEngine/Source/GameClient/GameText.cpp").read_text(encoding="utf-8-sig")
    need(gt,
        "static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =\n{\n\tNULL,\n\t\"Data\\\\Turkish\\\\Generals.str\",\n\t\"Data\\\\German\\\\Generals.str\",\n"
        "\t\"Data\\\\Ukrainian\\\\Generals.str\",\n\t\"Data\\\\Russian\\\\Generals.str\",\n\t\"Data\\\\French\\\\Generals.str\",\n};\n",
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
    need(pc, 'GUI:Language5\n"Français"\nEND\n\nTOOLTIP:Language\n', "Patch.str language")
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

    command_line = (code/"GameEngine/Source/Common/CommandLine.cpp").read_text(encoding="utf-8-sig")
    need(command_line, '\tstatic const char *const TheTextLanguageNames[ TEXT_LANGUAGE_COUNT ] = { "english", "turkish", "german", "ukrainian", "russian", "french" };\n', "CommandLine.cpp -language names")
    need(command_line, "/* -language <english|turkish|german|ukrainian|russian|french>: ", "CommandLine.cpp -language comment")
    result["checks"]["command_line_language_anchor"] = "PASS"

    # Stage 13: the corner box hidden by default, through upstream's own ShowNetBox switch.
    need(cpp, "\tm_showNetBox = TRUE;\n", "GlobalData.cpp net box default")
    unconditional(cpp, "\tm_showNetBox = TRUE;\n", "GlobalData.cpp net box default")
    if '{ "ShowNetBox",' not in catalog or "OPT_WND( \"CheckNetBox\" )" not in catalog:
        raise RuntimeError("OptionsCatalog.cpp: ShowNetBox sem a caixa de seleção CheckNetBox no menu")
    test = (code/"Tests/test_gameengine.cpp").read_text(encoding="utf-8-sig")
    from apply_stage13 import NETBOX_TEST
    need(test, NETBOX_TEST, "test_gameengine net box default check")
    result["checks"]["net_box_anchors"] = "PASS"

    # Stage 2, step 12: Data\PortugueseBrazil joins the trees a -mod never replaces.
    from apply_stage2 import FORK_OWNED_LANGUAGES, TEST_UNDER_DATA, CHAT_REQUESTS
    need((code/"GameEngine/Source/GameClient/GUI/GUICallbacks/InGameChat.cpp").read_text(encoding="utf-8-sig"), CHAT_REQUESTS, "InGameChat.cpp chat requests to the computer allies")
    fs = (code/"GameEngine/Source/Common/System/FileSystem.cpp").read_text(encoding="utf-8-sig")
    need(fs, FORK_OWNED_LANGUAGES, "FileSystem.cpp fork-owned language trees")
    need((code/"Tests/test_gameengine.cpp").read_text(encoding="utf-8-sig"), TEST_UNDER_DATA, "test_gameengine shipped masters under Data")
    result["checks"]["fork_owned_anchors"] = "PASS"

    # Stage 14: lobby AI difficulty names.
    info = (code/"GameEngine/Source/GameNetwork/GameInfo.cpp").read_text(encoding="utf-8-sig")
    need(info, "/** What a seat is called wherever one is listed: the lobby's drop-down, the seat itself, the game\n", "GameInfo.cpp SlotStateName comment")
    need(info, "\t\tcase SLOT_EASY_AI:\t\t\treturn UnicodeString( u\"Easy AI\" );\n", "GameInfo.cpp Easy AI")
    need(info, "\t\tcase SLOT_MED_AI:\t\t\t\treturn UnicodeString( u\"Medium AI\" );\n", "GameInfo.cpp Medium AI")
    need(info, "\t\tcase SLOT_BRUTAL_AI:\t\treturn UnicodeString( u\"Hard AI\" );\n", "GameInfo.cpp Hard AI")
    result["checks"]["ai_rung_name_anchors"] = "PASS"

    # The edition's classic look. Up to 2.4 the edition built it itself, in 2.5 and 2.6 it was
    # upstream's Classic interface plus stage 16's button markings, and since 3.0 upstream has one
    # interface only, laid out as Zero Hour shipped, with no HTML pages. Check it stays that way: an
    # interface choice coming back would need the edition to pick the classic one again.
    header = (code/"GameEngine/Include/Common/GlobalData.h").read_text(encoding="utf-8-sig")
    if "INTERFACE_STYLE" in header or "isClassicUI" in header:
        raise RuntimeError("GlobalData.h: o upstream voltou a ter escolha de interface; a edição precisa escolher a clássica de novo")
    if (code/"GameEngine/Include/GameClient/HtmlOverlay.h").exists():
        raise RuntimeError("HtmlOverlay.h: as páginas HTML voltaram ao upstream")
    # and 1.04's mouse when generals.exe starts without the launcher's -mouse
    unconditional(cpp, "\tm_mouseStyle = MOUSE_STYLE_CLASSIC;\n", "GlobalData.cpp Classic mouse without -mouse")
    quit_menu = (code/"GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/QuitMenu.cpp").read_text(encoding="utf-8-sig")
    need(quit_menu, 'TheTransitionHandler->reverse( quitMenuLayout == noSaveLoadQuitMenuLayout ? "QuitNoSaveBack" : "QuitFullBack" );', "QuitMenu.cpp EA's closing transitions")
    radar = (code/"GameEngine/Source/Common/System/Radar.cpp").read_text(encoding="utf-8-sig")
    need(radar, "TheControlBar->triggerRadarAttackGlow();", "Radar.cpp EA's under-attack blink")
    result["checks"]["classic_interface_anchors"] = "PASS"

    # Stage 17: generals.exe starts without upstream's launcher.
    from apply_stage17 import LAUNCHER_CHECK
    winmain = (code/"Main/WinMain.cpp").read_text(encoding="utf-8-sig")
    need(winmain, LAUNCHER_CHECK, "WinMain.cpp launcher start check")
    result["checks"]["launcher_check_anchor"] = "PASS"

    # Stage 18: the splash at its own size, and kept up in borderless and windowed.
    from apply_stage18 import ANCHORS
    for name, anchor in ANCHORS.items():
        need(winmain, anchor, f"WinMain.cpp splash: {name}")
    from apply_stage18 import CMAKE_SPLASH_COPY
    cmake = (code/"CMakeLists.txt").read_text(encoding="utf-8-sig")
    need(cmake, CMAKE_SPLASH_COPY, "CMakeLists.txt copies Install_Final.bmp next to the exe")
    result["checks"]["splash_anchors"] = "PASS"

    # Stage 19: the plates' command bar in place of ControlBar Pro.
    import apply_stage19
    for name, anchors in apply_stage19.ANCHORS.items():
        src = (code/apply_stage19.SOURCES[name]).read_text(encoding="utf-8-sig")
        for i, anchor in enumerate(anchors):
            need(src, anchor, f"{name} plates anchor {i + 1}")
    manifest = (code/"BuildFingerprint.manifest").read_text(encoding="utf-8").split("\n")
    for rel in apply_stage19.CONTROL_BAR_PRO_FILES:
        if not (code/rel).is_file():
            raise RuntimeError(f"{rel}: ausente, o upstream mudou o ControlBar Pro")
        need(manifest, rel, f"BuildFingerprint.manifest {rel}")
    for plate in ("America", "China", "GLA"):
        for piece in ("Left", "Center", "Right"):
            if not (code/f"Data/Art/Textures/ReforgedBar{plate}{piece}.tga").is_file():
                raise RuntimeError(f"ReforgedBar{plate}{piece}.tga: a placa saiu do upstream")
    result["checks"]["plates_bar_anchors"] = "PASS"

    # Stage 20: the Options menu's version label names the game.
    from apply_stage20 import VERSION_LABEL
    need(opt, VERSION_LABEL, "OptionsMenu.cpp version label")
    result["checks"]["version_label_anchor"] = "PASS"

    result["status"] = "PASS"
    return result

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    result = validate(repo)
    print(json.dumps(result, ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
