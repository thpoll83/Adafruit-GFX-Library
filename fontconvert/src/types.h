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
} FontSettings;

typedef struct ch_range {
	unsigned long first;
	unsigned long last;
} ch_range;

typedef struct glyph_name {
	char name[32];
} glyph_name;

extern FontSettings s;
