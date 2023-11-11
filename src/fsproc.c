/*
 * Copyright (C) 2023 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <Xm/Xm.h>
#include <Xm/MwmUtil.h>
#include <Xm/TextF.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Form.h>
#include <Xm/SeparatoG.h>
#include "main.h"
#include "cbproc.h"
#include "fsproc.h"
#include "const.h"
#include "comdlgs.h"
#include "graphics.h"
#include "guiutil.h"
#include "stack.h"
#include "path.h"
#include "fsutil.h"
#include "debug.h"
#include "memdb.h" /* must be the last header */

/* Icon xbm */
#include "xbm/copymove.xbm"
#include "xbm/copymove_m.xbm"

/* Wait before mapping the progress dialog */
#define PROG_MAP_TIMEOUT 500

/* Number of R/W blocks (st_blksize) */
#define COPY_BUFFER_NBLOCKS 16

/* Feedback dialog type and button ids; see feedback_dialog() */
#define FBT_NONE			0
#define FBT_RETRY_IGNORE	1
#define FBT_CONTINUE_SKIP	2
#define FBT_SKIP_CANCEL		3
#define FBT_FATAL			4

#define FB_RETRY_CONTINUE	0
#define FB_SKIP_IGNORE		1
#define FB_SKIP_IGNORE_ALL	2
#define FB_CANCEL			3
#define FB_NOPTIONS 4
#define FB_VALID_ID(id) ((id) >= 0 && (id) <= 3)

#define RPERM (S_IRUSR|S_IRGRP|S_IROTH)
#define WPERM (S_IWUSR|S_IWGRP|S_IWOTH)
#define XPERM (S_IXUSR|S_IXGRP|S_IXOTH)

/* Work process data */
enum wp_action {
	WP_COPY,
	WP_MOVE,
	WP_DELETE,
	WP_CHATTR
};

struct wp_data {
	enum wp_action action;
	char * const *srcs;
	size_t num_srcs;
	const char *dest;
	
	gid_t gid;
	uid_t uid;
	mode_t fmode;
	mode_t dmode;
	mode_t fmode_mask;
	mode_t dmode_mask;
	int att_flags;
	
	int msg_out_fd;
	int reply_in_fd;

	Boolean ignore_read_err;
	Boolean ignore_write_err;
	Boolean ignore_special;
	void *copy_buffer;
	size_t copy_buffer_size;
};

/* Instance data */
struct fsproc_data {
	Widget wshell;
	Widget wmain;
	Widget wcol;
	Widget wsrc;
	Widget wsrc_label;
	Widget wdest;
	Widget wdest_label;
	Widget witem;
	Widget witem_label;
	Widget wfbdlg;
	Widget wfbmsg;
	Widget wfbinput[FB_NOPTIONS];
	Widget wcancel;
	XtSignalId sigchld_sid;
	XtIntervalId map_iid;
	XtInputId msg_input_iid;
	int wp_status;
	int msg_in_fd;
	int msg_out_fd;
	int reply_in_fd;
	int reply_out_fd;
	Boolean feedback_pending;
	volatile pid_t wp_pid;
	
	struct fsproc_data *next;
};

/* Linked list of fsproc instances */
static struct fsproc_data *fs_procs = NULL;

/* Default new directory permissions */
#define MKDIR_PERMS (S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)

/* main/gui (progress window, feedback dialog) process routines */
static int create_progress_ui(struct fsproc_data*, enum wp_action);
static void destroy_progress_ui(struct fsproc_data*);
static struct fsproc_data *init_fsproc(void);
static void destroy_fsproc(struct fsproc_data*);
static void map_timeout_cb(XtPointer, XtIntervalId*);
static void shell_map_cb(Widget, XtPointer, XEvent*, Boolean*);
static void cancel_cb(Widget, XtPointer, XtPointer);
static void destroy_cb(Widget, XtPointer, XtPointer);
static void feedback_dialog(struct fsproc_data*, int, const char*);
static void feedback_cb(Widget, XtPointer, XtPointer);
static void window_close_cb(Widget, XtPointer, XtPointer);
static void progress_cb(XtPointer, int*, XtInputId*);
static void xt_prog_sigchld_handler(XtPointer, XtSignalId*);
static void quit_with_error(struct fsproc_data *d, const char*);

/* background/work process routines */
static int wp_main(struct fsproc_data*, struct wp_data*);
static int wp_post_message(struct wp_data*,
	int, const char*, const char*, const char*, const char*);
static int wp_copy_file(struct wp_data*,
	const char*, const char*, Boolean);
static int wp_copy_tree(struct wp_data*,
	const char*, const char*, Boolean);
static int wp_delete_file(struct wp_data*, const char *path);
static int wp_delete_directory(struct wp_data*, const char*);
static int wp_delete_tree(struct wp_data*, const char*);
static int wp_chattr_tree(struct wp_data*, const char *path,
	uid_t uid, gid_t gid, mode_t fmode, mode_t dmode,
	mode_t fmode_mask, mode_t dmode_mask, int flags);
static int wp_chattr(struct wp_data*, const char *path,
	uid_t uid, gid_t gid, mode_t mode, mode_t mode_mask, int flags);
static int wp_check_create_path(struct wp_data*,
	const char *path, mode_t mode);
static int wp_hard_link(struct wp_data*, 
	const char *from, const char *to);
static int wp_sym_link(struct wp_data*,
	const char *target, const char *link);
static char* wp_error_string(const char *verb, const char *src_name,
	const char *dest_name, const char *errstr);

/*
 * Allocates, initializes and inserts an fsproc_data struct
 * into the fs_procs list. Returns NULL on error.
 */
static struct fsproc_data *init_fsproc(void)
{
	int pipe_fd[2];
	struct fsproc_data *d;
	
	d = malloc(sizeof(struct fsproc_data));
	if(!d) return NULL;
	
	memset(d, 0, sizeof(struct fsproc_data));

	if(pipe(pipe_fd) == -1) {
		free(d);
		return NULL;
	}
	d->msg_in_fd = pipe_fd[0];
	d->msg_out_fd = pipe_fd[1];
	
	if(pipe(pipe_fd) == -1) {
		free(d);
		return NULL;
	}
	d->reply_in_fd = pipe_fd[0];
	d->reply_out_fd = pipe_fd[1];

	d->msg_input_iid = XtAppAddInput(app_inst.context, d->msg_in_fd,
		(XtPointer)XtInputReadMask, progress_cb, (XtPointer)d);

	d->sigchld_sid = XtAppAddSignal(app_inst.context,
		xt_prog_sigchld_handler, (XtPointer)d);

	d->next = fs_procs;
	fs_procs = d;
	
	return d;
}

/*
 * Removes fsproc_data specified from the fs_procs list and frees
 * all associated resources and the struct itself.
 */
static void destroy_fsproc(struct fsproc_data *d)
{
	if(d->msg_input_iid) XtRemoveInput(d->msg_input_iid);
	if(d->sigchld_sid) XtRemoveSignal(d->sigchld_sid);
	if(d->wp_pid) kill(SIGKILL, d->wp_pid);
	if(d->msg_in_fd != -1) close(d->msg_in_fd);
	if(d->reply_in_fd != -1) close(d->reply_in_fd);

	/* These are already closed and set to -1 if WP fork() succeeded */
	if(d->msg_out_fd != -1) close(d->msg_out_fd);
	if(d->reply_in_fd != -1) close(d->reply_in_fd);
	
	if(d == fs_procs) {
		fs_procs = d->next;
	} else {
		struct fsproc_data *c = fs_procs;

		while(c->next != d) {
			c = c->next;
			dbg_assert(c);
		}
		c->next = d->next;
	}
	free(d);

	memdb_lstat(1);
}


/*
 * Builds the progress dialog
 */
static int create_progress_ui(struct fsproc_data *d, enum wp_action action)
{
	Cardinal n;
	Arg args[16];
	Widget wform;
	XmString xms;
	char *title = NULL;
	Pixel frm_bg;
	XmRenderTable label_rt;
	static Pixmap icon_pix = None;
	static Pixmap icon_mask = None;
	XtCallbackRec destroy_cbr[] = {
		{ sub_shell_destroy_cb, NULL }, /* all sub-shells must have that */
		{ destroy_cb, (XtPointer) d},
		{ (XtCallbackProc)NULL, NULL}
	};
	XtCallbackRec cancel_cbr[] = {
		{ cancel_cb, (XtPointer) d},
		{ (XtCallbackProc)NULL, NULL}
	};

	if(!icon_pix) create_wm_icon(copymove, &icon_pix, &icon_mask);

	switch(action) {
		case WP_COPY:
		title = "Copying";
		break;
		case WP_MOVE:
		title = "Moving";
		break;
		case WP_DELETE:
		title = "Deleting";
		break;
		case WP_CHATTR:
		title = "Setting Attributes";
		break;
	}
	
	n = 0;
	XtSetArg(args[n], XmNdestroyCallback, destroy_cbr); n++;
	XtSetArg(args[n], XmNmappedWhenManaged, False); n++;
	XtSetArg(args[n], XmNmwmFunctions, MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE); n++;
	XtSetArg(args[n], XmNallowShellResize, True); n++;
	XtSetArg(args[n], XmNtitle, title); n++;
	XtSetArg(args[n], XmNiconName, title); n++;
	XtSetArg(args[n], XmNiconPixmap, icon_pix); n++;
	XtSetArg(args[n], XmNiconMask, icon_mask); n++;
	d->wshell = XtAppCreateShell(APP_NAME "Progress", APP_CLASS "Progress",
		applicationShellWidgetClass, app_inst.display, args, n);

	add_delete_window_handler(d->wshell, window_close_cb, (XtPointer)d);

	n = 0;
	XtSetArg(args[n], XmNhorizontalSpacing, 4); n++;
	XtSetArg(args[n], XmNverticalSpacing, 4); n++; 
	wform = XmCreateForm(d->wshell, "form", args, n);
	
	n = 0;
	XtSetArg(args[n], XmNmarginWidth, 0); n++;
	XtSetArg(args[n], XmNmarginHeight, 0); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
	XtSetArg(args[n], XmNpacking, XmPACK_COLUMN); n++;
	XtSetArg(args[n], XmNcolumns, 1); n++;
	d->wcol = XmCreateRowColumn(wform, "rowColumn", args, n);

	if(action == WP_CHATTR || action == WP_DELETE)
		xms = XmStringCreateLocalized("Target");
	else
		xms = XmStringCreateLocalized("Source");
	
	n = 0;
	XtSetArg(args[n], XmNlabelString, xms); n++;
	d->wsrc_label = XmCreateLabel(d->wcol, "sourceLabel", args, n);
	XmStringFree(xms);

	n = 0;
	XtSetArg(args[n], XmNbackground, &frm_bg); n++;
	XtSetArg(args[n], XmNtextRenderTable, &label_rt); n++;
	XtGetValues(d->wsrc_label, args, n);

	n = 0;
	XtSetArg(args[n], XmNeditable, False); n++;
	XtSetArg(args[n], XmNcursorPositionVisible, False); n++;
	XtSetArg(args[n], XmNshadowThickness, 1); n++;
	XtSetArg(args[n], XmNmarginHeight, 1); n++;
	XtSetArg(args[n], XmNmarginWidth, 1); n++;
	XtSetArg(args[n], XmNtraversalOn, False); n++;
	XtSetArg(args[n], XmNcolumns, 50); n++;
	XtSetArg(args[n], XmNbackground, frm_bg); n++;
	d->wsrc = XmCreateTextField(d->wcol, "source", args, n);
	XtUninstallTranslations(d->wsrc);
	
	if(action == WP_COPY || action == WP_MOVE){
		n = 0;
		xms = XmStringCreateLocalized("Destination");
		XtSetArg(args[n], XmNlabelString, xms); n++;
		d->wdest_label = XmCreateLabel(d->wcol,
			"destinationLabel", args, n);
		XmStringFree(xms);

		n = 0;
		XtSetArg(args[n], XmNeditable, False); n++;
		XtSetArg(args[n], XmNcursorPositionVisible, False); n++;
		XtSetArg(args[n], XmNshadowThickness, 1); n++;
		XtSetArg(args[n], XmNmarginHeight, 1); n++;
		XtSetArg(args[n], XmNmarginWidth, 1); n++;
		XtSetArg(args[n], XmNtraversalOn, False); n++;
		XtSetArg(args[n], XmNcolumns, 50); n++;
		XtSetArg(args[n], XmNbackground, frm_bg); n++;
		d->wdest = XmCreateTextField(d->wcol, "destination", args, n);
		XtUninstallTranslations(d->wdest);
	}
	
	n = 0;
	xms = XmStringCreateLocalized("Item");
	XtSetArg(args[n], XmNlabelString, xms); n++;
	d->witem_label = XmCreateLabel(d->wcol, "itemLabel", args, n);
	XmStringFree(xms);


	n = 0;
	XtSetArg(args[n], XmNeditable, False); n++;
	XtSetArg(args[n], XmNcursorPositionVisible, False); n++;
	XtSetArg(args[n], XmNshadowThickness, 1); n++;
	XtSetArg(args[n], XmNmarginHeight, 1); n++;
	XtSetArg(args[n], XmNmarginWidth, 1); n++;
	XtSetArg(args[n], XmNtraversalOn, False); n++;
	XtSetArg(args[n], XmNcolumns, 50); n++;
	XtSetArg(args[n], XmNbackground, frm_bg); n++;
	d->witem = XmCreateTextField(d->wcol, "item", args, n);
	XtUninstallTranslations(d->witem);

	n = 0;
	xms = XmStringCreateLocalized("Cancel");

	XtSetArg(args[n], XmNlabelString, xms); n++;
	XtSetArg(args[n], XmNshowAsDefault, True); n++;
	XtSetArg(args[n], XmNsensitive, True); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, d->wcol); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNactivateCallback, cancel_cbr); n++;
	d->wcancel = XmCreatePushButton(wform, "cancelButton", args, n);
	XmStringFree(xms);
	
	d->map_iid = XtAppAddTimeOut(app_inst.context,
		PROG_MAP_TIMEOUT, map_timeout_cb, (XtPointer)d);

	/* decremented in sub_shell_destroy_cb */
	app_inst.num_sub_shells++;

	if(action == WP_COPY || action == WP_MOVE) {
		XtManageChild(d->wdest_label);
		XtManageChild(d->wdest);
	}
	XtManageChild(d->wsrc_label);
	XtManageChild(d->wsrc);
	XtManageChild(d->wcancel);
	XtManageChild(d->wcol);
	XtManageChild(wform);

	place_shell_over(d->wshell, app_inst.wshell);

	XtRealizeWidget(d->wshell);
	
	XtAddEventHandler(d->wshell, StructureNotifyMask,
		False, shell_map_cb, (XtPointer)d);
	
	return 0;
}

/*
 * Destroys progress UI and feedback dialog widgets.
 * NOTE: associated fsproc data will be freed trough shell's destroy callback.
 */
static void destroy_progress_ui(struct fsproc_data *d)
{
	if(d->map_iid) XtRemoveTimeOut(d->map_iid);
	if(d->wfbdlg) XtDestroyWidget(d->wfbdlg);
	XtDestroyWidget(d->wshell);
}

/* Called when the progress dialog shell becomes visible */
static void shell_map_cb(Widget w, XtPointer p, XEvent *evt, Boolean *cont)
{
	struct fsproc_data *d = (struct fsproc_data*) p;
	*cont = True;
	
	if(evt->type == MapNotify) {
		XtRemoveTimeOut(d->map_iid);
		if(d->feedback_pending)
			XtManageChild(d->wfbdlg);
		d->feedback_pending = False;
	}
}

/* Deferred shell mapping timeout callback */
static void map_timeout_cb(XtPointer p, XtIntervalId *iid)
{
	struct fsproc_data *d = (struct fsproc_data*) p;
	XtMapWidget(d->wshell);
}

/* 
 * Cancel button callback
 * Pauses the work process and asks the user to confirm cancelling.
 */
static void cancel_cb(Widget w, XtPointer client, XtPointer call)
{
	struct fsproc_data *d = (struct fsproc_data*) client;

	enum mb_result res;
	
	XtSetSensitive(w, False);
	if(!d->wp_pid) return;
	
	kill(d->wp_pid, SIGSTOP);

	res = message_box(d->wshell, MB_QUESTION, APP_TITLE,
		"Really cancel? Any changes made so far won't be undone.");

	kill(d->wp_pid, SIGCONT);
	if(res == MBR_CONFIRM)
		kill(d->wp_pid, SIGTERM);
	else
		XtSetSensitive(w, True);
}

/* WM_DELETE_WINDOW handler */
static void window_close_cb(Widget w, XtPointer client, XtPointer call)
{
	struct fsproc_data *d = (struct fsproc_data*) client;
	cancel_cb(d->wcancel, (XtPointer)d, NULL);
}

/* Progress shell destroy callback */
static void destroy_cb(Widget w, XtPointer client, XtPointer call)
{
	struct fsproc_data *d = (struct fsproc_data*) client;
	destroy_fsproc(d);
}

/*
 * GUI feedback dialog. Invoked whenever user input is required.
 */
static void feedback_dialog(struct fsproc_data *d, int type, const char *msg)
{
	Arg args[16];
	Cardinal n = 0;
	Cardinal i;
	XmString xms;
	XmString *pxms = NULL;
	static XmString ret_ign_labels[FB_NOPTIONS];
	static XmString cont_skip_labels[FB_NOPTIONS];

	
	if(!d->wfbdlg) {
		Widget wsep;
		XtCallbackRec button_cbr[] = {
			{ (XtCallbackProc)feedback_cb, (XtPointer)d},
			{ NULL, NULL}
		};
		/* NOTE: The order here MUST coincide with FB_* constants */
		char* const ret_ign_sz[] = {
			"Retry", "Ignore", "Ignore All", "Cancel"
		};
		char* const cont_skip_sz[] = {
			"Proceed", "Skip", "Skip All", "Cancel"
		};
		
		xms = XmStringCreateLocalized(APP_TITLE);
		XtSetArg(args[n], XmNdeleteResponse, XmDO_NOTHING); n++;
		XtSetArg(args[n], XmNdialogTitle, xms); n++;
		XtSetArg(args[n], XmNnoResize, True); n++;
		XtSetArg(args[n], XmNautoUnmanage, True); n++;
		XtSetArg(args[n], XmNhorizontalSpacing, 4); n++;
		XtSetArg(args[n], XmNverticalSpacing, 4); n++;
		XtSetArg(args[n], XmNfractionBase, 4); n++;
		XtSetArg(args[n], XmNdialogStyle, XmDIALOG_APPLICATION_MODAL); n++;

		d->wfbdlg = XmCreateFormDialog(d->wshell,
			"feedbackDialog", args, n);
		XmStringFree(xms);

		XtSetArg(args[0], XmNmwmFunctions, MWM_FUNC_MOVE);
		XtSetValues(XtParent(d->wfbdlg), args, 1);
		
		n = 0;
		XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
		d->wfbmsg = XmCreateLabel(d->wfbdlg, "feedbackMessage", args, n);
		
		n = 0;
		XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
		XtSetArg(args[n], XmNtopWidget, d->wfbmsg); n++;
		XtSetArg(args[n], XmNseparatorType, XmSHADOW_ETCHED_IN); n++;
		wsep = XmCreateSeparatorGadget(d->wfbdlg, "separator", args, n);
		
		for(i = 0; i < FB_NOPTIONS; i++) {
			char btn_name[16];
			Cardinal latt[FB_NOPTIONS] = {
				XmATTACH_FORM, XmATTACH_POSITION,
				XmATTACH_POSITION, XmATTACH_POSITION
			};
			Cardinal ratt[FB_NOPTIONS] = {
				XmATTACH_POSITION, XmATTACH_POSITION,
				XmATTACH_POSITION, XmATTACH_FORM
			};

			n = 0;
			XtSetArg(args[n], XmNleftAttachment, latt[i]); n++;
			XtSetArg(args[n], XmNrightAttachment, ratt[i]); n++;			
			XtSetArg(args[n], XmNleftPosition, i); n++;			
			XtSetArg(args[n], XmNrightPosition, i + 1); n++;			
			XtSetArg(args[n], XmNdefaultButtonShadowThickness, 0); n++;
			XtSetArg(args[n], XmNactivateCallback, button_cbr); n++;
			XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
			XtSetArg(args[n], XmNtopWidget, wsep); n++;
			XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
			snprintf(btn_name, XtNumber(btn_name), "button%ud", i + 1);
			d->wfbinput[i] = XmCreatePushButton(d->wfbdlg,
				btn_name, args, n);
			
			ret_ign_labels[i] = XmStringCreateLocalized(ret_ign_sz[i]);
			cont_skip_labels[i] = XmStringCreateLocalized(cont_skip_sz[i]);
		}
		
		XtSetArg(args[0], XmNdefaultButton, d->wfbinput[0]);
		XtSetValues(d->wfbdlg, args, 1);
		
		XtManageChild(wsep);
		XtManageChild(d->wfbmsg);
		XtManageChildren(d->wfbinput, FB_NOPTIONS);
	}
	
	xms = XmStringCreateLocalized((String)msg);
	XtSetArg(args[0], XmNlabelString, xms);
	XtSetValues(d->wfbmsg, args, 1);
	XmStringFree(xms);
	XtSetSensitive(d->wfbinput[FB_RETRY_CONTINUE], True);
	
	switch(type) {
		case FBT_RETRY_IGNORE:
		pxms = ret_ign_labels;
		break;
		case FBT_CONTINUE_SKIP:
		pxms = cont_skip_labels;
		break;
		case FBT_SKIP_CANCEL:
		pxms = cont_skip_labels;
		XtSetSensitive(d->wfbinput[FB_RETRY_CONTINUE], False);
		break;
	}
	
	for(i = 0; i < FB_NOPTIONS; i++) {
		XtSetArg(args[0], XmNlabelString, pxms[i]);
		XtSetValues(d->wfbinput[i], args, 1);
	}
	
	if(!is_mapped(d->wshell)) {
		d->feedback_pending = True;
		XtMapWidget(d->wshell);
	} else {
		XtManageChild(d->wfbdlg);
	}
}

/*
 * GUI feedback dialog callback. Sends response value to the work proc.
 */
static void feedback_cb(Widget w, XtPointer client, XtPointer call)
{
	struct fsproc_data *d = (struct fsproc_data*) client;
	unsigned int id = 0;
	
	while(d->wfbinput[id] != w) id++;
	
	dbg_trace("feedback button id: %d\n", id);
	if(write(d->reply_out_fd, &id, sizeof(int)) != sizeof(int)) {
		quit_with_error(d, NULL);
	}
}

/*
 * GUI process message handler (sent by work proc in wp_post_message)
 * Any I/O errors here are considered fatal.
 */
static void progress_cb(XtPointer cd, int *pfd, XtInputId *iid)
{
	struct fsproc_data *d = (struct fsproc_data*)cd;
	int feedback;
	char *buffer;
	size_t src_len = 0;
	size_t dest_len = 0;
	size_t item_len = 0;
	size_t emsg_len = 0;
	size_t buf_len = 0;
	char *src = NULL;
	char *dest = NULL;
	char *item = NULL;
	char *emsg = NULL;
	ssize_t io;
	
	dbg_trace("WP %ld message\n", d->wp_pid);
	/* Don't bother if work proc has quit already */
	if(!d->wp_pid) {
		XtRemoveInput(d->msg_input_iid);
		d->msg_input_iid = None;
		return;
	}

	io = read(d->msg_in_fd, &feedback, sizeof(int));
	io += read(d->msg_in_fd, &src_len, sizeof(size_t));
	io += read(d->msg_in_fd, &dest_len, sizeof(size_t));
	io += read(d->msg_in_fd, &item_len, sizeof(size_t));
	io += read(d->msg_in_fd, &emsg_len, sizeof(size_t));
	
	if(io < (sizeof(int) + sizeof(size_t) * 4)) {
		XtRemoveInput(d->msg_input_iid);
		d->msg_input_iid = None;
		return;
	}
	
	buf_len = src_len + dest_len + item_len + emsg_len + 4;
	
	buffer = malloc(buf_len);
	if(!buffer) {
		quit_with_error(d, strerror(errno));
		return;
	}
	
	if(src_len) {
		src = buffer;
		read(d->msg_in_fd, src, src_len);
		src[src_len] = '\0';
	}

	if(dest_len) {
		dest = buffer + src_len + 1;
		read(d->msg_in_fd, dest, dest_len);
		dest[dest_len] = '\0';
	}

	if(item_len) {
		item = buffer + src_len + dest_len + 2;
		read(d->msg_in_fd, item, item_len);
		item[item_len] = '\0';
	}

	if(emsg_len) {
		emsg = buffer + src_len + dest_len + item_len + 3;		
		read(d->msg_in_fd, emsg, emsg_len);
		emsg[emsg_len] = '\0';
	}

	if(src) XmTextFieldSetString(d->wsrc, src);
	if(dest) XmTextFieldSetString(d->wdest, dest);
	if(item) {
		/* these are created unmanaged, since only required if
		 * recursive copy/move is being done */
		XtManageChild(d->witem_label);
		XtManageChild(d->witem);
		XmTextFieldSetString(d->witem, item);
	} else {
		XmTextFieldSetString(d->witem, "");
	}
	
	if(feedback == FBT_FATAL) {
		quit_with_error(d, emsg);
	} else if(feedback != FBT_NONE) {
		feedback_dialog(d, feedback, emsg);
	}
	free(buffer);
}

/*
 * Deferred (XtNoticeSignal) SIGCHLD handler triggered in prog_sig_handler
 */
static void xt_prog_sigchld_handler(XtPointer p, XtSignalId *id)
{
	struct fsproc_data *d = (struct fsproc_data*) p;

	if((WIFSIGNALED(d->wp_status) &&
		(WTERMSIG(d->wp_status) != SIGTERM))) {
		/* which means the work proc went south all by itself */
		message_box(d->wshell, MB_ERROR,
			APP_TITLE, "Background process terminated unexpectedly.");
	} else if(WIFEXITED(d->wp_status)) {
		/* NOTE: eventually WP should return meaningful error codes */
		if(WEXITSTATUS(d->wp_status)) {
			message_box(d->wshell, MB_ERROR, APP_TITLE,
			"The requested action could not be completed due to an error.");
		}
	}
	destroy_progress_ui(d);
}

/*
 * Displays an error message box and removes fsproc
 */
static void quit_with_error(struct fsproc_data *d, const char *error)
{
	if(d->msg_input_iid) {
		XtRemoveInput(d->msg_input_iid);
		d->msg_input_iid = None;
	}

	message_box(d->wshell, MB_ERROR,
		APP_TITLE, error ? error : "Unexpected internal error.");
	
	destroy_progress_ui(d);
}

/*
 * Forks off the work process
 */
static int wp_main(struct fsproc_data *d, struct wp_data *wpd)
{
	pid_t pid;
	size_t i;
	Boolean move = False;
	struct stat st_src;
	struct stat st_dest;
	int reply;
	char *cdest = NULL;
	char *cwd;

	pid = fork();
	if(pid == -1) return errno;
	
	wpd->msg_out_fd = d->msg_out_fd;
	wpd->reply_in_fd = d->reply_in_fd;

	if(pid) {
		/* the GUI process */
		d->wp_pid = pid;
		close(d->msg_out_fd);
		close(d->reply_in_fd);
		d->msg_out_fd = -1;
		d->reply_in_fd = -1;
		return 0;
	}

	/* the work process */
	dbg_trace("Work proc spawned: %ld\n", getpid());
	close(d->msg_in_fd);
	close(d->reply_out_fd);
	
	cwd = get_working_dir();
	if(!cwd) exit(EXIT_FAILURE);

	if(wpd->dest) {
		if(stat(wpd->dest, &st_dest) == -1) {
			wp_post_message(wpd, FBT_FATAL, NULL, wpd->dest, NULL,
				wp_error_string("Error accessing", wpd->dest,
				NULL, strerror(errno)));
			exit(EXIT_SUCCESS);
		} else if(!S_ISDIR(st_dest.st_mode)) {
			wp_post_message(wpd, FBT_FATAL, NULL, wpd->dest, NULL,
				wp_error_string("Error writing", wpd->dest,
				NULL, strerror(ENOTDIR)));
			exit(EXIT_SUCCESS);			
		}
		cdest = realpath(wpd->dest, NULL);
		if(!cdest) exit(EXIT_FAILURE);
		
		wpd->copy_buffer_size = st_dest.st_blksize * COPY_BUFFER_NBLOCKS;
		wpd->copy_buffer = malloc(wpd->copy_buffer_size);
		dbg_trace("Copy buffer: %ld KB\n", wpd->copy_buffer_size / 1024);
		if(!wpd->copy_buffer) return errno;
	} else {
		wpd->copy_buffer = NULL;
	}

	for(i = 0; i < wpd->num_srcs; i++) {
		char csrc[strlen(cwd) + strlen(wpd->srcs[i]) + 2];
		int rv = 0;

		build_path(csrc, cwd, wpd->srcs[i], NULL);
	
		wp_post_message(wpd, FBT_NONE, csrc, cdest, NULL, NULL);
	
		retry_src: /* see FB_RETRY_CONTINUE case below */
		
		rv = lstat(csrc, &st_src);
		if(rv == -1) {
			if(errno == ENOENT || !wpd->ignore_read_err) continue;

			reply = wp_post_message(wpd, FBT_RETRY_IGNORE, csrc, cdest, NULL,
				wp_error_string("Error reading", csrc, NULL, strerror(errno)));
			
			switch(reply) {
				case FB_RETRY_CONTINUE:
				goto retry_src;
				
				case FB_SKIP_IGNORE:
				continue;
				
				case FB_SKIP_IGNORE_ALL:
				wpd->ignore_read_err = True;
				continue;
			}
		}
		
		switch(wpd->action) {
			case WP_DELETE:
			
			if(S_ISDIR(st_src.st_mode))
				rv = wp_delete_tree(wpd, csrc);
			else
				rv = wp_delete_file(wpd, csrc);

			break; /* WP_DELETE */

			case WP_MOVE:
			move = True;
			case WP_COPY:
			
			if(st_src.st_dev == st_dest.st_dev &&
				st_src.st_ino == st_dest.st_ino) {
				wp_post_message(wpd, FBT_FATAL, NULL, cdest, NULL,
					wp_error_string("Error writing", cdest, NULL,
					"Source and destination are the same"));
				exit(EXIT_FAILURE);
			}
			
			if(S_ISDIR(st_src.st_mode)) {
				char *src_title = get_path_tail(csrc);
				char dest_fqn[strlen(cdest) + strlen(src_title) + 2];
				
				build_path(dest_fqn, cdest, src_title, NULL);
				rv = wp_copy_tree(wpd, csrc, dest_fqn, move);
			} else if(S_ISREG(st_src.st_mode)){
				char *src_title = get_path_tail(csrc);
				char dest_fqn[strlen(cdest) + strlen(src_title) + 2];
				
				build_path(dest_fqn, cdest, src_title, NULL);
				rv = wp_copy_file(wpd, csrc, dest_fqn, move);
			} else if(S_ISLNK(st_src.st_mode)){
				char *src_title = get_path_tail(csrc);
				char dest_fqn[strlen(cdest) + strlen(src_title) + 2];
				char *link_tgt;

				build_path(dest_fqn, cdest, src_title, NULL);
				if( (rv = get_link_target(cdest, &link_tgt)) ) break;

				rv = wp_sym_link(wpd, link_tgt, dest_fqn);
				free(link_tgt);
			} else if(!wpd->ignore_special) {
				char *msg = wp_error_string(
					"Skipping non-regular file", cdest, NULL,
					"No attempt will be made to copy/move special files.");
				if(wp_post_message(wpd, FBT_SKIP_CANCEL, NULL, cdest, NULL, msg)
					== FB_SKIP_IGNORE_ALL) wpd->ignore_special = True;
			}

			break; /* WP_COPY */
			
			case WP_CHATTR:
			if(S_ISDIR(st_src.st_mode)) {
				if(wpd->att_flags & ATT_RECUR) {
					rv = wp_chattr_tree(wpd, csrc, wpd->gid, wpd->uid,
							wpd->fmode, wpd->dmode,
							wpd->fmode_mask, wpd->dmode_mask,
							wpd->att_flags);
				} else {
					rv = wp_chattr(wpd, csrc, wpd->gid, wpd->uid,
						wpd->dmode, wpd->dmode_mask, wpd->att_flags);
				}
			} else {
				rv = wp_chattr(wpd, csrc, wpd->gid, wpd->uid,
					wpd->fmode, wpd->fmode_mask, wpd->att_flags);
			}
			break; /* WP_CHATTR */
		}

		if(rv) {
			/* since recoverable errors are handled within subroutines,
			 * mention that something didn't go as planned here */
			wp_post_message(wpd, FBT_FATAL, csrc,
				wpd->dest, NULL, strerror(rv));
			break;
		}
	}

	if(wpd->copy_buffer) free(wpd->copy_buffer);

	exit(EXIT_SUCCESS);
	return 0;
}

/*
 * Recursively copies/moves files from src to dest.
 * Both arguments must be fully qualified names. 
 */
static int wp_copy_tree(struct wp_data *wpd,
	const char *src, const char *dest, Boolean move)
{
	int errv = 0;
	DIR *dir = NULL;
	struct dirent *ent;
	struct stack *rec_stk = NULL;
	struct stack *rem_stk = NULL;
	struct stack *in_stk = NULL;
	char *cur_path;
	mode_t dir_mode;
	int reply;
	size_t src_len = strlen(src);
	size_t dest_len = strlen(dest);
	
	struct link_info {
		ino_t inode;
		char name[1];
	};
	
	if(move) {
		/* try to rename first, in case we're moving on the same FS
		 * on EXDEV we have to do full recursive copy... */
		if(!rename(src, dest))
			return 0;
		else if(errno != EXDEV)
			return errno;
	}

	/* set up stacks for recursion */
	rec_stk = stk_alloc();
	in_stk = stk_alloc();
	if(!rec_stk || !in_stk) {
		if(rec_stk) stk_free(rec_stk);
		if(in_stk) stk_free(in_stk);
		return ENOMEM;
	}
	if(stk_push(rec_stk, src, src_len + 1)) {
		stk_free(rec_stk);
		return ENOMEM;
	}
	
	if(move) {
		rem_stk = stk_alloc();
		if(!rem_stk || stk_push(rem_stk, src, src_len + 1)) {
			if(rem_stk) stk_free(rem_stk);
			stk_free(rec_stk);
			stk_free(in_stk);
			return ENOMEM;
		}
	}
	
	/* NOTE! cur_path needs to be freed when done */
	while( !errv && (cur_path = stk_pop(rec_stk, NULL)) ) {
		struct stat cd_st;
		char *dest_dir_fqn;

		retry_dir: /* see FB_RETRY_CONTINUE case below */
		if( lstat(cur_path, &cd_st) || !(dir = opendir(cur_path))) {

			if(wpd->ignore_read_err) {
				free(cur_path);
				continue;
			}

			reply = wp_post_message(wpd, FBT_RETRY_IGNORE, cur_path, dest, NULL,
					wp_error_string("Error reading directory",
					cur_path, NULL, strerror(errno)) );
					
			switch(reply) {
				case FB_RETRY_CONTINUE:
				goto retry_dir;
				
				case FB_SKIP_IGNORE:
				free(cur_path);
				continue;
				
				case FB_SKIP_IGNORE_ALL:
				wpd->ignore_read_err = True;
				free(cur_path);
				continue;
			}
		}
		
		dir_mode = cd_st.st_mode;
		
		/* create destination directory */
		dest_dir_fqn = build_path(NULL, dest, &cur_path[src_len], NULL);
		if(!dest_dir_fqn) {
			errv = errno;
			free(cur_path);
			break;
		}
		wp_check_create_path(wpd, dest_dir_fqn, dir_mode);
		free(dest_dir_fqn);
		
		while( (ent = readdir(dir)) && !errv) {
			struct stat st;
			char *cur_fqn;
			
			if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;
			
			cur_fqn = build_path(NULL, cur_path, ent->d_name, NULL);
			if(!cur_fqn) {
				errv = errno;
				break;
			}
			
			retry_sub_dir: /* see FB_RETRY_CONTINUE case below */
			if(stat(cur_fqn, &st) == -1) {
				if(wpd->ignore_read_err) {
					free(cur_fqn);
					continue;
				}

				reply = wp_post_message(wpd, FBT_RETRY_IGNORE, cur_fqn,
					dest, NULL,	wp_error_string("Error accessing", cur_path,
					NULL, strerror(errno)) );
				switch(reply) {
					case FB_RETRY_CONTINUE:
					goto retry_sub_dir;
					
					case FB_SKIP_IGNORE:
					free(cur_fqn);
					continue;
					
					case FB_SKIP_IGNORE_ALL:
					wpd->ignore_read_err = True;
					free(cur_fqn);
					continue;
				}				
			}
			
			/* if directory, push it onto stack for later processing */
			if(S_ISDIR(st.st_mode)) {
				size_t fqn_len = strlen(cur_fqn) + 1;
				errv = stk_push(rec_stk, cur_fqn, fqn_len);
				if(!errv && move) errv = stk_push(rem_stk, cur_fqn, fqn_len);
			} else if(S_ISLNK(st.st_mode)) {
			/* recreate a symbolic link */
				char *link_tgt;
				char *dest_fqn;
				
				if( (errv = get_link_target(cur_fqn, &link_tgt)) ) {
					free(cur_fqn);
					break;
				}

				dest_fqn = malloc(dest_len + strlen(cur_path)
					+ strlen(ent->d_name) + 3);
				if(!dest_fqn) {
					errv = errno;
					free(link_tgt);
					break;
				}
				
				build_path(dest_fqn, dest, &cur_path[src_len], NULL);
				if(wp_check_create_path(wpd, dest_fqn, dir_mode)) {
					free(dest_fqn);
					free(link_tgt);
					break;
				}
			
				wp_post_message(wpd, FBT_NONE, cur_path,
					dest_fqn, ent->d_name, NULL);

				build_path(dest_fqn, dest,
					&cur_path[src_len], ent->d_name, NULL);

				errv = wp_sym_link(wpd, link_tgt, dest_fqn);
				
				free(dest_fqn);
				free(link_tgt);
			} else if(S_ISREG(st.st_mode)) {
			/* process a regular file */
				char *dest_fqn;
				char *src_fqn;
				size_t fqn_len;

				fqn_len = dest_len + strlen(cur_path) + strlen(ent->d_name) + 3;
				dest_fqn = malloc(fqn_len + 1);
				fqn_len = src_len + strlen(cur_path) + strlen(ent->d_name) + 3;
				src_fqn = malloc(fqn_len + 1);
				if(!dest_fqn || !src_fqn) {
					if(dest_fqn) free(dest_fqn);
					if(src_fqn) free(src_fqn);
					errv = errno;
					break;
				}
				build_path(src_fqn, cur_path, ent->d_name, NULL);
				build_path(dest_fqn, dest, &cur_path[src_len], NULL);
				if(wp_check_create_path(wpd, dest_fqn, dir_mode)) {
					free(dest_fqn);
					free(src_fqn);
					break;
				}

				wp_post_message(wpd, FBT_NONE, cur_path,
					dest_fqn, ent->d_name, NULL);

				build_path(dest_fqn, dest,
					&cur_path[src_len], ent->d_name, NULL);
				
				/* handle inodes with multiple links... */
				if(st.st_nlink > 1) {
					size_t it = 0;
					struct link_info *li;

					while( (li = stk_iterate(in_stk, NULL, &it)) &&
						(st.st_ino != li->inode) );
					/* hard link to file that was copied already... */
					if(li) {
						dbg_trace("hardlink: %s -> %s\n", li->name, dest_fqn);
						errv = wp_hard_link(wpd, li->name, dest_fqn);
						free(src_fqn);
						free(dest_fqn);
						if(errv) break; else continue;
					} else {
					/* ...store inode info and copy the file */
						size_t len = sizeof(struct link_info) +
							strlen(dest_fqn) + 1;
						
						li = malloc(len);
						if(li) {
							li->inode = st.st_ino;
							strcpy(li->name, dest_fqn);
							errv = stk_push(in_stk, li, len);
						}
						if(!errv) {
							errv = wp_copy_file(wpd, src_fqn, dest_fqn, move);
						}
					}
				} else {
					/* inode with one link */
					errv = wp_copy_file(wpd, src_fqn, dest_fqn, move);
				}
				free(dest_fqn);
				free(src_fqn);
			} else if(!wpd->ignore_special) {
			/* special file? just ignore it... */
				char *msg = wp_error_string(
					"Skipping non-regular file", cur_fqn, NULL,
					"No attempt will be made to copy/move special files.");
					
				if(wp_post_message(wpd, FBT_SKIP_CANCEL,
					NULL, NULL, NULL, msg) == FB_SKIP_IGNORE_ALL)
						wpd->ignore_special = True;
				free(msg);
			}

			free(cur_fqn);
		}
		free(cur_path);
		if(dir) closedir(dir);
	}
	stk_free(rec_stk);
	stk_free(in_stk);
	
	/* purge directory structure */
	if(move) {
		while(!errv && (cur_path = stk_pop(rem_stk, NULL)) ) {
			errv = wp_delete_directory(wpd, cur_path);
			free(cur_path);
		}
		stk_free(rem_stk);
	}
	return errv;
}

/*
 * Copies/Moves single file from src to dest
 */
static int wp_copy_file(struct wp_data *wpd,
	const char *src, const char *dest, Boolean move)
{
	struct stat st_src, st_dest;
	int fin, fout;
	size_t nchunks, ichunk;
	off_t rest;
	ssize_t rw;
	int reply = 0;
	int res = 0;
	Boolean read_err = False;

	if(stat(src, &st_src)) return errno;

	if(!stat(dest, &st_dest)) {
		static Boolean ignore_exist = False;
		int fb_type;
		char *msg;

		if(ignore_exist) return 0;

		if(S_ISDIR(st_dest.st_mode))	{
			fb_type = FBT_SKIP_CANCEL;
			msg = "Destination exists and is a directory!";
		} else {
			if(st_src.st_dev == st_dest.st_dev &&
				st_src.st_ino == st_dest.st_ino) {
				fb_type = FBT_SKIP_CANCEL;
				msg = "Source and destination are the same file!";
			} else {
				fb_type = FBT_CONTINUE_SKIP;
				msg = "Destination file exists. Proceed and overwrite?";
			}
		}
		
		reply = wp_post_message(wpd, fb_type, NULL, NULL, NULL,
			wp_error_string("Error writing", dest, NULL, msg) );

		switch(reply) {
			case FB_RETRY_CONTINUE:
			if( (res = unlink(dest)) ) return res;
			break;
			
			case FB_SKIP_IGNORE:
			return 0;

			case FB_SKIP_IGNORE_ALL:
			ignore_exist = 1;
			return 0;
		}	
	}
	
	/* try to rename the file first if moving*/
	if(move && !rename(src, dest)) return 0;

	/* buffered copy */
	retry_open_src:
	fin = open(src, O_RDONLY);
	if(fin == -1) {
		if(wpd->ignore_read_err) return 0;
		
		reply = wp_post_message(wpd, FBT_RETRY_IGNORE, NULL, NULL, NULL,
			wp_error_string("Error reading", src, NULL, strerror(errno)) );

		switch(reply) {
			case FB_RETRY_CONTINUE:
			goto retry_open_src;
			break;
			
			case FB_SKIP_IGNORE:
			return 0;

			case FB_SKIP_IGNORE_ALL:
			wpd->ignore_read_err = True;
			return 0;
		}		
	}
	
	retry_open_dest:
	fout = open(dest, O_CREAT|O_WRONLY, st_src.st_mode);
	if(fout == -1) {
		if(wpd->ignore_write_err) {
			close(fin);
			return 0;
		}
		
		reply = wp_post_message(wpd, FBT_RETRY_IGNORE, NULL, NULL, NULL,
			wp_error_string("Error writing", dest, NULL, strerror(errno)) );

		switch(reply) {
			case FB_RETRY_CONTINUE:
			goto retry_open_dest;
			break;
			
			case FB_SKIP_IGNORE:
			close(fin);
			return 0;

			case FB_SKIP_IGNORE_ALL:
			wpd->ignore_write_err = True;
			close(fin);
			return 0;
		}		
	}
	
	if(st_src.st_size > wpd->copy_buffer_size){
		nchunks = st_src.st_size / wpd->copy_buffer_size;
		rest = st_src.st_size - (wpd->copy_buffer_size * nchunks);
	}else{
		nchunks = 0;
		rest = st_src.st_size;
	}
	
	for(ichunk = 0; ichunk < nchunks; ichunk++){
		rw = read(fin, wpd->copy_buffer, wpd->copy_buffer_size);
		if(rw != wpd->copy_buffer_size) {
			read_err = True;
			res = errno;
			break;
		}
		
		rw = write(fout, wpd->copy_buffer, wpd->copy_buffer_size);
		if(rw != wpd->copy_buffer_size) {
			read_err = False;
			res = errno;
			break;
		}
	}

	if(!res && rest){
		if( (rw = read(fin, wpd->copy_buffer, rest)) == rest){
			rw = write(fout, wpd->copy_buffer, rest);
			if(rw != rest) read_err = False;
		} else {
			read_err = True;
		}
		if(rw != rest) res = errno;
	}

	if(res) {
		const char *msg = read_err ? "Error reading" : "Error writing";
		const char *name = read_err ? src : dest;
		reply = wp_post_message(wpd, FBT_SKIP_CANCEL, NULL, NULL, NULL,
			wp_error_string(msg, name, NULL, strerror(res)) );
		if(reply == FB_SKIP_IGNORE_ALL) {
			if(read_err)
				wpd->ignore_read_err = True;
			else
				wpd->ignore_write_err = True;
		}
	} else if(fchmod(fout, st_src.st_mode) == -1) {
		reply = wp_post_message(wpd, FBT_SKIP_CANCEL, NULL, NULL, NULL,
			wp_error_string("Error setting attributes", dest,
			NULL, strerror(res)) );
		if(reply == FB_SKIP_IGNORE_ALL) wpd->ignore_write_err = True;
	}
	
	close(fin);
	close(fout);

	if(res)	{
		unlink(dest);
	} else if(move) {
		wp_delete_file(wpd, src);
	}

	return 0;	
}

/*
 * Creates a symbolic link (target -> link)
 */
static int wp_sym_link(struct wp_data *wpd,
	const char *target, const char *link)
{
	static Boolean ignore_err = False;
	retry_link:
	
	dbg_trace("symlink: %s -> %s\n", link, target);
	
	if( (symlink(target, link) == -1) && !ignore_err) {

		int reply;
		int msg_id;
		
		switch(errno) {
			case EEXIST:
			msg_id = FBT_CONTINUE_SKIP;
			break;
			default:
			msg_id = FBT_SKIP_CANCEL;
			break;
		}
		
		reply = wp_post_message(wpd, msg_id, NULL, NULL, NULL,
			wp_error_string("Error creating a symbolic link",
				link, target, strerror(errno)) );
		switch(reply) {
			case FB_RETRY_CONTINUE:
			goto retry_link;
			
			case FB_SKIP_IGNORE_ALL:
			ignore_err = True;
			break;
		}
	}
	return 0;
}

/*
 * Creates a hard link from -> to
 */
static int wp_hard_link(struct wp_data *wpd, const char *from, const char *to)
{
	static Boolean ignore_err = False;
	retry_link:

	if( (link(from, to) == -1) && (errno != ENOENT) && !ignore_err) {

		int reply;
		int msg_id;
		
		switch(errno) {
			case EEXIST:
			msg_id = FBT_CONTINUE_SKIP;
			break;
			default:
			msg_id = FBT_SKIP_CANCEL;
			break;
		}
		
		reply = wp_post_message(wpd, msg_id, NULL, NULL, NULL,
			wp_error_string("Error creating a hard link",
				from, to, strerror(errno)) );
		switch(reply) {
			case FB_RETRY_CONTINUE:
			goto retry_link;
			
			case FB_SKIP_IGNORE_ALL:
			ignore_err = True;
			break;
		}
	}
	return 0;
}

/*
 * Recursively deletes a directory 
 */
static int wp_delete_tree(struct wp_data *wpd, const char *src)
{
	int errv = 0;
	DIR *dir;
	struct dirent *ent;
	struct stack *rec_stk;
	struct stack *rem_stk;
	char *cur_path;
	int reply;
	size_t src_len = strlen(src);
	
	rec_stk = stk_alloc();
	rem_stk = stk_alloc();
	if(!rec_stk || !rem_stk) {
		if(rec_stk) stk_free(rec_stk);
		if(rem_stk) stk_free(rem_stk);
		return ENOMEM;
	}
	if(stk_push(rec_stk, src, src_len + 1) ||
		stk_push(rem_stk, src, src_len + 1)) {
			stk_free(rec_stk);
			stk_free(rem_stk);
			return ENOMEM;
	}
	
	/* NOTE! cur_path is on heap and needs to be freed when done */
	while( !errv && (cur_path = stk_pop(rec_stk, NULL)) ) {
		struct stat cd_st;

		wp_post_message(wpd, FBT_NONE, cur_path, NULL, NULL, NULL);

		retry_dir: /* see FB_RETRY_CONTINUE case below */
		if( stat(cur_path, &cd_st) || !(dir = opendir(cur_path))) {

			if(wpd->ignore_read_err) {
				free(cur_path);
				continue;
			}

			reply = wp_post_message(wpd, FBT_RETRY_IGNORE, cur_path, NULL, NULL,
					wp_error_string("Error reading directory",
					cur_path, NULL, strerror(errno)) );
					
			switch(reply) {
				case FB_RETRY_CONTINUE:
				goto retry_dir;
				
				case FB_SKIP_IGNORE:
				free(cur_path);
				continue;
				
				case FB_SKIP_IGNORE_ALL:
				wpd->ignore_read_err = True;
				free(cur_path);
				continue;
			}
			return errno;
		}
		
		while( (ent = readdir(dir)) && !errv) {
			struct stat st;
			char *cur_fqn;
			
			if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;

			cur_fqn = build_path(NULL, cur_path, ent->d_name, NULL);
			if(!cur_fqn) {
				errv = errno;
				break;
			}
			wp_post_message(wpd, FBT_NONE, cur_fqn, NULL, NULL, NULL);
			
			retry_sub_dir: /* see FB_RETRY_CONTINUE case below */
			if(lstat(cur_fqn, &st) == -1) {
				if(wpd->ignore_read_err) {
					free(cur_fqn);
					continue;
				}

				reply = wp_post_message(wpd, FBT_RETRY_IGNORE,
					cur_fqn, NULL, NULL,
					wp_error_string("Error accessing", cur_fqn,
					NULL, strerror(errno)) );
				switch(reply) {
					case FB_RETRY_CONTINUE:
					goto retry_sub_dir;
					
					case FB_SKIP_IGNORE:
					free(cur_fqn);
					continue;
					
					case FB_SKIP_IGNORE_ALL:
					wpd->ignore_read_err = True;
					free(cur_fqn);
					continue;
				}				
			}
			
			/* if directory, push it onto stack for later processing */
			if(S_ISDIR(st.st_mode)) {
				size_t fqn_len = strlen(cur_fqn) + 1;
				if(stk_push(rec_stk, cur_fqn, fqn_len) ||
					stk_push(rem_stk, cur_fqn, fqn_len)) errv = ENOMEM;
			} else { /* delete file otherwise */
				errv = wp_delete_file(wpd, cur_fqn);
			}
			free(cur_fqn);
		}
		free(cur_path);
		closedir(dir);
	}
	stk_free(rec_stk);
	
	/* purge directory structure */
	while(!errv && (cur_path = stk_pop(rem_stk, NULL)) ) {
		wp_post_message(wpd, FBT_NONE, cur_path, NULL, NULL, NULL);
		errv = wp_delete_directory(wpd, cur_path);
		free(cur_path);
	}
	stk_free(rem_stk);

	return errv;
}

/*
 * Recursively sets attributes
 */
static int wp_chattr_tree(struct wp_data *wpd, const char *src,
	uid_t uid, gid_t gid, mode_t fmode, mode_t dmode,
	mode_t fmode_mask, mode_t dmode_mask, int flags)
{
	int errv = 0;
	DIR *dir;
	struct dirent *ent;
	struct stack *rec_stk;
	struct stack *rem_stk;
	char *cur_path;
	int reply;
	size_t src_len = strlen(src);
	
	rec_stk = stk_alloc();
	rem_stk = stk_alloc();
	if(!rec_stk || !rem_stk) {
		if(rec_stk) stk_free(rec_stk);
		if(rem_stk) stk_free(rem_stk);
		return ENOMEM;
	}
	if(stk_push(rec_stk, src, src_len + 1) ||
		stk_push(rem_stk, src, src_len + 1)) {
			stk_free(rec_stk);
			stk_free(rem_stk);
			return ENOMEM;
	}

	/* NOTE: cur_path is on heap and needs to be freed when done */
	while( !errv && (cur_path = stk_pop(rec_stk, NULL)) ) {
		Boolean eacces_once = False;
		struct stat cd_st;

		wp_post_message(wpd, FBT_NONE, cur_path, NULL, NULL, NULL);

		retry_dir: /* see FB_RETRY_CONTINUE case below */
		if( stat(cur_path, &cd_st) || !(dir = opendir(cur_path))) {
			/* if read permission is requested, but not set,
			 * tweak it here to be able to recurse */
			if( (errno == EACCES) && !eacces_once &&
				((dmode & dmode_mask) & (RPERM)) ) {
				wp_chattr(wpd, cur_path, uid, gid, dmode, dmode_mask, flags);
				eacces_once = True;
				goto retry_dir;
			}
			
			if(wpd->ignore_read_err) {
				free(cur_path);
				continue;
			}

			reply = wp_post_message(wpd, FBT_RETRY_IGNORE, cur_path, NULL, NULL,
					wp_error_string("Error accessing directory",
					cur_path, NULL, strerror(errno)) );
					
			switch(reply) {
				case FB_RETRY_CONTINUE:
				goto retry_dir;
				
				case FB_SKIP_IGNORE:
				free(cur_path);
				continue;
				
				case FB_SKIP_IGNORE_ALL:
				wpd->ignore_read_err = True;
				free(cur_path);
				continue;
			}
			return errno;
		}

		/* we set directory permissions at last, but tweak them here
		 * if necessary to be able to recurse */
		if( !(cd_st.st_mode & WPERM) || !(cd_st.st_mode & XPERM) ) {
			wp_chattr(wpd, cur_path, uid, gid,
				WPERM|XPERM, WPERM|XPERM, flags);
		}
		
		while( (ent = readdir(dir)) && !errv) {
			struct stat st;
			char *cur_fqn;
			
			if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;
			
			cur_fqn = build_path(NULL, cur_path, ent->d_name, NULL);
			if(!cur_fqn) {
				errv = errno;
				break;
			}
			
			retry_sub_dir: /* see FB_RETRY_CONTINUE case below */
			if(stat(cur_fqn, &st) == -1) {
				int st_error = errno;
				
				if(wpd->ignore_read_err) continue;
				
				wp_post_message(wpd, FBT_NONE, cur_path, NULL, NULL, NULL);
				
				reply = wp_post_message(wpd, FBT_RETRY_IGNORE,
					cur_fqn, NULL, NULL,
					wp_error_string("Error accessing", cur_fqn,
						NULL, strerror(st_error)) );
				
				switch(reply) {
					case FB_RETRY_CONTINUE:
					goto retry_sub_dir;
					
					case FB_SKIP_IGNORE:
					free(cur_fqn);
					continue;
					
					case FB_SKIP_IGNORE_ALL:
					wpd->ignore_read_err = True;
					free(cur_fqn);
					continue;
				}				
			}
			
			/* if directory, push it onto the 'remaining' stack */
			if(S_ISDIR(st.st_mode)) {
				size_t fqn_len = strlen(cur_fqn) + 1;
				if(stk_push(rec_stk, cur_fqn, fqn_len) ||
					stk_push(rem_stk, cur_fqn, fqn_len)) errv = ENOMEM;
			} else {
				wp_post_message(wpd, FBT_NONE, cur_fqn, NULL, NULL, NULL);
				wp_chattr(wpd, cur_fqn, uid, gid, fmode, fmode_mask, flags);
			}
			free(cur_fqn);
		}
		free(cur_path);
		closedir(dir);
	}
	stk_free(rec_stk);
	
	/* process remaining directories */
	while(!errv && (cur_path = stk_pop(rem_stk, NULL)) ) {
		wp_post_message(wpd, FBT_NONE, cur_path, NULL, NULL, NULL);
		wp_chattr(wpd, cur_path, uid, gid, dmode, dmode_mask, flags);
		free(cur_path);
	}
	stk_free(rem_stk);

	return errv;
}

/*
 * Sets file/directory attributes and owner
 */
static int wp_chattr(struct wp_data *wpd, const char *path,
	uid_t uid, gid_t gid, mode_t mode, mode_t mode_mask, int flags)
{
	static Boolean ignore_err = False;
	int errv = 0;
	
	retry:
	
	if(flags & ATT_OWNER) {
		if(!chown(path, uid, gid))
			flags &= ~ATT_OWNER;
		else
			errv = errno;
	}
	
	if(!errv && (flags & (ATT_FMODE|ATT_DMODE)) ) {
		struct stat st;
		
		if(!stat(path, &st)) {
			mode = ( ((st.st_mode & 0xfff) & (~mode_mask)) |
				(mode & mode_mask) );
		} else {
			errv = errno;
		}

		if(!errv) {
			if(!chmod(path, mode))
				flags &= ~(ATT_FMODE|ATT_DMODE);
			else
				errv = errno;
		}
	}

	if(errv && !ignore_err) {
		int reply;
		
		reply = wp_post_message(wpd, FBT_RETRY_IGNORE, NULL, NULL, NULL,
			wp_error_string("Error setting attributes", path,
			NULL, strerror(errv)) );
		
		switch(reply) {
			case FB_RETRY_CONTINUE:
			
			errv = 0;
			goto retry;
			break;

			case FB_SKIP_IGNORE_ALL:
			ignore_err = True;
			break;
		}			
	}
	return 0;
}

/*
 * Deletes a single directory
 */
static int wp_delete_directory(struct wp_data *wpd, const char *path)
{
	static Boolean ignore_err = False;
	
	retry_rmdir:

	if(rmdir(path) == -1 && errno != ENOENT && !ignore_err) {
		int reply;
		reply = wp_post_message(wpd, FBT_RETRY_IGNORE, NULL, NULL, NULL,
					wp_error_string("Error removing",
					path, NULL, strerror(errno)) );
		switch(reply) {
			case FB_RETRY_CONTINUE:
			goto retry_rmdir;
			
			case FB_SKIP_IGNORE_ALL:
			ignore_err = True;
			case FB_SKIP_IGNORE:
			break;			
		}
	}
	return 0;
}

/*
 * Deletes a single file
 */
static int wp_delete_file(struct wp_data *wpd, const char *path)
{
	static Boolean ignore_err = False;
	
	retry_unlink:

	if(unlink(path) == -1 && errno != ENOENT && !ignore_err) {
		int reply;
		reply = wp_post_message(wpd, FBT_RETRY_IGNORE, NULL, NULL, NULL,
					wp_error_string("Error removing",
					path, NULL, strerror(errno)) );
		switch(reply) {
			case FB_RETRY_CONTINUE:
			goto retry_unlink;
			
			case FB_SKIP_IGNORE_ALL:
			ignore_err = True;
			break;
		}
	}
	return 0;
}

/*
 * Checks if path exists and creates it component-wise
 * Returns zero on success, errno on failure to create path.
 */
static int wp_check_create_path(struct wp_data *wpd,
	const char *path, mode_t mode)
{
	int ev;
	retry_create_path:

	if( (ev = create_path(path, mode)) ) {
		int reply;
		reply = wp_post_message(wpd, FBT_RETRY_IGNORE, NULL, NULL, NULL,
					wp_error_string("Error creating",
					path, NULL, strerror(errno)) );
		if(reply ==  FB_RETRY_CONTINUE)
			goto retry_create_path;
	}
	return ev;
}

/*
 * Sends a message to the parent (GUI) process, gets a response if required.
 * The response FB_CANCEL is handled right here by terminating the process.
 * Any IO errors here are considered fatal.
 */
static int wp_post_message(struct wp_data *wpd, int msg_id,
	const char *src, const char *dest, const char *item, const char *emsg)
{
	size_t src_len = src ? strlen(src) : 0;
	size_t dest_len = dest ? strlen(dest) : 0;
	size_t item_len = item ? strlen(item) : 0;
	size_t emsg_len = emsg ? strlen(emsg) : 0;
	ssize_t io, msg_data_len;
	
	msg_data_len = sizeof(size_t) * 4  + sizeof(int) +
		src_len + dest_len + item_len + emsg_len;
	
	io = write(wpd->msg_out_fd, &msg_id, sizeof(int));
	io += write(wpd->msg_out_fd, &src_len, sizeof(size_t));
	io += write(wpd->msg_out_fd, &dest_len, sizeof(size_t));
	io += write(wpd->msg_out_fd, &item_len, sizeof(size_t));
	io += write(wpd->msg_out_fd, &emsg_len, sizeof(size_t));
	
	if(src_len) io += write(wpd->msg_out_fd, src, src_len);
	if(dest_len) io += write(wpd->msg_out_fd, dest, dest_len);
	if(item_len) io += write(wpd->msg_out_fd, item, item_len);
	if(emsg_len) io += write(wpd->msg_out_fd, emsg, emsg_len);
	
	if(io < msg_data_len) exit(EXIT_FAILURE);
	
	/* Wait for and read the response if required */
	if(msg_id != FBT_NONE) {
		int oid;
		
		dbg_trace("%ld waiting for reply\n", getpid());
		io = read(wpd->reply_in_fd, &oid, sizeof(int));
		if(io < sizeof(int) || !FB_VALID_ID(oid)) {
			exit(EXIT_FAILURE);
		} else if(oid == FB_CANCEL) {
			exit(EXIT_SUCCESS);
		}
		return oid;
	}
	return 0;
}

/*
 * Non-reentrant. Generates user friendly, formatted error messages.
 */
static char* wp_error_string(const char *verb, const char *src_name,
	const char *dest_name, const char *errstr)
{
	static size_t last_len = 0;
	static char *buffer = NULL;
	size_t len;
	char tmpl1[] = "%s \'%s\'\n%s";
	char tmpl2[] = "%s \'%s\' to \'%s\'\n%s";
	
	if(!dest_name) {
		len = snprintf(NULL, 0, tmpl1, verb, src_name, errstr) + 2;
	} else {
		len = snprintf(NULL, 0, tmpl2, verb, src_name, dest_name, errstr) + 2;
	}
	
	if(len > last_len) {
		void *p;
		p = realloc(buffer, len + 1);
		if(!p) return strerror(errno);
		buffer = p;
		last_len = len;
	}

	if(!dest_name) {
		len = snprintf(buffer, len, tmpl1, verb, src_name, errstr);
	} else {
		len = snprintf(buffer, len, tmpl2, verb,
			src_name, dest_name, errstr);
	}
	if(len && !ispunct(buffer[len - 1])) strcat(buffer, ".");
	return buffer;
}

/* 
 * SIGCHLD handler for background process PIDs
 * Returns True if pid was an fsproc process
 */
Boolean fs_proc_sigchld(pid_t pid, int status)
{
	struct fsproc_data *cur = fs_procs;
	
	while(cur) {
		if(cur->wp_pid == pid) {
			dbg_trace("WP %ld finished\n", pid);
			cur->wp_pid = 0;
			cur->wp_status = status;
			XtNoticeSignal(cur->sigchld_sid);
			return True;
		}
		cur = cur->next;
	}
	return False;
}

int copy_files(char * const *srcs, size_t num_srcs, const char *dest)
{
	int res = 0;
	struct fsproc_data *d;
	struct wp_data wpd = {
		.action = WP_COPY,
		.srcs = srcs,
		.num_srcs = num_srcs,
		.dest = dest
	};
	
	d = init_fsproc();
	if(!d) return ENOMEM;
	create_progress_ui(d, WP_COPY);
	if( (res = wp_main(d, &wpd)) ) {
		destroy_progress_ui(d);
	}
	
	return res;
}

int move_files(char * const *srcs, size_t num_srcs, const char *dest)
{
	int res = 0;
	struct fsproc_data *d;
	struct wp_data wpd = {
		.action = WP_MOVE,
		.srcs = srcs,
		.num_srcs = num_srcs,
		.dest = dest
	};
	
	d = init_fsproc();
	if(!d) return ENOMEM;
	create_progress_ui(d, WP_MOVE);
	if( (res = wp_main(d, &wpd)) ) {
		destroy_progress_ui(d);
	}
	
	return res;

}

int delete_files(char * const *srcs, size_t num_srcs)
{
	int res = 0;
	struct fsproc_data *d;
	struct wp_data wpd = {
		.action = WP_DELETE,
		.srcs = srcs,
		.num_srcs = num_srcs,
		.dest = NULL
	};
	
	d = init_fsproc();
	if(!d) return ENOMEM;
	create_progress_ui(d, WP_DELETE);
	if( (res = wp_main(d, &wpd)) ) {
		destroy_progress_ui(d);
	}
	
	return res;
}

int set_attributes(char * const *srcs, size_t num_srcs,
	gid_t gid, uid_t uid, mode_t file_mode, mode_t dir_mode,
	mode_t file_mode_mask, mode_t dir_mode_mask,
	int att_flags)
{
	int res = 0;
	struct fsproc_data *d;
	struct wp_data wpd = {
		.action = WP_CHATTR,
		.srcs = srcs,
		.num_srcs = num_srcs,
		.dest = NULL,
		.gid = gid,
		.uid = uid,
		.fmode = file_mode,
		.dmode = dir_mode,
		.fmode_mask = file_mode_mask,
		.dmode_mask = dir_mode_mask,
		.att_flags = att_flags
	};

	d = init_fsproc();
	if(!d) return ENOMEM;
	create_progress_ui(d, WP_DELETE);
	if( (res = wp_main(d, &wpd)) ) {
		destroy_progress_ui(d);
	}
	
	return res;
}
