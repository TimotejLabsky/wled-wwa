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
the branch). Normally comment `/upstream` or `/upstream v16.0.2` on the pinned
"Upstream sync" issue: it opens a merge PR (merge it with a **merge commit**, never
squash) and flags unmerged upstream releases. By hand:

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
   all environments (~5 min with warm caches), and creates a **draft** GitHub Release
   with binaries:

```sh
git checkout --detach main
# set version to 16.0.1-wwa in package.json + package-lock.json
git commit -am "16.0.1-wwa"
git tag v16.0.1-wwa.1
git push origin v16.0.1-wwa.1
git checkout main
```

3. Verify on the strip (below), then publish the draft release manually on GitHub.

## Porting to a new major (17.x)

`main` tracks one upstream major at a time. Crossing a major is a **port on its own
branch**, not a sync: `/upstream` refuses refs of a different major and flags an upstream
`v17.x.y` release with a 🚀 line. Upstream `main` is `17.0.0-devV5`: ESP-IDF 5 (Tasmota
Arduino Core 3.3 / IDF 5.5), ~650 commits beyond `16_x`.

Trial merge of upstream `main` into this fork (2026-10-02): conflicts in ~20 files, but
almost all are upstream's own `16_x`-vs-`main` divergence (cherry-picked fixes, CI,
docs, `platformio.ini`) — take upstream's side there. Fork-specific hunks:
- `bus_manager.cpp`: upstream 17 has its own `TYPE_WS2812_WWA` entry labelled
  `"WS281x WWA" // amber ignored` — keep the fork's `SK6812 WWA` label and amber code.
- `led.cpp`: the fork's "restore `transitionDelay` after a one-time `tt:0`" fix collides
  with an upstream restructure — re-check whether 17 still needs it.
- `FX_fcn.cpp`: ledmap parser rewritten upstream (not fork code) — take upstream.

Size: pure upstream 17-dev `esp32dev` = 1,335,499 B, 84.9% of the 1.5 MB app partition,
same partition table (16.0.1-dev-wwa: 83%). Fits OTA; partition change not needed.

Procedure, once upstream tags `v17.0.0` (not before — dev churn is high):

```sh
git fetch upstream --tags
git switch -c wwa-17 main
git merge v17.0.0                     # resolve: upstream side except the fork delta
# package.json: 17.0.0-dev-wwa
pio run -e esp32dev                   # IDF 5 toolchain downloads on first build
```

1. Open a PR `wwa-17` → `main` (merge commit). Check the fork delta survived and V5
   behavior of the digital LED driver with the WWA bus (amber, ABL repaint, dithering).
2. Before flashing: read upstream's 17.0 release notes for the 16 → 17 OTA path
   (IDF 4 → 5 app on an old bootloader) and any config migration (`new-settings`);
   back up `cfg.json` + `presets.json`. Keep a USB cable ready for the first flash.
3. Run the full strip checklist below on `wwa-17`.
4. Merge into `main`, change the `/upstream` default branch from `16_x` to `17_x`
   (upstream's release branch) in `upstream-sync.yml`, update this file's `16_x` mentions.
5. Release `v17.0.0-wwa.1` as usual. No long-lived `wwa-16` branch unless a 16.x fix
   must ship after the switch.

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
