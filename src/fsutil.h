/*
 * Copyright (C) 2023 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef FSUTIL_H
#define FSUTIL_H

#define SIZE_CS_MAX 32
#define MODE_CS_MAX 12

/* Returns size string in units J. Random Hacker can grok easily */
char* get_size_string(unsigned long size, char buffer[SIZE_CS_MAX]);

/* Returns ls(1) style mode string for the mode specified */
char* get_mode_string(mode_t, char buffer[MODE_CS_MAX]);

/* Returns descriptive string for unix file type */
char* get_unix_type_string(mode_t);

/* Duplicates str, and replaces any control characters with whitespace */
char* gronk_ctrl_chars(const char *str);

/* Same as readlink, but allocates a buffer */
int get_link_target(const char *path, char **target);

/* Same as getcwd but allocates a buffer */
char* get_working_dir(void);

/* Returns 1 if path (must be fqn) appears to be mounted */
int path_mounted(const char *path);

#endif /* FSUTIL_H */
