// Persistence shared by the water editor and its CPU regression checks.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include <cmath>
#include <filesystem>
#include <istream>
#include <initializer_list>
#include <locale>
#include <string>

namespace WaterPreviewColor {

// Display RGB; the production water shader converts it to linear color.
struct Values {
    float red = 1.f;
    float green = 1.f;
    float blue = 1.f;
    float brightness = 1.f;
    float strength = 0.f;
};

inline bool valid(const Values &values)
{
    for (float channel : {values.red, values.green, values.blue, values.strength}) {
        if (!std::isfinite(channel) || channel < 0.f || channel > 1.f)
            return false;
    }
    return std::isfinite(values.brightness)
        && values.brightness >= 0.1f && values.brightness <= 1.f;
}

// Commit only a complete preset. Trailing fields are editing errors, just as
// malformed INI rules are, and must leave the currently displayed values intact.
inline bool readPreset(std::istream &input, Values &values)
{
    input.imbue(std::locale::classic());
    std::string version;
    Values parsed;
    if (!(input >> version >> parsed.red >> parsed.green >> parsed.blue
          >> parsed.brightness >> parsed.strength)
        || version != "WATER_PREVIEW_COLOR_V1" || !valid(parsed))
        return false;
    input >> std::ws;
    if (!input.eof() || input.bad())
        return false;
    values = parsed;
    return true;
}

// Default export names must not silently replace the loaded rule file. Resolve
// symlinks and hard links too; an explicit export destination remains permitted.
inline bool defaultExportOverwritesInput(const std::filesystem::path &input,
                                        const std::filesystem::path &output)
{
    if (input.empty())
        return false;
    std::error_code error;
    return std::filesystem::equivalent(input, output, error) && !error;
}

} // namespace WaterPreviewColor
