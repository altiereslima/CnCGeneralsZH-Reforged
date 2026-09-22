#!/usr/bin/env python3
from pathlib import Path
import sys

# O upstream escreve os três degraus de IA do lobby ("Easy AI", "Medium AI", "Hard AI") direto
# no código, fora do GameText, então nenhuma tradução os alcança. A edição PT-BR passa os três
# por rótulos próprios do fork (GUI:SlotEasyAI etc.). Um idioma sem esses rótulos, e os testes,
# que rodam sem TheGameText, continuam com os nomes em inglês. Na rede e no replay a IA viaja
# como código ("C" + letra), não pelo nome, então jogadores em idiomas diferentes seguem juntos.

def fail(msg):
    raise SystemExit("STAGE14: " + msg)

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
    import apply_stage13
    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage13.main()
    finally:
        sys.argv = old_argv

    info = code / "GameEngine" / "Source" / "GameNetwork" / "GameInfo.cpp"
    replace_once(
        info,
        "/** What a seat is called wherever one is listed: the lobby's drop-down, the seat itself, the game\n",
        "// PT-BR edition: a translation names the AI rungs through labels of the fork's own. A language\n"
        "// without them, and a test with no TheGameText, keeps the English names SlotStateName passes.\n"
        "static UnicodeString AIRungName( const char *label, const WideChar *english )\n"
        "{\n"
        "\tif( TheGameText != NULL )\n"
        "\t{\n"
        "\t\tBool exists = FALSE;\n"
        "\t\tUnicodeString name = TheGameText->fetch( label, &exists );\n"
        "\t\tif( exists )\n"
        "\t\t\treturn name;\n"
        "\t}\n"
        "\treturn UnicodeString( english );\n"
        "}\n"
        "\n"
        "/** What a seat is called wherever one is listed: the lobby's drop-down, the seat itself, the game\n",
    )
    replace_once(
        info,
        "\t\tcase SLOT_EASY_AI:\t\t\treturn UnicodeString( L\"Easy AI\" );\n"
        "\t\tcase SLOT_MED_AI:\t\t\t\treturn UnicodeString( L\"Medium AI\" );\n"
        "\t\tcase SLOT_BRUTAL_AI:\t\treturn UnicodeString( L\"Hard AI\" );\n",
        "\t\tcase SLOT_EASY_AI:\t\t\treturn AIRungName( \"GUI:SlotEasyAI\", L\"Easy AI\" );\n"
        "\t\tcase SLOT_MED_AI:\t\t\t\treturn AIRungName( \"GUI:SlotMediumAI\", L\"Medium AI\" );\n"
        "\t\tcase SLOT_BRUTAL_AI:\t\treturn AIRungName( \"GUI:SlotHardAI\", L\"Hard AI\" );\n",
    )

    print("STAGE14 APPLY PASS")
    print("Lobby AI difficulty names go through GUI:SlotEasyAI/SlotMediumAI/SlotHardAI.")

if __name__ == "__main__":
    main()
