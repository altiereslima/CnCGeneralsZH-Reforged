#!/usr/bin/env python3
from pathlib import Path
import sys

# (O estágio 15 é o runner de build, run_windows_stage15.py; este vem depois dele.)
#
# Desde a v2.1.0+ o upstream desenha a moldura da barra de comando com uma página HTML
# (Window/Html/ControlBar.html) no lugar das três placas texturizadas (Data/Art/Textures/
# ReforgedBar*.tga). O próprio W3DControlBar.cpp volta às placas quando a página não está lá.
# A edição PT-BR usa as placas por padrão: drawControlBarPage devolve FALSE como se a página
# faltasse, sem depender de apagar arquivo na pasta do jogo. Junto com a página somem a caixa
# de rede (relógio, hz/fps, frame) e os botões que ela desenhava; os botões originais da barra
# voltam a se desenhar sozinhos. "ClassicCommandBar = no" no Options.ini traz a página de volta.

def fail(msg):
    raise SystemExit("STAGE16: " + msg)

def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8-sig")
    n = text.count(old)
    if n != 1:
        fail(f"{path}: esperado 1 bloco, encontrado {n}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    code = repo / "GeneralsMD" / "Code"
    if not code.exists():
        fail("aponte para a raiz de CnCGeneralsZH-Reforged")

    sys.path.insert(0, str(Path(__file__).parent))
    import apply_stage14
    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage14.main()
    finally:
        sys.argv = old_argv

    # 1) O campo.
    replace_once(
        code / "GameEngine" / "Include" / "Common" / "GlobalData.h",
        "\tBool m_showHudOverlay;\t\t\t\t///< draw the fps / elapsed time / income line in the corner\n",
        "\tBool m_showHudOverlay;\t\t\t\t///< draw the fps / elapsed time / income line in the corner\n"
        "\tBool m_classicCommandBar;\t\t\t///< PT-BR edition: the textured plates instead of ControlBar.html\n",
    )

    # 2) Padrão: placas antigas. A âncora é a linha que o estágio 13 escreveu.
    replace_once(
        code / "GameEngine" / "Source" / "Common" / "GlobalData.cpp",
        "\tm_showHudOverlay = FALSE;\n",
        "\tm_showHudOverlay = FALSE;\n"
        "\t// PT-BR edition: the command bar keeps its textured plates; ClassicCommandBar = no in\n"
        "\t// Options.ini draws Window/Html/ControlBar.html in their place.\n"
        "\tm_classicCommandBar = TRUE;\n",
    )

    # 3) Options.ini lê a chave, sem controle no menu, como ShowHudOverlay.
    catalog = code / "GameEngine" / "Source" / "Common" / "OptionsCatalog.cpp"
    replace_once(
        catalog,
        "OPTION_BOOL_ACCESSORS( m_showHudOverlay )\n",
        "OPTION_BOOL_ACCESSORS( m_showHudOverlay )\n"
        "OPTION_BOOL_ACCESSORS( m_classicCommandBar )\n",
    )
    replace_once(
        catalog,
        "\t\tget_m_showHudOverlay, set_m_showHudOverlay },\n",
        "\t\tget_m_showHudOverlay, set_m_showHudOverlay },\n"
        "\n"
        "\t// PT-BR edition: the textured command bar plates by default.\n"
        "\t{ \"ClassicCommandBar\",\t\t\t\tNULL, \"GUI:HudOverlay\",\n"
        "\t\tOPTION_BOOL, APPLY_RESTART, 0, 1,\n"
        "\t\tget_m_classicCommandBar, set_m_classicCommandBar },\n",
    )

    # 4) A página só entra quando pedida.
    replace_once(
        code / "GameEngine" / "Source" / "GameClient" / "InGameUI.cpp",
        "\tif( m_controlBarPage.empty() )\n"
        "\t{\n"
        "\t\tTheControlBar->setPageSolids( NULL );\n"
        "\t\treturn FALSE;\n"
        "\t}\n",
        "\t// PT-BR edition: the textured plates unless Options.ini asks for the page\n"
        "\tif( m_controlBarPage.empty() || TheGlobalData->m_classicCommandBar )\n"
        "\t{\n"
        "\t\tTheControlBar->setPageSolids( NULL );\n"
        "\t\treturn FALSE;\n"
        "\t}\n",
    )

    print("STAGE16 APPLY PASS")
    print("Command bar uses the textured plates; ClassicCommandBar = no in Options.ini shows the HTML frame.")

if __name__ == "__main__":
    main()
