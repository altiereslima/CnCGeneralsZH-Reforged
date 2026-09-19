#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

def fail(msg):
    raise SystemExit("STAGE03: " + msg)

def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8-sig")
    n = text.count(old)
    if n != 1:
        fail(f"{path}: esperado 1 bloco, encontrado {n}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    pkg = Path(__file__).resolve().parents[1]
    code = repo / "GeneralsMD" / "Code"
    if not code.exists():
        fail("aponte para a raiz de CnCGeneralsZH-Reforged")

    # Apply Stage 02 from a clean upstream checkout.
    sys.path.insert(0, str(Path(__file__).parent))
    import apply_stage2
    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage2.main()
    finally:
        sys.argv = old_argv

    # Improve localized art fallback:
    # selected text language -> registry/install language -> generic Art directory.
    p = code / "GameEngineDevice" / "Source" / "W3DDevice" / "GameClient" / "W3DFileSystem.cpp"
    text = p.read_text(encoding="utf-8-sig")
    needle = (
        "\t// We need to be able to grab w3d's from a localization dir, since Germany hates exploding people units.\n"
        "\tif( fileType == FILE_TYPE_W3D )\n"
    )
    repl = (
        "\tAsciiString localizedLanguage = GetTextLanguageDirectory();\n\n"
        "\t// We need to be able to grab w3d's from a localization dir, since Germany hates exploding people units.\n"
        "\tif( fileType == FILE_TYPE_W3D )\n"
    )
    if text.count(needle) != 1:
        fail("não encontrei ponto de inserção do idioma em W3DFileSystem.cpp")
    text = text.replace(needle, repl, 1)

    selected_lookup = "GetTextLanguageDirectory().str()"
    selected_count = text.count(selected_lookup)
    if selected_count != 2:
        fail(f"W3DFileSystem.cpp: esperado exatamente 2 lookups do idioma selecionado após Stage 02, encontrado {selected_count}")
    text = text.replace(selected_lookup, "localizedLanguage.str()", 2)

    fallback_anchor = (
        "\t// see if the file exists\n"
        "\tm_fileExists = TheFileSystem->doesFileExist( m_filePath );\n\n\n\n"
        "\t// Now try the main lookup of hitting local files and big files\n"
    )
    fallback_block = (
        "\t// see if the file exists\n"
        "\tm_fileExists = TheFileSystem->doesFileExist( m_filePath );\n\n"
        "\t// A translation may not carry every localized image/model. Before the generic Art path,\n"
        "\t// retry the installation language (normally English) so an English install remains complete.\n"
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
        "\t\tm_fileExists = TheFileSystem->doesFileExist( m_filePath );\n"
        "\t}\n\n"
        "\t// Now try the main lookup of hitting local files and big files\n"
    )
    if text.count(fallback_anchor) != 1:
        fail("não encontrei fallback anchor em W3DFileSystem.cpp")
    p.write_text(text.replace(fallback_anchor, fallback_block, 1), encoding="utf-8", newline="")

    # Improve Bink fallback:
    # selected text language -> registry/install language -> Data/Movies.
    p = code / "GameEngineDevice" / "Source" / "VideoDevice" / "Bink" / "BinkVideoPlayer.cpp"
    old = (
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
    new = (
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
    replace_once(p, old, new)

    # Install the full Stage 03 locale payload into the source tree.
    src = pkg / "payload" / "GeneralsMD" / "Code" / "Data" / "PortugueseBrazil"
    dst = code / "Data" / "PortugueseBrazil"
    shutil.copytree(src, dst, dirs_exist_ok=True)

    print("STAGE03 APPLY PASS")
    print("Localized lookup: PortugueseBrazil -> install language -> generic.")
    print("Full PT-BR text/videos/textures copied into source tree.")

if __name__ == "__main__":
    main()
