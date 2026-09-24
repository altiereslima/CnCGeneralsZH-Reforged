# Brazilian Portuguese localization

PT-BR integration for CnCGeneralsZH-Reforged.

This directory contains only the localization data and the small build scripts required to apply
and validate it. The original English data remains untouched.

## Layout

- `Data/PortugueseBrazil/` — translated text and language configuration.
- `scripts/apply.py` — applies the source integration.
- `scripts/dependencies.py` — installs public build dependencies at pinned revisions.
- `scripts/validate.py` — validates source integration and build prerequisites.
- `scripts/build.py` — Win32/CMake build runner.
- `scripts/media.py` — optional installer for localized videos/textures.

Localized BIK/DDS/TGA assets are intentionally not stored in Git.

The validator also checks localized skirmish/multiplayer map titles before compilation.
