#pragma once
#include <ft2build.h>
#include FT_GLYPH_H
#include "../../gfxfont.h"
#include "types.h"

// Base FT_Load_Glyph flags for the current settings: the render target (mono !=
// 0 -> FT_LOAD_TARGET_MONO) plus the -H grid-fitting choice.
FT_Int32 glyph_load_flags(int mono);

// Select the best strike or set the char size on face based on global settings.
// Returns 0 on success, else the FreeType error (callers must not render on failure).
int setup_face_size(FT_Face face);

// Render codepoints [first, last] from face into table[]/names[], emitting
// bitmap bits via enbit().  Returns 0 on success.
int extract_range_ft(GFXglyph *table, glyph_name *names, FT_Face face,
                     FT_ULong first, FT_ULong last, int *bitmapOffset);

// Shape seq_str with HarfBuzz, render each resulting glyph into table[]/names[],
// and emit bitmap bits via enbit().  Returns the number of glyphs written, or -1.
int shape_and_render_sequence(GFXglyph *table, glyph_name *names, FT_Face face,
                               const char *seq_str, int *bitmapOffset);

// Like shape_and_render_sequence, but composite ALL glyphs of each comma-separated
// group into a SINGLE 1-bit bitmap using HarfBuzz GPOS positions, emitting one
// GFXglyph per group (the -C option).  Mono render path only.  Returns the number
// of groups written, or -1.
int composite_and_render_sequence(GFXglyph *table, glyph_name *names, FT_Face face,
                                   const char *seq_str, int *bitmapOffset);
