/*
 * Copyright (C) 2022 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */
#ifndef USRTOOL_H
#define USRTOOL_H

struct user_tool_rec {
	char *name;
	char *command;
	char *hist;
};

unsigned int get_user_tools(struct user_tool_rec**);

void user_tool_cbproc(Widget, XtPointer, XtPointer);

#endif /* USRTOOL_H */
