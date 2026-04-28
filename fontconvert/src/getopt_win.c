/* Minimal POSIX getopt for MSVC — public domain */
#include "getopt_win.h"
#include <stdio.h>
#include <string.h>

int   optind = 1;
int   opterr = 1;
int   optopt = 0;
char *optarg = NULL;

int getopt(int argc, char * const argv[], const char *optstring) {
    static int sp = 1;
    int c;
    const char *cp;

    if (sp == 1) {
        if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
            return -1;
        if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
            optind++;
            return -1;
        }
    }

    optopt = c = (unsigned char)argv[optind][sp];

    /* Locate option in optstring (skip leading ':' if present). */
    cp = strchr(optstring + (optstring[0] == ':' ? 1 : 0), c);
    if (cp == NULL || c == ':') {
        if (opterr && optstring[0] != ':')
            fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
        if (argv[optind][++sp] == '\0') { optind++; sp = 1; }
        return '?';
    }

    if (cp[1] == ':') {
        /* Option requires an argument. */
        if (argv[optind][sp + 1] != '\0') {
            optarg = &argv[optind++][sp + 1];
        } else if (++optind >= argc) {
            if (opterr && optstring[0] != ':')
                fprintf(stderr, "%s: option requires an argument -- %c\n",
                        argv[0], c);
            sp = 1;
            optarg = NULL;
            return optstring[0] == ':' ? ':' : '?';
        } else {
            optarg = argv[optind++];
        }
        sp = 1;
    } else {
        /* No argument. */
        if (argv[optind][++sp] == '\0') { sp = 1; optind++; }
        optarg = NULL;
    }
    return c;
}
