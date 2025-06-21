/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * Primary X selection based file copy/move-paste mechanism
 */

#include <stdlib.h>
#include <stdio.h>
#include <Xm/XmP.h>
#include <errno.h>
#include <wchar.h>
#include <wctype.h>
#include "listw.h"
#include "main.h"
#include "const.h"
#include "mbstr.h"
#include "comdlgs.h"
#include "fsproc.h"

struct req_data {
	char *location;
	Boolean move;
};

static Boolean owns_primary = False;

/* Forward declarations */
static void lose_selection_proc(Widget, Atom*);
static Boolean convert_selection_proc(Widget,
	Atom*, Atom*, Atom*, XtPointer*, unsigned long*, int*);
static void requestor_callback(Widget, XtPointer, Atom*,
	Atom*, XtPointer, unsigned long*, int*);

/* Returns True if this instance owns the selection */
Boolean is_selection_owner(void)
{
	Window owner;

	owner = XGetSelectionOwner(app_inst.display, XA_PRIMARY);
	if(owner != None && owner == XtWindow(app_inst.wshell)) return True;
	
	return False;
}

/*
 * Tries and grabs the primary X selection, returns True on success.
 */
Boolean grab_selection(void)
{
	owns_primary = XtOwnSelection(app_inst.wshell, XA_PRIMARY,
		XtLastTimestampProcessed(app_inst.display),
		convert_selection_proc, lose_selection_proc, NULL);

	file_list_highlight_selection(app_inst.wlist, owns_primary);
	
	return owns_primary;
}

void ungrab_selection(void)
{
	if(owns_primary) {
		XtDisownSelection(app_inst.wshell, XA_PRIMARY,
			XtLastTimestampProcessed(app_inst.display));
		owns_primary = False;
	}
	file_list_highlight_selection(app_inst.wlist, False);
}

/*
 * Called on selection ownership change.
 * Redraws selected items, if any, and resets selection ownership state.
 */
static void lose_selection_proc(Widget w, Atom *sel)
{
	owns_primary = False;
	file_list_highlight_selection(app_inst.wlist, False);

	if(app_inst.location) {
		short flags = UIF_DIR;
		struct file_list_selection *cur_sel =
			file_list_get_selection(app_inst.wlist);
		
		if(cur_sel->count) flags |= UIF_SEL;
		if(cur_sel->count == 1) flags |= UIF_SINGLE;
		
		set_ui_sensitivity(flags);
	}
}


/*
 * Primary selection converter.
 * Returns space separated list of selected files, if any, to the requestor.
 */
static Boolean convert_selection_proc(Widget w,
	Atom *sel, Atom *tgt, Atom *type_ret, XtPointer *val_ret,
	unsigned long *len_ret, int *fmt_ret)
{
	static Boolean initial = True;
	static Atom XA_TEXT = None;
	static Atom XA_UTF8_STRING = None;
	static Atom XA_TARGETS = None;
	static Atom XA_FILE_LIST = None;
	struct file_list_selection *cur_sel;

	cur_sel = file_list_get_selection(app_inst.wlist);
	
	if(initial) {
		initial = False;
		
		XA_TEXT = XInternAtom(app_inst.display, "TEXT", False);
		XA_UTF8_STRING = XInternAtom(app_inst.display, "UTF8_STRING", False);
		XA_TARGETS = XInternAtom(app_inst.display, "TARGETS", False);
		XA_FILE_LIST = XInternAtom(app_inst.display, CS_FILE_LIST, False);
	}
	
	if(*tgt == XA_TARGETS) {
		Atom targets[] = {
			XA_STRING, XA_TEXT,
			XA_UTF8_STRING, XA_FILE_LIST
		};
		
		char *data;
		
		data = XtMalloc(sizeof(targets)); /* Xt will XtFree this when done */
		if(!data) return False;

		memcpy(data, targets, sizeof(targets));
		*type_ret = *tgt;
		*fmt_ret = 32;
		*len_ret = sizeof(targets) / sizeof(Atom);
		*val_ret = data;

	} else if(*tgt == XA_TEXT || *tgt == XA_STRING || *tgt == XA_UTF8_STRING) {
		unsigned int i;
		unsigned long len = 0;
		char *data;
		
		for(i = 0; i < cur_sel->count; i++) {
			len += strlen(cur_sel->names[i]) + 1;
		}
		
		data = XtMalloc(len + 1); /* Xt will XtFree this when done */
		if(!data) return False;
		
		data[0] = '\0';
		
		for(i = 0; i <  cur_sel->count; i++) {
			strcat(data, cur_sel->names[i]);
			strcat(data, " ");
		}
		data[len - 1] = '\0';
		
		if(*tgt == XA_TEXT || *tgt == XA_STRING)
			mbs_to_latin1(data, data);
	
		*type_ret = *tgt;
		*fmt_ret = 8;
		*len_ret = strlen(data);
		*val_ret = data;

	} else if(*tgt == XA_FILE_LIST) {
		unsigned int i;
		unsigned long len = 0;
		char *data;
		char *path;
		char *pos;
		
		/* Generate a zero separated list of item names, which is
		 * terminated by a double-zero. The first item in the list
		 * is the full path to the working directory */
		path = realpath(app_inst.location, NULL);
		if(!path) return False;
		
		len = strlen(path) + 1;
		for(i = 0; i < cur_sel->count; i++) {
			len += strlen(cur_sel->names[i]) + 1;
		}
		
		data = XtMalloc(len + 1); /* Xt will XtFree this when done */
		
		strcpy(data, path);
		pos = data + strlen(path) + 1;
		
		for(i = 0; i <  cur_sel->count; i++) {
			strcpy(pos, cur_sel->names[i]);
			pos += strlen(cur_sel->names[i]);
			*pos = '\0';
			pos++;
		}
		*pos = '\0';
	
		*type_ret = *tgt;
		*fmt_ret = 8;
		*len_ret = len;
		*val_ret = data;	
	} else {
		return False;
	}

	return True;
}

/*
 * Gets a file list from primary selection (if any), invoking the
 * requestor_callback routine on success. 
 */
void paste_selection(Boolean move)
{
	static Atom XA_FILE_LIST = None;
	struct req_data *rd;
	
	if(XA_FILE_LIST == None) {
		XA_FILE_LIST = XInternAtom(app_inst.display, CS_FILE_LIST, False);
	}
	
	/* freed in requestor_callback */
	rd = malloc(sizeof(struct req_data));
	rd->location = realpath(app_inst.location, NULL);
	rd->move = move;
	if(!rd->location) {
		free(rd);
		return;
	}
	
	XtGetSelectionValue(app_inst.wshell, XA_PRIMARY, XA_FILE_LIST,
		requestor_callback, (XtPointer)rd,
		XtLastTimestampProcessed(app_inst.display));
}

/*
 * Receives a list of files to copy/move to CWD.
 */
static void requestor_callback(Widget w, XtPointer cdata, Atom *selection,
	Atom *type, XtPointer value, unsigned long *len, int *format)
{
	unsigned int i, n, res;
	char *ptr = NULL;
	char *sb;
	char **list;
	char *dest;
	struct req_data *rd = (struct req_data*)cdata;
	
	if(value == NULL || *len == 0) {
		message_box(app_inst.wshell, MB_WARN, NULL,
			"This action requires one or more items\n"
			"to be selected in another " APP_TITLE " window.");
		goto fail;
	}
	
	/*  count number of entries in the file list */
	ptr = (char*)value;
	
	for(n = 0; ; ) {
		if(ptr[0] == '\0') {
			n++;
			if(ptr[1] == '\0') break;
		}
		ptr++;
	}
	
	/* allocate memory required */
	list = malloc(sizeof(char*) * n);
	dest = get_working_dir();
	if(dest) {
		char *p = realpath(dest, NULL);
		free(dest);
		dest = p;
	}
	if(!list || !dest) {
		message_box(app_inst.wshell, MB_ERROR, NULL,
			"Could not complete requested action due to an error.");
		goto fail;
	}

	/* split the zero separated list into an array of char*s */
	ptr = (char*)value;
	sb = ptr;
	
	for(i = 0 ; ; ) {
		if(ptr[0] == '\0') {

			list[i] = sb;
			i++;
			sb = ptr + 1;

			if(ptr[1] == '\0') break;
		}
		ptr++;
	}
	
	n--; /* zeroth component is the source directory */

	if(!strcmp(list[0], dest)) {
		message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Source and destination are the same.");
		free(list);
		free(dest);
		goto fail;
	}
	
		
	/* these functions are async, and only return errors
	 * on malloc or fork failure */
	if(rd->move)
		res = move_files(list[0], list + 1, n, dest);
	else
		res = copy_files(list[0], list + 1, n, dest);

	if(res) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s",	strerror(res));
	}

	free(list);
	free(dest);
	
	fail:
	
	/* allocated in paste_selection */
	free(rd->location);
	free(rd);
}
