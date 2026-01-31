/*
 * Copyright (C) 2022-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "debug.h"

/*
 * Builds a path string by concatenating all arguments
 * Allocates memory required if buffer is NULL
 */
char *build_path(char *buffer, const char *base, ...)
{
	va_list ap;
	size_t len = 0;
	size_t i = 0;
	char *arg;
	
	if(!buffer) {
		va_start(ap, base);
		while( (arg = va_arg(ap, char*)) ) {
			len += strlen(arg) + 1;
		}
		va_end(ap);

		len += strlen(base);

		buffer = malloc(len + 1);
		if(!buffer) return NULL;
	}
	
	strcpy(buffer, base);
	i = strlen(buffer);
	if(i) i--;

	va_start(ap, base);
	while( (arg = va_arg(ap, char*)) ) {
		if(buffer[i] != '/' && arg[0] != '/') buffer[++i] = '/';
		while(*arg) {
			buffer[++i] = *arg;
			arg++;
		}
	}
	va_end(ap);

	if(!i || (buffer[i] != '/')) i++;
	buffer[i] = '\0';
	
	return buffer;
}

/*
 * Removes / from the end of the string and any doubles within
 */
char *strip_path(char *path)
{
	char *p = path;
	
	while(*p) {
		if(*p == '/') {
			if(p[1] == '/') {
				char *s = p + 1;
				
				while(s[1] && s[1] == '/') s++;
				
				if(s[1] == '\0' && p != path) {
					*p = '\0';
					break;
				} else {
					memcpy(p, s, strlen(s) + 1);
				}
			} else if(p[1] == '\0' && p != path) {
				*p = '\0';
				break;
			}
		}
		p++;
	}
	return path;
}

/*
 * Trims nelem elements from path
 */
char* trim_path(char *path, unsigned int nelem)
{
	unsigned int i;
	char *p = path;
	
	for(i = 0; i < nelem; i++){
		p = strrchr(path,'/');
		if(p) {
			if(p == path) {
				p[1] = '\0';
				break;
			} else {
				*p = '\0';
			}
		} else {
			break;
		}
	}
	return path;
}

/*
 * Returns the pointer to the last element in the path
 */
char *get_path_tail(const char *path)
{
	char *p = strrchr(path, '/');
	if(p) return p + 1;
	
	return (char*)path;
}

/*
 * Creates directory tree by walking up all components in path specified.
 * Returns zero on success, errno otherwise.
 */
int create_hier(const char *path, mode_t mode)
{
	char *sz;
	char *cp;
	char *t;
	int rv = 0;
	struct stat st;
	
	if(!stat(path, &st)) {
		if(S_ISDIR(st.st_mode))
			return EEXIST;
		else
			return ENOTDIR;
	}
	
	sz = strdup(path);
	if(!sz) return errno;
	
	cp = malloc(strlen(path) + 3);
	if(!cp) {
		free(sz);
		return errno;
	}
	
	if(path[0] == '/') {
		cp[0] = '/';
		cp[1] = '\0';
	} else {
		cp[0] = '\0';
	}
	t = strtok(sz, "/");
	if(!t) t = sz;
	
	do {
		strcat(cp, t);
		if(stat(cp, &st) == -1) {
			if(errno == ENOENT) {
				if(mkdir(cp, mode)) {
					rv = errno;
					break;
				}
			} else {
				rv = errno;
				break;
			}
		} else if(!S_ISDIR(st.st_mode)) {
			rv = ENOTDIR;
			break;
		}
		strcat(cp, "/");
	} while( (t = strtok(NULL, "/")) );
	
	free(sz);
	free(cp);
	
	return rv;
}


/*
 * Decodes a percent-encoded URL string.
 * Returns a newly allocated C string on success.
 */
char* decode_url(const char *url)
{
	const char *p = url;
	char *r;
	char *buf;
	
	r = buf = malloc(strlen(url) + 1);
	
	if(!buf) return NULL;
	
	while(*p) {

		if(*p == '%' && p[1] && p[2]) {
			char val[3];
			
			p++;
			memcpy(val, p, 2);
			val[2] = '\0';
			*r = (char)strtol(val, NULL, 16);
			
			p++;
		} else {
			*r = *p;
		}
		
		r++;
		p++;
	};
	*r = '\0';

	return buf;
}
