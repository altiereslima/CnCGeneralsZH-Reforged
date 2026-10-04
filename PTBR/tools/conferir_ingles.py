#!/usr/bin/env python3
"""Confere a tradução PT-BR contra o inglês do próprio jogo.

O inglês não está no repositório: o jogo lê o texto original da EA de Data\\English\\Generals.csf,
que vem dentro dos .big da instalação do Zero Hour (EnglishZH.big). O repositório só traz o que o
Reforged acrescentou (Data\\Patch.str), a tradução turca e a nossa. Este script abre os .big do
seu jogo, monta o inglês que o jogo mostra (o CSF, com o Patch.str por cima) e compara com o
Generals.str PT-BR.

Uso (Windows): ponha este arquivo na pasta do jogo, onde fica o generals.exe, e dê dois cliques.
Ou: python conferir_ingles.py "C:\\caminho\\do\\Zero Hour"

Gera conferencia_ingles.txt (o relatório) e ingles_do_jogo.str (todo o inglês, para consulta).
Só usa a biblioteca padrão do Python 3.8+.
"""
from pathlib import Path
import argparse, re, struct, sys, urllib.request
from datetime import datetime

RAW = "https://raw.githubusercontent.com/altiereslima/CnCGeneralsZH-Reforged/main/"
PTBR_URL = RAW + "PTBR/payload/GeneralsMD/Code/Data/PortugueseBrazil/Generals.str"
PATCH_URL = RAW + "GeneralsMD/Code/Data/Patch.str"
CSF_ENTRY = "data\\english\\generals.csf"
REG_KEY = r"SOFTWARE\Electronic Arts\EA Games\Command and Conquer Generals Zero Hour"
COMMON_FOLDERS = [
    r"C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour",
    r"C:\Program Files\EA Games\Command & Conquer Generals Zero Hour",
    r"C:\Program Files (x86)\EA Games\Command & Conquer Generals Zero Hour",
    r"C:\Program Files (x86)\Origin Games\Command and Conquer Generals Zero Hour\Command and Conquer Generals Zero Hour",
    r"C:\Program Files (x86)\EA Games\Command & Conquer The First Decade\Command & Conquer Generals Zero Hour",
]
# Os dois textos que a revisão de outubro de 2026 não conseguiu confirmar sem o inglês.
IN_QUESTION = ["GC_CHINABOSS:Resources", "CONTROLBAR:TooltipFireSpySatScan"]

def fail(msg):
    raise SystemExit("CONFERIR_INGLES: " + msg)

# ---------------------------------------------------------------- arquivos .big e .csf

def big_entries(path):
    """Diretório de um .big: {caminho em minúsculas: (offset, tamanho)}. Mesmo layout que o
    Win32BIGFileSystem lê: "BIGF", tamanho, número de arquivos e início dos dados em big-endian,
    e a partir de 0x10 cada entrada com offset, tamanho e caminho terminado em zero."""
    size_on_disk = Path(path).stat().st_size
    with open(path, "rb") as f:
        head = f.read(16)
        if len(head) < 16 or head[:4] not in (b"BIGF", b"BIG4"):
            return {}
        count, first = struct.unpack(">II", head[8:16])
        # o início dos dados marca o fim do diretório; se vier estranho, um diretório nunca passa de 16 MB
        table = f.read(first - 16 if 16 < first <= size_on_disk else 16 * 1024 * 1024)
    out = {}
    pos = 0
    for _ in range(count):
        if pos + 8 > len(table):
            break
        offset, size = struct.unpack(">II", table[pos:pos + 8])
        end = table.find(b"\0", pos + 8)
        if end < 0:
            break
        name = table[pos + 8:end].decode("latin-1").replace("/", "\\").lower()
        out[name] = (offset, size)
        pos = end + 1
    return out

def big_read(path, offset, size):
    with open(path, "rb") as f:
        f.seek(offset)
        return f.read(size)

def parse_csf(data):
    """{rótulo: texto} de um Generals.csf, como o GameTextManager::parseCSF lê: cabeçalho de seis
    inteiros, depois " LBL" com a contagem de textos e o nome, e cada texto " RTS" ou "WRTS" em
    UTF-16 com os bits invertidos. Vale o primeiro texto de cada rótulo."""
    if len(data) < 24 or data[:4] != b" FSC":
        fail("o arquivo não é um Generals.csf")
    version, num_labels, _strings, _skip, langid = struct.unpack("<iiiii", data[4:24])
    pos = 24
    out = {}
    while pos + 12 <= len(data):
        tag = data[pos:pos + 4]
        if tag != b" LBL":
            break
        count, length = struct.unpack("<ii", data[pos + 4:pos + 12])
        pos += 12
        label = data[pos:pos + length].decode("latin-1")
        pos += length
        for n in range(count):
            tag = data[pos:pos + 4]
            if tag not in (b" RTS", b"WRTS"):
                fail(f"Generals.csf corrompido perto de {label}")
            (length,) = struct.unpack("<i", data[pos + 4:pos + 8])
            pos += 8
            raw = data[pos:pos + length * 2]
            pos += length * 2
            if n == 0:
                units = struct.unpack(f"<{length}H", raw)
                text = "".join(chr(~u & 0xFFFF) for u in units)
                out[label] = text.strip()
            if tag == b"WRTS":
                (length,) = struct.unpack("<i", data[pos:pos + 4])
                pos += 4 + length
    return out, langid, num_labels

def find_csf(folder):
    """O Generals.csf inglês que o jogo usaria nesta pasta: um arquivo solto vence os .big; dos
    .big, um Patch*.big carregado depois vence (o último em ordem alfabética), e entre os outros
    vence o primeiro em ordem alfabética, como no Win32BIGFileSystem::init."""
    folder = Path(folder)
    for p in folder.glob("[Dd]ata/[Ee]nglish/*"):
        if p.name.lower() == "generals.csf":
            return p.read_bytes(), str(p)
    bigs = sorted((p for p in folder.iterdir() if p.suffix.lower() == ".big"), key=lambda p: p.name.lower())
    found, others = [], set()
    for big in bigs:
        try:
            entries = big_entries(big)
        except OSError:
            continue
        for name in entries:
            m = re.fullmatch(r"data\\([^\\]+)\\generals\.csf", name)
            if m and m.group(1) != "english":
                others.add(m.group(1))
        if CSF_ENTRY in entries:
            found.append((big, entries[CSF_ENTRY]))
    if not found:
        return None, sorted(others)
    patches = [x for x in found if x[0].name.lower().startswith("patch")]
    big, (offset, size) = patches[-1] if patches else found[0]
    return big_read(big, offset, size), f"{big} -> Data\\English\\Generals.csf"

# ---------------------------------------------------------------- arquivos .str

def parse_str(text):
    """{rótulo: texto cru entre as aspas} de um .str (rótulo, linha entre aspas, END)."""
    out = {}
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if not s or s.startswith("//"):
            i += 1
            continue
        j = i + 1
        vals = []
        while j < len(lines) and lines[j].strip() != "END":
            vals.append(lines[j].strip())
            j += 1
        v = " ".join(vals)
        out[s] = v[1:v.rfind('"')] if v.startswith('"') and v.count('"') >= 2 else v
        i = j + 1
    return out

def decode_str(raw):
    """O texto que o jogo mostra a partir do cru de um .str: \\n vira quebra de linha, \\" aspas."""
    out, k = [], 0
    while k < len(raw):
        ch = raw[k]
        if ch == "\\" and k + 1 < len(raw):
            nxt = raw[k + 1]
            out.append({"n": "\n", "t": "\t"}.get(nxt, nxt))
            k += 2
            continue
        out.append(" " if ch.isspace() else ch)
        k += 1
    return "".join(out)

def encode_str(text):
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\r\n", "\n").replace("\n", "\\n")

def load_text(arg, repo_copy, url, installed_copy, what):
    """O arquivo mais novo que houver: o indicado, o do repositório (quando o script roda de dentro
    dele), o do GitHub e, sem internet, o que está instalado na pasta do jogo."""
    if arg:
        return Path(arg).read_text(encoding="utf-8-sig"), arg
    if repo_copy.is_file():
        return repo_copy.read_text(encoding="utf-8-sig"), str(repo_copy)
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            return r.read().decode("utf-8-sig"), url
    except Exception as e:
        if installed_copy.is_file():
            return installed_copy.read_text(encoding="utf-8-sig"), str(installed_copy)
        print(f"Aviso: não achei o {what} nem consegui baixá-lo ({e}).")
        return None, None

# ---------------------------------------------------------------- comparação

SPEC = re.compile(r"%%|%[-+#0]*\d*(?:\.\d+)?(?:hs|ls|h|l|I64)?[diouxXeEfgGcCsSp]")
NUM = re.compile(r"\d+(?:[.,]\d+)*")
EN_TITLE = re.compile(r"^[A-Z][A-Z0-9 #'’.&/-]*[A-Z0-9]:(?!\d)")
PT_TITLE = re.compile(r"^[A-ZÀ-ÖØ-Þ0-9º#][A-ZÀ-ÖØ-Þ0-9º#ª '’.&/-]*:(?!\d)")

def norm(text):
    return "\n".join(" ".join(line.split()) for line in text.split("\n")).strip()

def edge_breaks(text):
    t = text.replace(" ", "")
    return len(t) - len(t.lstrip("\n")), len(t) - len(t.rstrip("\n"))

def numbers(text):
    return sorted(n.replace(".", "").replace(",", "") for n in NUM.findall(text))

def compare(english, ptbr):
    pt_low = {k.lower(): k for k in ptbr}
    sections = {k: [] for k in ("spec", "num", "title", "edge", "missing", "same")}
    hidden = 0
    for label in sorted(english, key=str.lower):
        en = english[label]
        key = pt_low.get(label.lower())
        if key is None:
            if en.startswith("*"):
                hidden += 1	# legendas que o próprio inglês esconde (ScriptActions::doSpeechPlay)
            elif en.strip():
                sections["missing"].append((label, en, None))
            continue
        pt = decode_str(ptbr[key])
        item = (label, en, pt)
        if sorted(SPEC.findall(en)) != sorted(SPEC.findall(pt)):
            sections["spec"].append(item)
        if numbers(en) != numbers(pt):
            sections["num"].append(item)
        if EN_TITLE.match(en.lstrip(" \n")) and not PT_TITLE.match(pt.lstrip(" \n")):
            sections["title"].append(item)
        if edge_breaks(en) != edge_breaks(pt):
            sections["edge"].append(item)
        if norm(en) == norm(pt) and sum(c.isalpha() for c in en) >= 4:
            sections["same"].append(item)
    return sections, hidden

TITLES = [
    ("spec", "Formatos (%d, %s, %%) diferentes: podem trocar ou cortar valores no jogo"),
    ("num", "Números diferentes (tempos, preços, quantidades, porcentagens)"),
    ("title", "Título em maiúsculas que o inglês tem e o PT-BR não (ex.: HINT: -> DICA:)"),
    ("edge", "Quebras de linha no começo ou no fim diferentes"),
    ("missing", "Sem tradução: aparecem em inglês no jogo, se o jogo usar o rótulo"),
    ("same", "Iguais ao inglês (nomes próprios são normais aqui)"),
]

def entry(label, en, pt):
    lines = [label, "  EN: " + encode_str(en)]
    if pt is not None:
        lines.append("  PT: " + encode_str(pt))
    return "\n".join(lines)

# ---------------------------------------------------------------- pasta do jogo

def registry_folders():
    try:
        import winreg
    except ImportError:
        return []
    out = []
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for view in (0, winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(hive, REG_KEY, 0, winreg.KEY_READ | view) as k:
                    out.append(winreg.QueryValueEx(k, "InstallPath")[0])
            except OSError:
                pass
    return out

def ask_folder():
    try:
        import tkinter
        from tkinter import filedialog
        root = tkinter.Tk()
        root.withdraw()
        chosen = filedialog.askdirectory(title="Escolha a pasta do Zero Hour (onde ficam os .big)")
        root.destroy()
        return chosen or None
    except Exception:
        try:
            typed = input("Cole o caminho da pasta do Zero Hour (onde ficam os .big): ").strip().strip('"')
        except EOFError:
            return None
        return typed or None

def has_bigs(folder):
    try:
        return folder.is_dir() and any(p.suffix.lower() == ".big" for p in folder.iterdir())
    except OSError:
        return False

def game_folder(arg):
    if arg:
        if not Path(arg).is_dir():
            fail(f"a pasta {arg} não existe")
        return [Path(arg)]
    here = Path(__file__).resolve().parent
    seen, out = set(), []
    for c in [here, Path.cwd()] + [Path(p) for p in registry_folders()] + [Path(p) for p in COMMON_FOLDERS]:
        try:
            key = str(c.resolve()).lower()
        except OSError:
            continue
        if key not in seen and has_bigs(c):
            seen.add(key)
            out.append(c)
    if not out:
        chosen = ask_folder()
        if chosen:
            out.append(Path(chosen))
    return out

def output_folder(arg):
    for d in ([Path(arg)] if arg else [Path(__file__).resolve().parent, Path.cwd(), Path.home()]):
        try:
            d.mkdir(parents=True, exist_ok=True)
            probe = d / ".conferir_ingles_teste"
            probe.write_text("ok", encoding="utf-8")
            probe.unlink()
            return d
        except OSError:
            continue
    fail("não consegui escrever o relatório em nenhuma pasta")

# ---------------------------------------------------------------- principal

def main():
    ap = argparse.ArgumentParser(description="Confere o PT-BR contra o inglês do jogo instalado.")
    ap.add_argument("jogo", nargs="?", help="pasta do Zero Hour, um .big ou um Generals.csf")
    ap.add_argument("--ptbr", help="Generals.str PT-BR (padrão: o da pasta do jogo, do repositório ou do GitHub)")
    ap.add_argument("--patch", help="Patch.str do Reforged (padrão: o da pasta do jogo, do repositório ou do GitHub)")
    ap.add_argument("--saida", help="pasta onde escrever o relatório")
    args = ap.parse_args()

    csf_data, source, folder = None, None, None
    target = Path(args.jogo) if args.jogo else None
    if target and target.is_file() and target.suffix.lower() == ".csf":
        csf_data, source, folder = target.read_bytes(), str(target), target.parent
    elif target and target.is_file() and target.suffix.lower() == ".big":
        entries = big_entries(target)
        if CSF_ENTRY not in entries:
            fail(f"{target} não tem Data\\English\\Generals.csf")
        csf_data, source, folder = big_read(target, *entries[CSF_ENTRY]), f"{target} -> Data\\English\\Generals.csf", target.parent
    else:
        languages = set()
        for candidate in game_folder(args.jogo):
            data, where = find_csf(candidate)
            if data is not None:
                csf_data, source, folder = data, where, candidate
                break
            languages.update(where)
        if csf_data is None:
            if languages:
                fail("o inglês não está nesta instalação; ela tem: " + ", ".join(sorted(languages)))
            fail("não achei a pasta do Zero Hour; rode de novo passando o caminho dela")

    english, langid, _declared = parse_csf(csf_data)
    pkg = Path(__file__).resolve().parent.parent	# PTBR/, quando o script roda de dentro do repositório
    patch_text, patch_src = load_text(args.patch, pkg.parent / "GeneralsMD/Code/Data/Patch.str", PATCH_URL,
                                      folder / "Data" / "Patch.str", "Patch.str")
    ptbr_text, ptbr_src = load_text(args.ptbr, pkg / "payload/GeneralsMD/Code/Data/PortugueseBrazil/Generals.str", PTBR_URL,
                                    folder / "Data" / "PortugueseBrazil" / "Generals.str", "Generals.str PT-BR")
    if ptbr_text is None:
        fail("sem o Generals.str PT-BR não há o que comparar; passe --ptbr com o caminho dele")

    # O inglês que o jogo mostra: o CSF, com o Patch.str do Reforged por cima.
    shown = dict(english)
    patched = 0
    if patch_text:
        low = {k.lower(): k for k in shown}
        for label, raw in parse_str(patch_text).items():
            shown.pop(low.get(label.lower(), label), None)
            shown[label] = decode_str(raw)
            patched += 1
    ptbr = parse_str(ptbr_text)
    sections, hidden = compare(shown, ptbr)

    out = output_folder(args.saida)
    report = out / "conferencia_ingles.txt"
    dump = out / "ingles_do_jogo.str"
    low_shown = {k.lower(): k for k in shown}
    low_pt = {k.lower(): k for k in ptbr}
    lines = [
        "Conferência do PT-BR contra o inglês do jogo",
        f"Gerado em {datetime.now():%Y-%m-%d %H:%M}",
        f"Inglês: {source} ({len(english)} rótulos, idioma {langid})",
        f"Patch.str: {patch_src or 'não encontrado'} ({patched} rótulos por cima do CSF)",
        f"PT-BR: {ptbr_src} ({len(ptbr)} rótulos)",
        "",
        "== Os textos em dúvida ==",
    ]
    for label in IN_QUESTION:
        en = shown.get(low_shown.get(label.lower(), ""), None)
        pt = ptbr.get(low_pt.get(label.lower(), ""), None)
        lines.append(entry(label, en if en is not None else "<não existe no inglês>", decode_str(pt) if pt is not None else "<não existe no PT-BR>"))
    summary = []
    for key, title in TITLES:
        items = sections[key]
        summary.append(f"{len(items):5}  {title}")
        lines += ["", f"== {title}: {len(items)} ==", ""]
        lines += [entry(*item) + "\n" for item in items]
    if hidden:
        lines += ["", f"({hidden} rótulos sem tradução que o próprio inglês esconde, começando com *, ficaram de fora)"]
    report.write_text("\n".join(lines) + "\n", encoding="utf-8-sig")
    dump.write_text("".join(f"{k}\n\"{encode_str(v)}\"\nEND\n\n" for k, v in sorted(shown.items(), key=lambda kv: kv[0].lower())), encoding="utf-8-sig")

    print("Inglês lido de:", source)
    print("\n".join(summary))
    print("\nRelatório:", report)
    print("Todo o inglês:", dump)

if __name__ == "__main__":
    # Com dois cliques a janela fecharia levando a mensagem junto: mostra e espera o Enter.
    interactive = len(sys.argv) == 1
    code = 0
    try:
        main()
    except SystemExit as e:
        if e.code not in (None, 0):
            print(e.code, file=sys.stderr)
            code = 1
    except Exception:
        import traceback
        traceback.print_exc()
        code = 1
    if interactive:
        try:
            input("\nAperte Enter para fechar...")
        except EOFError:
            pass
    sys.exit(code)
