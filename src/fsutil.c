/*
 * Copyright (C) 2023-2024 alx@fastestcode.org
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
#include "memdb.h" /* must be the last header */

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
char* get_size_string(unsigned long size, char buffer[SIZE_CS_MAX])
{
	/* What unit names are we using today? */
	char CS_BYTES[] = "B";
	char CS_KILO[] = "K";
	char CS_MEGA[] = "M";
	char CS_GIGA[] = "G";
	char CS_TERRA[] = "T";

	const double kilo = 1024;
	double fsize = size;
	char *sz_units = CS_BYTES;
	double dp;
	char *fmt;

	
	if(size >= pow(kilo, 4)) {
		fsize /= pow(kilo, 4);
		sz_units = CS_TERRA;		
	}else if(size >= pow(kilo, 3)) {
		fsize /=  pow(kilo, 3);
		sz_units = CS_GIGA;
	}else if(size >=  pow(kilo, 2)) {
		fsize /= pow(kilo, 2);
		sz_units = CS_MEGA;
	} else if(size >= kilo) {
		fsize /= kilo;
		sz_units = CS_KILO;
	}

	/* don't show decimal part if it's near .0 */
	dp = fsize - trunc(fsize);
	if(dp > 0.1 && dp < 0.9)
		fmt = "%.1f%s";
	else
		fmt = "%.0f%s";

	snprintf(buffer, SIZE_CS_MAX, fmt, fsize, sz_units);
	return buffer;
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
 * Duplicates str, and replaces any control characters with whitespace.
 */
char* gronk_ctrl_chars(const char *str)
{
	char *p, *s = strdup(str);
	
	if(!s) return NULL;
	
	p = s;
	while(*p != '\0') {
    	   if(iscntrl(*p)) *p = ' ';
    	   p++;
	}
	return s;
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

	sub = strdup(dir);
	trim_path(sub, 1);
	
	if(!stat(dir, &st_dir) && !stat(sub, &st_sub)) {
		same = (st_dir.st_dev == st_sub.st_dev) ? 0 : 1;
	}
	
	if(dir != path) free(dir);
	free(sub);
	return same;
}

