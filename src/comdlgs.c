/*
 * Copyright (C) 2023 alx@fastestcode.org
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
#include <Xm/Xm.h>
#include <Xm/MessageB.h>
#include <Xm/FileSB.h>
#include <Xm/SelectioB.h>
#include <Xm/TextF.h>
#include <Xm/MwmUtil.h>
#include "comdlgs.h"
#include "const.h"
#include "version.h"
#include "guiutil.h"
#include "debug.h"
#include "memdb.h" /* must be the last header */

/* Local prototypes */
static void msgbox_btn_cb(Widget w, XtPointer client, XtPointer call);
static void input_dlg_cb(Widget w, XtPointer client, XtPointer call);
static void input_modify_cb(Widget, XtPointer client, XtPointer call);
static void msgbox_popup_cb(Widget w, XtPointer client, XtPointer call);

struct input_dlg_data {
	int flags;
	char *string;
	Boolean valid;
	Boolean done;
};

/*
 * Displays a modal message dialog.
 */
enum mb_result message_box(Widget parent, enum mb_type type,
	const char *msg_title, const char *msg_str)
{
	Widget wbox = 0;
	XmString ok_text = NULL;
	XmString cancel_text = NULL;
	XmString extra_text = NULL;
	XmString msg_text;
	XmString title;
	Arg args[11];
	int i=0;
	enum mb_result result = _MBR_NVALUES;
	Boolean blocking=False;
	XWindowAttributes xwatt;
	Widget parent_shell;
	char *class = (type == MB_NOTIFY || type == MB_ERROR) ?
		"messageDialog" : "confirmationDialog";

	dbg_assert(parent);
	
	if(!msg_title){
		char *sz=NULL;
		XtSetArg(args[0],XmNtitle,&sz);
		XtGetValues(parent,args,1);
		title = XmStringCreateLocalized(sz);
	}else{
		title = XmStringCreateLocalized((char*)msg_title);
	}
	msg_text = XmStringCreateLocalized((char*)msg_str);
	
	wbox = XmCreateMessageDialog(parent, class, NULL, 0);
	
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
	XtSetArg(args[i], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); i++;
	XtSetArg(args[i], XmNnoResize, True); i++;
	XtSetArg(args[i], XmNdialogTitle, title); i++;
	XtSetValues(wbox, args, i);
	i=0;
	XtSetArg(args[i], XmNdeleteResponse, XmDESTROY); i++;
	XtSetArg(args[i], XmNmwmFunctions, MWM_FUNC_MOVE); i++;
	XtSetValues(XtParent(wbox), args, i);

	XmStringFree(title);
	XmStringFree(ok_text);
	XmStringFree(msg_text);
	if(cancel_text) XmStringFree(cancel_text);
	if(extra_text) XmStringFree(extra_text);
	
	if(blocking){
		XtAddCallback(wbox, XmNokCallback,
			msgbox_btn_cb, (XtPointer)&result);
		XtAddCallback(wbox, XmNcancelCallback,
			msgbox_btn_cb, (XtPointer)&result);
		XtAddCallback(wbox, XmNhelpCallback,
			msgbox_btn_cb, (XtPointer)&result);
	}
	
	parent_shell = parent;
	while(!XtIsShell(parent_shell)) parent_shell = XtParent(parent_shell);
	XGetWindowAttributes(XtDisplay(parent_shell),
		XtWindow(parent_shell), &xwatt);

	if(xwatt.map_state == IsUnmapped){
		/* this gets the dialog centered on the screen */
		XtAddCallback(XtParent(wbox), XmNpopupCallback, msgbox_popup_cb,
			(XtPointer)wbox);
	}

	XtManageChild(wbox);
	XmUpdateDisplay(wbox);
	if(blocking){
		while(result == _MBR_NVALUES)
			XtAppProcessEvent(XtWidgetToApplicationContext(wbox), XtIMAll);
		return result;
	}
	return True;
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

/* message_box response callback */
static void msgbox_btn_cb(Widget w, XtPointer client, XtPointer call)
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
	XtDestroyWidget(w);
}

/*
 * Manages message box position if parent isn't mapped
 */
static void msgbox_popup_cb(Widget w, XtPointer client, XtPointer call)
{
	int sw, sh, sx, sy;
	Dimension mw, mh;
	Arg args[2];
	Widget wbox = (Widget)client;

	XtRemoveCallback(w, XmNpopupCallback, msgbox_popup_cb, client);

	XtSetArg(args[0], XmNwidth, &mw);
	XtSetArg(args[1], XmNheight, &mh);
	XtGetValues(wbox, args, 2);

	get_screen_size(wbox, &sw, &sh, &sx, &sy);

	XtSetArg(args[0], XmNx, (sx + (sw - mw) / 2) );
	XtSetArg(args[1], XmNy, (sy + (sh - mh) / 2) );
	XtSetValues(wbox, args, 2);
}

/*
 * Displays a blocking file title input dialog.
 * Returns a valid string or NULL if cancelled.
 */
char* input_string_dlg(Widget parent, const char *msg_str,
	const char *init_str, int flags)
{
	Widget dlg;
	Arg arg[8];
	char *token;
	XmString xm_label_string;
	Widget wlabel;
	Widget wtext;
	Cardinal i = 0;
	struct input_dlg_data idd = {flags, NULL, False, False };

	XtSetArg(arg[i], XmNdialogType, XmDIALOG_PROMPT); i++;
	XtSetArg(arg[i], XmNtitle, APP_TITLE); i++;
	XtSetArg(arg[i], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); i++;

	dlg = XmCreatePromptDialog(parent, "inputDialog", arg, i);
	XtAddCallback(dlg, XmNokCallback, input_dlg_cb, (XtPointer)&idd);
	XtAddCallback(dlg, XmNcancelCallback, input_dlg_cb, (XtPointer)&idd);
	XtUnmanageChild(XmSelectionBoxGetChild(dlg, XmDIALOG_HELP_BUTTON));

	wlabel = XmSelectionBoxGetChild(dlg, XmDIALOG_SELECTION_LABEL);
	wtext = XmSelectionBoxGetChild(dlg, XmDIALOG_TEXT);
	XtAddCallback(wtext, XmNmodifyVerifyCallback,
		&input_modify_cb, (XtPointer)(long)flags);

	xm_label_string = XmStringCreateLocalized((String)msg_str);
	XtSetArg(arg[0], XmNlabelString, xm_label_string);
	XtSetValues(wlabel, arg, 1);
	XmStringFree(xm_label_string);
	
	i=0;
	XtSetArg(arg[i], XmNvalue, init_str); i++;
	XtSetArg(arg[i], XmNpendingDelete, True); i++;
	XtSetValues(wtext, arg, i);

	XtManageChild(dlg);
	
	if(init_str && (flags & ISF_PRESELECT)) {
		if(flags & ISF_FILENAME) {
			/* preselect file title sans extension */
			token = strrchr(init_str, '.');
			
			if(token){
				XmTextFieldSetSelection(wtext,0,
					strlen(init_str) - strlen(token),
					XtLastTimestampProcessed(XtDisplay(dlg)));
			}
		} else {
			XmTextFieldSetSelection(wtext, 0, strlen(init_str),
				XtLastTimestampProcessed(XtDisplay(dlg)));
		}
	}

	while(!idd.done){
		XtAppProcessEvent(XtWidgetToApplicationContext(dlg), XtIMAll);
	}
	XtDestroyWidget(dlg);
	XmUpdateDisplay(parent);

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
char* dir_select_dlg(Widget parent, const char *title,
	const char *init_path)
{
	Widget dlg;
	Arg arg[8];
	int i = 0;
	XmString xm_init_path = NULL;
	struct input_dlg_data idd = { 0, NULL, False, False };

	if(!init_path) init_path = getenv("HOME");
	if(init_path)
		xm_init_path=XmStringCreateLocalized((String)init_path);

	XtSetArg(arg[i], XmNfileTypeMask, XmFILE_DIRECTORY); i++;
	XtSetArg(arg[i], XmNpathMode, XmPATH_MODE_FULL); i++;
	XtSetArg(arg[i], XmNdirectory, xm_init_path); i++;
	XtSetArg(arg[i], XmNtitle, title); i++;
	XtSetArg(arg[i], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); i++;

	dlg = XmCreateFileSelectionDialog(parent, "dirSelect", arg, i);
	if(xm_init_path) XmStringFree(xm_init_path);
	XtUnmanageChild(XmFileSelectionBoxGetChild(dlg, XmDIALOG_LIST_LABEL));
	XtUnmanageChild(XtParent(
		XmFileSelectionBoxGetChild(dlg,XmDIALOG_LIST)));
	XtAddCallback(dlg, XmNokCallback, input_dlg_cb,(XtPointer)&idd);
	XtAddCallback(dlg, XmNcancelCallback, input_dlg_cb, (XtPointer)&idd);
	XtUnmanageChild(XmFileSelectionBoxGetChild(dlg, XmDIALOG_HELP_BUTTON));
	XtManageChild(dlg);

	while(!idd.done){
		XtAppProcessEvent(XtWidgetToApplicationContext(dlg), XtIMAll);
	}
	XtDestroyWidget(dlg);
	XmUpdateDisplay(parent);
	
	return idd.valid ? idd.string : NULL;
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
		str = XmStringUnparse(fscb->value, NULL, 0,
			XmCHARSET_TEXT, NULL, 0, XmOUTPUT_ALL);
		if(str[0] != '\0' || (idd->flags & ISF_ALLOWEMPTY)) {
			idd->string = strdup(str);
			idd->valid = True;
		} else {
			idd->valid = False;
		}
		idd->done = True;
		XtFree(str);
	}
	XtUnmanageChild(w);
}
