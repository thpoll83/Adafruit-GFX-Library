/* Minimal POSIX getopt for MSVC — public domain */
#pragma once
#ifndef GETOPT_WIN_H
#define GETOPT_WIN_H

extern int   optind;
extern int   opterr;
extern int   optopt;
extern char *optarg;

int getopt(int argc, char * const argv[], const char *optstring);

#endif /* GETOPT_WIN_H */
