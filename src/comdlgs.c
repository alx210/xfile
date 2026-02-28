/*
 * Copyright (C) 2023-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * Implements common dialogs and message boxes
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <Xm/Xm.h>
#include <Xm/MessageB.h>
#include <Xm/FileSB.h>
#include <Xm/SelectioB.h>
#include <Xm/TextF.h>
#include <Xm/List.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/MwmUtil.h>
#include "comdlgs.h"
#include "const.h"
#include "main.h"
#include "version.h"
#include "guiutil.h"
#include "path.h"
#include "debug.h"


/* Local prototypes */
static void blocking_msgbox_cb(Widget w, XtPointer client, XtPointer call);
static void msgbox_cb(Widget w, XtPointer client, XtPointer call);
static void input_dlg_cb(Widget w, XtPointer client, XtPointer call);
static void input_modify_cb(Widget, XtPointer client, XtPointer call);
static void dir_history_cb(Widget w, XtPointer client, XtPointer call);
static Boolean is_blank(const char *sz);
static char* get_history_fqn(const char *title);
static Boolean load_history(Widget wlist, const char *name);
static void store_history(Widget wlist, const char *name);
static void msgbox_delete_cb(Widget w, XtPointer, XtPointer);
static void input_dlg_delete_cb(Widget w, XtPointer, XtPointer);

#define HISTORY_VISIBLE_MAX	3

struct input_dlg_data {
	int flags;
	char *string;
	Widget whistory;
	Boolean has_history;
	Boolean valid;
	Boolean done;
};

/*
 * Displays a modal message dialog.
 */
enum mb_result message_box(Widget wparent, enum mb_type type,
	const char *msg_title, const char *msg_str)
{
	Widget wbox = 0;
	XmString ok_text = NULL;
	XmString cancel_text = NULL;
	XmString extra_text = NULL;
	XmString msg_text;
	XmString title;
	Arg args[11];
	int i = 0;
	enum mb_result result = _MBR_NVALUES;
	Boolean blocking = False;
	char *class = (type == MB_NOTIFY || type == MB_ERROR) ?
		"messageDialog" : "confirmationDialog";

	dbg_assert(wparent);
	
	if(!msg_title) msg_title = APP_TITLE;

	title = XmStringCreateLocalized((char*)msg_title);
	msg_text = XmStringCreateLocalized((char*)msg_str);
	
	i = 0;
	XtSetArg(args[i], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); i++;
	XtSetArg(args[i], XmNnoResize, True); i++;
	XtSetArg(args[i], XmNdialogTitle, title); i++;
	XtSetArg(args[i], XmNautoUnmanage, False); i++;

	wbox = XmCreateMessageDialog(wparent, class, args, i);
	
	i = 0;
	
	switch(type){
		case MB_CONFIRM:
		blocking = True;
		ok_text = XmStringCreateLocalized("OK");
		cancel_text=XmStringCreateLocalized("Cancel");
		XtSetArg(args[i], XmNdialogType, XmDIALOG_WARNING); i++;
		XtSetArg(args[i], XmNokLabelString, ok_text); i++;
		XtSetArg(args[i], XmNcancelLabelString, cancel_text); i++;
		XtUnmanageChild(XmMessageBoxGetChild(wbox, XmDIALOG_HELP_BUTTON));	
		break;
		
		case MB_QUESTION:
		blocking = True;
		ok_text = XmStringCreateLocalized("Yes");
		cancel_text = XmStringCreateLocalized("No");
		XtSetArg(args[i], XmNdialogType, XmDIALOG_QUESTION); i++;
		XtSetArg(args[i], XmNokLabelString, ok_text); i++;
		XtSetArg(args[i], XmNcancelLabelString, cancel_text); i++;
		XtUnmanageChild(XmMessageBoxGetChild(wbox, XmDIALOG_HELP_BUTTON));
		break;
		
		case MB_CQUESTION:
		blocking = True;
		ok_text = XmStringCreateLocalized("Yes");
		cancel_text = XmStringCreateLocalized("No");
		extra_text=XmStringCreateLocalized("Cancel");
		XtSetArg(args[i], XmNdialogType, XmDIALOG_QUESTION); i++;
		XtSetArg(args[i], XmNokLabelString, ok_text); i++;
		XtSetArg(args[i], XmNcancelLabelString, cancel_text); i++;
		XtSetArg(args[i], XmNhelpLabelString, extra_text); i++;
		break;
		
		case MB_NOTIFY:
		blocking = True;
		case MB_NOTIFY_NB:
		ok_text = XmStringCreateLocalized("OK");
		XtSetArg(args[i], XmNdialogType, XmDIALOG_INFORMATION); i++;
		XtSetArg(args[i], XmNokLabelString, ok_text); i++;
		XtUnmanageChild(XmMessageBoxGetChild(wbox, XmDIALOG_CANCEL_BUTTON));
		XtUnmanageChild(XmMessageBoxGetChild(wbox, XmDIALOG_HELP_BUTTON));
		break;

		case MB_WARN:
		blocking = True;
		case MB_WARN_NB:
		ok_text = XmStringCreateLocalized("OK");
		XtSetArg(args[i], XmNdialogType, XmDIALOG_WARNING); i++;
		XtSetArg(args[i], XmNokLabelString, ok_text); i++;
		XtUnmanageChild(XmMessageBoxGetChild(wbox, XmDIALOG_CANCEL_BUTTON));
		XtUnmanageChild(XmMessageBoxGetChild(wbox, XmDIALOG_HELP_BUTTON));
		break;
		
		case MB_ERROR:
		blocking = True;
		case MB_ERROR_NB:
		ok_text = XmStringCreateLocalized("Dismiss");
		XtSetArg(args[i], XmNdialogType, XmDIALOG_ERROR); i++;
		XtSetArg(args[i], XmNokLabelString, ok_text); i++;
		XtUnmanageChild(XmMessageBoxGetChild(wbox, XmDIALOG_CANCEL_BUTTON));
		XtUnmanageChild(XmMessageBoxGetChild(wbox, XmDIALOG_HELP_BUTTON));
		break;
	};
			
	XtSetArg(args[i], XmNmessageString, msg_text); i++;
	XtSetValues(wbox, args, i);

	XmStringFree(title);
	XmStringFree(ok_text);
	XmStringFree(msg_text);
	if(cancel_text) XmStringFree(cancel_text);
	if(extra_text) XmStringFree(extra_text);

	if(blocking){
		XtAddCallback(wbox, XmNokCallback,
			blocking_msgbox_cb, (XtPointer)&result);
			
		XtAddCallback(wbox, XmNcancelCallback,
			blocking_msgbox_cb, (XtPointer)&result);
			
		XtAddCallback(wbox, XmNhelpCallback,
			blocking_msgbox_cb, (XtPointer)&result);
		
		add_delete_window_handler(XtParent(wbox),
			msgbox_delete_cb, &result);
	} else {
		XtAddCallback(wbox, XmNokCallback,
			msgbox_cb, (XtPointer)&result);
	}

	/* we need to handle WM_DELETE_WINDOW for blocking dialogs ourselves */
	XtSetArg(args[0], XmNdeleteResponse, blocking ? XmDO_NOTHING : XmDESTROY);
	XtSetValues(XtParent(wbox), args, 1);
	
	XtManageChild(wbox);

	if(blocking){
		while(result == _MBR_NVALUES)
			XtAppProcessEvent(XtWidgetToApplicationContext(wparent), XtIMAll);
		
		XtDestroyWidget(wbox);
		XSync(XtDisplay(wparent), False);
		XmUpdateDisplay(wparent);
	
		/* since WM_DELETE_WINDOW handler always sets MBR_CANCEL */
		if(type != MB_CQUESTION && result == MBR_CANCEL)
			result = MBR_DECLINE;

		return result;
	}
	
	return 0;
}

/*
 * Processes printf format string and arguments and calls message_box
 */
enum mb_result va_message_box(Widget parent, enum mb_type type,
	const char *msg_title, const char *msg_fmt, ...)
{
	enum mb_result res;
	va_list ap;
	char *msg_buf;
	size_t n = 0;
	
	va_start(ap, msg_fmt);
	n = vsnprintf(NULL, 0, msg_fmt, ap) + 1;
	msg_buf = malloc(n + 1);
	va_end(ap);

	va_start(ap, msg_fmt);
	vsnprintf(msg_buf, n, msg_fmt, ap);
	va_end(ap);

	res = message_box(parent, type, msg_title, msg_buf);

	free(msg_buf);	
	return res;
}

/* Blocking message_box response callback */
static void blocking_msgbox_cb(Widget w, XtPointer client, XtPointer call)
{
	XmAnyCallbackStruct *cbs = (XmAnyCallbackStruct*)call;
	enum mb_result *res = (enum mb_result*)client;

	if(res){
		switch(cbs->reason){
			case XmCR_OK:
			*res = MBR_CONFIRM;
			break;
			case XmCR_CANCEL:
			*res = MBR_DECLINE;
			break;
			case XmCR_HELP:
			*res = MBR_CANCEL;
			break;
		}
	}
}

/* WM_DELETE_WINDOW handler for blocking message boxes */
static void msgbox_delete_cb(Widget w, XtPointer client, XtPointer call)
{
	enum mb_result *res = (enum mb_result*)client;
	
	*res = MBR_CANCEL;
}

/* Non-blocking dialog response handler; just destroys the widget */
static void msgbox_cb(Widget w, XtPointer client, XtPointer call)
{
	XtDestroyWidget(w);
}

/*
 * Displays a blocking string input dialog.
 * Returns a valid string or NULL if cancelled.
 */
char* input_string_dlg(Widget wparent, const char *title,
	const char *msg_str, const char *init_str, const char *context, int flags)
{
	Widget wdlg;
	Arg arg[8];
	char *token;
	XmString xm_label_string;
	Widget wlabel;
	Widget wtext;
	Cardinal i = 0;
	/* Reset text field's Home/End translations to defaults, since the
	 * selection box widget overrides them to control the list above,
	 * which is rather unexpected and not very useful either */
	static char alt_tt_src[] = 
		":s <Key>osfEndLine: end-of-line(extend)\n"
		":s <Key>osfBeginLine: beginning-of-line(extend)\n"
		":<Key>osfEndLine: end-of-line()\n"
		":<Key>osfBeginLine: beginning-of-line()\n";
	static XtTranslations alt_tt = NULL;

	struct input_dlg_data idd = {flags, NULL, None, False, False, False };
		
	XtSetArg(arg[i], XmNtitle, title ? title : APP_TITLE); i++;
	XtSetArg(arg[i], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); i++;
	XtSetArg(arg[i], XmNautoUnmanage, False); i++;
	
	if(context)
		wdlg = XmCreateSelectionDialog(wparent, "promptDialog", arg, i);
	else
		wdlg = XmCreatePromptDialog(wparent, "promptDialog", arg, i);
	
	XtSetArg(arg[0], XmNdeleteResponse, XmDO_NOTHING);
	XtSetValues(XtParent(wdlg), arg, 1);
	
	add_delete_window_handler(XtParent(wdlg),
		input_dlg_delete_cb, (XtPointer) &idd);
		
	XtUnmanageChild(XmSelectionBoxGetChild(wdlg, XmDIALOG_HELP_BUTTON));

	wtext = XmSelectionBoxGetChild(wdlg, XmDIALOG_TEXT);
	if(!alt_tt) alt_tt = XtParseTranslationTable(alt_tt_src);
	if(alt_tt) XtOverrideTranslations(wtext, alt_tt);
	
	XtAddCallback(wdlg, XmNokCallback, input_dlg_cb, (XtPointer)&idd);
	XtAddCallback(wdlg, XmNcancelCallback, input_dlg_cb, (XtPointer)&idd);
	XtAddCallback(wtext, XmNmodifyVerifyCallback,
		&input_modify_cb, (XtPointer)(long)flags);

	if(context) {
		XtUnmanageChild(XmSelectionBoxGetChild(wdlg, XmDIALOG_SELECTION_LABEL));
		XtUnmanageChild(XmSelectionBoxGetChild(wdlg, XmDIALOG_APPLY_BUTTON));
		wlabel = XmSelectionBoxGetChild(wdlg, XmDIALOG_LIST_LABEL);
		idd.whistory = XmSelectionBoxGetChild(wdlg, XmDIALOG_LIST);

		idd.has_history = load_history(idd.whistory, context);
		if(!idd.has_history) {
			XmString xms = XmStringCreateLocalized("No history available");
			XmListAddItem(idd.whistory, xms, 0);
			XtSetSensitive(idd.whistory, False);
			XmStringFree(xms);
			
			XtSetArg(arg[0], XmNvisibleItemCount, 1);
		} else {
			XtSetArg(arg[0], XmNvisibleItemCount, HISTORY_VISIBLE_MAX);
		}
		XtSetValues(idd.whistory, arg, 1);
	} else {
		wlabel = XmSelectionBoxGetChild(wdlg, XmDIALOG_SELECTION_LABEL);
	}
		
	xm_label_string = XmStringCreateLocalized((String)msg_str);
	XtSetArg(arg[0], XmNlabelString, xm_label_string);
	XtSetValues(wlabel, arg, 1);
	XmStringFree(xm_label_string);
	
	i = 0;
	XtSetArg(arg[i], XmNvalue, init_str); i++;
	XtSetArg(arg[i], XmNpendingDelete, True); i++;
	XtSetValues(wtext, arg, i);

	XtManageChild(wdlg);
	
	if(init_str && (flags & ISF_PRESELECT)) {
		if(flags & ISF_FILENAME) {
			/* preselect file title sans extension */
			token = strrchr(init_str, '.');
			
			if(token){
				XmTextFieldSetSelection(wtext,0,
					strlen(init_str) - strlen(token),
					XtLastTimestampProcessed(XtDisplay(wdlg)));
			}
		} else {
			XmTextFieldSetSelection(wtext, 0, strlen(init_str),
				XtLastTimestampProcessed(XtDisplay(wdlg)));
		}
	}
	
	while(!idd.done){
		XtAppProcessEvent(XtWidgetToApplicationContext(wdlg), XtIMAll);
	}
	
	XtDestroyWidget(wdlg);
	XSync(XtDisplay(wparent), False);
	XmUpdateDisplay(wparent);
	
	if(context && idd.valid && strlen(idd.string))
		store_history(idd.whistory, context);

	return idd.valid ? idd.string : NULL;
}

/*
 * Verification callback for string input.
 */
static void input_modify_cb(Widget w, XtPointer client, XtPointer call)
{
	XmTextVerifyCallbackStruct *vcs = (XmTextVerifyCallbackStruct*)call;
	int flags = (long)client;
	
	if(vcs->reason == XmCR_MODIFYING_TEXT_VALUE && vcs->text->ptr) {
		if((flags & ISF_NOSLASH) && strchr(vcs->text->ptr,'/'))
			vcs->doit=False;
	}
}

/*
 * Displays a blocking directory selection dialog.
 * Returns a valid path name or NULL if selection was cancelled.
 * If a valid path name is returned it must be freed by the caller.
 */
char* dir_select_dlg(Widget wparent, const char *title,
	const char *init_path, const char *context)
{
	Widget wdlg;
	Arg arg[8];
	int i = 0;
	XmString xm_init_path = NULL;
	struct input_dlg_data idd = { 0, NULL, None, False, False, False };

	if(!init_path) init_path = getenv("HOME");
	if(init_path)
		xm_init_path = XmStringCreateLocalized((String)init_path);

	XtSetArg(arg[i], XmNfileTypeMask, XmFILE_DIRECTORY); i++;
	XtSetArg(arg[i], XmNpathMode, XmPATH_MODE_FULL); i++;
	XtSetArg(arg[i], XmNdirectory, xm_init_path); i++;
	XtSetArg(arg[i], XmNtitle, title); i++;
	XtSetArg(arg[i], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); i++;
	XtSetArg(arg[i], XmNautoUnmanage, False); i++;

	wdlg = XmCreateFileSelectionDialog(wparent,
		"directorySelectionDialog", arg, i);
	
	if(xm_init_path) XmStringFree(xm_init_path);
	XtUnmanageChild(XmFileSelectionBoxGetChild(wdlg, XmDIALOG_LIST_LABEL));
	XtUnmanageChild(XtParent(
		XmFileSelectionBoxGetChild(wdlg,XmDIALOG_LIST)));
	XtAddCallback(wdlg, XmNokCallback, input_dlg_cb,(XtPointer)&idd);
	XtAddCallback(wdlg, XmNcancelCallback, input_dlg_cb, (XtPointer)&idd);
	XtUnmanageChild(XmFileSelectionBoxGetChild(wdlg, XmDIALOG_HELP_BUTTON));
	
	if(context) {
		i = 0;

		XtSetArg(arg[i], XmNvisibleItemCount, HISTORY_VISIBLE_MAX); i++;
		XtSetArg(arg[i], XmNselectionPolicy, XmSINGLE_SELECT); i++;
		idd.whistory = XmCreateScrolledList(wdlg, "history", arg, i);
		XtAddCallback(idd.whistory, XmNdefaultActionCallback,
			dir_history_cb, (XtPointer)wdlg);
		XtAddCallback(idd.whistory, XmNsingleSelectionCallback,
			dir_history_cb, (XtPointer)wdlg);
		
		idd.has_history = load_history(idd.whistory, context);
		if(!idd.has_history) {
			XmString xms = XmStringCreateLocalized("No history available");
			XmListAddItem(idd.whistory, xms, 0);
			XtSetArg(arg[0], XmNvisibleItemCount, 1);
			XtSetValues(idd.whistory, arg, 1);
			XtSetSensitive(idd.whistory, False);
		}
		XtManageChild(idd.whistory);
	}
	
	XtSetArg(arg[0], XmNdeleteResponse, XmDO_NOTHING);
	XtSetValues(XtParent(wdlg), arg, 1);
	
	add_delete_window_handler(XtParent(wdlg),
		input_dlg_delete_cb, (XtPointer) &idd);
	
	XtManageChild(wdlg);

	while(!idd.done){
		XtAppProcessEvent(XtWidgetToApplicationContext(wdlg), XtIMAll);
	}

	XtDestroyWidget(wdlg);
	XSync(XtDisplay(wparent), False);
	XmUpdateDisplay(wparent);
	
	if(context && idd.has_history)
		store_history(idd.whistory, context);

	return idd.valid ? idd.string : NULL;
}


/*
 * Directory selection dialog history list activation callback
 */
static void dir_history_cb(Widget w, XtPointer client, XtPointer call)
{
	XmListCallbackStruct *cbs = (XmListCallbackStruct*)call;
	Widget wdlg = (Widget)client;
	static XmString wildcard = NULL;
	XmString search;
	char *sz;
	int err_code = 0;
	struct stat st;
	
	if(cbs->reason == XmCR_SINGLE_SELECT) {
		if(!cbs->selected_item_count) /* reselect last */
			XmListSelectPos(w, cbs->item_position, False);
	} else if(cbs->reason != XmCR_DEFAULT_ACTION) return;
	
	sz = XmStringUnparse(cbs->item, NULL, XmMULTIBYTE_TEXT,
			XmMULTIBYTE_TEXT, NULL, 0, XmOUTPUT_ALL);
	
	if(stat(sz, &st)) {
		err_code = errno;
	} else if(S_ISDIR(st.st_mode) &&
		!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
		err_code = EPERM;
	} else if(! (S_ISDIR(st.st_mode)) ) {
		err_code = ENOTDIR;
	}
	
	if(err_code) {
		if(va_message_box(wdlg, MB_QUESTION, "Location Error",
			"Cannot stat \'%s\'.\n%s.\n"
			"Should this entry be removed from the list?",
			sz, strerror(err_code)) == MBR_CONFIRM) {
				XmListDeletePos(w, cbs->item_position);
			}
		free(sz);
		return;
	}
	free(sz);
	
	if(!wildcard)
		wildcard = XmStringCreateLocalized("/*");

	search = XmStringConcat(cbs->item, wildcard);
	
	XmFileSelectionDoSearch(wdlg, search);
	XmStringFree(search);
}

/*
 * Called by directory selection and string input dialogs.
 */
static void input_dlg_cb(Widget w, XtPointer client, XtPointer call)
{
	XmFileSelectionBoxCallbackStruct *fscb =
		(XmFileSelectionBoxCallbackStruct*)call;
	struct input_dlg_data *idd= (struct input_dlg_data*)client;

	if(fscb->reason == XmCR_CANCEL || fscb->reason == XmCR_NO_MATCH){
		idd->valid = False;
		idd->done = True;
	} else if(fscb->reason == XmCR_OK) {
		char *str;

		if(idd->whistory && !XmStringEmpty(fscb->value)) {
			int *pos_list;
			int count;
			
			/* Remove the "No history" text from the list */
			if(!idd->has_history) {
				XmListDeletePos(idd->whistory, 1);
				idd->has_history = True;
			}
			
			/* Check if entry being added is a duplicate, move
			 * it up instead of adding if that's the case */
			if(XmListGetMatchPos(idd->whistory,
				fscb->value, &pos_list, &count)) {
				XmListDeletePositions(idd->whistory, pos_list, count);
			}
			
			XmListAddItem(idd->whistory, fscb->value, 1);
		}

		str = XmStringUnparse(fscb->value, NULL, XmMULTIBYTE_TEXT,
			XmMULTIBYTE_TEXT, NULL, 0, XmOUTPUT_ALL);
		if(str[0] != '\0' || (idd->flags & ISF_ALLOWEMPTY)) {
			idd->string = strdup(str);
			idd->valid = True;
		} else {
			idd->valid = False;
		}
		idd->done = True;
		XtFree(str);
	}
}

/* Input and directory selection dialog WM_DELETE_WINDOW handler */
static void input_dlg_delete_cb(Widget w, XtPointer client, XtPointer call)
{
	struct input_dlg_data *idd= (struct input_dlg_data*)client;

	idd->valid = False;
	idd->done = True;
}

/* Returns True if the string is all blank characters */
static Boolean is_blank(const char *sz)
{
	char *p = (char*)sz;
	
	while(*p) {
		if(*p != ' ' && *p != '\t' && *p != '\n') return False;
		p++;
	}
	return True;
}

/* Given file title, returns a FQN: ~/.xfile/history/<title>
 * Caller is responsible for freeing the memory */
static char* get_history_fqn(const char *title)
{
	static char *path = NULL;
	static Boolean first_try = True;
	char *fqn = NULL;
	int rv;
	struct stat st;
	
	if(!path) {
		char *home = getenv("HOME");
		if(!home) return NULL;

		path = build_path(NULL, home, HOME_SUBDIR, HIST_SUBDIR, NULL);
		if(!path) return NULL;
	}
	
	if(lstat(path, &st)) {
		if(errno == ENOENT) {
			rv = create_hier(path, (S_IRUSR | S_IWUSR | S_IXUSR));
			if(rv && first_try) {
				stderr_msg("Failed to create \'%s\'. %s\n",
					path, strerror(rv));
				first_try = False;
			}
		} else if(first_try) {
			stderr_msg("Cannot access \'%s\'. %s.\n",
				path, strerror(errno));
			first_try = False;
		}
	} else if(S_ISDIR(st.st_mode)) {
		fqn = build_path(NULL, path, title, NULL);
	} else if(first_try) {
		stderr_msg("\'%s\' exists, but is not a directory.\n", path);
		first_try = False;
	}
	return fqn;
}

/* 
 * Loads strings from file in ~/.xfile/history/<name> into the list widget.
 * Returns True if any strings were added to the list, False otherwise
 */
static Boolean load_history(Widget wlist, const char *name)
{
	struct stat st;
	char *fqn = get_history_fqn(name);
	char *buf;
	FILE *file;
	int count = 0;

	if(!fqn) return False;

	if(stat(fqn, &st) || !st.st_size || !(file = fopen(fqn, "r"))) {
		free(fqn);
		return False;
	}
	
	buf = malloc(st.st_size + 2);
	if(buf) {
		size_t len = fread(buf, 1, (size_t)st.st_size, file);
		if(len) {
			char *ps, *p;

			buf[len] = '\n';
			buf[len + 1] = '\0';
			
			ps = p = buf;
		
			while(*p) {

				if(*p == '\n') {
					*p = '\0';
					if((p - ps) && !is_blank(ps)) {
						XmString xms = XmStringCreateLocalized(ps);

						if(!XmListItemExists(wlist, xms))
							XmListAddItem(wlist, xms, 0);

						XmStringFree(xms);
						count++;

						if(count == app_res.history_max) break;
					}
					ps = p + 1;
				}
				p++;
			}
			
		}
		free(buf);
	}
	
	fclose(file);
	free(fqn);
	
	return (count > 0) ? True : False;	
}

/* Stores items from a list widget to a file in ~/.xfile/history/<name> */
static void store_history(Widget wlist, const char *name)
{
	char *fqn = get_history_fqn(name);
	FILE *file;
	XmStringTable items = NULL;
	int i, count;
	Arg args[2];

	if(!fqn) return;
	
	XtSetArg(args[0], XmNitemCount, &count);
	XtSetArg(args[1], XmNitems, &items);
	XtGetValues(wlist, args, 2);
	
	file = fopen(fqn, "w");
	free(fqn);

	if(!file) return;
	
	for(i = 0; i < count; i++) {
		char *sz;

		sz = XmStringUnparse(items[i], NULL, XmMULTIBYTE_TEXT,
			XmMULTIBYTE_TEXT, NULL, 0, XmOUTPUT_ALL);
		if(!sz) continue;

		fputs(sz, file);
		fputc('\n', file);
		free(sz);
	}
	
	fclose(file);	
}
