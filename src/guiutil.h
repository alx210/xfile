/*
 * Copyright (C) 2012-2024 alx@fastestcode.org
 * This software is distributed under the terms of the MIT license.
 * See the included COPYING file for further information.
 */

#ifndef GUIUTIL_H
#define GUIUTIL_H

/* Removes PPosition flag and maps a shell */
void map_shell_unpos(Widget wshell);

/* Position woverlay over wshell, offset by title-bar/frame border */
void place_shell_over(Widget woverlay, Widget wshell);

/* Returns size and x/y offsets of the screen the widget is located on */
void get_screen_size(Widget w, int *pwidth,
	int *pheight, int *px, int *py);

/* Returns True if shell is mapped */
Boolean is_mapped(Widget wshell);

/* Focuses the shell specified, de-iconifies it if need be */
void raise_and_focus(Widget wshell);

/*
 * Adds the WM_DELETE_WINDOW window manager protocol callback to a shell.
 * Returns True on success.
 */
Boolean add_delete_window_handler(Widget w,
	XtCallbackProc proc, XtPointer closure);

/* 
 * Builds a pixmap and a mask from xbm data specified.
 */
void create_masked_pixmap(Display *dpy,
	const void *bits, const void *mask_bits,
	unsigned int width, unsigned int height,
	Pixel fg_color, Pixel bg_color,
	Pixmap *image, Pixmap *mask);

/*
 * Convenience function for setting XmLabel text.
 * psz may be null, in which case empty string is assumed.
 */
void set_label_string(Widget wlabel, const char *psz);

#endif /* GUIUTIL_H */
