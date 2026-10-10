#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

# Tela de abertura (o splash do Install_Final.bmp), trazida do commit ce074230 do fork 600rr
# ("support dynamic resolution for startup splash screen"), que o upstream não tem.
#
# O upstream desenha o splash sempre como 800x600 e só o mantém na tela em tela cheia exclusiva e
# em janela: em tela cheia sem bordas a janela fica vazia até o motor assumir. A edição PT-BR:
# - lê o tamanho real da imagem e dá esse tamanho à janela de abertura e ao desenho, para um
#   Install_Final.bmp maior que o original;
# - mantém o splash pintado também no modo sem bordas e em janela até o WM_SIZE em que o motor toma
#   a janela, e só então solta a imagem (ou ao sair, se o motor nunca chegou a redimensionar).
#
# A imagem é a da edição: PTBR/payload/GeneralsMD/Code/Data/Install_Final.bmp, widescreen, entra
# no lugar da do upstream em Data/, de onde o CMake a copia para o lado do generals.exe.

def fail(msg):
    raise SystemExit("STAGE18: " + msg)

def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8-sig")
    n = text.count(old)
    if n != 1:
        fail(f"{path}: esperado 1 bloco, encontrado {n}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")

# A cópia do CMake que leva Data/Install_Final.bmp para o Run; o validador confere.
CMAKE_SPLASH_COPY = (
    "  COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
    "          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Install_Final.bmp\n"
    "          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/\n"
)

# Âncoras do WinMain.cpp do upstream; o validador confere cada uma.
RESIZE = (
    "\t\t\t\tif (!gInitializing)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tgDoPaint = false;\n"
)
PAINT = "\t\t\t\t\t\t::BitBlt(dc, 0, 0, DEFAULT_XRESOLUTION, DEFAULT_YRESOLUTION, tmpDC, 0, 0, SRCCOPY);\n"
START_SIZE = (
    "\tInt startWidth = DEFAULT_XRESOLUTION,\n"
    "\t\t\tstartHeight = DEFAULT_YRESOLUTION;\n"
)
WINDOWED_SIZE = (
    "\tif (runWindowed) {\n"
    "\t\t// Makes the normal debug 800x600 window center in the screen.\n"
)
STOP_PAINT = (
    "\tgInitializing = false;\n"
    "\tif (!runWindowed) {\n"
    "\t\tgDoPaint = false;\n"
)
EARLY_RELEASE = (
    "\t\tif( initializeAppWindows( hInstance, nCmdShow, ApplicationIsWindowed) == false )\n"
    "\t\t\treturn 0;\n"
    "\n"
    "\t\tif (gLoadScreenBitmap!=NULL) {\n"
)
EXIT = (
    "\tTheMemoryPoolCriticalSection = NULL;\n"
    "\n"
    "\treturn 0;\n"
    "\n"
    "}  // end WinMain\n"
)
ANCHORS = {
    "WM_SIZE stop painting": RESIZE,
    "WM_PAINT splash blit": PAINT,
    "start size": START_SIZE,
    "windowed 800x600": WINDOWED_SIZE,
    "stop painting after create": STOP_PAINT,
    "splash released after create": EARLY_RELEASE,
    "WinMain exit": EXIT,
}

def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    code = repo / "GeneralsMD" / "Code"
    if not code.exists():
        fail("aponte para a raiz de CnCGeneralsZH-Reforged")

    sys.path.insert(0, str(Path(__file__).parent))
    import apply_stage17
    old_argv = sys.argv[:]
    try:
        sys.argv = [str(Path(__file__)), str(repo)]
        apply_stage17.main()
    finally:
        sys.argv = old_argv

    winmain = code / "Main" / "WinMain.cpp"

    # 1) O motor tomou a janela: o splash não volta a ser pintado, então a imagem pode ir.
    replace_once(winmain, RESIZE,
        RESIZE +
        "\t\t\t\t\t// PT-BR edition: the splash stays up until this resize, so let it go here.\n"
        "\t\t\t\t\tif (gLoadScreenBitmap != NULL) {\n"
        "\t\t\t\t\t\t::DeleteObject(gLoadScreenBitmap);\n"
        "\t\t\t\t\t\tgLoadScreenBitmap = NULL;\n"
        "\t\t\t\t\t}\n")

    # 2) Desenha a imagem no tamanho dela.
    replace_once(winmain, PAINT,
        "\t\t\t\t\t\t// PT-BR edition: blit the splash at its own size, whatever Install_Final.bmp is.\n"
        "\t\t\t\t\t\tBITMAP splashInfo;\n"
        "\t\t\t\t\t\tInt splashWidth = DEFAULT_XRESOLUTION;\n"
        "\t\t\t\t\t\tInt splashHeight = DEFAULT_YRESOLUTION;\n"
        "\t\t\t\t\t\tif (::GetObject(gLoadScreenBitmap, sizeof(BITMAP), &splashInfo)) {\n"
        "\t\t\t\t\t\t\tsplashWidth = splashInfo.bmWidth;\n"
        "\t\t\t\t\t\t\tsplashHeight = splashInfo.bmHeight;\n"
        "\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\t::BitBlt(dc, 0, 0, splashWidth, splashHeight, tmpDC, 0, 0, SRCCOPY);\n")

    # 3) A janela de abertura nasce do tamanho da imagem.
    replace_once(winmain, START_SIZE,
        START_SIZE +
        "\n"
        "\t// PT-BR edition: the opening window takes the splash's own size.\n"
        "\tif (gLoadScreenBitmap != NULL) {\n"
        "\t\tBITMAP splashInfo;\n"
        "\t\tif (::GetObject(gLoadScreenBitmap, sizeof(BITMAP), &splashInfo) &&\n"
        "\t\t\t\tsplashInfo.bmWidth > 0 && splashInfo.bmHeight > 0) {\n"
        "\t\t\tstartWidth = splashInfo.bmWidth;\n"
        "\t\t\tstartHeight = splashInfo.bmHeight;\n"
        "\t\t}\n"
        "\t}\n")

    # 4) Em janela, o 800x600 fica só para quando não há splash.
    replace_once(winmain, WINDOWED_SIZE,
        "\tif (runWindowed && gLoadScreenBitmap == NULL) {\n"
        "\t\t// Makes the normal debug 800x600 window center in the screen.\n")

    # 5) Sem bordas também continua pintando o splash até o motor assumir.
    replace_once(winmain, STOP_PAINT,
        "\tgInitializing = false;\n"
        "\tif (!runWindowed && !ApplicationIsBorderless) {\t// PT-BR edition: borderless keeps the splash too\n"
        "\t\tgDoPaint = false;\n")

    # 6) Só a tela cheia exclusiva solta a imagem logo depois de criar a janela.
    replace_once(winmain, EARLY_RELEASE,
        "\t\tif( initializeAppWindows( hInstance, nCmdShow, ApplicationIsWindowed) == false )\n"
        "\t\t\treturn 0;\n"
        "\n"
        "\t\t// PT-BR edition: windowed and borderless keep the splash until WM_SIZE, when W3D takes over.\n"
        "\t\tif (!ApplicationIsWindowed && !ApplicationIsBorderless && gLoadScreenBitmap != NULL) {\n")

    # 7) E ao sair, se o motor nunca redimensionou a janela.
    replace_once(winmain, EXIT,
        "\tTheMemoryPoolCriticalSection = NULL;\n"
        "\n"
        "\tif (gLoadScreenBitmap != NULL) {\n"
        "\t\t::DeleteObject(gLoadScreenBitmap);\n"
        "\t\tgLoadScreenBitmap = NULL;\n"
        "\t}\n"
        "\n"
        "\treturn 0;\n"
        "\n"
        "}  // end WinMain\n")

    # 8) A imagem da edição no lugar da do upstream.
    src = Path(__file__).resolve().parents[1] / "payload" / "GeneralsMD" / "Code" / "Data" / "Install_Final.bmp"
    if not src.is_file():
        fail(f"{src}: imagem de abertura da edição ausente")
    shutil.copyfile(src, code / "Data" / "Install_Final.bmp")

    print("STAGE18 APPLY PASS")
    print("The edition's widescreen splash, at its own size, kept up in borderless and windowed until W3D takes over.")

if __name__ == "__main__":
    main()
