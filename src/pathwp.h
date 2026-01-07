/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef PATHWP_H
#define PATHWP_H

#include "pathw.h"

#define DEF_MARGIN 2

/* Percent of font height */
#define DEF_BUTTON_HEIGHT 70

/* Pixels */
#define DEF_THIN_SHADOW 1
#define DEF_THICK_SHADOW 2

/* Directory Up button width relative to current font height */
#define DIRUP_WIDTH_FACTOR 1.4

/* For arrow icon data */
struct vector2d {
	float x;
	float y;
};

/* Motif class instance data */
struct path_field_class_part {
	XtPointer extension;
};

struct path_field_class_rec {
	CoreClassPart core;
	CompositeClassPart composite;
	ConstraintClassPart constraint;
	XmManagerClassPart manager;
	struct path_field_class_part path_field;
};

struct path_field_part {
	char *home;
	char *real_home;
	XmRenderTable text_rt;
	
	Dimension font_height;
	Widget winput;
	Widget wdirup;
	Widget *wcomp;
	char **sz_comp;
	unsigned int *comp_ids;
	unsigned int ncomp;
	unsigned int ncomp_max;
	Boolean editing;
	char *tmp_path;

	Pixmap pm_arrow;
	Dimension pm_arrow_size;
	GC blit_gc;
	
	/* Dimensions */
	Dimension btn_height;
	Dimension comp_width;
	
	/* Resources */
	Dimension shadow_thickness;
	Dimension highlight_thickness;
	Dimension margin_width;
	Dimension margin_height;
	Dimension btn_height_pc;
	Boolean compact_path;
	Boolean show_dirup;
	
	Boolean processing_callbacks;
	XtCallbackList change_cb;
};

struct path_field_rec {
	CorePart core;
	CompositePart composite;
	ConstraintPart constraint;
	XmManagerPart manager;
	struct path_field_part path_field;
};

extern void XmRenderTableGetDefaultFontExtents(XmRenderTable,
	int *height, int *ascent, int *descent);
extern void _XmMgrTraversal(Widget, Cardinal);

#ifdef PATHW_BITMAPS

/* Different sizes of the up arrow image to cover most typical DPIs */

static unsigned char uparrow5_bits[] = {
   0x1f, 0x1f, 0x1f, 0x1f, 0x1f
};

static unsigned char uparrow8_bits[] = {
	0x08, 0x1c, 0x3e, 0x7f, 0x1c, 0x18, 0xb8, 0x70
};

static unsigned char uparrow12_bits[] = {
   0x10, 0x00, 0x38, 0x00, 0x7c, 0x00, 0xfe, 0x00, 0xff, 0x01, 0x38, 0x00,
   0x38, 0x00, 0x70, 0x00, 0xf0, 0x0c, 0xc0, 0x07, 0x00, 0x00, 0x00, 0x00
};

static unsigned char uparrow16_bits[] = {
   0x00, 0x00, 0x40, 0x00, 0xe0, 0x00, 0xf0, 0x01, 0xf8, 0x03, 0xfc, 0x07,
   0xfe, 0x0f, 0xff, 0x1f, 0xf0, 0x01, 0xf0, 0x01, 0xf0, 0x01, 0xe0, 0x03,
   0xe0, 0xe7, 0xc0, 0x7f, 0x00, 0x3f, 0x00, 0x00
};

static unsigned char uparrow22_bits[] = {
   0x00, 0x02, 0x00, 0x00, 0x07, 0x00, 0x80, 0x0f, 0x00, 0xc0, 0x1f, 0x00,
   0xe0, 0x3f, 0x00, 0xf0, 0x7f, 0x00, 0xf8, 0xff, 0x00, 0xfc, 0xff, 0x01,
   0xfe, 0xff, 0x03, 0xff, 0xff, 0x07, 0xc0, 0x1f, 0x00, 0xc0, 0x1f, 0x00,
   0xc0, 0x1f, 0x00, 0x80, 0x3f, 0x00, 0x80, 0x3f, 0x3c, 0x00, 0x7f, 0x3e,
   0x00, 0xfe, 0x1f, 0x00, 0xfc, 0x0f, 0x00, 0xf0, 0x07, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#endif /* PATHW_BITMAPS */

#endif /* PATHWP_H */
