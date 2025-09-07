/*
 * Copyright (C) 2023-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include "path.h"
#include "fsutil.h"
#include "debug.h"

#define FS_EXP_MAX 6

/* Same as readlink, but allocates a buffer */
int get_link_target(const char *path, char **ptr)
{
	size_t size = 0;
	size_t read = 0;
	char *buf = NULL;
	
	while(size == read) {
		ssize_t res;
		char *new_buf;
		
		size += 256;
		new_buf = realloc(buf, size);
		if(!new_buf) {
			free(buf);
			return errno;
		} else {
			buf = new_buf;
		}
		res = readlink(path, buf, size);
		if(res == -1) {
			free(buf);
			return errno;
		}
		read = (size_t)res;
	}
	buf[read] = '\0';
	*ptr = buf;
	return 0;
}

/* Same as getcwd but allocates a buffer */
char* get_working_dir(void)
{
	char *cwd = NULL;
	char *buffer = NULL;
	size_t size = 0;	
	
	do {
		char *p;
		size += 256;
		p = realloc(buffer, size + 1);
		if(!p) {
			if(buffer) free(buffer);
			return NULL;
		}
		buffer = p;
		
		cwd = getcwd(buffer, size);
	}while(!cwd && (errno == ERANGE));
	
	if(!cwd && buffer) free(buffer);
	
	return cwd;
}

/*
 * Returns descriptive string for unix file type in st_mode
 */
char* get_unix_type_string(mode_t st_mode)
{
	char *n;
	switch(st_mode & S_IFMT) {
		case S_IFREG: n =  "Regular File"; break;
		case S_IFDIR: n = "Directory"; break;
		case S_IFLNK: n = "Symbolic Link"; break;
		case S_IFSOCK: n = "Socket"; break;
		case S_IFBLK: n = "Block Device"; break;
		case S_IFCHR: n = "Character Device"; break;
		case S_IFIFO: n = "FIFO"; break;
		default: n = "???"; break;
	}
	return n;
}

/*
 * Returns size string in units J. Random Hacker can grok easily
 */
char* get_fsize_string(const struct fsize *fs, char buffer[SIZE_CS_MAX])
{
	/* What unit names are we using today? */
	char *sz_names[] = {
		"B", "K", "M", "G", "T", "P", "E"
	};
	char *sz_units;
	char *fmt;
	double dp;
	double size = fs->size;

	sz_units = sz_names[(int)fs->exp];
	
	/* don't show decimal part if it's near .0 */
	dp = fs->size - trunc(fs->size);
	if(dp > 0.1 && dp < 0.9) {
		fmt = "%.1f%s";
	} else {
		if(dp >= 0.9) size = round(fs->size);
		fmt = "%.0f%s";
	}	
	snprintf(buffer, SIZE_CS_MAX, fmt, size, sz_units);
	return buffer;
}

/* Adds size to fs */
void add_fsize(struct fsize *fs, unsigned long size)
{
	unsigned int i = 0;
	double rsize = size;
	
	while( ((rsize / pow(FS_KILO, i)) >= FS_KILO)
	 	&& (i < FS_EXP_MAX) ) i++;

	if(i > fs->exp) {
		fs->size /= pow(FS_KILO, i - fs->exp);
		fs->exp = i;
	}
	fs->size += rsize / pow(FS_KILO, fs->exp);

	if( (fs->size > FS_KILO) && (fs->exp < FS_EXP_MAX) ) {
		 fs->size /= FS_KILO;
		 fs->exp += 1;
	}
	fs->factor = pow(FS_KILO, fs->exp);
}

void init_fsize(struct fsize *fs)
{
	fs->size = 0;
	fs->factor = 0;
	fs->exp = 0;
}

char* get_size_string(unsigned long size, char buffer[SIZE_CS_MAX])
{
	struct fsize fs = { 0 };

	add_fsize(&fs, size);
	
	return get_fsize_string(&fs, buffer);
}

/*
 * Returns ls style mode string for the mode specified
 */
char* get_mode_string(mode_t mode, char buf[MODE_CS_MAX])
{
	switch((mode & S_IFMT)) {
		case S_IFSOCK: buf[0] = 's'; break;
		case S_IFLNK: buf[0] = 'l'; break;
		case S_IFDIR: buf[0] = 'd'; break;
		case S_IFBLK: buf[0] = 'b'; break;
		case S_IFCHR: buf[0] = 'c'; break;
		case S_IFIFO: buf[0] = 'p'; break;
		default: buf[0] = '-'; break;
	};
	
	buf[1] = (mode & S_IRUSR) ? 'r' : '-';
	buf[2] = (mode & S_IWUSR) ? 'w' : '-';
	buf[3] = (mode & (S_ISUID|S_ISGID)) ?
		((mode & S_IXUSR) ? 'S' : 's') : ((mode & S_IXUSR) ? 'x' : '-');
	
	buf[4] = (mode & S_IRGRP) ? 'r' : '-';
	buf[5] = (mode & S_IWGRP) ? 'w' : '-';
	buf[6] = (mode & S_IXGRP) ? 'x' : '-';
	
	buf[7] = (mode & S_IROTH) ? 'r' : '-';
	buf[8] = (mode & S_IWOTH) ? 'w' : '-';
	buf[9] = (mode & S_ISVTX) ?
		((mode & S_IXOTH) ? 'T' : 't') : ((mode & S_IXOTH) ? 'x' : '-');

	buf[10] = '\0';
	
	return buf;
}

/*
 * Returns 1 if path (must be fqn) appears to be mounted
 */
int path_mounted(const char *path)
{
	char *dir = (char*)path;
	char *sub;
	struct stat st_dir;
	struct stat st_sub;
	int same = 0;
	
	if(!lstat(path, &st_dir) && S_ISLNK(st_dir.st_mode)) {
		dir = realpath(path, NULL);
	}
	
	if(!strcmp(dir, "/")) return 1;

	sub = strdup(dir);
	trim_path(sub, 1);
	
	if(!stat(dir, &st_dir) && !stat(sub, &st_sub)) {
		same = (st_dir.st_dev == st_sub.st_dev) ? 0 : 1;
	}
	
	if(dir != path) free(dir);
	free(sub);
	return same;
}
