#!/usr/bin/env python3
from pathlib import Path
import sys

# O rótulo de versão no rodapé do menu Opções: o upstream escreve "Reforged v3.4.2"; a edição PT-BR
# mostra o nome do jogo e o número da versão, "Command & Conquer Generals Zero Hour 3.4.2". O número
# continua vindo do BuildVersion.h que o CMake escreve, então acompanha cada atualização sozinho.

def fail(msg):
    raise SystemExit("STAGE20: " + msg)

def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8-sig")
    n = text.count(old)
    if n != 1:
        fail(f"{path}: esperado 1 bloco, encontrado {n}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")

# Âncora do OptionsMenu.cpp do upstream; o validador confere.
VERSION_LABEL = (
    "\tUnicodeString versionString( u\"Reforged \" );\n"
    "\tUnicodeString releaseVersion;\n"
    "\treleaseVersion.translate( TheVersion->getReleaseVersion() );\n"
)
GAME_NAME = "Command & Conquer Generals Zero Hour"

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    code = repo / "GeneralsMD" / "Code"
    if not code.exists():
        fail("aponte para a raiz de CnCGeneralsZH-Reforged")

    sys.path.insert(0, str(Path(__file__).parent))
    import apply_stage19
    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage19.main()
    finally:
        sys.argv = old_argv

    replace_once(code / "GameEngine" / "Source" / "GameClient" / "GUI" / "GUICallbacks" / "Menus" / "OptionsMenu.cpp",
        VERSION_LABEL,
        f"\t// PT-BR edition: the game's name and the release number, without the tag's \"v\".\n"
        f"\tUnicodeString versionString( u\"{GAME_NAME} \" );\n"
        "\tUnicodeString releaseVersion;\n"
        "\treleaseVersion.translate( AsciiString( TheVersion->getReleaseVersion().str() + 1 ) );\n")

    print("STAGE20 APPLY PASS")
    print(f"The Options menu's version label reads \"{GAME_NAME}\" and the release number.")

if __name__ == "__main__":
    main()
