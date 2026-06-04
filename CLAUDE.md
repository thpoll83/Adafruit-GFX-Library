# CLAUDE.md — AdafruitGFX

This file provides guidance to Claude Code (claude.ai/code) when working in the **AdafruitGFX** repo. It contains the Adafruit GFX library used by the PolyKybd firmware to drive the per-keycap OLED displays, plus the standalone `fontconvert` tool used to generate font headers for the firmware.

For cross-repo context (how this repo relates to `PolyKybdHost/` and `qmk_firmware/`), see [`../CLAUDE.md`](../CLAUDE.md).

## fontconvert tool (`fontconvert/`)

Standalone C tool that converts TTF/OTF fonts into Adafruit GFX `.h` bitmap headers for use in the firmware. Built with CMake; links FreeType 2.13.3 and HarfBuzz 2.6.7 (both built as static libs via `ExternalProject` in `freetype-hb/CMakeLists.txt`).

Full build, install, and usage instructions: [`fontconvert/README.md`](fontconvert/README.md)

**Quick build:**
```bash
cd fontconvert/cmake-build-debug
cmake ..
cmake --build .
cmake --install .   # installs to ~/.local/bin/fontconvert
```

> **Claude Code on the web: the CMake build fails — build against system libs instead.**
> The `ExternalProject` downloads are blocked by the web session's network policy:
> FreeType comes from `download.savannah.gnu.org` and HarfBuzz from `www.freedesktop.org`,
> **both return HTTP 403** (verified 2026-06-04). `cmake --build` dies at the FreeType
> download step. Don't conclude the toolchain is unavailable — just skip the
> ExternalProject and compile directly against the distro packages:
> ```bash
> sudo apt-get install -y libfreetype-dev libharfbuzz-dev
> cd fontconvert/src
> gcc -O2 -I. $(pkg-config --cflags freetype2 harfbuzz) \
>     cli.c dither.c font_render.c fontconvert.c \
>     $(pkg-config --libs freetype2 harfbuzz) -lm \
>     -o /tmp/fontconvert
> ```
> Distro versions (FreeType 2.13.2, HarfBuzz 8.3.0) differ from the pinned 2.13.3 / 2.6.7
> but the APIs `fontconvert` uses are stable across them — builds clean and runs correctly.
> If you instead need the normal CMake build to work, **GitHub and SourceForge mirrors are
> reachable** (FreeType: `github.com/freetype/freetype` archive or `downloads.sourceforge.net`;
> HarfBuzz: `github.com/harfbuzz/harfbuzz/releases`) and can be added as fallback `URL`s in
> `freetype-hb/CMakeLists.txt` (no `URL_HASH` is pinned there, so swapping the URL is safe).

### Variable-font weight (`-w`)
Most Noto families are now variable-font-only on Google Fonts, and FreeType renders their
**default instance** when no axis is selected — which is often very light (NotoSansJP defaults
to Thin/100, NotoSerifKR to ExtraLight/200). Use `-w<N>` to pin the `wght` axis (e.g. `-w500`
for Medium, `-w700` for Bold). Implemented via `FT_Get_MM_Var` / `FT_Set_Var_Design_Coordinates`
in `fontconvert.c`; value is clamped to the axis range and ignored with a warning on non-variable
fonts. The firmware's JP & KR ranges (`create_fonts.sh`) pass `-w500`.

### Architecture of `fontconvert.c`
- `extract_range_ft()` — iterates a codepoint range, looks up each glyph with `FT_Get_Char_Index()`, renders via FreeType, calls `render_bitmap_to_bits()`
- `shape_and_render_sequence()` — parses space/comma-separated hex codepoints, shapes them with `hb_shape()` (HarfBuzz), then renders the resulting glyph IDs through FreeType. Use this for any emoji that is multiple Unicode codepoints (ZWJ sequences, regional indicator flag pairs, skin-tone/gender modifier combos).
- `render_bitmap_to_bits()` — shared pixel dispatcher: `FT_PIXEL_MODE_BGRA` → `bgra_to_gray_buf()` + `floyd_steinberg_to_bits()` (color emoji); `FT_PIXEL_MODE_GRAY` → random dither; monochrome → direct bit extraction
- `bgra_to_gray_buf()` — composites BGRA (straight alpha) over white using sRGB luminance weights
- `floyd_steinberg_to_bits()` — Floyd-Steinberg error diffusion to 1-bit via `enbit()`

### Output format
`GFXfont` / `GFXglyph` structs in `gfxfont.h`:
- Bitmap array: 1-bit packed, no per-scanline padding, byte-padded per glyph
- Glyph array: `{ bitmapOffset, width, height, xAdvance, xOffset, yOffset }`
- Font struct: `{ *bitmap, *glyphs, first, last, yAdvance }` — range mode uses actual codepoints for `first`/`last`; sequence mode uses `0` / `glyph_count-1`

### Tests (`fontconvert/tests/`)
```bash
pip install pypng pytest          # pypng only — no PIL/Pillow
cd fontconvert/tests
pytest -v                         # 18 pass; 5 skipped until NotoColorEmoji downloaded

# Visual inspection: renders all glyphs to PNG contact sheet
python visualize_font.py path/to/Font.h --out-dir out/
# Generate + visualize in one step
python visualize_font.py --generate-from /path/to/font.ttf \
    --fontconvert ../cmake-build-debug/fontconvert --size 14 --out-dir out/
# Sequence mode
python visualize_font.py --generate-from /path/to/NotoColorEmoji.ttf \
    --grayscale --sequence "1F1E9 1F1EA" --out-dir out/
```
Color emoji (`TestColorEmoji`) and flag sequence (`test_flag_sequence_via_harfbuzz`) tests are auto-skipped when `fonts/Noto_CEmoji/NotoColorEmoji-Regular.ttf` is absent.

## Firmware font generation (consumer)

The PolyKybd firmware uses `fontconvert` to generate `.h` font headers consumed by `keyboards/handwired/polykybd/base/fonts/`. Generation scripts live in the firmware repo:

- `qmk_firmware/keyboards/handwired/polykybd/fonts/dl-fonts.sh` — downloads Noto fonts
- `qmk_firmware/keyboards/handwired/polykybd/create_fonts.sh` — invokes `fontconvert` for each character range, writes `.h` files to `base/fonts/generated/`
- `base/fonts/gfx_used_fonts.h` — auto-generated index of all `.h` files, builds `ALL_FONTS[]` array

## Key notes

- **fontconvert dependencies**: FreeType and HarfBuzz are built from source as static libs inside `fontconvert/cmake-build-debug/freetype-hb/`; no system-level install required.
