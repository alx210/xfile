/*
 * Copyright (C) 2023-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#include <Xm/Xm.h>
#include <Xm/MwmUtil.h>
#include <Xm/Form.h>
#include <Xm/LabelG.h>
#include <Xm/PushB.h>
#include <Xm/Label.h>

#include "main.h"
#include "graphics.h"
#include "guiutil.h"
#include "comdlgs.h"
#include "cbproc.h"
#include "const.h"
#include "fstab.h"
#include "exec.h"
#include "debug.h"

/* Window icon xbm */
#include "xbm/hrglas.xbm"
#include "xbm/hrglas_m.xbm"
#include "xbm/mount.xpm"
#include "xbm/umount.xpm"
#include "xbm/mnterr.xpm"

/* Forward declarations */
static void map_timeout_cb(XtPointer, XtIntervalId*);
static void term_timeout_cb(XtPointer, XtIntervalId*);
static void cmd_output_cb(XtPointer, int*, XtInputId*);
static void xt_sigchld_handler(XtPointer, XtSignalId*);
static struct mount_proc_data* alloc_mpdata(void);
static void destroy_mpdata(struct mount_proc_data*);
static void cancel_cb(Widget, XtPointer, XtPointer);
static void dialog_destroy_cb(Widget, XtPointer, XtPointer);
static void destroy_mpdata(struct mount_proc_data*);
static struct mount_proc_data* alloc_mpdata(void);
static int spawn_proc(struct mount_proc_data*, const char*, const char*);
static struct mount_proc_data* init_mount_proc(
	const char*, const char*, Boolean);

#define FB_MAP_TIMEOUT	500
#define KILL_GRACE_PERIOD 100

static const char sys_mount_cmd[] = "mount";
static const char sys_umount_cmd[] = "umount";

/* Feedback dialog data (initialized after fork) */
struct mount_proc_data {
	XtIntervalId map_iid;
	XtInputId cmd_output_iid;
	XtSignalId xt_sigchld_id;
	Widget wfbdlg;
	Widget wfbmsg;
	Widget wfbcancel;
	char *cmd_info;
	char *cmd_output;
	size_t cmd_buf_size;
	size_t cmd_buf_pos;
	int cmd_pipe_fd;

	volatile pid_t cmd_pid;
	volatile int cmd_status;
	
	struct mount_proc_data *next;
};

static struct mount_proc_data *mount_procs = NULL;
static Pixmap error_icon = None;


static struct mount_proc_data* alloc_mpdata(void)
{
	struct mount_proc_data *mp;
	
	mp = malloc(sizeof(struct mount_proc_data));
	if(!mp) return NULL;

	memset(mp, 0, sizeof(struct mount_proc_data));

	mp->cmd_pipe_fd = -1;
	
	if(mount_procs) {
		mp->next = mount_procs;
		mount_procs = mp;
	} else {
		mount_procs = mp;
	}
	return mp;
}

static void destroy_mpdata(struct mount_proc_data *mp)
{
	if(mount_procs == mp) {
		mount_procs = mp->next;
	} else if(mp->next) {
		struct mount_proc_data *cur = mount_procs;

		while(cur->next != mp) {
			cur = cur->next;
			dbg_assert(cur);
		}
		cur->next = mp->next;
	}
	if(mp->cmd_info) free(mp->cmd_info);
	if(mp->cmd_output) free(mp->cmd_output);
	if(mp->xt_sigchld_id) XtRemoveSignal(mp->xt_sigchld_id);
	if(mp->cmd_output_iid) XtRemoveInput(mp->cmd_output_iid);
	if(mp->map_iid) XtRemoveTimeOut(mp->map_iid);
	if(mp->cmd_pipe_fd >= 0) close(mp->cmd_pipe_fd);
	
	free(mp);
}

static struct mount_proc_data* init_mount_proc(
	const char *mpt_path, const char *cmd, Boolean mount)
{
	Arg args[20];
	Cardinal n;
	Widget wform;
	char *title = mount ? "Mounting" : "Unmounting";
	static Pixmap wm_icon_pix = None;
	static Pixmap wm_icon_mask = None;
	static Pixmap mount_icon = None;
	static Pixmap umount_icon = None;
	XmString xms;
	struct mount_proc_data *mp;
	char cmd_info_tmpl[] = "%s %s";
	char info_tmpl[] = "Waiting for the command to complete...\n%s";
	char *info_sz;
	size_t length;
	XtCallbackRec destroy_cbr[3] = {
		{ sub_shell_destroy_cb, NULL }, /* all sub-shells must have that */
		{ dialog_destroy_cb, NULL },
		{ NULL, NULL }
	};
	XtCallbackRec cancel_cbr[2] = {
		{ cancel_cb, NULL }, {NULL, NULL }
	};

	mp = alloc_mpdata();
	if(!mp) return NULL;

	length = snprintf(NULL, 0, cmd_info_tmpl, cmd, mpt_path) + 1;
	mp->cmd_info = malloc(length);
	snprintf(mp->cmd_info, length, cmd_info_tmpl, cmd, mpt_path);
	
	length = snprintf(NULL, 0, info_tmpl, mp->cmd_info) + 1;
	info_sz = malloc(length);
	snprintf(info_sz, length, info_tmpl, mp->cmd_info);
	
	if(!wm_icon_pix) create_wm_icon(app_inst.display,
		hrglas, &wm_icon_pix, &wm_icon_mask);
	
	n = 0;
	destroy_cbr[1].closure = (XtPointer) mp;
	XtSetArg(args[n], XmNmappedWhenManaged, False); n++;
	XtSetArg(args[n], XmNdestroyCallback, destroy_cbr); n++;
	XtSetArg(args[n], XmNmwmFunctions, MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE); n++;
	XtSetArg(args[n], XmNallowShellResize, True); n++;
	XtSetArg(args[n], XmNtitle, title); n++;
	XtSetArg(args[n], XmNiconName, title); n++;
	XtSetArg(args[n], XmNiconPixmap, wm_icon_pix); n++;
	XtSetArg(args[n], XmNiconMask, wm_icon_mask); n++;

	mp->wfbdlg = XtAppCreateShell(
		APP_NAME "Mount",
		APP_CLASS "Mount",
		applicationShellWidgetClass,
		app_inst.display, args, n);

	n = 0;
	XtSetArg(args[n], XmNautoUnmanage, True); n++;
	XtSetArg(args[n], XmNhorizontalSpacing, 8); n++;
	XtSetArg(args[n], XmNverticalSpacing, 8); n++;
	XtSetArg(args[n], XmNresizePolicy, XmRESIZE_GROW); n++;

	wform = XmCreateForm(mp->wfbdlg, "mountFeedback", args, n);

	if(!mount_icon) {
		if(!create_ui_pixmap(wform, mount_xpm, &mount_icon, NULL) ||
			!create_ui_pixmap(wform, umount_xpm, &umount_icon, NULL) ||
			!create_ui_pixmap(wform, mnterr_xpm, &error_icon, NULL)) {
				mount_icon = umount_icon = error_icon = XmUNSPECIFIED_PIXMAP;
		}
	}
	
	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNlabelPixmap, mount ? mount_icon : umount_icon); n++;
	XtSetArg(args[n], XmNlabelType, XmPIXMAP_AND_STRING); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNpixmapTextPadding, 8); n++;
	mp->wfbmsg = XmCreateLabel(wform, "mountStatus", args, n);
	set_label_string(mp->wfbmsg, info_sz);
	free(info_sz);

	n = 0;
	xms = XmStringCreateLocalized("Cancel");
	cancel_cbr[0].closure = (XtPointer)mp;
	XtSetArg(args[n], XmNactivateCallback, cancel_cbr); n++;
	XtSetArg(args[n], XmNlabelString, xms); n++;
	XtSetArg(args[n], XmNshowAsDefault, True); n++;
	XtSetArg(args[n], XmNsensitive, True); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, mp->wfbmsg); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	mp->wfbcancel = XmCreatePushButton(wform, "cancelButton", args, n);
	XmStringFree(xms);

	n = 0;
	XtSetArg(args[n], XmNdefaultButton, mp->wfbcancel); n++;
	XtSetArg(args[n], XmNcancelButton, mp->wfbcancel); n++;
	XtSetValues(mp->wfbdlg, args, n);
	
	XtManageChild(mp->wfbmsg);
	XtManageChild(mp->wfbcancel);
	XtManageChild(wform);
	
	place_shell_over(mp->wfbdlg, app_inst.wshell);
	
	XtRealizeWidget(mp->wfbdlg);
	
	/* decremented in sub_shell_destroy_cb */
	app_inst.num_sub_shells++;

	mp->map_iid = XtAppAddTimeOut(app_inst.context,
		FB_MAP_TIMEOUT, map_timeout_cb, (XtPointer)mp);
	
	return mp;
}

static void dialog_destroy_cb(Widget w, XtPointer client, XtPointer call)
{
	struct mount_proc_data *mp = (struct mount_proc_data*)client;

	if(mp->cmd_pid) kill(mp->cmd_pid, SIGKILL);
	destroy_mpdata((struct mount_proc_data*)client);
}


/* Deferred shell mapping timeout callback */
static void map_timeout_cb(XtPointer data, XtIntervalId *iid)
{
	struct mount_proc_data *mp = (struct mount_proc_data*)data;
	XtMapWidget(mp->wfbdlg);
}

static void cancel_cb(Widget w, XtPointer client, XtPointer call)
{
	struct mount_proc_data *mp = (struct mount_proc_data*)client;
	
	XtSetSensitive(w, False);
	
	if(mp->cmd_pid) {
		kill(mp->cmd_pid, SIGTERM);
		XtAppAddTimeOut(app_inst.context, 
			KILL_GRACE_PERIOD, term_timeout_cb, (XtPointer)mp);
	} else {
		XtDestroyWidget(mp->wfbdlg);
	}
}

/* Grace period for mount cmd to react to SIGTERM is over */
static void term_timeout_cb(XtPointer data, XtIntervalId *iid)
{
	struct mount_proc_data *mp = (struct mount_proc_data*)data;
	if(mp->cmd_pid) kill(mp->cmd_pid, SIGKILL);

	XtDestroyWidget(mp->wfbdlg);
}

/* Command stdout/stderr output capture callback */
static void cmd_output_cb(XtPointer data, int *pfd, XtInputId *piid)
{
	struct mount_proc_data *mp = (struct mount_proc_data*)data;
	const size_t read_size = 80;
	ssize_t bytes_read = 0;
	
	do {
		if((mp->cmd_buf_size - mp->cmd_buf_pos) < read_size) {
			char *ptr;
			size_t buf_size = mp->cmd_buf_size + read_size;

			ptr = realloc(mp->cmd_output, buf_size + 1);
			if(!ptr) {
				XtRemoveInput(mp->cmd_output_iid);
				mp->cmd_output_iid = None;
				return;
			}
			mp->cmd_output = ptr;
			mp->cmd_buf_size = buf_size;
		}

		bytes_read = read(mp->cmd_pipe_fd,
			(mp->cmd_output + mp->cmd_buf_pos), read_size);
		if(bytes_read > 0) {
			mp->cmd_buf_pos += (size_t) bytes_read;
		} else if(bytes_read == (-1)) {
			XtRemoveInput(mp->cmd_output_iid);
			mp->cmd_output_iid = None;
		}
	} while(!mp->cmd_pid && (bytes_read > 0));
	
	mp->cmd_output[mp->cmd_buf_pos] = '\0';
}

/* Synchronous part of the sigchld_handler (updates the feedback dialog)*/
static void xt_sigchld_handler(XtPointer data, XtSignalId *id)
{
	struct mount_proc_data *mp = (struct mount_proc_data*)data;
	
	if(WEXITSTATUS(mp->cmd_status) ) {
		Arg arg;
		XmString xms;
		char *msg;
		size_t msg_len;
		
		xms = XmStringCreateLocalized("Dismiss");
		XtSetArg(arg, XmNlabelString, xms);
		XtSetValues(mp->wfbcancel, &arg, 1);
		XmStringFree(xms);
		
		XtSetArg(arg, XmNlabelPixmap, error_icon);
		XtSetValues(mp->wfbmsg, &arg, 1);
		
		if(XtAppPending(app_inst.context) & XtIMAlternateInput) {
			XtAppProcessEvent(app_inst.context, XtIMAlternateInput);
		}

		if(mp->cmd_output && strlen(mp->cmd_output)) {
			char msg_tmpl[] = "Command \"%s\" failed with:\n%s";
			
			msg_len = snprintf(NULL, 0, msg_tmpl,
				mp->cmd_info, mp->cmd_output) + 1;
			msg = malloc(msg_len);
			snprintf(msg, msg_len, msg_tmpl, mp->cmd_info, mp->cmd_output);
		} else {
			char msg_tmpl[] = "Command \"%s\" failed "
				"without producing any output.";

			msg_len = snprintf(NULL, 0, msg_tmpl, mp->cmd_info) + 1;
			msg = malloc(msg_len);
			snprintf(msg, msg_len, msg_tmpl, mp->cmd_info);
		}
		set_label_string(mp->wfbmsg, msg);
		free(msg);
	} else {
		XtSetSensitive(mp->wfbcancel, False);
		XtDestroyWidget(mp->wfbdlg);
	}
}

/*
 * Forks off a new process to exec the mount command and capture its output.
 */
static int spawn_proc(struct mount_proc_data *mp,
	const char *cmd, const char *mnt_path)
{
	int pipe_fd[2];
	
	if(pipe(pipe_fd)) return errno;

	mp->cmd_pipe_fd = pipe_fd[0];
	fcntl(mp->cmd_pipe_fd, F_SETFL, O_NONBLOCK);

	mp->cmd_output_iid = XtAppAddInput(app_inst.context, pipe_fd[0],
		(XtPointer)XtInputReadMask, cmd_output_cb, (XtPointer)mp);

	mp->xt_sigchld_id = XtAppAddSignal(app_inst.context,
		xt_sigchld_handler, (XtPointer)mp);

	mp->cmd_pid = fork();
	if(mp->cmd_pid == (-1)) {
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		return errno;
	}
	
	if(!mp->cmd_pid) {
		char *tmp;
		char **args;
		size_t nargs;
		int rv = 0;
		
		close(pipe_fd[0]);
		dup2(pipe_fd[1], fileno(stderr));
		#ifdef MOUNT_CAPTURE_STDOUT
		dup2(pipe_fd[1], fileno(stdout));
		#endif

		tmp = strdup(cmd);		
		rv = split_arguments(tmp, &args, &nargs);
		if(rv) {
			fprintf(stderr, "%s\n", strerror(rv));
			_exit(EXIT_FAILURE);
		}
		args = realloc(args, (nargs + 1) * sizeof(char*));
		if(!args) {
			fprintf(stderr, "%s\n", strerror(errno));
			_exit(EXIT_FAILURE);
		}
		
		args[nargs] = (char*)mnt_path;
		args[nargs + 1] = NULL;
		
		if(execvp(args[0], args)) {
			fprintf(stderr, "Cannot exec %s.\n%s.\n", cmd, strerror(errno));
			_exit(EXIT_FAILURE);
		}
	}
	close(pipe_fd[1]);
	return 0;
}


/*
 * Async routine called from SIGCHLD handler in main.c
 */
Boolean mount_proc_sigchld(pid_t pid, int status)
{
	struct mount_proc_data *mp = mount_procs;
	
	while(mp) {
		if(pid == mp->cmd_pid) {
			dbg_trace("mount pid %d quit\n", pid);
			if(WIFEXITED(status)) {
				mp->cmd_pid = 0;
				mp->cmd_status = status;
				XtNoticeSignal(mp->xt_sigchld_id);
			}
			return True;
		}
		mp = mp->next;
	}
	return False;
}

int exec_mount(const char *mpt_path)
{
	int res = 0;
	static struct mount_proc_data *mp;
	char *cmd = (char*) sys_mount_cmd;
	
	if(app_res.media_dir &&
		!strncmp(mpt_path, app_res.media_dir, strlen(app_res.media_dir)) ) {
		cmd = app_res.media_mount_cmd;
		if(!cmd) {
			message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
				"The mediaMountCommand X resource must be also set if "
				"mediaDirectory is specified!");
			return EINVAL;
		}
	}
	
	mp = init_mount_proc(mpt_path, cmd, True);
	if(!mp) return ENOMEM;

	res = spawn_proc(mp, cmd, mpt_path);
	if(res) XtDestroyWidget(mp->wfbdlg);

	return res;
}

int exec_umount(const char *mpt_path)
{
	int res = 0;
	static struct mount_proc_data *mp;
	char *cmd = (char*) sys_umount_cmd;
	
	if(app_res.media_dir &&
		!strncmp(mpt_path, app_res.media_dir, strlen(app_res.media_dir)) ) {
		cmd = app_res.media_umount_cmd;
		if(!cmd) {
			message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
				"The mediaUnmountCommand X resource must be also set if "
				"mediaDirectory is specified!");
			return EINVAL;
		}
	}

	mp = init_mount_proc(mpt_path, cmd, False);
	if(!mp)	return ENOMEM;

	res = spawn_proc(mp, cmd, mpt_path);
	if(res) XtDestroyWidget(mp->wfbdlg);
	
	return res;
}
