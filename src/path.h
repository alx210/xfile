/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef PATH_H
#define PATH_H

#include <sys/stat.h>

#ifdef __GNUC__
#define SENTINEL  __attribute__ ((sentinel))
#else
#define SENTINEL
#endif

/*
 * Builds a path string by concatenating all arguments
 * Allocates memory required if buffer is NULL
 */
SENTINEL char *build_path(char *buffer, const char *base, ...);

/* Trims nelem elements from path */
char* trim_path(char *path, unsigned int nelem);

/* Removes / from the end of the string and any doubles within */
char *strip_path(char *path);


/* Returns the pointer to the last element in path */
char *get_path_tail(const char *path);

/*
 * Creates directory tree by walking up all components in path specified.
 * Returns zero on success, errno otherwise.
 */
int create_path(const char *path, mode_t mode);

/*
 * Decodes a percent-encoded URL string.
 * Returns a newly allocated C string on success.
 */
char* decode_url(const char *url);

#endif /* PATH_H */
