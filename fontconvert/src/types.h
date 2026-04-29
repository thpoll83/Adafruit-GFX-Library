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
	int offset;
	int render_mode;
	int dump_codepoints;
	char *sequence;
	DitherMode dither_mode;
	float exposure;
} FontSettings;

typedef struct ch_range {
	unsigned long first;
	unsigned long last;
} ch_range;

typedef struct glyph_name {
	char name[32];
} glyph_name;

extern FontSettings s;
