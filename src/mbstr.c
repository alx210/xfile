/*
 * Copyright (C) 2023-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include "debug.h"

/* Multibyte string utility routines */

/* 
 * Shortens a multibyte string to max_chrs.
 * Returns a string allocated from heap, or NULL on error.
 */
char* shorten_mb_string(const char *sz, size_t max_chrs, int ltor)
{
	size_t nbytes = strlen(sz);
	size_t nchrs = 0;
	size_t i = 0;
	char *result;
	
	mblen(NULL, 0);
	
	while(sz[i]) {
		int n = mblen(sz + i, nbytes - i);
		if(n <= 0) break;
		nchrs++;
		i += n;
	}

	if(nchrs <= max_chrs)
		return strdup(sz);
	
	mblen(NULL, 0);
	
	if(ltor) {
		int i;
		size_t n, nskip = nchrs - (max_chrs - 3);
		
		for(n = 0, i = 0; n < nskip; n++) { 
			i += mblen(sz + i, nbytes - i);
		}
		nbytes = strlen(sz + i);
		result = malloc(nbytes + 4);
		if(!result) return NULL;
		sprintf(result, "...%s", sz + i);
	} else {
		int i;
		size_t n, nskip = nchrs - (nchrs - (max_chrs - 3));
		
		for(n = 0, i = 0; n < nskip; n++) { 
			i += mblen(sz + i, nbytes - i);
		}
		result = malloc(i + 4);
		if(!result) return NULL;
		strncpy(result, sz, i);
		result[i] = '\0';
		strcat(result, "...");	
	}
	return result;
}

/* Returns number of characters in a multibyte string */
size_t mb_strlen(const char *sz)
{
	size_t nbytes = strlen(sz);
	size_t nchrs = 0;
	size_t i = 0;
	
	if(!nbytes) return 0;
	
	mblen(NULL, 0);
	
	while(sz[i]) {
		int n = mblen(sz + i, nbytes - i);
		if(n == -1) n = 1; else if(n == 0) break;
		nchrs++;
		i += n;
	}
	return nchrs;
}

/* Converts a multibyte string to lowercase */
char* mbs_tolower(const char *src)
{
	size_t nbytes = strlen(src);
	size_t is = 0;
	size_t id = 0;
	wchar_t wc;
	int ns, nd;
	char *dest;

	dest = malloc(mb_strlen(src) * MB_CUR_MAX + 1);
	if(!dest) return NULL;


	mbtowc(NULL, 0, 0);
	wctomb(NULL, 0);

	while(src[is]) {
		ns = mbtowc(&wc, src + is, nbytes - is);
		if(ns == -1) {
			dest[id] = '?';
			id++;
			is++;
			continue;
		}

		nd = wctomb(dest + id, towlower(wc));
		if(nd == -1) {
			dest[id] = '?';
			nd = 1;
		}

		is += ns;
		id += nd;
	}
	dest[id] = '\0';
	return dest;
}

/* 
 * Makes the pointed string suitable for GUI display by replacing control
 * characters with whitespace, and discarding any invalid multibyte sequences.
 * Returns new string allocated from heap, or NULL on error.
 */
char* mbs_make_displayable(const char *src)
{
	size_t nbytes = strlen(src);
	size_t is = 0;
	size_t id = 0;
	int ns;
	char *dest;

	dest = malloc(nbytes + 3);
	if(!dest) return NULL;

	mblen(NULL, 0);

	while(src[is]) {
		ns = mblen(src + is, nbytes - is);
		if(ns == -1) {
			dest[id] = '?';
			id++;
			is++;
			continue;
		} else if(ns == 1 && iscntrl(src[is])) {
			dest[id] = ' ';
		} else {
			memcpy(dest + id, src + is, ns);
		}
		id += ns;
		is += ns;
	}
	dest[id] = '\0';
	return dest;
}

/*
 * Converts a multibyte string to iso8859-1, substituting out of
 * code page characters with a question mark. The resulting string
 * is placed in dest, which may point to the same buffer as src.
 */
void mbs_to_latin1(const char *src, char *dest)
{
	size_t nbytes = strlen(src);
	size_t is = 0;
	size_t id = 0;
	int ns;

	mblen(NULL, 0);

	while(src[is]) {
		ns = mblen(src + is, nbytes - is);
		if(ns == -1) {
			dest[id] = '?';
			id++;
			is++;
			continue;
		} else if(ns > 2) {
			dest[id] = '?';
		} else if(ns == 2) {
			dest[id] = (src[is] & 0x1C) ? '?' :
				((src[is] << 6) | (src[is + 1] & 0x3F));
		} else {
			dest[id] = src[is];
		}
		id++;
		is += ns;
	}
	dest[id] = '\0';
}
