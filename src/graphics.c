/*
 * Copyright (C) 2023-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <Xm/Xm.h>
#include <X11/xpm.h>
#include "main.h"
#include "const.h"
#include "graphics.h"
#include "path.h"
#include "debug.h"
#include "icons.h"

#define PIXMAP_SUFFIX ".xpm"
#define SEARCH_PATHS_MAX 4
#define ICONS_GROW_BY 32

struct icon {
	char *name;
	Pixmap pixmap[_NUM_ICON_SIZES];
	Pixmap mask[_NUM_ICON_SIZES];
	Boolean loaded[_NUM_ICON_SIZES];
	Boolean failed[_NUM_ICON_SIZES];
};


static int create_icon_pixmap(struct icon*, enum icon_size size);
static struct icon* get_icon(const char *name, enum icon_size size);
static char** get_search_paths(void);

static struct icon *icons = NULL;
static unsigned int num_icons = 0;
static unsigned int icons_size = 0;

/*
 * Constructs and returns a NULL terminated array of pixmap search paths
 * in order of importance.
 */
static char** get_search_paths(void)
{
	static Boolean initialized = False;
	static char *search_paths[SEARCH_PATHS_MAX];
	char *home;
	char *path;
	int n = 0;
	
	if(!initialized) {
		if((home = getenv("HOME"))) {
			path = build_path(NULL, home, HOME_SUBDIR, PM_SUBDIR, NULL);
			if(path &&  !access(path, X_OK)) {
				search_paths[n] = path;
					n++;
			} else {
				free(path);
			}
		}
		path = build_path(NULL, PREFIX, SHARE_SUBDIR, PM_SUBDIR, NULL);
		if(path && !access(path, X_OK)) {
			search_paths[n] = path;
			n++;
		} else {
			free(path);
		}
		search_paths[n] = NULL;
		initialized = True;
	}

	return search_paths;
}

/*
 * Looks for a pixmap file in all known locations and returns FQN if found.
 * The caller is responsible for freeing the memory.
 */
static char* find_pixmap_file(char *title)
{
	int i;
	char **paths = get_search_paths();
	char *name = NULL;
	size_t len;
		
	for(i = 0; paths[i] != NULL; i++) {
		len = strlen(paths[i]) + strlen(title) + strlen(PIXMAP_SUFFIX) + 2;
		name = malloc(len);

		if(!name) return NULL;
		
		sprintf(name, "%s/%s%s", paths[i], title, PIXMAP_SUFFIX);
		
		if(!access(name, R_OK)) {
			dbg_trace("found pixmap in: %s\n", name);
			break;
		}
						
		free(name);
		name = NULL;
	}
	return name;
}

/*
 * Retrieves icon image struct of name and size specified,
 * creates one if it doesn't exist.
 */
static struct icon* get_icon(const char *name, enum icon_size size)
{
	unsigned int i;
	struct icon *ptr;
	int rv;
	
	for(i = 0; i < num_icons; i++) {
		if(!strcmp(name, icons[i].name)) {
			if(!icons[i].loaded[size] && !icons[i].failed[size]) {
				rv = create_icon_pixmap(&icons[i], size);
				if(rv == XpmSuccess)
					icons[i].loaded[size] = True;
				else
					icons[i].failed[size] = True;
			}
			return &icons[i];
		}
	}
	
	if(num_icons + 1 > icons_size) {
		ptr = realloc(icons, sizeof(struct icon) * (num_icons + ICONS_GROW_BY));
		if(!ptr) return None;
		icons_size += ICONS_GROW_BY;
		icons = ptr;
	}

	ptr = &icons[num_icons];
	num_icons++;

	memset(ptr, 0, sizeof(struct icon));
	ptr->name = strdup(name);
	
	rv = create_icon_pixmap(ptr, size);
	
	if(rv == XpmSuccess)
		ptr->loaded[size] = True;
	else
		ptr->failed[size] = True;

	return ptr;
}

/*
 * Creates actual pixmaps for an icon and assigns them to img.
 * Returns XpmSuccess on success.
 */
static int create_icon_pixmap(struct icon *icon, enum icon_size size)
{
	XpmAttributes att = {0};
	char *full_name = NULL;
	char *file_path = NULL;
	char **fallback = NULL;
	int res;
	
	att.valuemask = XpmVisual | XpmColormap |
		XpmDepth | XpmBitmapFormat | XpmColorSymbols;
	att.visual = app_inst.visual;
	att.colormap = app_inst.colormap;
	att.depth = DefaultDepthOfScreen(app_inst.screen);
	att.bitmap_format = ZPixmap;

	full_name = malloc(strlen(icon->name) + 3);
	if(!full_name) return XpmNoMemory;
	strcpy(full_name, icon->name);

	switch(size) {
		case IS_LARGE:
		strcat(full_name,".l");	
		fallback = file_l;
		break;
		case IS_MEDIUM:
		strcat(full_name,".m");
		fallback = file_m;
		break;
		case IS_SMALL:
		strcat(full_name,".s");
		fallback = file_s;
		break;
		case IS_TINY:
		strcat(full_name,".t");
		fallback = file_t;
		break;
		default:
		break;
	}
	
	/* look for a file first */
	if( (file_path = find_pixmap_file(full_name)) ) {
		res = XpmReadFileToPixmap(app_inst.display,
			XtWindow(app_inst.wshell), file_path,
			&icon->pixmap[size], &icon->mask[size], &att);
		
		free(file_path);
	} else {
	/* try built-in otherwise */
		unsigned int i;
		
		for(i = 0; i < num_xpm_images; i++) {
			if(!strcmp(xpm_images[i].name, full_name)){
				res = XpmCreatePixmapFromData(app_inst.display,
					XtWindow(app_inst.wshell), xpm_images[i].data,
					&icon->pixmap[size], &icon->mask[size], &att);
				break;
			}
		}
		/* create a generic fallback icon if all of above had failed */
		if(i == num_xpm_images) {
			res = XpmCreatePixmapFromData(app_inst.display,
				XtWindow(app_inst.wshell), fallback,
				&icon->pixmap[size], &icon->mask[size], &att);
			stderr_msg("Icon not found: %s%s\n", full_name, PIXMAP_SUFFIX);
		}
	}
	
	free(full_name);

	return res;
}

/*
 * Loads an icon image from search path, or built-in if none found.
 * Returns True on success, False otherwise.
 */
Boolean get_icon_pixmap(const char *name, enum icon_size size,
	Pixmap *pixmap, Pixmap *mask)
{
	struct icon *icon;
	
	icon = get_icon(name, size);

	if(icon->loaded[size] && !icon->failed[size]) {
		*pixmap = icon->pixmap[size];
		*mask = icon->mask[size];

		return True;
	}

	return False;
}

/*
 * Creates pixmap from XPM data, replacing symbolic colors with 
 * widget's if wui is not NULL. Returns True on success, False otherwise.
 */
Boolean create_ui_pixmap(Widget wui, char **data,
	Pixmap *image, Pixmap *mask)
{
	Cardinal n = 0;
	Arg args[5];
	XpmAttributes att = {0};
	XpmColorSymbol csym[5] = {0};
	int res;
	
	if(wui != NULL) {
		XtSetArg(args[n], XmNforeground, &csym[n].pixel);
		csym[n].name = "foreground";
		n++;
		XtSetArg(args[n], XmNbackground, &csym[n].pixel);
		csym[n].name = "background";	
		n++;
		XtSetArg(args[n], XmNtopShadowColor, &csym[n].pixel);
		csym[n].name = "topShadowColor";	
		n++;
		XtSetArg(args[n], XmNbottomShadowColor, &csym[n].pixel);
		csym[n].name = "bottomShadowColor";
		n++;
		XtSetArg(args[n], XmNbackground, &csym[n].pixel);
		csym[n].name = "none";
		n++;

		XtGetValues(wui, args, n);
	}

	att.valuemask = XpmVisual | XpmColormap |
		XpmDepth | XpmBitmapFormat | XpmColorSymbols;
	att.visual = app_inst.visual;
	att.colormap = app_inst.colormap;
	att.depth = DefaultDepthOfScreen(app_inst.screen);
	att.bitmap_format = ZPixmap;
	att.colorsymbols = csym;
	att.numsymbols = n;

	res = XpmCreatePixmapFromData(XtDisplay(app_inst.wshell),
			XtWindow(app_inst.wshell), data, image, mask, &att);

	return (res == XpmSuccess) ? True : False;
}
