/*
 * Copyright (C) 2022-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * fstab parser and utility functions
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "fstab.h"
#include "main.h"
#include "debug.h"


struct tab_rec {
	char *dev;
	char *mpt;
	char *fst;
	char *opt;
};

struct mnt_tab {
	struct tab_rec *recs;
	size_t size;
	void *data;
	time_t mtime;
};

/* Module globals */
static const char sz_fstab[] = "/etc/fstab";
static struct mnt_tab fstab = { 0 };

/* Forward declarations */
static void update_fstab(void);
static int read_tab(const char*, struct mnt_tab *tab);
static void gronk_tab(struct mnt_tab *tab);
static char* skip_ws(const char*);
static char* line(char*);
static char* token(char*);

int is_in_fstab(const char *path)
{
	size_t i;
	char *real_path;
	int exist = 0;
	
	update_fstab();
	
	real_path = realpath(path, NULL);
	if(!real_path) return 0;

	for(i = 0; i < fstab.size; i++) {
		if(!strcmp(fstab.recs[i].mpt, real_path))
			exist = 1;
	}
	
	free(real_path);
	return exist;
}

int has_fstab_entries(const char *path)
{
	size_t len;
	size_t i;
	char *real_path;
	int status = 0;
	
	update_fstab();
	
	real_path = realpath(path, NULL);
	if(!real_path) return 0;
	
	for(i = 0; i < fstab.size; i++) {
		char *p = strrchr(fstab.recs[i].mpt, '/');

		if(p && (p > fstab.recs[i].mpt) ) {
			len = (p - fstab.recs[i].mpt);
		} else continue;

		if(strlen(real_path) == len && 
			!strncmp(fstab.recs[i].mpt, real_path, len)) status = 1;
	}
	free(real_path);
	return status;
}

int get_mount_info(const char *path, char **dev, char **fs, char **opt)
{
	size_t i;
	char *real_path;
	int status = EINVAL;
	
	update_fstab();
	
	real_path = realpath(path, NULL);
	if(!real_path) return errno;
	
	for(i = 0; i < fstab.size; i++) {
		if(!strcmp(fstab.recs[i].mpt, real_path)) {
			*dev = fstab.recs[i].dev;
			*fs = fstab.recs[i].fst;
			*opt = fstab.recs[i].opt;
			status = 1;
			break;
		}
	}
	free(real_path);
	
	return status;
}

static void update_fstab(void)
{
	struct stat st;
	int errv;

	if(stat(sz_fstab, &st) == -1) {
		gronk_tab(&fstab);
	} else if(st.st_mtime > fstab.mtime) {
		gronk_tab(&fstab);
		errv = read_tab(sz_fstab, &fstab);
		if(errv) stderr_msg("Error reading %s: %s\n",
					sz_fstab, strerror(errv));
	}
}

static void gronk_tab(struct mnt_tab *tab)
{
	if(tab->size) {
		free(tab->recs);
		free(tab->data);
	}
	memset(tab, 0, sizeof(struct mnt_tab));
}

static int read_tab(const char *file_name, struct mnt_tab *rtab)
{
	FILE *file;
	struct stat st;
	struct tab_rec *tab = NULL;
	size_t tab_size = 0;
	char *buffer;
	char *p;

	if(stat(file_name, &st) == -1)
		return errno;
	
	if(!st.st_size) {
		rtab->size = 0;
		rtab->mtime = st.st_mtime;
		return 0;
	}

	file = fopen(file_name, "r");
	if(!file) return errno;

	buffer = malloc(st.st_size + 1);
	if(!buffer) return errno;

	fread(buffer, st.st_size, 1, file);
	buffer[st.st_size] = '\0';

	p = line(buffer);
	
	while(p) {
		struct tab_rec rec;

		p = skip_ws(p);
		
		rec.dev = token(p);
		rec.mpt = token(NULL);
		rec.fst = token(NULL);
		rec.opt = token(NULL);

		if(rec.dev && rec.mpt && rec.fst && rec.opt) {
			void *tmp;
			tmp = realloc(tab, sizeof(struct tab_rec) * (tab_size + 1));
			if(tmp) {
				tab = tmp;
			} else {
				if(tab) free(tab);
				free(buffer);
				return errno;
			}
			tab[tab_size] = rec;
			tab_size++;
		}
		p = line(NULL);
	}

	rtab->recs = tab;
	rtab->size = tab_size;
	rtab->data = buffer;
	rtab->mtime = st.st_mtime;

	return 0;
}

static char* line(char *p)
{
	static char *lp = NULL;
	char *l;
	
	if(!p) {
		if(lp == NULL || *lp == '\0') return NULL;
		p = lp;
	} else {
		lp = p;
	}
	while(*p != '\0' && *p != '\n') {
		if(*p == '#') *p = '\0';
		 p++;
	}
	l = lp;
	if(*p == '\n') {
		*p = '\0';
		lp = p + 1;
	} else {
		lp = NULL;
	}
	return skip_ws(l);
}

static char* token(char *p)
{
	static char *lp = NULL;
	char *t;

	if(!p) {
		if(lp == NULL || *lp == '\0')
			return NULL;
		else
			p = lp;
	}
	p = skip_ws(p);
	t = p;
	while(*p && !isspace(*p)) p++;
	
	if(p == t) {
		lp = NULL;
		return NULL;
	}
	lp = p + 1;
	*p = '\0';

	return t;	
}

static char* skip_ws(const char *p)
{
	while(isspace(*p)) p++;
	return (char*)p;
}
