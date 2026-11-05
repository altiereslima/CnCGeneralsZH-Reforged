#!/usr/bin/env python3
from pathlib import Path
import sys

# A barra de comando conforme os Gráficos Clássicos: as placas com eles, o ControlBar Pro sem eles.
#
# Na 3.0 o upstream (ddf1c5c3, "controlbar pro is the hud") passou a usar a barra do mod ControlBar
# Pro (xezon e FAS, MIT): a pintura de 1920x1080 em três pedaços, com as janelas do mod para a barra,
# a tela de promoções, as barras de poderes, o chat, o replay e as telas de fim de partida. Até a 2.7
# a edição PT-BR jogava com as placas texturizadas da barra (ReforgedBar*.tga) sobre o
# ControlBar.wnd da EA, em três painéis numa escala só; o upstream deixou as placas e o código delas
# no lugar, só desligado. O menu Esc e a diplomacia ele mesmo já devolveu aos da EA (afacaa40,
# da7c69ec).
#
# Desde a 3.5.1 o jogo abre com os Gráficos Clássicos (ClassicGraphics), e é com eles que a edição
# volta à barra da 2.7; sem eles fica a barra do upstream. A escolha vale para a partida inteira,
# como o próprio ClassicGraphics, que só muda na próxima vez que o jogo abre. Com os Gráficos
# Clássicos:
# - os esquemas do ControlBar Pro (ControlBarProReforged.ini) não são lidos; vale o esquema da EA de
#   cada lado, com as edições do ControlBarSchemeReforged.ini, e ele não é pintado, então
#   W3DCommandBarBackgroundDraw desenha as placas;
# - os três painéis ficam na escala única da 2.7 (a menor das duas escalas do carregador, ancorados
#   à esquerda, ao centro e à direita) em vez da altura da tela com a proporção da pintura de 1080;
# - cada janela vai ao painel pelo nome, como na 2.7, e as janelas sem nome e o CenterBackground
#   são exatamente o seu painel, para não engolir os cliques no mundo entre os painéis;
# - as janelas são as da EA, do WindowZH.big: as do ControlBar Pro saem do Data/Window para o
#   Data/Window/ModernHud, que o winCreateFromScript só procura sem os Gráficos Clássicos;
# - o ReplayControls.cpp procura o MoneyDisplay da EA em vez da janela só do ControlBar Pro.

def fail(msg):
    raise SystemExit("STAGE19: " + msg)

def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8")
    n = text.count(old)
    if n != 1:
        fail(f"{path}: esperado 1 bloco, encontrado {n}: {old[:80]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")

# As janelas do ControlBar Pro no Code/Data, relativas a GeneralsMD/Code. Vão para Data/Window/ModernHud.
MODERN_HUD_DIR = "Data/Window/ModernHud/"
CONTROL_BAR_PRO_WINDOWS = [
    "Data/Window/ControlBar.wnd",
    "Data/Window/ControlBarPopupDescription.wnd",
    "Data/Window/GenPowersShortcutBarChina.wnd",
    "Data/Window/GenPowersShortcutBarGLA.wnd",
    "Data/Window/GenPowersShortcutBarUS.wnd",
    "Data/Window/GeneralsExpPoints.wnd",
    "Data/Window/InGameChat.wnd",
    "Data/Window/ReplayControl.wnd",
    "Data/Window/Menus/Defeat.wnd",
    "Data/Window/Menus/DisconnectScreen.wnd",
    "Data/Window/Menus/LocalDefeat.wnd",
    "Data/Window/Menus/ObserverQuit.wnd",
    "Data/Window/Menus/Victorious.wnd",
]

# Âncoras do upstream; o validador confere cada uma.
PIECE_TRANSFORM = (
    "void ControlBarHudPieceTransform( Int panel, Real dispW, Real dispH, Real *sx, Real *sy, Real *originX )\n"
    "{\n"
    "\t*sy = dispH / CONTROL_BAR_DESIGN_H;\n"
    "\t*sx = *sy * HUD_ART_ASPECT * CONTROL_BAR_DESIGN_H / CONTROL_BAR_DESIGN_W;\n"
    "\t*originX = dispW * thePanelAnchorFraction[ panel ] - thePanelAnchorDesignX[ panel ] * *sx;\n"
    "}\n"
)
PANEL_FOR_WINDOW = (
    "static Int panelForWindow( const char *shortName, Real designCenterX )\n"
    "{\n"
    "\treturn strncmp( shortName, \"ObserverPlayer\", 14 ) == 0 ? ControlBar::CB_PANEL_LEFT : ControlBarHudPieceForDesignX( designCenterX );\n"
    "}\n"
)
LAYOUT_UNIFORM = (
    "\t// the rank screen and power bars at ControlBar Pro's pieces' scale\n"
    "\tReal sx, sy, unusedOriginX;\n"
    "\tControlBarHudPieceTransform( ControlBar::CB_PANEL_RIGHT, dispW, dispH, &sx, &sy, &unusedOriginX );\n"
)
PLATE_GUARD = (
    "\t// the bar is ControlBar Pro's, its scheme's painting (drawPainting), so a bar that is up wears no\n"
    "\t// plate; the plates are only the tests' to measure now\n"
    "\tif( TheControlBar != NULL )\n"
    "\t\treturn NULL;\n"
)
PLACE_IN_PANEL = (
    "\tif( strcmp( shortName, \"CenterBackground\" ) == 0 )\n"
    "\t\tchildShiftX = plate ? REAL_TO_INT_FLOOR( plate->gridShiftX * s ) : 0;\n"
    "\n"
    "\twin->winSetPosition( newX - newParentX, newY - newParentY );\n"
)
SCHEME_LOAD = (
    "\tm_loadingControlBarPro = TRUE;\n"
    "\tini.load( AsciiString( \"Data\\\\INI\\\\ControlBarProReforged.ini\" ), INI_LOAD_OVERWRITE, NULL );\n"
    "\tm_loadingControlBarPro = FALSE;\n"
)
FACTION_SCHEME = (
    "\tControlBarScheme *scheme = findSchemeForSide( side, TRUE );\n"
    "\tif( scheme != NULL )\n"
    "\t\treturn scheme;\n"
    "\n"
    "\tif( side.startsWithNoCase( \"China\" ) )\n"
    "\t\treturn findSchemeForSide( AsciiString( \"China\" ), TRUE );\n"
    "\tif( side.startsWithNoCase( \"GLA\" ) )\n"
    "\t\treturn findSchemeForSide( AsciiString( \"GLA\" ), TRUE );\n"
    "\treturn findSchemeForSide( AsciiString( \"America\" ), TRUE );\n"
)
SCHEME_PAINTED = (
    "Bool ControlBarSchemeManager::isCurrentSchemePainted( void ) const\n"
    "{\n"
    "\treturn m_currentScheme != NULL;\n"
    "}\n"
)
EXP_POINTS = (
    "\t\t// where it stands is ControlBar Pro's GeneralsExpPoints.wnd at its pieces' scale, centred at the\n"
    "\t\t// top (ControlBarLayoutUniform in ControlBar::initWindows), so nothing here moves it\n"
)
REPLAY_MONEY = "AsciiString( \"ControlBar.wnd:ControlBarProBlockInputMoney\" )"
WINDOW_OPEN = (
    "  // Open the input file\n"
    "\tinFile = TheFileSystem->openFile(filepath, File::READ);\n"
)
HUD_SQUARE = (
    "\tsquare.lo.x = 300;\n"
    "\tsquare.lo.y = 480;\n"
    "\tsquare.hi.x = 375;\n"
    "\tsquare.hi.y = 580;\n"
)
ANCHORS = {
    "ControlBar.cpp": [PIECE_TRANSFORM, PANEL_FOR_WINDOW, LAYOUT_UNIFORM, PLATE_GUARD, PLACE_IN_PANEL],
    "ControlBarScheme.cpp": [SCHEME_LOAD, FACTION_SCHEME, SCHEME_PAINTED, EXP_POINTS],
    "ReplayControls.cpp": [REPLAY_MONEY],
    "GameWindowManagerScript.cpp": [WINDOW_OPEN],
    "test_gameengine.cpp": [HUD_SQUARE],
}
SOURCES = {
    "ControlBar.cpp": "GameEngine/Source/GameClient/GUI/ControlBar/ControlBar.cpp",
    "ControlBarScheme.cpp": "GameEngine/Source/GameClient/GUI/ControlBar/ControlBarScheme.cpp",
    "ReplayControls.cpp": "GameEngine/Source/GameClient/GUI/GUICallbacks/ReplayControls.cpp",
    "GameWindowManagerScript.cpp": "GameEngine/Source/GameClient/GUI/GameWindowManagerScript.cpp",
    "test_gameengine.cpp": "Tests/test_gameengine.cpp",
}

def run_path(rel):
    """Onde o build põe um arquivo do Code/Data ao lado do generals.exe (a cópia do CMake)."""
    for src, dst in (("Data/Window/", "Window/"), ("Data/Art/Textures/", "Art/Textures/"), ("Data/INI/", "Data/INI/")):
        if rel.startswith(src):
            return (dst + rel[len(src):]).replace("/", "\\")
    fail(f"{rel}: sem lugar conhecido no Run")

def write_cleanup_bat(path):
    """Os builds 40 e 41 e o pacote do upstream deixam as janelas do ControlBar Pro soltas em Window,
    e um arquivo solto vence o WindowZH.big: sem apagá-las, os Gráficos Clássicos não voltam às janelas
    da EA. O .bat apaga só elas, na pasta onde está; as de Window\\ModernHud ficam."""
    lines = [
        "@echo off",
        "rem Edicao PT-BR: apaga as janelas do ControlBar Pro soltas em Window, de builds antigos ou do Reforged.",
        "rem Rode uma vez, na pasta do generals.exe, depois de copiar este build.",
        "cd /d \"%~dp0\"",
    ]
    lines += [f'if exist "{run_path(rel)}" del /q "{run_path(rel)}"' for rel in CONTROL_BAR_PRO_WINDOWS]
    lines += ["echo Janelas antigas do ControlBar Pro removidas.", "pause", ""]
    Path(path).write_text("\r\n".join(lines), encoding="ascii", newline="")

def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--cleanup-bat":
        write_cleanup_bat(sys.argv[2])
        return
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    code = repo / "GeneralsMD" / "Code"
    if not code.exists():
        fail("aponte para a raiz de CnCGeneralsZH-Reforged")

    sys.path.insert(0, str(Path(__file__).parent))
    import apply_stage18
    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage18.main()
    finally:
        sys.argv = old_argv

    bar = code / SOURCES["ControlBar.cpp"]
    # 1) A escolha da partida: Gráficos Clássicos levam às placas. E os três painéis delas numa escala
    #    só, a menor das duas do carregador, como na 2.7.
    replace_once(bar, PIECE_TRANSFORM,
        "/// PT-BR edition: Classic Graphics wears the plates' bar over EA's windows, as up to 2.7, and the\n"
        "/// Reforged picture ControlBar Pro's.  Fixed for the run, as ClassicGraphics is.\n"
        "Bool ControlBarPTBRClassic( void )\n"
        "{\n"
        "\treturn TheGlobalData == NULL || TheGlobalData->m_classicGraphics;\n"
        "}\n"
        "\n"
        "void ControlBarHudPieceTransform( Int panel, Real dispW, Real dispH, Real *sx, Real *sy, Real *originX )\n"
        "{\n"
        "\tif( ControlBarPTBRClassic() )\n"
        "\t{\n"
        "\t\t// the plates' bar: one scale for both axes, the smaller of the loader's two, so the three\n"
        "\t\t// panels fit side by side and nothing is stretched\n"
        "\t\tconst Real loadScaleX = dispW / CONTROL_BAR_DESIGN_W;\n"
        "\t\tconst Real loadScaleY = dispH / CONTROL_BAR_DESIGN_H;\n"
        "\t\t*sx = *sy = loadScaleX < loadScaleY ? loadScaleX : loadScaleY;\n"
        "\t\t*originX = dispW * thePanelAnchorFraction[ panel ] - thePanelAnchorDesignX[ panel ] * *sx;\n"
        "\t\treturn;\n"
        "\t}\n"
        "\t*sy = dispH / CONTROL_BAR_DESIGN_H;\n"
        "\t*sx = *sy * HUD_ART_ASPECT * CONTROL_BAR_DESIGN_H / CONTROL_BAR_DESIGN_W;\n"
        "\t*originX = dispW * thePanelAnchorFraction[ panel ] - thePanelAnchorDesignX[ panel ] * *sx;\n"
        "}\n")
    # 2) Nas placas, cada janela no painel pelo nome; as sem nome pelo lugar onde foram desenhadas.
    replace_once(bar, PANEL_FOR_WINDOW,
        "static Int panelForWindow( const char *shortName, Real designCenterX )\n"
        "{\n"
        "\tif( !ControlBarPTBRClassic() )\n"
        "\t\treturn strncmp( shortName, \"ObserverPlayer\", 14 ) == 0 ? ControlBar::CB_PANEL_LEFT : ControlBarHudPieceForDesignX( designCenterX );\n"
        "\n"
        "\t// PT-BR edition, Classic Graphics: the plates' bar, as up to 2.7\n"
        "\tstatic const char *leftNames[] =\n"
        "\t{\n"
        "\t\t\"LeftHUD\", \"WinUAttack\", \"BackgroundMarker\", \"ForegroundMarker\", \"OnTopDraw\", NULL\n"
        "\t};\n"
        "\tstatic const char *rightNames[] =\n"
        "\t{\n"
        "\t\t\"RightHUD\", \"GeneralsExp\", \"ExpBarForeground\", \"ButtonGeneral\",\n"
        "\t\t\"ButtonSmall\", \"ButtonMedium\", \"ButtonLarge\", NULL\n"
        "\t};\n"
        "\n"
        "\tif( shortName[ 0 ] == 0 )\n"
        "\t{\n"
        "\t\tfor( Int p = 0; p < ControlBar::CB_PANEL_COUNT; p++ )\n"
        "\t\t\tif( designCenterX >= thePanelDesignRect[ p ].lo.x &&\n"
        "\t\t\t\t\tdesignCenterX <= thePanelDesignRect[ p ].hi.x )\n"
        "\t\t\t\treturn p;\n"
        "\t\treturn ControlBar::CB_PANEL_CENTER;\n"
        "\t}\n"
        "\n"
        "\tfor( const char **n = leftNames; *n; n++ )\n"
        "\t\tif( strcmp( shortName, *n ) == 0 )\n"
        "\t\t\treturn ControlBar::CB_PANEL_LEFT;\n"
        "\n"
        "\tfor( const char **n = rightNames; *n; n++ )\n"
        "\t\tif( strcmp( shortName, *n ) == 0 )\n"
        "\t\t\treturn ControlBar::CB_PANEL_RIGHT;\n"
        "\n"
        "\treturn ControlBar::CB_PANEL_CENTER;\n"
        "}\n")
    # 3) A tela de promoções e as barras de poderes na escala da barra que está na tela.
    replace_once(bar, LAYOUT_UNIFORM,
        "\t// the rank screen and power bars at the bar's own scale: the plates' one scale under Classic\n"
        "\t// Graphics (PT-BR edition, as up to 2.7), ControlBar Pro's pieces' otherwise\n"
        "\tReal sx, sy, unusedOriginX;\n"
        "\tif( ControlBarPTBRClassic() )\n"
        "\t\tsx = sy = ControlBarUniformScale();\n"
        "\telse\n"
        "\t\tControlBarHudPieceTransform( ControlBar::CB_PANEL_RIGHT, dispW, dispH, &sx, &sy, &unusedOriginX );\n")
    # 4) Sob os Gráficos Clássicos as placas voltam a ser desenhadas.
    replace_once(bar, PLATE_GUARD,
        "\t// the bar is ControlBar Pro's, its scheme's painting (drawPainting), so a bar that is up wears no\n"
        "\t// plate - PT-BR edition: but for Classic Graphics, where the plates are the bar, as up to 2.7\n"
        "\tif( TheControlBar != NULL && !ControlBarPTBRClassic() )\n"
        "\t\treturn NULL;\n")
    # 5) Nas placas, os painéis de bloqueio sem nome e o CenterBackground são exatamente o seu painel.
    replace_once(bar, PLACE_IN_PANEL,
        "\tif( strcmp( shortName, \"CenterBackground\" ) == 0 )\n"
        "\t\tchildShiftX = plate ? REAL_TO_INT_FLOOR( plate->gridShiftX * s ) : 0;\n"
        "\n"
        "\t//\n"
        "\t// PT-BR edition, Classic Graphics, as up to 2.7: the unnamed children are the GameWinBlockInput\n"
        "\t// panes, whose only job is to keep clicks on the bar out of the world, so they become exactly\n"
        "\t// their panel.  CenterBackground blocks input as well as holding the command windows, and is\n"
        "\t// authored full width, so it gets the same treatment - left alone it would swallow every click\n"
        "\t// over the gaps.\n"
        "\t//\n"
        "\tif( ControlBarPTBRClassic() && ( shortName[ 0 ] == 0 || strcmp( shortName, \"CenterBackground\" ) == 0 ) )\n"
        "\t{\n"
        "\t\tnewX = m_panelRect[ panel ].lo.x;\n"
        "\t\tnewW = m_panelRect[ panel ].width();\n"
        "\t\tif( shortName[ 0 ] == 0 )\n"
        "\t\t{\n"
        "\t\t\tnewY = m_panelRect[ panel ].lo.y;\n"
        "\t\t\tnewH = m_panelRect[ panel ].height();\n"
        "\t\t}\n"
        "\t}\n"
        "\n"
        "\twin->winSetPosition( newX - newParentX, newY - newParentY );\n")

    scheme = code / SOURCES["ControlBarScheme.cpp"]
    # 6) Sob os Gráficos Clássicos os esquemas do ControlBar Pro não são lidos.
    replace_once(scheme, SCHEME_LOAD,
        "\t// PT-BR edition: not under Classic Graphics, where every side wears EA's scheme, which draws the\n"
        "\t// plates (W3DCommandBarBackgroundDraw)\n"
        "\tif( !ControlBarPTBRClassic() )\n"
        "\t{\n"
        "\t\tm_loadingControlBarPro = TRUE;\n"
        "\t\tini.load( AsciiString( \"Data\\\\INI\\\\ControlBarProReforged.ini\" ), INI_LOAD_OVERWRITE, NULL );\n"
        "\t\tm_loadingControlBarPro = FALSE;\n"
        "\t}\n")
    # 7) Sob os Gráficos Clássicos o esquema de cada lado é o da EA, pelo país, como o
    #    ControlBarPlateForSide lê um lado.
    replace_once(scheme, FACTION_SCHEME, FACTION_SCHEME.replace(", TRUE )", ", !ControlBarPTBRClassic() )"))
    # 8) Só a pintura do ControlBar Pro é pintada; o esquema da EA deixa as placas desenharem.
    replace_once(scheme, SCHEME_PAINTED,
        "Bool ControlBarSchemeManager::isCurrentSchemePainted( void ) const\n"
        "{\n"
        "\t// PT-BR edition: under Classic Graphics only ControlBar Pro's painting is painted, and EA's\n"
        "\t// schemes leave the bar to the plates\n"
        "\treturn m_currentScheme != NULL && ( !ControlBarPTBRClassic() || m_currentScheme->m_controlBarPro );\n"
        "}\n")
    # 9) Sob os Gráficos Clássicos, a tela de promoções da EA no meio da tela, como na 2.7.
    replace_once(scheme, EXP_POINTS,
        EXP_POINTS +
        "\t\t// PT-BR edition, Classic Graphics, as up to 2.7: EA's GeneralsExpPoints.wnd, the painting at its\n"
        "\t\t// shipped size in the middle of the screen\n"
        "\t\tif( ControlBarPTBRClassic() )\n"
        "\t\t{\n"
        "\t\t\tInt w = 0, h = 0;\n"
        "\t\t\twin->winGetSize( &w, &h );\n"
        "\t\t\twin->winSetPosition( ( (Int)TheDisplay->getWidth() - w ) / 2, ( (Int)TheDisplay->getHeight() - h ) / 2 );\n"
        "\t\t}\n")
    replace_once(scheme, "#include \"GameClient/ControlBarScheme.h\"\n",
        "#include \"GameClient/ControlBarScheme.h\"\n"
        "\n"
        "Bool ControlBarPTBRClassic( void );\t// PT-BR edition, ControlBar.cpp\n")

    # 10) O replay mede a faixa pelo dinheiro da barra que está na tela.
    replay = code / SOURCES["ReplayControls.cpp"]
    replace_once(replay, REPLAY_MONEY,
        "AsciiString( ControlBarPTBRClassic() ? \"ControlBar.wnd:MoneyDisplay\" : \"ControlBar.wnd:ControlBarProBlockInputMoney\" )")
    replace_once(replay, "#include \"Common/GlobalData.h\"\n",
        "#include \"Common/GlobalData.h\"\n"
        "\n"
        "Bool ControlBarPTBRClassic( void );\t// PT-BR edition, ControlBar.cpp\n")

    # 11) O teste que mede a barra na escala dela (the_hud_is_measured_at_the_command_bars_own_scale)
    #     mede o quadrado em unidades do ControlBar Pro, 75 por 100; o GlobalData dos testes abre com os
    #     Gráficos Clássicos, e nas placas um quadrado é 100 por 100.
    replace_once(code / SOURCES["test_gameengine.cpp"], HUD_SQUARE,
        "\t// PT-BR edition: Classic Graphics, the tests' default, wears the plates' bar at one scale for\n"
        "\t// both axes, so a square is 100 by 100\n"
        "\tsquare.lo.x = 300;\n"
        "\tsquare.lo.y = 480;\n"
        "\tsquare.hi.x = 400;\n"
        "\tsquare.hi.y = 580;\n")

    # 12) As janelas do ControlBar Pro vão para Data/Window/ModernHud, que só vale sem os Gráficos
    #     Clássicos; com eles ficam as da EA, do WindowZH.big. O manifesto acompanha.
    replace_once(code / SOURCES["GameWindowManagerScript.cpp"], WINDOW_OPEN,
        "  // Open the input file.  PT-BR edition: ControlBar Pro's windows ship in Window\\ModernHud and stand\n"
        "\t// in for EA's only while ClassicGraphics is off; under Classic Graphics they are EA's (WindowZH.big).\n"
        "\tinFile = NULL;\n"
        "\tif( TheGlobalData != NULL && !TheGlobalData->m_classicGraphics &&\n"
        "\t\t\tAsciiString( filepath ).startsWithNoCase( \"Window\\\\\" ) )\n"
        "\t{\n"
        "\t\tchar modernPath[ _MAX_PATH ];\n"
        "\t\tsnprintf( modernPath, ARRAY_SIZE(modernPath), \"Window\\\\ModernHud\\\\%s\", filepath + 7 );\n"
        "\t\tinFile = TheFileSystem->openFile( modernPath, File::READ );\n"
        "\t}\n"
        "\tif( inFile == NULL )\n"
        "\t\tinFile = TheFileSystem->openFile(filepath, File::READ);\n")
    manifest = code / "BuildFingerprint.manifest"
    lines = manifest.read_text(encoding="utf-8").split("\n")
    for rel in CONTROL_BAR_PRO_WINDOWS:
        src = code / rel
        if not src.is_file():
            fail(f"{rel}: ausente, o upstream mudou o ControlBar Pro")
        if lines.count(rel) != 1:
            fail(f"BuildFingerprint.manifest: esperado 1 linha {rel}, encontrado {lines.count(rel)}")
        moved = MODERN_HUD_DIR + rel[len("Data/Window/"):]
        dst = code / moved
        dst.parent.mkdir(parents=True, exist_ok=True)
        src.replace(dst)
        lines[lines.index(rel)] = moved
    manifest.write_text("\n".join(lines), encoding="utf-8", newline="")

    print("STAGE19 APPLY PASS")
    print("Classic Graphics: the plates' bar over EA's windows, as up to 2.7; otherwise ControlBar Pro's.")

if __name__ == "__main__":
    main()
