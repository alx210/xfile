/*
 * Application version and 'about' text
 */

#ifndef VERSION_H
#define VERSION_H

#define APP_VER 1
#define APP_REV 0
#ifdef DEBUG
#define APP_BLD "Debug; Built " __DATE__
#else
#define APP_BLD "Built " __DATE__
#endif

#define DESCRIPTION_CS "XFile - File manager for Unix/X11"

#define COPYRIGHT_CS "Copyright (C) 2023-2025 by alx@fastestcode.org\n\n" \
	"This program is distributed under the terms of the X/MIT license.\n" \
	"You may modify and distribute it accordingly.\n" \
	"See the included COPYING file for detailed information."

#endif /* VERSION_H */
