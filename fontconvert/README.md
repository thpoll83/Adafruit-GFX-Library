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
| `-s<N>` | Point size |
| `-v<NAME>` | Variant name embedded in the C identifiers |
| `-g` | Grayscale / BGRA color-emoji mode — quantises to 1-bit via `-D` algorithm |
| `-D<MODE>` | Dithering algorithm: `fs` (default), `stucki`, `bayer`, `threshold`, `random` |
| `-e<N>` | Exposure bias before dithering (−1.0 to 1.0, default 0.0) |
| `-r<N>` | Render-size override in pixels (useful for bitmap-only fonts like NotoColorEmoji) |
| `-W<N>` | Maximum rendered width in pixels; glyph is scaled down if wider |
| `-o<N>` / `-n<N>` | Positive / negative offset added to codepoints in the output struct |
| `-S <seq>` | HarfBuzz sequence: `"G[,G]..."` where G = space-separated hex codepoints |
| `-d` | Dump all codepoints in the font and exit |

Output is written to stdout; redirect to a `.h` file:

```bash
fontconvert -f fonts/noto-sans/NotoSans-Regular.ttf -s14 -v _Base_ 0x20 0x7e \
    > base/fonts/generated/NotoSans_Regular_Base_14pt.h
```

## Tests

```bash
pip install pypng pytest
cd tests
pytest -v
```

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
