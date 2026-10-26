#include "test_harness.h"
#include "WaterPreviewPreset.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

TEST(water_preview_presets_reject_partial_and_trailing_edits_atomically)
{
    const char *invalid[] = {
        "WATER_PREVIEW_COLOR_V1 0.2 0.3 0.4 0.67",
        "WATER_PREVIEW_COLOR_V1 0.2 0.3 0.4 0.67 0.8 extra",
        "WATER_PREVIEW_COLOR_V1 0.2 0.3 0.4 0.67 0.8\n0.9",
        "WATER_PREVIEW_COLOR_V2 0.2 0.3 0.4 0.67 0.8",
        "WATER_PREVIEW_COLOR_V1 0.2 0.3 0.4 0.01 0.8",
        "WATER_PREVIEW_COLOR_V1 0.2 0.3 0.4 0.67 nan",
    };
    for (const char *text : invalid) {
        WaterPreviewColor::Values values;
        values.red = 0.75f;
        std::istringstream input(text);
        CHECK(!WaterPreviewColor::readPreset(input, values));
        CHECK_EQ(values.red, 0.75f);
        CHECK_EQ(values.strength, 0.f);
    }
    WaterPreviewColor::Values values;
    std::istringstream input("WATER_PREVIEW_COLOR_V1\n0.2 0.3 0.4 0.67 0.8\n \t");
    CHECK(WaterPreviewColor::readPreset(input, values));
    CHECK_NEAR(values.red, 0.2f, 0.000001f);
    CHECK_NEAR(values.brightness, 0.67f, 0.000001f);
    CHECK_NEAR(values.strength, 0.8f, 0.000001f);
}

TEST(water_preview_default_export_preserves_loaded_rules_and_aliases)
{
    namespace fs = std::filesystem;
    char pattern[] = "water-preview-preset-XXXXXX";
    const char *directory = mkdtemp(pattern);
    CHECK(directory != nullptr);
    if (!directory) return;
    const fs::path root(directory);
    const fs::path input = root / "water-preview-rule.ini";
    std::ofstream(input) << "; authored rules\n";
    const fs::path alias = root / "alias.ini";
    fs::create_hard_link(input, alias);
    CHECK(WaterPreviewColor::defaultExportOverwritesInput(input, input));
    CHECK(WaterPreviewColor::defaultExportOverwritesInput(input, alias));
    CHECK(!WaterPreviewColor::defaultExportOverwritesInput(input, root / "export.ini"));
    CHECK(!WaterPreviewColor::defaultExportOverwritesInput({}, input));
    fs::remove_all(root);
}
