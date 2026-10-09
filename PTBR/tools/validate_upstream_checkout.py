#!/usr/bin/env python3
from pathlib import Path
import json
import sys

EXPECTED_SNAPSHOT = {
    "head_commit": "9e8c16a9d71b3c474db0316f9bdd553b0fb83b4d",
    "files": {
        "GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h": "1648b675ce4fd5a6e3fd21c6f0992c3dcf4de1b7",
        "GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp": "28bbaf40b334f1e74dded8e8ca701ee9842c4db6",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GameText.cpp": "f719f81e6cb6692218643b53a73b74a2cd2900b4",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp": "bf71a99673c5a64d93d954bcd350902becb0c199",
        "GeneralsMD/Code/Data/Patch.str": "7f24144a9e2cbf221d548338c6e0747b87cacb6e",
        "GeneralsMD/Code/GameEngine/Source/GameClient/GlobalLanguage.cpp": "02e3fd0fc5c8da55e8b6097ebc25aad51fcd7673",
        "GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp": "61b3a14c2a46b3f297a1535f04b9f581d2d26269",
        "GeneralsMD/Code/GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp": "725e18cc231d4d75ab2df60231132fef95e4c05e",
        "GeneralsMD/Code/CMakeLists.txt": "11bdfdb0735a8e58dcd2ee77581ea3bf366fe5ce",
        "GeneralsMD/Code/GameEngine/Include/Common/AsciiString.h": "3b58ca6b46023845e48daa5d0afc4488f2a2e066",
        "GeneralsMD/Code/GameEngine/Include/Common/Debug.h": "c097a75346d7f14e1288b3d36636c9ada3ded623",
        "GeneralsMD/Code/GameEngine/Source/Common/OptionsCatalog.cpp": "883df15d19200d27e6042a0d8475979a7956ee18",
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
    need(h,
        "\tTEXT_LANGUAGE_ENGLISH\t= 0,\n\tTEXT_LANGUAGE_TURKISH\t= 1,\n\tTEXT_LANGUAGE_GERMAN\t= 2,\n\n\tTEXT_LANGUAGE_COUNT\t\t= 3,\n",
        "GlobalData.h enum")
    result["checks"]["language_enum_anchor"] = "PASS"

    cpp = (code/"GameEngine/Source/Common/GlobalData.cpp").read_text(encoding="utf-8-sig")
    need(cpp, "\tm_textLanguage = TEXT_LANGUAGE_ENGLISH;\n", "GlobalData.cpp default")
    result["checks"]["default_language_anchor"] = "PASS"

    gt = (code/"GameEngine/Source/GameClient/GameText.cpp").read_text(encoding="utf-8-sig")
    need(gt,
        "static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =\n{\n\tNULL,\n\t\"Data\\\\Turkish\\\\Generals.str\",\n\t\"Data\\\\German\\\\Generals.str\",\n};\n",
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
    need(pc, 'GUI:Language2\n"Deutsch"\nEND\n\nTOOLTIP:Language\n', "Patch.str language")
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

    command_line = (code/"GameEngine/Source/Common/CommandLine.cpp").read_text(encoding="utf-8-sig")
    need(command_line, '\tstatic const char *const TheTextLanguageNames[ TEXT_LANGUAGE_COUNT ] = { "english", "turkish", "german" };\n', "CommandLine.cpp -language names")
    need(command_line, "/* -language <english|turkish|german>: ", "CommandLine.cpp -language comment")
    result["checks"]["command_line_language_anchor"] = "PASS"

    # Stage 13: the corner box hidden by default, through upstream's own ShowNetBox switch.
    need(cpp, "\tm_showNetBox = TRUE;\n", "GlobalData.cpp net box default")
    unconditional(cpp, "\tm_showNetBox = TRUE;\n", "GlobalData.cpp net box default")
    if '{ "ShowNetBox",' not in catalog or "OPT_WND( \"CheckNetBox\" )" not in catalog:
        raise RuntimeError("OptionsCatalog.cpp: ShowNetBox sem a caixa de seleção CheckNetBox no menu")
    test = (code/"Tests/test_gameengine.cpp").read_text(encoding="utf-8-sig")
    need(test, "\tCHECK( scratch->m_showNetBox );\n\t// the older plate's own switch", "test_gameengine net box default check")
    result["checks"]["net_box_anchors"] = "PASS"

    # Stage 14: lobby AI difficulty names.
    info = (code/"GameEngine/Source/GameNetwork/GameInfo.cpp").read_text(encoding="utf-8-sig")
    need(info, "/** What a seat is called wherever one is listed: the lobby's drop-down, the seat itself, the game\n", "GameInfo.cpp SlotStateName comment")
    need(info, "\t\tcase SLOT_EASY_AI:\t\t\treturn UnicodeString( u\"Easy AI\" );\n", "GameInfo.cpp Easy AI")
    need(info, "\t\tcase SLOT_MED_AI:\t\t\t\treturn UnicodeString( u\"Medium AI\" );\n", "GameInfo.cpp Medium AI")
    need(info, "\t\tcase SLOT_BRUTAL_AI:\t\treturn UnicodeString( u\"Hard AI\" );\n", "GameInfo.cpp Hard AI")
    result["checks"]["ai_rung_name_anchors"] = "PASS"

    # Stage 16: the Classic interface is upstream's now; check it still does what the edition had
    # done for itself up to 2.4, and anchor the one thing it still adds, the button markings.
    unconditional(cpp, "\tm_interfaceStyle = INTERFACE_STYLE_CLASSIC;\n", "GlobalData.cpp Classic is the interface without -interface")
    ui = (code/"GameEngine/Source/GameClient/InGameUI.cpp").read_text(encoding="utf-8-sig")
    need(ui, "\t// the Classic interface is EA's, and every page here has EA's windows, or nothing, to fall back on\n\tif( TheGlobalData->isClassicUI() )\n\t\treturn;\n", "InGameUI.cpp Classic skips every HTML page")
    quit_menu = (code/"GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/QuitMenu.cpp").read_text(encoding="utf-8-sig")
    need(quit_menu, 'TheTransitionHandler->reverse( quitMenuLayout == noSaveLoadQuitMenuLayout ? "QuitNoSaveBack" : "QuitFullBack" );', "QuitMenu.cpp EA's closing transitions")
    radar = (code/"GameEngine/Source/Common/System/Radar.cpp").read_text(encoding="utf-8-sig")
    need(radar, "TheControlBar->triggerRadarAttackGlow();", "Radar.cpp Classic under-attack blink")
    button = (code/"GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DPushButton.cpp").read_text(encoding="utf-8-sig")
    need(button, "#include \"Common/GlobalData.h\"\n", "W3DPushButton.cpp GlobalData include")
    need(button, "extern Real ControlBarHudScale( void );\n", "W3DPushButton.cpp HUD scale declaration")
    from apply_stage16 import BADGE_SCALE_USES
    need(button, "designPoints * ControlBarHudScale()", "W3DPushButton.cpp markings measured by the bar's scale", BADGE_SCALE_USES)
    result["checks"]["classic_interface_anchors"] = "PASS"

    # Stage 17: generals.exe starts without upstream's launcher.
    from apply_stage17 import LAUNCHER_CHECK
    winmain = (code/"Main/WinMain.cpp").read_text(encoding="utf-8-sig")
    need(winmain, LAUNCHER_CHECK, "WinMain.cpp launcher start check")
    result["checks"]["launcher_check_anchor"] = "PASS"

    result["status"] = "PASS"
    return result

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    result = validate(repo)
    print(json.dumps(result, ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
