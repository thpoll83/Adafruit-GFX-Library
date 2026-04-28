/*
TrueType to Adafruit_GFX font converter.  Derived from Peter Jakobs'
Adafruit_ftGFX fork & makefont tool, and Paul Kourany's Adafruit_mfGFX.

NOT AN ARDUINO SKETCH.  This is a command-line tool for preprocessing
fonts to be used with the Adafruit_GFX Arduino library.

For UNIX-like systems.  Outputs to stdout; redirect to header file, e.g.:
	./fontconvert -f~/Library/Fonts/FreeSans.ttf -s18 > FreeSans18pt7b.h

REQUIRES FREETYPE LIBRARY.  www.freetype.org

See notes at end for glyph nomenclature & other tidbits.
*/
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <ft2build.h>
#include <getopt.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_TRUETYPE_DRIVER_H
#include "../../gfxfont.h" // Adafruit_GFX font structures


#include <hb.h>
#include <hb-ft.h>


#define DPI 141 // Approximate res. of Adafruit 2.8" TFT

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

static FontSettings s = {
	.num_ranges = 0,
	.size = 12,
	.height = 0,
	.max_width = 0,
	.offset = 0,
	.render_mode = 0,
	.dump_codepoints = 0,
	.sequence = NULL,
	.dither_mode = DITHER_FLOYD_STEINBERG,
	.exposure = 0.0f,
};

extern char* optarg;
extern int optind, opterr, optopt;

typedef struct ch_range {
	FT_ULong first;
	FT_ULong last;
} ch_range;

typedef struct glyph_name {
	char name[32];
} glyph_name;

// Accumulate bits for output, with periodic hexadecimal byte write
void enbit(uint8_t value) {
	static uint8_t row = 0, sum = 0, bit = 0x80, firstCall = 1;
	if (value)
		sum |= bit; // Set bit if needed
	if (!(bit >>= 1)) { // Advance to next bit, end of byte reached?
		if (!firstCall) { // Format output table nicely
			if (++row >= 12) { // Last entry on line?
				printf(",\n  "); //   Newline format output
				row = 0; //   Reset row counter
			} else { // Not end of line
				printf(", "); //   Simple comma delim
			}
		}
		printf("0x%02X", sum); // Write byte value
		sum = 0; // Clear for next byte
		bit = 0x80; // Reset bit counter
		firstCall = 0; // Formatting flag
	}
}

int to_int(const char* str) {
	int result;
	if (strncmp("0x", str, 2) == 0) {
		sscanf(str, "%x", &result);
	} else {
		sscanf(str, "%d", &result);
	}
	return result;
}

unsigned long to_ulong(const char* str) {
	unsigned long result;
	if (strncmp("0x", str, 2) == 0) {
		sscanf(str, "%lx", &result);
	} else {
		sscanf(str, "%lu", &result);
	}
	return result;
}

void range_swap_if_needed(ch_range* range) {
	if (range->last < range->first) {
		int swap = range->first;
		range->first = range->last;
		range->last = swap;
	}
}

int range_count(const ch_range* range) {
	return range->last - range->first + 1;
}

// Convert a BGRA bitmap (straight alpha, as returned by FreeType for CBDT/color fonts)
// to a float grayscale buffer (0.0=black, 1.0=white) composited over a white background.
static float *bgra_to_gray_buf(const uint8_t *buf, int pitch, int width, int rows) {
	float *gray = (float *)malloc(width * rows * sizeof(float));
	if (!gray) return NULL;
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < width; x++) {
			const uint8_t *p = buf + y * pitch + x * 4;
			float b = p[0] / 255.0f;
			float g = p[1] / 255.0f;
			float r = p[2] / 255.0f;
			float a = p[3] / 255.0f;
			// Composite over white background (straight alpha)
			float ro = r * a + (1.0f - a);
			float go = g * a + (1.0f - a);
			float bo = b * a + (1.0f - a);
			// sRGB luminance weights
			gray[y * width + x] = 0.2126f * ro + 0.7152f * go + 0.0722f * bo;
		}
	}
	return gray;
}

// Floyd-Steinberg error diffusion to 1-bit via enbit().
// Standard kernel:  forward 7/16, below-left 3/16, below 5/16, below-right 1/16
static void dither_floyd_steinberg(float *gray, int width, int rows) {
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < width; x++) {
			float old = gray[y * width + x];
			if (old < 0.0f) old = 0.0f;
			if (old > 1.0f) old = 1.0f;
			float newval = (old >= 0.5f) ? 1.0f : 0.0f;
			enbit((uint8_t)(newval >= 0.5f ? 1 : 0));
			float err = old - newval;
			if (x + 1 < width)
				gray[y * width + x + 1]           += err * (7.0f / 16.0f);
			if (y + 1 < rows) {
				if (x > 0)
					gray[(y + 1) * width + x - 1] += err * (3.0f / 16.0f);
				gray[(y + 1) * width + x]          += err * (5.0f / 16.0f);
				if (x + 1 < width)
					gray[(y + 1) * width + x + 1]  += err * (1.0f / 16.0f);
			}
		}
	}
}

// Stucki error diffusion — wider kernel than Floyd-Steinberg (divisor 42),
// smoother gradients on larger glyphs.
// Kernel (row offsets +1 and +2):
//                    *   8/42  4/42
//   2/42 4/42 8/42  4/42 2/42
//   1/42 2/42 4/42  2/42 1/42
static void dither_stucki(float *gray, int width, int rows) {
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < width; x++) {
			float old = gray[y * width + x];
			if (old < 0.0f) old = 0.0f;
			if (old > 1.0f) old = 1.0f;
			float newval = old >= 0.5f ? 1.0f : 0.0f;
			enbit((uint8_t)(newval >= 0.5f ? 1 : 0));
			float err = old - newval;
#define S(dy, dx, w) \
	do { \
		int nx = x + (dx), ny = y + (dy); \
		if (nx >= 0 && nx < width && ny < rows) \
			gray[ny * width + nx] += err * (w) / 42.0f; \
	} while (0)
			S(0, 1, 8); S(0, 2, 4);
			S(1,-2, 2); S(1,-1, 4); S(1, 0, 8); S(1, 1, 4); S(1, 2, 2);
			S(2,-2, 1); S(2,-1, 2); S(2, 0, 4); S(2, 1, 2); S(2, 2, 1);
#undef S
		}
	}
}

// Bayer ordered dithering (4×4 matrix).  No error propagation — each pixel is
// compared against a spatially-varying threshold, giving a regular dot pattern.
static void dither_bayer(float *gray, int width, int rows) {
	static const float mat[4][4] = {
		{  0.5f/16.0f,  8.5f/16.0f,  2.5f/16.0f, 10.5f/16.0f },
		{ 12.5f/16.0f,  4.5f/16.0f, 14.5f/16.0f,  6.5f/16.0f },
		{  3.5f/16.0f, 11.5f/16.0f,  1.5f/16.0f,  9.5f/16.0f },
		{ 15.5f/16.0f,  7.5f/16.0f, 13.5f/16.0f,  5.5f/16.0f },
	};
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < width; x++)
			enbit(gray[y * width + x] >= mat[y & 3][x & 3] ? 1 : 0);
}

// Simple threshold at 0.5 — no dithering, hard edge.
static void dither_threshold(float *gray, int width, int rows) {
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < width; x++)
			enbit(gray[y * width + x] >= 0.5f ? 1 : 0);
}

// Random (stochastic) dithering — probability of white proportional to value.
static void dither_random(float *gray, int width, int rows) {
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < width; x++) {
			float v = gray[y * width + x];
			if (v <= 0.0f)      enbit(0);
			else if (v >= 1.0f) enbit(1);
			else                enbit((rand() % 256) < (int)(v * 256.0f) ? 1 : 0);
		}
}

// Apply exposure bias then dispatch to the selected dithering algorithm.
// exposure > 0 biases toward white; exposure < 0 biases toward black.
static void apply_dithering(float *gray, int width, int rows) {
	if (s.exposure != 0.0f) {
		for (int i = 0; i < width * rows; i++) {
			float v = gray[i] + s.exposure;
			gray[i] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
		}
	}
	switch (s.dither_mode) {
		case DITHER_STUCKI:           dither_stucki(gray, width, rows);           break;
		case DITHER_BAYER:            dither_bayer(gray, width, rows);            break;
		case DITHER_THRESHOLD:        dither_threshold(gray, width, rows);        break;
		case DITHER_RANDOM:           dither_random(gray, width, rows);           break;
		case DITHER_FLOYD_STEINBERG:
		default:                      dither_floyd_steinberg(gray, width, rows);  break;
	}
}

// Convert an 8-bit gray FreeType bitmap to a float buffer (0.0=black, 1.0=white).
static float *gray8_to_float_buf(const uint8_t *buf, int pitch, int width, int rows) {
	float *gray = (float *)malloc(width * rows * sizeof(float));
	if (!gray) return NULL;
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < width; x++)
			gray[y * width + x] = buf[y * pitch + x] / 255.0f;
	return gray;
}

// Bilinear downscale of a float grayscale buffer to dst_w x dst_h.
// Returns a newly allocated buffer; caller must free.
static float *scale_gray_buf(const float *src, int src_w, int src_h,
                              int dst_w, int dst_h) {
	float *dst = (float *)malloc(dst_w * dst_h * sizeof(float));
	if (!dst) return NULL;
	for (int dy = 0; dy < dst_h; dy++) {
		float sy = (float)dy * (src_h - 1) / (dst_h - 1 > 0 ? dst_h - 1 : 1);
		int sy0 = (int)sy, sy1 = sy0 + 1 < src_h ? sy0 + 1 : sy0;
		float fy = sy - sy0;
		for (int dx = 0; dx < dst_w; dx++) {
			float sx = (float)dx * (src_w - 1) / (dst_w - 1 > 0 ? dst_w - 1 : 1);
			int sx0 = (int)sx, sx1 = sx0 + 1 < src_w ? sx0 + 1 : sx0;
			float fx = sx - sx0;
			dst[dy * dst_w + dx] =
				src[sy0 * src_w + sx0] * (1 - fx) * (1 - fy) +
				src[sy0 * src_w + sx1] * fx       * (1 - fy) +
				src[sy1 * src_w + sx0] * (1 - fx) * fy       +
				src[sy1 * src_w + sx1] * fx       * fy;
		}
	}
	return dst;
}

// Compute scaled dimensions that fit within max_w x max_h, preserving aspect ratio.
static void fit_dimensions(int src_w, int src_h, int max_w, int max_h,
                            int *out_w, int *out_h) {
	*out_w = src_w; *out_h = src_h;
	if (max_h > 0 && src_h > max_h) {
		*out_w = src_w * max_h / src_h;
		*out_h = max_h;
	}
	if (max_w > 0 && *out_w > max_w) {
		*out_h = *out_h * max_w / *out_w;
		*out_w = max_w;
	}
	if (*out_w < 1) *out_w = 1;
	if (*out_h < 1) *out_h = 1;
}

// Shared pixel-to-bits renderer: dispatches on pixel_mode so both range and
// sequence paths use identical quantisation logic.
// Sets *out_w / *out_h to the actual pixel dimensions rendered (after scaling).
static void render_bitmap_to_bits(FT_Bitmap *bitmap, int *out_w, int *out_h) {
	int x, y;
	uint8_t bit;
	*out_w = (int)bitmap->width;
	*out_h = (int)bitmap->rows;
	if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
		float *gray_buf = bgra_to_gray_buf(bitmap->buffer, bitmap->pitch,
		                                   bitmap->width, bitmap->rows);
		if (gray_buf) {
			int dst_w, dst_h;
			fit_dimensions(bitmap->width, bitmap->rows,
			               s.max_width, s.height, &dst_w, &dst_h);
			if (dst_w != (int)bitmap->width || dst_h != (int)bitmap->rows) {
				float *scaled = scale_gray_buf(gray_buf, bitmap->width, bitmap->rows,
				                              dst_w, dst_h);
				free(gray_buf);
				gray_buf = scaled;
				fprintf(stderr, "Info: scaled %dx%d → %dx%d\n",
				        bitmap->width, bitmap->rows, dst_w, dst_h);
			}
			if (gray_buf) {
				*out_w = dst_w; *out_h = dst_h;
				apply_dithering(gray_buf, dst_w, dst_h);
				free(gray_buf);
			}
		}
	} else if (s.render_mode == 1) {
		float *gray_buf = gray8_to_float_buf(bitmap->buffer, bitmap->pitch,
		                                     bitmap->width, bitmap->rows);
		if (gray_buf) {
			apply_dithering(gray_buf, bitmap->width, bitmap->rows);
			free(gray_buf);
		}
	} else {
		for (y = 0; y < (int)bitmap->rows; y++)
			for (x = 0; x < (int)bitmap->width; x++) {
				int byte = x / 8;
				bit  = 0x80 >> (x & 7);
				enbit(bitmap->buffer[y * bitmap->pitch + byte] & bit);
			}
	}
}

// For outline fonts use FT_Set_Char_Size; for bitmap-only faces (e.g. CBDT/CBLC
// color emoji) select the strike whose y_ppem is closest to the requested size.
static void setup_face_size(FT_Face face) {
	if (face->num_fixed_sizes > 0) {
		int target_px = (s.size * DPI + 36) / 72;
		int best = 0, best_diff = INT_MAX;
		for (int i = 0; i < face->num_fixed_sizes; i++) {
			int diff = abs((face->available_sizes[i].y_ppem >> 6) - target_px);
			if (diff < best_diff) { best_diff = diff; best = i; }
		}
		FT_Select_Size(face, best);
		fprintf(stderr, "Info: bitmap font — selected strike %d (%dx%d px)\n",
		        best,
		        face->available_sizes[best].x_ppem >> 6,
		        face->available_sizes[best].y_ppem >> 6);
	} else {
		FT_Set_Char_Size(face, s.size << 6, 0, DPI, 0);
	}
}

int extract_range_ft(GFXglyph* table, glyph_name* names, FT_Face face,
									FT_ULong first, FT_ULong last, int* bitmapOffset) {
	FT_ULong codepoint;
	int err;
	static int table_idx = 0;

	FT_Glyph glyph;
	FT_Bitmap* bitmap;
	FT_BitmapGlyphRec* rec;

	// << 6 because '26dot6' fixed-point format
	setup_face_size(face);

	// Process glyphs and output huge bitmap data array
	for (codepoint = first; codepoint <= last; codepoint++, table_idx++) {
		uint32_t glyph_index = FT_Get_Char_Index(face, codepoint);

		if (glyph_index == 0) {
			fprintf(stderr, "Error getting glyph index for codepoint '0x%lx'\n",
							codepoint);
			continue;
		}

		if ((err = FT_Load_Glyph(face, glyph_index,
														 s.render_mode == 1
															 ? FT_LOAD_TARGET_NORMAL | FT_LOAD_COLOR
															 : FT_LOAD_TARGET_MONO))) {
			fprintf(stderr,
							"Error %d loading codepoint '0x%lx' with glyph index %u\n", err,
							codepoint, glyph_index);
			continue;
		}

		// the name is optional
		if ((err =
			FT_Get_Glyph_Name(face, glyph_index, names[table_idx].name, 32))) {
			names[table_idx].name[0] = 0;
		}

		// FT_Load_Glyph with FT_LOAD_COLOR already deposits a BGRA bitmap for
		// color emoji fonts (CBDT/sbix); calling FT_Render_Glyph on it destroys
		// the bitmap and produces width=0.  Only render outline glyphs.
		if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
			if ((err = FT_Render_Glyph(face->glyph, s.render_mode == 1
																								? FT_RENDER_MODE_NORMAL
																								: FT_RENDER_MODE_MONO))) {
				fprintf(stderr, "Error %d rendering char '%lu'\n", err, codepoint);
				continue;
			}
		}

		if ((err = FT_Get_Glyph(face->glyph, &glyph))) {
			fprintf(stderr, "Error %d getting glyph '%lu'\n", err, codepoint);
			continue;
		}

		bitmap = &(face->glyph->bitmap);
		rec = (FT_BitmapGlyphRec*)glyph;

		// Minimal font and per-glyph information is stored to
		// reduce flash space requirements.  Glyph bitmaps are
		// fully bit-packed; no per-scanline pad, though end of
		// each character may be padded to next byte boundary
		if (*bitmapOffset > 0xFFFF)
			fprintf(stderr,
			        "Warning: bitmapOffset %d exceeds uint16_t max (65535) for "
			        "codepoint 0x%lx — GFXglyph.bitmapOffset will wrap; "
			        "split this font into smaller ranges.\n",
			        *bitmapOffset, codepoint);
		table[table_idx].bitmapOffset = *bitmapOffset;
		table[table_idx].xAdvance = face->glyph->advance.x >> 6;
		table[table_idx].xOffset = rec->left;
		table[table_idx].yOffset = 1 - rec->top;

		if (bitmap->rows == 0 || bitmap->width == 0) {
			fprintf(
				stderr,
				"Info: No pixeldata found for 0x%lx / Pixel mode: %d Num Grays: %d ",
				codepoint, bitmap->pixel_mode, bitmap->num_grays);
			fprintf(stderr, "W: %d H: %d Format: %c%c%c%c\n", bitmap->width,
							bitmap->rows, (char)(face->glyph->format >> 24),
							(char)(face->glyph->format >> 16),
							(char)(face->glyph->format >> 8), (char)(face->glyph->format));
			table[table_idx].width = 0;
			table[table_idx].height = 0;
			continue;
		}

		int out_w, out_h;
		render_bitmap_to_bits(bitmap, &out_w, &out_h);
		table[table_idx].width = out_w;
		table[table_idx].height = out_h;

		// Pad end of char bitmap to next byte boundary if needed
		int n = (out_w * out_h) & 7;
		if (n) { // Pixel count not an even multiple of 8?
			n = 8 - n; // # bits to next multiple
			while (n--)
				enbit(0);
		}
		*bitmapOffset += (out_w * out_h + 7) / 8;

		FT_Done_Glyph(glyph);
	}

	return 0;
}

// Shape one comma-separated group of space-delimited hex codepoints with HarfBuzz,
// render each resulting glyph into table[]/names[] starting at *written, and emit
// bitmap bits via enbit().  Returns 0 on success, -1 on fatal error.
static int shape_render_group(GFXglyph *table, glyph_name *names,
                               hb_font_t *hb_font, FT_Face face,
                               const char *group_str, int group_idx,
                               int *bitmapOffset, int *written) {
	uint32_t cps[512];
	int n_cp = 0;
	int err;

	char *cp_buf = strdup(group_str);
	char *tok = strtok(cp_buf, " \t");
	while (tok && n_cp < 512) {
		char *end;
		unsigned long v = strtoul(tok, &end, 16);
		if (end != tok)
			cps[n_cp++] = (uint32_t)v;
		tok = strtok(NULL, " \t");
	}
	free(cp_buf);

	if (n_cp == 0)
		return 0; // empty group (e.g. trailing comma) — skip silently

	hb_buffer_t *hb_buf = hb_buffer_create();
	hb_buffer_set_content_type(hb_buf, HB_BUFFER_CONTENT_TYPE_UNICODE);
	for (int i = 0; i < n_cp; i++)
		hb_buffer_add(hb_buf, cps[i], i);
	hb_buffer_set_direction(hb_buf, HB_DIRECTION_LTR);
	hb_buffer_guess_segment_properties(hb_buf);
	hb_shape(hb_font, hb_buf, NULL, 0);

	unsigned int glyph_count;
	hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(hb_buf, &glyph_count);
	fprintf(stderr, "HarfBuzz: group[%d] %d codepoint(s) → %u glyph(s)\n",
	        group_idx, n_cp, glyph_count);

	for (unsigned int i = 0; i < glyph_count; i++) {
		uint32_t gid = glyph_info[i].codepoint;

		if (*written >= 2048) {
			fprintf(stderr, "Warning: glyph table full (2048 slots), skipping rest\n");
			break;
		}

		if ((err = FT_Load_Glyph(face, gid,
		                          s.render_mode == 1
		                            ? FT_LOAD_TARGET_NORMAL | FT_LOAD_COLOR
		                            : FT_LOAD_TARGET_MONO))) {
			fprintf(stderr, "Error %d loading glyph %u (group[%d] seq[%u])\n",
			        err, gid, group_idx, i);
			continue;
		}
		if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
			if ((err = FT_Render_Glyph(face->glyph,
			                            s.render_mode == 1
			                              ? FT_RENDER_MODE_NORMAL
			                              : FT_RENDER_MODE_MONO))) {
				fprintf(stderr, "Error %d rendering glyph %u (group[%d])\n",
				        err, gid, group_idx);
				continue;
			}
		}

		FT_Glyph ft_glyph;
		if ((err = FT_Get_Glyph(face->glyph, &ft_glyph))) {
			fprintf(stderr, "Error %d getting glyph %u\n", err, gid);
			continue;
		}

		FT_Bitmap        *bitmap = &face->glyph->bitmap;
		FT_BitmapGlyphRec *rec   = (FT_BitmapGlyphRec *)ft_glyph;

		if (*bitmapOffset > 0xFFFF)
			fprintf(stderr,
			        "Warning: bitmapOffset %d exceeds uint16_t max (65535) for "
			        "group[%d] seq[%u] gid %u — split into smaller ranges.\n",
			        *bitmapOffset, group_idx, i, gid);

		table[*written].bitmapOffset = *bitmapOffset;
		table[*written].xAdvance     = face->glyph->advance.x >> 6;
		table[*written].xOffset      = rec->left;
		table[*written].yOffset      = 1 - rec->top;
		snprintf(names[*written].name, sizeof(names[*written].name),
		         "g%d_%u", group_idx, gid);

		if (bitmap->rows == 0 || bitmap->width == 0) {
			fprintf(stderr, "Info: no pixel data for glyph %u (group[%d])\n",
			        gid, group_idx);
			FT_Done_Glyph(ft_glyph);
			table[*written].width  = 0;
			table[*written].height = 0;
			(*written)++;
			continue;
		}

		int out_w, out_h;
		render_bitmap_to_bits(bitmap, &out_w, &out_h);
		table[*written].width  = out_w;
		table[*written].height = out_h;

		int n = (out_w * out_h) & 7;
		if (n) { n = 8 - n; while (n--) enbit(0); }
		*bitmapOffset += (out_w * out_h + 7) / 8;

		FT_Done_Glyph(ft_glyph);
		(*written)++;
	}

	hb_buffer_destroy(hb_buf);
	return 0;
}

// Shape a sequence string with HarfBuzz, render each resulting glyph, populate
// table[] and names[], and emit bitmap data via enbit().
//
// Syntax:  "CP1 CP2, CP3 CP4 CP5, CP6"
//   Comma separates independent glyphs (each comma group is shaped separately).
//   Spaces separate codepoints within one glyph (HarfBuzz handles ligatures,
//   ZWJ, regional-indicator clustering, etc. within a group).
//
// Backward-compatible: a string with no commas is treated as one group, letting
// HarfBuzz cluster all codepoints automatically (original behaviour).
//
// Returns the number of GFXglyph entries written, or -1 on fatal error.
int shape_and_render_sequence(GFXglyph *table, glyph_name *names, FT_Face face,
                               const char *seq_str, int *bitmapOffset) {
	if (!seq_str || !*seq_str) {
		fprintf(stderr, "Error: empty -S sequence\n");
		return -1;
	}

	setup_face_size(face);
	hb_font_t *hb_font = hb_ft_font_create(face, NULL);

	int written    = 0;
	int group_idx  = 0;

	char *input      = strdup(seq_str);
	char *save_outer = NULL;
	char *group      = strtok_r(input, ",", &save_outer);

	while (group) {
		shape_render_group(table, names, hb_font, face,
		                   group, group_idx, bitmapOffset, &written);
		group = strtok_r(NULL, ",", &save_outer);
		group_idx++;
	}

	free(input);
	hb_font_destroy(hb_font);

	if (written == 0) {
		fprintf(stderr, "Error: no glyphs rendered from sequence '%s'\n", seq_str);
		return -1;
	}
	return written;
}

void print_usage(char* argv[]) {
	fprintf(stderr,
	        "usage: %s -f FONTFILE [-s SIZE] [-v VARIANT] [-g] [-r H] [-W W]\n"
	        "       %*s [-D MODE] [-e EXPOSURE] [-o OFFSET|-n OFFSET]\n"
	        "       %*s [-S \"G[,G]...\" | RANGES]\n"
	        "       where G = space-separated hex codepoints for one glyph\n",
	        argv[0], (int)strlen(argv[0]), "", (int)strlen(argv[0]), "");
	fprintf(stderr, "  Using FreeType Version %d.%d.%d\n\n", FREETYPE_MAJOR,
	        FREETYPE_MINOR, FREETYPE_PATCH);
	fprintf(stderr, "  options:\n");
	fprintf(stderr,
	        "    -f FILE   Font file to convert (.ttf or .otf)\n");
	fprintf(stderr,
	        "    -s N      Point size for the generated font (default: 12)\n");
	fprintf(stderr,
	        "    -v NAME   Variant name embedded in the C identifiers to avoid\n"
	        "              name clashes when multiple fonts are included together\n");
	fprintf(stderr,
	        "    -g        Grayscale / color-emoji mode:\n"
	        "                BGRA bitmaps (CBDT/sbix color fonts) are composited\n"
	        "                over white then quantised to 1-bit.\n"
	        "                8-bit gray bitmaps are also quantised to 1-bit.\n"
	        "              The dithering algorithm is selected with -D (default: fs).\n"
	        "              Required for NotoColorEmoji and similar color fonts.\n");
	fprintf(stderr,
	        "    -r N      Render-size override in pixels.  For bitmap-only fonts\n"
	        "              (e.g. NotoColorEmoji) the fixed strike size is used and\n"
	        "              -s is ignored; -r sets the yAdvance height reported in\n"
	        "              the GFXfont struct.  Also used by -W to bound scaling.\n");
	fprintf(stderr,
	        "    -W N      Maximum rendered width in pixels.  When a glyph (after\n"
	        "              any -r height limit) is still wider than N, it is scaled\n"
	        "              down proportionally so width <= N.  Useful for wide\n"
	        "              glyphs like landscape-orientation country flags.\n");
	fprintf(stderr,
	        "    -D MODE   Dithering algorithm used when -g is active (default: fs):\n"
	        "                fs        Floyd-Steinberg error diffusion — smooth\n"
	        "                          gradients, slight worm-pattern noise\n"
	        "                stucki    Stucki error diffusion — wider kernel,\n"
	        "                          sharper edges on larger glyphs\n"
	        "                bayer     Bayer 4×4 ordered dithering — regular\n"
	        "                          dot pattern, no error propagation\n"
	        "                threshold Hard threshold at 0.5 — no dithering,\n"
	        "                          cleanest edges for high-contrast art\n"
	        "                random    Stochastic dithering — noise-like pattern\n");
	fprintf(stderr,
	        "    -e N      Exposure bias applied before dithering (range -1.0 to\n"
	        "              1.0, default 0.0).  Positive values shift pixels toward\n"
	        "              white (lower effective threshold); negative values shift\n"
	        "              toward black.  Useful to compensate for OLED gamma.\n");
	fprintf(stderr,
	        "    -o N      Add N to every codepoint written into the output struct\n"
	        "              (positive offset; overridden by -n)\n");
	fprintf(stderr,
	        "    -n N      Subtract N from every codepoint written into the output\n"
	        "              struct (takes priority over -o)\n");
	fprintf(stderr,
	        "    -S \"...\"  Sequence of hex codepoints shaped by HarfBuzz.\n"
	        "              When -S is given, RANGES are ignored.\n"
	        "              Syntax:\n"
	        "                Spaces separate codepoints within one glyph —\n"
	        "                  HarfBuzz handles ZWJ, ligatures, regional-indicator\n"
	        "                  clustering, etc. within each group.\n"
	        "                Commas separate independent glyphs — each comma group\n"
	        "                  is shaped in its own HarfBuzz call.\n"
	        "              e.g. -S \"1F1E9 1F1EA\"                  German flag\n"
	        "                   -S \"1F1E9 1F1EA, 1F1EB 1F1F7\"     DE + FR flags\n"
	        "                   -S \"1F3F3 FE0F 200D 1F308\"         rainbow flag (ZWJ)\n"
	        "                   -S \"1F600, 1F601, 1F602\"           3 separate emoji\n");
	fprintf(stderr,
	        "    -d        Dump all codepoints (and variant selectors) present in\n"
	        "              the font to stderr, then exit without generating output.\n");
	fprintf(stderr,
	        "    RANGES    One or more first/last codepoint pairs (hex or decimal).\n"
	        "              A single value is treated as the last codepoint with\n"
	        "              first=' '(0x20).  Default range: 0x20–0x7E (printable\n"
	        "              ASCII).  Ignored when -S is given.\n");
	fprintf(stderr, "\n  examples:\n");
	fprintf(stderr,
	        "    # Printable ASCII, monochrome, size 14\n"
	        "    %s -fmy_font.ttf -s14 0x20 0x7e > MyFont14pt.h\n\n", argv[0]);
	fprintf(stderr,
	        "    # Japanese Hiragana, size 15\n"
	        "    %s -fhiragana.otf -s15 -v_Hiragana_ 0x3041 0x309f > Hiragana.h\n\n",
	        argv[0]);
	fprintf(stderr,
	        "    # Korean Hangul Jamo consonants and vowels, size 16\n"
	        "    %s -fjamo.otf -v_Consonants_ -s16 0x1100 0x1112 > Consonants.h\n"
	        "    %s -fjamo.otf -v_Vowels_ -s16 0x1161 0x1169 0x116d 0x116e"
	        " 0x1172 0x1175 > Vowels.h\n\n",
	        argv[0], argv[0]);
	fprintf(stderr,
	        "    # Color emoji (smiley faces), grayscale+dither, max height 50 px\n"
	        "    %s -fNotoColorEmoji.ttf -s20 -g -r50 -v_Emoji_ 0x1f600 0x1f64f"
	        " > Emoji.h\n\n", argv[0]);
	fprintf(stderr,
	        "    # Two flags explicitly comma-separated, scaled to fit 60x36 px\n"
	        "    %s -fNotoColorEmoji.ttf -s20 -g -r36 -W60 -v_Flags_"
	        " -S \"1F1E9 1F1EA, 1F1EB 1F1F7\" > Flags.h\n", argv[0]);
}

int parse_args(int argc, char* argv[], char** fontFileName,
							 char** fontVariantName) {
	int opt;
	int offset_used = 0;

	if (argc <= 1) {
		return -1;
	}

	while ((opt = getopt(argc, argv, "dgs:f:v:r:o:n:S:W:D:e:")) != -1) {
		switch (opt) {
		case 's':
			if (!optarg) {
				printf("Missing value for argument s!\n");
				return -1;
			}
			s.size = to_int(optarg);
			break;

		case 'r':
			if (!optarg) {
				printf("Missing value for argument r!\n");
				return -1;
			}
			s.height = to_int(optarg);
			break;

		case 'o':
			if (!optarg) {
				printf("Missing value for argument o!\n");
				return -1;
			}
			if (offset_used == 0) {
				s.offset = -to_int(optarg);
			} else {
				printf("Ignoring argument o in favor of argument n!\n");
			}
			break;

		case 'n':
			if (!optarg) {
				printf("Missing value for argument n!\n");
				return -1;
			}

			s.offset = -to_int(optarg);
			offset_used = 1;
			break;

		case 'f':
			if (!optarg) {
				printf("Missing value for argument f!\n");
				return -1;
			}
			*fontFileName = strdup(optarg);
			break;

		case 'v':
			if (!optarg) {
				printf("Missing value for argument v!\n");
				return -1;
			}
			*fontVariantName = strdup(optarg);
			break;

		case 'g':
			s.render_mode = 1;
			break;

		case 'd':
			s.dump_codepoints = 1;
			break;

		case 'S':
			if (!optarg) {
				printf("Missing value for argument S!\n");
				return -1;
			}
			s.sequence = strdup(optarg);
			break;

		case 'W':
			if (!optarg) {
				printf("Missing value for argument W!\n");
				return -1;
			}
			s.max_width = to_int(optarg);
			break;

		case 'D':
			if (!optarg) {
				printf("Missing value for argument D!\n");
				return -1;
			}
			if (strcmp(optarg, "fs") == 0 || strcmp(optarg, "floyd") == 0) {
				s.dither_mode = DITHER_FLOYD_STEINBERG;
			} else if (strcmp(optarg, "stucki") == 0) {
				s.dither_mode = DITHER_STUCKI;
			} else if (strcmp(optarg, "bayer") == 0) {
				s.dither_mode = DITHER_BAYER;
			} else if (strcmp(optarg, "threshold") == 0) {
				s.dither_mode = DITHER_THRESHOLD;
			} else if (strcmp(optarg, "random") == 0) {
				s.dither_mode = DITHER_RANDOM;
			} else {
				fprintf(stderr, "Unknown dithering mode '%s'. "
				        "Valid options: fs, stucki, bayer, threshold, random\n", optarg);
				return -1;
			}
			break;

		case 'e':
			if (!optarg) {
				printf("Missing value for argument e!\n");
				return -1;
			}
			s.exposure = strtof(optarg, NULL);
			if (s.exposure < -1.0f) s.exposure = -1.0f;
			if (s.exposure >  1.0f) s.exposure =  1.0f;
			break;

		case '?':
			printf("Ignoring unknown option: %c\n", optopt);
			break;

		case ':':
			printf("Missing argument for %c!\n", optopt);
			return -1;
		}
	}

	if (optind < argc) {
		s.num_ranges = argc - optind;

		if (s.num_ranges == 1) {
			s.num_ranges = -2;
		} else if ((s.num_ranges % 2) != 0) {
			fprintf(stderr,
							"Range end not specified! %d free arguments supplied.\n\n",
							s.num_ranges);
			return -1;
		}
		s.num_ranges /= 2;
	}

	return 0;
}

void dump_font_info(FT_Face face, const char* fontName) {
	fprintf(stderr, "%s Stats:\n", fontName);
	fprintf(stderr, "Num faces: %lu Num glyphs: %lu Maps: %d\n", face->num_faces,
					face->num_glyphs, face->num_charmaps);

	FT_UInt32* selectors = FT_Face_GetVariantSelectors(face);
	if (*selectors != 0) {
		fprintf(stderr, "Selectors:");
		while (*selectors != 0) {
			fprintf(stderr, " 0x%x", *selectors);
			selectors++;
		}
		fprintf(stderr, ";\n");
	}

	fprintf(stderr, "=============================================\n");
	// Ensure an unicode characater map is loaded
	FT_Select_Charmap(face, FT_ENCODING_UNICODE);

	FT_UInt gid;
	FT_ULong codepoint = FT_Get_First_Char(face, &gid);
	while (gid != 0) {
		FT_UInt32* variants = FT_Face_GetVariantsOfChar(face, codepoint);
		fprintf(stderr, "Codepoint: 0x%lx, gid: %u", codepoint, gid);

		if (*variants != 0) {
			fprintf(stderr, " Variants:");
			while (*variants != 0) {
				fprintf(stderr, " 0x%x", *variants);
				variants++;
			}
			fprintf(stderr, ";");
		}

		fprintf(stderr, "\n");

		codepoint = FT_Get_Next_Char(face, codepoint, &gid);
	}
}

int main(int argc, char* argv[]) {
	FT_Library library;
	GFXglyph* table;
	glyph_name* names;
	FT_Face face;
	ch_range* ranges;
	int total_num = 0, skipped = 0;
	int err;
	int i, j, r;
	FT_ULong codepoint;
	char *fontName, *fontFileName = NULL, *fontVariantName = NULL;
	char c;
	int bitmapOffset = 0;

	if (parse_args(argc, argv, &fontFileName, &fontVariantName) != 0 ||
		fontFileName == NULL) {
		print_usage(argv);
		return 1;
	}

	if (!(ranges = (ch_range*)malloc((s.num_ranges <= 0 ? 1 : s.num_ranges) *
		sizeof(ch_range)))) {
		fprintf(stderr, "Malloc error\n");
		return 1;
	}

	srand(0); // deterministic random please, os start with 0

	ranges[0].first = ' ';
	ranges[0].last = '~';

	if (s.num_ranges < 0) {
		s.num_ranges = 1; // default range
		ranges[0].last = to_ulong(argv[optind]);
		range_swap_if_needed(&ranges[0]);
		total_num = range_count(&ranges[0]);
	} else if (s.num_ranges > 0) {
		for (i = 0; i < s.num_ranges; ++i) {
			ranges[i].first = to_ulong(argv[optind++]);
			ranges[i].last = to_ulong(argv[optind++]);
			range_swap_if_needed(&ranges[i]);
			total_num += range_count(&ranges[i]);
		}
	} else { // num_ranges == 0
		s.num_ranges = 1; // default range
		range_swap_if_needed(&ranges[0]);
		total_num = range_count(&ranges[0]);
	}
	int last_range = s.num_ranges - 1;

	// printf("// Loading FreeType library...\n");
	// Init FreeType lib, load font
	if ((err = FT_Init_FreeType(&library))) {
		fprintf(stderr, "FreeType init error: %d", err);
		return err;
	}

	// Use TrueType engine version 35, without subpixel rendering.
	// This improves clarity of fonts since this library does not
	// support rendering multiple levels of gray in a glyph.
	// See https://github.com/adafruit/Adafruit-GFX-Library/issues/103
	FT_UInt interpreter_version = (s.render_mode == 1)
																	? TT_INTERPRETER_VERSION_40
																	: TT_INTERPRETER_VERSION_35;
	FT_Property_Set(library, "truetype", "interpreter-version",
									&interpreter_version);

	// printf("// Loading front from %s\n", fontFileName);
	if (fontFileName[0] == '~' && strlen(fontFileName) > 2) {
		const char* homedir;

		if ((homedir = getenv("HOME")) == NULL) {
			homedir = getpwuid(getuid())->pw_dir;
		}
		if (chdir(homedir) != 0) {
			fprintf(stderr, "Could not access '%s'\n", homedir);
			return 1;
		}
		fontName = &fontFileName[2];
	} else {
		fontName = fontFileName;
	}

	if ((err = FT_New_Face(library, fontName, 0, &face))) {
		fprintf(stderr, "Font load error: %d", err);
		FT_Done_FreeType(library);
		return err;
	}

	if (s.dump_codepoints) {
		dump_font_info(face, fontName);

		FT_Done_FreeType(library);

		return 0;
	}

	// Allocate space for font name and glyph table
	// Sequence mode may emit up to 64 shaped glyphs regardless of codepoint ranges
	int alloc_num = (s.sequence && total_num < 2048) ? 2048 : total_num;
	if ((!(fontName = malloc(strlen(fontFileName) + 22))) ||
		(!(table = (GFXglyph*)malloc(alloc_num * sizeof(GFXglyph)))) ||
		(!(names = (glyph_name*)malloc(alloc_num * sizeof(glyph_name))))) {
		fprintf(stderr, "Malloc error\n");
		return 1;
	}

	printf("// ");
	for (i = 0; i < argc; ++i) {
		printf("%s ", argv[i]);
	}
	printf("\n// Visualize your font via "
		"https://tchapi.github.io/Adafruit-GFX-Font-Customiser\n\n");

	// Derive font table names from filename.  Period (filename
	// extension) is truncated and replaced with the font size & bits.
	const char* start = strrchr(fontFileName, '/');
	if (start) {
		strcpy(fontName, start + 1);
	} else {
		strcpy(fontName, fontFileName);
	}

	free(fontFileName);
	fontFileName = strrchr(fontName, '.'); // Find last period (file ext)
	if (!fontFileName)
		fontFileName = &fontName[strlen(fontName)]; // If none, append
	// Insert font size and 7/8/16 bit.  fontName was alloc'd w/extra
	// space to allow this, we're not sprintfing into Forbidden Zone.
	sprintf(fontFileName, "%s%dpt%db",
					fontVariantName == NULL ? "" : fontVariantName, s.size,
					(ranges[last_range].last > 127)
						? (ranges[last_range].last > 255)
								? 16
								: 8
						: 7);
	// Space and punctuation chars in name replaced w/ underscores.
	for (i = 0; (c = fontName[i]); i++) {
		if (isspace(c) || ispunct(c))
			fontName[i] = '_';
	}

	if (s.sequence) {
		// --- SEQUENCE MODE: HarfBuzz shapes the codepoints, output single block ---
		printf("/* sequence: %s */\nconst uint8_t %sBitmaps[] PROGMEM = {\n"
		       "  /* shaped sequence */  ", s.sequence, fontName);
		int seq_count = shape_and_render_sequence(table, names, face,
		                                          s.sequence, &bitmapOffset);
		printf("\n };\n\n");

		if (seq_count <= 0) {
			FT_Done_FreeType(library);
			return 1;
		}

		printf("const GFXglyph %sGlyphs[] PROGMEM = {\n", fontName);
		printf("// bmpOff,   w,   h,xAdv, xOff, yOff      sequence\n");
		for (i = 0; i < seq_count; i++) {
			printf("  { %5d, %3d, %3d, %3d, %4d, %4d }",
			       table[i].bitmapOffset, table[i].width, table[i].height,
			       table[i].xAdvance, table[i].xOffset, table[i].yOffset);
			if (i < seq_count - 1)
				printf(",   // seq[%d] %s\n", i, names[i].name);
		}
		printf(" }; // seq[%d] %s\n\n", seq_count - 1, names[seq_count - 1].name);

		printf("const GFXfont %s PROGMEM = {\n", fontName);
		printf("  (uint8_t  *)%sBitmaps,\n", fontName);
		printf("  (GFXglyph *)%sGlyphs,\n", fontName);
		if (s.height != 0)
			face->size->metrics.height = s.height;
		else if (face->size->metrics.height == 0)
			face->size->metrics.height = table[0].height;
		else
			face->size->metrics.height = (uint8_t)(face->size->metrics.height >> 6);
		printf("  0x%02X, // first\n  0x%02X, // last\n  %ld   //height\n };\n\n",
		       0, seq_count - 1, face->size->metrics.height);
		printf("// Approx. %d bytes\n", bitmapOffset + seq_count * 7 + 7);

	} else {
		// --- RANGE MODE: original codepoint-range extraction ---
		printf("/* num ranges: %d */\nconst uint8_t %sBitmaps[] PROGMEM = {\n",
		       s.num_ranges, fontName);
		for (i = 0; i < s.num_ranges; ++i) {
			printf("  /* range %d (0x%lx - 0x%lx): */  ", i, ranges[i].first,
			       ranges[i].last);
			err = extract_range_ft(table, names, face, ranges[i].first,
			                       ranges[i].last, &bitmapOffset);
			printf("\n");
		}
		if (err != 0) {
			FT_Done_FreeType(library);
			return err;
		}
		printf(" };\n\n");

		printf("const GFXglyph %sGlyphs[] PROGMEM = {\n", fontName);
		j = 0;
		for (r = 0; r < s.num_ranges; ++r) {
			printf("// bmpOff,   w,   h,xAdv, xOff, yOff      range %d (0x%lx - 0x%lx)\n",
			       r, ranges[r].first, ranges[r].last);
			for (codepoint = ranges[r].first; codepoint <= ranges[r].last; ++codepoint) {
				printf("  { %5d, %3d, %3d, %3d, %4d, %4d }", table[j].bitmapOffset,
				       table[j].width, table[j].height, table[j].xAdvance,
				       table[j].xOffset, table[j].yOffset);
				if (codepoint < ranges[r].last || r < last_range) {
					printf(",   // 0x%02lX %s ", codepoint, names[j].name);
					if ((codepoint >= ' ') && (codepoint <= '~'))
						printf(" '%c'", (int)codepoint);
					printf(" (#%d)\n", j);
				}
				j++;
			}
			if (r != last_range) {
				for (codepoint = ranges[r].last + 1;
				     codepoint < ranges[r + 1].first; ++codepoint) {
					printf("  { %5d, %3d, %3d, %3d, %4d, %4d },   // 0x%02lX (skip)\n",
					       0, 0, 0, 0, 0, 0, codepoint);
					skipped++;
				}
			}
		}
		printf(" }; // 0x%02lX %s ", ranges[last_range].last, names[j - 1].name);
		if ((ranges[last_range].last >= ' ') && (ranges[last_range].last <= '~'))
			printf(" '%c'", (int)ranges[last_range].last);
		printf(" (#%d)\n\n", j - 1);

		printf("const GFXfont %s PROGMEM = {\n", fontName);
		printf("  (uint8_t  *)%sBitmaps,\n", fontName);
		printf("  (GFXglyph *)%sGlyphs,\n", fontName);
		if (s.height != 0)
			face->size->metrics.height = s.height;
		else if (face->size->metrics.height == 0)
			face->size->metrics.height = table[0].height;
		else
			face->size->metrics.height = (uint8_t)(face->size->metrics.height >> 6);
		printf("  0x%02lX, // first\n  0x%02lX, // last\n  %ld   //height\n };\n\n",
		       ranges[0].first + s.offset, ranges[last_range].last + s.offset,
		       face->size->metrics.height);
		printf("// Approx. %d bytes\n", bitmapOffset + (total_num + skipped) * 7 + 7);
	}
	// Size estimate is based on AVR struct and pointer sizes; actual size may vary.

	FT_Done_FreeType(library);

	return 0;
}

/* -------------------------------------------------------------------------

Character metrics are slightly different from classic GFX & ftGFX.
In classic GFX: cursor position is the upper-left pixel of each 5x7
character; lower extent of most glyphs (except those w/descenders)
is +6 pixels in Y direction.
W/new GFX fonts: cursor position is on baseline, where baseline is
'inclusive' (containing the bottom-most row of pixels in most symbols,
except those with descenders; ftGFX is one pixel lower).

Cursor Y will be moved automatically when switching between classic
and new fonts.  If you switch fonts, any print() calls will continue
along the same baseline.

										...........#####.. -- yOffset
										..........######..
										..........######..
										.........#######..
										........#########.
	 * = Cursor pos.  ........#########.
										.......##########.
										......#####..####.
										......#####..####.
			 *.#..        .....#####...####.
			 .#.#.        ....##############
			 #...#        ...###############
			 #...#        ...###############
			 #####        ..#####......#####
			 #...#        .#####.......#####
====== #...# ====== #*###.........#### ======= Baseline
										|| xOffset

glyph->xOffset and yOffset are pixel offsets, in GFX coordinate space
(+Y is down), from the cursor position to the top-left pixel of the
glyph bitmap.  i.e. yOffset is typically negative, xOffset is typically
zero but a few glyphs will have other values (even negative xOffsets
sometimes, totally normal).  glyph->xAdvance is the distance to move
the cursor on the X axis after drawing the corresponding symbol.

There's also some changes with regard to 'background' color and new GFX
fonts (classic fonts unchanged).  See Adafruit_GFX.cpp for explanation.
*/
