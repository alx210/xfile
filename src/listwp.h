/*
 * Copyright (C) 2019 alx@fastestcode.org
 * This software is distributed under the terms of the MIT license.
 * See the included COPYING file for further information.
 */

/* FileList widget private header */

#ifndef LISTWP_H
#define LISTWP_H

#include "listw.h"
#include <Xm/PrimitiveP.h>
#include <Xm/ScrollBar.h>

/* Time format for detailed view */
#define TIME_FMT "%d %b %Y %H:%M"
#define TIME_BUFSIZ 64

/* Number of items to grow list storage by */
#define LIST_GROW_BY	64

/* Max number of chars for incremental search and how much time
 * passes between key presses until we reset */
#define LOOKUP_STR_MAX 64
#define LOOKUP_TIMEOUT 1500

/* Size of widget struct fields that store mode dependent data */
#define NVIEW_MODES 2

/* Default resource values for margins, spacings and gizmos */
#define DEF_MARGIN	2
#define MAX_MARGIN	16

#define DEF_SPACING 2
#define MAX_SPACING 8

#define DEF_OUTLINE 1
#define MAX_OUTLINE 4

/* Offset in pixels which we consider intended dragging */
#define DEF_DRAG_OFFSET 4
#define MAX_DRAG_OFFSET 16

/* Edge scrolling values */
#define DEF_SCRL_FACTOR 3
#define MAX_SCRL_FACTOR 10
#define DEF_SCRL_TIME 50
#define DEF_SCRL_PIX 3

/* File title shortening */
#define SHORTEN_LEN_MIN 12
#define SHORTEN_LEN_MAX 24

/* Rendition tags */
#define RT_REGULAR	"regular"
#define RT_DIRECT	"directory"
#define RT_SYMLINK	"symlink"
#define RT_SPECIAL	"special"

enum {
	FL_FLABEL,
	FL_FMODE,
	FL_FOWNER,
	FL_FSIZE,
	FL_FTIME,
	NFIELDS
};

/* List item record */
struct item_rec {
	char *name;
	char *title;
	XmString label[NFIELDS];
	
	int db_type;
	unsigned int user_flags;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	time_t ctime;
	time_t mtime;
	unsigned long size;
	Boolean is_symlink;
	
	Pixmap icon_image;
	Pixmap icon_mask;
	Boolean selected;
		
	int x;
	int y;
	unsigned short text_yoff;
	unsigned short width;
	unsigned short detail_width;
	unsigned short field_widths[NFIELDS];
	unsigned short icon_width;
	unsigned short icon_height;
};

/* Used for rubber-banding */
struct rectangle {
	int x;
	int y;
	int width;
	int height;
};

/* Motif class instance data */
struct file_list_class_part {
	XtPointer extension;
};

struct file_list_class_rec {
	CoreClassPart core;
	XmPrimitiveClassPart primitive;
	struct file_list_class_part file_list;
};

/* Motif widget instance data */
struct file_list_part {
	Widget wview;
	Widget whscrl;
	Widget wvscrl;
	
	GC bg_gc;
	GC sbg_gc;
	GC nfbg_gc;
	GC xor_gc;
	GC label_gc;
	GC icon_gc;
	
	Pixel sfg_pixel;
	Pixel fg_pixel;
	Pixmap stipple;
	
	/* data */
	struct item_rec *items;
	unsigned int items_size; /* items array size in item_rec units */
	unsigned int num_items; /* number of items containing data */
	
	/* list and item dimensions */
	unsigned int xoff;
	unsigned int yoff;
	unsigned int list_width;
	unsigned int list_height;
	unsigned short icon_width_max;
	unsigned short icon_height_max;
	unsigned short text_height_max;
	unsigned short item_width_max[NVIEW_MODES];
	unsigned short item_height_max;
	unsigned short field_widths[NFIELDS];
	unsigned int row_height;
	unsigned int ncolumns;

	/* selection state */
	unsigned int cursor;
	unsigned int ext_position;
	struct rectangle sel_rect;
	Boolean dragging;
	Boolean sel_add_mode;
	Boolean owns_prim_sel;
	Boolean has_focus;

	/* shared selection data */
	unsigned int sel_count;
	char **sel_names;

	/* pointer state */
	int ptr_last_x;
	int ptr_last_y;
	XtIntervalId dblclk_timeout;
	Boolean dblclk_lock;
	unsigned int last_clicked;
	
	/* edge scrolling data */
	XtIntervalId autoscrl_timeout;
	int autoscrl_vec;
	
	/* lookup buffer */
	wchar_t sz_lookup[LOOKUP_STR_MAX + 1];
	XtIntervalId lookup_timeout;

	/* resources */
	Dimension margin_w;
	Dimension margin_h;
	Dimension horz_spacing;
	Dimension vert_spacing;
	Dimension label_margin;
	Dimension label_spacing;
	Dimension outline_width;
	Dimension drag_offset;
	unsigned short shorten;
	Boolean show_contents;
	Boolean grab_prim_sel;
	Boolean silent;
	XmRenderTable label_rt;
	XtCallbackList default_action_cb;
	XtCallbackList sel_change_cb;
	XtCallbackList dir_up_cb;
	XtCallbackList delete_cb;
	short dblclk_int;
	short scrl_factor;
	short sort_order;
	short sort_direction;
	short view_mode;
	Pixel highlight_fg;
	Pixel highlight_bg;
};

struct file_list_rec {
	CorePart core;
	XmPrimitivePart primitive;
	struct file_list_part file_list;
};

/* libXm internals */
extern void _XmPrimitiveFocusIn(Widget, XEvent*, String*, Cardinal*);
extern void _XmPrimitiveFocusOut(Widget, XEvent*, String*, Cardinal*);
extern void _XmTraverseNextTabGroup(Widget, XEvent*, String*, Cardinal*);
extern void _XmTraversePrevTabGroup(Widget, XEvent*, String*, Cardinal*);
extern void XmRenderTableGetDefaultFontExtents(XmRenderTable,
	int *height, int *ascent, int *descent);

#endif /* LISTWP_H */
