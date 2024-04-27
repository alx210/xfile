/*
 * Copyright (C) 2022-2024 alx@fastestcode.org
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
#include <Xm/DrawnB.h>
#include <Xm/XmP.h>
#include <Xm/DrawP.h>
#include <Xm/ManagerP.h>
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
static void input_activate_cb(Widget, XtPointer, XtPointer);
static void input_focus_cb(Widget, XtPointer, XtPointer);
static void input_unfocus_cb(Widget, XtPointer, XtPointer);
static void set_path_from_component(Widget, unsigned int);
static Boolean notify_client(Widget, const char*);
static void update_location_data(Widget, const char*);

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
		XmNmarginWidth,
		XmCMarginWidth,
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

static void input_activate_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	Widget wparent = XtParent(w);
	struct path_field_part *wp = PART(wparent);
	char *new_path = XmTextFieldGetString(w);

	if(notify_client(wparent, new_path)) {
		path_field_set_location(wparent, new_path, False);
		XtFree(new_path);	
	} else if(wp->tmp_path) {
		XmTextFieldSetString(w, wp->tmp_path);
	}
	
	wp->editing = False;
	
	/* assuming the next widget is whatever this path box sets path for
	 * (the file list widget, most likely) */
	_XmMgrTraversal(w, XmTRAVERSE_GLOBALLY_FORWARD);
		
}


static void input_focus_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	Arg args[1];
	struct path_field_part *wp = PART(pclient);
	Cardinal i;
	char *cur_path = XmTextFieldGetString(w);

	for(i = 0; i < wp->ncomp; i++) {
		/* avoid armed buttons getting stuck in armed state when
		 * remapped, by making them insensitive first */
		XtSetSensitive(wp->wcomp[i], False);
		XtUnmapWidget(wp->wcomp[i]);
	}

	wp->editing = True;

	draw_outline((Widget)pclient);

	XtSetArg(args[0], XmNcursorPositionVisible, True);
	XtSetValues(w, args, 1);
	XmTextFieldSetInsertionPosition(w, strlen(cur_path));
	XtFree(cur_path);
}

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

	for(i = 0; i < wp->ncomp; i++) {
		XtMapWidget(wp->wcomp[i]);
		XtSetSensitive(wp->wcomp[i], True);
	}
	
	draw_outline((Widget)pclient);
	
	XmTextFieldSetInsertionPosition(w, 0);
	XtSetArg(args[0], XmNcursorPositionVisible, False);
	XtSetValues(w, args, 1);
}


static Dimension compute_height(Widget w)
{
	struct path_field_part *wp = PART(w);
	Dimension wanted_height;

	wanted_height = wp->font_height + 2 + wp->btn_height +
		wp->margin_height * 2  + wp->shadow_thickness * 2 +
		wp->highlight_thickness * 2;
	return wanted_height;
}

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
	XtCallbackRec input_activate_cbr[] =
		{ {input_activate_cb, (XtPointer)wnew }, { NULL, NULL } };
	XtCallbackRec input_focus_cbr[] =
		{ {input_focus_cb, (XtPointer)wnew }, { NULL, NULL } };
	XtCallbackRec input_unfocus_cbr[] =
		{ {input_unfocus_cb, (XtPointer)wnew }, { NULL, NULL } };
	
	
	wp->editing = False;
	wp->tmp_path = NULL;
	wp->ncomp = 0;
	wp->ncomp_max = 0;
	wp->wcomp = NULL;
	wp->comp_ids = NULL;
	wp->home = getenv("HOME");
	if(wp->home) wp->real_home = realpath(wp->home, NULL);
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
	
	XtSetArg(args[0], XmNhighlightThickness, &wp->highlight_thickness);
	XtGetValues(wp->winput, args, 1);

	XtResizeWidget(wnew, 50, compute_height(wnew), 0);	
	XtManageChild(wp->winput);
}

static void resize(Widget w)
{
	struct path_field_part *wp = PART(w);
	Position cx, cy;
	Dimension cw, ch;

	compute_client_area(w, &cx, &cy, &cw, &ch);
	XtConfigureWidget(wp->winput, cx, cy + wp->btn_height,
		cw, ch - wp->btn_height, 0);
}

static XtGeometryResult query_geometry(Widget w,
	XtWidgetGeometry *ig, XtWidgetGeometry *pg)
{
	pg->request_mode = CWHeight;
	pg->height = compute_height(w);
	return XtGeometryAlmost;
}

static Boolean set_values(Widget wcur, Widget wreq,
	Widget wset, ArgList args, Cardinal *nargs)
{
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

	wp->ncomp = 0;
	wp->ncomp_max = 0;
	wp->wcomp = NULL;
	wp->comp_ids = NULL;
}

/*
 * Updates component button data and their positions.
 */
static void update_location_data(Widget w, const char *path)
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

	while(p[i]) {
		size_t blen = 0;
		size_t clen = 0;
		char *s = p + i;
		
		while(p[i] && p[i] != '/') {
			/* mind multibyte; we need position in characters for XmTextField */
			int bytes = mblen(p + i, path_len - i);
			if(bytes == -1) {
				/* shouldn't happen, but if, we disable them buttons */
				WARNING(w, "Invalid character string");
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
	draw_outline(w);
	
	if(wp->tmp_path)
		XtFree(wp->tmp_path);

	wp->tmp_path = strdup(path);
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
	XmTextFieldSetString(wp->winput, path);

	/* remove discarded data */
	for(i = id; i < wp->ncomp; i++) {
		XtUnmanageChild(wp->wcomp[i]);
		XtFree(wp->sz_comp[i]);
	}
	wp->ncomp = id;
	draw_outline(w);

	if(!notify_client(w, path)) {
		XmTextFieldSetString(wp->winput, prev_path);
		update_location_data(w, prev_path);
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
 * Public routines
 */
int path_field_set_location(Widget w, const char *location, Boolean notify)
{
	struct path_field_part *wp = PART(w);
	char *subst_path = NULL;
	
	/* Make sure we're not heading into an infinite callback loop */
	if(wp->processing_callbacks && notify) {
		WARNING(w, "Can't set location from a notify callback!");
		return EINVAL;
	}

	/* replace $HOME part with a ~ if requested */
	if(wp->compact_path) {
		if(!strncmp(location, wp->home, strlen(wp->home)) )
			subst_path = wp->home;
		else if (!strncmp(location, wp->real_home, strlen(wp->real_home)) )
			subst_path = wp->real_home;
	}
	
	if(subst_path) {
		char *path, *p;

		path = XtMalloc(strlen(location));
		p = (char*) location + strlen(subst_path);
		sprintf(path, "~%s", p);
		
		if(notify && !notify_client(w, path)) {
			XtFree(path);
			return EINVAL;
		}
		XmTextFieldSetString(wp->winput, path);
		update_location_data(w, path);
		XtFree(path);
	} else {
		if(notify && !notify_client(w, location)) return EINVAL;
		XmTextFieldSetString(wp->winput, (char*)location);
		update_location_data(w, location);
	}
	return 0;
}

char* path_field_get_location(Widget w)
{
	struct path_field_part *wp = PART(w);
	char *path = XmTextFieldGetString(w);
	char *ret_path = path;

	if(wp->compact_path && (path[0] == '~') ) {
		size_t len = strlen(wp->home) + strlen(path) + 1;
		/* replace ~ with $HOME */
		ret_path = XtMalloc(len + 1);
		sprintf(ret_path, "%s/%s", wp->home, &path[1]);
		XtFree(path);
	}
	
	return ret_path;
}
