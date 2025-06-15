/*
 * Copyright (C) 2023-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef FSUTIL_H
#define FSUTIL_H

#define SIZE_CS_MAX 32
#define MODE_CS_MAX 12
#define FS_KILO 1024

struct fsize {
	double size;
	double factor;
	unsigned int exp;
};

/* Initializes fsize struct to zero */
void init_fsize(struct fsize *fs);

/* Adds size to fs */
void add_fsize(struct fsize *fs, unsigned long size);

/* Returns size string in units J. Random Hacker can grok easily */
char* get_fsize_string(const struct fsize*, char buffer[SIZE_CS_MAX]);
char* get_size_string(unsigned long size, char buffer[SIZE_CS_MAX]);

/* Returns ls(1) style mode string for the mode specified */
char* get_mode_string(mode_t, char buffer[MODE_CS_MAX]);

/* Returns descriptive string for unix file type */
char* get_unix_type_string(mode_t);

/* Same as readlink, but allocates a buffer */
int get_link_target(const char *path, char **target);

/* Same as getcwd but allocates a buffer */
char* get_working_dir(void);

/* Returns 1 if path (must be fqn) appears to be mounted */
int path_mounted(const char *path);

#endif /* FSUTIL_H */
