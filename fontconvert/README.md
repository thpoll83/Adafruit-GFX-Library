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

## Option reference and internals

_Moved from the repo `CLAUDE.md` on 2026-10-10, with stale paths and names corrected._

### Variable-font weight (`-w`)
Most Noto families are now variable-font-only on Google Fonts, and FreeType renders their
**default instance** when no axis is selected — which is often very light (NotoSansJP defaults
to Thin/100, NotoSerifKR to ExtraLight/200). Use `-w<N>` to pin the `wght` axis (e.g. `-w500`
for Medium, `-w700` for Bold). Implemented via `FT_Get_MM_Var` / `FT_Set_Var_Design_Coordinates`
in `fontconvert.c`; value is clamped to the axis range and ignored with a warning on non-variable
fonts. The firmware's JP & KR categories used to pass `-w500`; `fonts.yaml` now sets
`weight: 400, hinting: auto` for both.

### Grid-fitting (`-H`) and pixel-exact sizing (`-p`) — the small-text levers
`-H native|auto|none` picks the hinting applied before rasterising; `native` is the
default and keeps every previously generated header byte-identical. **Use `-Hauto`
for any small 1-bit UI text (≲20 px em)** — there a stem is 1–2 px, so ungridfitted
the two edges of one stem round independently and the same stem comes out 1 px here
and 2 px there, bowls go lopsided and crossbars drop. Two traps make this silent:
**NotoSans (and most modern variable fonts) ship NO hinting bytecode** —
`maxSizeOfInstructions == 0`, no `fpgm`, a 7-byte `prep` that only sets dropout
control — **and FreeType will not fall back to its autohinter** when a face has even
that stub `prep`. So `native` renders them completely ungridfitted, and the
`TT_INTERPRETER_VERSION_35` `fontconvert.c` sets for the mono path (`render_mode==1 ? 40 : 35`) is a no-op (no
bytecode to interpret). This was the cause of the PolyKybd status-OLED "numbers look
strange" report (2026-07).

`-p<N>` sets the em size in **pixels** instead of `-s` points-at-141-DPI. `-s` can
only reach `round(N*141/72)` — 12, 14, 16, 18 … — so every **odd ppem is
unreachable**, a ~14% jump per step at OLED sizes. Since grid-fitting snaps
cap-height to whole pixels, the reachable heights come in steps, and the size that
keeps an existing layout's footprint *while* gaining grid-fitting is frequently not
expressible in points. Always **measure** cap-height / x-height / worst-case string
widths against the previous header before adopting a size. Consumers: the PolyKybd
firmware's `fonts/gen-status-fonts.sh`.

### Range relocation (`-o`) — a second face of the SAME characters
`-o<N>` ADDs N to the emitted `GFXfont` `first`/`last` while still rendering the
real characters of the requested range. That is what lets a **second face of one
repertoire** live in a single front-to-back font table: the table is scanned in
order and the first font covering a codepoint wins, so a second 'a' at 0x61 could
never be reached — emitted at `0xF0000 + 0x61` it can. PolyKybd's bigger keycap
legend sizes are exactly this (`-o0xF0000` / `-o0xF3000` with `-b32`, one tier per
offset); the consumer adds the same base before its lookup.

⚠️ **Until 2026-08-20 `-o` SUBTRACTED** — it assigned `s.offset = -N`, making it an
exact alias of `-n` despite the help text saying "add". Nothing in the tree used it
(no `fonts.yaml` entry, no script), so correcting it changed no generated header —
but an older fontconvert binary will silently emit the wrong range for anything
relying on it. The range-mode guard already covers both directions (emitted
codepoint below 0, or above 0xFFFF without `-b32`).

`-F` is the sequence-mode (`-S`) equivalent; use `-o` for ranges, `-F` for sequences.

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

### Independent yAdvance (`-Y`)
`-r` sets **both** the rendered pixel size and the emitted `GFXfont` `yAdvance`.
`-Y<N>` overrides **only** the emitted `yAdvance` (0 = use `-r`/native), so a glyph
can be rasterised at one size but vertically *positioned* as if taller/shorter —
the consumer draws at `baseline + (yAdvance - base_yAdvance) + yOffset`. PolyKybd's
colour-emoji category (`emoji_fig` in `fonts.yaml`) uses `-r40 -Y48`: NotoColorEmoji glyphs fill the box and sit
high (`yOffset ≈ -31` at 40 px), clipping the 40 px keycap top at every size; the
larger `yAdvance` shifts the full-height glyph down ~8 px so it lands at y=0..40 with
zero clipping. Implemented in `fontconvert.c` (the `emit_yadv` override in both the
range- and sequence-mode footers); `-Y` does not rescale the bitmap.

### Per-glyph auto-levels (`-N`)
`-N` normalizes each glyph independently *before* dithering so that a chosen
**white point** maps to white (`v /= ref`), keeping black at 0 (skipped when the
ref is already ~1). Because the colour path composites BGRA over **black**
(`gray = a*lum`), a dark-colour emoji (dark-red/purple face, eggplant, dark moon)
has low luminance and would dither down to a few scattered dots; `-N` stretches
it back to the full range so the shape reads. The white point is the **99th
percentile** brightness (256-bin histogram), not the absolute max, so a lone hot
pixel — e.g. a white sparkle on a dark object — can't cap the gain and leave the
body dark; the brightest ~1% clamp to white and the bulk stretches. For a glyph
with a broad bright region the 99th percentile equals the max (no regression);
the `NORM_PCT` constant in `dither.c` tunes it. Implemented as the first step of
`apply_dithering()`, so `-G`/`-c`/`-e`/`-D` still act on the normalized values.
PolyKybd's `emoji_fig` category sets `normalize: true`.

### Invert (`-I`) and edge-preserve (`-E`) — the "outlined icon" pipeline
Two composable colour-glyph post-processes (applied in `render_bitmap_to_bits`
after the dither, in the order **invert → edges → outline**):
- **`-I`** flips the dithered bits inside the alpha mask (bright↔dark), giving an
  outline/icon look. With `-N` the glyph is brightened first, so `-I` then
  *hollows* it (even, outlined look); without `-N` a dark glyph inverts to a
  solid fill. Background (transparent) stays dark.
- **`-E`** overlays the glyph's interior feature edges (gradient of the
  *pre-dither* gray, snapshotted in `apply_dithering` into `s_edge_gray`), forced
  lit, so eyes/mouth/details stay crisp. Edges are kept `EDGE_BAND` px clear of
  the alpha boundary so `-O1` remains a clean single-pixel outline (no doubling).
  `EDGE_THRESH`/`EDGE_BAND` in `dither.c` tune it.

PolyKybd's `emoji_fig` category uses `-N -I -E -O1 -Dfs -r40 -Y48` ("col4" from the
visual study): per-glyph auto-levels, invert to an outlined icon, crisp interior
edges, 1px silhouette. `-I`/`-E` are BGRA-only; `-O` still works on gray/mono.

### Architecture of the renderer (`font_render.c`, `dither.c`)
- `extract_range_ft()` (`font_render.c`) — iterates a codepoint range, looks up each glyph with `FT_Get_Char_Index()`, renders via FreeType, calls `render_bitmap_to_bits()`
- `shape_and_render_sequence()` (`font_render.c`) — parses space/comma-separated hex codepoints, shapes them with `hb_shape()` (HarfBuzz), then renders the resulting glyph IDs through FreeType. Use this for any emoji that is multiple Unicode codepoints (ZWJ sequences, regional indicator flag pairs, skin-tone/gender modifier combos).
- `render_bitmap_to_bits()` (`dither.c`) — shared pixel dispatcher: `FT_PIXEL_MODE_BGRA` → `bgra_to_gray_buf()` + `apply_dithering()` (color emoji; `-D` picks e.g. `dither_floyd_steinberg()`); `FT_PIXEL_MODE_GRAY` → random dither; monochrome → direct bit extraction
- `bgra_to_gray_buf()` (`dither.c`) — composites BGRA (straight alpha) over **black** (`gray = a*lum`) using sRGB luminance weights
- `dither_floyd_steinberg()` (`dither.c`) — Floyd-Steinberg error diffusion; bits are emitted via `enbit()`

### Output format
`GFXfont` / `GFXglyph` structs in `gfxfont.h`:
- Bitmap array: **COLUMN-NATIVE (OLED page)** 1-bit packed. 1 byte = 8 **vertical**
  pixels; `cb = (height + 7) / 8` page-bytes per column, so a glyph is `width * cb`
  whole bytes (byte-padded per glyph). Index as `byte = bitmapOffset + x*cb + (y>>3)`,
  bit `1 << (y & 7)` — **LSB = top of the page**. This matches the SSD1306 page
  memory layout so the PolyKybd firmware can `memcpy` a whole column-byte at once
  instead of setting pixels one at a time. ⚠️ This is **NOT** the classic Adafruit
  row-major layout (`bit = y*width + x`, MSB-first); the dither/edge/outline pipeline
  still works row-major internally (`enbit` / the `s_cap` capture), then `emit_buf_col`
  (`dither.c`) transposes to column-native at the single per-glyph emit point.
- Glyph array: `{ bitmapOffset, width, height, xAdvance, xOffset, yOffset }`
- Font struct: `{ *bitmap, *glyphs, first, last, yAdvance }` — range mode uses actual codepoints for `first`/`last`; sequence mode uses `first` = the `-F` base codepoint (default 0) and `last` = `first + glyph_count - 1`
