#!/usr/bin/env python3
from pathlib import Path
import sys

# A caixa do canto superior direito (relógio da partida, taxas de quadros e, em rede, a conexão)
# vinha ligada para todos no upstream. A edição PT-BR começa com ela escondida.
#
# Desde a v2.3.0 o upstream deu à caixa uma chave própria, com controle no menu de opções:
# ShowNetBox ("Info Box" desde a 2.6). A edição PT-BR só muda o padrão para desligado;
# marcar a opção no menu, ou "ShowNetBox = yes" no Options.ini, traz a caixa de volta. Desde a
# 3.0 o upstream tem uma interface só, no layout do Zero Hour, e ela desenha a caixa.
#
# Até a v2.2.1 a edição fazia isso com ShowHudOverlay = FALSE e uma linha ShowHudOverlay própria
# no catálogo. O upstream agora exige que essa linha não exista (ela voltaria a ler um "no" antigo
# do Options.ini), e com ela o menu mostraria a opção nova marcada sem caixa nenhuma na tela.

# Desde a 3.5.1 (ffa57fbc, "the game opens classic with every added setting off") o próprio upstream
# começa com a caixa escondida. O estágio não muda mais nada: só confere que o padrão continua
# desligado, para a edição perceber se o upstream voltar atrás.
NETBOX_DEFAULT = "\tm_showNetBox = FALSE;\n"

def fail(msg):
    raise SystemExit("STAGE13: " + msg)

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

    # O padrão do upstream já é o da edição.
    cpp = (code / "GameEngine" / "Source" / "Common" / "GlobalData.cpp").read_text(encoding="utf-8")
    if cpp.count(NETBOX_DEFAULT) != 1:
        fail("GlobalData.cpp: o upstream não começa mais com a caixa do canto escondida")

    print("STAGE13 APPLY PASS")
    print("Corner box hidden by default; the Info Box option (ShowNetBox) shows it.")

if __name__ == "__main__":
    main()
