/*
 * Copyright (C) 2023-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <Xm/XmP.h>
#include <Xm/RepType.h>
#include <Xm/RowColumn.h>
#ifndef NO_XKB
#include <X11/XKBlib.h>
#endif
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fnmatch.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <wchar.h>
#include <wctype.h>
#include "listwp.h"
#include "mbstr.h"
#include "debug.h"

/* Local routines */
static void class_initialize(void);
static void initialize(Widget, Widget, ArgList, Cardinal*);
static void init_gcs(Widget w);
static void destroy(Widget);
static void realize(Widget, XtValueMask*, XSetWindowAttributes*);
static void expose(Widget, XEvent*, Region);
static void redraw_area(Widget, short, short,
	unsigned short, unsigned short, Boolean);
static void get_visible_range(Widget, unsigned int*, unsigned int*);
static void redraw_all(Widget);
static Boolean widget_display_rect(Widget, XRectangle*);
static void resize(Widget);
static Boolean set_values(Widget, Widget, Widget, ArgList, Cardinal*);
static XtGeometryResult query_geometry(Widget,
	XtWidgetGeometry*, XtWidgetGeometry*);
static void default_render_table(Widget, int, XrmValue*);
static void default_hspacing(Widget, int, XrmValue*);
static void default_select_color(Widget, int, XrmValue*);
static void free_item(struct item_rec*);
static Boolean find_item(const struct file_list_part *fl,
	const char *name, unsigned int *pindex);
static void draw_item(Widget, unsigned int index, Boolean);
static void draw_rubber_bands(Widget);
static void get_selection_rect(Widget, struct rectangle*);
static Boolean make_labels(Widget, struct item_rec*);
static void compute_item_extents(Widget, unsigned int);
static void sort_list(Widget);
static int compare_names(const char*, const char*);
static int sort_by_name(const void*, const void*);
static int sort_by_name_des(const void*, const void*);
static int sort_by_time(const void*, const void*);
static int sort_by_time_des(const void*, const void*);
static int sort_by_type(const void*, const void*);
static int sort_by_type_des(const void*, const void*);
static int sort_by_suffix(const void*, const void*);
static int sort_by_suffix_des(const void*, const void*);
static int sort_by_size(const void*, const void*);
static int sort_by_size_des(const void*, const void*);
static void compute_placement(Widget, Dimension, Dimension);
static void update_sbar_range(Widget, Dimension, Dimension);
static void update_sbar_visibility(Widget, Dimension, Dimension);
static void hscroll_cb(Widget, XtPointer, XtPointer);
static void vscroll_cb(Widget, XtPointer, XtPointer);
static void get_view_dimensions(Widget, Boolean, Dimension*, Dimension*);
static Boolean get_at_xy(Widget, int, int, unsigned int*);
static Boolean hit_test(Widget w, int x, int y,
	unsigned int, Boolean*);
static void select_within_rect(Widget, const struct rectangle*, Boolean);
static void select_at_xy(Widget, int, int, Boolean);
static void select_range(Widget, unsigned int, unsigned int);
static void select_at_cursor(Widget, Boolean, Boolean);
static void set_cursor(Widget, unsigned int);
static unsigned int get_cursor(Widget);
static void default_action_handler(Widget, unsigned int);
static void sel_change_handler(Widget, Boolean);
static void dblclk_timeout_cb(XtPointer, XtIntervalId*);
static void autoscrl_timeout_cb(XtPointer, XtIntervalId*);
static void activate(Widget, XEvent*, String*, Cardinal*);
static void select_current(Widget, XEvent*, String*, Cardinal*);
static void primary_button(Widget, XEvent*, String*, Cardinal*);
static void button_motion(Widget, XEvent*, String*, Cardinal*);
static void move_cursor(Widget, XEvent*, String*, Cardinal*);
static void lookup_input(Widget, XEvent*, String*, Cardinal*);
static void reset_lookup(Widget, XEvent*, String*, Cardinal*);
static void focus_in(Widget, XEvent*, String*, Cardinal*);
static void focus_out(Widget, XEvent*, String*, Cardinal*);
static void scroll(Widget, XEvent*, String*, Cardinal*);
static void pg_scroll(Widget, XEvent*, String*, Cardinal*);
static void secondary_button(Widget, XEvent*, String*, Cardinal*);
static void dir_up(Widget, XEvent*, String*, Cardinal*);
static void delete(Widget, XEvent*, String*, Cardinal*);

#define WARNING(w,s) XtAppWarning(XtWidgetToApplicationContext(w), s)

/* Widget resources */
#define RFO(fld) XtOffsetOf(struct file_list_rec, fld)
static XtResource resources[] = {
	{
		XmNrenderTable,
		XmCRenderTable,
		XmRRenderTable,
		sizeof(XmRenderTable),
		RFO(file_list.label_rt),
		XtRCallProc,
		(XtPointer)default_render_table
	},
	{
		XfNactivateCallback,
		XfCActivateCallback,
		XmRCallback,
		sizeof(XtPointer),
		RFO(file_list.default_action_cb),
		XmRCallback,
		NULL	
	},
	{
		XfNselectionChangeCallback,
		XfCSelectionChangeCallback,
		XmRCallback,
		sizeof(XtPointer),
		RFO(file_list.sel_change_cb),
		XmRCallback,
		NULL	
	},
	{
		XfNdirectoryUpCallback,
		XfCDirectoryUpCallback,
		XmRCallback,
		sizeof(XtPointer),
		RFO(file_list.dir_up_cb),
		XmRCallback,
		NULL	
	},
	{
		XfNdeleteCallback,
		XfCDeleteCallback,
		XmRCallback,
		sizeof(XtPointer),
		RFO(file_list.delete_cb),
		XmRCallback,
		NULL	
	},
	{
		XfNdoubleClickInterval,
		XfCDoubleClickInterval,
		XtRShort,
		sizeof(short),
		RFO(file_list.dblclk_int),
		XtRImmediate,
		(void*) -1	
	},
	{
		XfNsortOrder,
		XfCSortOrder,
		XfRSortOrder,
		sizeof(short),
		RFO(file_list.sort_order),
		XtRImmediate,
		(void*)XfNAME
	},
	{
		XfNsortDirection,
		XfCSortDirection,
		XfRSortDirection,
		sizeof(short),
		RFO(file_list.sort_direction),
		XtRImmediate,
		(void*)XfDESCEND
	},
	{
		XfNviewMode,
		XfCViewMode,
		XfRViewMode,
		sizeof(short),
		RFO(file_list.view_mode),
		XtRImmediate,
		(void*)XfCOMPACT
	},
	{
		XfNverticalScrollBar,
		XfCVerticalScrollBar,
		XtRWidget,
		sizeof(Widget),
		RFO(file_list.wvscrl),
		XtRImmediate,
		(void*)None
	},
	{
		XfNhorizontalScrollBar,
		XfCHorizontalScrollBar,
		XtRWidget,
		sizeof(Widget),
		RFO(file_list.whscrl),
		XtRImmediate,
		(void*)None
	},
	{
		XfNmarginWidth,
		XfCMarginWidth,
		XtRDimension,
		sizeof(Dimension),
		RFO(file_list.margin_w),
		XtRImmediate,
		(void*)DEF_MARGIN
	},
	{
		XfNmarginHeight,
		XfCMarginHeight,
		XtRDimension,
		sizeof(Dimension),
		RFO(file_list.margin_h),
		XtRImmediate,
		(void*)DEF_MARGIN
	},
	{
		XfNverticalSpacing,
		XfCVerticalSpacing,
		XtRDimension,
		sizeof(Dimension),
		RFO(file_list.vert_spacing),
		XtRImmediate,
		(void*)DEF_SPACING
	},
	{
		XfNhorizontalSpacing,
		XfCHorizontalSpacing,
		XtRDimension,
		sizeof(Dimension),
		RFO(file_list.horz_spacing),
		XtRCallProc,
		(void*)default_hspacing
	},
	{
		XfNlabelMargin,
		XfCLabelMargin,
		XtRDimension,
		sizeof(Dimension),
		RFO(file_list.label_margin),
		XtRImmediate,
		(void*)DEF_MARGIN
	},
	{
		XfNlabelSpacing,
		XfCLabelSpacing,
		XtRDimension,
		sizeof(Dimension),
		RFO(file_list.label_spacing),
		XtRImmediate,
		(void*)0
	},
	{
		XfNoutlineWidth,
		XfCOutlineWidth,
		XtRDimension,
		sizeof(Dimension),
		RFO(file_list.outline_width),
		XtRImmediate,
		(void*)DEF_OUTLINE
	},
	{
		XfNdragOffset,
		XfCDragOffset,
		XtRDimension,
		sizeof(Dimension),
		RFO(file_list.drag_offset),
		XtRImmediate,
		(void*)DEF_DRAG_OFFSET
	},
	{
		XfNautoScrollSpeed,
		XfCAutoScrollSpeed,
		XtRShort,
		sizeof(short),
		RFO(file_list.scrl_factor),
		XtRImmediate,
		(void*)DEF_SCRL_FACTOR
	},
	{
		XfNselectColor,
		XfCSelectColor,
		XtRPixel,
		sizeof(Pixel),
		RFO(file_list.select_pixel),
		XtRCallProc,
		(void*)default_select_color
	},
	{
		XfNshortenLabels,
		XfCShortenLabels,
		XtRShort,
		sizeof(unsigned short),
		RFO(file_list.shorten),
		XtRImmediate,
		(void*)0
	},
	{
		XfNsilent,
		XfCSilent,
		XtRBoolean,
		sizeof(Boolean),
		RFO(file_list.silent),
		XtRImmediate,
		(void*)False
	},
	{
		XfNlookupTimeOut,
		XfCLookupTimeOut,
		XtRShort,
		sizeof(unsigned short),
		RFO(file_list.lookup_time),
		XtRImmediate,
		(void*)DEF_LOOKUP_TIMEOUT
	},
	{
		XfNnumberedSort,
		XfCNumberedSort,
		XtRBoolean,
		sizeof(Boolean),
		RFO(file_list.numbered_sort),
		XtRImmediate,
		(void*)True
	},
	{
		XfNcaseSensitive,
		XfCCaseSensitive,
		XtRBoolean,
		sizeof(Boolean),
		RFO(file_list.case_sensitive),
		XtRImmediate,
		(void*)False
	}
};
#undef RFO

static char translations[] = {
	"Shift<Btn1Down>: PrimaryButton(Down, Extend) \n"
	"Shift<Btn1Up>: PrimaryButton(Up, Extend) \n"
	"Ctrl<Btn1Down>: PrimaryButton(Down, Add) \n"
	"Ctrl<Btn1Up>: PrimaryButton(Up, Add) \n"
	
	"Ctrl<Key>space: Select(Add) \n"

	"Shift<Key>osfLeft: MoveCursor(Left, Extend) \n"
	"Shift<Key>osfRight: MoveCursor(Right, Extend) \n"
	"Shift<Key>osfUp: MoveCursor(Up, Extend) \n"
	"Shift<Key>osfDown: MoveCursor(Down, Extend) \n"
	"Shift<Key>osfBeginLine: MoveCursor(Begin, Extend) \n"
	"Shift<Key>osfEndLine: MoveCursor(End, Extend) \n"
	
	"<Btn1Down>: PrimaryButton(Down) \n"
	"<Btn1Up>: PrimaryButton(Up) \n"
	"<Btn1Motion>: PrimaryButtonMotion() \n"
	"<Btn3Down>: SecondaryButton(Down) \n"

	"<Key>Return: Select() Activate() \n"	
	"<Key>osfActivate: Select() Activate() \n"
	"<Key>space: Select(Toggle) \n"
	"<Key>osfLeft: MoveCursor(Left) \n"
	"<Key>osfRight: MoveCursor(Right) \n"
	"<Key>osfUp: MoveCursor(Up) \n"
	"<Key>osfDown: MoveCursor(Down) \n"
	"<Key>osfBeginLine: MoveCursor(Begin) \n"
	"<Key>osfEndLine: MoveCursor(End) \n"
	
	"Shift<Key>osfPageUp: PageScroll(Left) \n"
	"Shift<Key>osfPageDown: PageScroll(Right) \n"
	"<Key>osfPageUp: PageScroll(Up) \n"
	"<Key>osfPageDown: PageScroll(Down) \n"
	"<Btn4Down>: Scroll(Up) \n"
	"<Btn5Down>: Scroll(Down) \n"
	"Shift<Btn4Down>: PageScroll(Up) \n"
	"Shift<Btn5Down>: PageScroll(Down) \n"
	"<Key>osfBackSpace: DirectoryUp() \n"
	"<Key>osfDelete: Delete() \n"

	"s ~m ~a <Key>Tab: PreviousTabGroup() \n"
	"~m ~a <Key>Tab: NextTabGroup() \n"
	
	"<Key>osfCancel: ResetLookUp() \n"
	"<KeyDown>: LookUpInput() \n"
	
	"<FocusIn>: FocusIn() \n"
	"<FocusOut>: FocusOut()"
};

static XtActionsRec actions[] = {
	{ "Activate", activate },
	{ "Select", select_current },
	{ "PrimaryButton", primary_button },
	{ "PrimaryButtonMotion", button_motion },
	{ "MoveCursor", move_cursor },
	{ "Scroll", scroll },
	{ "PageScroll", pg_scroll },
	{ "DirectoryUp", dir_up },
	{ "Delete", delete },
	{ "LookUpInput", lookup_input },
	{ "ResetLookUp", reset_lookup },
	{ "FocusIn", focus_in },
	{ "FocusOut", focus_out },
	{ "SecondaryButton", secondary_button },
	{ "NextTabGroup", _XmTraverseNextTabGroup },
	{ "PreviousTabGroup", _XmTraversePrevTabGroup }
};

static char *sort_dir_names[] = {
	"ascend", "descend"
};

static char *sort_by_names[] = {
	"name", "time", "suffix", "type", "size"
};

static char *view_mode_names[] = {
	"compact", "detailed"
};

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

static struct file_list_class_rec fileListWidgetClassRec = {
	.core.superclass = (WidgetClass)&xmPrimitiveClassRec,
	.core.class_name = "FileList",
	.core.widget_size = sizeof(struct file_list_rec),
	.core.class_initialize = class_initialize,
	.core.class_part_initialize = NULL,
	.core.class_inited = False,
	.core.initialize = initialize,
	.core.initialize_hook = NULL,
	.core.realize = realize,
	.core.actions = actions,
	.core.num_actions = XtNumber(actions),
	.core.resources = resources,
	.core.num_resources = XtNumber(resources),
	.core.xrm_class = NULLQUARK,
	.core.compress_motion = True,
	.core.compress_exposure = True,
	.core.compress_enterleave = True,
	.core.visible_interest = True,
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
	.core.tm_table = translations,
	.core.query_geometry = query_geometry,
	.core.display_accelerator = NULL,
	.core.extension = NULL,

	.primitive.border_highlight = NULL,
	.primitive.border_unhighlight = NULL,
	.primitive.translations = NULL,
	.primitive.arm_and_activate = NULL,
	.primitive.syn_resources = NULL,
	.primitive.num_syn_resources = 0,
	.primitive.extension = (XtPointer) &primClassExtRec
};

WidgetClass fileListWidgetClass = (WidgetClass) &fileListWidgetClassRec;

#define FL_PART(w) &((struct file_list_rec*)w)->file_list
#define FL_REC(w) (struct file_list_rec*)w
#define CORE_WIDTH(w) (((struct file_list_rec*)w)->core.width)
#define CORE_HEIGHT(w) (((struct file_list_rec*)w)->core.height)

/* Though most unixen have qsort_r now, there are discrepancies */
static int (*qsort_strcmp_fp)(const char*, const char*) = strcmp;

/*
 * Draws an item. If erase is True, clears the area first.
 */
static void draw_item(Widget w, unsigned int index, Boolean erase)
{
	struct file_list_part *fl = FL_PART(w);
	struct item_rec *r = &fl->items[index];
	Display *dpy = XtDisplay(w);
	Window wnd = XtWindow(w);
	int x = r->x - fl->xoff;
	int y = r->y - fl->yoff;
	Dimension item_width =  ((fl->view_mode == XfCOMPACT) ?
		r->width : fl->item_width_max[XfDETAILED]);
	Position lx = x + fl->icon_width_max + fl->label_margin;
	Position ly = y + r->text_yoff;
	Dimension lw = item_width - (fl->icon_width_max + fl->label_margin);
	Dimension lh = fl->item_height_max - r->text_yoff;
	
	if((x + item_width < 0) || (y + fl->item_height_max < 0)) return;

	if(erase) {
		XClearArea(dpy, wnd, x, y,
		(fl->view_mode == XfCOMPACT) ?
		(r->width + 1) : (fl->item_width_max[XfDETAILED] + 1),
		fl->item_height_max + 1, False);
	}
	
	if(r->icon_image) {
		XSetClipOrigin(dpy, fl->icon_gc, x, y);
		XSetClipMask(dpy, fl->icon_gc, r->icon_mask);
		XCopyArea(dpy, r->icon_image, wnd, fl->icon_gc, 0, 0,
			r->icon_width, r->icon_height, x, y);
		
		if(r->selected) {
			XSetTSOrigin(dpy, fl->icon_gc, x, y);
			XFillRectangle(dpy, wnd, fl->icon_gc, x, y,
				r->icon_width, r->icon_height);
		}
	}
	
	if(r->selected) {
		XFillRectangle(dpy, wnd,
			(fl->highlight_sel ? fl->sbg_gc : fl->nfbg_gc),
			lx, ly, lw + 1, lh + 1);
	}
	
	XSetForeground(dpy, fl->label_gc,
		(r->selected) ? fl->sfg_pixel : fl->fg_pixel);
	
	if(fl->view_mode == XfCOMPACT) {
		XmStringDraw(dpy, wnd, fl->label_rt, r->label[FL_FLABEL],
			fl->label_gc, lx, ly, lw, XmALIGNMENT_BEGINNING,
			XmSTRING_DIRECTION_DEFAULT, NULL);
	} else {
		int i;
		Position clx = lx;
		
		for(i = 0; i < NFIELDS; i++) {
			XmStringDraw(dpy, wnd, fl->label_rt, r->label[i],
				fl->label_gc, clx, ly, fl->field_widths[i],
				XmALIGNMENT_BEGINNING,
				XmSTRING_DIRECTION_DEFAULT, NULL);
			clx += fl->field_widths[i] + fl->label_spacing;
		}
	}
	
	if(index == fl->cursor && fl->has_focus)
		XDrawRectangle(dpy, wnd, fl->xor_gc, lx, ly, lw, lh);
}

/*
 * Draws inverted rectangle using sel_rect coordinates
 */
static void draw_rubber_bands(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	struct rectangle rc;
	
	if(!fl->sel_rect.width && !fl->sel_rect.height) return;
	
	get_selection_rect(w, &rc);

	XDrawRectangle(XtDisplay(w), XtWindow(w),
		fl->xor_gc, rc.x, rc.y, rc.width, rc.height);	
}

/*
 * Returns current selection rectangle (rubber-bands), making
 * sure it extends towards/into the positive half-plane only
 */
static void get_selection_rect(Widget w,  struct rectangle *rc)
{
	struct file_list_part *fl = FL_PART(w);
	
	if(fl->sel_rect.width > 0) {
		rc->x = fl->sel_rect.x;
		rc->width = fl->sel_rect.width;
	} else {
		rc->x = fl->sel_rect.x + fl->sel_rect.width;
		rc->width = abs(fl->sel_rect.width);
	}
	
	if(fl->sel_rect.height > 0) {
		rc->y = fl->sel_rect.y;
		rc->height = fl->sel_rect.height;
	} else {
		rc->y = fl->sel_rect.y + fl->sel_rect.height;
		rc->height = abs(fl->sel_rect.height);
	}
}

/*
 * Redraw a rectangular area, clearing it if requested,
 * without generating X exposures.
 */
static void redraw_area(Widget w, short x, short y,
	unsigned short width, unsigned short height, Boolean clear)
{
	struct file_list_part *fl = FL_PART(w);
	Region reg;
	XRectangle rect = { x, y, width, height };
	XEvent tmp = {0};
	
	if(!fl->show_contents) {
		XClearArea(XtDisplay(w), XtWindow(w), x, y, width, height, False);
		return;
	}

	if(clear) XClearArea(XtDisplay(w), XtWindow(w), x, y, width, height, False);
	reg = XCreateRegion();
	XUnionRectWithRegion(&rect, reg, reg);
	expose(w, &tmp, reg);
	XDestroyRegion(reg);
}

static void redraw_all(Widget w)
{
	redraw_area(w, 0, 0, CORE_WIDTH(w), CORE_HEIGHT(w), True);
}

/*
 * Quick sorts the list
 */
static void sort_list(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	Boolean asc = (fl->sort_direction == XfASCEND) ? True : False;

	if(fl->num_items < 2) return;
	
	qsort_strcmp_fp = (fl->numbered_sort) ? compare_names : strcmp;

	switch(fl->sort_order) {
		case XfNAME:
		qsort(fl->items, fl->num_items,
			sizeof(struct item_rec),
			asc ? sort_by_name : sort_by_name_des);
		break;
		
		case XfTIME:
		qsort(fl->items, fl->num_items,
			sizeof(struct item_rec),
			asc ? sort_by_time : sort_by_time_des);
		break;
		
		case XfSUFFIX:
		qsort(fl->items, fl->num_items,
			sizeof(struct item_rec),
			asc ? sort_by_suffix : sort_by_suffix_des);
		break;
		
		case XfTYPE:
		qsort(fl->items, fl->num_items,
			sizeof(struct item_rec),
			asc ? sort_by_type : sort_by_type_des);
		break;

		case XfSIZE:
		qsort(fl->items, fl->num_items,
			sizeof(struct item_rec),
			asc ? sort_by_size : sort_by_size_des);
		break;
	};
}

/*
 * This is like strcmp, except it will compare
 * coinciding strings of digits numerically
 */
static int compare_names(const char *a, const char *b)
{
	int res;
	
	for( ; *a && *b; a++, b++) {
		if(isdigit(*a) && isdigit(*b)) {
			const char *sp_a = a;
			const char *sp_b = b;
			unsigned int ai = 0;
			unsigned int bi = 0;
			unsigned int fa = 1;
			const char *p;

			while(*sp_a && isdigit(*sp_a)) sp_a++;
			while(*sp_b && isdigit(*sp_b)) sp_b++;

			p = sp_a;

			do {
				p--;
				ai += (*p - '0') * fa;
				fa *= 10;
			} while(p != a);

			p = sp_b;
			fa = 1;

			do {
				p--;
				bi += (*p - '0') * fa;
				fa *= 10;
			} while(p != b);
			
			a = sp_a;
			b = sp_b;
			
			res = (ai - bi);
			if(res) return res;
		} else {
			res = *a - *b;
			if(res) return res;
		}
	}
	
	if(*a) res = 1; else if(*b) res = -1; else res = 0;
	
	return res;
}

/*
 * Different flavors of quick sort compare functions
 */
static int sort_by_name_des(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);
	return r ? r : qsort_strcmp_fp(a->tr_name, b->tr_name);
}

static int sort_by_name(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);
	return r ? r : qsort_strcmp_fp(b->tr_name, a->tr_name);
}

static int sort_by_time_des(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);
	if(!r) r = (b->mtime - a->mtime);
	if(!r) r = sort_by_name_des(a, b);
	
	return r;
}

static int sort_by_time(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);
	if(!r) r = (a->mtime - b->mtime);
	if(!r) r = sort_by_name(a, b);
	
	return r;
}

static int sort_by_type_des(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);
	if(!r) r = (a->db_type - b->db_type);
	if(!r) r = sort_by_suffix_des(a, b);

	return r;
}

static int sort_by_type(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);
	if(!r) r = (b->db_type - a->db_type);
	if(!r) r = sort_by_suffix(a, b);

	return r;
}


static int sort_by_suffix_des(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);

	if(!r) {
		char *sa = strrchr(a->tr_name, '.');
		char *sb = strrchr(b->tr_name, '.');
		if(sa && sb) {
			r = qsort_strcmp_fp(sa + 1, sb + 1);
		} else if(sa || sb) {
			r = (sa) ? 1 : -1;
		} else r = 0;
	}

	return r ? r : sort_by_name_des(a, b);
}

static int sort_by_suffix(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);

	if(!r) {
		char *sa = strrchr(a->tr_name, '.');
		char *sb = strrchr(b->tr_name, '.');
		if(sa && sb) {
			r = qsort_strcmp_fp(sa + 1, sb + 1);
		} else if(sa || sb) {
			r = (sa) ? -1 : 1;
		} else r = 0;
	}

	return r ? r : sort_by_name(a, b);
}

static int sort_by_size_des(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);
	if(!r) r = (b->size - a->size);
	if(!r) r = sort_by_name_des(b, a);

	return r;
}

static int sort_by_size(const void *aptr, const void *bptr)
{
	int r;
	const struct item_rec *a = (struct item_rec*)aptr;
	const struct item_rec *b = (struct item_rec*)bptr;
	r = S_ISDIR(b->mode) - S_ISDIR(a->mode);
	if(!r) r = (a->size - b->size);
	if(!r) r = sort_by_name(a, b);

	return r;
}

/*
 * Frees all fields in an item_rec struct
 */
static void free_item(struct item_rec *in)
{
	int i;
	
	free(in->name);
	if(in->tr_name && (in->tr_name != in->name)) free(in->tr_name);
	if(in->title) free(in->title);
	
	for(i = 0; i < NFIELDS; i++)
		XmStringFree(in->label[i]);
}

/*
 * Retrieves item index from name. Returns True if found.
 */
static Boolean find_item(const struct file_list_part *fl,
	const char *name, unsigned int *pindex)
{
	unsigned int i;
	
	for(i = 0; i < fl->num_items; i++) {
		if(!strcmp(fl->items[i].name, name)) {
			*pindex = i;
			return True;
		}
	}
	return False;
}

/*
 * Computes dimensions (width, height and lalbel offset) for the given item
 */
static void compute_item_extents(Widget w, unsigned int index)
{
	Dimension text_w = 0, text_h = 0, height;
	struct file_list_part *fl = FL_PART(w);
	struct item_rec *in = &fl->items[index];
	
	XmStringExtent(fl->label_rt, in->label[FL_FLABEL], &text_w, &text_h);
	in->width = fl->icon_width_max + text_w + fl->label_margin;
	
	if(fl->text_height_max < text_h) fl->text_height_max = text_h;
	
	height = ((fl->icon_height_max > fl->text_height_max)
		? fl->icon_height_max : fl->text_height_max);

	in->text_yoff = height - text_h;
	
	if(fl->item_width_max[XfCOMPACT] < in->width)
		fl->item_width_max[XfCOMPACT] = in->width;
	
	if(height > fl->item_height_max) fl->item_height_max = height;
}

/*
 * Computes item placement information
 */
static void compute_placement(Widget w,
	Dimension view_width, Dimension view_height)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int width_max = fl->item_width_max[fl->view_mode];
	unsigned int cx = fl->margin_w;
	unsigned int cy = fl->margin_h;
	unsigned int cols_max = 1;
	unsigned int i = 0, icol = 0;

	/* make sure we're not in drag-select mode */
	if(fl->dragging) {
		fl->dragging = False;
		redraw_all(w);
	}
	
	if(!fl->num_items) {
		fl->list_width = 0;
		fl->list_height = 0;
		update_sbar_range(w, view_width, view_height);
		return;
	}
	
	if(fl->view_mode == XfCOMPACT) {
		if((view_width - fl->margin_w * 2) > 0) {
			cols_max = ((float)view_width - fl->margin_w * 2) /
				(fl->horz_spacing + width_max);
		}
		if(!cols_max) cols_max = 1;
	}
	
	/* Lay out nodes left to right, top to bottom */
	for(i = 0; i < fl->num_items; i++)
	{
		if(icol == cols_max) {
			icol = 0;
			cx = fl->margin_w;
			cy += fl->vert_spacing + fl->item_height_max;
		}
		
		fl->items[i].x = cx;
		fl->items[i].y = cy;

		cx += fl->horz_spacing + width_max;
		icol++;
	}
	
	fl->ncolumns = (fl->num_items < cols_max) ? fl->num_items : cols_max;
	fl->list_width = fl->ncolumns *
		(width_max + fl->horz_spacing) + fl->margin_w * 2;
	fl->list_height = cy + fl->item_height_max + fl->margin_h;
	fl->row_height = fl->item_height_max + fl->horz_spacing;
}

/*
 * Recalculates view offset values and updates scroll-bar sliders
 * according to list dimensions and viewable area
 */
static void update_sbar_range(Widget w,
	Dimension view_width, Dimension view_height)
{
	struct file_list_part *fl = FL_PART(w);
	Arg args[10];

	if(fl->whscrl) {
		int n = 0;
		
		if(fl->list_width > view_width) {
			if(fl->xoff >= (fl->list_width - view_width)) {
				fl->xoff -= (fl->xoff - (fl->list_width - view_width));
			}
		
			XtSetArg(args[n], XmNminimum, 0); n++;
			XtSetArg(args[n], XmNmaximum, fl->list_width); n++;
			XtSetArg(args[n], XmNsliderSize, view_width); n++;
			XtSetArg(args[n], XmNincrement, view_width); n++;
			XtSetArg(args[n], XmNpageIncrement, view_width); n++;
		} else {
			fl->xoff = 0;
			
			XtSetArg(args[n], XmNminimum, 0); n++;
			XtSetArg(args[n], XmNmaximum, 100); n++;
			XtSetArg(args[n], XmNsliderSize, 100); n++;
		}
		XtSetArg(args[n], XmNvalue, (int)fl->xoff); n++;
		XtSetValues(fl->whscrl, args, n);
	}
	
	if(fl->wvscrl) {
		int n = 0;
		
		if(fl->list_height > view_height) {
			if(fl->yoff >= (fl->list_height - view_height)) {
				fl->yoff -= (fl->yoff - (fl->list_height - view_height));
			}
			
			XtSetArg(args[n], XmNminimum, 0); n++;
			XtSetArg(args[n], XmNmaximum, fl->list_height); n++;
			XtSetArg(args[n], XmNsliderSize, view_height); n++;
			XtSetArg(args[n], XmNincrement, fl->row_height); n++;
			XtSetArg(args[n], XmNpageIncrement, view_height); n++;
		} else {
			fl->yoff = 0;
			
			XtSetArg(args[n], XmNminimum, 0); n++;
			XtSetArg(args[n], XmNmaximum, 100); n++;
			XtSetArg(args[n], XmNsliderSize, 100); n++;
		}
		XtSetArg(args[n], XmNvalue, (int)fl->yoff); n++;
		XtSetValues(fl->wvscrl, args, n);
	}
}


/*
 * Checks if scrollbars are needed for given viewport size and
 * manages/unmanages them.
 */
void update_sbar_visibility(Widget w,
	Dimension view_width, Dimension view_height)
{
	struct file_list_part *fl = FL_PART(w);
	Boolean need_hsb = False;
	Boolean need_vsb = False;
	Dimension hsb_size = fl->whscrl ? XtHeight(fl->whscrl) : 0;
	Dimension vsb_size = fl->wvscrl ? XtWidth(fl->wvscrl) : 0;

	fl->in_sb_update = True;	

	if(view_width < vsb_size || view_height < hsb_size) {
		XtUnmanageChild(fl->whscrl);
		XtUnmanageChild(fl->wvscrl);
		fl->in_sb_update = False;
		return;
	}
	
	if(fl->wvscrl && (view_height < fl->list_height))
		view_width -= vsb_size;

	if(fl->whscrl && (view_width < fl->list_width))
		view_height -= hsb_size;

	if(fl->wvscrl && (view_height < fl->list_height) )
		need_vsb = True;
	if(fl->whscrl && (view_width < fl->list_width) )
		need_hsb = True;

	if(need_hsb && fl->whscrl)
		XtManageChild(fl->whscrl);
	else if(fl->wvscrl)
		XtUnmanageChild(fl->whscrl);

	if(need_vsb && fl->wvscrl)
		XtManageChild(fl->wvscrl);
	else if(fl->wvscrl)
		XtUnmanageChild(fl->wvscrl);
	
	fl->in_sb_update = False;
}

/*
 * Returns view dimensions including space occupied by scrollbars
 */
static void get_view_dimensions(Widget w, Boolean sbars,
	Dimension *rwidth, Dimension *rheight)
{
	struct file_list_part *fl = FL_PART(w);
	Dimension width = CORE_WIDTH(w);
	Dimension height = CORE_HEIGHT(w);
	
	if(sbars) {
		Dimension hsb_size = fl->whscrl ? XtHeight(fl->whscrl) : 0;
		Dimension vsb_size = fl->wvscrl ? XtWidth(fl->wvscrl) : 0;

		if(fl->wvscrl && XtIsManaged(fl->wvscrl))
			width += hsb_size;

		if(fl->whscrl && XtIsManaged(fl->whscrl))
			height += vsb_size;
	}
	
	*rwidth = width;
	*rheight = height;
}


/*
 * Horizontal scrollbar callback
 */
static void hscroll_cb(Widget wsb, XtPointer ud, XtPointer cd)
{
	Widget w = (Widget)ud;
	Display *dpy = XtDisplay(w);
	Window wnd = XtWindow(w);
	struct file_list_rec *fl = (struct file_list_rec*)w;
	XmScrollBarCallbackStruct *cbs = (XmScrollBarCallbackStruct*)cd;
	int delta = cbs->value - fl->file_list.xoff;
	
	if(!fl->file_list.num_items) return;
	
	fl->file_list.xoff = cbs->value;
	
	if(fl->core.background_pixmap != XmUNSPECIFIED_PIXMAP) {
		/* if a background pixmap is set, we cannot use optimized scrolling
		 * code below, since it'd cause staggering pattern  */
		redraw_all(w);
		return;
	}
	
	if(delta > 0) {
		XCopyArea(dpy, wnd, wnd, fl->file_list.bg_gc, delta,
			0, fl->core.width - delta, fl->core.height, 0, 0);
		redraw_area(w, fl->core.width - delta, 0, delta, fl->core.height, True);
	} else if(delta < 0){
		XCopyArea(dpy, wnd, wnd, fl->file_list.bg_gc, 0,
			0, fl->core.width + delta, fl->core.height, -delta, 0);
		redraw_area(w, 0, 0, -delta, fl->core.height, True);
	}
}

/*
 * Vertical scrollbar callback
 */
static void vscroll_cb(Widget wsb, XtPointer ud, XtPointer cd)
{
	Widget w = (Widget)ud;
	Display *dpy = XtDisplay((Widget)ud);
	Window wnd = XtWindow((Widget)ud);
	struct file_list_rec *fl =(struct file_list_rec*)w;
	XmScrollBarCallbackStruct *cbs = (XmScrollBarCallbackStruct*)cd;
	int delta = cbs->value - fl->file_list.yoff;

	if(!fl->file_list.num_items) return;

	fl->file_list.yoff = cbs->value;

	/* dragging the thumb quickly may result in deltas larger than the view */
	if(abs(delta) > CORE_HEIGHT(w)) {
		redraw_all(w);
		return;
	}

	if(fl->core.background_pixmap != XmUNSPECIFIED_PIXMAP) {
		/* if a background pixmap is set, we cannot use optimized scrolling
		 * code below, since it'd cause staggering pattern  */
		redraw_all(w);
		return;
	}
	
	if(delta > 0) {
		XCopyArea(dpy, wnd, wnd, fl->file_list.bg_gc, 0,
			delta, fl->core.width, fl->core.height - delta, 0, 0);
		redraw_area(w, 0, fl->core.height - delta,
			fl->core.width, delta, True);
	} else if(delta < 0) {
		XCopyArea(dpy, wnd, wnd, fl->file_list.bg_gc, 0,
			0, fl->core.width, fl->core.height + delta, 0, -delta);
		redraw_area(w, 0, 0, fl->core.width, -delta, True);
	}
}

/*
 * Returns rendition tag for unix file mode specified
 */
static char *get_rendition_tag(const struct item_rec *rec)
{
	char *n;
	
	if(rec->is_symlink) {
		n = RT_SYMLINK;
	} else {
		switch(rec->mode & S_IFMT) {
			case S_IFREG: n = RT_REGULAR; break;
			case S_IFDIR: n = RT_DIRECT; break;
			default: n = RT_SPECIAL; break;
		}
	}
	return n;
}

/*
 * Constructs string labels for the given item, returns True on success.
 * All item_rec fields, except for labels, must have been initialized already.
 */
#define TMP_BUFSIZ	64
static Boolean make_labels(Widget w, struct item_rec *irec)
{
	struct file_list_part *fl = FL_PART(w);
	struct tm tm_file;
	char sz_tmp[TMP_BUFSIZ];
	char sz_time[TIME_BUFSIZ];
	char *psz_tmp;
	size_t len;
	struct passwd *pw;
	struct group *gr;
	XmString xms;
	char *rend_tag;
	Dimension field_widths[NFIELDS];
	Dimension label_width;
	int i;
	
	rend_tag = get_rendition_tag(irec);
	
	/* Label part */
	psz_tmp = mbs_make_displayable(irec->title ? irec->title : irec->name);
	if(psz_tmp) {
		xms = XmStringGenerate(psz_tmp, NULL, XmCHARSET_TEXT, rend_tag);
		free(psz_tmp);
	} else return False;
	
	if(!xms) return False;
	field_widths[FL_FLABEL] = XmStringWidth(fl->label_rt, xms);
	irec->label[FL_FLABEL] = xms;

	get_size_string(irec->size, sz_tmp);
	xms = XmStringGenerate(sz_tmp, NULL, XmCHARSET_TEXT, rend_tag);
	if(!xms) {
		XmStringFree(irec->label[FL_FLABEL]);
		return False;
	}
	field_widths[FL_FSIZE] = XmStringWidth(fl->label_rt, xms);
	irec->label[FL_FSIZE] = xms;
	
	/* User/Group part */
	gr = getgrgid(irec->gid);
	pw = getpwuid(irec->uid);
	
	if(gr && pw) {
		len = strlen(gr->gr_name) + strlen(pw->pw_name);
		psz_tmp = malloc(len + 3);
		sprintf(psz_tmp, "%s:%s", pw->pw_name, gr->gr_name);
	} else {
		psz_tmp = malloc(TMP_BUFSIZ);
		snprintf(psz_tmp, TMP_BUFSIZ, "%d:%d", irec->uid, irec->gid);
	}

	xms = XmStringGenerate(psz_tmp, NULL, XmCHARSET_TEXT, rend_tag);
	free(psz_tmp);
	
	if(!xms) {
		XmStringFree(irec->label[FL_FLABEL]);
		XmStringFree(irec->label[FL_FSIZE]);
		return False;
	}
	
	field_widths[FL_FOWNER] = XmStringWidth(fl->label_rt, xms);
	irec->label[FL_FOWNER] = xms;

	/* Mode part */
	get_mode_string(irec->mode, sz_tmp);

	xms = XmStringGenerate(sz_tmp, NULL, XmCHARSET_TEXT, rend_tag);

	if(!xms) {
		XmStringFree(irec->label[FL_FLABEL]);
		XmStringFree(irec->label[FL_FSIZE]);
		XmStringFree(irec->label[FL_FOWNER]);
		return False;
	}
	
	field_widths[FL_FMODE] = XmStringWidth(fl->label_rt, xms);
	irec->label[FL_FMODE] = xms;

	/* Time part */
	localtime_r(&irec->mtime, &tm_file);
	strftime(sz_time, TIME_BUFSIZ, TIME_FMT, &tm_file);
	
	xms = XmStringGenerate(sz_time, NULL, XmCHARSET_TEXT, rend_tag);

	if(!xms) {
		XmStringFree(irec->label[FL_FLABEL]);
		XmStringFree(irec->label[FL_FSIZE]);
		XmStringFree(irec->label[FL_FOWNER]);
		XmStringFree(irec->label[FL_FMODE]);
		return False;
	}
	
	field_widths[FL_FTIME] = XmStringWidth(fl->label_rt, xms);
	irec->label[FL_FTIME] = xms;
	
	/* Store label and field widths and update maximums */
	for(i = 0; i < NFIELDS; i++) {
		irec->field_widths[i] = field_widths[i];

		if(field_widths[i] > fl->field_widths[i]) {
			fl->field_widths[i] = field_widths[i];
		}
	}

	label_width = fl->icon_width_max + fl->label_margin +
		fl->label_spacing * (NFIELDS - 1);
	
	for(i = 0; i < NFIELDS; i++)
		label_width += fl->field_widths[i];
	
	irec->detail_width = label_width;
	
	if(fl->item_width_max[XfDETAILED] < label_width)
		fl->item_width_max[XfDETAILED] = label_width;
	
	return True;
}




/*
 * Checks for intersection of two rectangles.
 * Returns True if they intersect in any kind of way.
 */
static Boolean intersect_rects(
	int ax, int ay, int ax1, int ay1, int bx, int by, int bx1, int by1)
{
	/* point within */
	if((bx > ax && bx < ax1 && by > ay && by < ay1) ||
		(ax > bx && ax < bx1 && ay > by && ay < by1)) return True;

	/* perpendicular edges */
	#define IC(x, y, z, u, v, w) (x > u && x < w && v > y && v < z)
	if(IC(ax, ay, ay1, bx, by, bx1) ||
		IC(ax, ay, ay1, bx, by1, bx1) ||
		IC(ax1, ay, ay1, bx, by, bx1) ||
		IC(ax1, ay, ay1, bx, by1, bx1) ||
		IC(bx, by, by1, ax, ay, ax1) ||
		IC(bx, by, by1, ax, ay1, ax1) ||
		IC(bx1, by, by1, ax, ay, ax1) ||
		IC(bx1, by, by1, ax, ay1, ax1)) return True;
	#undef IC

	return False;
}

/*
 * Marks items within rect (in list coordinates) as selected, unmarking the
 * rest if add is False. If add is true, selection within rect will be
 * inverted, the rest is untouched.
 */
static void select_within_rect(Widget w,
	const struct rectangle *rc, Boolean add)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int changed = 0;
	unsigned int i;
	
	for(i = 0; i < fl->num_items; i++) {
		Dimension item_width = (fl->view_mode == XfCOMPACT) ? 
			fl->items[i].width : fl->item_width_max[fl->view_mode];
	
		if(intersect_rects(
			rc->x, rc->y, rc->x + rc->width, rc->y + rc->height,
			fl->items[i].x, fl->items[i].y,
			fl->items[i].x + item_width,
			fl->items[i].y + fl->item_height_max))
		{
			
			if(add && (fl->items[i].selected))
				fl->items[i].selected = False;
			else
				fl->items[i].selected = True;
			
			draw_item(w, i, True);
			changed++;
		} else if((fl->items[i].selected) && !add) {
			fl->items[i].selected = False;
			draw_item(w, i, True);
			changed++;
		}
	}
	if(changed) sel_change_handler(w, True);
}

/*
 * Checks if x,y is within i, returns True if so; also sets 'islabel' to
 * True if point is within the label part.
 */
static Boolean hit_test(Widget w, int x, int y,
	unsigned int i, Boolean *islabel)
{
	struct file_list_part *fl = FL_PART(w);
	Dimension item_width = (fl->view_mode == XfCOMPACT) ? 
		fl->items[i].width : fl->item_width_max[fl->view_mode];

	if(islabel) *islabel = False;

	if(x > fl->items[i].x && y > fl->items[i].y &&
		x < (fl->items[i].x + item_width) &&
		y < (fl->items[i].y + fl->item_height_max)) {

		if( x > (fl->items[i].x + fl->icon_width_max) &&
			y > (fl->items[i].y + fl->items[i].text_yoff) &&
			x < (fl->items[i].x + item_width) &&
			y < (fl->items[i].y + fl->item_height_max)) {
			if(islabel) *islabel = True;
		}
		return True;
	}
	return False;
}

/*
 * Checks for item at x, y. Returns True and places index in 'res'
 * on success, returns False otherwise.
 */
static Boolean get_at_xy(Widget w, int x, int y, unsigned int *res)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int i;

	for(i = 0; i < fl->num_items; i++) {
		if(hit_test(w, x, y, i, NULL)) {
			*res = i;
			return True;
		}
	}

	return False;
}

/*
 * Marks item at x,y as selected. Removes current selection if 'add' is False.
 */
static void select_at_xy(Widget w, int x, int y, Boolean add)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int changed = 0;
	unsigned int i;

	for(i = 0; i < fl->num_items; i++) {
		if(hit_test(w, x, y, i, NULL))
		{
			if(add && (fl->items[i].selected)) {
				fl->items[i].selected = False;
			} else if(!(fl->items[i].selected)){
				fl->items[i].selected = True;
			}
			changed++;
			
			set_cursor(w, i);
			draw_item(w, i, True);
		} else if(!add && (fl->items[i].selected)) {
			fl->items[i].selected = False;
			draw_item(w, i, True);
			changed++;
		}
	}
	if(changed) sel_change_handler(w, True);
}

/*
 * Marks items spanning from index a to b as selected, unmarking the rest.
 */
static void select_range(Widget w, unsigned int a, unsigned int b)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int changed = 0;
	unsigned int i;
	
	if(a > b) {
		unsigned int tmp;
		tmp = b;
		b = a;
		a = tmp;
	} 
	
	for(i = 0; i < fl->num_items; i++) {
		if(i >= a && i <= b){
			fl->items[i].selected = True;
			draw_item(w, i, True);
			changed++;
		} else if(fl->items[i].selected) {
			fl->items[i].selected = False;
			draw_item(w, i, True);
			changed++;
		}
	}
	if(changed) sel_change_handler(w, True);
}

/*
 * Marks the item at the cursor as selected, toggles if toggle is true,
 * deselects all other items if replace is true
 */
static void select_at_cursor(Widget w, Boolean toggle, Boolean replace)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int i, cur;

	if(!fl->num_items) return;
	
	cur = get_cursor(w);
	
	if(toggle) {
		if(fl->items[cur].selected)
			fl->items[cur].selected = False;
		else
			fl->items[cur].selected = True;
	} else {
		fl->items[cur].selected = True;
	}
	draw_item(w, cur, True);

	if(replace) {
		for(i = 0; i < fl->num_items; i++) {
			if((i != cur) && (fl->items[i].selected)) {
				fl->items[i].selected = False;
				draw_item(w, i, True);
			}
		}
	}
	sel_change_handler(w, True);
}

/*
 * Sets cursor to i, repaints both, previous and current items
 */
static void set_cursor(Widget w, unsigned int i)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int prev = fl->cursor;

	dbg_assert(i < fl->num_items);

	if(i == fl->cursor) return;
	
	fl->cursor = i;
	fl->ext_position = i;
	if(prev < fl->num_items) {
		draw_item(w, prev, True);
	}
	draw_item(w, i, True);
}

/*
 * Returns cursor index, resets cursor if it's invalid
 */
static unsigned int get_cursor(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	
	dbg_assert(fl->num_items);
	
	if(fl->cursor >= fl->num_items) {
		fl->cursor = 0;
		fl->ext_position = 0;
		draw_item(w, 0, True);
	}
	return fl->cursor;
}

/*
 * Scroll the item into the viewable area
 */
static void scroll_into_view(Widget w, unsigned int i)
{
	struct file_list_part *fl = FL_PART(w);
	int view_height = CORE_HEIGHT(w);
	int v;
	
	dbg_assert(fl->num_items);
	
	if(!fl->wvscrl || (view_height >= fl->list_height)) return;

	if((fl->items[i].y + fl->item_height_max) > (fl->yoff + view_height)) {
		/* below viewable area */
		v = fl->items[i].y - (view_height -
			(fl->item_height_max + fl->margin_h));
			
	} else if(fl->items[i].y < fl->yoff) {
		/* above viewable area */
		v = fl->items[i].y - fl->margin_h;
	} else {
		/* within view */
		return;
	}

	XmScrollBarSetValues(fl->wvscrl, (v > 0) ? v : 0, 0, 0, 0, True);
}

/*
 * Determines the range of items currently visible in the viewing area
 * and returns their indices in p_start/p_end.
 */
static void get_visible_range(Widget w,
	unsigned int *p_start, unsigned int *p_end)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int i = 0;
	unsigned int j = fl->num_items;
	double fi;
	
	if(!fl->num_items) {
		*p_start = 0;
		*p_end = 0;
		return;
	}
	
	/* compute the start item from the amount of items that'll fit
	 * into the off-screen area, and end item from index + number of items
	 * that'll fit into the viewable area */
	if(fl->view_mode == XfDETAILED) {
		fi = (double)fl->yoff / (fl->item_height_max + fl->vert_spacing);

		j = ceil((double)CORE_HEIGHT(w) / 
			((fl->item_height_max + fl->vert_spacing))) + ceilf(fi);
		i = floorf(fi);

	} else if(fl->view_mode == XfCOMPACT) {
		double fi = ((double)fl->yoff /
			(fl->item_height_max + fl->vert_spacing));
		
		j = ceil(((double)CORE_HEIGHT(w) /
			(fl->item_height_max + fl->vert_spacing))) *
			fl->ncolumns + ceilf(fi) * fl->ncolumns;

		i = floorf(fi) * fl->ncolumns;
	}
	/* ...make sure not to overflow */
	if(j > fl->num_items) j = fl->num_items;
	
	*p_start = i;
	*p_end = j;
}

/*
 * Intrinsic widget routines
 */
static void expose(Widget w, XEvent *evt, Region reg)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int i = 0;
	unsigned int nitems = fl->num_items;
	
	if(!fl->num_items || !fl->show_contents ||
		!((struct file_list_rec*)w)->core.visible) return;
	
	get_visible_range(w, &i, &nitems);
	
	for( ; i < nitems; i++) {

		int res = XRectInRegion(reg,
			fl->items[i].x - fl->xoff,
			fl->items[i].y - fl->yoff,
			((fl->view_mode == XfCOMPACT) ? 
			fl->items[i].width : fl->item_width_max[fl->view_mode]),
			fl->item_height_max);
		
		if(res & (RectangleIn | RectanglePart)) {
			/* We cannot paint over Xft rendered text, since it's being
			 * alpha-blended and results in artifacts; and the region isn't
			 * guaranteed to have been erased completely */
			
			#ifdef USE_XFT /* defined by motif */
			draw_item(w, i, True);
			#else
			draw_item(w, i, (res & RectangleIn) ? True : False);
			#endif
		}
	}
}

static void resize(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	if(!fl->in_sb_update) {
		update_sbar_range(w, CORE_WIDTH(w), CORE_HEIGHT(w));
	}
}

static XtGeometryResult query_geometry(Widget w,
	XtWidgetGeometry *ig, XtWidgetGeometry *pg)
{
	struct file_list_part *fl = FL_PART(w);
	
	if(ig->request_mode & (CWWidth | CWHeight)) {
		Dimension rwidth = (ig->request_mode & CWWidth) ?
			ig->width : CORE_WIDTH(w);
		Dimension rheight = (ig->request_mode & CWHeight) ?
			ig->height : CORE_HEIGHT(w);
		
		if(!fl->in_sb_update) {
			compute_placement(w, rwidth, rheight);
			update_sbar_visibility(w, rwidth,rheight);
			update_sbar_range(w, rwidth, rheight);
		}
	}

	memcpy(pg, ig, sizeof(XtWidgetGeometry));
	return XtGeometryYes;
}

static Boolean widget_display_rect(Widget w, XRectangle *r)
{
	struct file_list_rec *fl = (struct file_list_rec*) w;
	r->x = fl->file_list.margin_w;
	r->y = fl->file_list.margin_h;
	r->width = fl->core.width - fl->file_list.margin_w;
	r->height = fl->core.height - fl->file_list.margin_h;
	return True;
}

static void realize(Widget w, XtValueMask *mask, XSetWindowAttributes *att)
{
	struct file_list_part *fl = FL_PART(w);
	Display *dpy = XtDisplay(w);
	Window wnd;
	const char stipple_data[] = {
		0x55, 0xAA, 0x55, 0xAA,
		0x55, 0xAA, 0x55, 0xAA
	};
	
	(*fileListWidgetClass->core_class.superclass->core_class.realize)
		(w, mask, att);

	wnd = XtWindow(w);
	
	/* stipple for shading selected icons */
	fl->stipple = XCreatePixmapFromBitmapData(
		dpy, wnd, (char*)stipple_data, 8, 8, 0, 1, 1);

	XSetStipple(dpy, fl->icon_gc, fl->stipple);
	XSetFillStyle(dpy, fl->icon_gc, FillStippled);
	XSetForeground(dpy, fl->icon_gc, fl->select_pixel);
}

static void initialize(Widget wreq, Widget wnew,
	ArgList init_args, Cardinal *ninit_args)
{
	struct file_list_rec *fl = (struct file_list_rec*) wnew;
	Arg args[10];
	Cardinal n;
	XtCallbackRec sb_callback[2] = {NULL};
	
	/* Initial state */
	fl->file_list.items = NULL;
	fl->file_list.items_size = 0;
	fl->file_list.num_items = 0;
	fl->file_list.cursor = 0;
	fl->file_list.ext_position = 0;
	fl->file_list.icon_width_max = 0;
	fl->file_list.icon_height_max = 0;
	fl->file_list.item_height_max = 0;
	fl->file_list.text_height_max = 0;
	fl->file_list.sel_count = 0;
	fl->file_list.sel_names = NULL;
	fl->file_list.dblclk_timeout = None;
	fl->file_list.autoscrl_timeout = None;
	fl->file_list.autoscrl_vec = 0;
	fl->file_list.lookup_timeout = None;
	fl->file_list.show_contents = False;
	fl->file_list.sz_lookup[0] = '\0';
	fl->file_list.dragging = False;
	fl->file_list.in_sb_update = False;
	fl->file_list.ptr_last_valid = False;
	fl->file_list.highlight_sel = False;

	if(fl->file_list.shorten && (fl->file_list.shorten < SHORTEN_LEN_MIN)) {
		WARNING(wnew, "shortenLabels length too short, using default!");
		fl->file_list.shorten = SHORTEN_LEN_MAX;
	}
	
	if(fl->file_list.lookup_time < MIN_LOOKUP_TIMEOUT ||
		fl->file_list.lookup_time > MAX_LOOKUP_TIMEOUT) {
		WARNING(wnew, "Invalid lookupTimeout value, using default!");
		fl->file_list.lookup_time = DEF_LOOKUP_TIMEOUT;
	}
	fl->file_list.lookup_time *= 1000;

	memset(fl->file_list.field_widths, 0,
		sizeof(fl->file_list.field_widths));
	memset(fl->file_list.item_width_max, 0,
		sizeof(fl->file_list.item_width_max));
	
	/* Dynamic defaults */
	if(fl->file_list.dblclk_int == -1) {
		fl->file_list.dblclk_int = XtGetMultiClickTime(XtDisplay(wnew));
	}
	
	if(fl->file_list.label_spacing == 0) {
		int height, ascent, descent;
		
		XmRenderTableGetDefaultFontExtents(
			fl->file_list.label_rt, &height, &ascent, &descent);
		fl->file_list.label_spacing = (Dimension) height;
	}
	
	/* Scroll-bars */
	n = 0;
	XtSetArg(args[n], XmNminimum, 0); n++;
	XtSetArg(args[n], XmNmaximum, 100); n++;
	XtSetArg(args[n], XmNsliderSize, 100); n++;
	XtSetArg(args[n], XmNincrement, 1); n++;
	XtSetArg(args[n], XmNpageIncrement, 1); n++;
	XtSetArg(args[n], XmNvalueChangedCallback, sb_callback); n++;
	XtSetArg(args[n], XmNdragCallback, sb_callback); n++;

	sb_callback[0].closure = (XtPointer)wnew;
	sb_callback[0].callback = hscroll_cb;
	if(fl->file_list.whscrl) XtSetValues(fl->file_list.whscrl, args, n);
	sb_callback[0].callback = vscroll_cb;
	if(fl->file_list.wvscrl) XtSetValues(fl->file_list.wvscrl, args, n);
	
	init_gcs(wnew);
}

static void init_gcs(Widget w)
{
	struct file_list_rec *fl = (struct file_list_rec*) w;
	XGCValues gcv;
	XtGCMask gc_mask;
	XColor xc = { 0 };

	/* Label GC */
	gcv.function = GXcopy;
	gcv.foreground = fl->primitive.foreground;
	gc_mask = GCForeground | GCFunction;

	/* XmStringDraw needs an allocated GC */
	fl->file_list.label_gc = XtAllocateGC(w, 0,	gc_mask, &gcv,
		gc_mask | GCClipMask | GCFont, 0);

	/* stipple is used to shade selected icons and is inited in realize()
	 * since we need a window handle to create the bitmap */
	fl->file_list.icon_gc = XtAllocateGC(w, 0, gc_mask, &gcv, 0, 0);
	fl->file_list.fg_pixel = fl->primitive.foreground;

	/* Generic fg/bg shareable GCs */
	gcv.foreground = fl->core.background_pixel;
	fl->file_list.bg_gc = XtGetGC(w, GCForeground, &gcv);
	
	/* selected background */
	gcv.foreground = fl->file_list.select_pixel;
	fl->file_list.sbg_gc = XtGetGC(w, GCForeground, &gcv);

	xc.pixel = fl->file_list.select_pixel;
	XQueryColor(XtDisplay(w), fl->core.colormap, &xc);
	
	/* unfocused (grayed) selection background */
	xc.red = xc.green = xc.blue =
		((unsigned int)xc.red + xc.green + xc.blue) / 3;
	XAllocColor(XtDisplay(w), fl->core.colormap, &xc);
	gcv.foreground = fl->file_list.nfbg_pixel = xc.pixel;
	fl->file_list.nfbg_gc = XtGetGC(w, GCForeground, &gcv);
	
	/* selection foreground color */
	xc.red /= 256;
	xc.green /= 256;
	xc.blue /= 256;

	fl->file_list.sfg_pixel =
		(((unsigned int)xc.red * xc.red + xc.green * xc.green +
			xc.blue * xc.blue) > DEF_FG_THRESHOLD) ?
		BlackPixelOfScreen(fl->core.screen) :
		WhitePixelOfScreen(fl->core.screen);
	
	/* Rubber bands shareable GC */
	gcv.function = GXinvert;
	gcv.foreground = fl->primitive.foreground;
	gcv.line_width = fl->file_list.outline_width;
	gcv.line_style = LineOnOffDash;
	gcv.dashes = gcv.line_width;
	fl->file_list.xor_gc = XtGetGC(w,
		GCForeground | GCFunction |	GCLineWidth |
		GCLineStyle | GCDashList, &gcv);
}

static Boolean set_values(Widget wcur, Widget wreq,
	Widget wset, ArgList args, Cardinal *nargs)
{
	struct file_list_rec *cur = (struct file_list_rec*) wcur;
	struct file_list_rec *req = (struct file_list_rec*) wreq;
	struct file_list_rec *set = (struct file_list_rec*) wset;
	
	/* Creation only resources */
	set->file_list.shorten = cur->file_list.shorten;
	
	/* FIXME: Although this routine processes resources set programmatically,
	 *        eventually it should validate values specified */
	if( (cur->primitive.foreground != set->primitive.foreground) ||
		(cur->core.background_pixel != set->core.background_pixel) ) {

		XtReleaseGC(wcur, set->file_list.label_gc);
		XtReleaseGC(wcur, set->file_list.icon_gc);
		XtReleaseGC(wcur, set->file_list.bg_gc);
		XtReleaseGC(wcur, set->file_list.nfbg_gc);
		XtReleaseGC(wcur, set->file_list.sbg_gc);
		XtReleaseGC(wcur, set->file_list.xor_gc);
		XFreeColors(XtDisplay(wcur), cur->core.colormap,
			&cur->file_list.nfbg_pixel, 1, 0);
		
		init_gcs(wset);
	}

	if(cur->file_list.sort_direction != set->file_list.sort_direction ||
		cur->file_list.sort_order != set->file_list.sort_order) {
		sort_list(wset);
	}

	compute_placement(wset, req->core.width, req->core.height);
	update_sbar_visibility(wset, CORE_WIDTH(wset), CORE_HEIGHT(wset));
	update_sbar_range(wset, CORE_WIDTH(wset), CORE_HEIGHT(wset));
	
	return (XtIsRealized(wset) ? True : False);
}

static void class_initialize(void)
{
	XmRepTypeRegister(XfRSortOrder,
		sort_by_names, NULL, XtNumber(sort_by_names));

	XmRepTypeRegister(XfRSortDirection,
		sort_dir_names, NULL, XtNumber(sort_dir_names));

	XmRepTypeRegister(XfRViewMode,
		view_mode_names, NULL, XtNumber(view_mode_names));
}

static void destroy(Widget w)
{
	struct file_list_part *fl = FL_PART(w);

	if(fl->items) {
		unsigned int i;
		
		for(i = 0; i < fl->num_items; i++) {
			free_item(&fl->items[i]);
		}
		free(fl->items);
		fl->items = NULL;
	}
	
	if(fl->sel_names) free(fl->sel_names);

	if(fl->autoscrl_timeout != None) {
		fl->autoscrl_vec = 0;
		XtRemoveTimeOut(fl->autoscrl_timeout);
		fl->autoscrl_timeout = None;
	}

	if(fl->lookup_timeout != None) {
		XtRemoveTimeOut(fl->lookup_timeout);
		fl->lookup_timeout = None;
	}


	XtReleaseGC(w, fl->label_gc);
	XtReleaseGC(w, fl->icon_gc);
	XtReleaseGC(w, fl->bg_gc);
	XtReleaseGC(w, fl->nfbg_gc);
	XtReleaseGC(w, fl->sbg_gc);
	XtReleaseGC(w, fl->xor_gc);

	XFreeColors(XtDisplay(w), ((struct file_list_rec*) w)->core.colormap,
		&fl->nfbg_pixel, 1, 0);
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

static void default_hspacing(Widget w, int offset, XrmValue *pv)
{
	static Dimension result;
	XmRenderTable rt;
	int height, ascent, descent;
	

	rt = XmeGetDefaultRenderTable(w, XmLABEL_RENDER_TABLE);

	XmRenderTableGetDefaultFontExtents(rt, &height, &ascent, &descent);
	result = height / 2;

	pv->addr = (XtPointer) &result;
	pv->size = sizeof(Dimension);
}

/*
 * Actions
 */
static void activate(Widget w, XEvent *evt, String *params, Cardinal *nparams)
{
	default_action_handler(w,
		(evt->type == KeyPress) ? evt->xkey.state : evt->xbutton.state);
}

static void select_current(Widget w, XEvent *evt,
	String *params, Cardinal *nparams)
{
	if(*nparams){
		if(!strcasecmp(params[0], "ADD"))
			select_at_cursor(w, True, False);
		else if(!strcasecmp(params[0], "Toggle"))
			select_at_cursor(w, True, True);
	} else {
		select_at_cursor(w, False, True);
	}
}

/* This is set in lookup_input */
static void lookup_timeout_cb(XtPointer data, XtIntervalId *iid)
{
	struct file_list_part *fl = FL_PART(data);
	fl->sz_lookup[0] = '\0';
	fl->lookup_timeout = None;
}

/*
 * Resets incremental search string
 */
static void reset_lookup(Widget w, XEvent *evt,
	String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);

	if(evt->type != KeyPress) {
		WARNING(w, "Wrong event type for the action ResetLookUp()");
		return;
	}

	if(fl->lookup_timeout != None) {
		XtRemoveTimeOut(fl->lookup_timeout);
		fl->lookup_timeout = None;
	}
	fl->sz_lookup[0] = '\0';
}

/*
 * Collects keyboard input and performs incremental search,
 * moving the cursor to the closest match found.
 */
static void lookup_input(Widget w, XEvent *evt,
	String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);
	KeySym keysym;
	size_t pos;

	if(evt->type != KeyPress) {
		WARNING(w, "Wrong event type for the action LookUpInput()");
		return;
	}

	#ifdef NO_XKB
	keysym = XKeycodeToKeysym(XtDisplay(w), evt->xkey.keycode,
		(evt->xkey.state & (ShiftMask | LockMask)) ? 1 : 0);
	#else
	keysym = XkbKeycodeToKeysym(XtDisplay(w), evt->xkey.keycode, 0,
		(evt->xkey.state & (ShiftMask | LockMask)) ? 1 : 0);
	#endif

	if(IsCursorKey(keysym) || IsFunctionKey(keysym) ||
		IsMiscFunctionKey(keysym) || IsModifierKey(keysym) ||
		IsKeypadKey(keysym) || IsPFKey(keysym) ||
		IsPrivateKeypadKey(keysym)) return;

	pos = wcslen(fl->sz_lookup);
	if(pos == LOOKUP_STR_MAX) {
		fl->sz_lookup[0] = '\0';
		if(!fl->silent) XBell(XtDisplay(w), 100);
		return;
	}

	if(iswprint((wchar_t)keysym)) {
		unsigned int i;

		if(fl->lookup_timeout != None) {
			XtRemoveTimeOut(fl->lookup_timeout);
			fl->lookup_timeout = None;
		}

		fl->sz_lookup[pos] = (wchar_t)keysym;
		fl->sz_lookup[pos + 1] = '\0';
	
		for(i = 0; i < fl->num_items; i++) {
			wchar_t wcs_title[LOOKUP_STR_MAX + 1];
			if(mbstowcs(wcs_title, fl->items[i].name, LOOKUP_STR_MAX) == -1) {
				if(!fl->silent) XBell(XtDisplay(w), 100);
				return;
			}
			
			if(!wcsncmp(fl->sz_lookup, wcs_title, pos + 1)) {
				scroll_into_view(w, i);
				set_cursor(w, i);
				select_at_cursor(w, False, True);
				break;
			}
		}
		if(i == fl->num_items && !fl->silent)
			XBell(XtDisplay(w), 100);
	
		fl->lookup_timeout = XtAppAddTimeOut(
			XtWidgetToApplicationContext(w),
			fl->lookup_time, lookup_timeout_cb, (XtPointer)w);
	}
}

/* This is set in primary_button handler */
static void dblclk_timeout_cb(XtPointer data, XtIntervalId *iid)
{
	struct file_list_part *fl = FL_PART(data);
	fl->dblclk_timeout = None;
}

/*
 * Handles single/rectangle-selection and double click activation
 */
static void primary_button(Widget w, XEvent *evt,
	String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);
	Boolean extend = False;
	
	if(evt->type != ButtonPress && evt->type != ButtonRelease) {
		WARNING(w, "Wrong event type for the action PrimaryButton()");
		return;
	}
	
	if(! (*nparams)) {
		WARNING(w, "Missing mandatory action parameter for "
			"PrimaryButton(Up|Down, [Extend])");
		return;
	}
	
	if(*nparams > 1) {
		if(!strcasecmp(params[1], "ADD")) {
			fl->sel_add_mode = True;
		} else if(!strcasecmp(params[1], "EXTEND")) {
			extend = True;
			fl->sel_add_mode = False;
		}
	} else {
		fl->sel_add_mode = False;
	}
	
	if(!strcasecmp(params[0], "DOWN")) {
		XmProcessTraversal(w, XmTRAVERSE_CURRENT);
		fl->ptr_last_x = evt->xbutton.x;
		fl->ptr_last_y = evt->xbutton.y;
		fl->ptr_last_valid = True;
		fl->dblclk_lock = False;
		
		if(extend) {
			unsigned int to;
			
			if(get_at_xy(w, evt->xbutton.x + fl->xoff,
				evt->xbutton.y + fl->yoff, &to)) {
				select_range(w, get_cursor(w), to);
			}

		} else {
			if(fl->dblclk_timeout == None) {
				if(get_at_xy(w, evt->xbutton.x + fl->xoff,
					evt->xbutton.y + fl->yoff, &fl->last_clicked)) {
					set_cursor(w, fl->last_clicked);
					fl->dblclk_timeout = XtAppAddTimeOut(
						XtWidgetToApplicationContext(w),
						fl->dblclk_int, dblclk_timeout_cb, w);
				}
			} else {
				unsigned int cur;
				
				XtRemoveTimeOut(fl->dblclk_timeout);
				fl->dblclk_timeout = None;
				
				if(get_at_xy(w, evt->xbutton.x + fl->xoff,
					evt->xbutton.y + fl->yoff, &cur) &&
					(cur == fl->last_clicked)) {
						fl->dblclk_lock = True;
					}
			}
		}

	} else if(!strcasecmp(params[0], "UP")) {
		
		fl->ptr_last_valid = False;
		
		if(fl->autoscrl_timeout != None) {
			XtRemoveTimeOut(fl->autoscrl_timeout);
			fl->autoscrl_timeout = None;
			fl->autoscrl_vec = 0;
		}

		if(fl->dragging) {
			struct rectangle rc;
			
			/* we're in drag mode; erase rubber bands, if any */
			draw_rubber_bands(w);
			
			get_selection_rect(w, &rc);

			rc.x += fl->xoff;
			rc.y += fl->yoff;

			select_within_rect(w, &rc, fl->sel_add_mode);
			
			/* reset selection rectangle and drag mode */
			memset(&fl->sel_rect, 0, sizeof(struct rectangle));
			fl->dragging = False;
		} else if(fl->dblclk_lock) {
			fl->dblclk_lock = False;
			default_action_handler(w, evt->xbutton.state);
		} else if(!extend) {
			select_at_xy(w, fl->ptr_last_x + fl->xoff,
				fl->ptr_last_y + fl->yoff, fl->sel_add_mode);
		}
	}
}

/*
 * Automatic scrolling callback. Initiated when the mouse pointer
 * is dragged over the top/bottom edge of the view, while in selection mode.
 */
static void autoscrl_timeout_cb(XtPointer data, XtIntervalId *iid)
{
	struct file_list_part *fl = FL_PART(data);
	Widget w = (Widget)data;
	unsigned int scrl_max = fl->list_height - CORE_HEIGHT(data);
	int scrl_pos = (int)fl->yoff + fl->autoscrl_vec;
	

	/* don't do anything if state has changed since timer was set */
	if(!fl->dragging || !fl->autoscrl_vec ||
		scrl_pos < 0 || scrl_pos > scrl_max) {
		fl->autoscrl_timeout = None;
		return;
	}

	/* erase previous rubber-bands, scroll the view and redraw them */
	draw_rubber_bands(w);

	XmScrollBarSetValues(fl->wvscrl, scrl_pos, 0, 0, 0, True);
	
	fl->sel_rect.y -= fl->autoscrl_vec;
	fl->sel_rect.height += fl->autoscrl_vec;
	fl->ptr_last_y -= fl->autoscrl_vec;

	draw_rubber_bands(w);

	fl->autoscrl_timeout = XtAppAddTimeOut(
		XtWidgetToApplicationContext((Widget)data),
		(DEF_SCRL_TIME / fl->scrl_factor), autoscrl_timeout_cb, (Widget)data);
}

/*
 * Draws selection rectangle rubber bands and
 * updates sel_rect coordinates accordingly
 */
static void button_motion(Widget w, XEvent *evt,
	String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);
	Dimension view_height = CORE_HEIGHT(w);
	int delx, dely;

	if(evt->type != MotionNotify) {
		WARNING(w, "Wrong event type for the action PrimaryButtonMotion()");
		return;
	}
	
	if(!fl->ptr_last_valid) return;

	delx = evt->xbutton.x - fl->ptr_last_x;
	dely = evt->xbutton.y - fl->ptr_last_y;
	
	if(fl->dragging) {
		/* erase previous rubber bands, if any */
		draw_rubber_bands(w);
	} else {
		/* don't initiate 'drag mode' unless above offset */
		if(abs(delx) < fl->drag_offset && abs(dely) < fl->drag_offset)
			return;
	}
	
	/* check if we're outside the viewable area; initiate auto-scrolling
	 * NOTE: we don't care about horizontal, since row-major layout is the
	 *       only layout we support currently */
	if( ((evt->xbutton.y > view_height) || evt->xbutton.y < 0) &&
		(fl->list_height > view_height) ) {

		if(fl->autoscrl_timeout == None) {

			fl->autoscrl_vec = (evt->xbutton.y > 0) ?
				DEF_SCRL_PIX : -DEF_SCRL_PIX;

			fl->autoscrl_timeout = XtAppAddTimeOut(
				XtWidgetToApplicationContext(w),
				(DEF_SCRL_TIME / fl->scrl_factor),
				autoscrl_timeout_cb, w);
		}
	} else if(fl->autoscrl_timeout != None) {
		XtRemoveTimeOut(fl->autoscrl_timeout);
		fl->autoscrl_timeout = None;
		fl->autoscrl_vec = 0;
	}
	
	/* (re)calculate rubber bands and initiate drag mode */
	fl->sel_rect.x = fl->ptr_last_x;
	fl->sel_rect.width = delx;
	fl->sel_rect.y = fl->ptr_last_y;
	fl->sel_rect.height = dely;
	fl->dragging = True;
	
	draw_rubber_bands(w);
}

/*
 * Handles secondary button select & post context menu
 */
static void secondary_button(Widget w, XEvent *evt,
	String *params, Cardinal *nparams)
{
	struct file_list_rec *rec = FL_REC(w);
	struct file_list_part *fl = FL_PART(w);

	if(fl->dragging) return;

	if(evt->type != ButtonPress) {
		WARNING(w, "Wrong event type for the action SecondaryButton()");
		return;
	}
	XmProcessTraversal(w, XmTRAVERSE_CURRENT);

	if(fl->sel_count < 2) {
		select_at_xy(w, evt->xbutton.x + fl->xoff,
			evt->xbutton.y + fl->yoff, False);
	}
	
	if(rec->core.num_popups) {
		Widget menu = ((CompositeRec*)
			rec->core.popup_list[0])->composite.children[0];
		XmMenuPosition(menu, &evt->xbutton);
		XtManageChild(menu);
	}
}

/*
 * Moves the cursor, extends the selection and scrolls the view if necessary.
 */
static void move_cursor(Widget w, XEvent *evt,
	String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);
	Boolean extend = False;
	unsigned int cursor = get_cursor(w);
	unsigned int new_cursor;
	int row, col;

	if(fl->dragging) return;

	if(evt->type != KeyPress) {
		WARNING(w, "Wrong event type for the action MoveCursor()");
		return;
	}
	
	if(! (*nparams)) {
		WARNING(w, "Missing mandatory action parameter for "
			"MoveCursor(Up|Down|Left|Right, [Extend])");
		return;
	}

	if(!fl->num_items) return;

	/* when extending, cursor stays stationary and is used to compute
	 * the range, while ext_position carries the current position */
	if((*nparams > 1) && (!strcasecmp(params[1],"EXTEND"))) {
		cursor = fl->ext_position;
		extend = True;
	}
	
	row = cursor / fl->ncolumns;
	col = cursor % fl->ncolumns;
	
	if(!strcasecmp(params[0], "UP")) {
		row--;
	} else if(!strcasecmp(params[0], "DOWN")) {
		row++;
	} else if(!strcasecmp(params[0], "LEFT")) {
		col--;
		if(col < 0) {
			col = fl->ncolumns - 1;
			row--;
		}		
	} else if(!strcasecmp(params[0], "RIGHT")) {
		col++;
		if(col == fl->ncolumns) {
			col = 0;
			row++;
		}
	} else if(!strcasecmp(params[0], "BEGIN")) {
		row = 0;
		col = 0;
	} else if(!strcasecmp(params[0], "END")) {
		row = (fl->num_items - 1) / fl->ncolumns;
		col = (fl->num_items - 1) % fl->ncolumns;
	}

	if(row < 0) return;

	new_cursor = row * fl->ncolumns + col;
	
	if(new_cursor >= fl->num_items) return;
	
	if(extend) {
		cursor =  fl->cursor;
		select_range(w, cursor, new_cursor);
		fl->ext_position = new_cursor;
	} else {
		/* set_cursor will also reset ext_position */
		set_cursor(w, new_cursor);
	}
	
	scroll_into_view(w, new_cursor);
}

/*
 * Scrolls the view page-wise
 */
static void pg_scroll(Widget w, XEvent *evt, String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);

	if(fl->dragging) return;
	
	if(! (*nparams)) {
		WARNING(w, "Missing mandatory action parameter for "
			"PageScroll(Up|Down|Left|Right)");
		return;
	}
	if(!fl->wvscrl || !fl->whscrl) return;
	
	if(!strcasecmp(params[0], "UP")) {
		XtCallActionProc(fl->wvscrl, "PageUpOrLeft", evt, params, 1);
	} else if(!strcasecmp(params[0], "DOWN")) {
		XtCallActionProc(fl->wvscrl, "PageDownOrRight", evt, params, 1);
	} else if(!strcasecmp(params[0], "LEFT")) {
		XtCallActionProc(fl->whscrl, "PageUpOrLeft", evt, params, 1);
	} else if(!strcasecmp(params[0], "RIGHT")) {
		XtCallActionProc(fl->whscrl, "PageDownOrRight", evt, params, 1);
	}
}

/*
 * Scrolls the view row-wise
 */
static void scroll(Widget w, XEvent *evt, String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);
	
	if(fl->dragging) return;
	
	if(! (*nparams)) {
		WARNING(w, "Missing mandatory action parameter for "
			"Scroll(Up|Down|Left|Right)");
		return;
	}
	
	if(!fl->wvscrl || !fl->whscrl) return;

	if(!strcasecmp(params[0], "UP")) {
		XtCallActionProc(fl->wvscrl, "IncrementUpOrLeft", evt, params, 1);
	} else if(!strcasecmp(params[0], "DOWN")) {
		XtCallActionProc(fl->wvscrl, "IncrementDownOrRight", evt, params, 1);
	} else if(!strcasecmp(params[0], "LEFT")) {
		XtCallActionProc(fl->whscrl, "IncrementUpOrLeft", evt, params, 1);
	} else if(!strcasecmp(params[0], "RIGHT")) {
		XtCallActionProc(fl->whscrl, "IncrementDownOrRight", evt, params, 1);
	}	
}

/* Focus in/out */
static void focus_in(Widget w, XEvent *evt, String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);
	
	fl->has_focus = True;
	fl->ptr_last_valid = False;
	if(fl->num_items && fl->show_contents &&
		((struct file_list_rec*)w)->core.visible) {
			draw_item(w, get_cursor(w), True);
	}
	_XmPrimitiveFocusIn(w, evt, NULL, NULL);
}

static void focus_out(Widget w, XEvent *evt, String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);

	fl->has_focus = False;

	if(fl->autoscrl_timeout) {
		XtRemoveTimeOut(fl->autoscrl_timeout);
		fl->autoscrl_timeout = None;
	}

	if(fl->dblclk_timeout) {
		XtRemoveTimeOut(fl->dblclk_timeout);
		fl->dblclk_timeout = None;
	}

	if(fl->dragging) {
		fl->dragging = False;
		redraw_all(w);
	} else if(fl->num_items && fl->show_contents &&
		((struct file_list_rec*)w)->core.visible) {
		draw_item(w, get_cursor(w), True);
	}
	
	_XmPrimitiveFocusOut(w, evt, NULL, NULL);
}

/*
 * User interaction endpoint routines
 */
static void default_action_handler(Widget w, unsigned int mod_mask)
{
	struct file_list_part *fl = FL_PART(w);

	if(fl->num_items && fl->default_action_cb) {
		unsigned int i = get_cursor(w);
	
	 	struct file_list_sel cbd = {
			.count = 1,
			.names = fl->sel_names,
			.item.name = fl->items[i].name,
			.item.title = fl->items[i].title,
			.item.db_type = fl->items[i].db_type,
			.item.size = fl->items[i].size,
			.item.mode = fl->items[i].mode,
			.item.uid = fl->items[i].uid,
			.item.gid = fl->items[i].gid,
			.item.ctime = fl->items[i].ctime,
			.item.mtime = fl->items[i].mtime,
			.item.is_symlink = fl->items[i].is_symlink,
			.item.icon = fl->items[i].icon_image,
			.item.icon_mask = fl->items[i].icon_mask,
			.item.user_flags = fl->items[i].user_flags
		};
		add_fsize(&cbd.size_total, fl->items[i].size);
				
		XtCallCallbackList(w, fl->default_action_cb, (XtPointer)&cbd);
	}
}

/*
 *  This is just for navigation convenience
 */
static void dir_up(Widget w, XEvent *evt, String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);

	if(fl->dir_up_cb)
		XtCallCallbackList(w, fl->dir_up_cb, (XtPointer)NULL);
}

static void delete(Widget w, XEvent *evt, String *params, Cardinal *nparams)
{
	struct file_list_part *fl = FL_PART(w);

	if(fl->sel_count && fl->delete_cb)
		XtCallCallbackList(w, fl->delete_cb, (XtPointer)NULL);
}

/*
 * Called whenever selection changes. Updates sel_names and sel_count
 * fields of the widget and calls user callbacks.
 */
static void sel_change_handler(Widget w, Boolean initial)
{
	struct file_list_part *fl = FL_PART(w);
	struct file_list_sel cbd = { 0 };
	unsigned int i, count = 0;
	
	for(i = 0; i < fl->num_items; i++) {
		if(fl->items[i].selected) count++;
	}

	if(count) {
		unsigned int j;
		
		if(count > fl->sel_count) {
			char **ptr;

			ptr = realloc(fl->sel_names, sizeof(char*) * count);
			if(!ptr) {
				free(fl->sel_names);
				fl->sel_names = NULL;
				fl->sel_count = 0;
				WARNING(w, "Failed to allocate memory for selection");
				return;
			}
			fl->sel_names = ptr;
		}

		for(i = 0, j = 0; i < fl->num_items; i++) {
			if(fl->items[i].selected) {
				fl->sel_names[j++] = fl->items[i].name;
				draw_item(w, i, True);
			}
		}
	
		fl->sel_count = count;
		file_list_get_selection(w, &cbd);
	} else {
		fl->sel_count = 0;
	}

	if(fl->sel_change_cb) {
		cbd.initial = initial;
		XtCallCallbackList(w, fl->sel_change_cb, &cbd);
	}
}

/*
 * Switches selection highlighting, redraws selected items
 */
void file_list_highlight_selection(Widget w, Boolean on)
{
	struct file_list_part *fl = FL_PART(w);
	
	if(fl->highlight_sel == on) return;
	
	fl->highlight_sel = on;

	if(fl->sel_count) {
		int i;

		for(i = 0; i < fl->num_items; i++) {
			if(fl->items[i].selected)
				draw_item(w, i, True);
		}
	}
}

/*
 * Public routines
 */
int file_list_add(Widget w,
	const struct file_list_item *its, Boolean replace)
{
	struct file_list_part *fl = FL_PART(w);
	struct item_rec tmp = {0};
	unsigned int i = fl->num_items;
	Boolean selected = False;
	Boolean exists = False;

	if(!replace || !(exists = find_item(fl, its->name, &i)) ) {
		/* add new */
		replace = False;
		
		if(fl->num_items + 1 > fl->items_size) {
			void *np = realloc(fl->items,
				sizeof(struct item_rec) * (fl->num_items + LIST_GROW_BY));

			if(!np) return ENOMEM;

			fl->items = np;
			fl->items_size += LIST_GROW_BY;
		}

		memset(&fl->items[i], 0, sizeof(struct item_rec));
	}
	if(exists) selected = fl->items[i].selected;

	tmp.name = strdup(its->name);
	if(!tmp.name) return errno;
	
	if(fl->case_sensitive) {
		tmp.tr_name = tmp.name;
	} else {
		tmp.tr_name = mbs_tolower(its->name);
		if(!tmp.tr_name) {
			free(tmp.name);
			return ENOMEM;
		}
	}
		
	if(fl->shorten){
		char *title = its->title ? its->title : its->name;
		
		/* TBD: ultimately, we'd want to compute the average label length
		 *      from all items and then abbreviate anything longer than that,
		 *      rather than using a fixed length. This requires however all
		 *      label data to be rebuilt, once the average changes to any
		 *      significant extent.	 */
		if(fl->shorten && (mb_strlen(its->title) > fl->shorten)) {
			tmp.title = shorten_mb_string(title, fl->shorten, False);
		} else tmp.title = strdup(title);
	
		if(!tmp.title) {
			free(tmp.name);
			return ENOMEM;
		}
	}

	tmp.size = its->size;
	tmp.ctime = its->ctime;
	tmp.mtime = its->mtime;
	tmp.mode = its->mode;
	tmp.uid = its->uid;
	tmp.gid = its->gid;
	tmp.db_type = its->db_type;
	tmp.user_flags = its->user_flags;
	tmp.icon_image = its->icon;
	tmp.icon_mask = its->icon_mask;
	tmp.is_symlink = its->is_symlink;
	tmp.selected = selected;
	
	/* cache icon dimensions */
	if(its->icon != None) {
		Window root;
		unsigned int foo;
		unsigned int iw, ih;
		XGetGeometry(XtDisplay(w), its->icon, &root,
			(int*)&foo, (int*)&foo, &iw, &ih, &foo, &foo);
		tmp.icon_width = iw;
		tmp.icon_height = ih;
		
		if(fl->icon_width_max < iw) fl->icon_width_max = iw;
		if(fl->icon_height_max < ih) fl->icon_height_max = ih;
	}

	if(!make_labels(w, &tmp)) {
		free(tmp.name);
		if(tmp.title) free(tmp.title);
		return ENOMEM;
	}

	/* finally, merge temporary struct into the array */
	if(replace)
		free_item(&fl->items[i]);
	else
		fl->num_items++;

	memcpy(&fl->items[i], &tmp, sizeof(struct item_rec));

	/* recompute layout and redraw if contents are shown */
	compute_item_extents(w, i);
	
	if(fl->show_contents) {
		Dimension view_width, view_height;
		
		get_view_dimensions(w, True, &view_width, &view_height);
		sort_list(w);
		compute_placement(w, view_width, view_height);
		update_sbar_visibility(w, view_width, view_height);
		get_view_dimensions(w, False, &view_width, &view_height);
		update_sbar_range(w, view_width, view_height);
		redraw_all(w);
	}
	if(selected) sel_change_handler(w, False);

	return 0;
}

int file_list_remove(Widget w, const char *name)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int i;
	Boolean selected;
	
	if(!find_item(fl, name, &i)) return ENOENT;
	
	selected = fl->items[i].selected;
	
	free_item(&fl->items[i]);
	
	if(fl->num_items > 1) {
		memmove(&fl->items[i], &fl->items[i + 1],
			sizeof(struct item_rec) * (fl->num_items - i));
	}
	
	fl->num_items--;
	
	fl->item_width_max[XfCOMPACT] = 0;
	fl->item_width_max[XfDETAILED] = 0;
	memset(fl->field_widths, 0, sizeof(fl->field_widths));
	
	for(i = 0; i < fl->num_items; i++) {
		int j;
		
		if(fl->item_width_max[XfCOMPACT] < fl->items[i].width)
			fl->item_width_max[XfCOMPACT] = fl->items[i].width;
		if(fl->item_width_max[XfDETAILED] < fl->items[i].detail_width)
			fl->item_width_max[XfDETAILED] = fl->items[i].detail_width;
		

		for(j = 0; j < NFIELDS; j++) {
			if(fl->items[i].field_widths[j] > fl->field_widths[j]) {
				fl->field_widths[j] = fl->items[i].field_widths[j];
			}
		}

	}
	
	if(!fl->num_items) {
		fl->cursor = 0;
		fl->ext_position = 0;
		fl->icon_width_max = 0;
		fl->icon_height_max = 0;
		fl->item_height_max = 0;
		fl->text_height_max = 0;
		fl->sel_count = 0;
		memset(fl->item_width_max, 0, sizeof(fl->item_width_max));
		if(fl->sel_names) free(fl->sel_names);
		fl->sel_names = NULL;
	}
	
	/* recompute layout and redraw if contents are shown */
	if(fl->show_contents) {
		Dimension view_width, view_height;
		
		get_view_dimensions(w, True, &view_width, &view_height);
		sort_list(w);
		compute_placement(w, view_width, view_height);
		update_sbar_visibility(w, view_width, view_height);
		get_view_dimensions(w, False, &view_width, &view_height);
		update_sbar_range(w, view_width, view_height);
		redraw_all(w);
	}
	if(selected) sel_change_handler(w, False);	

	return 0;
}

int file_list_select_name(Widget w, const char *name, Boolean add)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int i;
	
	if(!find_item(fl, name, &i)) return ENOENT;

	fl->items[i].selected = True;
	
	draw_item(w, i, True);
	sel_change_handler(w, True);
	
	return 0;
}

int file_list_select_pattern(Widget w, const char *pattern, Boolean add)
{
	struct file_list_part *fl = FL_PART(w);
	Boolean sel_changed = False;
	Boolean matched = False;
	unsigned int i = 0;
	
	for(i = 0; i < fl->num_items; i++) {
		if(!fnmatch(pattern, fl->items[i].name, 0)) {
			fl->items[i].selected = True;
			matched = True;
			sel_changed = True;
		} else {
			if(!add && fl->items[i].selected) {
				fl->items[i].selected = False;
				sel_changed = True;
			}
		}
	}
	
	if(sel_changed) {
		redraw_all(w);
		sel_change_handler(w, True);
	}
	
	return matched ? 0 : ENOENT;
}


void file_list_select_all(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int changed = 0;
	unsigned int i;
	
	for(i = 0; i < fl->num_items; i++) {
		if(!(fl->items[i].selected)) {
			fl->items[i].selected = True;
			draw_item(w, i, True);
			changed++;
		}
	}
	
	if(changed) sel_change_handler(w, True);
}

void file_list_deselect(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int changed = 0;
	unsigned int i;
	
	for(i = 0; i < fl->num_items; i++) {
		if(fl->items[i].selected) {
			fl->items[i].selected = False;
			draw_item(w, i, True);
			changed++;
		}
	}
	
	if(changed) sel_change_handler(w, True);
}

void file_list_invert_selection(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int i;

	for(i = 0; i < fl->num_items; i++) {
		if(fl->items[i].selected)
			fl->items[i].selected = False;
		else
			fl->items[i].selected = True;
		
		draw_item(w, i, True);
	}
	
	if(fl->num_items) sel_change_handler(w, True);
}

void file_list_show_contents(Widget w, Boolean show)
{
	struct file_list_part *fl = FL_PART(w);

	if(show) {
		sort_list(w);
		compute_placement(w, CORE_WIDTH(w), CORE_HEIGHT(w));
	}

	fl->show_contents = show;	
	update_sbar_visibility(w, CORE_WIDTH(w), CORE_HEIGHT(w));
	update_sbar_range(w, CORE_WIDTH(w), CORE_HEIGHT(w));
	redraw_all(w);
}

void file_list_remove_all(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	int i;
	Boolean selected = False;
	
	if(!fl->num_items) return;
	
	for(i = 0; i < fl->num_items; i++) {
		selected |= fl->items[i].selected;
		free_item(&fl->items[i]);
	}

	fl->num_items = 0;
	fl->cursor = 0;
	fl->ext_position = 0;
	fl->icon_width_max = 0;
	fl->icon_height_max = 0;
	fl->item_height_max = 0;
	fl->text_height_max = 0;
	fl->sel_count = 0;
	fl->list_width = 0;
	fl->list_height = 0;
	memset(fl->item_width_max, 0, sizeof(fl->item_width_max));
	memset(fl->field_widths, 0, sizeof(fl->field_widths));
	if(fl->sel_names) free(fl->sel_names);
	fl->sel_names = NULL;
	
	update_sbar_range(w, CORE_WIDTH(w), CORE_HEIGHT(w));
	update_sbar_visibility(w, CORE_WIDTH(w), CORE_HEIGHT(w));
	
	redraw_all(w);
	if(selected) sel_change_handler(w, False);
}

Boolean file_list_get_selection(Widget w, struct file_list_sel *sel)
{
	struct file_list_part *fl = FL_PART(w);
	struct fsize fs = { 0 };
	unsigned int i = 0;
			
	if(fl->sel_count > 1) {
		for(i = 0; i < fl->num_items; i++) {
			if(fl->items[i].selected)
				add_fsize(&fs, fl->items[i].size);
		}

		sel->count = fl->sel_count;
		sel->size_total = fs;
		sel->names = (fl->sel_count) ? fl->sel_names : NULL;

		i = get_cursor(w);

	} else if(fl->sel_count == 1) {
		for(i = 0; i < fl->num_items; i++) {
			if(fl->items[i].selected) break;
		}

		sel->count = 1;
		sel->names = fl->sel_names;
		add_fsize(&sel->size_total, fl->items[i].size);
	} else return False;

	sel->item.name = fl->items[i].name;
	sel->item.title = fl->items[i].title;
	sel->item.db_type = fl->items[i].db_type;
	sel->item.size = fl->items[i].size;
	sel->item.mode = fl->items[i].mode;
	sel->item.uid = fl->items[i].uid;
	sel->item.gid = fl->items[i].gid;
	sel->item.ctime = fl->items[i].ctime;
	sel->item.mtime = fl->items[i].mtime;
	sel->item.is_symlink = fl->items[i].is_symlink;
	sel->item.icon = fl->items[i].icon_image;
	sel->item.icon_mask = fl->items[i].icon_mask;
	sel->item.user_flags = fl->items[i].user_flags;	

	return True;
}

unsigned int file_list_count(Widget w)
{
	struct file_list_part *fl = FL_PART(w);
	return fl->num_items;
}
