# Setup do fork

Configure estes GitHub Actions secrets:

- `LZH_LIGHT_URL`
- `LZH_LIGHT_SHA256`
- `LZH_LIGHT_TOKEN` (opcional)
- `PTBR_MEDIA_URL`
- `PTBR_MEDIA_SHA256`
- `PTBR_MEDIA_TOKEN` (opcional)

Depois execute **Actions → PT-BR Win32 Build → Run workflow**.

## Bundle LZH-Light

Use `tools/make_lzh_bundle.py` em um checkout que possua o LZH-Light no layout esperado.

## Bundle de mídia PT-BR

Use `tools/make_ptbr_media_bundle.py` apontando para o diretório `PortugueseBrazil` completo.
Esse bundle contém somente os 4 BIKs e 4 texturas localizadas. Não inclua arquivos `.big`.
