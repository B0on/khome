# khome renderer

Stage 1 of the Kindle launcher is the drawing core: it renders the seven
design-system screens (plus page 2 of `home-many`) into 8-bit grayscale PNGs
on the Mac, for comparison with `design/system/screens/`. Stage 2 adds the
Kindle-side `khome` (`src/fb.c`, `src/input.c`, `src/lipc.c`, `src/state.c`,
`src/main.c`): framebuffer output, touch taps, lipc events, and the
own/release dance that borrows the screen from the stock UI.

## Build

```sh
cd launcher
./build.sh host   # build/host/khome-render  (macOS, PNG output)
./build.sh arm    # build/arm/khome-bench + build/arm/khome (Paperwhite 2, static ARM)
./build.sh all    # both
```

The host build uses `cc -O2 -Wall -Wextra -Werror`; the ARM build uses
`zig cc -target arm-linux-musleabi -mcpu=cortex_a9 -static -O2`. `png_out.c` is
host-only; the ARM bench draws in memory and prints per-screen timings.

## Deploy to the Kindle

```sh
launcher/tools/deploy.sh            # build, then copy to /mnt/us/khome
ssh kindle 'nohup /mnt/us/khome/khome.sh >/dev/null 2>&1 &'
```

`deploy.sh` sends `build/arm/khome`, `kindle/khome/khome.sh`, the three TTFs
(only when their sha256 differs) and the icons. `khome.sh` sets
`PATH`/`DISPLAY`/`TZ`, refuses to start twice (`flock` plus `pidof khome`), and
restores the framework when `khome` exits for any reason.

## Test

```sh
./build.sh test     # unit tests + design/mock + device/golden checks
./build.sh golden   # regenerate launcher/tests/golden/device/*.png
```

`./build.sh test` builds `build/host/khome-tests` (C unit tests in `tests/`,
host only), renders every screen to `build/test-out`, and runs
`tests/test_render.py`. The render tests require:

- design output within 1.2% differing pixels of `design/system/screens/` and
  the largest 4-connected difference component at most 1200 px;
  `home-many-2` has no mock.
- device output pixel-identical to `tests/golden/device/`; rewrite the goldens
  with `./build.sh golden` when a rendering change is intended.
- every output PNG 8-bit grayscale, 16 tones only (0, 17, ..., 255), and sized
  1072 x 1448 (design) or 758 x 1024 (device).

Run `./build.sh golden` only after reviewing a design change: the device PNGs
are a regression snapshot, not a comparison against the mocks.

## Render

```sh
./build/host/khome-render all /tmp/khome-out
```

Writes `/tmp/khome-out/design/*.png` (1072 x 1448) and
`/tmp/khome-out/device/*.png` (758 x 1024).

Single screen:

```sh
./build/host/khome-render home design out.png
./build/host/khome-render settings device out.png
```

Screens: `home`, `home-many`, `home-many-2`, `home-empty`, `settings`,
`settings-wifi-off`, `launching`, `sleep`. Scales: `design` (1.0) and
`device` (758/1072).

Fonts are read at runtime from `design/system/fonts/` (relative to the repo
root or `launcher/`). Icons are read from `launcher/assets/icons/`. Override
with `KHOME_FONTS` / `KHOME_ICONS`:

```sh
KHOME_FONTS=../design/system/fonts KHOME_ICONS=assets/icons \
  ./build/host/khome-render all /tmp/khome-out
```

## Compare with the mocks

```sh
python3 tools/diff-mock.py
# --render /tmp/khome-out/design --mock design/system/screens \
# --out /tmp/khome-out/diff
```

Standard-library Python. Prints the fraction of differing pixels per screen
and writes `*-diff.png` abs-difference images.

## Icons

```sh
./tools/make-icons.sh
```

Renders `design/system/icons/*.svg` with headless Chrome at the design size
and at `round(size * 758 / 1072)`, then quantizes both sets to 16 tones, the
same method as `design/system/render.sh`. Output: `assets/icons/design/` and
`assets/icons/device/`.

## Source layout

| Path | Contents |
|---|---|
| `src/canvas.[ch]` | 8-bit canvas, rects, AA circles/arcs/lines, rounded rects, blit, 16-tone quantize, icon loading |
| `src/text.[ch]` | stb_truetype fonts, glyph cache, letter-spaced lines, measurement, alignment, ellipsis |
| `src/gpos.[ch]` | minimal GPOS pair-kerning reader (stb_truetype does not read GPOS) |
| `src/layout.[ch]` | pure design-unit layout for every screen; tap rects and clock refresh rect |
| `src/model.h` | `HomeModel`, `SettingsModel`, `LaunchingModel`, `SleepModel` |
| `src/fixtures.c` | display data copied from the mock HTML |
| `src/png_out.c` | `stbi_write_png` output (host only) |
| `src/render_main.c` | `khome-render` CLI |
| `src/bench_main.c` | ARM `khome-bench` |
| `third_party/` | stb headers + provenance |
| `assets/icons/` | design-size and device-size icon PNGs |
| `tools/make-icons.sh` | regenerate the icon PNGs |
| `tools/diff-mock.py` | mock comparison |
| `tests/` | C unit tests, render/mock/golden tests, device goldens |
