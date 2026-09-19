#!/usr/bin/env python3
from pathlib import Path
import argparse, json

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
    need(h,"TEXT_LANGUAGE_COUNT\t\t\t\t\t= 3","count 3")
    need(h,"AsciiString GetTextLanguageDirectory( void );","resolver declaration")
    checks["globaldata_h"]="PASS"

    cpp=(code/"GameEngine/Source/Common/GlobalData.cpp").read_text(encoding="utf-8")
    need(cpp,'case TEXT_LANGUAGE_PORTUGUESE_BRAZIL: return AsciiString("PortugueseBrazil");',"resolver mapping")
    need(cpp,"m_textLanguage = TEXT_LANGUAGE_PORTUGUESE_BRAZIL;","default PTBR")
    checks["globaldata_cpp"]="PASS"

    gt=(code/"GameEngine/Source/GameClient/GameText.cpp").read_text(encoding="utf-8")
    need(gt,'"Data\\\\PortugueseBrazil\\\\Generals.str"',"PTBR overlay")
    checks["game_text_overlay"]="PASS"

    opt=(code/"GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp").read_text(encoding="utf-8")
    need(opt,"ReforgedPTBRLanguageInitialized","one-shot migration")
    need(opt,'(*this)["TextLanguage"] = "2";',"PTBR preference")
    checks["options_migration"]="PASS"

    patch=(code/"Data/Patch.str").read_text(encoding="utf-8")
    need(patch,'GUI:Language2\n"Português (Brasil)"\nEND',"Portuguese UI choice")
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
