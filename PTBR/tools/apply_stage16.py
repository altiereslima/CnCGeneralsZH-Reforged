#!/usr/bin/env python3
from pathlib import Path
import sys

# (O estágio 15 é o runner de build, run_windows_stage15.py; este vem depois dele.)
#
# Interface clássica. Até a 2.4 a edição PT-BR tinha uma interface clássica própria, a chave
# ClassicInterface do Options.ini: as placas texturizadas da barra no lugar da página HTML, o menu
# Esc da EA com as transições dele, a plaquinha do canto pela opção do menu e o piscar de "sob
# ataque" da EA. Desde a 2.5 o upstream tem a dele, a interface Classic, que é a que abre quando o
# generals.exe roda sem o -interface que o launcher passa: o caso da edição PT-BR. Ela pula toda
# página HTML (readHtmlPage), desenha as três placas ReforgedBar, deixa o menu Esc nas transições
# da EA e acende o piscar de "sob ataque"; a chave própria saiu, e o validador confere que a
# Classic do upstream continua fazendo isso.
#
# Fica uma coisa que a Classic do upstream não faz: os números nos cantos dos botões (fila, recarga,
# preço) são medidos pela escala do HUD, inclusive para decidir quando tirar o "$" ou o "s" por
# falta de espaço, mas a barra Classic é montada na escala uniforme, maior. Na Classic as duas
# medidas voltam à escala da barra em que os botões estão.

def fail(msg):
    raise SystemExit("STAGE16: " + msg)

# Quantas vezes o W3DPushButton.cpp do upstream mede uma marcação pela escala da barra; o validador
# confere o mesmo número, para que um uso novo não fique de fora sem ninguém notar.
BADGE_SCALE_USES = 2

def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8-sig")
    n = text.count(old)
    if n != 1:
        fail(f"{path}: esperado 1 bloco, encontrado {n}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")

def replace_all(path, old, new, count):
    text = path.read_text(encoding="utf-8-sig")
    n = text.count(old)
    if n != count:
        fail(f"{path}: esperado {count} ocorrências de {old!r}, encontrado {n}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="")

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

    # Os números nos cantos dos botões na escala da barra Classic, a uniforme.
    button = code / "GameEngineDevice" / "Source" / "W3DDevice" / "GameClient" / "GUI" / "Gadget" / "W3DPushButton.cpp"
    replace_once(
        button,
        "extern Real ControlBarHudScale( void );\n",
        "extern Real ControlBarHudScale( void );\n"
        "extern Real ControlBarUniformScale( void );\n"
        "\n"
        "/** PT-BR edition: the scale of the bar a button stands on.  The Classic bar is laid out at the\n"
        "\t* uniform scale, so its markings are sized and judged cramped by it; the page's bar by the HUD's. */\n"
        "static Real badgeBarScale( void )\n"
        "{\n"
        "\tif( TheGlobalData != NULL && TheGlobalData->isClassicUI() )\n"
        "\t\treturn ControlBarUniformScale();\n"
        "\treturn ControlBarHudScale();\n"
        "}\n",
    )
    # Todo lugar que mede uma marcação pela escala da barra: o tamanho da fonte (getBadgeFont) e o
    # teste que tira o "$" ou o "s" quando a marcação não cabe (drawBadge).
    replace_all(button, "designPoints * ControlBarHudScale()", "designPoints * badgeBarScale()", BADGE_SCALE_USES)

    print("STAGE16 APPLY PASS")
    print("Classic bar's button markings at the bar's uniform scale; the rest of Classic is upstream's.")

if __name__ == "__main__":
    main()
