#pragma once
#include <stdint.h>

typedef enum {
	DITHER_FLOYD_STEINBERG = 0,
	DITHER_STUCKI,
	DITHER_BAYER,
	DITHER_THRESHOLD,
	DITHER_RANDOM,
} DitherMode;

typedef enum {
	HINT_NATIVE = 0, /* -Hnative: let FreeType decide (the historical default).
	                    For a TrueType face this means the bytecode interpreter,
	                    which grid-fits only if the font actually CARRIES hinting
	                    instructions. */
	HINT_AUTO,       /* -Hauto: FT_LOAD_FORCE_AUTOHINT — FreeType's own autohinter
	                    grid-fits the outline in BOTH axes regardless of what the
	                    font ships. */
	HINT_NONE,       /* -Hnone: FT_LOAD_NO_HINTING — raw scaled outline. */
} HintMode;

typedef struct settings {
	int num_ranges;
	int size;
	int pixel_size; /* -p: set the em size in PIXELS directly (0 = use -s points).
	                   -s is points at a fixed 141 DPI, so it can only land on
	                   ppem = round(size * 141/72) — i.e. 12, 14, 16, 18, ...  Every
	                   odd ppem is unreachable, and down at OLED sizes that is a
	                   ~14% jump per step with nothing in between, so the size whose
	                   grid-fitted cap-height actually fits the layout often simply
	                   is not expressible in points.  -p addresses the raster grid
	                   directly. */
	int height;
	int yadvance;   /* -Y: override the emitted GFXfont yAdvance independently of -r
	                   (0 = use -r/native).  -r sets both rendered pixel size and
	                   yAdvance; -Y changes only the emitted yAdvance, so a glyph
	                   can be rasterised at one size but positioned (via the
	                   consumer's baseline + (yAdvance - base) math) as if
	                   taller/shorter — e.g. shift colour-emoji down to clear the
	                   keycap top.  Does not rescale the bitmap. */
	int xshift;     /* -X: add N (may be negative) to every non-empty glyph's
	                   emitted xOffset — a horizontal nudge to re-centre a glyph
	                   the consumer draws with fixed leading padding (e.g. the
	                   keycap's 2-space emoji prefix shoves a wide portrait glyph
	                   off the right edge; -X-12 pulls it back to centre). */
	int max_width;
	int weight;
	int offset;
	int bits;       /* codepoint width of the emitted GFXfont first/last:
	                   16 (default) or 32. 32 allows SMP codepoints (> 0xFFFF)
	                   to be written directly, with no -n PUA shift. */
	int render_mode;
	int dump_codepoints;
	char *sequence;
	int seq_first;  /* base codepoint for the emitted GFXfont in sequence (-S)
	                   mode: `first` is set to this value (default 0) and `last`
	                   to first + glyph_count - 1.  Lets HarfBuzz-shaped
	                   sequences (flags, ZWJ emoji) be addressed at a stable
	                   codepoint range (e.g. a Private-Use base) instead of the
	                   synthetic indices 0..N-1, which collide with control
	                   codes when the consumer renders them as text. */
	DitherMode dither_mode;
	float exposure;
	float contrast;
	float gamma_val;
	float saturation_boost;
	float sharpness;
	int outline;
	int invert;     /* -I: invert the dithered bits inside the alpha mask (colour
	                   glyphs).  Bright areas go dark and dark areas light, giving
	                   an outline/icon look (pairs with -E and -O).  Background
	                   (transparent) stays dark. */
	int edge_preserve; /* -E: overlay the glyph's interior feature edges (Sobel on
	                   the post-adjustment gray, restricted to the interior away
	                   from the alpha boundary so -O stays a clean 1px outline)
	                   onto the dithered bits, forced lit — keeps eyes/mouth/etc.
	                   crisp instead of dissolving into dither. Colour glyphs. */
	int normalize;  /* -N: per-glyph auto-levels.  Before dithering, scale each
	                   glyph's gray buffer so its brightest pixel maps to white
	                   (v /= max), keeping black at 0.  Recovers dark-colour
	                   emoji (e.g. a dark-red face) whose low luminance would
	                   otherwise dither down to a few scattered dots. */
	int composite;  /* sequence (-S) mode: when set (-C), composite ALL glyphs of
	                   each comma-separated group into ONE bitmap using HarfBuzz
	                   GPOS positions, emitting a single GFXglyph per group.  Lets a
	                   base+mark cluster (e.g. dotted-circle U+25CC + a Devanagari
	                   matra) render as one addressable glyph with the mark correctly
	                   attached, instead of separate side-by-side glyphs.  Mono path
	                   only. */
	HintMode hinting; /* -H: how the outline is grid-fitted before rasterising.
	                   Matters most in the 1-bit mono path at small pixel sizes,
	                   where every stem is 1-2 px: without grid-fitting, stem
	                   edges land on arbitrary fractional positions and round
	                   independently, so nominally identical stems come out 1 px
	                   on one side and 2 px on the other and round bowls go
	                   lopsided.  HINT_NATIVE only helps when the face carries
	                   bytecode; many modern variable fonts (e.g. Noto Sans) ship
	                   NONE, and FreeType will not fall back to its autohinter on
	                   its own if the face has even a stub `prep` table — so an
	                   unhinted face renders unhinted unless -Hauto is asked for
	                   explicitly.  Default stays HINT_NATIVE so existing
	                   generated headers stay byte-identical. */
} FontSettings;

typedef struct ch_range {
	unsigned long first;
	unsigned long last;
} ch_range;

typedef struct glyph_name {
	char name[32];
} glyph_name;

extern FontSettings s;
