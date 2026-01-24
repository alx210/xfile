/*
 * Copyright (C) 2019-2026 alx@fastestcode.org
 * This software is distributed under the terms of the MIT license.
 * See the included COPYING file for further information.
 */

/* Progress widget private data structures */

#ifndef PROGWP_H
#define PROGWP_H
#include <Xm/PrimitiveP.h>

/* Background color magnitude at which text turns white */
#define FG_THRESHOLD 56000

/* Animation speed for 'intermediate' state */
#define ANIM_FRAMES 8
#define ANIM_FPS 8
#define ANIM_FTIME (1000 / ANIM_FPS)

/* Intermediate state */
#define PROG_INTER	(-1)

struct progress_part {
	XmString xm_text;
	XmRenderTable text_rt;
	GC fg_gc;
	GC bg_gc;
	GC abg_gc;
	Pixel fg_pixel;
	Pixel afg_pixel;
	Pixel prog_pixel;
	Pixmap stipple;
	Dimension hpadding;
	Dimension vpadding;
	Dimension font_height;
	Boolean is_mapped;
	XtIntervalId anim_timer;
	int anim_frame;
	int value;
};

struct progress_rec {
	CorePart core;
	XmPrimitivePart primitive;
	struct progress_part progress;
};

struct progress_class_part {
	XtPointer extension;
};

struct progress_class_rec {
	CoreClassPart core;
	XmPrimitiveClassPart primitive;
	struct progress_class_part progress;
};

/* libXm internals */
extern void XmRenderTableGetDefaultFontExtents(XmRenderTable,
	int *height, int *ascent, int *descent);

#endif /* PROGWP_H */
