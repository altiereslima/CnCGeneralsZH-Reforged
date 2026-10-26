# Water Preview

`water_preview` is the dedicated native editor for Modern water materials. It shares the
production SDL renderer, GPU FFT, Modern water shader and `ModernWater.ini` parser with
the game. It requires no game assets, save or running match. The synthetic flat submerged
bed and fixed lighting make comparisons repeatable; this is not a campaign-map viewer.

The game defaults to Classic; opt in to Modern under Options > Water style.
The standalone preview always uses Modern.

## Configuration and matching

On an ordinary launch the tool looks beside its executable for
`overlay/Data/INI/ModernWater.ini`, then `Data/INI/ModernWater.ini`. If neither exists it
uses the same built-in rule as the game. `--config FILE.ini` selects another file;
a missing file uses built-in defaults and reports that choice. Invalid configuration
produces a diagnostic and stops the preview so a bad edit cannot be mistaken for a
successful comparison. An empty file intentionally disables all replacement rules.

The initial map inputs are color **#324B32** (RGB 50,75,50), texture name `TSWater03.tga`,
water depth **2.0**, and minimum opacity **0.8**. These are the common authored water
settings in `GC_ChemGeneral` and `MD_USA05`. The texture name is used for rule matching;
the tool does not load the authored texture's pixels. The default matched material uses
**#2D3A0B** (RGB 45,58,11), **67% brightness**, **80% pollution**, and bubbles.

```ini
ModernWaterRule PollutedWater
  MatchColors
    Color = R:50 G:75 B:50
  End
  MatchTextures
    Texture = TSWater03.tga
  End
  ReplacementColor = R:45 G:58 B:11
  Brightness = 0.67
  Pollution = 0.80
  Bubbles = Yes
End
```

Each list accepts additional entries. A rule matches any listed color **and** any listed
texture; the first enabled matching rule wins. Colors match exact RGB bytes. Texture
matching ignores case and directory prefixes. An unmatched color is used by Modern
water without pollution; white preserves its normal blue appearance. Pollution does not
control tint strength. The Modern material's bubbles are procedural surface shading
with popping rings, not a volumetric gas simulation.

## Game integration

The global source file is `GeneralsMD/Code/Data/INI/ModernWater.ini`. A normal native
build stages it as `<build>/overlay/Data/INI/ModernWater.ini` (in this checkout,
`build-linux/overlay/Data/INI/ModernWater.ini`). The game reads `Data/INI/ModernWater.ini`
through its existing filesystem/overlay lookup at startup. There is no in-game settings
control for these rules.

Matching uses the effective map `StandingWaterColor` and `StandingWaterTexture` after
the existing map overrides have loaded, including save restoration. It does not inspect
map names, save names or the chosen general. The material is resolved again when those
effective values change. Classic water retains its authored parameters; Modern rules
replace only the material color and effects, retaining the map's depth and opacity.

If the game cannot read the INI or an edit is invalid, it logs a diagnostic and keeps the
complete built-in default rule set. Parsing is transactional: a valid first rule followed
by an invalid rule cannot install a partial configuration. A valid empty/comment-only
file disables all replacement rules. The preview instead stops on an invalid file so
editing mistakes are visible. Neither the game nor the preview automatically regenerates
a missing INI or rewrites its loaded configuration; preview export writes only its
selected output.

Keep durable project changes in the source INI. Building `water_preview`, building the
`zh_overlay` target, or running a normal full build copies it into the runtime overlay.
Restaging recreates that overlay, so direct edits to the staged copy are replaced. Restart
the game or preview after editing the configuration; neither watches the file for changes.

## Build and run

With an existing configured native build and dependencies:

```sh
cmake --build build-linux --target water_preview -j 4
./build-linux/water_preview
./build-linux/water_preview --help
```

The preview does not edit the game configuration or maps automatically.

## Interactive editor

- **1:** clean Modern baseline; **2:** resolved/editable material; **3:** split comparison,
  baseline on the left. Split mode uses two cropped views of the same scene and shader.
- **H:** show or hide the RGB, brightness and pollution panel.
- **L:** load the saved editor colors. This opens the panel and reports success,
  a missing preset, or an invalid preset without closing the viewer.
- **B:** toggle bubbles; the title reports their state. **[ / ]:** adjust pollution.
- **Space:** pause animation. Drag outside the panel or use arrows to orbit. Wheel or
  **+ / -** zooms. **R** resets the camera.
- **S:** save `water-preview.png` in the working directory, or the `--screenshot` path.
- **E:** export the current material as `water-preview-rule.ini` in the working
  directory, or the `--export-config` path. Export replaces that destination with a
  single rule matching the selected map color and texture; it does not merge a rule set.
- **Escape:** close the viewer.

**Reset color** restores the material resolved at startup. **Save color** stores the five
panel values in SDL's per-user `CnCGeneralsZH-Reforged/WaterPreview/colors.ini` preference
file. Press **L** to restore those colors in the current session, or use **`--load-preset`**
to restore them at startup. Ordinary starts always resolve the INI, and old Dr Thrax
preview preferences cannot override it silently.
The bubble toggle is a material option exported to INI, not part of the five-value editor
preset. Unsaved slider changes affect only the current session.

## Repeatable comparisons and export

```sh
./build-linux/water_preview --offscreen --time 12 --mode split \
  --screenshot /tmp/water-split.png

./build-linux/water_preview --map-color C03020 --map-texture OtherWater.tga \
  --offscreen --screenshot /tmp/water-red-map.png

./build-linux/water_preview --color 2D3A0B --brightness 0.67 --strength 0.8 \
  --export-config /tmp/proposed-modern-water.ini
```

`--export-config` writes the startup material immediately and makes **E** update that
same file after edits. The exported rule is parsed by the shared parser before writing.
Explicit `--color`, `--brightness` and `--strength` override the resolved material and any
explicitly loaded editor preset. `--no-bubbles` overrides the rule's bubble switch.
`--map-color` and `--map-texture` change matching inputs; `--map-depth` and `--map-opacity`
change the independent authored depth/opacity parameters. `--strength 0` removes
pollution while retaining the selected color; **1** or `--mode clean` shows the untouched
Modern baseline. `--hide-controls` excludes the editor panel from captures.

Offscreen mode defaults to one frame. `--frames 180` runs three seconds with deterministic
1/60-second steps. `--time` sets the initial wave time; `--width` and `--height` set output
size. Startup never loads a saved preset without `--load-preset`, including offscreen
captures; **L** loads it explicitly during an interactive session.

## Implementation

The tool compiles `Tools/water_preview.cpp` and `Tools/WaterPreviewColorPanel.h` and links
the ordinary production shader library. Material inputs use the dedicated constants in
`Platform/ModernWaterMaterial.h`; light colors, texture factors and vertex metadata retain
their production meanings. Material matching and constants are shared with the game renderer. The preview is
built explicitly and does not add an editor dependency to the game executable.

## Validation

The source includes material parsing, map-override/cache, preset persistence, GPU
water/FFT and compiled uniform-layout checks. Run the Release CTest suite in the
configured build, and build `water_preview` explicitly. Runtime checks should use
isolated preferences and copied saves, compare both water styles, and retain
actual source/executable hashes alongside captures and timing samples.
