#pragma once
#include <stdint.h>

typedef enum {
	DITHER_FLOYD_STEINBERG = 0,
	DITHER_STUCKI,
	DITHER_BAYER,
	DITHER_THRESHOLD,
	DITHER_RANDOM,
} DitherMode;

typedef struct settings {
	int num_ranges;
	int size;
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
} FontSettings;

typedef struct ch_range {
	unsigned long first;
	unsigned long last;
} ch_range;

typedef struct glyph_name {
	char name[32];
} glyph_name;

extern FontSettings s;
