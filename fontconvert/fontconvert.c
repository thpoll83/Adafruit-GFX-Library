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
#include <getopt.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_TRUETYPE_DRIVER_H
#include "../gfxfont.h" // Adafruit_GFX font structures

#define DPI 141 // Approximate res. of Adafruit 2.8" TFT

extern char *optarg;
extern int optind, opterr, optopt;

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

int extract_range(GFXglyph *table, glyph_name *names, FT_Face *face, int size,
                  int first, int last, int *bitmapOffset) {
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

    // the name is optional
    if ((err = FT_Get_Glyph_Name(*face, (*face)->glyph->glyph_index,
                                 names[j].name, 32))) {
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

void print_usage(char *argv[]) {
  fprintf(stderr,
          "usage: %s -f FONTFILE [-s SIZE] [-v FONT_VARIANT_NAME] [RANGES]\n",
          argv[0]);
  fprintf(stderr, "  options:\n");
  fprintf(stderr,
          "    -f font file name to use (usually some .ttf or .otf file)\n");
  fprintf(stderr,
          "    -s optional size of the generated pixel font (default is 12)\n");
  fprintf(stderr, "    -v optional font variant name for the generated code "
                  "(avoiding name clashes)\n");
  fprintf(stderr, "    -r optional value to override of the font height\n");
  fprintf(stderr, "    -o provide an optional offset applied to all extracted"
                  "unicode codepoints (can be negative with -n)\n");
  fprintf(stderr, "    -n provide an optional negative offset applied to all"
                  "extracted unicode codepoints (higher priority than -o)\n");
  fprintf(stderr, "    RANGES are pairs of values or a single last value (with "
                  "start=default): last|(first last)+\n");
  fprintf(stderr, "      if there is no range, the default from ' '(32) to "
                  "'~'(126) will be used\n\n");
  fprintf(stderr, "  examples:\n  =========\n");
  fprintf(stderr, "    extract Japanese Hiragana, size 12:\n");
  fprintf(stderr,
          "      %s -f../../fonts/hiragana_font.otf -s 12 12353 12447\n",
          argv[0]);
  fprintf(stderr, "    extract with default range, size 18:\n");
  fprintf(stderr, "      %s -f../../fonts/my_font.otf -s18\n", argv[0]);
  fprintf(stderr, "    extract only until 'Z', size 22:\n");
  fprintf(stderr, "      %s -f../../fonts/my_font.otf -s22 0x5a\n", argv[0]);
  fprintf(
      stderr,
      "    extract Korean Hangul Jamo basic consonants and vowels, size 16:\n");
  fprintf(stderr,
          "      %s -f jamo_font.otf -v _Consonants_ -s 16 0x1100 0x1112\n",
          argv[0]);
  fprintf(stderr,
          "      %s -f jamo_font.otf -v _Vowels_ -s 16 0x1161 0x1169 0x116d "
          "0x116e 0x1172 0x1175\n",
          argv[0]);
}

int parse_args(int argc, char *argv[], int *num_ranges, int *size, int *height, int *offset,
               char **fontFileName, char **fontVariantName) {
  int opt;

  if (argc <= 1) {
    return -1;
  }

  while ((opt = getopt(argc, argv, "s:f:v:r:o:n:")) != -1) {
    switch (opt) {
    case 's':
      if (!optarg) {
        printf("Missing value for argument s!\n");
        return -1;
      }
      *size = to_num(optarg);
      break;
    
    case 'r':
      if (!optarg) {
        printf("Missing value for argument r!\n");
        return -1;
      }
      *height = to_num(optarg);
      break;

    case 'o':
      if (!optarg) {
        printf("Missing value for argument o!\n");
        return -1;
      }
      if(*offset==0) {
        *offset = to_num(optarg);
      } else {
        printf("Ignoring argument o!\n");
      }
      break;

    case 'n':
      if (!optarg) {
        printf("Missing value for argument n!\n");
        return -1;
      }

      *offset = -to_num(optarg);
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

    case '?':
      printf("Ignoring unknown option: %c\n", optopt);
      break;

    case ':':
      printf("Missing argument for %c!\n", optopt);
      return -1;
    }
  }

  if (optind < argc) {
    *num_ranges = argc - optind;

    if (*num_ranges == 1) {
      *num_ranges = -2;
    } else if ((*num_ranges % 2) != 0) {
      fprintf(stderr,
              "Range end not specified! %d free arguments supplied.\n\n",
              *num_ranges);
      return -1;
    }
    *num_ranges /= 2;
  }

  return 0;
}

int main(int argc, char *argv[]) {
  FT_Library library;
  GFXglyph *table;
  glyph_name *names;
  FT_Face face;
  ch_range *ranges;
  int num_ranges = 0, total_num = 0, skipped = 0;
  int err, size = 12, height = 0, codepoint_offset = 0;
  int i, j, r;
  char *fontName, *fontFileName = NULL, *fontVariantName = NULL;
  char c;
  int bitmapOffset = 0;

  if (parse_args(argc, argv, &num_ranges, &size, &height,
                 &codepoint_offset, &fontFileName,
                 &fontVariantName) != 0 || fontFileName == NULL) {
    print_usage(argv);
    return 1;
  }

  if (!(ranges = (ch_range *)malloc((num_ranges <= 0 ? 1 : num_ranges) *
                                    sizeof(ch_range)))) {
    fprintf(stderr, "Malloc error\n");
    return 1;
  }

  ranges[0].first = ' ';
  ranges[0].last = '~';

  if (num_ranges < 0) {
    num_ranges = 1; // default range
    ranges[0].last = to_num(argv[optind]);
    range_swap_if_needed(&ranges[0]);
    total_num = range_count(&ranges[0]);
  } else if (num_ranges > 0) {
    for (i = 0; i < num_ranges; ++i) {
      ranges[i].first = to_num(argv[optind++]);
      ranges[i].last = to_num(argv[optind++]);
      range_swap_if_needed(&ranges[i]);
      total_num += range_count(&ranges[i]);
    }
  } else {          // num_ranges == 0
    num_ranges = 1; // default range
    range_swap_if_needed(&ranges[0]);
    total_num = range_count(&ranges[0]);
  }

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
  FT_UInt interpreter_version = TT_INTERPRETER_VERSION_35;
  FT_Property_Set(library, "truetype", "interpreter-version",
                  &interpreter_version);

  // printf("// Loading front from %s\n", fontFileName);
  if (fontFileName[0] == '~' && strlen(fontFileName) > 2) {
    const char *homedir;

    if ((homedir = getenv("HOME")) == NULL) {
      homedir = getpwuid(getuid())->pw_dir;
    }
    chdir(homedir);
    fontName = &fontFileName[2];
  } else {
    fontName = fontFileName;
  }

  if ((err = FT_New_Face(library, fontName, 0, &face))) {
    fprintf(stderr, "Font load error: %d", err);
    FT_Done_FreeType(library);
    return err;
  }

    // Allocate space for font name and glyph table
  if ((!(fontName = malloc(strlen(fontFileName) + 22))) ||
      (!(table = (GFXglyph *)malloc(total_num * sizeof(GFXglyph)))) ||
      (!(names = (glyph_name *)malloc(total_num * sizeof(glyph_name))))) {
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
  const char *start = strrchr(fontFileName, '/');
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
          fontVariantName == NULL ? "" : fontVariantName, size,
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
    err = extract_range(table, names, &face, size, ranges[i].first,
                        ranges[i].last, &bitmapOffset);
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
        printf(",   // 0x%02X %s ", i, names[j].name);
        if ((i >= ' ') && (i <= '~')) {
          printf(" '%c'", i);
        }
        printf(" (#%d)\n", j);
      }
      j++;
    }
    if (r != num_ranges - 1) {
      for (i = ranges[r].last + 1; i < ranges[r + 1].first; ++i) {
        printf("  { %5d, %3d, %3d, %3d, %4d, %4d },   // 0x%02X (skip)\n",
               0, 0, 0, 0, 0, 0, i);
        skipped++;
      }
    }
  }

  printf(" }; // 0x%02X %s ", ranges[num_ranges - 1].last, names[j - 1].name);
  if ((ranges[num_ranges - 1].last >= ' ') &&
      (ranges[num_ranges - 1].last <= '~')) {
    printf(" '%c'", ranges[num_ranges - 1].last);
  }
  printf(" (#%d)\n\n", j - 1);

  // Output font structure
  printf("const GFXfont %s PROGMEM = {\n", fontName);
  printf("  (uint8_t  *)%sBitmaps,\n", fontName);
  printf("  (GFXglyph *)%sGlyphs,\n", fontName);

  //consider height override
  if(height!=0) {
    face->size->metrics.height = height;
  } else if(face->size->metrics.height == 0) {
    face->size->metrics.height = table[0].height;
  } else {
    face->size->metrics.height = (uint8_t)(face->size->metrics.height >> 6);
  }

  printf("  0x%02X, // first\n  0x%02X, // last\n  %ld   //height\n };\n\n",
           ranges[0].first + codepoint_offset,
           ranges[num_ranges - 1].last + codepoint_offset,
           face->size->metrics.height);

  printf("// Approx. %d bytes\n", bitmapOffset + (total_num + skipped) * 7 + 7);
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
