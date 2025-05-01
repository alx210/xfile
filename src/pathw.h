/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */


#ifndef PATHW_H 
#define PATHW_H

struct path_field_cb_data {
	char *value;
	Boolean accept;
};

#define XfNcompactPath "compactPath"
#define XfCCompactPath "CompactPath"
#define XfNbuttonHeight "buttonHeight"
#define XfCButtonHeight "ButtonHeight"

extern WidgetClass pathFieldWidgetClass;

#define CreatePathField(parent, name, args, nargs) \
	XtCreateWidget(name, pathFieldWidgetClass, parent, args, nargs)
#define CreateManagedPathField(parent, name, args, nargs) \
	XtCreateManagedWidget(name, pathFieldWidgetClass, parent, args, nargs)
#define VaCreatePathField(parent, name, ...) \
	XtVaCreateWidget(name, pathFieldWidgetClass, parent, __VA_ARGS__)
#define VaCreateManagedPathField(parent, name, ...) \
	XtVaCreateManagedWidget(name, pathFieldWidgetClass, parent, __VA_ARGS__)

int path_field_set_location(Widget w, const char *location, Boolean notify);

#endif /* PATHW_H */
