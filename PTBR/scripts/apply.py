#!/usr/bin/env python3
from pathlib import Path
import argparse
import shutil

MARKER = "ReforgedPTBRLanguageInitialized"

OLD_TOOLTIP = (
    'TOOLTIP:Language\n'
    '"The language of menus, orders, briefings and subtitles. Voices and videos stay as installed. Takes effect the next time the game starts."\n'
    'END\n'
)
NEW_TOOLTIP = (
    'TOOLTIP:Language\n'
    '"The language of menus, orders, briefings, subtitles and localized videos. Voices stay as installed. Takes effect the next time the game starts."\n'
    'END\n'
)

def fail(msg):
    raise RuntimeError(msg)

def replace_once(path: Path, old: str, new: str):
    text=path.read_text(encoding="utf-8-sig")
    n=text.count(old)
    if n != 1:
        fail(f"{path}: expected exactly 1 source anchor, found {n}")
    path.write_text(text.replace(old,new,1),encoding="utf-8",newline="")

def validate_clean_source(repo: Path):
    code=repo/"GeneralsMD/Code"
    if not code.is_dir():
        fail("not a CnCGeneralsZH-Reforged checkout")

    h=(code/"GameEngine/Include/Common/GlobalData.h").read_text(encoding="utf-8-sig")
    if h.count("\tTEXT_LANGUAGE_ENGLISH\t= 0,\n\tTEXT_LANGUAGE_TURKISH\t= 1,\n\n\tTEXT_LANGUAGE_COUNT\t\t= 2,\n") != 1:
        fail("GlobalData.h language enum no longer matches the validated source")

    cpp=(code/"GameEngine/Source/Common/GlobalData.cpp").read_text(encoding="utf-8-sig")
    if cpp.count("\tm_textLanguage = TEXT_LANGUAGE_ENGLISH;\n") != 1:
        fail("GlobalData.cpp default language anchor changed")

    gt=(code/"GameEngine/Source/GameClient/GameText.cpp").read_text(encoding="utf-8-sig")
    overlay=(
        "static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =\n"
        "{\n\tNULL,\n\t\"Data\\\\Turkish\\\\Generals.str\",\n};\n"
    )
    if gt.count(overlay) != 1 or '"Data\\\\Patch.str"' not in gt:
        fail("GameText.cpp overlay architecture changed")

    opt=(code/"GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp").read_text(encoding="utf-8-sig")
    if opt.count('load("Options.ini");') != 1:
        fail("OptionsMenu.cpp preferences anchor changed")

    patch=(code/"Data/Patch.str").read_text(encoding="utf-8-sig")
    if patch.count('GUI:Language1\n"Türkçe"\nEND\n\nTOOLTIP:Language\n') != 1:
        fail("Patch.str language anchor changed")

    gl=(code/"GameEngine/Source/GameClient/GlobalLanguage.cpp").read_text(encoding="utf-8-sig")
    if gl.count('fname.format("Data\\\\%s\\\\Language.ini", GetRegistryLanguage().str());') != 1:
        fail("GlobalLanguage.cpp language path anchor changed")

    w3d=(code/"GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp").read_text(encoding="utf-8-sig")
    if w3d.count("GetRegistryLanguage().str()") != 2:
        fail("W3DFileSystem.cpp localized lookup anchors changed")

    bink=(code/"GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp").read_text(encoding="utf-8-sig")
    bink_call="sprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );"
    if bink.count(bink_call) != 1:
        fail("BinkVideoPlayer.cpp localized lookup anchor changed")

    cm=(code/"CMakeLists.txt").read_text(encoding="utf-8-sig")
    if cm.count("${CMAKE_CURRENT_SOURCE_DIR}/Data/Turkish") < 1:
        fail("CMake locale copy anchor changed")

def source_state(repo: Path):
    h=repo/"GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h"
    text=h.read_text(encoding="utf-8-sig")
    if "TEXT_LANGUAGE_PORTUGUESE_BRAZIL" in text:
        return "patched"
    return "clean"

def apply(repo: Path, package: Path):
    if source_state(repo) == "patched":
        return "already-patched"

    validate_clean_source(repo)
    code=repo/"GeneralsMD/Code"

    p=code/"GameEngine/Include/Common/GlobalData.h"
    replace_once(
        p,
        "\tTEXT_LANGUAGE_ENGLISH\t= 0,\n\tTEXT_LANGUAGE_TURKISH\t= 1,\n\n\tTEXT_LANGUAGE_COUNT\t\t= 2,\n",
        "\tTEXT_LANGUAGE_ENGLISH\t\t\t\t= 0,\n"
        "\tTEXT_LANGUAGE_TURKISH\t\t\t\t= 1,\n"
        "\tTEXT_LANGUAGE_PORTUGUESE_BRAZIL\t= 2,\n\n"
        "\tTEXT_LANGUAGE_COUNT\t\t\t\t\t= 3,\n"
    )
    replace_once(
        p,
        "\tTEXT_LANGUAGE_COUNT\t\t\t\t\t= 3,\n};\n",
        "\tTEXT_LANGUAGE_COUNT\t\t\t\t\t= 3,\n};\n\n"
        "// Selected text-language directory for localized loose assets.\n"
        "AsciiString GetTextLanguageDirectory( void );\n"
    )

    p=code/"GameEngine/Source/Common/GlobalData.cpp"
    replace_once(
        p,
        "GlobalData* TheWritableGlobalData = NULL;\t\t\t\t///< The global data singleton\n",
        "GlobalData* TheWritableGlobalData = NULL;\t\t\t\t///< The global data singleton\n\n"
        "AsciiString GetTextLanguageDirectory( void )\n"
        "{\n"
        "\tif (TheGlobalData)\n"
        "\t{\n"
        "\t\tswitch (TheGlobalData->m_textLanguage)\n"
        "\t\t{\n"
        "\t\t\tcase TEXT_LANGUAGE_TURKISH: return AsciiString(\"Turkish\");\n"
        "\t\t\tcase TEXT_LANGUAGE_PORTUGUESE_BRAZIL: return AsciiString(\"PortugueseBrazil\");\n"
        "\t\t\tdefault: break;\n"
        "\t\t}\n"
        "\t}\n"
        "\treturn GetRegistryLanguage();\n"
        "}\n"
    )
    replace_once(
        p,
        "\t// the words the game shipped with until somebody picks a translation\n"
        "\tm_textLanguage = TEXT_LANGUAGE_ENGLISH;\n",
        "\t// PT-BR edition: Portuguese (Brazil) is the first-run language.\n"
        "\tm_textLanguage = TEXT_LANGUAGE_PORTUGUESE_BRAZIL;\n"
    )

    p=code/"GameEngine/Source/GameClient/GameText.cpp"
    replace_once(
        p,
        "static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =\n"
        "{\n\tNULL,\n\t\"Data\\\\Turkish\\\\Generals.str\",\n};\n",
        "static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =\n"
        "{\n\tNULL,\n\t\"Data\\\\Turkish\\\\Generals.str\",\n\t\"Data\\\\PortugueseBrazil\\\\Generals.str\",\n};\n"
    )

    p=code/"GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp"
    old_ctor=(
        "OptionPreferences::OptionPreferences( void )\n{\n"
        "\t// note, the superclass will put this in the right dir automatically, this is just a leaf name\n"
        "\tload(\"Options.ini\");\n}\n"
    )
    new_ctor=(
        "OptionPreferences::OptionPreferences( void )\n{\n"
        "\t// note, the superclass will put this in the right dir automatically, this is just a leaf name\n"
        "\tload(\"Options.ini\");\n\n"
        "\t// PT-BR edition migration. Do this once, then leave future language changes alone.\n"
        f"\tif (find(AsciiString(\"{MARKER}\")) == end())\n"
        "\t{\n\t\t(*this)[\"TextLanguage\"] = \"2\";\n"
        f"\t\t(*this)[\"{MARKER}\"] = \"1\";\n"
        "\t\twrite();\n\t}\n}\n"
    )
    replace_once(p,old_ctor,new_ctor)

    p=code/"Data/Patch.str"
    replace_once(
        p,
        'GUI:Language1\n"Türkçe"\nEND\n\nTOOLTIP:Language\n',
        'GUI:Language1\n"Türkçe"\nEND\n\nGUI:Language2\n"Português (Brasil)"\nEND\n\nTOOLTIP:Language\n'
    )

    p=code/"GameEngine/Source/GameClient/GlobalLanguage.cpp"
    replace_once(
        p,
        '#include "Common/INI.h"\n#include "Common/Registry.h"\n',
        '#include "Common/INI.h"\n#include "Common/GlobalData.h"\n#include "Common/Registry.h"\n'
    )
    replace_once(
        p,
        "\tINI ini;\n\tAsciiString fname;\n"
        "\tfname.format(\"Data\\\\%s\\\\Language.ini\", GetRegistryLanguage().str());\n\n",
        "\tINI ini;\n"
        "\tAsciiString languageDir = GetTextLanguageDirectory();\n"
        "\tAsciiString fname;\n"
        "\tfname.format(\"Data\\\\%s\\\\Language.ini\", languageDir.str());\n"
        "\tif (!TheFileSystem->doesFileExist(fname.str()))\n"
        "\t{\n\t\tlanguageDir = GetRegistryLanguage();\n"
        "\t\tfname.format(\"Data\\\\%s\\\\Language.ini\", languageDir.str());\n\t}\n\n"
    )
    replace_once(
        p,
        'tempName.format("Data\\\\%s\\\\Language9x.ini", GetRegistryLanguage().str());',
        'tempName.format("Data\\\\%s\\\\Language9x.ini", languageDir.str());'
    )

    p=code/"GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp"
    text=p.read_text(encoding="utf-8-sig")
    if text.count("GetRegistryLanguage().str()") != 2:
        fail("W3DFileSystem.cpp source anchors changed")
    text=text.replace("GetRegistryLanguage().str()","GetTextLanguageDirectory().str()",2)
    needle=(
        "\t// We need to be able to grab w3d's from a localization dir, since Germany hates exploding people units.\n"
        "\tif( fileType == FILE_TYPE_W3D )\n"
    )
    repl=(
        "\tAsciiString localizedLanguage = GetTextLanguageDirectory();\n\n"
        "\t// We need to be able to grab w3d's from a localization dir, since Germany hates exploding people units.\n"
        "\tif( fileType == FILE_TYPE_W3D )\n"
    )
    if text.count(needle) != 1:
        fail("W3D localized insertion point changed")
    text=text.replace(needle,repl,1)
    if text.count("GetTextLanguageDirectory().str()") != 2:
        fail("W3D selected-language lookup count changed")
    text=text.replace("GetTextLanguageDirectory().str()","localizedLanguage.str()",2)
    old=(
        "\t// see if the file exists\n\tm_fileExists = TheFileSystem->doesFileExist( m_filePath );\n\n\n\n"
        "\t// Now try the main lookup of hitting local files and big files\n"
    )
    new=(
        "\t// see if the file exists\n\tm_fileExists = TheFileSystem->doesFileExist( m_filePath );\n\n"
        "\t// A translation may not carry every localized image/model. Before the generic Art path,\n\t// retry the installation language (normally English) so an English install remains complete.\n"
        "\tif( m_fileExists == FALSE && localizedLanguage.compareNoCase( GetRegistryLanguage().str() ) != 0 )\n"
        "\t{\n"
        "\t\tif( fileType == FILE_TYPE_W3D )\n"
        "\t\t{\n"
        "\t\t\tstatic const char *localizedPathFormat = \"Data/%s/Art/W3D/\";\n"
        "\t\t\tsnprintf(m_filePath, ARRAY_SIZE(m_filePath), localizedPathFormat, GetRegistryLanguage().str());\n"
        "\t\t\tstrlcat( m_filePath, filename, ARRAY_SIZE(m_filePath) );\n"
        "\t\t}\n"
        "\t\telse if( isImageFileType(fileType) )\n"
        "\t\t{\n"
        "\t\t\tstatic const char *localizedPathFormat = \"Data/%s/Art/Textures/\";\n"
        "\t\t\tsnprintf(m_filePath, ARRAY_SIZE(m_filePath), localizedPathFormat, GetRegistryLanguage().str());\n"
        "\t\t\tstrlcat( m_filePath, filename, ARRAY_SIZE(m_filePath) );\n"
        "\t\t}\n"
        "\t\tm_fileExists = TheFileSystem->doesFileExist( m_filePath );\n\t}\n\n"
        "\t// Now try the main lookup of hitting local files and big files\n"
    )
    if text.count(old) != 1:
        fail("W3D fallback anchor changed")
    p.write_text(text.replace(old,new,1),encoding="utf-8",newline="")

    p=code/"GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp"
    replace_once(
        p,
        "sprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );",
        "sprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetTextLanguageDirectory().str(), pVideo->m_filename.str(), VIDEO_EXT );"
    )
    old=(
        "\t\tchar localizedFilePath[ _MAX_PATH ];\n"
        "\t\tsprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetTextLanguageDirectory().str(), pVideo->m_filename.str(), VIDEO_EXT );\n"
        "\t\tHBINK handle = BinkOpen(localizedFilePath , BINKPRELOADALL );\n"
        "\t\tDEBUG_ASSERTLOG(!handle, (\"opened localized bink file %s\\n\", localizedFilePath));\n"
        "\t\tif (!handle)\n"
        "\t\t{\n"
        "\t\t\tchar filePath[ _MAX_PATH ];\n"
        "\t\t\tsnprintf( filePath, ARRAY_SIZE(filePath), \"%s\\\\%s.%s\", VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );\n"
        "\t\t\thandle = BinkOpen(filePath , BINKPRELOADALL );\n"
        "\t\t\tDEBUG_ASSERTLOG(!handle, (\"opened bink file %s\\n\", localizedFilePath));\n"
        "\t\t}\n"
    )
    new=(
        "\t\tchar localizedFilePath[ _MAX_PATH ];\n"
        "\t\tAsciiString localizedLanguage = GetTextLanguageDirectory();\n"
        "\t\tsprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, localizedLanguage.str(), pVideo->m_filename.str(), VIDEO_EXT );\n"
        "\t\tHBINK handle = BinkOpen(localizedFilePath , BINKPRELOADALL );\n"
        "\t\tDEBUG_ASSERTLOG(!handle, (\"opened localized bink file %s\\n\", localizedFilePath));\n\n"
        "\t\tif (!handle && localizedLanguage.compareNoCase( GetRegistryLanguage().str() ) != 0)\n"
        "\t\t{\n"
        "\t\t\tsprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );\n"
        "\t\t\thandle = BinkOpen(localizedFilePath , BINKPRELOADALL );\n"
        "\t\t}\n\n"
        "\t\tif (!handle)\n"
        "\t\t{\n"
        "\t\t\tchar filePath[ _MAX_PATH ];\n"
        "\t\t\tsnprintf( filePath, ARRAY_SIZE(filePath), \"%s\\\\%s.%s\", VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );\n"
        "\t\t\thandle = BinkOpen(filePath , BINKPRELOADALL );\n"
        "\t\t\tDEBUG_ASSERTLOG(!handle, (\"opened bink file %s\\n\", filePath));\n"
        "\t\t}\n"
    )
    replace_once(p,old,new)

    # Barracks exit recovery. The original queue exit code trusts UnitCreatePoint and
    # NaturalRallyPoint even when a nearby wall/structure occupies those cells.
    # Recover only infantry-production barracks, leaving other factories unchanged.
    p=code/"GameEngine/Source/GameLogic/Object/Update/ProductionExitUpdate/QueueProductionExitUpdate.cpp"
    old_exit=(
        "\t\tnewObj->setPosition( &createPoint );\n"
        "\t\tnewObj->setOrientation( exitAngle );\n\n"
        "\t\t//\n"
        "\t\t// Objects that are created in the air from producers that are in the air get \n"
    )
    new_exit=(
        "\t\tnewObj->setPosition( &createPoint );\n"
        "\t\tnewObj->setOrientation( exitAngle );\n\n"
        "\t\tAIUpdateInterface *ai = newObj->getAIUpdateInterface();\n"
        "\t\tif( !creationInAir && ai && ai->isDoingGroundMovement() && creationObject->isKindOf( KINDOF_FS_BARRACKS ) )\n"
        "\t\t{\n"
        "\t\t\tCoord3D adjustedCreatePoint = createPoint;\n"
        "\t\t\tif( TheAI->pathfinder()->adjustToPossibleDestination( newObj, ai->getLocomotorSet(), &adjustedCreatePoint ) )\n"
        "\t\t\t{\n"
        "\t\t\t\tcreatePoint = adjustedCreatePoint;\n"
        "\t\t\t\tnewObj->setPosition( &createPoint );\n"
        "\t\t\t}\n"
        "\t\t}\n\n"
        "\t\t//\n"
        "\t\t// Objects that are created in the air from producers that are in the air get \n"
    )
    replace_once(p,old_exit,new_exit)

    old_path=(
        "\t\tTheAI->pathfinder()->addObjectToPathfindMap( newObj );\n"
        "\t\tCoord3D tmp;\n"
        "\t\tgetNaturalRallyPoint(tmp);\n"
        "\t\t// Grid it.\n"
        "\t\tTheAI->pathfinder()->snapPosition(newObj, &tmp);\n"
        "\t\tstd::vector<Coord3D> exitPath;\n"
        "\t\texitPath.push_back(tmp);\n\n"
        "\t\tAIUpdateInterface  *ai = newObj->getAIUpdateInterface();\n"
    )
    new_path=(
        "\t\tTheAI->pathfinder()->addObjectToPathfindMap( newObj );\n"
        "\t\tCoord3D tmp;\n"
        "\t\tgetNaturalRallyPoint(tmp);\n"
        "\t\t// Grid it.\n"
        "\t\tTheAI->pathfinder()->snapPosition(newObj, &tmp);\n"
        "\t\tif( ai && ai->isDoingGroundMovement() && creationObject->isKindOf( KINDOF_FS_BARRACKS ) )\n"
        "\t\t{\n"
        "\t\t\tCoord3D adjustedExitPoint = tmp;\n"
        "\t\t\tif( TheAI->pathfinder()->adjustDestination( newObj, ai->getLocomotorSet(), &adjustedExitPoint ) )\n"
        "\t\t\t\ttmp = adjustedExitPoint;\n"
        "\t\t}\n"
        "\t\tstd::vector<Coord3D> exitPath;\n"
        "\t\texitPath.push_back(tmp);\n\n"
    )
    replace_once(p,old_path,new_path)

    # USA/GLA barracks use DefaultProductionExitUpdate. Their UnitCreatePoint is
    # deliberately the producer center, so keep that spawn behavior and recover
    # only a blocked natural exit point. Scope it to FS_BARRACKS.
    p=code/"GameEngine/Source/GameLogic/Object/Update/ProductionExitUpdate/DefaultProductionExitUpdate.cpp"
    old_default_path=(
        "\t\tTheAI->pathfinder()->addObjectToPathfindMap( newObj );\n"
        "\t\tCoord3D tmp;\n"
        "\t\tgetNaturalRallyPoint(tmp);\n"
        "\t\tstd::vector<Coord3D> exitPath;\n"
        "\t\texitPath.push_back(tmp);\n\n"
        "\t\tAIUpdateInterface  *ai = newObj->getAIUpdateInterface();\n"
    )
    new_default_path=(
        "\t\tTheAI->pathfinder()->addObjectToPathfindMap( newObj );\n"
        "\t\tCoord3D tmp;\n"
        "\t\tgetNaturalRallyPoint(tmp);\n"
        "\t\tAIUpdateInterface *ai = newObj->getAIUpdateInterface();\n"
        "\t\tif( ai && ai->isDoingGroundMovement() && creationObject->isKindOf( KINDOF_FS_BARRACKS ) )\n"
        "\t\t{\n"
        "\t\t\tCoord3D adjustedExitPoint = tmp;\n"
        "\t\t\tif( TheAI->pathfinder()->adjustDestination( newObj, ai->getLocomotorSet(), &adjustedExitPoint ) )\n"
        "\t\t\t\ttmp = adjustedExitPoint;\n"
        "\t\t}\n"
        "\t\tstd::vector<Coord3D> exitPath;\n"
        "\t\texitPath.push_back(tmp);\n\n"
    )
    replace_once(p,old_default_path,new_default_path)

    p=code/"CMakeLists.txt"
    replace_once(
        p,
        "  COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
        "          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Turkish\n"
        "          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/Turkish\n",
        "  COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
        "          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Turkish\n"
        "          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/Turkish\n"
        "  COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
        "          ${CMAKE_CURRENT_SOURCE_DIR}/Data/PortugueseBrazil\n"
        "          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/PortugueseBrazil\n"
    )

    patch=code/"Data/Patch.str"
    replace_once(patch,OLD_TOOLTIP,NEW_TOOLTIP)

    src=package/"Data/PortugueseBrazil"
    dst=code/"Data/PortugueseBrazil"
    shutil.copytree(src,dst,dirs_exist_ok=True)
    return "applied"

def main():
    ap=argparse.ArgumentParser(description="Apply the Brazilian Portuguese integration")
    ap.add_argument("repo",nargs="?",default=".")
    args=ap.parse_args()
    repo=Path(args.repo).resolve()
    package=Path(__file__).resolve().parents[1]
    status=apply(repo,package)
    print(f"PT-BR APPLY PASS ({status})")

if __name__=="__main__":
    main()
