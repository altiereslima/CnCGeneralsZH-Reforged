# Build setup

No secret is required for the text-only PT-BR build.

The workflow installs the public build dependencies automatically at pinned revisions. Optional
localized videos/textures can be supplied with:

- `PTBR_MEDIA_URL`
- `PTBR_MEDIA_SHA256`
- `PTBR_MEDIA_TOKEN` (optional)

If no media bundle is configured, the build continues normally and the game falls back to the
installed/English/generic media.

Every push to `main` starts the Windows build automatically.
