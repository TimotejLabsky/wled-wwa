# Release workflow (WWA fork)

This fork tracks upstream [WLED](https://github.com/wled/WLED) branch `16_x` and adds
SK6812 WWA (warm white / cold white / amber) support plus fade-quality improvements.

## Versioning

- `package.json` `version` drives the firmware version string and binary names
  (`WLED_<version>_ESP32.bin`). Scheme: `<upstream version>-wwa`, e.g. `16.0.1-dev-wwa`.
- Release tags: `v<version>.<n>` where `<n>` counts fork releases on the same upstream
  base, e.g. `v16.0.1-dev-wwa.1`.

## Syncing with upstream

Prefer merging upstream **release tags** over branch tips (known-good states):

```sh
git fetch upstream
git merge upstream/16_x        # or a release tag, e.g. v16.0.1
# resolve conflicts — the fork's delta lives in:
#   wled00/bus_manager.{h,cpp}  (WWA amber, spatial dithering, setBrightness16)
#   wled00/led.cpp              (perceptual fade curve, stall-proof transitions)
#   wled00/FX_fcn.cpp, FX.h     (setBrightness16 plumbing)
pio run -e esp32dev
```

## Publishing a release

1. Merge + build + verify (below), commit, push.
2. Tag and push — CI (`release.yml`) builds all environments and attaches binaries
   to a GitHub Release automatically (full matrix takes ~1 h):

```sh
git tag v16.0.1-dev-wwa.1
git push origin v16.0.1-dev-wwa.1
```

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
