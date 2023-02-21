/*
 * Copyright (C) 2022 alx@fastestcode.org
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
	Widget *wcomp;
	char **sz_comp;
	unsigned int *comp_ids;
	unsigned int ncomp;
	unsigned int ncomp_max;
	Boolean editing;
	char *tmp_path;
	
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

#endif /* PATHWP_H */
