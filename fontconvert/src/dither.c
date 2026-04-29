#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "dither.h"
#include "types.h"

// When non-NULL, enbit() writes to this buffer instead of stdout.
static uint8_t *s_cap     = NULL;
static int      s_cap_pos = 0;

void enbit(uint8_t value) {
	static uint8_t row = 0, sum = 0, bit = 0x80, firstCall = 1;
	if (s_cap) {
		if (value) s_cap[s_cap_pos >> 3] |= 0x80 >> (s_cap_pos & 7);
		s_cap_pos++;
		return;
	}
	if (value)
		sum |= bit;
	if (!(bit >>= 1)) {
		if (!firstCall) {
			if (++row >= 12) {
				printf(",\n  ");
				row = 0;
			} else {
				printf(", ");
			}
		}
		printf("0x%02X", sum);
		sum = 0;
		bit = 0x80;
		firstCall = 0;
	}
}

// Convert a BGRA bitmap (straight alpha, as returned by FreeType for CBDT/color fonts)
// to a float grayscale buffer (0.0=black, 1.0=white) composited over a black background.
// Transparent pixels (alpha=0) become 0.0 so they stay dark on the OLED, matching the
// wavy flag shape that NotoColorEmoji embeds as partial transparency around the flag.
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
			float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
			if (s.saturation_boost > 0.0f) {
				float cmax = r > g ? (r > b ? r : b) : (g > b ? g : b);
				float cmin = r < g ? (r < b ? r : b) : (g < b ? g : b);
				float sat  = cmax > 0.0f ? (cmax - cmin) / cmax : 0.0f;
				lum += s.saturation_boost * sat;
				if (lum > 1.0f) lum = 1.0f;
			}
			gray[y * width + x] = a * lum;
		}
	}
	return gray;
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

// Floyd-Steinberg error diffusion to 1-bit via enbit().
// Standard kernel: forward 7/16, below-left 3/16, below 5/16, below-right 1/16
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

// Bayer ordered dithering (4x4 matrix).  No error propagation — each pixel is
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

// 3×3 Gaussian unsharp mask: sharpened = input + amount*(input - blur).
// Preserves stripe edges that bilinear downscaling softens.
static void apply_unsharp_mask(float *gray, int width, int rows, float amount) {
	float *blurred = (float *)malloc(width * rows * sizeof(float));
	if (!blurred) return;
	static const float k[3][3] = {
		{ 1/16.f, 2/16.f, 1/16.f },
		{ 2/16.f, 4/16.f, 2/16.f },
		{ 1/16.f, 2/16.f, 1/16.f },
	};
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0.0f;
			for (int dy = -1; dy <= 1; dy++) {
				int ny = y + dy < 0 ? 0 : y + dy >= rows  ? rows  - 1 : y + dy;
				for (int dx = -1; dx <= 1; dx++) {
					int nx = x + dx < 0 ? 0 : x + dx >= width ? width - 1 : x + dx;
					sum += gray[ny * width + nx] * k[dy+1][dx+1];
				}
			}
			blurred[y * width + x] = sum;
		}
	}
	for (int i = 0; i < width * rows; i++) {
		float v = gray[i] + amount * (gray[i] - blurred[i]);
		gray[i] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
	}
	free(blurred);
}

void apply_dithering(float *gray, int width, int rows) {
	if (s.sharpness > 0.0f)
		apply_unsharp_mask(gray, width, rows, s.sharpness);
	if (s.gamma_val != 1.0f) {
		float inv_g = 1.0f / s.gamma_val;
		for (int i = 0; i < width * rows; i++) {
			float v = gray[i];
			gray[i] = v <= 0.0f ? 0.0f : powf(v, inv_g);
		}
	}
	if (s.contrast != 1.0f) {
		for (int i = 0; i < width * rows; i++) {
			float v = (gray[i] - 0.5f) * s.contrast + 0.5f;
			gray[i] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
		}
	}
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

// For BGRA bitmaps: apply a two-sided border around the alpha content boundary.
//
// Outer ring: non-content pixels within t steps of a content pixel → lit.
//   Creates a bright halo in the transparent background.
//
// Inner ring: content pixels within t steps of a non-content pixel → lit.
//   Seals dithering gaps at the alpha boundary.  Near the wavy edge of a flag,
//   bilinear scaling can produce semi-transparent pixels whose composited gray
//   value falls just below the dithering threshold, leaving a dark fringe inside
//   the alpha mask.  Forcing those pixels lit closes the gap between flag content
//   and the outer halo (critical for white-background flags like JP and KR).
static void alpha_content_outline(uint8_t *buf, FT_Bitmap *bitmap,
                                  int out_w, int out_h, int t) {
	int src_w = (int)bitmap->width;
	int src_h = (int)bitmap->rows;

	float *alpha = (float *)malloc(src_w * src_h * sizeof(float));
	if (!alpha) return;
	for (int y = 0; y < src_h; y++)
		for (int x = 0; x < src_w; x++)
			alpha[y * src_w + x] =
				bitmap->buffer[y * bitmap->pitch + x * 4 + 3] > 0 ? 1.0f : 0.0f;

	float *alpha_sc;
	if (src_w != out_w || src_h != out_h) {
		alpha_sc = scale_gray_buf(alpha, src_w, src_h, out_w, out_h);
		free(alpha);
		if (!alpha_sc) return;
	} else {
		alpha_sc = alpha;
	}

	// 1-bit content mask (threshold 0.5)
	int n_bytes = (out_w * out_h + 7) / 8;
	uint8_t *mask = (uint8_t *)calloc(n_bytes, 1);
	if (!mask) { free(alpha_sc); return; }
	for (int i = 0; i < out_w * out_h; i++)
		if (alpha_sc[i] >= 0.5f)
			mask[i >> 3] |= 0x80 >> (i & 7);
	free(alpha_sc);

	for (int y = 0; y < out_h; y++) {
		for (int x = 0; x < out_w; x++) {
			int idx = y * out_w + x;
			int is_content = (mask[idx >> 3] >> (7 - (idx & 7))) & 1;
			int near_other = 0;
			for (int dy = -t; dy <= t && !near_other; dy++) {
				for (int dx = -t; dx <= t && !near_other; dx++) {
					if (dx == 0 && dy == 0) continue;
					int nx = x + dx, ny = y + dy;
					if (nx < 0 || nx >= out_w || ny < 0 || ny >= out_h) {
						// bbox edge counts as non-content for the inner ring
						if (is_content) near_other = 1;
						continue;
					}
					int nidx = ny * out_w + nx;
					int n_content = (mask[nidx >> 3] >> (7 - (nidx & 7))) & 1;
					if (n_content != is_content) near_other = 1;
				}
			}
			if (near_other)
				buf[idx >> 3] |= 0x80 >> (idx & 7);
		}
	}
	free(mask);
}

// Morphological dilation: any dark pixel within the t-pixel Chebyshev neighbourhood
// of a lit pixel is set lit.  Original lit pixels are never cleared.
// buf is modified in-place; a temporary copy is used as the source so that newly
// set pixels do not influence the neighbourhood lookup of subsequent pixels.
static void morphological_outline(uint8_t *buf, int w, int h, int t) {
	int n_bytes = (w * h + 7) / 8;
	uint8_t *src = (uint8_t *)malloc(n_bytes);
	if (!src) return;
	memcpy(src, buf, n_bytes);

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int idx = y * w + x;
			if ((src[idx >> 3] >> (7 - (idx & 7))) & 1)
				continue;  // already lit
			int found = 0;
			for (int dy = -t; dy <= t && !found; dy++) {
				for (int dx = -t; dx <= t && !found; dx++) {
					if (dx == 0 && dy == 0) continue;
					int nx = x + dx, ny = y + dy;
					if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
					int nidx = ny * w + nx;
					if ((src[nidx >> 3] >> (7 - (nidx & 7))) & 1)
						found = 1;
				}
			}
			if (found)
				buf[idx >> 3] |= 0x80 >> (idx & 7);
		}
	}
	free(src);
}

// Emit n_bits bits from packed-bits buffer buf via enbit().
static void emit_buf(const uint8_t *buf, int n_bits) {
	for (int i = 0; i < n_bits; i++)
		enbit((buf[i >> 3] >> (7 - (i & 7))) & 1);
}

// Core render: populates *out_w/*out_h and streams bits via enbit() (or into
// s_cap when capture mode is active).
static void render_core(FT_Bitmap *bitmap, int *out_w, int *out_h) {
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

void render_bitmap_to_bits(FT_Bitmap *bitmap, int *out_w, int *out_h) {
	if (s.outline <= 0) {
		render_core(bitmap, out_w, out_h);
		return;
	}

	// Allocate worst-case capture buffer (no scaling can increase size).
	int max_bytes = ((int)bitmap->width * (int)bitmap->rows + 7) / 8;
	if (max_bytes <= 0) {
		render_core(bitmap, out_w, out_h);
		return;
	}

	s_cap = (uint8_t *)calloc(max_bytes, 1);
	if (!s_cap) {
		render_core(bitmap, out_w, out_h);
		return;
	}
	s_cap_pos = 0;

	render_core(bitmap, out_w, out_h);

	uint8_t *captured = s_cap;
	int n_bits = *out_w * *out_h;
	s_cap     = NULL;
	s_cap_pos = 0;

	if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA)
		alpha_content_outline(captured, bitmap, *out_w, *out_h, s.outline);
	else
		morphological_outline(captured, *out_w, *out_h, s.outline);
	emit_buf(captured, n_bits);
	free(captured);
}
