#pragma once
#include <stdint.h>
#include <ft2build.h>
#include FT_GLYPH_H

// Accumulate bits for output, with periodic hexadecimal byte write.
void enbit(uint8_t value);

// Apply pre-processing (unsharp mask, gamma, contrast, exposure) then dispatch
// to the dithering algorithm selected in the global FontSettings.
// gray values: 0.0=black, 1.0=white.
void apply_dithering(float *gray, int width, int rows);

// Dispatch a FreeType bitmap to enbit() via the appropriate pixel converter
// and dithering path.  Writes actual rendered dimensions to *out_w/*out_h.
// Emits COLUMN-NATIVE (OLED page) bytes: w * ((h+7)/8) whole bytes per glyph.
void render_bitmap_to_bits(FT_Bitmap *bitmap, int *out_w, int *out_h);

// Emit a row-major MSB-first capture buffer (bit = y*w + x) as column-native
// (OLED page) bytes via enbit() — the .plyf/firmware glyph layout.  Used by the
// composite (-C) path in font_render.c, which builds its own row-major canvas.
void emit_buf_col(const uint8_t *buf, int w, int h);
