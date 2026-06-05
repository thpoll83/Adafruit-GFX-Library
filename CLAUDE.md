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

> **Claude Code on the web: the CMake build works, but only via fallback mirrors.**
> The `ExternalProject` primary download hosts are blocked by the web session's network
> policy: FreeType's `download.savannah.gnu.org` and HarfBuzz's `www.freedesktop.org`
> **both return HTTP 403** (verified 2026-06-04). `freetype-hb/CMakeLists.txt` therefore
> lists fallback `URL`s (CMake tries each in order): FreeType from SourceForge / GitHub,
> HarfBuzz from GitHub *releases* (must be the release asset, not the source archive — the
> Linux build runs `<SOURCE_DIR>/configure`, which only the release tarball ships). With
> those in place the normal `cmake .. && cmake --build .` succeeds end-to-end.
>   - The FreeType ExternalProjects also pass `-DFT_DISABLE_BZIP2=TRUE -DFT_DISABLE_BROTLI=TRUE`
>     (alongside the existing ZLIB/PNG disables). Without them, FreeType auto-detects the
>     distro's bzip2/brotli *headers* and references `BZ2_*` / `BrotliDecoderDecompress`, which
>     aren't on the link line → final link fails. The tool needs none of these.
>   - Don't conclude the toolchain is unavailable if a download 403s — check the mirror list.
>
> **Fast alternative — skip the ExternalProject, build against distro libs directly:**
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
> Handy when you just need the binary quickly and don't want to wait on the full EP build.

### Variable-font weight (`-w`)
Most Noto families are now variable-font-only on Google Fonts, and FreeType renders their
**default instance** when no axis is selected — which is often very light (NotoSansJP defaults
to Thin/100, NotoSerifKR to ExtraLight/200). Use `-w<N>` to pin the `wght` axis (e.g. `-w500`
for Medium, `-w700` for Bold). Implemented via `FT_Get_MM_Var` / `FT_Set_Var_Design_Coordinates`
in `fontconvert.c`; value is clamped to the axis range and ignored with a warning on non-variable
fonts. The firmware's JP & KR ranges (`create_fonts.sh`) pass `-w500`.

### Sequence base codepoint (`-F`) and colour-glyph outline (`-O`)
`-F<cp>` sets the emitted `GFXfont`'s `first` in **sequence (`-S`) mode** to `cp`
(default 0), with `last = cp + count - 1` — so HarfBuzz-shaped sequences (flags,
ZWJ emoji) get a stable codepoint range (e.g. a Private-Use base `-F0xE000`)
instead of the synthetic indices `0..N-1`, which collide with control codes when
the firmware renders them as text. PolyKybd's language-layer flags use this (see
`qmk_firmware/.../fonts/gen-lang-fonts.sh`).

`-O<N>` on **colour (BGRA) glyphs** draws an N-pixel border on the *inside* of
the alpha boundary, so `-O1` is a true 1px outline (not the old two-sided ~2px
band); it outlines the flag on a dark background and seals the white-flag
dithering fringe (JP, KR). Colour strikes (NotoColorEmoji) render at a fixed
native size that `-r`/`-W` then shrinks, so `font_render.c` rescales the glyph
metrics (advance/left/top) to the emitted bitmap — without it a downscaled colour
glyph reports a ~5× too-large `xAdvance`/`yOffset`.

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

The PolyKybd firmware uses `fontconvert` to generate `.h` font headers consumed by `keyboards/handwired/polykybd/base/fonts/`. Generation is **config-driven** and lives in the firmware repo (`fontconvert` itself stays a focused single-font tool — it does **not** read the config):

- `qmk_firmware/keyboards/handwired/polykybd/fonts/fonts.yaml` — single source of truth: an ordered list of font entries (font file, size, variant, ranges, weight, bits, …) grouped into categories. List order = `ALL_FONTS[]` priority.
- `qmk_firmware/keyboards/handwired/polykybd/fonts/generate_fonts.py` — reads the YAML, invokes `fontconvert` once per entry, writes one header per category to `base/fonts/generated/`, and composes `base/fonts/gfx_used_fonts.h` (the `ALL_FONTS[]` table). `--check` mode flags stale headers for CI.
- `qmk_firmware/keyboards/handwired/polykybd/fonts/dl-fonts.sh` — downloads Noto fonts.
- `create_fonts.sh` is now a thin deprecated wrapper forwarding to `generate_fonts.py`.

**Byte-reproducibility note:** the firmware's committed headers are built with the **pinned** `fontconvert` (FreeType 2.13.3 / HarfBuzz 2.6.7 — the CMake ExternalProject below). The distro fast-path build (2.13.2 / 8.3.0) renders a handful of glyphs ~1px differently, so use the pinned build to regenerate without spurious diffs.

## Key notes

- **fontconvert dependencies**: FreeType and HarfBuzz are built from source as static libs inside `fontconvert/cmake-build-debug/freetype-hb/`; no system-level install required.
