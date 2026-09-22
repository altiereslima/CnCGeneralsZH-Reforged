# Setup do fork

Nenhum segredo é necessário para compilar a tradução textual.

O workflow instala automaticamente, em versões/commits fixados:

- zlib 1.1.4;
- GameSpy SDK;
- LZH-Light 1.0;
- min-dx8-sdk (cabeçalhos DirectX 8 que o CMake exige para configurar).

O build é x64 (o upstream removeu o Win32 na v2.0.0). Som e vídeo usam XAudio2 e FFmpeg; as DLLs
do FFmpeg já vêm versionadas no repositório e entram no artifact. Miles e Bink não são mais usados.
A arte ampliada do Reforged (`art-latest`, mais de 1 GB) não é baixada no CI.

A mídia localizada é opcional. Para incluí-la no artifact, configure:

- `PTBR_MEDIA_URL`
- `PTBR_MEDIA_SHA256`
- `PTBR_MEDIA_TOKEN` (opcional)

Se URL e SHA estiverem ambos vazios, o build continua normalmente e o runtime usa fallback de mídia.
Se somente um deles estiver configurado, o workflow para para denunciar configuração incompleta.

Todo push no `main` dispara o build automaticamente.
