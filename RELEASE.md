# Release workflow (WWA fork)

This fork tracks upstream [WLED](https://github.com/wled/WLED) branch `16_x` and adds
SK6812 WWA (warm white / cold white / amber) support plus fade-quality improvements.

## Versioning

Mirrors upstream's convention: dev builds carry `-dev`, releases never do.

- `package.json` `version` drives the firmware version string and binary names
  (`WLED_<version>_ESP32.bin`). Keep `package-lock.json`'s two version fields in sync.
- **On `main`** (tracking `16_x` tips): `<upstream dev version>-wwa`, e.g.
  `16.0.1-dev-wwa` — matches upstream `16_x`'s `16.0.1-dev`.
- **Releases**: based on an upstream **release tag**, never a branch tip. Version is
  `<upstream release>-wwa` (no `-dev`), e.g. `16.0.1-wwa` — it promises the build
  contains everything in upstream `v16.0.1` plus the WWA delta (and possibly later
  `16_x` fixes already merged to `main`).
- Release tags: `v<version>.<n>` where `<n>` counts fork releases on the same upstream
  base, e.g. `v16.0.1-wwa.1`, `v16.0.1-wwa.2`.

## Syncing with upstream

Merge `16_x` tips freely into `main` for fixes; merge upstream **release tags** when
one exists (upstream cuts them as side commits off `16_x`, so they don't arrive via
the branch):

```sh
git fetch upstream --tags
git merge upstream/16_x        # routine sync
git merge v16.0.1              # when upstream tags a release
# package.json will conflict on version — keep the fork's `-dev-wwa` version on main.
# The real delta lives in:
#   wled00/bus_manager.{h,cpp}  (WWA amber, spatial dithering, setBrightness16)
#   wled00/led.cpp              (perceptual fade curve, stall-proof transitions)
#   wled00/FX_fcn.cpp, FX.h     (setBrightness16 plumbing)
pio run -e esp32dev
```

## Publishing a release

Like upstream, a release is a **side commit off `main`** (not on the branch): it bumps
the version from `-dev-wwa` to `-wwa`, gets tagged, and `main` keeps its dev version.

1. Ensure `main` contains the upstream release tag (see above), builds, and is pushed.
2. Cut the release commit and tag; CI (`release.yml`) triggers on any tag push, builds
   all environments (~1 h), and creates a **draft** GitHub Release with binaries:

```sh
git checkout --detach main
# set version to 16.0.1-wwa in package.json + package-lock.json
git commit -am "16.0.1-wwa"
git tag v16.0.1-wwa.1
git push origin v16.0.1-wwa.1
git checkout main
```

3. Verify on the strip (below), then publish the draft release manually on GitHub.

## When upstream 17.x lands

The fork tracks `16_x`. Upstream `main` is already `17.0.0-dev` with large refactors
(`new-settings`, `http-api-refactor`) that likely touch the fork's delta files —
moving to 17.x will be a deliberate porting task, not a routine merge.

## Verifying on the strip

Device: `wled-bedroom-bed.local` (ESP32, 349 LEDs). OTA:

```sh
curl -F "update=@build_output/release/WLED_16.0.1-dev-wwa_ESP32.bin" http://wled-bedroom-bed.local/update
```

Note: OTA is restricted to the device's own subnet (`same-subnet` security setting) —
the uploading machine must be on the IoT network.

Fade verification checklist (0.7 s default transition unless noted):

- off → on and on → off fade at high (100%) and low (~8%) brightness — smooth ramp,
  no pause-then-jump, no color-temperature shift near black
- slow fade (`{"tt":30}`) to ~8% — sub-code smoothness from spatial dithering,
  no visible flicker (dithering is spatial only, never temporal)
- send `{"tt":0, "bri":128}` then toggle — the next fade must still ramp
  (transition must not stick at 0)
- save a preset mid-fade (`{"psave":250}`, then `{"pdel":250}`) — fade must pause
  and resume, not jump (stall-proof transition clock)
