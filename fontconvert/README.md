# fontconvert

Converts TTF/OTF fonts into Adafruit GFX `.h` bitmap headers for use in
PolyKybd QMK firmware driving per-keycap OLED displays.

FreeType 2.13.3 and HarfBuzz 2.6.7 are built from source as static libraries
(no system-level install required).

## Build

```bash
mkdir -p cmake-build-debug && cd cmake-build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..   # downloads and builds FreeType + HarfBuzz (~5 min first time)
cmake --build .                      # produces ./fontconvert
```

For a release build:

```bash
mkdir -p cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

## Install

```bash
# Install to ~/.local/bin/  (default, no sudo needed)
cmake --install cmake-build-debug

# Install to /usr/local/bin/  (system-wide, needs sudo)
sudo cmake --install cmake-build-debug --prefix /usr/local
```

## Usage

```bash
# Codepoint range — monochrome
fontconvert -f<FONT.ttf> -s<SIZE> [-v <VARIANT>] FIRST LAST [FIRST2 LAST2 ...]

# Grayscale / color-emoji with dithering and exposure control
fontconvert -f<FONT.ttf> -s20 -g -D stucki -e 0.1 -r50 -v _Emoji_ 0x1f600 0x1f64f

# HarfBuzz sequence mode (-S) — one glyph (ZWJ sequence)
fontconvert -f<FONT.ttf> -s20 -g -v _Rainbow_ -S "1F3F3 FE0F 200D 1F308"

# HarfBuzz sequence mode — multiple glyphs, comma-separated
fontconvert -f<FONT.ttf> -s20 -g -r36 -W60 -v _Flags_ \
    -S "1F1E9 1F1EA, 1F1EB 1F1F7, 1F1FA 1F1F8"
```

Within `-S`, **spaces** separate codepoints that form a single glyph (HarfBuzz handles
ZWJ, regional-indicator pairs, ligatures, etc.); **commas** separate independent glyphs,
each shaped in its own HarfBuzz call.

| Flag | Meaning |
|------|---------|
| `-f<FILE>` | Font file (TTF/OTF) |
| `-s<N>` | Point size (points at a fixed 141 DPI — so it only reaches even ppem: `-s6`→12 px, `-s7`→14 px, `-s8`→16 px …) |
| `-p<N>` | Em size in **pixels**, addressing the raster grid directly (0/unset = use `-s`). Reaches the odd ppem `-s` cannot. The emitted symbol is named `..._<N>px<bits>b` |
| `-H<MODE>` | Grid-fitting: `native` (default — whatever the face provides), `auto` (force FreeType's autohinter), `none` (raw outline). See [Hinting](#hinting--h--matters-most-for-small-1-bit-text) |
| `-v<NAME>` | Variant name embedded in the C identifiers |
| `-g` | Grayscale / BGRA color-emoji mode — quantises to 1-bit via `-D` algorithm |
| `-D<MODE>` | Dithering algorithm: `fs` (default), `stucki`, `bayer`, `threshold`, `random` |
| `-e<N>` | Exposure bias before dithering (−1.0 to 1.0, default 0.0) |
| `-r<N>` | Render-size override in pixels (useful for bitmap-only fonts like NotoColorEmoji) |
| `-W<N>` | Maximum rendered width in pixels; glyph is scaled down if wider |
| `-w<N>` | Variable-font weight: pin the `wght` axis to N (e.g. `500` Medium, `700` Bold). Many Noto families ship variable-only and default to a light instance (NotoSansJP defaults to Thin/100, NotoSerifKR to ExtraLight/200) — use `-w` to select a usable weight. Clamped to the axis range; ignored with a warning on non-variable fonts |
| `-o<N>` / `-n<N>` | Positive / negative offset added to codepoints in the output struct |
| `-S <seq>` | HarfBuzz sequence: `"G[,G]..."` where G = space-separated hex codepoints |
| `-d` | Dump all codepoints in the font and exit |

Output is written to stdout; redirect to a `.h` file:

```bash
fontconvert -f fonts/noto-sans/NotoSans-Regular.ttf -s14 -v _Base_ 0x20 0x7e \
    > base/fonts/generated/NotoSans_Regular_Base_14pt.h
```

## Hinting (`-H`) — matters most for small 1-bit text

At the sizes small UI text actually renders (roughly ≤ 20 px em) a stem is only
1–2 px wide, so whether the outline is snapped to the pixel grid decides whether
the *two edges of one stem* round the same way. Without grid-fitting they round
independently: the same stem lands 1 px on one side of a glyph and 2 px on the
other, round bowls come out lopsided, and crossbars drop out. Digits show it
worst, because a reader compares them against each other.

Two traps make this bite silently:

1. **Most modern variable fonts ship no hinting bytecode at all.** `NotoSans` has
   `maxSizeOfInstructions == 0`, no `fpgm`, and a 7-byte `prep` that only sets
   dropout control — every glyph carries zero instructions.
2. **FreeType will not fall back to its own autohinter** for such a face if it has
   even that stub `prep` table (its "no instructions" heuristic checks the `fpgm`
   *and* `prep` sizes). So the default `native` path renders these fonts
   **completely ungridfitted**, and the `TT_INTERPRETER_VERSION_35` this tool sets
   for the mono path does not help — there is no bytecode for it to interpret.

Pass `-Hauto` to force the autohinter, which grid-fits stems and x-/cap-height in
both axes regardless of what the font carries.

Grid-fitting snaps cap-height to whole pixels, so the reachable glyph heights come
in **steps** — and since `-s` is points at a fixed 141 DPI it can only land on even
ppem, which often skips the step you want. Use `-p<N>` (pixels) to address the grid
directly, and *measure* cap-height / x-height / string widths against the previous
build before adopting a size, so a fixed layout does not move:

```bash
# grid-fitted 15 px, semibold — the PolyKybd status-OLED body font
fontconvert -f NotoSans-Regular.ttf -p15 -w600 -Hauto -v _Small_ 0x20 0x7e
```

`-Hnative` is the default and keeps previously generated headers byte-identical.

## Tests

```bash
pip install pypng pytest
cd tests
pytest -v
```

The test suite always runs against `cmake-build-debug/fontconvert` (hardcoded in
`tests/test_gfx_fonts.py`).  It does **not** test the installed binary.  To test
a release build, point `FONTCONVERT` in that file at `cmake-build-release/fontconvert`.

Test classes:

| Class | What it covers |
|-------|---------------|
| `TestFixtureParse` | Parses the checked-in NotoSans Base header and verifies structure |
| `TestMonoRendering` | Renders A–Z from DejaVuSans in monochrome via fontconvert |
| `TestGrayscaleRendering` | Same range with `-g` grayscale mode |
| `TestSequenceMode` | HarfBuzz `-S` space-separated mode with 3-glyph ABC sequence |
| `TestCommaSequence` | `-S` comma-separated multi-glyph syntax: count, content, backward compat |
| `TestColorEmoji` | Smileys 0x1F600–0x1F60F from NotoColorEmoji (BGRA→1-bit pipeline) |
| `TestColorEmojiFlags` | All 258 ISO 3166-1 country flags from NotoColorEmoji (comma-separated `-S`) |
| `TestFlagDitheringVariants` | All 5 dithering modes × 3 exposure values for flags — writes 15 contact-sheet PNGs |
| `TestAllFirmwareFonts` | Every `*.h` GFXfont under `base/fonts/` — parses, validates, **writes a contact-sheet PNG** to `tests/font_test_output/` |

Run just the firmware-font sweep with PNG output:

```bash
cd tests
pytest -v -k TestAllFirmwareFonts
# → writes one PNG per font to tests/font_test_output/
```

Visual inspection of a single header:

```bash
python tests/visualize_font.py path/to/Font.h --out-dir out/

# Generate + visualize in one step
python tests/visualize_font.py \
    --generate-from /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
    --fontconvert cmake-build-debug/fontconvert --size 14 --out-dir out/

# Scan entire generated/ directory and render all headers
python tests/visualize_font.py --scan-dir
```
