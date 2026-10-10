# CLAUDE.md: Adafruit-GFX-Library (fontconvert)

The Adafruit GFX library fork used by the PolyKybd firmware, plus `fontconvert/`, the C
tool that turns TTF/OTF fonts into the GFX `.h` headers the firmware compiles.

**Rules for all PolyKybd repos** are in `../polykybd-claude/CLAUDE.md`. If that repo is
not attached, ask the user to attach `thpoll83/polykybd-claude`. Default branch: `master`.
Build, install, usage and the full option reference (`-w -H -p -o -F -O -Y -N -I -E`,
the renderer's architecture, the output format):
[`fontconvert/README.md`](fontconvert/README.md).

## Building fontconvert

CMake, linking FreeType 2.13.3 and HarfBuzz 2.6.7 built as static libs through
`ExternalProject` (`freetype-hb/CMakeLists.txt`):

```bash
cd fontconvert && mkdir -p cmake-build-debug && cd cmake-build-debug
cmake .. && cmake --build . && cmake --install .   # -> ~/.local/bin/fontconvert
```

- **This pinned build is the one that reproduces the firmware's committed headers byte
  for byte.** Use it to regenerate them.
- **In the web session the primary download hosts return 403** (savannah, freedesktop).
  `freetype-hb/CMakeLists.txt` lists fallback URLs that work: SourceForge/GitHub for
  FreeType, the GitHub *release* tarball for HarfBuzz (the source archive lacks
  `configure`). Don't conclude the toolchain is unavailable after one 403.
- **FreeType is built with `-DFT_DISABLE_BZIP2=TRUE -DFT_DISABLE_BROTLI=TRUE`**, or it
  picks up the distro headers and the link fails.
- **Fast path, not byte-reproducible**: build against distro libs (FreeType 2.13.2,
  HarfBuzz 8.3.0), which render a few glyphs ~1 px differently:
  ```bash
  sudo apt-get install -y libfreetype-dev libharfbuzz-dev
  cd fontconvert/src && gcc -O2 -I. $(pkg-config --cflags freetype2 harfbuzz) \
      cli.c dither.c font_render.c fontconvert.c \
      $(pkg-config --libs freetype2 harfbuzz) -lm -o /tmp/fontconvert
  ```
- ⚠️ **Keep the `.gitignore` entry a glob (`fontconvert/cmake-build-*/`).** It once
  listed one name, and `cmake-build-pinned/` was committed by accident: 5,224 files,
  11 MB of FreeType and HarfBuzz sources, found only because a scanner flagged an
  `eval()` inside it. `git rm -r --cached` untracks without deleting.

## Rules that are easy to get wrong

- ⚠️ **The bitmap is column-native (SSD1306 page layout), not Adafruit's row-major.**
  `byte = bitmapOffset + x*cb + (y>>3)`, bit `1 << (y & 7)`, LSB at the top. The
  pipeline works row-major internally and `emit_buf_col()` transposes at the one emit
  point.
- ⚠️ **Use `-Hauto` for small 1-bit text** (below ~20 px em). NotoSans ships no hinting
  bytecode and FreeType won't fall back to its autohinter, so `native` (the default)
  renders ungridfitted. This was the status-OLED "numbers look strange" report.
- **`-p<N>` sets the em in pixels.** `-s` points at 141 DPI cannot reach odd ppem.
  Measure cap-height and string widths against the previous header before adopting a
  size.
- ⚠️ **`-o` ADDS its offset since 2026-08-20; before that it subtracted.** An older
  binary silently emits the wrong range.
- **Colour (BGRA) glyphs composite over black** (`gray = a*lum`), so a dark emoji needs
  `-N` to read.
- **Variable fonts render their default instance** (often Thin) unless `-w<N>` pins
  `wght`.

## Tests

```bash
pip install pypng pytest     # pypng only, no Pillow
cd fontconvert/tests && pytest -v
python visualize_font.py path/to/Font.h --out-dir out/   # contact sheet of every glyph
```

- ⚠️ **Several tests skip silently.** The colour-emoji and flag tests need
  `fonts/Noto_CEmoji/NotoColorEmoji-Regular.ttf`. `test_gfx_fonts.py` still points at
  the firmware's old `keyboards/handwired/polykybd/` path and a fixture
  (`0NotoSans_Regular_Base_14pt.h`) that no longer exists, so its firmware-font tests
  never run. Read the skip count, not the pass count.

## The firmware side

The firmware drives generation from `qmk_firmware/keyboards/polykybd/fonts/fonts.yaml`
with `generate_fonts.py` (`--check` flags stale headers); `fontconvert` itself reads no
config. Output goes to `keyboards/polykybd/base/fonts/generated/`, composed by
`gfx_used_fonts.h` (`RESIDENT_FONTS[]`). Details: the qmk `fonts/README.md` and the
Fonts section of `qmk_firmware/CLAUDE.md`.
