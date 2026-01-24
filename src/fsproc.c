/*
 * Copyright (C) 2023-2026 alx@fastestcode.org
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
#include <time.h>
#include <Xm/Xm.h>
#include <Xm/MwmUtil.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
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
#include "mbstr.h"
#include "fsutil.h"
#include "progw.h"
#include "debug.h"

/* Icon xbm */
#include "xbm/copymove.xbm"
#include "xbm/copymove_m.xbm"
#include "xbm/copy.xpm"
#include "xbm/move.xpm"
#include "xbm/delete.xpm"
#include "xbm/chattr.xpm"

/* Wait before mapping the progress dialog */
#define PROG_MAP_TIMEOUT 500

/* Number of R/W blocks (st_blksize) */
#define COPY_BUFFER_NBLOCKS 16

/* GUI messages */
#define MSG_STAT 0
#define MSG_PROG 1
#define MSG_FDBK 2

/* Feedback dialog type and button ids; see feedback_dialog() */
#define FBT_RETRY_IGNORE	0
#define FBT_CONTINUE_SKIP	1
#define FBT_SKIP_CANCEL		2
#define FBT_FATAL			3

#define FB_RETRY_CONTINUE	0
#define FB_SKIP_IGNORE		1
#define FB_SKIP_IGNORE_ALL	2
#define FB_CANCEL			3
#define FB_NOPTIONS 4
#define FB_VALID_ID(id) ((id) >= 0 && (id) <= 3)

/* Controls when path/file names get abbreviated */
#define PROG_FNAME_MAX	40

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

#define NUM_ACTIONS 4

struct wp_data {
	enum wp_action action;
	const char *wdir;
	char * const *srcs;
	char * const *dsts;
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
	double one_percent_size;
	double percent_total;
};

/* Instance data */
struct fsproc_data {
	enum wp_action action;
	Widget wshell;
	Widget wmain;
	Widget wprstatus;
	Widget wprogress;
	Widget wprcancel;
	Widget wfbdlg;
	Widget wfbmsg;
	Widget wfbinput[FB_NOPTIONS];
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
static int create_progress_ui(struct fsproc_data*);
static void destroy_progress_ui(struct fsproc_data*);
static struct fsproc_data *init_fsproc(enum wp_action);
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

/* background/work process routines */
static int wp_main(struct fsproc_data*, struct wp_data*);
static void wp_post_stat(struct wp_data*, const char*);
static void wp_post_astat(struct wp_data*,
	const char*, const char*, const char*);
static void wp_post_prog(struct wp_data*, int);
static int wp_post_msg(struct wp_data*, int, const char*);
static int wp_count(struct wp_data*, const char*);
static int wp_count_tree(const char*, struct fsize*);
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
static Boolean wp_check_same(const char *csrc, const char *cdest);
static void wp_sigpipe_handler(int);

/*
 * Allocates, initializes and inserts an fsproc_data struct
 * into the fs_procs list. Returns NULL on error.
 */
static struct fsproc_data *init_fsproc(enum wp_action action)
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
	
	d->action = action;

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
}


/*
 * Builds the progress dialog
 */
static int create_progress_ui(struct fsproc_data *d)
{
	Cardinal n;
	Arg args[20];
	Widget wform;
	XmString xms;
	static Pixmap label_pix[NUM_ACTIONS] = { None };
	static Pixmap icon_pix = None;
	static Pixmap icon_mask;
	static char *title_str[NUM_ACTIONS] = {
		"Copying", "Moving", "Deleting", "Setting Attributes"
	};

	XtCallbackRec destroy_cbr[] = {
		{ sub_shell_destroy_cb, NULL }, /* all sub-shells must have that */
		{ destroy_cb, (XtPointer) d},
		{ (XtCallbackProc)NULL, NULL}
	};
	XtCallbackRec cancel_cbr[] = {
		{ cancel_cb, (XtPointer) d},
		{ (XtCallbackProc)NULL, NULL}
	};

	if(icon_pix == None) create_wm_icon(
		app_inst.display, copymove, &icon_pix, &icon_mask);

	n = 0;
	XtSetArg(args[n], XmNdestroyCallback, destroy_cbr); n++;
	XtSetArg(args[n], XmNmappedWhenManaged, False); n++;
	XtSetArg(args[n], XmNmwmFunctions, MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE); n++;
	XtSetArg(args[n], XmNallowShellResize, True); n++;
	XtSetArg(args[n], XmNtitle, title_str[d->action]); n++;
	XtSetArg(args[n], XmNiconName, title_str[d->action]); n++;
	XtSetArg(args[n], XmNiconPixmap, icon_pix); n++;
	XtSetArg(args[n], XmNiconMask, icon_mask); n++;
	d->wshell = XtAppCreateShell(
		APP_NAME "ProgressDialog", APP_CLASS,
		topLevelShellWidgetClass,
		app_inst.display, args, n);

	add_delete_window_handler(d->wshell, window_close_cb, (XtPointer)d);

	n = 0;
	XtSetArg(args[n], XmNmarginWidth, 10); n++;
	XtSetArg(args[n], XmNmarginHeight, 10); n++;
	XtSetArg(args[n], XmNhorizontalSpacing, 8); n++;
	XtSetArg(args[n], XmNverticalSpacing, 8); n++;
	XtSetArg(args[n], XmNresizePolicy, XmRESIZE_GROW); n++;
	wform = XmCreateForm(d->wshell, "main", args, n);

	if(label_pix[0] == None) {
		int i;
		char **pix_data[NUM_ACTIONS] = {
			copy_xpm, move_xpm,
			delete_xpm, chattr_xpm
		};
		
		for(i = 0; i < NUM_ACTIONS; i++) {
			if(!create_ui_pixmap(wform, pix_data[i], &label_pix[i], NULL))
				label_pix[i] = XmUNSPECIFIED_PIXMAP;
		}
	}
	
	n = 0;
	xms = XmStringCreateLocalized("");
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNpixmapTextPadding, 8); n++;
	XtSetArg(args[n], XmNlabelPixmap, label_pix[d->action]); n++;
	XtSetArg(args[n], XmNlabelType, XmPIXMAP_AND_STRING); n++;
	XtSetArg(args[n], XmNlabelString, xms); n++;
	d->wprstatus = XmCreateLabel(wform, "progress", args, n);
	XmStringFree(xms);
	
	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, d->wprstatus); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	d->wprogress = CreateProgress(wform, "progressBar", args, n);
	
	n = 0;
	xms = XmStringCreateLocalized("Cancel");

	XtSetArg(args[n], XmNlabelString, xms); n++;
	XtSetArg(args[n], XmNmarginWidth, 4); n++;
	XtSetArg(args[n], XmNmarginHeight, 4); n++;
	XtSetArg(args[n], XmNshowAsDefault, True); n++;
	XtSetArg(args[n], XmNsensitive, True); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, d->wprogress); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNactivateCallback, cancel_cbr); n++;
	d->wprcancel = XmCreatePushButton(wform, "cancelButton", args, n);
	XmStringFree(xms);

	d->map_iid = XtAppAddTimeOut(app_inst.context,
		PROG_MAP_TIMEOUT, map_timeout_cb, (XtPointer)d);

	/* decremented in sub_shell_destroy_cb */
	app_inst.num_sub_shells++;

	XtManageChild(d->wprstatus);
	XtManageChild(d->wprogress);
	XtManageChild(d->wprcancel);
	XtManageChild(wform);

	place_shell_over(d->wshell, app_inst.wshell);

	XtRealizeWidget(d->wshell);
	
	XtAddEventHandler(d->wshell, StructureNotifyMask,
		False, shell_map_cb, (XtPointer)d);
	
	return 0;
}

/*
 * Destroys progress UI and feedback dialog widgets.
 * This function is called by the SIGCHLD handler for WPs, hence it shouldn't
 * be called after a successfull fork().
 * NOTE: associated fsproc data will be freed through shell's destroy callback.
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
	if(d->wp_pid) XtMapWidget(d->wshell);
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

	res = message_box(d->wshell, MB_QUESTION, APP_TITLE, "Really cancel?");

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
	cancel_cb(d->wprcancel, (XtPointer)d, NULL);
}

/* Progress shell destroy callback */
static void destroy_cb(Widget w, XtPointer client, XtPointer call)
{
	struct fsproc_data *d = (struct fsproc_data*) client;
	destroy_fsproc(d);
}

/*
 * GUI feedback dialog. Handles WP's MSG_FDBK messages and posts back
 * a response (in feedback_cb).
 */
static void feedback_dialog(struct fsproc_data *d, int type, const char *msg)
{
	Arg args[16];
	Cardinal n = 0;
	Cardinal i;
	XmString xms;
	XmString *pxms = NULL;
	static Pixmap err_pixmap = None;
	static Pixmap warn_pixmap = None;
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
		
		n = 0;
		xms = XmStringCreateLocalized(APP_TITLE);
		XtSetArg(args[n], XmNdeleteResponse, XmDO_NOTHING); n++;
		XtSetArg(args[n], XmNdialogTitle, xms); n++;
		XtSetArg(args[n], XmNnoResize, True); n++;
		XtSetArg(args[n], XmNautoUnmanage, True); n++;
		XtSetArg(args[n], XmNmarginWidth, 10); n++;
		XtSetArg(args[n], XmNmarginHeight, 10); n++;
		XtSetArg(args[n], XmNhorizontalSpacing, 8); n++;
		XtSetArg(args[n], XmNverticalSpacing, 8); n++;
		XtSetArg(args[n], XmNfractionBase, 4); n++;
		XtSetArg(args[n], XmNdialogStyle, XmDIALOG_APPLICATION_MODAL); n++;

		d->wfbdlg = XmCreateFormDialog(d->wshell,
			"feedbackDialog", args, n);
		XmStringFree(xms);

		XtSetArg(args[0], XmNmwmFunctions, MWM_FUNC_MOVE);
		XtSetValues(XtParent(d->wfbdlg), args, 1);

		warn_pixmap = get_standard_icon(d->wfbdlg, "xm_warning");
		err_pixmap = get_standard_icon(d->wfbdlg, "xm_error");
		
		n = 0;
		XtSetArg(args[n], XmNpixmapTextPadding, 8); n++;
		XtSetArg(args[n], XmNlabelType, XmPIXMAP_AND_STRING); n++;
		XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
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
			XtSetArg(args[n], XmNmarginWidth, 4); n++;
			XtSetArg(args[n], XmNmarginHeight, 4); n++;
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
	XtSetArg(args[1], XmNlabelPixmap,
		(type == FBT_CONTINUE_SKIP) ? warn_pixmap : err_pixmap);
	XtSetValues(d->wfbmsg, args, 2);
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
	write(d->reply_out_fd, &id, sizeof(int));
}

/*
 * GUI process message handler (sent by work proc's wp_post* functions)
 * Read errors are ignored here, since abnormal WP terminations are
 * handled in the SIGCHLD handler.
 */
static void progress_cb(XtPointer cd, int *pfd, XtInputId *iid)
{
	struct fsproc_data *d = (struct fsproc_data*)cd;
	int msg_type;
	ssize_t rc = 0;
	
	/* Don't bother if work proc has quit already */
	if(!d->wp_pid) {
		XtRemoveInput(d->msg_input_iid);
		d->msg_input_iid = None;
		return;
	}

	rc = read(d->msg_in_fd, &msg_type, sizeof(int));
	if(!rc) return;
	
	if(msg_type == MSG_STAT) {
		int msg_len;
		char *msg;
		
		rc = read(d->msg_in_fd, &msg_len, sizeof(int));
		if(rc < sizeof(int)) return;
		
		if(msg_len) {
			msg = malloc(msg_len + 1);
			if(!msg) return;

			rc = read(d->msg_in_fd, msg, msg_len);
			if(rc == msg_len)  {
				msg[rc] = '\0';
				set_label_string(d->wprstatus, msg);
				free(msg);
			}
		}
		
	} else if(msg_type == MSG_PROG) {
		int prog_val;
		
		rc = read(d->msg_in_fd, &prog_val, sizeof(int));
		if(rc == sizeof(int))
			ProgressSetValue(d->wprogress, prog_val);
	
	} else if(msg_type == MSG_FDBK) {
		int fb_type;
		int msg_len;
		char *msg;
		
		rc = read(d->msg_in_fd, &fb_type, sizeof(int));
		rc += read(d->msg_in_fd, &msg_len, sizeof(int));
		
		if(rc == sizeof(int) * 2) {

			msg = malloc(msg_len + 1);
			if(!msg) return;

			rc = read(d->msg_in_fd, msg, msg_len);
			if(rc < msg_len) {
				free(msg);
				return;
			}

			msg[msg_len] = '\0';

			if(fb_type == FBT_FATAL) {
				int reply = FB_CANCEL;
				
				if(d->wp_pid) {
					XtMapWidget(d->wshell);
					message_box(d->wshell, MB_ERROR, "Error", msg);
				} else {
					stderr_msg("%s: %s", "Error", msg);
				}
				/* since we don't use feedback_dialog */
				write(d->reply_out_fd, &reply, sizeof(int));
			} else {
				/* will write reply_out_fd in feedback_cb */
				feedback_dialog(d, fb_type, msg);
			}

			free(msg);
		}
	}
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
		/* TBD: eventually WP should return meaningful error codes */
		if(WEXITSTATUS(d->wp_status)) {
			message_box(d->wshell, MB_ERROR, APP_TITLE,
			"The requested action could not be completed due to an error.");
		}
	}
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
	const char *cwd;
	struct timespec proc_time[2];

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
	
	wpd->ignore_read_err = False;
	wpd->ignore_write_err = False;
	wpd->ignore_special = False;
	wpd->one_percent_size = 0;
	wpd->percent_total = 0;
	
	clock_gettime(CLOCK_MONOTONIC, &proc_time[0]);
	rsignal(SIGPIPE, wp_sigpipe_handler, 0);

	close(d->msg_in_fd);
	close(d->reply_out_fd);

	if(wpd->wdir) {
		if(chdir(wpd->wdir)) exit(EXIT_FAILURE);
		cwd = wpd->wdir;
	} else {
		cwd = get_working_dir();
		if(!cwd) exit(EXIT_FAILURE);
	}

	if((wpd->action == WP_COPY) || (wpd->action == WP_MOVE)) {
		wp_post_prog(wpd, PROG_INTERMEDIATE);
		wp_post_stat(wpd, "Gathering data...");
		wp_post_prog(wpd, 0);
		if(wp_count(wpd, cwd)) exit(EXIT_FAILURE);
	} else if(wpd->action == WP_CHATTR) {
		wp_post_prog(wpd, PROG_INTERMEDIATE);
		wp_post_stat(wpd, "Setting attributes...");		
	} else if(wpd->action == WP_DELETE) {
		wp_post_prog(wpd, PROG_INTERMEDIATE);
		wp_post_stat(wpd, "Deleting files...");		
	}

	if(wpd->dest) {
		if(stat(wpd->dest, &st_dest) == -1) {
			wp_post_msg(wpd, FBT_FATAL,
				wp_error_string("Error accessing", wpd->dest,
				NULL, strerror(errno)) );
			exit(EXIT_SUCCESS);
		} else if(!S_ISDIR(st_dest.st_mode)) {
			wp_post_msg(wpd, FBT_FATAL,
				wp_error_string("Error writing", wpd->dest,
				NULL, strerror(ENOTDIR)) );
			exit(EXIT_SUCCESS);			
		}
		cdest = realpath(wpd->dest, NULL);
		if(!cdest) exit(EXIT_FAILURE);
		
		wpd->copy_buffer_size = (st_dest.st_blksize ?
			st_dest.st_blksize : 512) * COPY_BUFFER_NBLOCKS;
		wpd->copy_buffer = malloc(wpd->copy_buffer_size);
		if(!wpd->copy_buffer) return errno;
	} else {
		wpd->copy_buffer = NULL;
	}

	for(i = 0; i < wpd->num_srcs; i++) {
		char csrc[strlen(cwd) + strlen(wpd->srcs[i]) + 2];
		char *ctitle = wpd->dsts ?
			wpd->dsts[i] : get_path_tail(wpd->srcs[i]);
		int rv = 0;

		build_path(csrc, cwd, wpd->srcs[i], NULL);
		
		retry_src: /* see FB_RETRY_CONTINUE case below */
		
		rv = lstat(csrc, &st_src);
		if(rv == -1) {
			if(errno == ENOENT || !wpd->ignore_read_err) continue;

			reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
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
			
			if(S_ISDIR(st_src.st_mode)) {
				rv = wp_delete_tree(wpd, csrc);
			} else {
				wp_post_astat(wpd, "Deleting", csrc, NULL);
				rv = wp_delete_file(wpd, csrc);
			}

			break; /* WP_DELETE */

			case WP_MOVE:
			move = True;
			case WP_COPY:

			if(S_ISDIR(st_src.st_mode)) {
				char dest_fqn[strlen(cdest) + strlen(ctitle) + 2];

				build_path(dest_fqn, cdest, ctitle, NULL);

				if(wp_check_same(csrc, cdest)) {
					wp_post_msg(wpd, FBT_SKIP_CANCEL,
						wp_error_string(move ? "Error moving" : "Error copying",
						csrc, NULL, "Source and destination are same!"));
					continue;
				}

				rv = wp_copy_tree(wpd, csrc, dest_fqn, move);
			} else if(S_ISREG(st_src.st_mode)) {
				char dest_fqn[strlen(cdest) + strlen(ctitle) + 2];

				build_path(dest_fqn, cdest, ctitle, NULL);
				wp_post_astat(wpd, move ? "Moving" : "Copying", csrc, cdest);
				rv = wp_copy_file(wpd, csrc, dest_fqn, move);

			} else if(S_ISLNK(st_src.st_mode)) {
				char dest_fqn[strlen(cdest) + strlen(ctitle) + 2];
				char *link_tgt;

				build_path(dest_fqn, cdest, ctitle, NULL);
				if( (rv = get_link_target(csrc, &link_tgt)) ) break;

				wp_post_astat(wpd, "Symlinking", link_tgt, dest_fqn);
				rv = wp_sym_link(wpd, link_tgt, dest_fqn);
				if(move && !rv) wp_delete_file(wpd, csrc);
				free(link_tgt);

			} else if(!wpd->ignore_special) {
				char *msg = wp_error_string(
					"Skipping non-regular file", cdest, NULL,
					"No attempt will be made to copy/move special files.");
				if(wp_post_msg(wpd, FBT_SKIP_CANCEL, msg)
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
			wp_post_msg(wpd, FBT_FATAL, strerror(rv));
			break;
		}
	}

	if(wpd->copy_buffer) free(wpd->copy_buffer);

	/* If it took long enough for the progress dialog to get mapped
	 * in the GUI process, hang in there a bit longer so it doesn't
	 * suddenly disappear while possibly still updating */
	clock_gettime(CLOCK_MONOTONIC, &proc_time[1]);
	
	if((proc_time[1].tv_sec - proc_time[0].tv_sec) ||
		((proc_time[1].tv_nsec - proc_time[0].tv_nsec) >
			PROG_MAP_TIMEOUT * 1000000) ) {	
			sleep(1);
	}

	exit(EXIT_SUCCESS);
	return 0;
}

/*
 * Walks through the list of sources in wpd and computes total size.
 */
static int wp_count(struct wp_data *wpd, const char *cwd)
{
	unsigned int i;
	struct stat st;
	struct fsize fs_total = { 0 };
	size_t cwd_len = strlen(cwd);
	int rv = 0;
	
	for(i = 0; i < wpd->num_srcs; i++) {
		char cname[cwd_len + strlen(wpd->srcs[i]) + 2];

		build_path(cname, cwd, wpd->srcs[i], NULL);
		
		if(!stat(cname, &st)) {
			if(S_ISDIR(st.st_mode))
				rv = wp_count_tree(cname, &fs_total);
			else
				add_fsize(&fs_total, st.st_size);
		}
		if(rv) break;
	}
	wpd->one_percent_size = (fs_total.size / 100) * fs_total.factor;

	return rv;
}

/*
 * Called by wp_count for directories.
 * Recursively walks the directory structure under top and computes total size.
 */
static int wp_count_tree(const char *top, struct fsize *fs_total)
{
	int errv = 0;
	DIR *dir;
	struct dirent *ent;
	struct stack *rec_stk;
	char *cur_path;
	size_t len = strlen(top);
	
	rec_stk = stk_alloc();
	if(!rec_stk) return ENOMEM;

	if(stk_push(rec_stk, top, len + 1)) {
		stk_free(rec_stk);
		return ENOMEM;
	}
	
	/* NOTE: cur_path is on heap and needs to be freed when done */
	while( !errv && (cur_path = stk_pop(rec_stk, NULL)) ) {
		struct stat cd_st;
		int rv;

		if(! (rv = lstat(cur_path, &cd_st)) ) {
			/* skip symbolic links to directories */
			if(S_ISLNK(cd_st.st_mode)) {
				free(cur_path);
				continue;
			}
		} else {
			free(cur_path);
			continue;
		}
		

		if(!(dir = opendir(cur_path)) ) {
			free(cur_path);
			continue;
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
			
			if(lstat(cur_fqn, &st) == -1) {
				free(cur_fqn);
				continue;
			}
			
			/* if directory, push it onto the recursion stack */
			if(S_ISDIR(st.st_mode)) {
				size_t fqn_len = strlen(cur_fqn) + 1;
				if(stk_push(rec_stk, cur_fqn, fqn_len))
					errv = ENOMEM;
			/* or proceed normally, ignoring symbolic links */
			} else if(!S_ISLNK(st.st_mode)) {
				add_fsize(fs_total, st.st_size);
			}
			free(cur_fqn);
		}
		free(cur_path);
		closedir(dir);
	}
	stk_free(rec_stk);

	return errv;
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
	struct stack *ro_stk = NULL;
	char *cur_path;
	mode_t dir_mode;
	int reply;
	size_t src_len = strlen(src);
	size_t dest_len = strlen(dest);
	
	struct link_info {
		ino_t inode;
		char name[1];
	};
	
	wp_post_prog(wpd, (int)(0.5 + wpd->percent_total));
	
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
	ro_stk = stk_alloc();
	if(!rec_stk || !in_stk || !ro_stk) {
		if(rec_stk) stk_free(rec_stk);
		if(in_stk) stk_free(in_stk);
		if(ro_stk) stk_free(ro_stk);
		return ENOMEM;
	}
	if(stk_push(rec_stk, src, src_len + 1)) {
		stk_free(in_stk);
		stk_free(ro_stk);
		stk_free(rec_stk);
		return ENOMEM;
	}
	
	if(move) {
		rem_stk = stk_alloc();
		if(!rem_stk || stk_push(rem_stk, src, src_len + 1)) {
			if(rem_stk) stk_free(rem_stk);
			stk_free(rec_stk);
			stk_free(in_stk);
			stk_free(ro_stk);
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
			wp_post_stat(wpd, (move ? "Moving..." : "Copying..."));
			reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
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
		
		/* create destination directory */
		dest_dir_fqn = build_path(NULL, dest, &cur_path[src_len], NULL);
		if(!dest_dir_fqn) {
			errv = errno;
			free(cur_path);
			break;
		}
		/* if source dir is read-only, make the copy writable temporarily */
		if( !(cd_st.st_mode & WPERM) ) {
			stk_push(ro_stk, dest_dir_fqn, strlen(dest_dir_fqn) + 1);
			dir_mode = cd_st.st_mode | S_IWUSR;
		} else {
			dir_mode = cd_st.st_mode;
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
			if(lstat(cur_fqn, &st) == -1) {
				if(wpd->ignore_read_err) {
					free(cur_fqn);
					continue;
				}

				reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
					wp_error_string("Error accessing", cur_path,
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
			
				build_path(dest_fqn, dest,
					&cur_path[src_len], ent->d_name, NULL);

				wp_post_astat(wpd, "Symlinking", link_tgt, dest_fqn);
				errv = wp_sym_link(wpd, link_tgt, dest_fqn);
				if(move && !errv) wp_delete_file(wpd, cur_fqn);
				
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

				wp_post_astat(wpd, (move ? "Moving" : "Copying"),
					cur_fqn, dest_fqn);

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
					
				if(wp_post_msg(wpd, FBT_SKIP_CANCEL, msg)
					== FB_SKIP_IGNORE_ALL) wpd->ignore_special = True;

				free(msg);
			}

			free(cur_fqn);
		}
		free(cur_path);
		if(dir) closedir(dir);
	}
	stk_free(rec_stk);
	stk_free(in_stk);
	
	/* restore read-only directory attributes, if any */
	while( (cur_path = stk_pop(ro_stk, NULL)) ) {
		struct stat st;

		if(!stat(cur_path, &st))
			chmod(cur_path, st.st_mode & (~S_IWUSR));
		free(cur_path);
	}
	stk_free(ro_stk);
	
	wp_post_prog(wpd, 100);
	
	/* purge directory structure */
	if(move) {
		wp_post_stat(wpd, "Deleting directory tree...");
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
	double size_total = 0;
	Boolean read_err = False;

	if(stat(src, &st_src)) {
		wp_post_msg(wpd, FBT_SKIP_CANCEL,
			wp_error_string("Error reading", src, NULL, strerror(errno)) );
		return 0;
	}

	if(!lstat(dest, &st_dest)) {
		static Boolean ignore_exist = False;
		int fb_type;
		char *msg;

		if(ignore_exist) goto skip_copying;

		if(S_ISDIR(st_dest.st_mode))	{
			fb_type = FBT_SKIP_CANCEL;
			msg = "Destination exists and is a directory!";
		} else if(S_ISLNK(st_dest.st_mode)) {
			fb_type = FBT_CONTINUE_SKIP;
			msg = "Destination file exists and is a symbolic link.\n"
					"No attempt will be made to write through it.\n"
					"Proceed and overwrite the symbolic link?";
		} else if(S_ISREG(st_dest.st_mode)) {
			if(st_src.st_dev == st_dest.st_dev &&
				st_src.st_ino == st_dest.st_ino) {
				fb_type = FBT_SKIP_CANCEL;
				msg = "Source and destination are the same file!";
			} else {
				fb_type = FBT_CONTINUE_SKIP;
				msg = "Destination file exists. Proceed and overwrite?";
			}
		} else {
			fb_type = FBT_SKIP_CANCEL;
			msg = "Destination file exists and is a special file.\n"
					"No attempt will be made to write to it.";
		}
		
		reply = wp_post_msg(wpd, fb_type,
			wp_error_string("Error writing", dest, NULL, msg) );

		switch(reply) {
			case FB_RETRY_CONTINUE:
			if( unlink(dest) ) {
				wp_post_msg(wpd, FBT_SKIP_CANCEL,
					wp_error_string("Error overwriting", dest,
					NULL, strerror(errno)) );
				goto skip_copying;
			}
			break;
			
			case FB_SKIP_IGNORE:
			goto skip_copying;
			break;

			case FB_SKIP_IGNORE_ALL:
			ignore_exist = 1;
			goto skip_copying;
			break;
		}	
	}

	/* try to rename the file first if moving */
	if(move && !rename(src, dest)) goto skip_copying;

	/* buffered copy */
	retry_open_src:
	fin = open(src, O_RDONLY);
	if(fin == -1) {
		if(wpd->ignore_read_err) goto skip_copying;
		
		reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
			wp_error_string("Error reading", src, NULL, strerror(errno)) );

		switch(reply) {
			case FB_RETRY_CONTINUE:
			goto retry_open_src;
			break;
			
			case FB_SKIP_IGNORE:
			goto skip_copying;
			break;


			case FB_SKIP_IGNORE_ALL:
			wpd->ignore_read_err = True;
			goto skip_copying;
			break;
		}		
	}
	
	retry_open_dest:
	fout = open(dest, O_CREAT|O_WRONLY, st_src.st_mode);
	if(fout == -1) {
		if(wpd->ignore_write_err) {
			close(fin);
			goto skip_copying;
		}
		
		reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
			wp_error_string("Error writing", dest, NULL, strerror(errno)) );

		switch(reply) {
			case FB_RETRY_CONTINUE:
			goto retry_open_dest;
			break;
			
			case FB_SKIP_IGNORE:
			close(fin);
			goto skip_copying;
			break;
			
			case FB_SKIP_IGNORE_ALL:
			wpd->ignore_write_err = True;
			close(fin);
			goto skip_copying;
			break;
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
		
		size_total += (size_t)rw;

		if(size_total >= wpd->one_percent_size) {
			wp_post_prog(wpd, (int) (0.5 + wpd->percent_total +
				(size_total / wpd->one_percent_size)) );
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

	close(fin);

	if(!res && move) {
		wp_delete_file(wpd, src);
	}
	
	if(res) {
		const char *msg = read_err ? "Error reading" : "Error writing";
		const char *name = read_err ? src : dest;
		
		/* wp_post_msg will not return if cancelled,
		 * so make sure to clean up before calling it */
		unlink(dest);
		
		reply = wp_post_msg(wpd, FBT_SKIP_CANCEL,
			wp_error_string(msg, name, NULL, strerror(res)) );
		if(reply == FB_SKIP_IGNORE_ALL) {
			if(read_err)
				wpd->ignore_read_err = True;
			else
				wpd->ignore_write_err = True;
		}
	} else if(fchmod(fout, st_src.st_mode) == -1) {
		reply = wp_post_msg(wpd, FBT_SKIP_CANCEL,
			wp_error_string("Error setting attributes", dest,
			NULL, strerror(res)) );
		if(reply == FB_SKIP_IGNORE_ALL) wpd->ignore_write_err = True;
	}

	close(fout);
	
	skip_copying:

	wpd->percent_total += 
		st_src.st_size ? ((double)st_src.st_size / wpd->one_percent_size) : 0;
	wp_post_prog(wpd, (int)(0.5 + wpd->percent_total));
	
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
		
		reply = wp_post_msg(wpd, msg_id,
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
		
		reply = wp_post_msg(wpd, msg_id,
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

		wp_post_astat(wpd, "Deleting all in", cur_path, NULL);

		retry_dir: /* see FB_RETRY_CONTINUE case below */
		if( stat(cur_path, &cd_st) || !(dir = opendir(cur_path))) {

			if(wpd->ignore_read_err) {
				free(cur_path);
				continue;
			}

			reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
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
			
			retry_sub_dir: /* see FB_RETRY_CONTINUE case below */
			if(lstat(cur_fqn, &st) == -1) {
				if(wpd->ignore_read_err) {
					free(cur_fqn);
					continue;
				}

				reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
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
		wp_post_stat(wpd, "Deleting directory tree...");
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
		int rv;

		wp_post_astat(wpd, "Setting attributes in", cur_path, NULL);

		retry_dir: /* see FB_RETRY_CONTINUE case below */

		if(! (rv = lstat(cur_path, &cd_st)) ) {
			/* skip symbolic links to directories */
			if(S_ISLNK(cd_st.st_mode)) {
				free(cur_path);
				continue;
			}
		}

		if(rv || !(dir = opendir(cur_path))) {
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

			reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
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
			if(lstat(cur_fqn, &st) == -1) {
				int st_error = errno;
				
				if(wpd->ignore_read_err) continue;
				
				reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
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
			/* or proceed normally, ignoring symbolic links */
			} else if(!S_ISLNK(st.st_mode)) {
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
		wp_post_stat(wpd, "Processing directory tree...");
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
		
		reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
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
		reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
					wp_error_string("Error deleting",
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
		reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
					wp_error_string("Error deleting",
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

	if( (ev = create_path(path, mode)) == EEXIST) ev = 0;

	if(ev) {
		int reply;
		reply = wp_post_msg(wpd, FBT_RETRY_IGNORE,
					wp_error_string("Error creating",
					path, NULL, strerror(errno)) );
		if(reply ==  FB_RETRY_CONTINUE)
			goto retry_create_path;
	}
	return ev;
}

/*
 * Sends a message to the parent (GUI) process, which displays it in a
 * blocking message dialog and returns a response ID.
 * The response FB_CANCEL is handled right here by terminating the process.
 * Any IO errors here are considered fatal.
 */
static int wp_post_msg(struct wp_data *wpd, int fb_type, const char *msg)
{
	unsigned long pid = (unsigned long)getpid();
	int msg_len = strlen(msg);
	ssize_t io, msg_data_len;
	int msg_type = MSG_FDBK;
	int oid;
	
	msg_data_len = sizeof(int) * 3 + msg_len;
	
	io = write(wpd->msg_out_fd, &msg_type, sizeof(int));
	io += write(wpd->msg_out_fd, &fb_type, sizeof(int));
	io += write(wpd->msg_out_fd, &msg_len, sizeof(int));
	io += write(wpd->msg_out_fd, msg, msg_len);

	if(io < msg_data_len) {
		stderr_msg("Subprocess %lu cannot communicate with"
			" the GUI process; exiting!\n", pid);
		exit(EXIT_FAILURE);
	}
	
	dbg_trace("%ld waiting for reply\n", getpid());

	io = read(wpd->reply_in_fd, &oid, sizeof(int));
	if(io < sizeof(int) || !FB_VALID_ID(oid)) {
		stderr_msg("Subprocess %lu received invalid response from"
			" the GUI process; exiting!\n", pid);
		exit(EXIT_FAILURE);
	} else if(fb_type == FBT_FATAL || oid == FB_CANCEL) {
		exit(EXIT_SUCCESS);
	}

	return oid;
}

/*
 * Constructs and sends an action status update message
 * to the parent (GUI) process.
 */
static void wp_post_astat(struct wp_data *wpd,
	const char *action, const char *csrc, const char *cdest)
{
	char *buffer;
	int buf_len = 0;
	int msg_type = MSG_STAT;
	char *sz_src;
	char *sz_dest;
	
	sz_src = mbs_make_displayable(csrc);

	if(mb_strlen(sz_src) > PROG_FNAME_MAX) {
		char *tmp;
		tmp = shorten_mb_string(sz_src, PROG_FNAME_MAX, True);
		if(tmp) {
			free(sz_src);
			sz_src = tmp;
		}
	}

	if(cdest) {
		sz_dest = mbs_make_displayable(cdest);
		if(mb_strlen(sz_dest) > PROG_FNAME_MAX) {
			char *tmp;
			tmp = shorten_mb_string(sz_dest, PROG_FNAME_MAX, True);
			if(tmp) {
				free(sz_dest);
				sz_dest = tmp;
			}
		}
		buf_len = snprintf(NULL, 0,	"%s \'%s\'\nto \'%s\'.",
			action, sz_src, sz_dest) + 1;

		buffer = malloc(buf_len);

		snprintf(buffer, buf_len, "%s \'%s\'\nto \'%s\'.",
			action, sz_src, sz_dest);
	} else {
		buf_len = snprintf(NULL, 0,	"%s \'%s\'.",
			action, sz_src) + 1;

		buffer = malloc(buf_len);

		snprintf(buffer, buf_len, "%s \'%s\'.",
			action, sz_src);
	}

	write(wpd->msg_out_fd, &msg_type, sizeof(int));
	buf_len = strlen(buffer);
	write(wpd->msg_out_fd, &buf_len, sizeof(int));
	write(wpd->msg_out_fd, buffer, buf_len);
}

/*
 * Sends a status update message to the parent (GUI) process.
 */
static void wp_post_stat(struct wp_data *wpd, const char *str)
{
	int msg_type = MSG_STAT;
	size_t len;

	write(wpd->msg_out_fd, &msg_type, sizeof(int));
	len = strlen(str);
	write(wpd->msg_out_fd, &len, sizeof(int));
	write(wpd->msg_out_fd, str, len);

}

/*
 * Sends total progress update to the parent (GUI) process.
 */
static void wp_post_prog(struct wp_data *wpd, int prog)
{
	int msg_type = MSG_PROG;

	if(prog > 100) prog = 100;
	write(wpd->msg_out_fd, &msg_type, sizeof(int));
	write(wpd->msg_out_fd, &prog, sizeof(int));
}

/*
 * Generates GUI friendly, formatted error messages. Non-reentrant,
 * because it maintains a buffer that it uses to return messages.
 */
static char* wp_error_string(const char *verb, const char *csrc_name,
	const char *cdest_name, const char *errstr)
{
	static size_t last_len = 0;
	static char *buffer = NULL;
	size_t len;
	char tmpl1[] = "%s \'%s\'\n%s";
	char tmpl2[] = "%s \'%s\' to \'%s\'\n%s";
	char *sz_src = NULL;
	char *sz_dest = NULL;
	
	/* Make sure names are good to display in a message box */
	sz_src = mbs_make_displayable(csrc_name);

	if(mb_strlen(sz_src) > PROG_FNAME_MAX) {
		char *tmp;
		tmp = shorten_mb_string(sz_src, PROG_FNAME_MAX, True);
		if(tmp) {
			free(sz_src);
			sz_src = tmp;
		}
	}
	
	if(cdest_name) {
		sz_dest = mbs_make_displayable(cdest_name);
		if(mb_strlen(sz_dest) > PROG_FNAME_MAX) {
			char *tmp;
			tmp = shorten_mb_string(sz_dest, PROG_FNAME_MAX, True);
			if(tmp) {
				free(sz_dest);
				sz_dest = tmp;
			}
		}
	}
	
	/* Construct the message */
	if(!sz_dest) {
		len = snprintf(NULL, 0, tmpl1, verb, sz_src, errstr) + 2;
	} else {
		len = snprintf(NULL, 0, tmpl2, verb, sz_src, sz_dest, errstr) + 2;
	}
	
	if(len > last_len) {
		buffer = realloc(buffer, len + 1);
		last_len = len;
	}

	if(!sz_dest) {
		len = snprintf(buffer, len, tmpl1, verb, sz_src, errstr);
	} else {
		len = snprintf(buffer, len, tmpl2, verb, sz_src, sz_dest, errstr);
	}
	
	/* Add a . to the end of the string if nothing there yet */
	if(len && !ispunct(buffer[len - 1])) strcat(buffer, ".");
	
	free(sz_src);
	free(sz_dest);
		
	return buffer;
}

/*
 * Returns True if dest is same as src or is its sub-directory
 * Both paths must be FQN and exist.
 */
static Boolean wp_check_same(const char *csrc, const char *cdest)
{
	char *src = realpath(csrc, NULL);
	char *dest = realpath(cdest, NULL);
	
	if(src && dest) {
		size_t src_len = strlen(src);
		size_t dest_len = strlen(dest);

		if(src_len == dest_len) {
			if(!strcmp(src, dest)) return True;
		} else if(src_len < dest_len) {
			if( (dest[src_len] == '/') &&
				!strncmp(src, dest, src_len) )
					return True;
		}
	} else {
		/* shouldn't normally happen */
		exit(EXIT_FAILURE);
	}

	if(src) free(src);
	if(dest) free(dest);
	
	return False;
}


/*
 * WP's SIGPIPE handler
 */
static void wp_sigpipe_handler(int sig)
{
	static Boolean first = True;
	
	if(first) {
		unsigned long pid = (unsigned long)getpid();
		stderr_msg("Subprocess %lu IPC failed!", pid);
		first = False;
	}
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

int copy_files(const char *wd, char * const *srcs,
	size_t num_srcs, const char *dest)
{
	int res = 0;
	struct fsproc_data *d;
	struct wp_data wpd = {
		.action = WP_COPY,
		.wdir = wd,
		.srcs = srcs,
		.dsts = NULL,
		.num_srcs = num_srcs,
		.dest = dest
	};
	
	d = init_fsproc(WP_COPY);
	if(!d) return ENOMEM;
	
	create_progress_ui(d);
	if( (res = wp_main(d, &wpd)) ) {
		destroy_progress_ui(d);
	}
	
	return res;
}

int move_files(const char *wd, char * const *srcs,
	size_t num_srcs, const char *dest)
{
	int res = 0;
	struct fsproc_data *d;
	struct wp_data wpd = {
		.action = WP_MOVE,
		.wdir = wd,
		.srcs = srcs,
		.dsts = NULL,
		.num_srcs = num_srcs,
		.dest = dest
	};
	
	d = init_fsproc(WP_MOVE);
	if(!d) return ENOMEM;
	
	create_progress_ui(d);
	if( (res = wp_main(d, &wpd)) ) {
		destroy_progress_ui(d);
	}
	
	return res;

}

int delete_files(const char *wd, char * const *srcs, size_t num_srcs)
{
	int res = 0;
	struct fsproc_data *d;
	struct wp_data wpd = {
		.action = WP_DELETE,
		.wdir = wd,
		.srcs = srcs,
		.dsts = NULL,
		.num_srcs = num_srcs,
		.dest = NULL
	};
	
	d = init_fsproc(WP_DELETE);
	if(!d) return ENOMEM;
	
	create_progress_ui(d);
	if( (res = wp_main(d, &wpd)) ) {
		destroy_progress_ui(d);
	}
	
	return res;
}

int set_attributes(const char *wd, char * const *srcs, size_t num_srcs,
	gid_t gid, uid_t uid, mode_t file_mode, mode_t dir_mode,
	mode_t file_mode_mask, mode_t dir_mode_mask,
	int att_flags)
{
	int res = 0;
	struct fsproc_data *d;
	struct wp_data wpd = {
		.action = WP_CHATTR,
		.wdir = wd,
		.srcs = srcs,
		.num_srcs = num_srcs,
		.dsts = NULL,
		.dest = NULL,
		.gid = gid,
		.uid = uid,
		.fmode = file_mode,
		.dmode = dir_mode,
		.fmode_mask = file_mode_mask,
		.dmode_mask = dir_mode_mask,
		.att_flags = att_flags
	};

	d = init_fsproc(WP_CHATTR);
	if(!d) return ENOMEM;
	
	create_progress_ui(d);
	if( (res = wp_main(d, &wpd)) ) {
		destroy_progress_ui(d);
	}
	
	return res;
}

int dup_files(const char *wd, char * const *srcs, size_t num_srcs)
{
	struct stat st;
	size_t i;
	int res = 0;
	char **dsts;
	struct fsproc_data *d;
	char *dup_suffix = app_res.dup_suffix;
	struct wp_data wpd = {
		.action = WP_COPY,
		.wdir = wd,
		.srcs = srcs,
		.dsts = NULL,
		.num_srcs = num_srcs,
		.dest = (wd ? wd : get_working_dir())
	};

	
	if(!wpd.dest) return ENOENT;

	dsts = malloc(sizeof(char*) * num_srcs);
	if(!dsts) return errno;
	
	if(num_srcs == 1) {
		unsigned short in = 0;
		char init_name[strlen(srcs[0]) + strlen(dup_suffix) + 8];
		
		sprintf(init_name, "%s%s", srcs[0], dup_suffix);

		while(!stat(init_name, &st)) {
			sprintf(init_name, "%s%s%hu", srcs[0], dup_suffix, ++in);
			if(!in) break;
		}
		
		dsts[0] = input_string_dlg(app_inst.wshell, "Duplicate Item",
			"Specify a new name", init_name, NULL, ISF_NOSLASH | ISF_PRESELECT);
		
		if(!dsts[0]) {
			free(dsts);
			return 0;
		}
	} else {
		char *suffix;
		size_t suffix_len;
		
		suffix = input_string_dlg(app_inst.wshell,
			"Duplicate Multiple Items",
			"Specify a suffix to be appended to names.\n\n"
			"A number will also be appended if a name,\n"
			"despite the suffix added, is already taken.",
			dup_suffix, NULL, ISF_NOSLASH | ISF_PRESELECT | ISF_ALLOWEMPTY);
		
		if(!suffix) {
			free(dsts);
			return 0;
		}
		
		suffix_len = strlen(suffix);
		
		for(i = 0; i < num_srcs; i++) {
			dsts[i] = malloc(strlen(srcs[i]) + suffix_len + 8);
			if(!dsts[i]) {
				while(i) free(dsts[--i]);
				free(dsts);
				free(suffix);
				return ENOMEM;
			}
		}

		for(i = 0; i < num_srcs; i++) {
			unsigned short in = 0;
			
			size_t name_len = (strlen(srcs[i]) + suffix_len + 8);
			snprintf(dsts[i], name_len, "%s%s", srcs[i], suffix);
			
			while(!stat(dsts[i], &st)) {
				snprintf(dsts[i], name_len, "%s%s%hu", srcs[i], suffix, ++in);
				if(!in) break;
			}
		}
		free(suffix);
	}

	wpd.dsts = dsts;

	d = init_fsproc(WP_COPY);
	if(d) {
		create_progress_ui(d);
		if( (res = wp_main(d, &wpd)) ) {
			destroy_progress_ui(d);
		}
	}
	
	for(i = 0; i < num_srcs; i++)
		free(dsts[i]);
	
	free(dsts);	
	
	return res;
}
