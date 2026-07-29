#pragma once
#include <ft2build.h>
#include FT_GLYPH_H
#include "types.h"

int to_int(const char *str);
// Checked integer parse: 1 + *out on full success, 0 on any junk.
int parse_int(const char *str, int *out);
unsigned long to_ulong(const char *str);
void range_swap_if_needed(ch_range *range);
int range_count(const ch_range *range);

void print_usage(char *argv[]);
int parse_args(int argc, char *argv[], char **fontFileName, char **fontVariantName);

void dump_font_info(FT_Face face, const char *fontName);
