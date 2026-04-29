#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#  endif
#  define strdup _strdup
#  include "getopt_win.h"
#else
#  include <getopt.h>
   extern char *optarg;
   extern int   optind, opterr, optopt;
#endif

#include <ft2build.h>
#include FT_GLYPH_H

#include "cli.h"
#include "types.h"

int to_int(const char *str) {
	int result;
	if (strncmp("0x", str, 2) == 0)
		sscanf(str, "%x", &result);
	else
		sscanf(str, "%d", &result);
	return result;
}

unsigned long to_ulong(const char *str) {
	unsigned long result;
	if (strncmp("0x", str, 2) == 0)
		sscanf(str, "%lx", &result);
	else
		sscanf(str, "%lu", &result);
	return result;
}

void range_swap_if_needed(ch_range *range) {
	if (range->last < range->first) {
		unsigned long swap = range->first;
		range->first = range->last;
		range->last = swap;
	}
}

int range_count(const ch_range *range) {
	return range->last - range->first + 1;
}

void print_usage(char *argv[]) {
	fprintf(stderr,
	        "usage: %s -f FONTFILE [-s SIZE] [-v VARIANT] [-g] [-r H] [-W W]\n"
	        "       %*s [-D MODE] [-e EXPOSURE] [-c CONTRAST] [-o OFFSET|-n OFFSET]\n"
	        "       %*s [-S \"G[,G]...\" | RANGES]\n"
	        "       where G = space-separated hex codepoints for one glyph\n",
	        argv[0], (int)strlen(argv[0]), "", (int)strlen(argv[0]), "");
	fprintf(stderr, "  Using FreeType Version %d.%d.%d\n\n", FREETYPE_MAJOR,
	        FREETYPE_MINOR, FREETYPE_PATCH);
	fprintf(stderr, "  options:\n");
	fprintf(stderr,
	        "    -f FILE   Font file to convert (.ttf or .otf)\n");
	fprintf(stderr,
	        "    -s N      Point size for the generated font (default: 12)\n");
	fprintf(stderr,
	        "    -v NAME   Variant name embedded in the C identifiers to avoid\n"
	        "              name clashes when multiple fonts are included together\n");
	fprintf(stderr,
	        "    -g        Grayscale / color-emoji mode:\n"
	        "                BGRA bitmaps (CBDT/sbix color fonts) are composited\n"
	        "                over white then quantised to 1-bit.\n"
	        "                8-bit gray bitmaps are also quantised to 1-bit.\n"
	        "              The dithering algorithm is selected with -D (default: fs).\n"
	        "              Required for NotoColorEmoji and similar color fonts.\n");
	fprintf(stderr,
	        "    -r N      Render-size override in pixels.  For bitmap-only fonts\n"
	        "              (e.g. NotoColorEmoji) the fixed strike size is used and\n"
	        "              -s is ignored; -r sets the yAdvance height reported in\n"
	        "              the GFXfont struct.  Also used by -W to bound scaling.\n");
	fprintf(stderr,
	        "    -W N      Maximum rendered width in pixels.  When a glyph (after\n"
	        "              any -r height limit) is still wider than N, it is scaled\n"
	        "              down proportionally so width <= N.  Useful for wide\n"
	        "              glyphs like landscape-orientation country flags.\n");
	fprintf(stderr,
	        "    -D MODE   Dithering algorithm used when -g is active (default: fs):\n"
	        "                fs        Floyd-Steinberg error diffusion — smooth\n"
	        "                          gradients, slight worm-pattern noise\n"
	        "                stucki    Stucki error diffusion — wider kernel,\n"
	        "                          sharper edges on larger glyphs\n"
	        "                bayer     Bayer 4×4 ordered dithering — regular\n"
	        "                          dot pattern, no error propagation\n"
	        "                threshold Hard threshold at 0.5 — no dithering,\n"
	        "                          cleanest edges for high-contrast art\n"
	        "                random    Stochastic dithering — noise-like pattern\n");
	fprintf(stderr,
	        "    -e N      Exposure bias applied before dithering (range -1.0 to\n"
	        "              1.0, default 0.0).  Positive values shift pixels toward\n"
	        "              white (lower effective threshold); negative values shift\n"
	        "              toward black.  Useful to compensate for OLED gamma.\n");
	fprintf(stderr,
	        "    -c N      Contrast multiplier applied before dithering (default:\n"
	        "              1.0 = unchanged, 0.0 = flat gray, >1.0 = more contrast).\n"
	        "              Stretches gray values around 0.5: output =\n"
	        "              (input - 0.5) * N + 0.5, clamped to [0, 1].  Applied\n"
	        "              after -G and -U, before -e.\n");
	fprintf(stderr,
	        "    -G N      Gamma correction applied before dithering (default: 1.0\n"
	        "              = unchanged).  output = input^(1/N), so N > 1 lifts\n"
	        "              midtones toward white and N < 1 darkens them.  More\n"
	        "              nuanced than -c for distinguishing mid-luminance colours\n"
	        "              (e.g. red vs black) without blowing out whites.\n");
	fprintf(stderr,
	        "    -B N      Saturation boost for color (BGRA) bitmaps only (default:\n"
	        "              0.0 = off, range 0.0-1.0).  Adds N * HSV-saturation to\n"
	        "              the luminance before compositing, lifting fully-saturated\n"
	        "              colours (red, blue, green) away from neutral gray/black.\n"
	        "              Applied during BGRA→gray conversion, before -U/-G/-c/-e.\n");
	fprintf(stderr,
	        "    -U N      Unsharp-mask strength applied before dithering (default:\n"
	        "              0.0 = off).  Subtracts a 3×3 Gaussian blur and adds N\n"
	        "              times the difference back: output = input + N*(input-blur).\n"
	        "              Sharpens stripe edges that bilinear downscaling softens.\n"
	        "              Applied before -G/-c/-e.\n");
	fprintf(stderr,
	        "    -o N      Add N to every codepoint written into the output struct\n"
	        "              (positive offset; overridden by -n)\n");
	fprintf(stderr,
	        "    -n N      Subtract N from every codepoint written into the output\n"
	        "              struct (takes priority over -o)\n");
	fprintf(stderr,
	        "    -S \"...\"  Sequence of hex codepoints shaped by HarfBuzz.\n"
	        "              When -S is given, RANGES are ignored.\n"
	        "              Syntax:\n"
	        "                Spaces separate codepoints within one glyph —\n"
	        "                  HarfBuzz handles ZWJ, ligatures, regional-indicator\n"
	        "                  clustering, etc. within each group.\n"
	        "                Commas separate independent glyphs — each comma group\n"
	        "                  is shaped in its own HarfBuzz call.\n"
	        "              e.g. -S \"1F1E9 1F1EA\"                  German flag\n"
	        "                   -S \"1F1E9 1F1EA, 1F1EB 1F1F7\"     DE + FR flags\n"
	        "                   -S \"1F3F3 FE0F 200D 1F308\"         rainbow flag (ZWJ)\n"
	        "                   -S \"1F600, 1F601, 1F602\"           3 separate emoji\n");
	fprintf(stderr,
	        "    -O N      Outline thickness in pixels (default: 0 = disabled).\n"
	        "              Morphological dilation: every dark pixel within N pixels\n"
	        "              (Chebyshev distance) of any lit pixel is set lit.  The\n"
	        "              original lit pixels are preserved; the result is a lit\n"
	        "              halo that follows the actual glyph shape rather than its\n"
	        "              bounding box.  Useful for glyphs with fine dark features\n"
	        "              (e.g. thin stripes in country flags) that need thickening\n"
	        "              to remain visible at small display sizes.  Glyph\n"
	        "              dimensions are not changed.\n");
	fprintf(stderr,
	        "    -d        Dump all codepoints (and variant selectors) present in\n"
	        "              the font to stderr, then exit without generating output.\n");
	fprintf(stderr,
	        "    RANGES    One or more first/last codepoint pairs (hex or decimal).\n"
	        "              A single value is treated as the last codepoint with\n"
	        "              first=' '(0x20).  Default range: 0x20–0x7E (printable\n"
	        "              ASCII).  Ignored when -S is given.\n");
	fprintf(stderr, "\n  examples:\n");
	fprintf(stderr,
	        "    # Printable ASCII, monochrome, size 14\n"
	        "    %s -fmy_font.ttf -s14 0x20 0x7e > MyFont14pt.h\n\n", argv[0]);
	fprintf(stderr,
	        "    # Japanese Hiragana, size 15\n"
	        "    %s -fhiragana.otf -s15 -v_Hiragana_ 0x3041 0x309f > Hiragana.h\n\n",
	        argv[0]);
	fprintf(stderr,
	        "    # Korean Hangul Jamo consonants and vowels, size 16\n"
	        "    %s -fjamo.otf -v_Consonants_ -s16 0x1100 0x1112 > Consonants.h\n"
	        "    %s -fjamo.otf -v_Vowels_ -s16 0x1161 0x1169 0x116d 0x116e"
	        " 0x1172 0x1175 > Vowels.h\n\n",
	        argv[0], argv[0]);
	fprintf(stderr,
	        "    # Color emoji (smiley faces), grayscale+dither, max height 50 px\n"
	        "    %s -fNotoColorEmoji.ttf -s20 -g -r50 -v_Emoji_ 0x1f600 0x1f64f"
	        " > Emoji.h\n\n", argv[0]);
	fprintf(stderr,
	        "    # Two flags explicitly comma-separated, scaled to fit 60x36 px\n"
	        "    %s -fNotoColorEmoji.ttf -s20 -g -r36 -W60 -v_Flags_"
	        " -S \"1F1E9 1F1EA, 1F1EB 1F1F7\" > Flags.h\n", argv[0]);
}

int parse_args(int argc, char *argv[], char **fontFileName,
               char **fontVariantName) {
	int opt;
	int offset_used = 0;

	if (argc <= 1)
		return -1;

	while ((opt = getopt(argc, argv, "dgs:f:v:r:o:n:S:W:D:e:c:G:B:U:O:")) != -1) {
		switch (opt) {
		case 's':
			if (!optarg) { printf("Missing value for argument s!\n"); return -1; }
			s.size = to_int(optarg);
			break;

		case 'r':
			if (!optarg) { printf("Missing value for argument r!\n"); return -1; }
			s.height = to_int(optarg);
			break;

		case 'o':
			if (!optarg) { printf("Missing value for argument o!\n"); return -1; }
			if (offset_used == 0)
				s.offset = -to_int(optarg);
			else
				printf("Ignoring argument o in favor of argument n!\n");
			break;

		case 'n':
			if (!optarg) { printf("Missing value for argument n!\n"); return -1; }
			s.offset = -to_int(optarg);
			offset_used = 1;
			break;

		case 'f':
			if (!optarg) { printf("Missing value for argument f!\n"); return -1; }
			*fontFileName = strdup(optarg);
			break;

		case 'v':
			if (!optarg) { printf("Missing value for argument v!\n"); return -1; }
			*fontVariantName = strdup(optarg);
			break;

		case 'g':
			s.render_mode = 1;
			break;

		case 'd':
			s.dump_codepoints = 1;
			break;

		case 'S':
			if (!optarg) { printf("Missing value for argument S!\n"); return -1; }
			s.sequence = strdup(optarg);
			break;

		case 'W':
			if (!optarg) { printf("Missing value for argument W!\n"); return -1; }
			s.max_width = to_int(optarg);
			break;

		case 'D':
			if (!optarg) { printf("Missing value for argument D!\n"); return -1; }
			if (strcmp(optarg, "fs") == 0 || strcmp(optarg, "floyd") == 0) {
				s.dither_mode = DITHER_FLOYD_STEINBERG;
			} else if (strcmp(optarg, "stucki") == 0) {
				s.dither_mode = DITHER_STUCKI;
			} else if (strcmp(optarg, "bayer") == 0) {
				s.dither_mode = DITHER_BAYER;
			} else if (strcmp(optarg, "threshold") == 0) {
				s.dither_mode = DITHER_THRESHOLD;
			} else if (strcmp(optarg, "random") == 0) {
				s.dither_mode = DITHER_RANDOM;
			} else {
				fprintf(stderr, "Unknown dithering mode '%s'. "
				        "Valid options: fs, stucki, bayer, threshold, random\n", optarg);
				return -1;
			}
			break;

		case 'e':
			if (!optarg) { printf("Missing value for argument e!\n"); return -1; }
			s.exposure = strtof(optarg, NULL);
			if (s.exposure < -1.0f) s.exposure = -1.0f;
			if (s.exposure >  1.0f) s.exposure =  1.0f;
			break;

		case 'c':
			if (!optarg) { printf("Missing value for argument c!\n"); return -1; }
			s.contrast = strtof(optarg, NULL);
			if (s.contrast < 0.0f) s.contrast = 0.0f;
			break;

		case 'G':
			if (!optarg) { printf("Missing value for argument G!\n"); return -1; }
			s.gamma_val = strtof(optarg, NULL);
			if (s.gamma_val <= 0.0f) s.gamma_val = 0.01f;
			break;

		case 'B':
			if (!optarg) { printf("Missing value for argument B!\n"); return -1; }
			s.saturation_boost = strtof(optarg, NULL);
			if (s.saturation_boost < 0.0f) s.saturation_boost = 0.0f;
			if (s.saturation_boost > 1.0f) s.saturation_boost = 1.0f;
			break;

		case 'U':
			if (!optarg) { printf("Missing value for argument U!\n"); return -1; }
			s.sharpness = strtof(optarg, NULL);
			if (s.sharpness < 0.0f) s.sharpness = 0.0f;
			break;

		case 'O':
			if (!optarg) { printf("Missing value for argument O!\n"); return -1; }
			s.outline = to_int(optarg);
			if (s.outline < 0) s.outline = 0;
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
		s.num_ranges = argc - optind;

		if (s.num_ranges == 1) {
			s.num_ranges = -2;
		} else if ((s.num_ranges % 2) != 0) {
			fprintf(stderr,
			        "Range end not specified! %d free arguments supplied.\n\n",
			        s.num_ranges);
			return -1;
		}
		s.num_ranges /= 2;
	}

	return 0;
}

void dump_font_info(FT_Face face, const char *fontName) {
	fprintf(stderr, "%s Stats:\n", fontName);
	fprintf(stderr, "Num faces: %lu Num glyphs: %lu Maps: %d\n", face->num_faces,
	        face->num_glyphs, face->num_charmaps);

	FT_UInt32 *selectors = FT_Face_GetVariantSelectors(face);
	if (*selectors != 0) {
		fprintf(stderr, "Selectors:");
		while (*selectors != 0) {
			fprintf(stderr, " 0x%x", *selectors);
			selectors++;
		}
		fprintf(stderr, ";\n");
	}

	fprintf(stderr, "=============================================\n");
	FT_Select_Charmap(face, FT_ENCODING_UNICODE);

	FT_UInt gid;
	FT_ULong codepoint = FT_Get_First_Char(face, &gid);
	while (gid != 0) {
		FT_UInt32 *variants = FT_Face_GetVariantsOfChar(face, codepoint);
		fprintf(stderr, "Codepoint: 0x%lx, gid: %u", codepoint, gid);

		if (*variants != 0) {
			fprintf(stderr, " Variants:");
			while (*variants != 0) {
				fprintf(stderr, " 0x%x", *variants);
				variants++;
			}
			fprintf(stderr, ";");
		}

		fprintf(stderr, "\n");
		codepoint = FT_Get_Next_Char(face, codepoint, &gid);
	}
}
