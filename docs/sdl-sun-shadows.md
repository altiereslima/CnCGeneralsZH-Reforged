# Linux sun and smoke shadows

This feature starts directly from `reference/3.1.0` and has no dependency on the
Modern water or volumetric hazard-cloud features. Linux uses mapped sun shadows
through SDL GPU. Windows retains its existing Direct3D 11 shadows;
macOS retains its existing scene shadows. Shadow capability is independent of
per-pixel lighting.

## Behavior and controls

The renderer reuses the existing sun camera, caster selection and filtering.
Registered units and buildings, bridges, batched foliage, object palms and
sorted translucent meshes such as rotor discs can cast. Friendly cloak casters
retain their per-draw partial coverage through sorting. Eligible solid,
terrain, road, bridge, tree and particle draws receive sun and smoke shadows.
Legacy water and reflections keep the existing 3.1.0 paths.

Existing shadow preferences remain authoritative. `ShadowQuality = 0`, `1` or
`2` chooses a 1024, 2048 or 4096 square sun map, with a consistent world-space
filter width. Unit casters honor `UseShadowVolumes`; tree and bridge capture
honors the ordinary volume/decal options. Trees already loaded by multiplayer
can cast when the `ShowTrees` preference is off. `VolumetricSmokeShadows`
independently controls smoke projection, including an otherwise empty capture
when ordinary geometry shadows are disabled.

`-shadowmapreport`, or the harness's `--report`, reads back depth occupancy and
reports accepted receiver counts.
Diagnostic readback affects timing.

## State and fallback

Scene generation, target, viewport and camera determine ownership. Nested
scenes and reflections cannot borrow another view's map or suppress its legacy
shadows. Queued draws keep immutable constants, receiver exclusions and cloak
coverage. Capture restores device state; replaced sun textures survive until
queued readers have been submitted.

Unsupported depth targets, invalid sizes and failed captures retain legacy
shadows. Smoke allocation or pipeline failure retains particle blobs while
solid mapped shadows can continue. Optional receiver-program or pipeline
failure retries ordinary geometry and latches legacy for later scenes until
device recreation. When mapped pixels were already submitted, that scene
retains its ownership to avoid double darkening. Fallback does not atomically
replay a partially submitted frame.

The native engine receiver block extends from 22 to 159 float4s and keeps its
existing prefix. Both reconstruction matrices use explicit `row_major` HLSL
matching CPU memory. Unused Windows lighting fields stay zero. The emulated
pixel register bank remains 32 entries, the draw keeps 8 sampler slots and each
stage binds one native uniform block. Sun depth and smoke use fragment slots 5
and 6. These native blocks do not require water FFT textures or water uniforms.
Sun-depth reports use a small independent depth-to-R32 diagnostic pass without
sampling the scene's depth attachment.

## Reproduce a scene

Use an existing Release executable, its overlay and a prepared file-link root
containing your own game data. No dependencies are installed by the harness.
Each output directory must be new. Run GPU workloads serially.

```sh
python3 GeneralsMD/Code/Tools/sun-shadow-check.py \
  --generals "$PWD/build-linux/generals" \
  --root /absolute/path/to/prepared-game-root \
  --overlay "$PWD/build-linux/overlay" \
  --out "$PWD/build-linux/shadow-solid-mapped" \
  --case solid --gpu-debug --report
```

Legacy comparisons use Classic graphics. Retain the executable, scene and
settings when comparing images.

Available cases are `solid`, `smoke`, `cloak`, `trees`, `forest`, `bridge`,
`helicopter`, `water` and `river`. Water cases inspect unchanged legacy water and
reflection behavior. The harness also accepts `--quality 0|1|2`, `--trees yes|no`,
`--geometry-shadows yes|no` and `--smoke-shadows yes|no`. Scenario fixtures are
copied into a private overlay only for cases that need them.

`--replay` accepts an existing recording for a case without scenario actions.
`--frame` sets the case's screenshot frame; the run ends 60 frames later.

`manifest.json` records the command, explicit environment, executable hash,
source SHA/diff, settings and scenario/replay hashes, exit status, limit-frame
CRC, screenshot hashes and available timing/receiving diagnostics. Git
provenance is bound to the harness's source checkout. Inspect images and logs
and compare CRCs; process exit alone does not establish correct visuals.

Use `--measure` for matched timing runs, with the same scene, settings,
executable and driver. This excludes screenshots, reports and GPU validation.
The device probe includes loading;
`HEADLESS FRAMETIME` starts after logic frame 60. These diagnostics are not a
universal steady-state FPS result.

## Validation and limits

After configuring a Release build with the repository's existing dependencies:

```sh
cmake --build build-linux --target posix_draw_resolve_selfcheck \
  posix_gpu_draw_selfcheck sdl_gpu_frame_selfcheck test_sun_shadow_sorting
ctest --test-dir build-linux --output-on-failure \
  -R '^(posix_draw_resolve_selfcheck|posix_gpu_draw_selfcheck|sdl_gpu_frame_selfcheck|test_sun_shadow_sorting|test_ffshadercompile)$'
```

Focused fixtures cover analytic translated-camera receivers,
smoke moments, tree silhouettes, sorted cloak coverage, state restoration,
queued resource replacement, empty clears, reset and injected creation failures.
The sun-target frame test retains the baseline gamma and clear checks. Runtime
sun fixtures are Linux-only. GPU tests skip when no usable device is available.

Real-game scenes and replay comparisons complement these fixtures. No old
stacked-branch result establishes this independent branch's validation. Record
its exact tested SHA, hardware, commands, failures and skips with the PR.
Linux evidence does not establish native Windows/macOS compatibility or visual
parity on every Linux GPU and driver.
