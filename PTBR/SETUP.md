# Setup do fork

Nenhum segredo é necessário para compilar a tradução textual.

O workflow instala automaticamente, em versões/commits fixados:

- zlib 1.1.4 (do zlib.net, ou do espelho do projeto libpng no SourceForge quando o zlib.net
  falha; os dois passam pelo mesmo MD5);
- GameSpy SDK;
- LZH-Light 1.0;
- min-dx8-sdk (cabeçalhos DirectX 8 que o CMake exige para configurar);
- litehtml e nanosvg, que a interface HTML do upstream usa, com o `litehtml-parsed-css.patch`;
- NumPy e Pillow, que o teste `order_cameos_selfcheck` do ctest importa.

O que a edição PT-BR muda no jogo, e a chave do `Options.ini` que volta ao comportamento do upstream:

- idioma inicial Português (Brasil); a opção de idioma do menu continua valendo;
- barra de comando com as placas texturizadas e menu Esc original: `ClassicInterface = no`
  traz as páginas HTML do upstream;
- caixa do relógio e FPS no canto escondida: a opção "Caixa de Relógio e FPS" do menu
  (`ShowNetBox = yes`) mostra;
- nomes das IAs do lobby traduzidos.

Os resultados do runner ficam em `PTBR/PTBR_STAGE15_RESULTS/`.

O inglês original não está no repositório: o jogo o lê de `Data\English\Generals.csf`, dentro do
`EnglishZH.big` da instalação do Zero Hour. Para conferir a tradução contra ele, rode
`PTBR/tools/conferir_ingles.py` na pasta do jogo (ou passe o caminho dela). O script gera
`conferencia_ingles.txt` com números, formatos, títulos e quebras de linha que não batem e os
rótulos sem tradução, e não precisa de nada além do Python.

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
