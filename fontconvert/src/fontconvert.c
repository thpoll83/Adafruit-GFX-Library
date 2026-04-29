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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#  endif
#  include <direct.h>
#  define strdup _strdup
#  define chdir  _chdir
#  include "getopt_win.h"
#else
#  include <getopt.h>
#  include <pwd.h>
#  include <sys/types.h>
#  include <unistd.h>
   extern char *optarg;
   extern int   optind, opterr, optopt;
#endif

#include <ft2build.h>
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_TRUETYPE_DRIVER_H
#include "../../gfxfont.h"

#include "types.h"
#include "cli.h"
#include "font_render.h"

FontSettings s = {
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
	.contrast = 1.0f,
	.outline = 0,
};

int main(int argc, char *argv[]) {
	FT_Library library;
	GFXglyph *table;
	glyph_name *names;
	FT_Face face;
	ch_range *ranges;
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

	if (!(ranges = (ch_range *)malloc((s.num_ranges <= 0 ? 1 : s.num_ranges) *
	    sizeof(ch_range)))) {
		fprintf(stderr, "Malloc error\n");
		return 1;
	}

	srand(0); // deterministic random

	ranges[0].first = ' ';
	ranges[0].last = '~';

	if (s.num_ranges < 0) {
		s.num_ranges = 1;
		ranges[0].last = to_ulong(argv[optind]);
		range_swap_if_needed(&ranges[0]);
		total_num = range_count(&ranges[0]);
	} else if (s.num_ranges > 0) {
		for (i = 0; i < s.num_ranges; ++i) {
			ranges[i].first = to_ulong(argv[optind++]);
			ranges[i].last  = to_ulong(argv[optind++]);
			range_swap_if_needed(&ranges[i]);
			total_num += range_count(&ranges[i]);
		}
	} else {
		s.num_ranges = 1;
		range_swap_if_needed(&ranges[0]);
		total_num = range_count(&ranges[0]);
	}
	int last_range = s.num_ranges - 1;

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

	if (fontFileName[0] == '~' && strlen(fontFileName) > 2) {
		const char *homedir;
#ifdef _WIN32
		homedir = getenv("USERPROFILE");
		if (homedir == NULL) homedir = getenv("HOMEPATH");
#else
		if ((homedir = getenv("HOME")) == NULL)
			homedir = getpwuid(getuid())->pw_dir;
#endif
		if (homedir == NULL || chdir(homedir) != 0) {
			fprintf(stderr, "Could not access home directory\n");
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

	// Sequence mode may emit up to 2048 shaped glyphs regardless of codepoint ranges
	int alloc_num = (s.sequence && total_num < 2048) ? 2048 : total_num;
	if ((!(fontName = malloc(strlen(fontFileName) + 22))) ||
	    (!(table = (GFXglyph *)malloc(alloc_num * sizeof(GFXglyph)))) ||
	    (!(names = (glyph_name *)malloc(alloc_num * sizeof(glyph_name))))) {
		fprintf(stderr, "Malloc error\n");
		return 1;
	}

	printf("// ");
	for (i = 0; i < argc; ++i)
		printf("%s ", argv[i]);
	printf("\n// Visualize your font via "
	       "https://tchapi.github.io/Adafruit-GFX-Font-Customiser\n\n");

	const char *start = strrchr(fontFileName, '/');
	if (start)
		strcpy(fontName, start + 1);
	else
		strcpy(fontName, fontFileName);

	free(fontFileName);
	fontFileName = strrchr(fontName, '.');
	if (!fontFileName)
		fontFileName = &fontName[strlen(fontName)];
	sprintf(fontFileName, "%s%dpt%db",
	        fontVariantName == NULL ? "" : fontVariantName, s.size,
	        (ranges[last_range].last > 127)
	          ? (ranges[last_range].last > 255) ? 16 : 8
	          : 7);
	for (i = 0; (c = fontName[i]); i++)
		if (isspace(c) || ispunct(c))
			fontName[i] = '_';

	if (s.sequence) {
		// SEQUENCE MODE: HarfBuzz shapes the codepoints, output single block
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
		// RANGE MODE: original codepoint-range extraction
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
		printf("// Approx. %d bytes\n",
		       bitmapOffset + (total_num + skipped) * 7 + 7);
	}

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
