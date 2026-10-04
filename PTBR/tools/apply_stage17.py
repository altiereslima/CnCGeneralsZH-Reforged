#!/usr/bin/env python3
from pathlib import Path
import sys

# Abrir pelo generals.exe. Desde a v2.4.0 o executável do upstream se recusa a abrir sem o -rk7 que o
# launcher dele passa ("Please start Zero Hour Reforged from its launcher."), a não ser com
# -multiInstance ou numa execução sem ninguém olhando (-headless, ZH_UNATTENDED). O launcher não está
# no repositório e o build PT-BR não o inclui: quem joga copia o build por cima da instalação e abre o
# generals.exe, como em toda versão até a 2.3. A edição PT-BR tira a checagem.

def fail(msg):
    raise SystemExit("STAGE17: " + msg)

LAUNCHER_CHECK = (
    "\tif (findEarlyCommandLineOption( L\"-rk7\" ) == NULL &&\n"
    "\t\t\tfindEarlyCommandLineOption( L\"-multiInstance\" ) == NULL &&\n"
    "\t\t\t!isUnattendedProcess())\n"
    "\t{\n"
    "\t\t::MessageBoxA( NULL, \"Please start Zero Hour Reforged from its launcher.\", \"Zero Hour Reforged\", MB_OK | MB_ICONINFORMATION );\n"
    "\t\treturn 1;\n"
    "\t}\n"
)

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
    import apply_stage16
    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage16.main()
    finally:
        sys.argv = old_argv

    replace_once(
        code / "Main" / "WinMain.cpp",
        LAUNCHER_CHECK,
        "\t// PT-BR edition: started straight from generals.exe, as every build before 2.4.0 was.  The\n"
        "\t// edition ships without upstream's launcher, which is what passes -rk7.\n",
    )

    print("STAGE17 APPLY PASS")
    print("generals.exe starts without the launcher's -rk7.")

if __name__ == "__main__":
    main()
