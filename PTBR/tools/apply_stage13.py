#!/usr/bin/env python3
from pathlib import Path
import sys

# A caixa do canto superior direito (relógio da partida, taxas de quadros e, em rede, a conexão)
# vem ligada para todos no upstream. A edição PT-BR começa com ela escondida.
#
# Desde a v2.3.0 o upstream deu à caixa uma chave própria, com controle no menu de opções:
# ShowNetBox ("Clock And Frame Rate Box"). A edição PT-BR só muda o padrão para desligado;
# marcar a opção no menu, ou "ShowNetBox = yes" no Options.ini, traz a caixa de volta. Desde a
# 2.5 isso vale para a interface Reforged (-interface reforged): a Classic do upstream, a que a
# edição abre, segue o HUD da EA, sem caixa no canto e sem essa opção no menu dela.
#
# Até a v2.2.1 a edição fazia isso com ShowHudOverlay = FALSE e uma linha ShowHudOverlay própria
# no catálogo. O upstream agora exige que essa linha não exista (ela voltaria a ler um "no" antigo
# do Options.ini), e com ela o menu mostraria a opção nova marcada sem caixa nenhuma na tela.

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
        "\tm_showNetBox = TRUE;\n",
        "\t// PT-BR edition: the corner box starts hidden; the Clock And Frame Rate Box option\n"
        "\t// (ShowNetBox in Options.ini) brings it back.\n"
        "\tm_showNetBox = FALSE;\n",
    )

    # 2) O teste do upstream que exige a caixa ligada de início passa a exigir o contrário.
    test = code / "Tests" / "test_gameengine.cpp"
    replace_once(
        test,
        "\tCHECK( scratch->m_showNetBox );\n"
        "\t// the older plate's own switch",
        "\tCHECK( !scratch->m_showNetBox );\t// PT-BR edition: off until the player ticks it\n"
        "\t// the older plate's own switch",
    )

    print("STAGE13 APPLY PASS")
    print("Corner box hidden by default; the Clock And Frame Rate Box option (ShowNetBox) shows it.")

if __name__ == "__main__":
    main()
