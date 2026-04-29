#pragma once
#include <stdint.h>
#include <ft2build.h>
#include FT_GLYPH_H

// Accumulate bits for output, with periodic hexadecimal byte write.
void enbit(uint8_t value);

// Apply exposure bias then dispatch to the dithering algorithm selected in
// the global FontSettings.  gray values: 0.0=black, 1.0=white.
void apply_dithering(float *gray, int width, int rows);

// Dispatch a FreeType bitmap to enbit() via the appropriate pixel converter
// and dithering path.  Writes actual rendered dimensions to *out_w/*out_h.
void render_bitmap_to_bits(FT_Bitmap *bitmap, int *out_w, int *out_h);
