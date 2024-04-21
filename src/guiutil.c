/*
 * Copyright (C) 2012-2024 alx@fastestcode.org
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
