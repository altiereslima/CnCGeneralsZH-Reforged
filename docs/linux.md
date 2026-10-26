

## Modern water contribution

On Linux, Classic stays the default. Opt in to Modern under Options > Water style;
changes apply live.
Windows keeps its original water shader identities; macOS uses the legacy SDL
water programs and hides the Linux selector. Material rules are loaded from
`Data/INI/ModernWater.ini` through the game filesystem. Source edits are staged
into the build overlay; invalid or missing game rules use built-in defaults.

Build the standalone editor explicitly: `cmake --build build-linux --target water_preview`.
See [water-preview.md](water-preview.md) for controls, matching, explicit exports
and the distinction between game fallback and preview parse failures.
