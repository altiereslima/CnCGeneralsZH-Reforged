# Setup do fork

Nenhum segredo é necessário para compilar a tradução textual.

O workflow instala automaticamente, em versões/commits fixados:

- zlib 1.1.4;
- GameSpy SDK;
- LZH-Light 1.0;
- stubs públicos do Miles usados apenas para gerar a import library.

A mídia localizada é opcional. Para incluí-la no artifact, configure:

- `PTBR_MEDIA_URL`
- `PTBR_MEDIA_SHA256`
- `PTBR_MEDIA_TOKEN` (opcional)

Se URL e SHA estiverem ambos vazios, o build continua normalmente e o runtime usa fallback de mídia.
Se somente um deles estiver configurado, o workflow para para denunciar configuração incompleta.

Todo push no `main` dispara o build automaticamente.
