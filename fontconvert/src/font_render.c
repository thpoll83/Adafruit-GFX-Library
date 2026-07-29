#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define strdup   _strdup
#  define strtok_r strtok_s
#endif

#include <ft2build.h>
#include FT_GLYPH_H
#include <hb.h>
#include <hb-ft.h>

#include "font_render.h"
#include "dither.h"
#include "types.h"

#define DPI 141

// Scale a glyph metric (advance / left / top) that FreeType reported at a font's
// native size so it tracks a bitmap that -r/-W has since downscaled.  num/den =
// emitted/native dimension.  Rounds half away from zero so small offsets don't
// collapse to 0.
static int scale_metric(int v, int num, int den) {
	if (den == 0 || num == den) return v;
	double r = (double)v * (double)num / (double)den;
	return (int)(r + (r >= 0 ? 0.5 : -0.5));
}

// Base FT_Load_Glyph flags: the render target plus the -H grid-fitting choice.
// mono = 1 selects the 1-bit path (FT_LOAD_TARGET_MONO), 0 the colour/gray one.
FT_Int32 glyph_load_flags(int mono) {
	FT_Int32 flags = mono ? FT_LOAD_TARGET_MONO
	                      : (FT_LOAD_TARGET_NORMAL | FT_LOAD_COLOR);
	switch (s.hinting) {
	case HINT_AUTO: flags |= FT_LOAD_FORCE_AUTOHINT; break;
	case HINT_NONE: flags |= FT_LOAD_NO_HINTING; break;
	case HINT_NATIVE:
	default: break;
	}
	return flags;
}

// For outline fonts use FT_Set_Char_Size; for bitmap-only faces (e.g. CBDT/CBLC
// color emoji) select the strike whose y_ppem is closest to the requested size.
void setup_face_size(FT_Face face) {
	if (face->num_fixed_sizes > 0) {
		int target_px = s.pixel_size > 0 ? s.pixel_size
		                                 : (s.size * DPI + 36) / 72;
		int best = 0, best_diff = INT_MAX;
		for (int i = 0; i < face->num_fixed_sizes; i++) {
			int diff = abs((int)(face->available_sizes[i].y_ppem >> 6) - target_px);
			if (diff < best_diff) { best_diff = diff; best = i; }
		}
		FT_Select_Size(face, best);
		fprintf(stderr, "Info: bitmap font — selected strike %d (%dx%d px)\n",
		        best,
		        (int)(face->available_sizes[best].x_ppem >> 6),
		        (int)(face->available_sizes[best].y_ppem >> 6));
	} else if (s.pixel_size > 0) {
		FT_Set_Pixel_Sizes(face, 0, s.pixel_size);
	} else {
		FT_Set_Char_Size(face, s.size << 6, 0, DPI, 0);
	}
}

int extract_range_ft(GFXglyph *table, glyph_name *names, FT_Face face,
                     FT_ULong first, FT_ULong last, int *bitmapOffset) {
	FT_ULong codepoint;
	int err;
	static int table_idx = 0;

	FT_Glyph glyph;
	FT_Bitmap *bitmap;
	FT_BitmapGlyphRec *rec;

	setup_face_size(face);

	for (codepoint = first; codepoint <= last; codepoint++, table_idx++) {
		uint32_t glyph_index = FT_Get_Char_Index(face, codepoint);

		if (glyph_index == 0) {
			fprintf(stderr, "Error getting glyph index for codepoint '0x%lx'\n",
			        codepoint);
			continue;
		}

		if ((err = FT_Load_Glyph(face, glyph_index,
		                         glyph_load_flags(s.render_mode != 1)))) {
			fprintf(stderr,
			        "Error %d loading codepoint '0x%lx' with glyph index %u\n", err,
			        codepoint, glyph_index);
			continue;
		}

		if ((err = FT_Get_Glyph_Name(face, glyph_index, names[table_idx].name, 32)))
			names[table_idx].name[0] = 0;

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
		rec = (FT_BitmapGlyphRec *)glyph;

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
			fprintf(stderr,
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

		// A color strike (NotoColorEmoji etc.) renders at a fixed native size that
		// -r/-W then shrinks; FreeType still reports advance/left/top at the native
		// size, so rescale the metrics to the emitted bitmap.
		if (out_w != (int)bitmap->width || out_h != (int)bitmap->rows) {
			table[table_idx].xAdvance = scale_metric(table[table_idx].xAdvance, out_w, (int)bitmap->width);
			table[table_idx].xOffset  = scale_metric(rec->left, out_w, (int)bitmap->width);
			table[table_idx].yOffset  = scale_metric(1 - rec->top, out_h, (int)bitmap->rows);
		}

		int n = (out_w * out_h) & 7;
		if (n) { n = 8 - n; while (n--) enbit(0); }
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
		return 0;

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

		if ((err = FT_Load_Glyph(face, gid, glyph_load_flags(s.render_mode != 1)))) {
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

		// See extract_range_ft: rescale color-strike metrics to the shrunk bitmap.
		if (out_w != (int)bitmap->width || out_h != (int)bitmap->rows) {
			table[*written].xAdvance = scale_metric(table[*written].xAdvance, out_w, (int)bitmap->width);
			table[*written].xOffset  = scale_metric(rec->left, out_w, (int)bitmap->width);
			table[*written].yOffset  = scale_metric(1 - rec->top, out_h, (int)bitmap->rows);
		}

		int n = (out_w * out_h) & 7;
		if (n) { n = 8 - n; while (n--) enbit(0); }
		*bitmapOffset += (out_w * out_h + 7) / 8;

		FT_Done_Glyph(ft_glyph);
		(*written)++;
	}

	hb_buffer_destroy(hb_buf);
	return 0;
}

int shape_and_render_sequence(GFXglyph *table, glyph_name *names, FT_Face face,
                               const char *seq_str, int *bitmapOffset) {
	if (!seq_str || !*seq_str) {
		fprintf(stderr, "Error: empty -S sequence\n");
		return -1;
	}

	setup_face_size(face);
	hb_font_t *hb_font = hb_ft_font_create(face, NULL);

	int written   = 0;
	int group_idx = 0;

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

// One captured mono glyph plus its device placement (baseline = 0, +y down).
typedef struct {
	uint8_t *bits;          // packed 1-bpp, row-major continuous, w*h bits
	int w, h;
	int devL, devT;         // top-left pixel position relative to the cluster origin
} composed_glyph;

// Composite one comma-separated group: shape it with HarfBuzz, render each glyph
// MONO, then OR all glyphs into a single bitmap at their GPOS-derived pixel
// positions.  Emits one GFXglyph into table[]/names[] and streams the composite
// bits via enbit() (byte-padded, exactly like extract_range_ft).  *written is
// advanced by one on success.
static void composite_render_group(GFXglyph *table, glyph_name *names,
                                   hb_font_t *hb_font, FT_Face face,
                                   const char *group_str, int group_idx,
                                   int *bitmapOffset, int *written) {
	uint32_t cps[512];
	int n_cp = 0;

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
		return;

	hb_buffer_t *hb_buf = hb_buffer_create();
	hb_buffer_set_content_type(hb_buf, HB_BUFFER_CONTENT_TYPE_UNICODE);
	for (int i = 0; i < n_cp; i++)
		hb_buffer_add(hb_buf, cps[i], i);
	hb_buffer_set_direction(hb_buf, HB_DIRECTION_LTR);
	hb_buffer_guess_segment_properties(hb_buf);
	hb_shape(hb_font, hb_buf, NULL, 0);

	unsigned int glyph_count;
	hb_glyph_info_t     *gi = hb_buffer_get_glyph_infos(hb_buf, &glyph_count);
	hb_glyph_position_t *gp = hb_buffer_get_glyph_positions(hb_buf, &glyph_count);
	fprintf(stderr, "HarfBuzz: group[%d] %d codepoint(s) → %u glyph(s) [composite]\n",
	        group_idx, n_cp, glyph_count);

	composed_glyph *cg = (composed_glyph *)calloc(
	    glyph_count ? glyph_count : 1, sizeof(composed_glyph));
	if (!cg) { hb_buffer_destroy(hb_buf); return; }

	double penx = 0.0, peny = 0.0;
	int placed = 0;
	for (unsigned int i = 0; i < glyph_count; i++) {
		uint32_t gid = gi[i].codepoint;
		if (FT_Load_Glyph(face, gid, glyph_load_flags(1))) {
			fprintf(stderr, "Error loading glyph %u (group[%d] composite)\n",
			        gid, group_idx);
			continue;
		}
		if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP)
			if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO)) {
				fprintf(stderr, "Error rendering glyph %u (group[%d])\n",
				        gid, group_idx);
				continue;
			}

		FT_Bitmap *bm = &face->glyph->bitmap;
		int w = (int)bm->width, h = (int)bm->rows;
		// HarfBuzz x/y offsets and advances are in 26.6 fixed point (the FT size).
		double gx = penx + gp[i].x_offset / 64.0;
		double gy = peny - gp[i].y_offset / 64.0;     // HB +y is up; device +y is down
		int devL = (int)lround(gx) + face->glyph->bitmap_left;
		int devT = (int)lround(gy) - face->glyph->bitmap_top;
		penx += gp[i].x_advance / 64.0;
		peny -= gp[i].y_advance / 64.0;

		if (w > 0 && h > 0) {
			uint8_t *copy = (uint8_t *)calloc((w * h + 7) / 8, 1);
			if (copy) {
				int pos = 0;
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++, pos++)
						if (bm->buffer[y * bm->pitch + (x >> 3)] & (0x80 >> (x & 7)))
							copy[pos >> 3] |= 0x80 >> (pos & 7);
				cg[placed].bits = copy;
				cg[placed].w = w; cg[placed].h = h;
				cg[placed].devL = devL; cg[placed].devT = devT;
				placed++;
			}
		}
	}
	int total_adv = (int)lround(penx);
	hb_buffer_destroy(hb_buf);

	if (*written >= 2048) {
		fprintf(stderr, "Warning: glyph table full (2048), skipping group %d\n",
		        group_idx);
		for (int k = 0; k < placed; k++) free(cg[k].bits);
		free(cg);
		return;
	}

	// Union bounding box of all placed glyphs.
	int minL = 0, minT = 0, maxR = 0, maxB = 0, have = 0;
	for (int k = 0; k < placed; k++) {
		int l = cg[k].devL, t = cg[k].devT;
		int r = l + cg[k].w, b = t + cg[k].h;
		if (!have) { minL = l; minT = t; maxR = r; maxB = b; have = 1; }
		else {
			if (l < minL) minL = l;
			if (t < minT) minT = t;
			if (r > maxR) maxR = r;
			if (b > maxB) maxB = b;
		}
	}
	int CW = have ? (maxR - minL) : 0;
	int CH = have ? (maxB - minT) : 0;

	if (*bitmapOffset > 0xFFFF)
		fprintf(stderr, "Warning: bitmapOffset %d exceeds uint16_t max for "
		        "group[%d] — split into smaller sequences.\n", *bitmapOffset, group_idx);

	table[*written].bitmapOffset = *bitmapOffset;
	table[*written].width    = CW;
	table[*written].height   = CH;
	table[*written].xAdvance = total_adv;
	table[*written].xOffset  = have ? minL : 0;
	table[*written].yOffset  = have ? (1 + minT) : 0;   // GFX convention: 1 - top
	snprintf(names[*written].name, sizeof(names[*written].name),
	         "g%d_compose%u", group_idx, glyph_count);

	if (have) {
		uint8_t *canvas = (uint8_t *)calloc((CW * CH + 7) / 8, 1);
		if (!canvas) {
			for (int k = 0; k < placed; k++) free(cg[k].bits);
			free(cg);
			return;
		}
		for (int k = 0; k < placed; k++) {
			int ox = cg[k].devL - minL, oy = cg[k].devT - minT;
			int sp = 0;
			for (int y = 0; y < cg[k].h; y++)
				for (int x = 0; x < cg[k].w; x++, sp++)
					if (cg[k].bits[sp >> 3] & (0x80 >> (sp & 7))) {
						int cpos = (oy + y) * CW + (ox + x);
						canvas[cpos >> 3] |= 0x80 >> (cpos & 7);
					}
		}
		int nbits = CW * CH;
		for (int i = 0; i < nbits; i++)
			enbit((canvas[i >> 3] >> (7 - (i & 7))) & 1);
		int n = nbits & 7;
		if (n) { n = 8 - n; while (n--) enbit(0); }
		*bitmapOffset += (nbits + 7) / 8;
		free(canvas);
	}

	for (int k = 0; k < placed; k++) free(cg[k].bits);
	free(cg);
	(*written)++;
}

int composite_and_render_sequence(GFXglyph *table, glyph_name *names, FT_Face face,
                                   const char *seq_str, int *bitmapOffset) {
	if (!seq_str || !*seq_str) {
		fprintf(stderr, "Error: empty -S sequence\n");
		return -1;
	}

	setup_face_size(face);
	hb_font_t *hb_font = hb_ft_font_create(face, NULL);

	int written = 0, group_idx = 0;
	char *input = strdup(seq_str);
	char *save_outer = NULL;
	char *group = strtok_r(input, ",", &save_outer);
	while (group) {
		composite_render_group(table, names, hb_font, face, group, group_idx,
		                       bitmapOffset, &written);
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
