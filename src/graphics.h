/*
 * Copyright (C) 2023 alx@fastestcode.org
 * This software is distributed under the terms of the MIT license.
 * See the included COPYING file for further information.
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

/* Built-in icons (see mkicons.sh) */
#define ICON_FILE "file"
#define ICON_DIR "dir"
#define ICON_NXDIR "nxdir"
#define ICON_MPT "mpt"
#define ICON_MPTI "mpti"
#define ICON_TEXT "text"
#define ICON_BIN "bin"
#define ICON_DLNK "dlink"

/* get_icon_pixmap size constants */
enum icon_size {
	IS_LARGE,
	IS_MEDIUM,
	IS_SMALL,
	IS_TINY,
	_NUM_ICON_SIZES
};

/*
 * Loads an icon image from search path, or built-in if none found.
 * Returns True on success, False otherwise.
 */
Boolean get_icon_pixmap(const char *name,
	enum icon_size size, Pixmap *pixmap, Pixmap *mask);

/*
 * Creates pixmap from XPM data, replacing symbolic colors with 
 * widget's if wui is not NULL. Returns True on success, False otherwise.
 */
Boolean create_ui_pixmap(Widget wui, char **data,
	Pixmap *image, Pixmap *mask);


/* Builds a window manager icon and mask from bitmap data */
#define create_wm_icon(name, image, mask) \
	__create_wm_icon(name##_bits, name##_m_bits,\
	 name##_width, name##_height, image, mask)

void __create_wm_icon(
	const void *bits, const void *mask_bits,
	unsigned int width, unsigned int height,
	Pixmap *image, Pixmap *mask);

#endif /* GRAPHICS_H */
