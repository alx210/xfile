/*
 * Copyright (C) 2019-2025 alx@fastestcode.org
 * This software is distributed under the terms of the MIT license.
 * See the included COPYING file for further information.
 */

/* Motif FileList widget public header */

#ifndef LISTW_H 
#define LISTW_H

#include <Xm/Primitive.h>
#include "fsutil.h"

extern WidgetClass fileListWidgetClass;

struct file_list_item {
	char *name;
	char *title;
	int db_type;
	off_t size;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	time_t ctime;
	time_t mtime;
	Boolean is_symlink;
	Pixmap icon;
	Pixmap icon_mask;
	unsigned int user_flags;	
};

struct file_list_selection {
	Boolean initial; /* False if change is due to update or removal of items */
	unsigned int count;
	char **names;
	struct fsize size_total;
	struct file_list_item item; /* focused item */
};

/* Enumerated resources */
enum { XfASCEND, XfDESCEND };
enum { XfNAME, XfTIME, XfSUFFIX, XfTYPE, XfSIZE };
enum { XfCOMPACT, XfDETAILED };

#define XfRSortOrder "SortOrder"
#define XfRSortDirection "SortDirection"
#define XfRViewMode "ViewMode"

/* Resource names and classes */
#define XfNactivateCallback "defaultActionCallback"
#define XfCActivateCallback "DefaultActionCallback"
#define XfNselectionChangeCallback "selectionChangeCallback"
#define XfCSelectionChangeCallback "SelectionChangeCallback"
#define XfNdirectoryUpCallback "directoryUpCallback"
#define XfCDirectoryUpCallback "DirectoryUpCallback"
#define XfNdeleteCallback "deleteCallback"
#define XfCDeleteCallback "DeleteCallback"
#define XfNdoubleClickInterval XmNdoubleClickInterval
#define XfCDoubleClickInterval XmCDoubleClickInterval
#define XfNsortOrder "sortOrder"
#define XfCSortOrder "SortOrder"
#define XfNsortDirection "sortDirection"
#define XfCSortDirection "SortDirection"
#define XfNverticalScrollBar "verticalScrollBar"
#define XfCVerticalScrollBar "VerticalScrollBar"
#define XfNhorizontalScrollBar "horizontalScrollBar"
#define XfCHorizontalScrollBar "HorizontalScrollBar"
#define XfNmarginWidth "marginWidth"
#define XfCMarginWidth "MarginWidth"
#define XfNmarginHeight "marginHeight"
#define XfCMarginHeight "MarginHeight"
#define XfNhorizontalSpacing "horizontalSpacing"
#define XfCHorizontalSpacing "HorizontalSpacing"
#define XfNverticalSpacing "horizontalSpacing"
#define XfCVerticalSpacing "VerticalSpacing"
#define XfNlabelMargin "labelMargin"
#define XfCLabelMargin "LabelMargin"
#define XfNlabelSpacing "labelSpacing"
#define XfCLabelSpacing "LabelSpacing"
#define XfNviewMode "viewMode"
#define XfCViewMode "ViewMode"
#define XfNoutlineWidth "outlineWidth"
#define XfCOutlineWidth "OutlineWidth"
#define XfNdragOffset "dragOffset"
#define XfCDragOffset "DragOffset"
#define XfNautoScrollSpeed "autoScrollSpeed"
#define XfCAutoScrollSpeed "AutoScrollSpeed"
#define XfNownPrimarySelection "ownPrimarySelection"
#define XfCOwnPrimarySelection "OwnPrimarySelection"
#define XfNselectColor "selectColor"
#define XfCSelectColor "SelectColor"
#define XfNshortenLabels "shortenLabels"
#define XfCShortenLabels "ShortenLabels"
#define XfNsilent "silent"
#define XfCSilent "Silent"
#define XfNlookupTimeOut "lookupTimeout"
#define XfCLookupTimeOut "LookupTimeout"
#define XfNnumberedSort "numberedSort"
#define XfCNumberedSort "NumberedSort"
#define XfNcaseSensitive "caseSensitive"
#define XfCCaseSensitive "CaseSensitive"
#define XfNforegroundThreshold "foregroundThreshold"
#define XfCForegroundThreshold "ForegroundThreshold"


#define CreateFileList(parent, name, args, nargs) \
	XtCreateWidget(name, fileListWidgetClass, parent, args, nargs)
#define CreateManagedFileList(parent, name, args, nargs) \
	XtCreateManagedWidget(name, fileListWidgetClass, parent, args, nargs)
#define VaCreateFileList(parent, name, ...) \
	XtVaCreateWidget(name, fileListWidgetClass, parent, __VA_ARGS__)
#define VaCreateManagedFileList(parent, name, ...) \
	XtVaCreateManagedWidget(name, fileListWidgetClass, parent, __VA_ARGS__)

/*
 * Adds or replaces an item, returns zero on success, errno otherwise.
 */
int file_list_add(Widget, const struct file_list_item*, Boolean replace);

/*
 * Finds and removes name item, returns zero on success, errno otherwise.
 */
int file_list_remove(Widget, const char *name);

/*
 * Finds and selects an item matching the specific name, removes active
 * selection if add is False. Returns zero on success, ENOENT otherwise.
 */
int file_list_select_name(Widget, const char *name, Boolean add);


/*
 * Finds and selects all items matching the pattern, removes active selection
 * if add is False. Returns zero on success, ENOENT otherwise.
 */
int file_list_select_pattern(Widget, const char *pattern, Boolean add);

/*
 * Selects all items, if any.
 */
void file_list_select_all(Widget);

/*
 * Removes active selection.
 */
void file_list_deselect(Widget);

/*
 * Inverts active selection.
 */
void file_list_invert_selection(Widget);

/*
 * Returns number of items in the list
 */
unsigned int file_list_count(Widget);

/*
 * Retrieves current selection data, or NULL if no selection.
 * Returned data must not be modified or freed.
 */
struct file_list_selection* file_list_get_selection(Widget);

/*
 * Makes items in the list visible. Adding and removing items is less
 * computationally expensive, since sorting and layout functions are
 * deferred until items are shown.
 */
void file_list_show_contents(Widget, Boolean show);


/*
 * Retrieves file_list_item struct for the item name specified.
 * Returns True on success.
 */
Boolean file_list_get_item(Widget, const char *name,
	struct file_list_item *ret);

/*
 * Deletes all items
 */
void file_list_remove_all(Widget);

/*
 * Changes selection highlighting state to convey whether
 * primary selection is owned (True) or lost (False)
 */
void file_list_highlight_selection(Widget, Boolean);

#endif /* LISTW_H */
