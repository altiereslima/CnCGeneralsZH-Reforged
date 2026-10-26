#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

MARKER = "ReforgedPTBRLanguageInitialized"

# Âncoras do upstream que o passo 12 estende; o validador confere as duas.
FORK_OWNED_LANGUAGES = '\t\t|| starts("data\\\\turkish\\\\") || starts("data\\\\german\\\\")\n'
CHAT_REQUESTS = '\t\t{ u"money", AIREQ_MONEY }, { u"para", AIREQ_MONEY } };\n'
LANGUAGE_ENUM = (
    "\tTEXT_LANGUAGE_ENGLISH\t= 0,\n\tTEXT_LANGUAGE_TURKISH\t= 1,\n\tTEXT_LANGUAGE_GERMAN\t= 2,\n"
    "\tTEXT_LANGUAGE_UKRAINIAN\t= 3,\n\tTEXT_LANGUAGE_RUSSIAN\t= 4,\n\tTEXT_LANGUAGE_FRENCH\t= 5,\n\n"
    "\tTEXT_LANGUAGE_COUNT\t\t= 6,\n"
)
TEST_UNDER_DATA = '\t\tconst char *underData[] = { "INI/", "Scripts/", "Turkish/", "German/", "Ukrainian/", "Russian/", "French/" };\n'

def die(msg):
    raise SystemExit("STAGE02: " + msg)

def replace_once(path: Path, old: str, new: str):
    text = path.read_text(encoding="utf-8-sig")
    n = text.count(old)
    if n != 1:
        die(f"{path}: esperado 1 bloco, encontrado {n}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    pkg = Path(__file__).resolve().parents[1]
    code = repo / "GeneralsMD" / "Code"
    if not code.exists():
        die("aponte para a raiz de CnCGeneralsZH-Reforged")

    # 1) Language enum + shared localized-asset directory resolver.
    p = code / "GameEngine" / "Include" / "Common" / "GlobalData.h"
    replace_once(
        p,
        LANGUAGE_ENUM,
        LANGUAGE_ENUM.replace("\tTEXT_LANGUAGE_FRENCH\t= 5,\n\n\tTEXT_LANGUAGE_COUNT\t\t= 6,\n",
            "\tTEXT_LANGUAGE_FRENCH\t= 5,\n"
            "\tTEXT_LANGUAGE_PORTUGUESE_BRAZIL\t= 6,\t// PT-BR edition\n\n"
            "\tTEXT_LANGUAGE_COUNT\t\t= 7,\n")
    )
    replace_once(
        p,
        "\tTEXT_LANGUAGE_COUNT\t\t= 7,\n};\n",
        "\tTEXT_LANGUAGE_COUNT\t\t= 7,\n};\n\n"
        "// Selected text-language directory for localized loose assets.\n"
        "AsciiString GetTextLanguageDirectory( void );\n"
    )

    # 2) Resolver implementation + PT-BR first-run default.
    p = code / "GameEngine" / "Source" / "Common" / "GlobalData.cpp"
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
        "\t\t\tcase TEXT_LANGUAGE_GERMAN: return AsciiString(\"German\");\n"
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

    # 3) Register PT-BR text overlay.
    p = code / "GameEngine" / "Source" / "GameClient" / "GameText.cpp"
    replace_once(
        p,
        "static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =\n"
        "{\n"
        "\tNULL,\n"
        "\t\"Data\\\\Turkish\\\\Generals.str\",\n"
        "\t\"Data\\\\German\\\\Generals.str\",\n"
        "\t\"Data\\\\Ukrainian\\\\Generals.str\",\n"
        "\t\"Data\\\\Russian\\\\Generals.str\",\n"
        "\t\"Data\\\\French\\\\Generals.str\",\n"
        "};\n",
        "static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =\n"
        "{\n"
        "\tNULL,\n"
        "\t\"Data\\\\Turkish\\\\Generals.str\",\n"
        "\t\"Data\\\\German\\\\Generals.str\",\n"
        "\t\"Data\\\\Ukrainian\\\\Generals.str\",\n"
        "\t\"Data\\\\Russian\\\\Generals.str\",\n"
        "\t\"Data\\\\French\\\\Generals.str\",\n"
        "\t\"Data\\\\PortugueseBrazil\\\\Generals.str\",\n"
        "};\n"
    )

    # 4) Existing Reforged Options.ini migrates once to PT-BR, then user choice is respected.
    p = code / "GameEngine" / "Source" / "GameClient" / "GUI" / "GUICallbacks" / "Menus" / "OptionsMenu.cpp"
    old_ctor = (
        "OptionPreferences::OptionPreferences( void )\n"
        "{\n"
        "\t// note, the superclass will put this in the right dir automatically, this is just a leaf name\n"
        "\tload(\"Options.ini\");\n"
        "}\n"
    )
    new_ctor = (
        "OptionPreferences::OptionPreferences( void )\n"
        "{\n"
        "\t// note, the superclass will put this in the right dir automatically, this is just a leaf name\n"
        "\tload(\"Options.ini\");\n\n"
        "\t// PT-BR edition migration. Do this once, then leave future language changes alone.\n"
        "\t// Portuguese (Brazil) has moved as upstream added languages: it was 2 under marker 1, 3 under\n"
        "\t// marker 2 (upstream 2.6 put German at 2) and is 6 under marker 3 (upstream 3.4 put Ukrainian,\n"
        "\t// Russian and French at 3 to 5). A saved Portuguese is carried over; any other pick stays.\n"
        f"\tOptionPreferences::iterator migrated = find(AsciiString(\"{MARKER}\"));\n"
        "\tif (migrated == end())\n"
        "\t{\n"
        "\t\t(*this)[\"TextLanguage\"] = \"6\";\n"
        f"\t\t(*this)[\"{MARKER}\"] = \"3\";\n"
        "\t\twrite();\n"
        "\t}\n"
        "\telse if (migrated->second == \"1\" || migrated->second == \"2\")\n"
        "\t{\n"
        "\t\tif ((*this)[\"TextLanguage\"] == (migrated->second == \"1\" ? \"2\" : \"3\"))\n"
        "\t\t\t(*this)[\"TextLanguage\"] = \"6\";\n"
        f"\t\t(*this)[\"{MARKER}\"] = \"3\";\n"
        "\t\twrite();\n"
        "\t}\n"
        "}\n"
    )
    replace_once(p, old_ctor, new_ctor)

    # 5) Language name in the UI.
    p = code / "Data" / "Patch.str"
    replace_once(
        p,
        "GUI:Language5\n\"Français\"\nEND\n\nTOOLTIP:Language\n",
        "GUI:Language5\n\"Français\"\nEND\n\n"
        "GUI:Language6\n\"Português (Brasil)\"\nEND\n\n"
        "TOOLTIP:Language\n"
    )

    # 6) Language.ini follows TextLanguage, with install-language fallback.
    p = code / "GameEngine" / "Source" / "GameClient" / "GlobalLanguage.cpp"
    replace_once(
        p,
        '#include "Common/INI.h"\n#include "Common/Registry.h"\n',
        '#include "Common/INI.h"\n#include "Common/GlobalData.h"\n#include "Common/Registry.h"\n'
    )
    replace_once(
        p,
        "\tINI ini;\n"
        "\tAsciiString fname;\n"
        "\tfname.format(\"Data\\\\%s\\\\Language.ini\", GetRegistryLanguage().str());\n\n",
        "\tINI ini;\n"
        "\tAsciiString languageDir = GetTextLanguageDirectory();\n"
        "\tAsciiString fname;\n"
        "\tfname.format(\"Data\\\\%s\\\\Language.ini\", languageDir.str());\n"
        "\tif (!TheFileSystem->doesFileExist(fname.str()))\n"
        "\t{\n"
        "\t\tlanguageDir = GetRegistryLanguage();\n"
        "\t\tfname.format(\"Data\\\\%s\\\\Language.ini\", languageDir.str());\n"
        "\t}\n\n"
    )
    replace_once(
        p,
        "tempName.format(\"Data\\\\%s\\\\Language9x.ini\", GetRegistryLanguage().str());",
        "tempName.format(\"Data\\\\%s\\\\Language9x.ini\", languageDir.str());"
    )

    # 7) Localized textures/models follow TextLanguage first.
    p = code / "GameEngineDevice" / "Source" / "W3DDevice" / "GameClient" / "W3DFileSystem.cpp"
    text = p.read_text(encoding="utf-8-sig")
    old_lookup = "GetRegistryLanguage().str()"
    lookup_count = text.count(old_lookup)
    if lookup_count != 2:
        die(f"{p}: esperado exatamente 2 lookups de idioma do registro, encontrado {lookup_count}")
    text = text.replace(old_lookup, "GetTextLanguageDirectory().str()", 2)
    p.write_text(text, encoding="utf-8", newline="")

    # 8) Localized Bink movies follow TextLanguage first. The base game's own folder (the original
    #    Generals campaign movies) keeps the install's language: the edition ships nothing there.
    p = code / "GameEngineDevice" / "Source" / "VideoDevice" / "Bink" / "BinkVideoPlayer.cpp"
    old = "sprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );"
    new = "sprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetTextLanguageDirectory().str(), pVideo->m_filename.str(), VIDEO_EXT );"
    replace_once(p, old, new)

    # 9) Build copies whole PT-BR locale tree.
    p = code / "CMakeLists.txt"
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

    # 10) -language takes the edition's language by name too. The names table has TEXT_LANGUAGE_COUNT
    #     slots, and the slot the enum above adds was a null that the loop compared an unknown name
    #     against.
    p = code / "GameEngine" / "Source" / "Common" / "CommandLine.cpp"
    replace_once(
        p,
        "\tstatic const char *const TheTextLanguageNames[ TEXT_LANGUAGE_COUNT ] = { \"english\", \"turkish\", \"german\", \"ukrainian\", \"russian\", \"french\" };\n",
        "\tstatic const char *const TheTextLanguageNames[ TEXT_LANGUAGE_COUNT ] = { \"english\", \"turkish\", \"german\", \"ukrainian\", \"russian\", \"french\", \"portuguese\" };\n"
    )
    replace_once(
        p,
        "/* -language <english|turkish|german|ukrainian|russian|french>: ",
        "/* -language <english|turkish|german|ukrainian|russian|french|portuguese>: "
    )

    # 12) Data\PortugueseBrazil is the edition's, as Data\Turkish and Data\German are upstream's: a -mod
    #     never replaces the text, the videos or the screens there. Since 3.0 a test walks Code/Data,
    #     where stage 3 puts that tree, and fails on a file outside FileSystem::isForkOwnedPath.
    p = code / "GameEngine" / "Source" / "Common" / "System" / "FileSystem.cpp"
    replace_once(p, FORK_OWNED_LANGUAGES, FORK_OWNED_LANGUAGES.replace(
        'starts("data\\\\german\\\\")',
        'starts("data\\\\german\\\\") || starts("data\\\\portuguesebrazil\\\\")'))
    p = code / "Tests" / "test_gameengine.cpp"
    replace_once(p, TEST_UNDER_DATA, TEST_UNDER_DATA.replace('"French/" };', '"French/", "PortugueseBrazil/" };'))

    # 13) The chat words that ask the computer allies for help, an attack or money are English and
    #     Turkish upstream (help, attack, money; yardim, saldir, para); the loading tip names the
    #     Portuguese ones.
    p = code / "GameEngine" / "Source" / "GameClient" / "GUI" / "GUICallbacks" / "InGameChat.cpp"
    replace_once(p, CHAT_REQUESTS,
        '\t\t{ u"money", AIREQ_MONEY }, { u"para", AIREQ_MONEY },\n'
        '\t\t// PT-BR edition: the Portuguese words the loading tip (GUI:LoadTip8) names\n'
        '\t\t{ u"ajuda", AIREQ_HELP }, { u"atacar", AIREQ_ATTACK }, { u"ataque", AIREQ_ATTACK },\n'
        '\t\t{ u"dinheiro", AIREQ_MONEY } };\n')

    # 11) Seed locale file.
    src = pkg / "payload" / "GeneralsMD" / "Code" / "Data" / "PortugueseBrazil" / "Generals.str"
    dst = code / "Data" / "PortugueseBrazil" / "Generals.str"
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)

    print("STAGE02 PATCH PASS")
    print("Runtime locale: Data/PortugueseBrazil")
    print("Data/English remains untouched.")

if __name__ == "__main__":
    main()
