/*
TrueType to Adafruit_GFX font converter.  Derived from Peter Jakobs'
Adafruit_ftGFX fork & makefont tool, and Paul Kourany's Adafruit_mfGFX.

NOT AN ARDUINO SKETCH.  This is a command-line tool for preprocessing
fonts to be used with the Adafruit_GFX Arduino library.

For UNIX-like systems.  Outputs to stdout; redirect to header file, e.g.:
  ./fontconvert ~/Library/Fonts/FreeSans.ttf 18 > FreeSans18pt7b.h

REQUIRES FREETYPE LIBRARY.  www.freetype.org

See notes at end for glyph nomenclature & other tidbits.
*/
#ifndef ARDUINO

#include <ctype.h>
#include <ft2build.h>
#include <stdint.h>
#include <stdio.h>
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_TRUETYPE_DRIVER_H
#include "../gfxfont.h" // Adafruit_GFX font structures

#define DPI 141 // Approximate res. of Adafruit 2.8" TFT

typedef struct ch_range {
  int first;
  int last;
} ch_range;

typedef struct glyph_name {
  char name[32];
} glyph_name;

// Accumulate bits for output, with periodic hexadecimal byte write
void enbit(uint8_t value) {
  static uint8_t row = 0, sum = 0, bit = 0x80, firstCall = 1;
  if (value)
    sum |= bit;          // Set bit if needed
  if (!(bit >>= 1)) {    // Advance to next bit, end of byte reached?
    if (!firstCall) {    // Format output table nicely
      if (++row >= 12) { // Last entry on line?
        printf(",\n  "); //   Newline format output
        row = 0;         //   Reset row counter
      } else {           // Not end of line
        printf(", ");    //   Simple comma delim
      }
    }
    printf("0x%02X", sum); // Write byte value
    sum = 0;               // Clear for next byte
    bit = 0x80;            // Reset bit counter
    firstCall = 0;         // Formatting flag
  }
}

int to_num(const char *str) {
  int result;
  if (strncmp("0x", str, 2) == 0) {
    sscanf(str, "%x", &result);
  } else {
    sscanf(str, "%d", &result);
  }
  return result;
}

void range_swap_if_needed(ch_range *range) {
  if (range->last < range->first) {
    int swap = range->first;
    range->first = range->last;
    range->last = swap;
  }
}

int range_count(const ch_range *range) {
  return range->last - range->first + 1;
}

int extract_range(GFXglyph *table, glyph_name* names, FT_Face *face, int size, int first, int last,
                  int *bitmapOffset) {
  int i, err, x, y, byte;
  static int j = 0;

  FT_Glyph glyph;
  FT_Bitmap *bitmap;
  FT_BitmapGlyphRec *g;

  uint8_t bit;

  // << 6 because '26dot6' fixed-point format
  FT_Set_Char_Size(*face, size << 6, 0, DPI, 0);

  // Process glyphs and output huge bitmap data array
  for (i = first; i <= last; i++, j++) {
    // MONO renderer provides clean image with perfect crop
    // (no wasted pixels) via bitmap struct.
    if ((err = FT_Load_Char(*face, i, FT_LOAD_TARGET_MONO))) {
      fprintf(stderr, "Error %d loading char '%c'\n", err, i);
      continue;
    }

    //the name is optional
    if ((err = FT_Get_Glyph_Name(*face, i, names[j].name, 32))) {
      names[j].name[0] = 0;
    }

    if ((err = FT_Render_Glyph((*face)->glyph, FT_RENDER_MODE_MONO))) {
      fprintf(stderr, "Error %d rendering char '%c'\n", err, i);
      continue;
    }

    if ((err = FT_Get_Glyph((*face)->glyph, &glyph))) {
      fprintf(stderr, "Error %d getting glyph '%c'\n", err, i);
      continue;
    }

    bitmap = &((*face)->glyph->bitmap);
    g = (FT_BitmapGlyphRec *)glyph;

    // Minimal font and per-glyph information is stored to
    // reduce flash space requirements.  Glyph bitmaps are
    // fully bit-packed; no per-scanline pad, though end of
    // each character may be padded to next byte boundary
    // when needed.  16-bit offset means 64K max for bitmaps,
    // code currently doesn't check for overflow.  (Doesn't
    // check that size & offsets are within bounds either for
    // that matter...please convert fonts responsibly.)
    table[j].bitmapOffset = *bitmapOffset;
    table[j].width = bitmap->width;
    table[j].height = bitmap->rows;
    table[j].xAdvance = (*face)->glyph->advance.x >> 6;
    table[j].xOffset = g->left;
    table[j].yOffset = 1 - g->top;

    for (y = 0; y < bitmap->rows; y++) {
      for (x = 0; x < bitmap->width; x++) {
        byte = x / 8;
        bit = 0x80 >> (x & 7);
        enbit(bitmap->buffer[y * bitmap->pitch + byte] & bit);
      }
    }

    // Pad end of char bitmap to next byte boundary if needed
    int n = (bitmap->width * bitmap->rows) & 7;
    if (n) {     // Pixel count not an even multiple of 8?
      n = 8 - n; // # bits to next multiple
      while (n--)
        enbit(0);
    }
    *bitmapOffset += (bitmap->width * bitmap->rows + 7) / 8;

    FT_Done_Glyph(glyph);
  }

  return 0;
}

int main(int argc, char *argv[]) {
  FT_Library library;
  GFXglyph *table;
  glyph_name *names;
  FT_Face face;
  ch_range *ranges;
  int first = ' ';
  int last = '~';
  int total_num = 0, skipped = 0;
  int err, size, i, j, r;
  char *fontName, *ptr, c;
  int bitmapOffset = 0;

  // Parse command line.  Valid syntaxes are:
  //   fontconvert [filename] [size]
  //   fontconvert [filename] [size] [last char]
  //   fontconvert [filename] [size] [first char] [last char]
  // Unless overridden, default first and last chars are
  // ' ' (space) and '~', respectively

  if (argc < 3 || (argc > 5 && (argc % 2) == 0)) {
    if (argc < 3) {
      fprintf(stderr, "Too few arguments!\n\n");
    } else {
      fprintf(stderr, "Range end not specified! %d arguments supplied.\n\n",
              argc);
    }
    fprintf(stderr, "Usage: %s fontfile size ([first] [last])*\n", argv[0]);
    fprintf(stderr, "If there is no first and last element, the\n");
    fprintf(stderr, "default range from ' '(%d) to '~'(%d) will be used.\n\n",
            first, last);
    fprintf(stderr, "Examples\n========\n");
    fprintf(stderr, "  Extract Japanese Hiragana, size 12:\n");
    fprintf(stderr, "          %s hiragana_font.otf 12 12353 12447\n", argv[0]);
    fprintf(stderr, "  Extract with default range, size 18:\n");
    fprintf(stderr, "          %s my_font.otf 18\n", argv[0]);
    fprintf(stderr, "  Extract only until 'Z', size 22:\n");
    fprintf(stderr, "          %s my_font.otf 22 0x5a\n", argv[0]);
    fprintf(
        stderr,
        "  Extract Korean Hangul Jamo basic consonants and vovels, size 16:\n");
    fprintf(stderr,
            "          %s jamo_font.otf 16 0x1100 0x1112 0x1161 0x1169 0x116d "
            "0x116e 0x1172 0x1175\n",
            argv[0]);
    return 1;
  }

  size = to_num(argv[2]);

  ptr = strrchr(argv[1], '/'); // Find last slash in filename
  if (ptr)
    ptr++; // First character of filename (path stripped)
  else
    ptr = argv[1]; // No path; font in local dir.

  int num_ranges = argc <= 5 ? 1 : (argc - 5) / 2 + 1;
  if (!(ranges = (ch_range *)malloc(num_ranges * sizeof(ch_range)))) {
    fprintf(stderr, "Malloc error\n");
    return 1;
  }

  ranges[0].first = first;
  ranges[0].last = last;

  if (argc == 4) {
    ranges[0].last = to_num(argv[3]);
    range_swap_if_needed(&ranges[0]);
    total_num = range_count(&ranges[0]);
  } else if (argc > 4) {
    for (i = 0; i < num_ranges; ++i) {
      ranges[i].first = to_num(argv[5 + i * 2 - 2]);
      ranges[i].last = to_num(argv[5 + i * 2 - 1]);
      range_swap_if_needed(&ranges[i]);
      total_num += range_count(&ranges[i]);
    }
  } else {
    range_swap_if_needed(&ranges[0]);
    total_num = range_count(&ranges[0]);
  }

  // Allocate space for font name and glyph table
  if ((!(fontName = malloc(strlen(ptr) + 22))) ||
      (!(table = (GFXglyph *)malloc(total_num * sizeof(GFXglyph)))) ||
      (!(names = (glyph_name *)malloc(total_num * sizeof(glyph_name))))) {
    fprintf(stderr, "Malloc error\n");
    return 1;
  }

  // Init FreeType lib, load font
  if ((err = FT_Init_FreeType(&library))) {
    fprintf(stderr, "FreeType init error: %d", err);
    return err;
  }

  // Use TrueType engine version 35, without subpixel rendering.
  // This improves clarity of fonts since this library does not
  // support rendering multiple levels of gray in a glyph.
  // See https://github.com/adafruit/Adafruit-GFX-Library/issues/103
  FT_UInt interpreter_version = TT_INTERPRETER_VERSION_35;
  FT_Property_Set(library, "truetype", "interpreter-version",
                  &interpreter_version);

  if ((err = FT_New_Face(library, argv[1], 0, &face))) {
    fprintf(stderr, "Font load error: %d", err);
    FT_Done_FreeType(library);
    return err;
  }
  // Currently all symbols from 'first' to 'last' are processed.
  // Fonts may contain WAY more glyphs than that, but this code
  // will need to handle encoding stuff to deal with extracting
  // the right symbols, and that's not done yet.
  // fprintf(stderr, "%ld glyphs\n", face->num_glyphs);

  // Derive font table names from filename.  Period (filename
  // extension) is truncated and replaced with the font size & bits.
  strcpy(fontName, ptr);
  ptr = strrchr(fontName, '.'); // Find last period (file ext)
  if (!ptr)
    ptr = &fontName[strlen(fontName)]; // If none, append
  // Insert font size and 7/8/16 bit.  fontName was alloc'd w/extra
  // space to allow this, we're not sprintfing into Forbidden Zone.
  sprintf(ptr, "%dpt%db", size,
          (ranges[num_ranges - 1].last > 127)
              ? (ranges[num_ranges - 1].last > 255) ? 16 : 8
              : 7);
  // Space and punctuation chars in name replaced w/ underscores.
  for (i = 0; (c = fontName[i]); i++) {
    if (isspace(c) || ispunct(c))
      fontName[i] = '_';
  }

  printf("const uint8_t %sBitmaps[] PROGMEM = {\n  ", fontName);

  for (i = 0; i < num_ranges; ++i) {
    // In case we want to se the range segemnts in the bitmap:
    // printf(" /*range %d (0x%x - 0x%x) */ ", i, ranges[i].first,
    // ranges[i].last);
    err = extract_range(table, names, &face, size, ranges[i].first, ranges[i].last,
                        &bitmapOffset);
  }

  if (err != 0) {
    FT_Done_FreeType(library);
    return err;
  }

  printf(" };\n\n"); // End bitmap array

  // Output glyph attributes table (one per character)
  printf("const GFXglyph %sGlyphs[] PROGMEM = {\n", fontName);
  j = 0;
  for (r = 0; r < num_ranges; ++r) {
    printf("// bmpOff,   w,   h,xAdv, xOff, yOff      range %d (0x%x - 0x%x)\n",
           r, ranges[r].first, ranges[r].last);
    for (i = ranges[r].first; i <= ranges[r].last; ++i) {
      printf("  { %5d, %3d, %3d, %3d, %4d, %4d }", table[j].bitmapOffset,
             table[j].width, table[j].height, table[j].xAdvance,
             table[j].xOffset, table[j].yOffset);
      if (i < ranges[r].last || r < num_ranges - 1) {
        printf(",   // 0x%02X %s", i, names[j].name);
        if ((i >= ' ') && (i <= '~')) {
          printf(" '%c'", i);
        }
        putchar('\n');
      }
      j++;
    }
    if (r != num_ranges - 1) {
      for (i = ranges[r].last + 1; i < ranges[r + 1].first; ++i) {
        printf("  { %5d, %3d, %3d, %3d, %4d, %4d },   // 0x%02X (skip)\n", 0, 0,
               0, 0, 0, 0, i);
        skipped++;
      }
    }
  }

  last = ranges[num_ranges - 1].last;
  printf(" }; // 0x%02X %s", last, names[j-1].name);
  if ((last >= ' ') && (last <= '~'))
    printf(" '%c'", last);
  printf("\n\n");

  // Output font structure
  printf("const GFXfont %s PROGMEM = {\n", fontName);
  printf("  (uint8_t  *)%sBitmaps,\n", fontName);
  printf("  (GFXglyph *)%sGlyphs,\n", fontName);
  if (face->size->metrics.height == 0) {
    // No face height info, assume fixed width and get from a glyph.
    printf("  0x%02X, 0x%02X, %d /*height*/ };\n\n", ranges[0].first,
           ranges[num_ranges - 1].last, table[0].height);
  } else {
    printf("  0x%02X, // first\n  0x%02X, // last\n  %ld  // height\n };\n\n", ranges[0].first,
           ranges[num_ranges - 1].last, face->size->metrics.height >> 6);
  }

  printf("// Approx. %d bytes\n", bitmapOffset + (total_num+skipped) * 7 + 7);
  // Size estimate is based on AVR struct and pointer sizes;
  // actual size may vary.

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

#endif /* !ARDUINO */
