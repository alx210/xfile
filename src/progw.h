/*
 * Copyright (C) 2019-2026 alx@fastestcode.org
 * This software is distributed under the terms of the MIT license.
 * See the included COPYING file for further information.
 */

/* Motif Progress widget public header */

#ifndef PROGW_H 
#define PROGW_H

extern WidgetClass progressWidgetClass;

#define CreateProgress(parent, name, args, nargs) \
	XtCreateWidget(name, progressWidgetClass, parent, args, nargs)
#define CreateManagedProgress(parent, name, args, nargs) \
	XtCreateManagedWidget(name, progressWidgetClass, parent, args, nargs)
#define VaCreateProgress(parent, name, ...) \
	XtVaCreateWidget(name, progressWidgetClass, parent, __VA_ARGS__)
#define VaCreateManagedProgress(parent, name, ...) \
	XtVaCreateManagedWidget(name, progressWidgetClass, parent, __VA_ARGS__)

#define PROG_INTERMEDIATE (-1)

void ProgressSetValue(Widget, int);

#endif /* PROGW_H */
