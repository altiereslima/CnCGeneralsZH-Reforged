// test_glyph_rasteriser's regression golden: GlyphRasteriserClass on the vendored FreeType 2.14.3 over
// macOS's font files, gasp-faithful.  Printed by the test with ZH_GLYPH_GOLDEN_PRINT=1; a census of
// the game's text (D6) chose the faces and sizes.  { face, bold, points, lfWidth, file version, string
// advance, ink left, top, right, bottom, coverage hash }.
const Golden GOLDEN[] =
{
	{ "Arial", false, 8, 0, "Version 5.01.2x", 181, 0, 3, 179, 12, 0x48e1a6bau },
	{ "Arial", false, 10, 0, "Version 5.01.2x", 217, 1, 3, 215, 15, 0xe6a350fbu },
	{ "Arial", false, 12, 0, "Version 5.01.2x", 259, 1, 2, 256, 17, 0x852a8094u },
	{ "Arial", false, 14, 0, "Version 5.01.2x", 317, 1, 3, 314, 21, 0x196b2dd3u },
	{ "Arial", true, 10, 0, "Version 5.01.2x", 235, 1, 2, 233, 15, 0x0374e668u },
	{ "Arial", true, 12, 0, "Version 5.01.2x", 279, 1, 2, 277, 17, 0x6cce10bdu },
	{ "Times New Roman", false, 10, 0, "Version 5.01.4x", 195, 0, 2, 192, 14, 0x21bcb0cfu },
	{ "Times New Roman", false, 14, 0, "Version 5.01.4x", 280, 1, 3, 277, 19, 0x846a3633u },
	{ "Courier New", false, 12, 0, "Version 5.00.2x", 380, 1, 3, 375, 16, 0xc39d0dbfu },
	{ "Arial", false, 15, 8, "Version 5.01.2x", 301, 1, 3, 298, 22, 0x5867f6d0u },		// "Generals" 15
	{ "Arial", false, 20, 10, "Version 5.01.2x", 376, 1, 5, 372, 30, 0x114b1f8bu },		// "Generals" 20
};
