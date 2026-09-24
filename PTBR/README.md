# Zero Hour Reforged — Português (Brasil)

Integração PT-BR para `CnCGeneralsZH-Reforged`.

O Git contém o overlay textual (`Generals.str`, `Language.ini`), patchers, validadores e workflow.
zlib, GameSpy, LZH-Light e o min-dx8-sdk são instalados automaticamente em versões fixadas antes
da compilação x64.

Os BIKs e texturas PT-BR não ficam no Git. Eles são opcionais no CI: se um bundle privado for
configurado, entram no artifact; caso contrário, a compilação continua e o jogo usa fallback de mídia.

Veja `SETUP.md`.
