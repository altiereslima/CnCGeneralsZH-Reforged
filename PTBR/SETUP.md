# Setup do fork

Configure estes GitHub Actions secrets:

- `PTBR_MEDIA_URL`
- `PTBR_MEDIA_SHA256`
- `PTBR_MEDIA_TOKEN` (opcional)

Depois execute **Actions → PT-BR Win32 Build → Run workflow**.

## Dependências de build

zlib 1.1.4, GameSpy SDK e LZH-Light 1.0 são obtidos automaticamente pelo workflow em versões/commits fixados.

## Bundle de mídia PT-BR

Use `tools/make_ptbr_media_bundle.py` apontando para o diretório `PortugueseBrazil` completo.
Esse bundle contém somente os 4 BIKs e 4 texturas localizadas. Não inclua arquivos `.big`.
