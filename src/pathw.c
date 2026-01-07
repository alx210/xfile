/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * Implements the PathField widget
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <Xm/DisplayP.h>
#include <Xm/TextF.h>
#include <Xm/DrawnBP.h>
#include <Xm/XmP.h>
#include <Xm/DrawP.h>
#include <Xm/ManagerP.h>

#define PATHW_BITMAPS
#include "pathwp.h"
#include "debug.h"

static Dimension compute_height(Widget w);
static void compute_client_area(Widget w, Position *x, Position *y,
	Dimension *width, Dimension *height);
static void class_initialize(void);
static void initialize(Widget, Widget, ArgList, Cardinal*);
static void destroy(Widget);
static void resize(Widget w);
static void expose(Widget, XEvent*, Region);
static void change_managed(Widget);
static void draw_outline(Widget);
static Dimension compute_height(Widget);
static XtGeometryResult query_geometry(Widget,
	XtWidgetGeometry*, XtWidgetGeometry*);
static Boolean set_values(Widget, Widget, Widget, ArgList, Cardinal*);
static void default_render_table(Widget, int, XrmValue*);
static void default_shadow_thickness(Widget, int, XrmValue*);
static void comp_button_cb(Widget, XtPointer, XtPointer);
static void up_button_expose_cb(Widget, XtPointer, XtPointer);
static void up_button_cb(Widget, XtPointer, XtPointer);
static void input_activate_cb(Widget, XtPointer, XtPointer);
static void input_focus_cb(Widget, XtPointer, XtPointer);
static void input_unfocus_cb(Widget, XtPointer, XtPointer);
static void set_path_from_component(Widget, unsigned int);
static Boolean notify_client(Widget, const char*);
static void update_visuals(Widget, const char*);
static void set_display_string(Widget, const char*, Boolean);
static char *strip_path(char*);

/* Widget resources */
#define RFO(fld) XtOffsetOf(struct path_field_rec, fld)
static XtResource resources[] = {
	{
		XmNrenderTable,
		XmCRenderTable,
		XmRRenderTable,
		sizeof(XmRenderTable),
		RFO(path_field.text_rt),
		XtRCallProc,
		(XtPointer)default_render_table
	},
	{
		XmNshadowThickness,
		XmCShadowThickness,
		XmRDimension,
		sizeof(Dimension),
		RFO(path_field.shadow_thickness),
		XtRCallProc,
		(XtPointer)default_shadow_thickness
	},
	{
		XmNvalueChangedCallback,
		XmCValueChangedCallback,
		XmRCallback,
		sizeof(XtCallbackList),
		RFO(path_field.change_cb),
		XmRCallback,
		NULL	
	},
	{
		XmNmarginWidth,
		XmCMarginWidth,
		XmRDimension,
		sizeof(Dimension),
		RFO(path_field.margin_width),
		XmRImmediate,
		(XtPointer)DEF_MARGIN
	},
	{
		XmNmarginHeight,
		XmCMarginHeight,
		XmRDimension,
		sizeof(Dimension),
		RFO(path_field.margin_height),
		XmRImmediate,
		(XtPointer)DEF_MARGIN
	},
	{
		XfNcompactPath,
		XfCCompactPath,
		XmRBoolean,
		sizeof(Boolean),
		RFO(path_field.compact_path),
		XmRImmediate,
		(XtPointer)True
	},
	{
		XfNshowUpButton,
		XfCShowUpButton,
		XmRBoolean,
		sizeof(Boolean),
		RFO(path_field.show_dirup),
		XmRImmediate,
		(XtPointer)True
	},
	{
		XfNbuttonHeight,
		XfCButtonHeight,
		XmRDimension,
		sizeof(Dimension),
		RFO(path_field.btn_height_pc),
		XmRImmediate,
		(XtPointer)DEF_BUTTON_HEIGHT
	}
};

static struct path_field_class_rec pathFieldWidgetClassRec = {
	.core.superclass = (WidgetClass)&xmManagerClassRec,
	.core.class_name = "PathField",
	.core.widget_size = sizeof(struct path_field_rec),
	.core.class_initialize = class_initialize,
	.core.class_part_initialize = NULL,
	.core.class_inited = False,
	.core.initialize = initialize,
	.core.initialize_hook = NULL,
	.core.realize = XtInheritRealize,
	.core.actions = NULL,
	.core.num_actions = 0,
	.core.resources = resources,
	.core.num_resources = XtNumber(resources),
	.core.xrm_class = NULLQUARK,
	.core.compress_motion = True,
	.core.compress_exposure = True,
	.core.compress_enterleave = True,
	.core.visible_interest = False,
	.core.destroy = destroy,
	.core.resize = resize,
	.core.expose = expose,
	.core.set_values = set_values,
	.core.set_values_hook = NULL,
	.core.set_values_almost = XtInheritSetValuesAlmost,
	.core.get_values_hook = NULL,
	.core.accept_focus = NULL,
	.core.version = XtVersion,
	.core.callback_private = NULL,
	.core.tm_table = XtInheritTranslations,
	.core.query_geometry = query_geometry,
	.core.display_accelerator = NULL,
	.core.extension = NULL,

	.composite.geometry_manager = NULL,
	.composite.change_managed = change_managed,
	.composite.insert_child = XtInheritInsertChild,
	.composite.delete_child = XtInheritDeleteChild,
	.composite.extension = NULL,
	
	.constraint.resources = NULL,
	.constraint.num_resources = 0,
	.constraint.constraint_size = 0,
	.constraint.initialize = NULL,
	.constraint.destroy = NULL,
	.constraint.set_values = NULL,
	.constraint.extension = NULL,
	
	.manager.translations = XtInheritTranslations,
	.manager.syn_resources = NULL,
	.manager.num_syn_resources = 0,
	.manager.syn_constraint_resources = NULL,
	.manager.parent_process = XmInheritParentProcess,
	.manager.extension = NULL
};

WidgetClass pathFieldWidgetClass = (WidgetClass) &pathFieldWidgetClassRec;

#define WARNING(w,s) XtAppWarning(XtWidgetToApplicationContext(w), s)
#define PART(w) &((struct path_field_rec*)w)->path_field
#define REC(w) (struct path_field_rec*)w
#define CORE_WIDTH(w) (((struct path_field_rec*)w)->core.width)
#define CORE_HEIGHT(w) (((struct path_field_rec*)w)->core.height)

static void comp_button_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	unsigned int *id = (unsigned int*)pclient;
	set_path_from_component(XtParent(w), *id);
}

/*
 * Called when path input text field is activated (Return key, usually).
 * Notifies the client and updates the current path if valid.
 */
static void input_activate_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	Widget wparent = XtParent(w);
	struct path_field_part *wp = PART(wparent);
	char *new_path = XmTextFieldGetString(w);

	if(notify_client(wparent, new_path)) {
		strip_path(new_path);
		path_field_set_location(wparent, new_path, False);
		XtFree(new_path);	
	} else if(wp->tmp_path) {
		XmTextFieldSetString(w, wp->tmp_path);
	}
	
	wp->editing = False;
	if(wp->show_dirup) XtSetSensitive(wp->wdirup, True);
	
	/* assuming the next widget is whatever this path box sets path for
	 * (the file list widget, most likely) */
	_XmMgrTraversal(w, XmTRAVERSE_GLOBALLY_FORWARD);
		
}

/*
 * Called whenever the path input text field receives focus.
 * Stores current path string to a temporary buffer, and sets
 * all other gadgets insensitive.
 */
static void input_focus_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	Arg args[1];
	struct path_field_part *wp = PART(pclient);
	Cardinal i;
	char *cur_path = XmTextFieldGetString(w);

	for(i = 0; i < wp->ncomp; i++) {
		/* avoid armed buttons getting stuck in armed state when remapped */
		((XmDrawnButtonRec*)wp->wcomp[i])->drawnbutton.armed = False;
		XtUnmapWidget(wp->wcomp[i]);
	}

	wp->editing = True;
	if(wp->show_dirup) XtSetSensitive(wp->wdirup, False);
	
	draw_outline((Widget)pclient);

	XtSetArg(args[0], XmNcursorPositionVisible, True);
	XtSetValues(w, args, 1);
	XmTextFieldSetInsertionPosition(w, strlen(cur_path));
	XtFree(cur_path);
}

/*
 * Called whenever the path input text field is unfocused.
 * Restores path string saved in input_focus_cb, and sets
 * other gadgets sensitive.
 */
static void input_unfocus_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	Arg args[2];
	struct path_field_part *wp = PART(pclient);
	Cardinal i;

	if(wp->tmp_path) {
		char *cur_path = XmTextFieldGetString(w);

		if(strcmp(wp->tmp_path, cur_path)) 
			XmTextFieldSetString(w, wp->tmp_path);

		XtFree(cur_path);
	}

	wp->editing = False;
	if(wp->show_dirup) XtSetSensitive(wp->wdirup, True);
	
	for(i = 0; i < wp->ncomp; i++) {
		((XmDrawnButtonRec*)wp->wcomp[i])->drawnbutton.armed = False;
		((XmDrawnButtonRec*)wp->wcomp[i])->drawnbutton.click_count = 0;
		XtMapWidget(wp->wcomp[i]);
	}
	
	draw_outline((Widget)pclient);
	
	XmTextFieldSetInsertionPosition(w, 0);
	XtSetArg(args[0], XmNcursorPositionVisible, False);
	XtSetValues(w, args, 1);
}

/*
 * Renders the up-arrow graphic in the directory up button
 */
static void up_button_expose_cb(Widget w, XtPointer closure, XtPointer cdata)
{
	XGCValues gcv;
	Position cx, cy;
	XmDrawnButtonCallbackStruct *cbs = (XmDrawnButtonCallbackStruct*)cdata;
	CorePart *core = &((XmDrawnButtonRec*)w)->core;
	XmPrimitivePart *prim = &((XmDrawnButtonRec*)w)->primitive;
	XmLabelPart *label = &((XmDrawnButtonRec*)w)->label;
	struct path_field_part *wp = PART(closure);
	
	Position shadow = (prim->highlight_thickness > prim->shadow_thickness) ?
		prim->highlight_thickness : prim->shadow_thickness;
	Position margin = (label->margin_width > label->margin_height) ?
		label->margin_width : label->margin_height;

	Dimension minor = (core->width > core->height) ? 
			core->height : core->width;
 	Dimension face_size = (minor - (shadow + margin) * 2);

	if((cbs->reason != XmCR_EXPOSE) ||	(face_size <= 0)) return;

	/* Create appropriately sized arrow pixmap, if not done yet
	 * (it may have been deleted in resize() also) */
	if(!wp->pm_arrow) {
		unsigned char *bits;
		Dimension bits_size;
		
		if(face_size > 22) {
			bits = uparrow22_bits;
			bits_size = 22;
		} else if(face_size > 16) {
			bits = uparrow16_bits;
			bits_size = 16;
		} else if(face_size > 12) {
			bits = uparrow12_bits;
			bits_size = 12;
		} else if(face_size > 8) {
			bits = uparrow8_bits;
			bits_size = 8;
		} else {
			bits = uparrow5_bits;
			bits_size = 5;
		}
		wp->pm_arrow = XCreatePixmapFromBitmapData(XtDisplay(w),
			XtWindow(w), (char*)bits, bits_size, bits_size, 1, 0, 1);
		if(!wp->pm_arrow) {
			WARNING(w, "Failed to create button image\n");
			return;
		}
		wp->pm_arrow_size = bits_size;
	}
	
	cx =  (core->width - wp->pm_arrow_size) / 2;
	cy = (core->height - wp->pm_arrow_size) / 2;
	gcv.clip_x_origin = cx;
	gcv.clip_y_origin = cy;

	gcv.clip_mask = wp->pm_arrow;
	XChangeGC(XtDisplay(w), wp->blit_gc,
		GCClipXOrigin | GCClipYOrigin | GCClipMask, &gcv);
	
	if(core->sensitive) {
		XCopyGC(XtDisplay(w), label->normal_GC, GCForeground, wp->blit_gc);
		XFillRectangle(XtDisplay(w), cbs->window, wp->blit_gc,
			cx, cy, wp->pm_arrow_size, wp->pm_arrow_size);
	} else {
		XCopyGC(XtDisplay(w), label->insensitive_GC,
			GCForeground, wp->blit_gc);
		XFillRectangle(XtDisplay(w), cbs->window, wp->blit_gc,
			cx, cy, wp->pm_arrow_size, wp->pm_arrow_size);
	}
}


/* Called when directory up button is pressed */
static void up_button_cb(Widget w, XtPointer pclient, XtPointer cdata)
{
	struct path_field_part *wp = PART(pclient);

	if(wp->ncomp > 0) {
		set_path_from_component((Widget)pclient, wp->ncomp - 1);
	} else if(wp->compact_path) {
		char *path = XmTextFieldGetString(wp->winput);
		char *ptr;
		
		if(strcmp(path, "~")) {
			XtFree(path);
			return;
		}
		XtFree(path);
		
		path = XtNewString(wp->home);
		if(!path) {
			WARNING(pclient, "Memory allocation error!\n");
			return;
		}
		
		ptr = strrchr(path, '/');
		if(ptr && (ptr != path)) {
			*ptr = '\0';
			path_field_set_location((Widget)pclient, path, True);
		}
		XtFree(path);
	}
}



/* Returns path widget height */
static Dimension compute_height(Widget w)
{
	struct path_field_part *wp = PART(w);
	Dimension wanted_height;

	wanted_height = wp->font_height + wp->btn_height +
		wp->margin_height * 2  + wp->shadow_thickness * 2 +
		wp->highlight_thickness * 2 + 2;
	return wanted_height;
}

/* Returns path widget's usable area */
static void compute_client_area(Widget w, Position *x, Position *y,
	Dimension *width, Dimension *height)
{
	struct path_field_part *wp = PART(w);
	Position hmargin = wp->shadow_thickness;
	Position vmargin = wp->shadow_thickness;

	*x = hmargin;
	*y = vmargin;
	
	if(CORE_WIDTH(w) > hmargin * 2)
		*width = CORE_WIDTH(w) - hmargin * 2;
	else
		*width = 0;
	
	if(CORE_HEIGHT(w) > vmargin * 2)
		*height = CORE_HEIGHT(w) - vmargin * 2;
	else
		*height = 0;
}

/*
 * Widget class methods
 */
static void class_initialize(void) { /* Nothing to do here */ }

static void initialize(Widget wreq, Widget wnew,
	ArgList init_args, Cardinal *ninit_args)
{
	struct path_field_part *wp = PART(wnew);
	struct path_field_rec *wr = REC(wnew);
	Arg args[12];
	Cardinal n;
	int fheight, fascent, fdescent;
	char *home;
	XGCValues gcv;
	XtGCMask gc_mask;
	XtCallbackRec input_activate_cbr[] =
		{ {input_activate_cb, (XtPointer)wnew }, { NULL, NULL } };
	XtCallbackRec input_focus_cbr[] =
		{ {input_focus_cb, (XtPointer)wnew }, { NULL, NULL } };
	XtCallbackRec input_unfocus_cbr[] =
		{ {input_unfocus_cb, (XtPointer)wnew }, { NULL, NULL } };
	XtCallbackRec up_button_activate_cbr[] =
		{ {up_button_cb, (XtPointer)wnew }, { NULL, NULL } };
	XtCallbackRec up_button_expose_cbr[] =
		{ {up_button_expose_cb, (XtPointer)wnew }, { NULL, NULL } };
	
	wp->editing = False;
	wp->tmp_path = NULL;
	wp->ncomp = 0;
	wp->ncomp_max = 0;
	wp->wcomp = NULL;
	wp->comp_ids = NULL;
	wp->home = NULL;
	wp->pm_arrow = None;
	
	home = getenv("HOME");
	if(home) {
		wp->home = XtNewString(home);
		if(wp->home) {
			strip_path(wp->home);
			wp->real_home = realpath(wp->home, NULL);
		}
	}
	
	if(!wp->home || !wp->real_home) {
		WARNING(wnew, "Invalid HOME path");
		wp->compact_path = False;
	}
	
	wr->manager.shadow_thickness = wp->shadow_thickness;
	
	XmRenderTableGetDefaultFontExtents(wp->text_rt,
		&fheight, &fascent, &fdescent);
	wp->font_height = (Dimension) fheight;
	
	/* actual button height in pixels from font height percentage */
	wp->btn_height = (((float)wp->btn_height_pc / 100) * fheight) + 
		wp->shadow_thickness * 2;

	gcv.function = GXcopy;
	gc_mask = GCForeground | GCClipMask | GCClipXOrigin | GCClipYOrigin;
	wp->blit_gc = XtAllocateGC(wnew, 0, GCFunction, &gcv, gc_mask, ~gc_mask);
	
	n = 0;
	XtSetArg(args[n], XmNfocusCallback, input_focus_cbr); n++;
	XtSetArg(args[n], XmNlosingFocusCallback, input_unfocus_cbr); n++;
	XtSetArg(args[n], XmNactivateCallback, input_activate_cbr); n++;
	XtSetArg(args[n], XmNcursorPositionVisible, False); n++;
	XtSetArg(args[n], XmNmarginWidth, wp->margin_width); n++;
	XtSetArg(args[n], XmNmarginHeight, wp->margin_height); n++;
	XtSetArg(args[n], XmNrenderTable, wp->text_rt); n++;
	XtSetArg(args[n], XmNshadowThickness, 0); n++;
	wp->winput = XmCreateTextField(wnew, "pathInput", args, n);
	
	n = 0;
	XtSetArg(args[n], XmNpushButtonEnabled, True); n++;
	XtSetArg(args[n], XmNhighlightThickness, 0); n++;
	XtSetArg(args[n], XmNlabelType, XmPIXMAP); n++;
	XtSetArg(args[n], XmNtraversalOn, False); n++;
	XtSetArg(args[n], XmNsensitive, False); n++;
	XtSetArg(args[n], XmNactivateCallback, up_button_activate_cbr); n++;
	XtSetArg(args[n], XmNexposeCallback, up_button_expose_cbr); n++;
	wp->wdirup = XmCreateDrawnButton(wnew, "upButton", args, n);
	
	XtSetArg(args[0], XmNhighlightThickness, &wp->highlight_thickness);
	XtGetValues(wp->winput, args, 1);

	XtResizeWidget(wnew, 50, compute_height(wnew), 0);
	if(wp->show_dirup) XtManageChild(wp->wdirup);
	XtManageChild(wp->winput);
}

static void resize(Widget w)
{
	struct path_field_part *wp = PART(w);
	Position cx, cy;
	Dimension cw, ch, ub_width = 0;

	compute_client_area(w, &cx, &cy, &cw, &ch);
	
	if(wp->show_dirup) {
		Dimension ub_x;
		
		if(wp->pm_arrow) {
			/* so it will be recomputed in up_button_exposure_cb */
			XFreePixmap(XtDisplay(w), wp->pm_arrow);
			wp->pm_arrow = None;
		}
		
		ub_width =((float)wp->font_height * DIRUP_WIDTH_FACTOR);
		ub_x = ((cx + cw) > ub_width) ? (cx + cw) - ub_width : 0;
		
		XtConfigureWidget(wp->wdirup, ub_x, cy + wp->btn_height,
			ub_width, ch - wp->btn_height, 0);
	}
	
	XtConfigureWidget(wp->winput, cx, cy + wp->btn_height,
		cw - ub_width, ch - wp->btn_height, 0);
}

static XtGeometryResult query_geometry(Widget w,
	XtWidgetGeometry *ig, XtWidgetGeometry *pg)
{
	pg->request_mode = CWHeight;
	pg->height = compute_height(w);
	return XmeReplyToQueryGeometry(w, ig, pg);
}

static Boolean set_values(Widget wcur, Widget wreq,
	Widget wset, ArgList args, Cardinal *nargs)
{
	struct path_field_part *wp_cur = PART(wcur);
	struct path_field_part *wp_set = PART(wset);

	if(wp_cur->show_dirup != wp_set->show_dirup) {
		if(wp_set->show_dirup)
			XtManageChild(wp_set->wdirup);
		else
			XtUnmanageChild(wp_set->wdirup);

		resize(wset);
	}

	return True;
}

static void expose(Widget w, XEvent *evt, Region reg)
{
	draw_outline(w);
}

static void change_managed(Widget w)
{
	XmeNavigChangeManaged(w);
}

static void draw_outline(Widget w)
{
	Display *dpy = XtDisplay(w);
	Window wnd = XtWindow(w);
	struct path_field_part *wp = PART(w);
	struct path_field_rec *wr = REC(w);
	Dimension cw = CORE_WIDTH(w) - wp->shadow_thickness * 2;
	
	if(!wp->ncomp || wp->editing || wp->comp_width < cw) {
		Position pos = (wp->editing || !wp->ncomp) ?
			wp->shadow_thickness : wp->comp_width;
		Dimension width = (wp->editing || !wp->ncomp) ?
			cw : cw - (wp->comp_width - wp->shadow_thickness);

		/* fill up unused path component space with an out shadow */
		XFillRectangle(dpy, wnd, wr->manager.background_GC,
			pos, wp->shadow_thickness, width, wp->btn_height);

		XmeDrawShadows(dpy, wnd,
			wr->manager.top_shadow_GC,
			wr->manager.bottom_shadow_GC,
			pos,
			wp->shadow_thickness,
			width,
			wp->btn_height,
			wp->shadow_thickness, XmSHADOW_OUT);	
	}

	if(wp->shadow_thickness) {
		XmeDrawShadows(dpy, wnd,
			wr->manager.top_shadow_GC,
			wr->manager.bottom_shadow_GC,
			0, 0, wr->core.width, wr->core.height,
			wp->shadow_thickness, XmSHADOW_IN);
	}
}

static void destroy(Widget w)
{
	struct path_field_part *wp = PART(w);
	
	if(wp->wcomp) XtFree((char*)wp->wcomp);
	if(wp->sz_comp) XtFree((char*)wp->sz_comp);
	if(wp->comp_ids) XtFree((char*)wp->comp_ids);
	if(wp->tmp_path) XtFree(wp->tmp_path);
	if(wp->home) XtFree(wp->home);
	if(wp->pm_arrow) XFreePixmap(XtDisplay(w), wp->pm_arrow);
	if(wp->blit_gc) XtReleaseGC(w, wp->blit_gc);

	wp->ncomp = 0;
	wp->ncomp_max = 0;
	wp->wcomp = NULL;
	wp->comp_ids = NULL;
	wp->pm_arrow = None;
}

/*
 * Sets winput's text, and updates component widgets if 'update' is True.
 */
static void set_display_string(Widget w, const char *path, Boolean update)
{
	struct path_field_part *wp = PART(w);
	size_t nbytes = strlen(path);
	size_t i = 0;
	int n;
	char *buf;
	Boolean bad_chars = False;

	/* XXX in case the path string contains invalid multibyte sequences, we
	 *     fix them here so that component button navigation remains usable,
	 *     though the displayed string becomes an approximation */
	buf = XtMalloc(nbytes + 1);
	if(!buf) {
		WARNING(w, "Memory allocation error");
		return;
	}

	mblen(NULL, 0);

	while(path[i]) {
		n = mblen(path + i, nbytes - i);
		if(n == -1) {
			buf[i] = '?';
			n = 1;
			bad_chars = True;
		} else {
			memcpy(buf + i, path + i, n);
		}
		i += n;
	}
	buf[i] = '\0';
	if(bad_chars) WARNING(w, "Invalid multibyte character string");
	XmTextFieldSetString(wp->winput, buf);
	
	if(update) update_visuals(w, buf);
	
	if(wp->tmp_path)
		XtFree(wp->tmp_path);

	wp->tmp_path = buf;
}

/*
 * Updates component button data and their positions.
 */
static void update_visuals(Widget w, const char *path)
{
	struct path_field_part *wp = PART(w);
	Position tx, ty;
	Position bx, cx, cy;
	Dimension cw, ch;
	size_t i, nc, path_len;
	XmTextPosition tpos;
	char *p;
	
	/* count elements and preallocate required space */
	p = (char*)path;
	nc = 1; /* first ~ or / implied */
	while(*p) {
		if(*p == '/') nc++;
		p++;
	}

	for(i = 0; i < wp->ncomp; i++) {
		/* avoid armed buttons getting stuck in armed state when remapped */
		((XmDrawnButtonRec*)wp->wcomp[i])->drawnbutton.armed = False;
		XtUnmanageChild(wp->wcomp[i]);
		XtFree(wp->sz_comp[i]);
	}
	
	/* see if we need to add some buttons */
	if(nc > wp->ncomp_max) {
		Arg args[10];
		Cardinal n = 0;
		XtCallbackRec activate_cbr[] = {
			{ comp_button_cb, NULL }, { NULL, NULL }
		};
		
		wp->ncomp_max = nc;
		wp->wcomp = (Widget*)XtRealloc((char*)wp->wcomp, sizeof(Widget) * nc);
		wp->sz_comp = (char**)XtRealloc((char*)wp->sz_comp, sizeof(char*) * nc);
		wp->comp_ids = (unsigned int*)
			XtRealloc((char*)wp->comp_ids, sizeof(unsigned int) * nc);

		/* since comp_ids address was probably relocated,
		 * update it for existing widgets. */
		XtSetArg(args[0], XmNactivateCallback, activate_cbr);
		for(i = 0; i < wp->ncomp; i++) {
			wp->comp_ids[i] = i;
			activate_cbr[0].closure = &wp->comp_ids[i];
			XtSetValues(wp->wcomp[i], args, 1);
		}
		
		/* add more component push-buttons */
		n = 0;
		XtSetArg(args[n], XmNlabelType, XmPIXMAP); n++;
		XtSetArg(args[n], XmNheight, wp->btn_height); n++;
		XtSetArg(args[n], XmNhighlightThickness, 0); n++;
		XtSetArg(args[n], XmNpushButtonEnabled, True); n++;
		XtSetArg(args[n], XmNtraversalOn, False); n++;
		XtSetArg(args[n], XmNactivateCallback, activate_cbr); n++;
		for(i = wp->ncomp; i < nc; i++) {
			wp->comp_ids[i] = i;
			activate_cbr[0].closure = &wp->comp_ids[i];
			wp->wcomp[i] = XmCreateDrawnButton(w, "pathPart", args, n);
		}
	}
	
	/* measure component lengths and place component buttons */
	compute_client_area(w, &cx, &cy, &cw, &ch);

	i = 0;
	nc = 0;
	bx = cx;
	tpos = 0;
	p = (char*)path;
	path_len = strlen(path);
	
	mblen(NULL, 0);
	
	while(p[i]) {
		size_t blen = 0;
		size_t clen = 0;
		char *s = p + i;
		
		while(p[i] && p[i] != '/') {
			/* mind multibyte; we need position in characters for XmTextField */
			int bytes = mblen(p + i, path_len - i);
			if(bytes == -1) {
				/* shouldn't happen, since we check in set_display_string */
				wp->ncomp = 0;
				return;
			}
			blen += bytes;
			i += bytes;
			clen++;
		}
		if(p[i] == '\0' || p[i + 1] == '\0') break;
		
		if(!blen) {
			wp->sz_comp[nc] = XtNewString("/");
		} else {
			wp->sz_comp[nc] = XtMalloc(blen + 1);
			memcpy(wp->sz_comp[nc], s, blen);
			wp->sz_comp[nc][blen] = '\0';
		}
		blen++; /* last char must have been /, so a single byte */
		clen++;
		
		tpos += clen;
		if(XmTextFieldPosToXY(wp->winput, tpos, &tx, &ty)) {
			tx += cx;
			XtConfigureWidget(wp->wcomp[nc],
				bx, cy, tx - bx, wp->btn_height, 0);
			XtManageChild(wp->wcomp[nc]);
			bx = tx;
		}
		i++;
		nc++;
	}
	wp->comp_width = bx;
	wp->ncomp = nc;

	if(wp->show_dirup) {
		if(nc == 0) {
			if(!wp->compact_path ||
				(wp->compact_path && strcmp(path, "~"))) {
					XtSetSensitive(wp->wdirup, False);
			} else if(wp->compact_path) {
				XtSetSensitive(wp->wdirup, True);
			}
		} else {
			XtSetSensitive(wp->wdirup, True);
		}
	}
	
	draw_outline(w);
}

/*
 * Called by component button activation callback.
 * Removes all components past index (id) specified, updates the text field
 * and calls "value changed" callback. Undoes changes if the callback
 * returns with negative answer.
 */
static void set_path_from_component(Widget w, unsigned int id)
{
	struct path_field_part *wp = PART(w);
	size_t len = 0;
	unsigned int i;
	char *path;
	char *prev_path;
	
	prev_path = XmTextFieldGetString(wp->winput);
	
	for(i = 0, len = 0; i <= id; i++) {
		len += strlen(wp->sz_comp[i]) + 1;
	}
	
	path = XtMalloc(len + 1);
	path[0] = '\0';

	for(i = 0; i <= id; i++) {
		strcat(path, wp->sz_comp[i]);
		if(wp->sz_comp[i][0] != '/' && i != id)
			strcat(path, "/");
	}

	/* update the widget  */
	if(id > 0) {
		Arg arg[1];
		Position pos;
		XtSetArg(arg[0], XmNx, &pos);
		XtGetValues(wp->wcomp[id], arg, 1);
		wp->comp_width = pos;
	} else {
		wp->comp_width = 0;
	}
	set_display_string(w, path, False);

	/* remove discarded data */
	for(i = id; i < wp->ncomp; i++) {
		XtUnmanageChild(wp->wcomp[i]);
		XtFree(wp->sz_comp[i]);
	}
	wp->ncomp = id;
	draw_outline(w);

	if(!notify_client(w, path)) {
		set_display_string(w, prev_path, True);
		XtFree(prev_path);
	}
	XtFree(path);
}

/*
 * Calls "value changed" callback with path string specified.
 * Returns true if client's answer was affirmative.
 */
static Boolean notify_client(Widget w, const char *path)
{
	struct path_field_part *wp = PART(w);
	struct path_field_cb_data cbd;
	char *exp_path = NULL;
	
	if(wp->compact_path && (path[0] == '~') ) {
		size_t len = strlen(wp->home) + strlen(path) + 1;
		/* replace ~ with $HOME */
		exp_path = XtMalloc(len + 1);
		sprintf(exp_path, "%s%s", wp->home, &path[1]);
		cbd.value = exp_path;
	} else {
		cbd.value = (char*)path;
	}
	dbg_trace("new path: %s\n", cbd.value);
	cbd.accept = True;
	wp->processing_callbacks = True;
	XtCallCallbackList(w, wp->change_cb, (XtPointer)&cbd);
	if(exp_path) XtFree(exp_path);
	wp->processing_callbacks = False;
	return cbd.accept;
}

/*
 * Dynamic defaults
 */
static void default_render_table(Widget w, int offset, XrmValue *pv)
{
	static XmRenderTable rt;

	rt = XmeGetDefaultRenderTable(w, XmLABEL_RENDER_TABLE);

	pv->addr = (XPointer) &rt;
	pv->size = sizeof(XmRenderTable);
}

static void default_shadow_thickness(Widget w, int offset, XrmValue *pv)
{
	XmDisplay xmdpy = (XmDisplay)XmGetXmDisplay(XtDisplay(w));
	static Dimension thickness;
	
	if(xmdpy->display.enable_thin_thickness)
		thickness = DEF_THIN_SHADOW;
	else
		thickness = DEF_THICK_SHADOW;
	pv->addr = (XtPointer) &thickness;
	pv->size = sizeof(Dimension);
}

/*
 * Removes / from the end of the string and any doubles within
 */
static char *strip_path(char *path)
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
 * Public routines
 */
int path_field_set_location(Widget w, const char *loc, Boolean notify)
{
	struct path_field_part *wp = PART(w);
	char *subst_path = NULL;
	char *new_path;
	
	/* Make sure we're not heading into an infinite callback loop */
	if(wp->processing_callbacks && notify) {
		WARNING(w, "Can't set location from a notify callback!");
		return EINVAL;
	}
	
	new_path = XtNewString(loc);
	if(!new_path) return ENOMEM;
	strip_path(new_path);
	
	/* replace $HOME part with a ~ if requested */
	if(wp->compact_path) {
		if(!strncmp(new_path, wp->home, strlen(wp->home)) )
			subst_path = wp->home;
		else if (!strncmp(new_path, wp->real_home, strlen(wp->real_home)) )
			subst_path = wp->real_home;
	}
	
	if(subst_path) {
		char *path, *p;

		path = XtMalloc(strlen(new_path));
		p = (char*) new_path + strlen(subst_path);
		sprintf(path, "~%s", p);
		
		if(notify && !notify_client(w, path)) {
			XtFree(path);
			XtFree(new_path);
			return EINVAL;
		}
		set_display_string(w, path, True);
		XtFree(path);
	} else {
		if(notify && !notify_client(w, new_path)) {
			XtFree(new_path);
			return EINVAL;
		}
		set_display_string(w, (char*)new_path, True);
	}
	XtFree(new_path);
	return 0;
}

