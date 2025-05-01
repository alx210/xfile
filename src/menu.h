/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */
 
/*
 * Constants data structures and prototypes for menu construction routines.
 */

#ifndef MENU_H
#define MENU_H

/* Menu item types */
enum item_type {
	IT_PUSH,
	IT_TOGGLE,
	IT_RADIO,
	IT_SEPARATOR,
	IT_CASCADE,
	IT_CASCADE_RADIO, /* sets the XmNradioBehavior flag */
	IT_CASCADE_HELP, /* position it to the left */
	IT_END
};

struct menu_item {
	enum item_type type;
	char *name;			/* widget name */
	char *label;		/* item text */
	XtCallbackProc callback; /* activation callback */
	XtPointer cb_data;
};

/* Number of sub-menus (IT_CASCADE elements) a popup can contain */
#define MENU_MAX_LEVELS 4

/* Mnemonic prefix character */
#define MENU_MC_PREFIX '&'

/*
 * This is used by update_context_menu.
 * All dynamic context menu items are XmPushButtons named contextAction#.
 */
struct ctx_menu_item {
	char *label;
	XtCallbackProc callback;
	XtPointer cb_data;
};

/* TBD: Used by update_context_menu to store context data */
struct ctx_menu_data {
	Widget wmenu;
	Widget *witems;
	unsigned int nitems;
};

/* Rendition tag used for highligting the default action in context menus */
#define DEFAULT_ACTION_RTAG "defaultAction"

/*
 * Populates the specified menu bar with pulldown menus.
 * First element in the items array must be an IT_CASCADE*. All items
 * that follow a cascade item until IT_END will belong to the sub-menu
 * associated with that cascade button.
 */
void build_menu_bar(Widget wmenu_bar,
	const struct menu_item *items, unsigned int nitems);


/*
 * Creates a popup menu and returns the widget handle
 */
Widget create_popup(Widget wparent, const char *name,
	const struct menu_item *items, unsigned int nitems);

/*
 * Modifies context sensitive menu adding/updating/replacing the
 * *actions* part according to items. On the initial call, ctx must
 * have been initialized to contain valid popup shell handle, and other
 * fields set to NULL/zero; this function allocates extra data within
 * that is reused across consecutive calls.
 */
void modify_context_menu(struct ctx_menu_data *ctx,
	const struct ctx_menu_item *items, unsigned int nitems,
	unsigned int idefault);

/* Adds items to an existing pulldown menu */
void add_menu_items(Widget wmenu,
	const struct menu_item *items, unsigned int nitems);

/* Returns menu item Widget with the corresponding name */
Widget get_menu_item(Widget wmenu, const char *name);

/* Sets menu item's sensitivity */
void enable_menu_item(Widget wmenu, const char *name, Boolean enable);

#endif /* MENU_H */
