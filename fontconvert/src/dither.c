#include <stdlib.h>
#include <stdio.h>
#include "dither.h"
#include "types.h"

void enbit(uint8_t value) {
	static uint8_t row = 0, sum = 0, bit = 0x80, firstCall = 1;
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
			float ro = r * a + (1.0f - a);
			float go = g * a + (1.0f - a);
			float bo = b * a + (1.0f - a);
			gray[y * width + x] = 0.2126f * ro + 0.7152f * go + 0.0722f * bo;
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

void apply_dithering(float *gray, int width, int rows) {
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

void render_bitmap_to_bits(FT_Bitmap *bitmap, int *out_w, int *out_h) {
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
