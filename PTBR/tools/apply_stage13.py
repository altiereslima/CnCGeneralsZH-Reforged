#!/usr/bin/env python3
from pathlib import Path
import sys

# A linha de números no canto superior direito (relógio, tempo de jogo, hz/fps, renderizador,
# frame) vem ligada para todos no upstream e não tem controle no menu. A edição PT-BR começa
# com ela escondida; "ShowHudOverlay = yes" no Options.ini traz de volta, do mesmo jeito que
# as faixas do observador (ShowProductionStrip etc.) são lidas sem controle no menu.

def fail(msg):
    raise SystemExit("STAGE13: " + msg)

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
    import apply_stage12
    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage12.main()
    finally:
        sys.argv = old_argv

    # 1) Padrão desligado.
    replace_once(
        code / "GameEngine" / "Source" / "Common" / "GlobalData.cpp",
        "\tm_showHudOverlay = TRUE;\n",
        "\t// PT-BR edition: the corner readout starts hidden; ShowHudOverlay = yes in Options.ini\n"
        "\t// brings it back.\n"
        "\tm_showHudOverlay = FALSE;\n",
    )

    # 2) Options.ini volta a ler a chave, sem controle no menu.
    catalog = code / "GameEngine" / "Source" / "Common" / "OptionsCatalog.cpp"
    replace_once(
        catalog,
        "OPTION_BOOL_ACCESSORS( m_showSuperweaponStrip )\n",
        "OPTION_BOOL_ACCESSORS( m_showSuperweaponStrip )\n"
        "OPTION_BOOL_ACCESSORS( m_showHudOverlay )\n",
    )
    replace_once(
        catalog,
        "\t\tget_m_showSuperweaponStrip, set_m_showSuperweaponStrip },\n"
        "\n"
        "\t{ NULL, NULL, NULL, OPTION_BOOL, APPLY_LIVE, 0, 0, NULL, NULL }\n",
        "\t\tget_m_showSuperweaponStrip, set_m_showSuperweaponStrip },\n"
        "\n"
        "\t// PT-BR edition: the corner readout is off by default, so it needs a key to come back by.\n"
        "\t{ \"ShowHudOverlay\",\t\t\t\t\tNULL, \"GUI:HudOverlay\",\n"
        "\t\tOPTION_BOOL, APPLY_LIVE, 0, 1,\n"
        "\t\tget_m_showHudOverlay, set_m_showHudOverlay },\n"
        "\n"
        "\t{ NULL, NULL, NULL, OPTION_BOOL, APPLY_LIVE, 0, 0, NULL, NULL }\n",
    )

    # 3) O teste do upstream que exige o overlay ligado e fora do catálogo passa a exigir o contrário.
    test = code / "Tests" / "test_gameengine.cpp"
    replace_once(
        test,
        "\t\t\"ShowHudOverlay\", \"ArchiveReplays\", NULL\n",
        "\t\t\"ArchiveReplays\", NULL\n",
    )
    replace_once(
        test,
        "\tCHECK( scratch->m_showHudOverlay );\n",
        "\tCHECK( !scratch->m_showHudOverlay );\t// PT-BR edition: hidden until Options.ini asks for it\n"
        "\tCHECK( findOptionDef( \"ShowHudOverlay\" ) != NULL );\n",
    )

    print("STAGE13 APPLY PASS")
    print("HUD corner readout hidden by default; ShowHudOverlay = yes in Options.ini shows it.")

if __name__ == "__main__":
    main()
