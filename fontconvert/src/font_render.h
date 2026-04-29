#pragma once
#include <ft2build.h>
#include FT_GLYPH_H
#include "../../gfxfont.h"
#include "types.h"

// Select the best strike or set the char size on face based on global settings.
void setup_face_size(FT_Face face);

// Render codepoints [first, last] from face into table[]/names[], emitting
// bitmap bits via enbit().  Returns 0 on success.
int extract_range_ft(GFXglyph *table, glyph_name *names, FT_Face face,
                     FT_ULong first, FT_ULong last, int *bitmapOffset);

// Shape seq_str with HarfBuzz, render each resulting glyph into table[]/names[],
// and emit bitmap bits via enbit().  Returns the number of glyphs written, or -1.
int shape_and_render_sequence(GFXglyph *table, glyph_name *names, FT_Face face,
                               const char *seq_str, int *bitmapOffset);
