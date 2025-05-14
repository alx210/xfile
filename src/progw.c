/*
 * Copyright (C) 2023-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/* Implements the progress widget */

#include <stdlib.h>
#include <stdio.h>
#include <Xm/XmP.h>
#include <Xm/PrimitiveP.h>
#include <Xm/DrawP.h>
#include "progwp.h"
#include "debug.h"

/* Local routines */
static void class_initialize(void);
static void initialize(Widget, Widget, ArgList, Cardinal*);
static void init_gcs(Widget w);
static void destroy(Widget);
static void realize(Widget, XtValueMask*, XSetWindowAttributes*);
static void expose(Widget, XEvent*, Region);
static void redraw(Widget);
static Boolean widget_display_rect(Widget, XRectangle*);
static void resize(Widget);
static Boolean set_values(Widget, Widget, Widget, ArgList, Cardinal*);
static XtGeometryResult query_geometry(Widget,
	XtWidgetGeometry*, XtWidgetGeometry*);
static void default_render_table(Widget, int, XrmValue*);
static void default_select_color(Widget, int, XrmValue*);
static XmString get_text(Widget w);
static void get_drawable_area(Widget,
	Position*, Position*, Dimension*, Dimension*);
static void anim_timeout_cb(XtPointer data, XtIntervalId *iid);
static void map_event_cb(Widget, XtPointer, XEvent*, Boolean*);

#define WARNING(w,s) XtAppWarning(XtWidgetToApplicationContext(w), s)

/* Widget resources */
#define RFO(fld) XtOffsetOf(struct progress_rec, fld)
static XtResource resources[] = {
	{
		XmNrenderTable,
		XmCRenderTable,
		XmRRenderTable,
		sizeof(XmRenderTable),
		RFO(progress.text_rt),
		XtRCallProc,
		(XtPointer)default_render_table
	},
	{
		XmNhighlightColor,
		XmCHighlightColor,
		XtRPixel,
		sizeof(Pixel),
		RFO(progress.prog_pixel),
		XtRCallProc,
		(void*)default_select_color
	},
	{
		XmNvalue,
		XmCValue,
		XtRInt,
		sizeof(int),
		RFO(progress.value),
		XtRImmediate,
		(XtPointer)0
	}
};
#undef RFO

/* Widget class declarations */
static XmPrimitiveClassExtRec primClassExtRec = {
	.next_extension = NULL,
	.record_type = NULLQUARK,
	.version = XmPrimitiveClassExtVersion,
	.record_size = sizeof(XmPrimitiveClassExtRec),
	.widget_baseline = NULL,
	.widget_display_rect = widget_display_rect,
	.widget_margins = NULL
};

static struct progress_class_rec class_rec_def = {
	.core.superclass = (WidgetClass)&xmPrimitiveClassRec,
	.core.class_name = "Progress",
	.core.widget_size = sizeof(struct progress_rec),
	.core.class_initialize = class_initialize,
	.core.class_part_initialize = NULL,
	.core.class_inited = False,
	.core.initialize = initialize,
	.core.initialize_hook = NULL,
	.core.realize = realize,
	.core.actions = NULL,
	.core.num_actions = 0,
	.core.resources = resources,
	.core.num_resources = XtNumber(resources),
	.core.xrm_class = NULLQUARK,
	.core.compress_motion = True,
	.core.compress_exposure = XtExposeCompressMaximal,
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

	.primitive.border_highlight = NULL,
	.primitive.border_unhighlight = NULL,
	.primitive.translations = XtInheritTranslations,
	.primitive.arm_and_activate = NULL,
	.primitive.syn_resources = NULL,
	.primitive.num_syn_resources = 0,
	.primitive.extension = (XtPointer) &primClassExtRec
};

WidgetClass progressWidgetClass = (WidgetClass) &class_rec_def;

#define PROG_PART(w) (&((struct progress_rec*)w)->progress)
#define PROG_REC(w) ((struct progress_rec*)w)
#define PRIM_PART(w) (&((XmPrimitiveRec*)w)->primitive)
#define CORE_WIDTH(w) (((struct progress_rec*)w)->core.width)
#define CORE_HEIGHT(w) (((struct progress_rec*)w)->core.height)

static void redraw(Widget w)
{
	Display *dpy = XtDisplay(w);
	Window wnd = XtWindow(w);
	XmPrimitivePart *prim = PRIM_PART(w);
	struct progress_part *prog = PROG_PART(w);
	Dimension width = CORE_WIDTH(w);
	Dimension height = CORE_HEIGHT(w);
	Dimension shadow = prim->shadow_thickness;
	Dimension text_width, text_height;
	Dimension pw;
	Position cx, cy;
	Dimension cw, ch;
	Position tx, ty;
	XRectangle clip;
	XmString text;
	
	if( (width < shadow * 2) || (height < shadow * 2) ) return;

	XFillRectangle(dpy, wnd, prog->bg_gc, shadow, shadow,
		width - shadow, height - shadow);

	XmeDrawShadows(dpy, wnd, prim->top_shadow_GC, prim->bottom_shadow_GC,
		0, 0, width, height, shadow, XmSHADOW_IN);
	
	get_drawable_area(w, &cx, &cy, &cw, &ch);
	if(!cw || !ch) return;

	text = get_text(w);
	XmStringExtent(prog->text_rt, text, &text_width, &text_height);

	if(prog->value == PROG_INTER) {
		XSetFillStyle(dpy, prog->abg_gc, FillStippled);
		XFillRectangle(dpy, wnd, prog->abg_gc, cx, cy, cw, ch);
	} else {
		pw = ((float)cw / 100) * prog->value;
		tx = cx + (cw - text_width) / 2;
		ty = cy + (ch - text_height) / 2;

		XSetFillStyle(dpy, prog->abg_gc, FillSolid);
		XFillRectangle(dpy, wnd, prog->abg_gc, cx, cy, pw, ch);
		
		if((prog->fg_pixel != prog->afg_pixel) && (pw >= tx)) {
			/* complex case; inverted foreground for the covered area */
			clip.x = tx;
			clip.y = ty;
			clip.width = (pw - tx);
			clip.height = ch;

			XSetForeground(dpy, prog->fg_gc, prog->afg_pixel);
			XmStringDraw(dpy, wnd, prog->text_rt, text, prog->fg_gc,
				tx, ty,	text_width, XmALIGNMENT_BEGINNING,
				XmSTRING_DIRECTION_DEFAULT, &clip);
			
			if(pw <= (tx + text_width)) {
				clip.x = tx + (pw - tx);
				clip.width = cw - tx;
				XSetForeground(dpy, prog->fg_gc, prog->fg_pixel);		
				XmStringDraw(dpy, wnd, prog->text_rt, text, prog->fg_gc,
					tx, ty,	text_width, XmALIGNMENT_BEGINNING,
					XmSTRING_DIRECTION_DEFAULT, &clip);
			}
		} else {
			/* simple case; same foreground for plain and covered area */
			clip.x = tx;
			clip.y = ty;
			clip.width = cw - tx;
			clip.height = cy - ty;

			XSetForeground(dpy, prog->fg_gc, prog->fg_pixel);
			XmStringDraw(dpy, wnd, prog->text_rt, text, prog->fg_gc,
				tx, ty,	text_width, XmALIGNMENT_BEGINNING,
				XmSTRING_DIRECTION_DEFAULT, &clip);		
		}
	}
}

/* Returns cached XmString, creates/updates it if necessary */
static XmString get_text(Widget w)
{
	struct progress_part *prog = PROG_PART(w);

	if(!prog->xm_text) {
		char szbuf[8];
		snprintf(szbuf, 8, "%u%%", (unsigned short)prog->value);
		prog->xm_text = XmStringCreateLocalized(szbuf);
	}
	return prog->xm_text;
}

/* Returns drawable client area sans shadow and margins */
static void get_drawable_area(Widget w,
	Position *x, Position *y, Dimension *width, Dimension *height)
{
	/* struct progress_part *prog = PROG_PART(w); */
	XmPrimitivePart *prim = PRIM_PART(w);

	if(x) *x = prim->shadow_thickness;
	if(y) *y = prim->shadow_thickness;
	if(width) *width = CORE_WIDTH(w) - prim->shadow_thickness * 2;
	if(height) *height = CORE_HEIGHT(w) - prim->shadow_thickness * 2;
}

/* Returns preferred widget dimensions */
static void get_pref_dimensions(Widget w, Dimension *width, Dimension *height)
{
	struct progress_part *prog = PROG_PART(w);
	XmPrimitivePart *prim = PRIM_PART(w);
	Dimension shadow = prim->shadow_thickness;
	Dimension text_width, text_height;

	XmStringExtent(prog->text_rt, get_text(w), &text_width, &text_height);
	
	if(width) *width = shadow * 2 + prog->hpadding * 2 + text_width;
	if(height) *height = shadow * 2 + prog->vpadding * 2 + text_height;
}

/*
 * Intrinsic widget routines
 */
static void expose(Widget w, XEvent *evt, Region reg)
{
	redraw(w);
}

static void resize(Widget w)
{
	/* nothing to do here yet */
}

static XtGeometryResult query_geometry(Widget w,
	XtWidgetGeometry *ig, XtWidgetGeometry *pg)
{
	Dimension pref_width;
	Dimension pref_height;

	get_pref_dimensions(w, &pref_width, &pref_height);
	
	pg->request_mode = 0;
	
	if((ig->request_mode & CWWidth) && (ig->width < pref_width)) {
		pg->request_mode |= CWWidth;
		pg->width = pref_width;
	}
		
	if((ig->request_mode & CWHeight) && (ig->height < pref_height)) {
		pg->request_mode |= CWHeight;
		pg->height = pref_height;
	}
	
	return XmeReplyToQueryGeometry(w, ig, pg);
}

static Boolean widget_display_rect(Widget w, XRectangle *r)
{
	get_drawable_area(w, &r->x, &r->y, &r->width, &r->height);
	return True;
}

static void realize(Widget w, XtValueMask *mask, XSetWindowAttributes *att)
{
	struct progress_part *p = PROG_PART(w);
	Display *dpy = XtDisplay(w);
	Window wnd;
	const char stipple_data[] = {
		0xC1, 0xE0, 0x70, 0x38,
		0x1C, 0x0E, 0x07, 0x83
	};
	
	(*progressWidgetClass->core_class.superclass->core_class.realize)
		(w, mask, att);

	wnd = XtWindow(w);
	
	p->stipple = XCreatePixmapFromBitmapData(
		dpy, wnd, (char*)stipple_data, 8, 8, 0, 1, 1);
	XSetStipple(dpy, p->abg_gc, p->stipple);
}

static void initialize(Widget wreq, Widget wnew,
	ArgList init_args, Cardinal *ninit_args)
{
	struct progress_part *p = PROG_PART(wnew);
	int height, ascent, descent;
	Dimension pref_width, pref_height;
	
	p->anim_timer = None;
	p->anim_frame = 0;
	p->xm_text = NULL;
	
	XmRenderTableGetDefaultFontExtents(
		p->text_rt,	&height, &ascent, &descent);
	p->hpadding = height / 5;
	p->vpadding = height / 5;
	p->font_height = height;

	init_gcs(wnew);
	
	get_pref_dimensions(wnew, &pref_width, &pref_height);

	if((CORE_WIDTH(wreq) == 0))
		CORE_WIDTH(wnew) = pref_width;
	
	if((CORE_HEIGHT(wreq) == 0))
		CORE_HEIGHT(wnew) = pref_height;

	if(p->value == PROG_INTER) {
		p->anim_timer = XtAppAddTimeOut(
			XtWidgetToApplicationContext(wnew),
			ANIM_FTIME, anim_timeout_cb, (XtPointer)wnew);
	}
	XtAddEventHandler(wnew, StructureNotifyMask, False, map_event_cb, NULL);
}

static void init_gcs(Widget w)
{
	struct progress_rec *r = PROG_REC(w);
	XGCValues gcv;
	XtGCMask gc_mask;
	XColor xc = { 0 };

	r->progress.fg_pixel = r->primitive.foreground;
	
	/* Label GC */
	gcv.function = GXcopy;
	gcv.foreground = r->progress.fg_pixel;
	gc_mask = GCForeground | GCFunction;

	/* XmStringDraw needs an allocated GC */
	r->progress.fg_gc = XtAllocateGC(w, 0, gc_mask, &gcv,
		gc_mask | GCClipMask | GCFont, 0);

	/* Generic fg/bg shareable GCs */
	gcv.foreground = r->core.background_pixel;
	r->progress.bg_gc = XtGetGC(w, GCForeground, &gcv);
	
	/* progress background */
	gcv.foreground = r->progress.prog_pixel;
	r->progress.abg_gc = XtGetGC(w, GCForeground, &gcv);

	xc.pixel = r->progress.prog_pixel;
	XQueryColor(XtDisplay(w), r->core.colormap, &xc);
	
	/* text on progress-bar color */
	xc.red /= 256;
	xc.green /= 256;
	xc.blue /= 256;

	r->progress.afg_pixel = 
		(((unsigned int)xc.red * xc.red + xc.green * xc.green +
			xc.blue * xc.blue) > FG_THRESHOLD) ?
		BlackPixelOfScreen(r->core.screen) :
		WhitePixelOfScreen(r->core.screen);
}

static Boolean set_values(Widget wcur, Widget wreq,
	Widget wset, ArgList args, Cardinal *nargs)
{
	struct progress_rec *cur = (struct progress_rec*) wcur;
	struct progress_rec *set = (struct progress_rec*) wset;
	
	if(cur->progress.anim_timer) {
		XtRemoveTimeOut(cur->progress.anim_timer);
		cur->progress.anim_timer = None;
	}
		
	
	if( (cur->primitive.foreground != set->primitive.foreground) ||
		(cur->core.background_pixel != set->core.background_pixel) ) {

		XtReleaseGC(wcur, set->progress.fg_gc);
		XtReleaseGC(wcur, set->progress.bg_gc);
		XtReleaseGC(wcur, set->progress.abg_gc);
		
		init_gcs(wset);
	}
	if((set->progress.value < (-1)) || (set->progress.value > 100)) {
		WARNING(wset, "Invalid progress value\n");
		set->progress.value = cur->progress.value;
	}
	
	if(cur->progress.text_rt != set->progress.text_rt) {
		int height, ascent, descent;

		XmRenderTableGetDefaultFontExtents(
			cur->progress.text_rt, &height, &ascent, &descent);
		cur->progress.hpadding = height / 4;
		cur->progress.vpadding = height / 3;
		cur->progress.font_height = height;
	}

	if(set->progress.value != cur->progress.value) {
		if(cur->progress.xm_text) {
			XmStringFree(cur->progress.xm_text);
			cur->progress.xm_text = NULL;
		}
		if(set->progress.value == PROG_INTER) {
			cur->progress.anim_timer = XtAppAddTimeOut(
				XtWidgetToApplicationContext(wcur),
				ANIM_FTIME, anim_timeout_cb, (XtPointer)wcur);
		}
	}

	return (XtIsRealized(wset) ? True : False);
}

static void class_initialize(void)
{
	/* nothing to do here yet */
}

static void destroy(Widget w)
{
	struct progress_part *p = PROG_PART(w);
	
	if(p->anim_timer) {
		XtRemoveTimeOut(p->anim_timer);
		p->anim_timer = None;
	}

	XtReleaseGC(w, p->fg_gc);
	XtReleaseGC(w, p->bg_gc);
	XtReleaseGC(w, p->abg_gc);
	XFreePixmap(XtDisplay(w), p->stipple);
	
	if(p->xm_text) XmStringFree(p->xm_text);
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

static void default_select_color(Widget w, int offset, XrmValue *pv)
{
	XmeGetDefaultPixel(w, XmSELECT, offset, pv);
}


static void anim_timeout_cb(XtPointer data, XtIntervalId *iid)
{
	Widget w = (Widget)data;
	struct progress_part *p = PROG_PART(w);
	XtAppContext ctx = XtWidgetToApplicationContext(w);

	if(p->value == PROG_INTER) {
		p->anim_frame++;

		if(p->anim_frame >= ANIM_FRAMES)
			p->anim_frame = 0;

		XSetTSOrigin(XtDisplay(w), p->abg_gc, 0, p->anim_frame);
		
		redraw(w);

		p->anim_timer = XtAppAddTimeOut(
			ctx, ANIM_FTIME, anim_timeout_cb, data);
	}
}

static void map_event_cb(Widget w, XtPointer ptr, XEvent *evt, Boolean *cont)
{
	struct progress_part *p = PROG_PART(w);
	XtAppContext ctx = XtWidgetToApplicationContext(w);
	
	if(evt->type == UnmapNotify) {
		if(p->anim_timer) {
			XtRemoveTimeOut(p->anim_timer);
			p->anim_timer = None;
		}		
		
		p->is_mapped = False;
	} else if(evt->type == MapNotify) {
		p->is_mapped = True;
		
		if(p->value == PROG_INTER) {
			p->anim_timer = XtAppAddTimeOut(
				ctx, ANIM_FTIME, anim_timeout_cb, (XtPointer)w);
		}
	}

	*cont = True;
}

/* Public routines */
void ProgressSetValue(Widget w, int value)
{
	struct progress_part *p = PROG_PART(w);
	XtAppContext ctx = XtWidgetToApplicationContext(w);
	
	if((value < (-1)) || (value > 100)) {
		WARNING(w, "Invalid progress value\n");
		return;
	}
	if(p->xm_text) {
		XmStringFree(p->xm_text);
		p->xm_text = NULL;
	}
	p->value = value;
	
	if(p->is_mapped) {
		if(value == PROG_INTER) {
			p->anim_timer = XtAppAddTimeOut(
				ctx, ANIM_FTIME, anim_timeout_cb, (XtPointer)w);
		} else if(p->anim_timer) {
			XtRemoveTimeOut(p->anim_timer);
			p->anim_timer = None;
		}
		redraw(w);
	}
}

