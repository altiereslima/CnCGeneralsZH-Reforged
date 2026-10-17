#!/usr/bin/env python3
from pathlib import Path
import sys

# A barra de comando de placas no lugar do ControlBar Pro.
#
# Na 3.0 o upstream (ddf1c5c3, "controlbar pro is the hud") passou a usar a barra do mod ControlBar
# Pro (xezon e FAS, MIT): a pintura de 1920x1080 em três pedaços, com as janelas do mod para a barra,
# a tela de promoções, as barras de poderes, o chat, o replay e as telas de fim de partida. Até a 2.7
# a edição PT-BR jogava com as placas texturizadas da barra (ReforgedBar*.tga) sobre o
# ControlBar.wnd da EA, em três painéis numa escala só; o upstream deixou as placas e o código delas
# no lugar, só desligado. O menu Esc e a diplomacia ele mesmo já devolveu aos da EA (afacaa40,
# da7c69ec).
#
# Este estágio desfaz a troca no build:
# - os esquemas do ControlBar Pro (ControlBarProReforged.ini) não são lidos; vale o esquema da EA de
#   cada lado, com as edições do ControlBarSchemeReforged.ini, e ele não é pintado, então
#   W3DCommandBarBackgroundDraw volta a desenhar as placas;
# - os três painéis voltam à escala única da 2.7 (a menor das duas escalas do carregador, ancorados
#   à esquerda, ao centro e à direita) em vez da altura da tela com a proporção da pintura de 1080;
# - cada janela volta ao painel pelo nome, como na 2.7, e as janelas sem nome e o CenterBackground
#   voltam a ser exatamente o seu painel, para não engolir os cliques no mundo entre os painéis;
# - as janelas, a arte e o esquema do ControlBar Pro saem do Code/Data, então o Run fica com as
#   janelas da EA que estão no WindowZH.big; o BuildFingerprint.manifest perde as mesmas linhas;
# - o ReplayControls.cpp, que procurava uma janela só do ControlBar Pro, procura o MoneyDisplay da EA.

def fail(msg):
    raise SystemExit("STAGE19: " + msg)

def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8-sig")
    n = text.count(old)
    if n != 1:
        fail(f"{path}: esperado 1 bloco, encontrado {n}: {old[:80]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")

# O que o ControlBar Pro trouxe para o Code/Data e ainda está lá, relativo a GeneralsMD/Code.
CONTROL_BAR_PRO_FILES = [
    "Data/INI/ControlBarProReforged.ini",
    "Data/Window/ControlBar.wnd",
    "Data/Window/ControlBarPopupDescription.wnd",
    "Data/Window/ControlBarPro.txt",
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
] + [f"Data/Art/Textures/ReforgedHud{n}.{ext}" for n, ext in (
    ("AmericaCommandBar", "dds"), ("AmericaPowers", "dds"), ("BlankTexture", "tga"),
    ("ChinaCommandBar", "dds"), ("ChinaPowers", "dds"), ("ControlButtons", "dds"),
    ("DisconnectWindow", "dds"), ("GlaCommandBar", "dds"), ("GlaPowers", "dds"),
    ("Helpbox", "dds"), ("ObsCommandBar", "dds"), ("ObsPlayerWindow", "dds"))
] + [f"Data/INI/MappedImages/HandCreated/ReforgedHud{n}.ini" for n in (
    "AmericaCommandBar", "AmericaPowers", "BlankTexture", "ChinaCommandBar", "ChinaPowers",
    "ControlButtons", "DisconnectWindow", "GlaCommandBar", "GlaPowers", "Helpbox",
    "ObsCommandBar", "ObsPlayerWindow")
]

# Âncoras do upstream; o validador confere cada uma.
PIECE_TRANSFORM = (
    "\t*sy = dispH / CONTROL_BAR_DESIGN_H;\n"
    "\t*sx = *sy * HUD_ART_ASPECT * CONTROL_BAR_DESIGN_H / CONTROL_BAR_DESIGN_W;\n"
    "\t*originX = dispW * thePanelAnchorFraction[ panel ] - thePanelAnchorDesignX[ panel ] * *sx;\n"
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
REPLAY_MONEY = "\"ControlBar.wnd:ControlBarProBlockInputMoney\""
ANCHORS = {
    "ControlBar.cpp": [PIECE_TRANSFORM, PANEL_FOR_WINDOW, LAYOUT_UNIFORM, PLATE_GUARD, PLACE_IN_PANEL],
    "ControlBarScheme.cpp": [SCHEME_LOAD, FACTION_SCHEME, SCHEME_PAINTED, EXP_POINTS],
    "ReplayControls.cpp": [REPLAY_MONEY],
}
SOURCES = {
    "ControlBar.cpp": "GameEngine/Source/GameClient/GUI/ControlBar/ControlBar.cpp",
    "ControlBarScheme.cpp": "GameEngine/Source/GameClient/GUI/ControlBar/ControlBarScheme.cpp",
    "ReplayControls.cpp": "GameEngine/Source/GameClient/GUI/GUICallbacks/ReplayControls.cpp",
}

def run_path(rel):
    """Onde o build põe um arquivo do Code/Data ao lado do generals.exe (a cópia do CMake)."""
    for src, dst in (("Data/Window/", "Window/"), ("Data/Art/Textures/", "Art/Textures/"), ("Data/INI/", "Data/INI/")):
        if rel.startswith(src):
            return (dst + rel[len(src):]).replace("/", "\\")
    fail(f"{rel}: sem lugar conhecido no Run")

def write_cleanup_bat(path):
    """Os builds 40 e 41 deixaram estes arquivos soltos na pasta do jogo, e um arquivo solto vence o
    WindowZH.big: sem apagá-los, a barra da EA não volta. O .bat apaga só eles, na pasta onde está."""
    lines = [
        "@echo off",
        "rem Edicao PT-BR: apaga os arquivos do ControlBar Pro que os builds 40 e 41 deixaram na pasta do jogo.",
        "rem Rode uma vez, na pasta do generals.exe, depois de copiar este build.",
        "cd /d \"%~dp0\"",
    ]
    lines += [f'if exist "{run_path(rel)}" del /q "{run_path(rel)}"' for rel in CONTROL_BAR_PRO_FILES]
    lines += ["echo Arquivos do ControlBar Pro removidos.", "pause", ""]
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
    # 1) Os três painéis numa escala só, a menor das duas do carregador, como na 2.7.
    replace_once(bar, PIECE_TRANSFORM,
        "\t// PT-BR edition: the plates' bar, as up to 2.7: one scale for both axes, the smaller of the\n"
        "\t// loader's two, so the three panels fit side by side and nothing is stretched\n"
        "\tconst Real loadScaleX = dispW / CONTROL_BAR_DESIGN_W;\n"
        "\tconst Real loadScaleY = dispH / CONTROL_BAR_DESIGN_H;\n"
        "\t*sx = *sy = loadScaleX < loadScaleY ? loadScaleX : loadScaleY;\n"
        "\t*originX = dispW * thePanelAnchorFraction[ panel ] - thePanelAnchorDesignX[ panel ] * *sx;\n"
        "\t(void)HUD_ART_ASPECT;\n")
    # 2) Cada janela no painel pelo nome; as sem nome pelo lugar onde foram desenhadas.
    replace_once(bar, PANEL_FOR_WINDOW,
        "static Int panelForWindow( const char *shortName, Real designCenterX )\n"
        "{\n"
        "\t// PT-BR edition: the plates' bar, as up to 2.7\n"
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
    # 3) A tela de promoções e as barras de poderes na mesma escala única.
    replace_once(bar, LAYOUT_UNIFORM,
        "\t// PT-BR edition: the rank screen and power bars at the plates' one scale, as up to 2.7\n"
        "\tconst Real sx = ControlBarUniformScale();\n"
        "\tconst Real sy = sx;\n")
    # 4) As placas voltam a ser desenhadas.
    replace_once(bar, PLATE_GUARD,
        "\t// PT-BR edition: the plates are the bar again, ControlBar Pro's painting is not loaded\n")
    # 5) Os painéis de bloqueio sem nome e o CenterBackground são exatamente o seu painel.
    replace_once(bar, PLACE_IN_PANEL,
        "\tif( strcmp( shortName, \"CenterBackground\" ) == 0 )\n"
        "\t\tchildShiftX = plate ? REAL_TO_INT_FLOOR( plate->gridShiftX * s ) : 0;\n"
        "\n"
        "\t//\n"
        "\t// PT-BR edition, as up to 2.7: the unnamed children are the GameWinBlockInput panes, whose only\n"
        "\t// job is to keep clicks on the bar out of the world, so they become exactly their panel.\n"
        "\t// CenterBackground blocks input as well as holding the command windows, and is authored full\n"
        "\t// width, so it gets the same treatment - left alone it would swallow every click over the gaps.\n"
        "\t//\n"
        "\tif( shortName[ 0 ] == 0 || strcmp( shortName, \"CenterBackground\" ) == 0 )\n"
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
    # 6) Os esquemas do ControlBar Pro não são lidos.
    replace_once(scheme, SCHEME_LOAD,
        "\t// PT-BR edition: ControlBar Pro's schemes are not read; every side wears EA's, which draws the\n"
        "\t// plates (W3DCommandBarBackgroundDraw)\n")
    # 7) O esquema de cada lado é o da EA, pelo país, como o ControlBarPlateForSide lê um lado.
    replace_once(scheme, FACTION_SCHEME, FACTION_SCHEME.replace(", TRUE )", ", FALSE )"))
    # 8) Só a pintura do ControlBar Pro é pintada; o esquema da EA deixa as placas desenharem.
    replace_once(scheme, SCHEME_PAINTED,
        "Bool ControlBarSchemeManager::isCurrentSchemePainted( void ) const\n"
        "{\n"
        "\t// PT-BR edition: only ControlBar Pro's painting is painted; EA's schemes leave the bar to the plates\n"
        "\treturn m_currentScheme != NULL && m_currentScheme->m_controlBarPro;\n"
        "}\n")
    # 9) A tela de promoções da EA no meio da tela, como a 2.7 fazia antes do ControlBarLayoutUniform.
    replace_once(scheme, EXP_POINTS,
        "\t\t// PT-BR edition, as up to 2.7: EA's GeneralsExpPoints.wnd, the painting at its shipped size in\n"
        "\t\t// the middle of the screen\n"
        "\t\tInt w = 0, h = 0;\n"
        "\t\twin->winGetSize( &w, &h );\n"
        "\t\twin->winSetPosition( ( (Int)TheDisplay->getWidth() - w ) / 2, ( (Int)TheDisplay->getHeight() - h ) / 2 );\n")

    # 10) O replay mede a faixa pelo dinheiro da EA.
    replace_once(code / SOURCES["ReplayControls.cpp"], REPLAY_MONEY, "\"ControlBar.wnd:MoneyDisplay\"")

    # 11) As janelas, a arte e o esquema do ControlBar Pro saem do Code/Data e do manifesto.
    manifest = code / "BuildFingerprint.manifest"
    lines = manifest.read_text(encoding="utf-8").split("\n")
    for rel in CONTROL_BAR_PRO_FILES:
        path = code / rel
        if not path.is_file():
            fail(f"{rel}: ausente, o upstream mudou o ControlBar Pro")
        if lines.count(rel) != 1:
            fail(f"BuildFingerprint.manifest: esperado 1 linha {rel}, encontrado {lines.count(rel)}")
        path.unlink()
        lines.remove(rel)
    manifest.write_text("\n".join(lines), encoding="utf-8", newline="")

    print("STAGE19 APPLY PASS")
    print("The plates' command bar over EA's windows, as up to 2.7; ControlBar Pro left out of the build.")

if __name__ == "__main__":
    main()
