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

# Grayscale / color-emoji  (-g)
fontconvert -f<FONT.ttf> -s20 -g -r50 -v _Emoji_ 0x1f600 0x1f64f

# HarfBuzz sequence mode  (-S) — ZWJ emoji, flag pairs, ligatures
fontconvert -f<FONT.ttf> -s20 -g -v _FlagDE_ -S "1F1E9 1F1EA"
```

| Flag | Meaning |
|------|---------|
| `-f<FILE>` | Font file (TTF/OTF) |
| `-s<N>` | Point size |
| `-v<NAME>` | Variant name embedded in the C identifiers |
| `-g` | Grayscale / BGRA color-emoji mode (Floyd-Steinberg dithering to 1-bit) |
| `-r<N>` | Render size override (pixels) |
| `-n<HEX>` | Offset added to codepoints in the output struct |
| `-S <seq>` | Space-separated hex codepoint sequence (HarfBuzz shaping) |

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
| `TestGrayscaleRendering` | Same range with Floyd-Steinberg grayscale (`-g`) |
| `TestSequenceMode` | HarfBuzz `-S` sequence mode with 3-glyph ABC sequence |
| `TestColorEmoji` | Smileys 0x1F600–0x1F60F from NotoColorEmoji (BGRA→1-bit pipeline) |
| `TestColorEmojiFlags` | All 258 ISO 3166-1 country flags from NotoColorEmoji |
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
