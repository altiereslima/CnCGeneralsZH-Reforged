#!/usr/bin/env python3
from pathlib import Path
import sys

# (O estágio 15 é o runner de build, run_windows_stage15.py; este vem depois dele.)
#
# Interface clássica. Desde a v2.1.0+ o upstream desenha duas coisas com páginas HTML:
#
# - a moldura da barra de comando (Window/Html/ControlBar.html), no lugar das três placas
#   texturizadas Data/Art/Textures/ReforgedBar*.tga. O W3DControlBar.cpp volta às placas quando
#   a página não está lá. Junto com a página somem a caixa de rede (relógio, hz/fps, frame) e os
#   botões que ela desenhava; os botões originais da barra voltam a se desenhar sozinhos.
# - o menu Esc (Window/Html/QuitMenu.html): compacto, sem o logo, com a tela escurecida. O
#   themeQuitMenu deixa o menu original quando a página não está lá. O QuitMenu.cpp também trocou
#   as transições de abrir e fechar da EA por mostrar e esconder na hora, com ou sem página.
#
# A edição PT-BR fica com o original nos dois: as páginas respondem como se faltassem, sem
# depender de apagar arquivo na pasta do jogo, e o menu Esc volta a abrir e fechar pelas próprias
# transições. "ClassicInterface = no" no Options.ini traz as páginas do upstream de volta.
#
# Desde a v2.2.0 a página também monta a barra de novo (grade 6x3, barra de retratos), numa escala
# própria, 70% da uniforme. Essa montagem só roda com a página, então a barra clássica continua no
# layout da EA, na escala uniforme; mas os números nos cantos dos botões (fila, recarga, preço)
# passaram a usar a escala da página em qualquer caso. Com a interface clássica eles voltam à
# escala da barra em que estão.

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
        "\tBool m_classicInterface;\t\t\t///< PT-BR edition: the original command bar plates and Esc menu\n",
    )

    # 2) Padrão: interface original. A âncora é a linha que o estágio 13 escreveu.
    replace_once(
        code / "GameEngine" / "Source" / "Common" / "GlobalData.cpp",
        "\tm_showHudOverlay = FALSE;\n",
        "\tm_showHudOverlay = FALSE;\n"
        "\t// PT-BR edition: the command bar keeps its textured plates and the Esc menu its original\n"
        "\t// layout; ClassicInterface = no in Options.ini draws upstream's HTML pages instead.\n"
        "\tm_classicInterface = TRUE;\n",
    )

    # 3) Options.ini lê a chave, sem controle no menu, como ShowHudOverlay.
    catalog = code / "GameEngine" / "Source" / "Common" / "OptionsCatalog.cpp"
    replace_once(
        catalog,
        "OPTION_BOOL_ACCESSORS( m_showHudOverlay )\n",
        "OPTION_BOOL_ACCESSORS( m_showHudOverlay )\n"
        "OPTION_BOOL_ACCESSORS( m_classicInterface )\n",
    )
    replace_once(
        catalog,
        "\t\tget_m_showHudOverlay, set_m_showHudOverlay },\n",
        "\t\tget_m_showHudOverlay, set_m_showHudOverlay },\n"
        "\n"
        "\t// PT-BR edition: the original command bar plates and Esc menu by default.\n"
        "\t{ \"ClassicInterface\",\t\t\t\t\tNULL, \"GUI:HudOverlay\",\n"
        "\t\tOPTION_BOOL, APPLY_RESTART, 0, 1,\n"
        "\t\tget_m_classicInterface, set_m_classicInterface },\n",
    )

    ui = code / "GameEngine" / "Source" / "GameClient" / "InGameUI.cpp"

    # 4) A página da barra só entra quando pedida.
    replace_once(
        ui,
        "\tif( m_controlBarPage.empty() )\n"
        "\t{\n"
        "\t\tTheControlBar->setPageSolids( NULL );\n"
        "\t\treturn FALSE;\n"
        "\t}\n",
        "\t// PT-BR edition: the textured plates unless Options.ini asks for the page\n"
        "\tif( m_controlBarPage.empty() || TheGlobalData->m_classicInterface )\n"
        "\t{\n"
        "\t\tTheControlBar->setPageSolids( NULL );\n"
        "\t\treturn FALSE;\n"
        "\t}\n",
    )

    # 5) A página do menu Esc também.
    replace_once(
        ui,
        "\t\treadHtmlPage( QUIT_MENU_PAGE, m_quitMenuPage );\n"
        "\t}\n"
        "\tif( m_quitMenuPage.empty() )\n"
        "\t\treturn;\n",
        "\t\treadHtmlPage( QUIT_MENU_PAGE, m_quitMenuPage );\n"
        "\t}\n"
        "\t// PT-BR edition: the original Esc menu unless Options.ini asks for the page\n"
        "\tif( m_quitMenuPage.empty() || TheGlobalData->m_classicInterface )\n"
        "\t\treturn;\n",
    )

    # 6) E o menu Esc original abre e fecha pelas transições da EA, como antes.
    quit_menu = code / "GameEngine" / "Source" / "GameClient" / "GUI" / "GUICallbacks" / "Menus" / "QuitMenu.cpp"
    replace_once(
        quit_menu,
        "static void showQuitMenuLayout( const char *group )\n"
        "{\n"
        "\tTheTransitionHandler->remove( group );\n"
        "\tTheTransitionHandler->setGroup( group );\n"
        "\tTheTransitionHandler->remove( group, TRUE );\n"
        "}\n",
        "static void showQuitMenuLayout( const char *group )\n"
        "{\n"
        "\tTheTransitionHandler->remove( group );\n"
        "\tTheTransitionHandler->setGroup( group );\n"
        "\t// PT-BR edition: the original menu keeps its opening transition\n"
        "\tif( !TheGlobalData->m_classicInterface )\n"
        "\t\tTheTransitionHandler->remove( group, TRUE );\n"
        "}\n",
    )
    replace_once(
        quit_menu,
        "static void hideQuitMenuLayout( void )\n"
        "{\n"
        "\tif( quitMenuLayout )\n"
        "\t\tquitMenuLayout->hide( TRUE );\n"
        "}\n",
        "static void hideQuitMenuLayout( void )\n"
        "{\n"
        "\t// PT-BR edition: the original menu leaves the way it came, through its own transitions\n"
        "\tif( TheGlobalData->m_classicInterface )\n"
        "\t{\n"
        "\t\tif( quitMenuLayout && quitMenuLayout == noSaveLoadQuitMenuLayout )\n"
        "\t\t\tTheTransitionHandler->reverse( \"QuitNoSaveBack\" );\n"
        "\t\telse if( quitMenuLayout && quitMenuLayout == fullQuitMenuLayout )\n"
        "\t\t\tTheTransitionHandler->reverse( \"QuitFullBack\" );\n"
        "\t\treturn;\n"
        "\t}\n"
        "\tif( quitMenuLayout )\n"
        "\t\tquitMenuLayout->hide( TRUE );\n"
        "}\n",
    )

    # 7) Os números nos cantos dos botões na escala da barra clássica, a uniforme.
    button = code / "GameEngineDevice" / "Source" / "W3DDevice" / "GameClient" / "GUI" / "Gadget" / "W3DPushButton.cpp"
    replace_once(
        button,
        "// USER INCLUDES //////////////////////////////////////////////////////////////\n"
        "#include \"GameClient/Gadget.h\"\n",
        "// USER INCLUDES //////////////////////////////////////////////////////////////\n"
        "#include \"Common/GlobalData.h\"\n"
        "#include \"GameClient/Gadget.h\"\n",
    )
    replace_once(
        button,
        "extern Real ControlBarHudScale( void );\n",
        "extern Real ControlBarHudScale( void );\n"
        "extern Real ControlBarUniformScale( void );\t///< PT-BR edition: the scale the classic bar stands at\n",
    )
    replace_once(
        button,
        "\tInt pointSize = REAL_TO_INT_FLOOR( designPoints * ControlBarHudScale() );\n",
        "\t// PT-BR edition: the classic bar is laid out at the uniform scale, and its markings with it\n"
        "\tconst Real barScale = ( TheGlobalData != NULL && TheGlobalData->m_classicInterface )\n"
        "\t\t? ControlBarUniformScale() : ControlBarHudScale();\n"
        "\tInt pointSize = REAL_TO_INT_FLOOR( designPoints * barScale );\n",
    )

    print("STAGE16 APPLY PASS")
    print("Original command bar plates and Esc menu; ClassicInterface = no in Options.ini shows the HTML pages.")

if __name__ == "__main__":
    main()
