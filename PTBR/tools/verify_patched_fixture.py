#!/usr/bin/env python3
from pathlib import Path
import argparse, json, sys
from validate_upstream_checkout import unconditional

CORE_LOCALE_FILES=["Generals.str","Language.ini"]
MEDIA_LOCALE_FILES=[
    "Movies/EA_LOGO.BIK","Movies/EA_LOGO640.BIK",
    "Movies/sizzle_review.bik","Movies/sizzle_review640.bik",
    "Art/Textures/defeated.dds","Art/Textures/gameover.dds",
    "Art/Textures/GameOver.tga","Art/Textures/victorious.dds",
]

def need(text,needle,name):
    if needle not in text:
        raise RuntimeError(f"missing postcondition: {name}")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("repo")
    ap.add_argument("--allow-missing-media",action="store_true")
    args=ap.parse_args()
    repo=Path(args.repo).resolve()
    code=repo/"GeneralsMD/Code"
    checks={}

    h=(code/"GameEngine/Include/Common/GlobalData.h").read_text(encoding="utf-8")
    need(h,"TEXT_LANGUAGE_PORTUGUESE_BRAZIL","PTBR enum")
    need(h,"TEXT_LANGUAGE_COUNT\t\t\t\t\t= 4","count 4")
    need(h,"AsciiString GetTextLanguageDirectory( void );","resolver declaration")
    checks["globaldata_h"]="PASS"

    cpp=(code/"GameEngine/Source/Common/GlobalData.cpp").read_text(encoding="utf-8")
    need(cpp,'case TEXT_LANGUAGE_PORTUGUESE_BRAZIL: return AsciiString("PortugueseBrazil");',"resolver mapping")
    need(cpp,"m_textLanguage = TEXT_LANGUAGE_PORTUGUESE_BRAZIL;","default PTBR")
    checks["globaldata_cpp"]="PASS"

    # every table with a slot per text language has one entry per language, PT-BR's included: a slot
    # left out is a null, and -language compared an unknown name against it
    import re
    tables=re.compile(r"\[\s*TEXT_LANGUAGE_COUNT\s*\]\s*=\s*\{([^}]*)\}")
    for src in sorted((code/"GameEngine").rglob("*.cpp")):
        for body in tables.findall(src.read_text(encoding="utf-8",errors="replace")):
            entries=[e for e in (x.strip() for x in re.sub(r"//[^\n]*","",body).split(",")) if e]
            if len(entries)!=4:
                raise RuntimeError(f"{src.name}: a TEXT_LANGUAGE_COUNT table has {len(entries)} entries, not 4")
    command_line=(code/"GameEngine/Source/Common/CommandLine.cpp").read_text(encoding="utf-8")
    need(command_line,'"german", "portuguese" };',"-language knows portuguese")
    checks["language_tables"]="PASS"

    gt=(code/"GameEngine/Source/GameClient/GameText.cpp").read_text(encoding="utf-8")
    need(gt,'"Data\\\\PortugueseBrazil\\\\Generals.str"',"PTBR overlay")
    checks["game_text_overlay"]="PASS"

    opt=(code/"GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp").read_text(encoding="utf-8")
    need(opt,"ReforgedPTBRLanguageInitialized","one-shot migration")
    need(opt,'(*this)["TextLanguage"] = "3";',"PTBR preference")
    need(opt,'else if (migrated->second == "1")',"PT-BR 2 from before German carried over to 3")
    checks["options_migration"]="PASS"

    patch=(code/"Data/Patch.str").read_text(encoding="utf-8")
    need(patch,'GUI:Language3\n"Português (Brasil)"\nEND',"Portuguese UI choice")
    need(patch,"briefings, subtitles and localized videos","updated tooltip")
    checks["patch_str"]="PASS"

    gl=(code/"GameEngine/Source/GameClient/GlobalLanguage.cpp").read_text(encoding="utf-8")
    need(gl,'#include "Common/GlobalData.h"',"direct GlobalData include")
    need(gl,"AsciiString languageDir = GetTextLanguageDirectory();","selected Language.ini")
    need(gl,"languageDir = GetRegistryLanguage();","Language.ini fallback")
    checks["global_language"]="PASS"

    w3d=(code/"GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp").read_text(encoding="utf-8")
    need(w3d,"AsciiString localizedLanguage = GetTextLanguageDirectory();","selected art language")
    need(w3d,"localizedLanguage.compareNoCase( GetRegistryLanguage().str() ) != 0","registry art fallback")
    checks["localized_art_fallback"]="PASS"

    bink=(code/"GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp").read_text(encoding="utf-8")
    need(bink,"AsciiString localizedLanguage = GetTextLanguageDirectory();","selected Bink language")
    need(bink,"localizedLanguage.compareNoCase( GetRegistryLanguage().str() ) != 0","registry Bink fallback")
    need(bink,'"%s\\\\%s.%s", VIDEO_PATH',"generic movie fallback")
    checks["localized_bink_fallback"]="PASS"

    cm=(code/"CMakeLists.txt").read_text(encoding="utf-8")
    need(cm,"Data/PortugueseBrazil","PTBR CMake copy")
    checks["cmake_locale_copy"]="PASS"

    need(cpp,"m_showNetBox = FALSE;","corner box off by default")
    unconditional(cpp,"\tm_showNetBox = FALSE;\n","corner box default in every build")
    catalog=(code/"GameEngine/Source/Common/OptionsCatalog.cpp").read_text(encoding="utf-8")
    need(catalog,'{ "ShowNetBox",',"ShowNetBox option the player turns it back on with")
    if '{ "ShowHudOverlay",' in catalog:
        raise RuntimeError("ShowHudOverlay row back in the catalog: upstream reads an old 'no' through it")
    checks["net_box_hidden"]="PASS"

    info=(code/"GameEngine/Source/GameNetwork/GameInfo.cpp").read_text(encoding="utf-8")
    ptbr=(code/"Data/PortugueseBrazil/Generals.str").read_text(encoding="utf-8-sig")
    for label in ("GUI:SlotEasyAI","GUI:SlotMediumAI","GUI:SlotHardAI"):
        need(info,f'AIRungName( "{label}"',f"{label} lookup")
        need(ptbr,f"\n{label}\n",f"{label} translation")
    checks["ai_rung_names"]="PASS"

    # one interface since upstream 3.0, laid out as Zero Hour shipped; nothing of the edition's own
    if '{ "ClassicInterface",' in catalog:
        raise RuntimeError("the edition's own ClassicInterface key is back")
    checks["classic_interface"]="PASS"

    winmain=(code/"Main/WinMain.cpp").read_text(encoding="utf-8")
    if "Please start Zero Hour Reforged from its launcher." in winmain:
        raise RuntimeError("missing postcondition: generals.exe still refuses to start without the launcher")
    need(winmain,"PT-BR edition: started straight from generals.exe","launcher check removed")
    checks["starts_without_launcher"]="PASS"
    need(winmain,"::BitBlt(dc, 0, 0, splashWidth, splashHeight, tmpDC, 0, 0, SRCCOPY);","splash blit at its own size")
    need(winmain,"if (runWindowed && gLoadScreenBitmap == NULL) {","windowed 800x600 only without a splash")
    need(winmain,"if (!runWindowed && !ApplicationIsBorderless) {","borderless keeps painting the splash")
    need(winmain,"if (!ApplicationIsWindowed && !ApplicationIsBorderless && gLoadScreenBitmap != NULL) {","splash kept until WM_SIZE")
    if winmain.count("::DeleteObject(gLoadScreenBitmap);") != 3:
        raise RuntimeError("splash: esperado liberar a imagem em 3 lugares (WM_SIZE, tela cheia, saída)")
    splash=Path(__file__).resolve().parents[1]/"payload/GeneralsMD/Code/Data/Install_Final.bmp"
    if (code/"Data/Install_Final.bmp").read_bytes()!=splash.read_bytes():
        raise RuntimeError("missing postcondition: Data/Install_Final.bmp is not the edition's splash")
    checks["splash_own_size"]="PASS"

    fs=(code/"GameEngine/Source/Common/System/FileSystem.cpp").read_text(encoding="utf-8")
    need(fs,'starts("data\\\\german\\\\") || starts("data\\\\portuguesebrazil\\\\")',"Data\\PortugueseBrazil is fork-owned")
    test=(code/"Tests/test_gameengine.cpp").read_text(encoding="utf-8")
    need(test,'"Turkish/", "German/", "PortugueseBrazil/" };',"shipped-masters test keeps Data\\ in front of PortugueseBrazil")
    checks["fork_owned_locale"]="PASS"

    # the plates' command bar, ControlBar Pro left out
    bar=(code/"GameEngine/Source/GameClient/GUI/ControlBar/ControlBar.cpp").read_text(encoding="utf-8")
    scheme=(code/"GameEngine/Source/GameClient/GUI/ControlBar/ControlBarScheme.cpp").read_text(encoding="utf-8")
    need(bar,"*sx = *sy = loadScaleX < loadScaleY ? loadScaleX : loadScaleY;","panels at one scale")
    need(bar,'"LeftHUD", "WinUAttack", "BackgroundMarker"',"windows to panels by name")
    if "the plates are only the tests' to measure now" in bar:
        raise RuntimeError("missing postcondition: the plates are still switched off")
    need(scheme,"return m_currentScheme != NULL && m_currentScheme->m_controlBarPro;","EA's schemes leave the bar to the plates")
    if 'ControlBarProReforged.ini" ), INI_LOAD_OVERWRITE' in scheme:
        raise RuntimeError("missing postcondition: ControlBar Pro's schemes are still read")
    if "ControlBarProBlockInputMoney" in (code/"GameEngine/Source/GameClient/GUI/GUICallbacks/ReplayControls.cpp").read_text(encoding="utf-8"):
        raise RuntimeError("missing postcondition: the replay strip still looks for ControlBar Pro's money pane")
    sys.path.insert(0,str(Path(__file__).parent))
    import apply_stage19
    manifest=(code/"BuildFingerprint.manifest").read_text(encoding="utf-8").split("\n")
    left=[rel for rel in apply_stage19.CONTROL_BAR_PRO_FILES if (code/rel).exists() or rel in manifest]
    if left:
        raise RuntimeError("missing postcondition: ControlBar Pro still in the build: "+", ".join(left))
    checks["plates_command_bar"]="PASS"

    loc=code/"Data/PortugueseBrazil"
    miss_core=[x for x in CORE_LOCALE_FILES if not (loc/x).is_file()]
    if miss_core:
        raise RuntimeError("missing core locale payload: "+", ".join(miss_core))
    checks["core_locale_payload"]="PASS"

    present=[x for x in MEDIA_LOCALE_FILES if (loc/x).is_file()]
    if present and len(present)!=len(MEDIA_LOCALE_FILES):
        miss=[x for x in MEDIA_LOCALE_FILES if x not in present]
        raise RuntimeError("partial localized media payload: missing "+", ".join(miss))
    if len(present)==len(MEDIA_LOCALE_FILES):
        checks["localized_media_payload"]="PASS"
    elif args.allow_missing_media:
        checks["localized_media_payload"]="SKIPPED_OPTIONAL_NOT_PRESENT"
    else:
        raise RuntimeError("localized media payload is absent")

    print(json.dumps({"status":"PASS","checks":checks},ensure_ascii=False,indent=2))

if __name__=="__main__":
    main()
