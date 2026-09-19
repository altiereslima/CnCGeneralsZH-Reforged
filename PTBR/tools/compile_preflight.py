#!/usr/bin/env python3
from pathlib import Path
import json, sys

def need(text, needle, label):
    if needle not in text:
        raise RuntimeError(f"{label}: ausente: {needle}")

def main():
    repo=Path(sys.argv[1]).resolve()
    code=repo/'GeneralsMD/Code'
    out={'status':'PASS','checks':{}}

    # GlobalLanguage now directly declares the helper it calls.
    gl=(code/'GameEngine/Source/GameClient/GlobalLanguage.cpp').read_text(encoding='utf-8-sig')
    need(gl,'#include "Common/GlobalData.h"','GlobalLanguage direct include')
    need(gl,'AsciiString languageDir = GetTextLanguageDirectory();','GlobalLanguage helper call')
    need(gl,'TheFileSystem->doesFileExist','GlobalLanguage filesystem API')
    out['checks']['global_language_direct_declaration']='PASS'

    # GlobalData implementation has direct Registry declaration and helper definition.
    gd=(code/'GameEngine/Source/Common/GlobalData.cpp').read_text(encoding='utf-8-sig')
    if '#include "Common/registry.h"' not in gd and '#include "Common/Registry.h"' not in gd:
        raise RuntimeError('GlobalData.cpp: Registry include ausente')
    need(gd,'AsciiString GetTextLanguageDirectory( void )','language resolver definition')
    need(gd,'return GetRegistryLanguage();','registry fallback')
    out['checks']['globaldata_registry_contract']='PASS'

    # W3D and Bink both directly include GlobalData and Registry in current upstream.
    w3d=(code/'GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp').read_text(encoding='utf-8-sig')
    need(w3d,'#include "Common/GlobalData.h"','W3D GlobalData include')
    need(w3d,'#include "Common/Registry.h"','W3D Registry include')
    need(w3d,'localizedLanguage.compareNoCase( GetRegistryLanguage().str() )','W3D fallback comparison')
    out['checks']['w3d_include_contracts']='PASS'

    bink=(code/'GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp').read_text(encoding='utf-8-sig')
    need(bink,'#include "Common/GlobalData.h"','Bink GlobalData include')
    need(bink,'#include "Common/Registry.h"','Bink Registry include')
    need(bink,'localizedLanguage.compareNoCase( GetRegistryLanguage().str() )','Bink fallback comparison')
    out['checks']['bink_include_contracts']='PASS'

    # Options migration uses public UserPreferences APIs and PreferenceMap operations.
    opt=(code/'GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp').read_text(encoding='utf-8-sig')
    need(opt,'#include "Common/UserPreferences.h"','Options UserPreferences include')
    need(opt,'find(AsciiString("ReforgedPTBRLanguageInitialized"))','Options marker lookup')
    need(opt,'write();','Options persistence')
    out['checks']['options_migration_source_contract']='PASS'

    # Language selector is range-bound to enum count.
    catalog=(code/'GameEngine/Source/Common/OptionsCatalog.cpp').read_text(encoding='utf-8-sig')
    need(catalog,'TEXT_LANGUAGE_COUNT - 1','Options language enum range')
    out['checks']['language_range_contract']='PASS'

    # Basic structural sanity: all added localized routing symbols appear exactly where expected.
    h=(code/'GameEngine/Include/Common/GlobalData.h').read_text(encoding='utf-8-sig')
    if h.count('TEXT_LANGUAGE_PORTUGUESE_BRAZIL') != 1:
        raise RuntimeError('GlobalData.h: PTBR enum count inesperado')
    if h.count('GetTextLanguageDirectory') != 1:
        raise RuntimeError('GlobalData.h: resolver declaration count inesperado')
    out['checks']['globaldata_declaration_counts']='PASS'

    print(json.dumps(out,ensure_ascii=False,indent=2))

if __name__=='__main__': main()
