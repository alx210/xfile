/*
 * Copyright (C) 2012-2025 alx@fastestcode.org
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
 * Sets shell's title and icon name using EWMH if available, or normal hints
 * otherwise. Either title or icon_name may be NULL, if no change desired.
 */
void set_shell_title(Widget wshell, const char *title, const char *icon_name);

/*
 * Adds the WM_DELETE_WINDOW window manager protocol callback to a shell.
 * Returns True on success.
 */
Boolean add_delete_window_handler(Widget w,
	XtCallbackProc proc, XtPointer closure);

/*
 * Convenience function for setting XmLabel text.
 * psz may be null, in which case empty string is assumed.
 */
void set_label_string(Widget wlabel, const char *psz);

/* Returns average character width for the ASCII set */
Dimension get_average_char_width(XmRenderTable rt);

/* Builds a window manager icon and mask from bitmap data */
#define create_wm_icon(dpy, name, image, mask) \
	__create_wm_icon(dpy, name##_bits, name##_m_bits,\
	 name##_width, name##_height, image, mask)

void __create_wm_icon(Display*,
	const void *bits, const void *mask_bits,
	unsigned int width, unsigned int height,
	Pixmap *image, Pixmap *mask);

/*
 * Retrieves standard motif icon pixmap
 * Returns XmUNSPECIFIED_PIXMAP if not found
 */
Pixmap get_standard_icon(Widget w, const char *name);

/*
 * Returns pixmap dimensions and depth. Depth may be NULL if not needed.
 */
void get_pixmap_info(Display *dpy, Pixmap pixmap,
	unsigned int *rwidth, unsigned int *rheight, unsigned int *rdepth);

/*
 * Scales a pixmap to n times of its original size.
 * Returns an new pixmap on success, None otherwise.
 */
Pixmap scale_pixmap(Display *dpy, Visual *vi,
	Pixmap src_pixmap, unsigned int n);

#endif /* GUIUTIL_H */
