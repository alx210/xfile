/*
 * Copyright (C) 2012-2025 alx@fastestcode.org
 * This software is distributed under the terms of the MIT/X license.
 * See the included COPYING file for further information.
 */
 
/*
 * GUI utility functions
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <Xm/Xm.h>
#include <Xm/Protocols.h>
#include <Xm/MwmUtil.h>
#include <X11/Xatom.h>
#ifndef NO_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include "debug.h"

/*
 * Returns size and x/y offsets of the screen the widget is located on.
 */
void get_screen_size(Widget w, int *pwidth, int *pheight, int *px, int *py)
{
	Display *display = XtDisplay(w);
	Screen *scr;
	
	#ifndef NO_XINERAMA
	if(XineramaIsActive(display)){
		Position wx, wy;
		int i, nxis;
		XineramaScreenInfo *xis;
		
		xis = XineramaQueryScreens(display, &nxis);
		while(w && !XtIsShell(w)) w = XtParent(w);
		XtVaGetValues(w, XmNx, &wx, XmNy, &wy, NULL);
		
		for(i = 0; i < nxis; i++){
			if((wx >= xis[i].x_org && wx <= (xis[i].x_org + xis[i].width)) &&
				(wy >= xis[i].y_org && wy <= (xis[i].y_org + xis[i].height)))
				break;
		}
		
		if(i < nxis){
			*pwidth = xis[i].width;
			*pheight = xis[i].height;
			*px = xis[i].x_org;
			*py = xis[i].y_org;
			XFree(xis);
			return;
		}
		XFree(xis);
	}
	#endif /* !NO_XINERAMA */
	
	scr = XtScreen(w);
	*pwidth = XWidthOfScreen(scr);
	*pheight = XHeightOfScreen(scr);
	*px = 0;
	*py = 0;
}


/* Maps a shell after removing PPosition hint */
void map_shell_unpos(Widget wshell)
{
	XSizeHints size_hints;
	long supplied_hints;
	
	dbg_assert(XtIsShell(wshell));

	if(XGetWMNormalHints(XtDisplay(wshell), XtWindow(wshell),
		&size_hints, &supplied_hints)){
		
		size_hints.flags &= ~PPosition;
		XSetWMNormalHints(XtDisplay(wshell), XtWindow(wshell), &size_hints);
	}
	XtMapWidget(wshell);
}

/*
 * Position woverlay over wshell, offset by title-bar/frame border
 */
void place_shell_over(Widget woverlay, Widget wshell)
{
	Display *dpy;
	Window wnd;
	Arg args[4];
	Position x, y;
	unsigned long frame[4] = {8, 0, 16, 0};
	Atom ret_type;
	int ret_fmt;
	unsigned long ret_items;
	unsigned long ret_bytes;
	unsigned char *data;
	static Atom xa_frm_extents = None;
	const char atom_name[] = "_NET_FRAME_EXTENTS";

	
	dpy = XtDisplay(wshell);
	wnd = XtWindow(wshell);
	
	if(xa_frm_extents == None)
		xa_frm_extents = XInternAtom(dpy, atom_name, False);

	
	if(	(xa_frm_extents != None) &&
		(XGetWindowProperty(dpy, wnd, xa_frm_extents, 0, 4,
			False, XA_CARDINAL, &ret_type, &ret_fmt, &ret_items,
			&ret_bytes, &data) == Success) ) {

		if(ret_type == XA_CARDINAL && (ret_items == 4) ) {
			memcpy(frame, data, sizeof(unsigned long) * ret_items);
		}
		XFree(data);
	}

	XtSetArg(args[0], XmNx, &x);
	XtSetArg(args[1], XmNy, &y);
	XtGetValues(wshell, args, 2);
	
	x += frame[0]; y += frame[2];
	
	XtSetArg(args[0], XmNx, x);
	XtSetArg(args[1], XmNy, y);
	XtSetValues(woverlay, args, 2);
}

/* Returns True if shell is mapped */
Boolean is_mapped(Widget wshell)
{
	XWindowAttributes xwatt;

	while(wshell && !XtIsShell(wshell))
		wshell = XtParent(wshell);
	dbg_assert(wshell);
	
	XGetWindowAttributes(XtDisplay(wshell), XtWindow(wshell), &xwatt);

	return (xwatt.map_state == IsUnmapped) ? False : True;

}

/* Focuses the shell specified, de-iconifies it if need be */
void raise_and_focus(Widget w)
{
	static Atom XaNET_ACTIVE_WINDOW = None;
	static Atom XaWM_STATE = None;
	static Atom XaWM_CHANGE_STATE = None;
	Atom ret_type;
	int ret_fmt;
	unsigned long ret_items;
	unsigned long ret_bytes;
	unsigned int *state = NULL;
	XClientMessageEvent evt;
	Display *dpy = XtDisplay(w);

	while(w && !XtIsShell(w)) w = XtParent(w);
	dbg_assert(w);

	if(XaWM_STATE == None){
		XaWM_STATE = XInternAtom(dpy, "WM_STATE", True);
		XaWM_CHANGE_STATE = XInternAtom(dpy, "WM_CHANGE_STATE", True);
		XaNET_ACTIVE_WINDOW = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
	}

	if(XaWM_STATE == None) return;

	if(XGetWindowProperty(dpy, XtWindow(w), XaWM_STATE, 0, 1,
		False, XaWM_STATE, &ret_type, &ret_fmt, &ret_items,
		&ret_bytes, (unsigned char**)&state) != Success) return;

	if(ret_type == XaWM_STATE && ret_fmt && *state == IconicState){
		evt.type = ClientMessage;
		evt.send_event = True;
		evt.message_type = XaWM_CHANGE_STATE;
		evt.display = dpy;
		evt.window = XtWindow(w);
		evt.format = 32;
		evt.data.l[0] = NormalState;
		XSendEvent(dpy, XDefaultRootWindow(dpy), True,
			SubstructureNotifyMask|SubstructureRedirectMask, (XEvent*)&evt);
	}else{
		if(XaNET_ACTIVE_WINDOW){
			evt.type = ClientMessage,
			evt.send_event = True;
			evt.serial = 0;
			evt.display = dpy;
			evt.window = XtWindow(w);
			evt.message_type = XaNET_ACTIVE_WINDOW;
			evt.format = 32;

			XSendEvent(dpy, XDefaultRootWindow(dpy), False,
				SubstructureNotifyMask|SubstructureRedirectMask, (XEvent*)&evt);
		}else{
			XRaiseWindow(dpy,XtWindow(w));
			XSync(dpy, False);
			XSetInputFocus(dpy, XtWindow(w), RevertToParent, CurrentTime);
		}
	}
	XFree((char*)state);
}

/*
 * Adds the WM_DELETE_WINDOW window manager protocol callback to a shell
 */
Boolean add_delete_window_handler(Widget w,
	XtCallbackProc proc, XtPointer closure)
{
	static Atom xa_delete = None;
	
	if(xa_delete == None) {
		xa_delete = XInternAtom(XtDisplay(w), "WM_DELETE_WINDOW", False);
		if(xa_delete == None) return False;
	}
	XmAddWMProtocolCallback(w, xa_delete, proc, closure);
	return True;
}

/* 
 * Builds a masked icon pixmap from xbm data specified.
 */
void create_masked_pixmap(Display *dpy,
	const void *bits, const void *mask_bits,
	unsigned int width, unsigned int height,
	Pixel fg_color, Pixel bg_color,
	Pixmap *image, Pixmap *mask)
{
	Window root;
	int depth, screen;
	Screen *pscreen;
	
	pscreen = XDefaultScreenOfDisplay(dpy);
	screen = XScreenNumberOfScreen(pscreen);
	root = RootWindowOfScreen(pscreen);
	depth = DefaultDepth(dpy, screen);
	
	*image = XCreatePixmapFromBitmapData(dpy, root,
		(char*)bits, width, height, fg_color, bg_color, depth);
	*mask = XCreatePixmapFromBitmapData(dpy, root,
		(char*)mask_bits, width, height, 1, 0, 1);
}

/*
 * Convenience function for setting XmLabel text.
 * psz may be null, in which case empty string is assumed.
 */
void set_label_string(Widget wlabel, const char *psz)
{
	Arg arg[1];
	
	XmString cs = XmStringCreateLocalized(psz ? (char*)psz : "");
	XtSetArg(arg[0], XmNlabelString, cs);
	XtSetValues(wlabel, arg, 1);
	XmStringFree(cs);
}

/*
 * Builds a window manager icon and mask from bitmap data
 * This is invoked with the create_wm_icon macro
 */
void __create_wm_icon(Display *dpy,
	const void *bits, const void *mask_bits,
	unsigned int width, unsigned int height,
	Pixmap *image, Pixmap *mask)
{
	Window root;
	int depth, screen;
	Screen *pscreen;
	Pixel fg_color, bg_color;
	
	pscreen = XDefaultScreenOfDisplay(dpy);
	screen = XScreenNumberOfScreen(pscreen);
	root = RootWindowOfScreen(pscreen);
	depth = DefaultDepth(dpy, screen);
	
	fg_color = BlackPixel(dpy, screen);
	bg_color = WhitePixel(dpy, screen);

	*image = XCreatePixmapFromBitmapData(dpy, root,
		(char*)bits, width, height, fg_color, bg_color, depth);
	*mask = XCreatePixmapFromBitmapData(dpy, root,
		(char*)mask_bits, width, height, 1, 0, 1);
}


/*
 * Retrieves standard motif icon pixmap
 */
Pixmap get_standard_icon(Widget w, const char *name)
{
	Pixel fg;
	Pixel bg;
	Arg args[2];
	
	XtSetArg(args[0], XmNbackground, &bg);
	XtSetArg(args[1], XmNforeground, &fg);
	XtGetValues(w, args, 2);

	return XmGetPixmap(XtScreen(w), (char*)name, fg, bg);
}

/*
 * Returns pixmap dimensions and depth.
 * Depth may be NULL if not needed.
 */
void get_pixmap_info(Display *dpy, Pixmap pixmap,
	unsigned int *rwidth, unsigned int *rheight, unsigned int *rdepth)
{
	unsigned int depth;
	Window wnd;
	int x, y;
	unsigned int bw;
	
	XGetGeometry(dpy, pixmap, &wnd,
		&x, &y, rwidth, rheight, &bw, &depth);

	if(rdepth) *rdepth = depth;
}

/*
 * Scales a pixmap to n times of its original size.
 * Returns an new pixmap on success, None otherwise.
 */
Pixmap scale_pixmap(Display *dpy, Visual *vi,
	Pixmap src_pixmap, unsigned int scale)
{
	unsigned int width;
	unsigned int height;
	unsigned int depth;
	unsigned int cx, cy;
	Pixmap dst_pixmap;
	XImage *xisrc;
	XImage *xidst;

	if(!vi) vi = XDefaultVisual(dpy, DefaultScreen(dpy));
	
	get_pixmap_info(dpy, src_pixmap, &width, &height, &depth);
	
	xisrc = XGetImage(dpy, src_pixmap, 0, 0,
		width, height, AllPlanes, ZPixmap);
	if(!xisrc) return None;
	
	xidst = XCreateImage(dpy, vi, xisrc->depth, ZPixmap, 0, NULL,
		width * scale, height * scale, xisrc->bitmap_pad, 0);
	if(xidst) {
		xidst->data = malloc((width * scale) *
			(height * scale) * (xisrc->bitmap_pad / 8));
		if(!xidst->data) {
			XDestroyImage(xisrc);
			XDestroyImage(xidst);
			return None;
		}
	} else {
		XDestroyImage(xisrc);
		return None;
	}

	for(cy = 0; cy < height; cy++) {
		for(cx = 0; cx < width; cx++) {
			unsigned int dx, dy;
			Pixel px;
			
			px = XGetPixel(xisrc, cx, cy);
			
			for(dy = 0; dy < scale; dy++) {
				for(dx = 0; dx < scale; dx++) {
					XPutPixel(xidst, cx * scale + dx,
						cy * scale + dy, px);
				}
			}
		}
	}
	
	XDestroyImage(xisrc);

	dst_pixmap = XCreatePixmap(dpy, src_pixmap,
		width * scale, height * scale, depth);
	if(dst_pixmap) {
		GC blt_gc = XCreateGC(dpy, dst_pixmap, 0, NULL);

		XPutImage(dpy, dst_pixmap, blt_gc, xidst, 0, 0, 0, 0,
			width * scale, height * scale);

		XFreeGC(dpy, blt_gc);
	}
	XDestroyImage(xidst);
	
	return dst_pixmap;
}
