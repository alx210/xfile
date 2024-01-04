/*
 * Copyright (C) 2023 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include "menu.h"
#include "comdlgs.h"
#include "main.h"
#include "const.h"
#include "typedb.h"
#include "filemgr.h"
#include "graphics.h"
#include "listw.h"
#include "pathw.h"
#include "path.h"
#include "fstab.h"
#include "fsutil.h"
#include "debug.h"
#include "memdb.h" /* must be the last header */


/* Reader/watcher process data */
struct read_proc_data {
	volatile pid_t pid;
	volatile int status;
	XtInputId iid;
	XtSignalId sigid;
	int in_fd;
	int out_fd;
	Boolean init_done;
};

/* Reader/watcher message data */
enum msg_reason {
	MSG_ADD,
	MSG_REMOVE,
	MSG_UPDATE,
	MSG_EOD
};

struct msg_data {
	int reason;
	int stat_errno;
	mode_t mode;
	Boolean is_symlink;
	Boolean is_mpoint;
	Boolean is_mounted;
	time_t ctime;
	time_t mtime;
	gid_t gid;
	uid_t uid;
	int db_index;
	off_t size;
	size_t name_len;
	unsigned int files_total;
	unsigned int files_skipped;
	unsigned long size_total;
};

struct watch_rec {
	char *name;
	time_t mtime;
	Boolean is_mpoint;
	dev_t device;
	Boolean touched;
};


/* Watcher process  nice() value*/
#ifndef WATCH_PROC_NICE
#define WATCH_PROC_NICE	10
#endif

/* Reader/watcher process return values */
#define RP_SUCCES 0
#define RP_ENOACC 1
#define RP_ENOMEM 2
#define RP_IOFAIL 3

/* Used by the watcher process */
#ifndef FILE_LIST_GROW_BY
#define FILE_LIST_GROW_BY 256
#endif

/* Status-bar update interval in MS (while reading a directory) */
#ifndef STATUS_UPDATE_INT
#define STATUS_UPDATE_INT 250
#endif

/* Local prototypes */
static int read_directory(void);
static int read_proc_main(pid_t, int);
static int read_proc_watch(const char*, pid_t, int,
	struct watch_rec*, size_t, size_t, Boolean);
static void reader_callback_proc(XtPointer, int*, XtInputId*);
static void read_error_msg(const char*, const char*, Boolean);
static void xt_read_proc_sig_handler(XtPointer,XtSignalId*);
static void read_proc_sigterm(int sig);
static void read_proc_sigalrm(int sig);
static Boolean filter(const char*, mode_t);
static void status_timeout_cb(XtPointer, XtIntervalId*);
static void reset_context_data(void);

/* Local variables */
static struct read_proc_data rp_data = {0};
static XtIntervalId xt_update_iid = None;

/*
 * One time file manager iniialization routine.
 * Creates communication pipes and registers them with Xt.
 */
int initialize(void)
{
	int res;
	int pipe_fd[2];
	
	res = pipe(pipe_fd);
	if(res) return errno;
	
	rp_data.in_fd = pipe_fd[0];
	rp_data.out_fd = pipe_fd[1];
	
	rp_data.sigid = XtAppAddSignal(app_inst.context,
			xt_read_proc_sig_handler, NULL);

	rp_data.iid = XtAppAddInput(app_inst.context, rp_data.in_fd,
		(XtPointer)XtInputReadMask, reader_callback_proc, NULL);
	
	return 0;
}

/* 
 * Reads directory specified; if absolute is False, path is appended
 * to the current location. Returns 0 on success, errno otherwise.
 */
int set_location(const char *path, Boolean absolute)
{
	struct stat st;
	char *psz;
	int err = 0;

	dbg_assert(path);
	
	if(absolute) {
		psz = strdup(path);

		if(!psz) {
			err = errno;
			read_error_msg(path, strerror(err), True);
			return err;
		}
	} else {
		dbg_assert(app_inst.location); /* relative to what? */
		
		size_t len = strlen(app_inst.location) + strlen(path) + 2;
		
		psz = malloc(len);
		sprintf(psz, "%s/%s", app_inst.location, path);
		strip_path(psz);
	}
	
	/* check if it's a directory with R+X access */
	if(stat(psz, &st) == -1) {
		err = errno;
		read_error_msg(psz, strerror(err), True);
	} else if(!S_ISDIR(st.st_mode)) {
		err = ENOTDIR;
		read_error_msg(psz, strerror(err), True);
	}
	if(err) {
		free(psz);
		return err;
	}
	
	dbg_trace("changing to: %s\n", psz);
	if(chdir(psz)) {
		read_error_msg(psz, strerror(errno), True);
		free(psz);
		return errno;
	}
	
	set_ui_sensitivity(0);
	update_context_menus(NULL, 0, 0);
	
	if(app_inst.location) free(app_inst.location);
	app_inst.location = psz;
	
	path_field_set_location(app_inst.wpath, psz, False);
	
	return read_directory();
}

/*
 * Discards all data and rereads current directory.
 */
int reread(void)
{
	dbg_assert(app_inst.location);
	return read_directory();
}

/*
 * Forces the reader process to check for changes
 */
void force_update(void)
{
	if(!rp_data.pid) {
		dbg_trace("NO watcher process\n");
		return;
	}
	kill(rp_data.pid, SIGALRM);
}

/*
 * Updates file manager context menus
 */
void update_context_menus(const struct ctx_menu_item *items,
	int nitems, int idefault)
{
	static Boolean inited = False;
	static struct ctx_menu_data file_ctx = {None, NULL, 0};
	static struct ctx_menu_data popup_ctx = {None, NULL, 0};	
	
	if(!inited) {
		dbg_assert(app_inst.wmfile && app_inst.wmctx);

		file_ctx.wmenu = app_inst.wmfile,
		popup_ctx.wmenu = app_inst.wmctx,
		inited = True;
	}

	modify_context_menu(&file_ctx, items, nitems, idefault);
	modify_context_menu(&popup_ctx, items, nitems, idefault);
}

/*
 * Sets detault (directory contents) status text
 */
void set_default_status_text(void)
{
	char sz_size[SIZE_CS_MAX];
	char *item_noun = (app_inst.nfiles_shown > 1) ? "items" : "item";
	
	if(!(app_inst.nfiles_shown + app_inst.nfiles_hidden)) {
		set_status_text("No items to display");
		return;
	}
	
	if(app_inst.size_shown) {
		if(app_inst.nfiles_hidden)
			set_status_text("%s in %u %s (%u %s)",
				get_size_string(app_inst.size_shown, sz_size),
				app_inst.nfiles_shown, item_noun,
				app_inst.nfiles_hidden, "not shown");
		else
			set_status_text("%s in %u %s",
				get_size_string(app_inst.size_shown, sz_size),
				app_inst.nfiles_shown, item_noun);
	} else {
		if(app_inst.nfiles_hidden)
			set_status_text("%u %s (%u %s)",
				app_inst.nfiles_shown, item_noun,
				app_inst.nfiles_hidden, "not shown");
		else
			set_status_text("%u %s", app_inst.nfiles_shown, item_noun);
	}
}

void set_sel_status_text(void)
{
	struct stat st;
	char sz_size[SIZE_CS_MAX];
	char sz_mode[MODE_CS_MAX];
			
	if(app_inst.cur_sel.count > 1) {

		Boolean enoent = False;
		size_t size_total = 0;
		unsigned int i;
		unsigned int n;
		
		for(i = 0, n = 0; i < app_inst.cur_sel.count; i++) {
			if(!stat(app_inst.cur_sel.names[i], &st)) {
				size_total += st.st_size;
				n++;
			} else if(errno == ENOENT) {
				enoent = True;
			}
		}
		set_status_text("%s in %u items selected",
			get_size_string(size_total, sz_size), n);

		if(enoent) force_update();

	} else if(app_inst.cur_sel.count == 1) {
		char *sz_owner;
		char *disp_name;
		struct passwd *pw;
		struct group *gr;

		disp_name = gronk_ctrl_chars(app_inst.cur_sel.names[0]);
		
		if(stat(app_inst.cur_sel.names[0], &st)) {
			if(errno == ENOENT) {
		 		force_update();
			} else {
				set_status_text("Cannot stat '\%s\': %s",
					disp_name, strerror(errno));
			}
			return;
		}

		gr = getgrgid(st.st_gid);
		pw = getpwuid(st.st_uid);

		if(gr && pw) {
			size_t len = strlen(gr->gr_name) + strlen(pw->pw_name);
			sz_owner = malloc(len + 3);
			sprintf(sz_owner, "%s:%s", pw->pw_name, gr->gr_name);
		} else {
			sz_owner = malloc(32);
			snprintf(sz_owner, 32, "%d:%d", st.st_uid, st.st_gid);
		}
		
		get_mode_string(st.st_mode, sz_mode);
		get_size_string(st.st_size, sz_size);
		
		set_status_text("%s  %s  %s  %s",
			disp_name, sz_mode, sz_owner, sz_size);
		
		free(disp_name);
		free(sz_owner);
	} else {
		set_default_status_text();
	}
}

/*
 * Asynchronous routine, called by the GUI process SIGCHLD handler.
 * Checks whether pid is a reader/watcher proc and sets up a
 * deferred GUI routine that reports an error if necessary.
 * Returns False if pid is not a reader/watcher process.
 */
Boolean read_proc_sigchld(pid_t pid, int status)
{
	if(rp_data.pid == pid) {
		dbg_printf("%d: signaled parent\n", pid);
		rp_data.pid = 0;
		rp_data.status = status;
		XtNoticeSignal(rp_data.sigid);
		return True;
	}
	return False;
}

/*
 * Deferred (XtNoticeSignal) SIGCHLD handler triggered in read_proc_exit
 */
static void xt_read_proc_sig_handler(XtPointer p, XtSignalId *id)
{
	if(xt_update_iid) {
		XtRemoveTimeOut(xt_update_iid);
		xt_update_iid = None;
	}
	
	if( WIFEXITED(rp_data.status) && WEXITSTATUS(rp_data.status) ) {
		char *errcs;
		
		switch(WEXITSTATUS(rp_data.status)) {
			case RP_ENOACC:
			errcs = "The location is not accessible";
			break;
			case RP_ENOMEM:
			errcs = "Memory allocation error";
			break;
			case RP_IOFAIL:
			errcs = "Data I/O error";
			break;
			default:
			errcs = "Unexpected error";
			break;
		}
		
		read_error_msg(app_inst.location, errcs, False);

	} else if((WIFSIGNALED(rp_data.status) &&
		(WTERMSIG(rp_data.status) != SIGTERM))) {
		read_error_msg(app_inst.location,
			"Process terminated unexpectedly", False);
	}
	reset_context_data();
}
	
/*
 * Resets global context data 
 */
static void reset_context_data(void)
{
	memset(&app_inst.cur_sel, 0, sizeof(struct file_list_sel));
	app_inst.nfiles_read = 0;
	app_inst.nfiles_shown = 0;
	app_inst.nfiles_hidden = 0;
	app_inst.size_shown = 0;
	
	file_list_remove_all(app_inst.wlist);
	file_list_show_contents(app_inst.wlist, False);
	
	rp_data.init_done = False;

	set_default_status_text();
	set_ui_sensitivity(0);
	update_shell_title(NULL);
}


/*
 * Forks off a directory reader.
 */
static int read_directory(void)
{
	pid_t pid;
	pid_t parent_pid = getpid();
	
	set_status_text("Reading %s...", app_inst.location);

	if(rp_data.pid) stop_read_proc();

	/* reset global context data */
	memset(&app_inst.cur_sel, 0, sizeof(struct file_list_sel));
	app_inst.nfiles_read = 0;
	app_inst.nfiles_shown = 0;
	app_inst.nfiles_hidden = 0;
	app_inst.size_shown = 0;
	
	file_list_remove_all(app_inst.wlist);
	file_list_show_contents(app_inst.wlist, False);
	
	/* reset reader proc data */
	rp_data.init_done = False;

	pid = fork();
	if(pid == (-1)) return errno;
	
	if(!pid) {
		int res;
		
		close(XConnectionNumber(app_inst.display));
		close(rp_data.in_fd);
		res = read_proc_main(parent_pid, rp_data.out_fd);
		
		_exit(res);
	}

	rp_data.pid = pid;

	xt_update_iid = XtAppAddTimeOut(app_inst.context,
		STATUS_UPDATE_INT, status_timeout_cb, NULL);

	return 0;
}

/*
 * Kills the directory reader/watcher process if it's active
 */
void stop_read_proc(void)
{
	if(xt_update_iid) {
		XtRemoveTimeOut(xt_update_iid);
		xt_update_iid = None;
	}

	if(rp_data.pid){
		pid_t pid = rp_data.pid;
		dbg_printf("waiting for %d to exit\n", rp_data.pid);
		rp_data.pid = 0;
		kill(pid, SIGKILL);
		waitpid(pid, (int*)&rp_data.status, 0);
	}
	reset_context_data();
}


/*
 * Called periodically to update status bar text while a directory is read
 */
static void status_timeout_cb(XtPointer data, XtIntervalId *iid)
{
	if(rp_data.init_done) return;
	
	set_status_text("Reading %s (%u items)",
		app_inst.location, app_inst.nfiles_read);
	XtAppAddTimeOut(app_inst.context,
		STATUS_UPDATE_INT, status_timeout_cb, NULL);
}

/*
 * Processes messages sent by directory reader
 */
static void reader_callback_proc(XtPointer cd, int *pfd, XtInputId *iid)
{
	/* fname_buf is reused across calls and just grows if necessary */
	static char *fname_buf = NULL;
	static size_t buf_size = 0;
	
	struct file_list_item fli;
	struct msg_data msg;
	struct file_type_rec *ft = NULL;
	Pixmap pm_icon;
	Pixmap pm_mask;
	Boolean update = False;
	int res;

	if(read(*pfd, &msg, sizeof(struct msg_data)) == -1) {
			read_error_msg(app_inst.location, strerror(errno), False);
			stop_read_proc();
			return;
	}

	if(msg.name_len > buf_size) {
		char *new_ptr;
		new_ptr = realloc(fname_buf, (buf_size += 256));
		if(!new_ptr) {
			read_error_msg(app_inst.location, strerror(errno), False);
			buf_size = 0;
			stop_read_proc();
			return;
		}
		fname_buf = new_ptr;
	}

	if(msg.name_len) {
		if(read(*pfd, fname_buf, msg.name_len) == -1) {
			read_error_msg(app_inst.location, strerror(errno), False);
			stop_read_proc();
			return;
		}			
		fname_buf[msg.name_len] = '\0';
	}
	
	switch(msg.reason) {
		case MSG_EOD: {
			Boolean changed = 
				(app_inst.nfiles_hidden != msg.files_skipped ||
				app_inst.nfiles_shown != msg.files_total ||
				app_inst.size_shown != msg.size_total) ? True : False;

			app_inst.nfiles_hidden = msg.files_skipped;
			app_inst.nfiles_shown = msg.files_total;
			app_inst.size_shown = msg.size_total;

			if(!rp_data.init_done) {
				rp_data.init_done = True;
				changed = True;
				if(xt_update_iid) {
					XtRemoveTimeOut(xt_update_iid);
					xt_update_iid = None;
				}

				set_ui_sensitivity(UIF_DIR);
				file_list_show_contents(app_inst.wlist, True);
				update_shell_title(app_inst.location);
			}

			if(changed) {
				if(app_inst.cur_sel.count)
					set_sel_status_text();
				else
					set_default_status_text();
			}
		} break;
		
		case MSG_UPDATE:
		update = True; /* ...and fall through */
		case MSG_ADD:

		if(DB_DEFINED(msg.db_index)) {
			ft = &app_inst.type_db.recs[msg.db_index];
		}

		/* figure out what icon to use if no DB match, or pixmap is missing */
		if(!ft || !ft->icon_name ||
			!get_icon_pixmap(ft->icon_name,
				app_inst.icon_size_id, &pm_icon, &pm_mask)) {

			char *icon_name;

			if(msg.is_symlink && msg.stat_errno) {
				icon_name = ICON_DLNK;
			} else {
				switch(msg.mode & S_IFMT) {
					case S_IFREG:
						if(DB_ISTEXT(msg.db_index))
							icon_name = ICON_TEXT;
						else if(DB_ISBIN(msg.db_index))
							icon_name = ICON_BIN;
						else
							icon_name = ICON_FILE;
					break;
					
					case S_IFDIR:
						if(access(fname_buf, R_OK | X_OK)) {
							icon_name = ICON_NXDIR;
						} else if(msg.is_mpoint) {
							if(msg.is_mounted)
								icon_name = ICON_MPT;
							else
								icon_name = ICON_MPTI;
						} else {
							icon_name = ICON_DIR;
						}
					break;
					
					default:
						icon_name = ICON_FILE;
					break;
				}
			}
			get_icon_pixmap(icon_name,
				app_inst.icon_size_id, &pm_icon, &pm_mask);
		}
		
		fli.name = fname_buf;
		fli.title = fname_buf;
		fli.db_type = msg.db_index;
		fli.size = msg.size;
		fli.mode = msg.mode;
		fli.uid = msg.uid;
		fli.gid = msg.gid;
		fli.ctime = msg.ctime;
		fli.mtime = msg.mtime;
		fli.icon = pm_icon;
		fli.icon_mask = pm_mask;
		fli.is_symlink = msg.is_symlink;
		fli.user_flags = (msg.is_mpoint ? FLI_MNTPOINT : 0) |
			(msg.is_mounted ? FLI_MOUNTED : 0);
		
		res = file_list_add(app_inst.wlist, &fli, update);
		
		app_inst.nfiles_read = msg.files_total;
		
		if(res)	{
			read_error_msg(app_inst.location, strerror(res), False);
			stop_read_proc();
		}		
		break;
		
		case MSG_REMOVE:
		file_list_remove(app_inst.wlist, fname_buf);
		break;
	}
}

/*
 * Reader error message reporting convenience routine.
 */
static void read_error_msg(const char *path, const char *estr, Boolean blocking)
{
	char tmpl[] = "Error reading %s\n%s.";
	char *buffer;

	if(!estr) estr = "Unexpected error";
	buffer = malloc(snprintf(NULL, 0, tmpl, path, estr) + 1);
	sprintf(buffer, tmpl, path, estr);
	message_box(app_inst.wshell,
		(blocking ? MB_ERROR : MB_ERROR_NB), APP_TITLE, buffer);
	free(buffer);
}

/*
 * Returns True if file_name should be displayed
 */
static Boolean filter(const char *file_name, mode_t mode)
{
	if(file_name[0] == '.' && !app_res.show_all)
		return False;

	if(S_ISDIR(mode) && !app_res.filter_dirs)
		return True;

	if(app_inst.filter) {
		char *filter = app_inst.filter;
		Boolean invert = False;
		Boolean match;
		
		if(*filter == '!') {
			filter++;
			invert = True;
		}
		match = (fnmatch(filter, file_name, 0) == 0) ? False : True;
		if(invert) match = match ? False : True;
		
		return match;
	}
	return True;
}


/*
 * Directory reader process entry point.
 * Reads the CWD, then enters the 'watch' routine.
 */
static int read_proc_main(pid_t parent_pid, int pipe_fd)
{
	char *cur_path;
	DIR *dir = NULL;
	struct dirent *ent;
	struct stat st;
	struct msg_data msg;
	struct watch_rec *file_list = NULL;
	size_t list_size = 0;
	size_t nfiles = 0;
	unsigned int files_total = 0;
	unsigned int files_skipped = 0;
	unsigned int size_total = 0;
	Boolean has_mpts;
	ssize_t out;
	
	dbg_printf("%d: new read/watch process\n", getpid());
	rsignal(SIGALRM, read_proc_sigalrm, 0);
	rsignal(SIGTERM, read_proc_sigterm, 0);
	
	cur_path = get_working_dir();
	if(!cur_path) return RP_ENOMEM;
	
	has_mpts = (has_fstab_entries(cur_path) ? True : False);

	dbg_trace("%s: %s mount points\n", cur_path,
		(has_mpts ? "has" : "doesn't have"));
	
	if(!(dir = opendir(cur_path))){
		dbg_printf("%d: can't opendir cwd\n", getpid());
		return RP_ENOACC;
	}

	while((ent = readdir(dir))){
		Boolean is_mpoint = False;
		Boolean is_mounted = False;
		msg.stat_errno = 0;
		
		if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		
		if(lstat(ent->d_name, &st) == -1) {
			msg.stat_errno = errno;
			memset(&st, 0, sizeof(struct stat));
		} else if(S_ISLNK(st.st_mode)) {
			msg.is_symlink = True;
			if(stat(ent->d_name, &st) == -1) {
				msg.stat_errno = errno;
				memset(&st, 0, sizeof(struct stat));
			}
		} else {
			msg.is_symlink = False;
		}

		if( !filter(ent->d_name, st.st_mode) ) {
			files_skipped++;
			continue;
		}

		files_total++;
		size_total += st.st_size;
	
		/* file list for the watch routine */
		if(list_size < (nfiles + 1)) {
			size_t new_size = list_size + FILE_LIST_GROW_BY;
			file_list = realloc(file_list, sizeof(struct watch_rec) * new_size);
			if(!file_list) return RP_ENOMEM;
			list_size = new_size;
		}
		file_list[nfiles].name = strdup(ent->d_name);
		file_list[nfiles].mtime = st.st_mtime;
		file_list[nfiles].device = st.st_dev;
		file_list[nfiles].touched = False;
		
		/* is it a mount point ? */
		if(S_ISDIR(st.st_mode)) {
			char cur_fqn[strlen(cur_path) + strlen(ent->d_name) + 2];
			sprintf(cur_fqn, "%s/%s", cur_path, ent->d_name);
			
			if(path_mounted(cur_fqn)) {
				is_mpoint = True;
				is_mounted = True;
			} if(has_mpts) {
				is_mpoint = (is_in_fstab(cur_fqn) ? True : False);
			}
		}
		file_list[nfiles].is_mpoint = is_mpoint;

		nfiles++;
		
		/* message */
		if(S_ISREG(st.st_mode)) {
			msg.db_index = db_match(ent->d_name, &app_inst.type_db);
		} else {
			msg.db_index = DB_UNKNOWN;
		}
		msg.name_len = strlen(ent->d_name);
		msg.size = st.st_size;
		msg.mode = st.st_mode;
		msg.ctime = st.st_ctime;
		msg.mtime = st.st_mtime;
		msg.gid = st.st_gid;
		msg.uid = st.st_uid;
		msg.reason = MSG_ADD;
		msg.files_total = files_total;
		msg.files_skipped = files_skipped;
		msg.size_total = size_total;
		msg.is_mpoint = is_mpoint;
		msg.is_mounted = is_mounted;
		
		out = write(pipe_fd, &msg, sizeof(struct msg_data));
		out += write(pipe_fd, ent->d_name, msg.name_len);
		if(out < (sizeof(struct msg_data) + msg.name_len))
				return RP_IOFAIL;
	}
	closedir(dir);

	memset(&msg, 0, sizeof(struct msg_data));
	msg.reason = MSG_EOD;
	msg.files_total = files_total;
	msg.files_skipped = files_skipped;
	msg.size_total = size_total;
	out = write(pipe_fd, &msg, sizeof(struct msg_data));
	if(out < sizeof(struct msg_data))
		return RP_IOFAIL;
	
	/* read_proc_watch returns on failure only */
 	return read_proc_watch(cur_path, parent_pid, pipe_fd,
		file_list, list_size, nfiles, has_mpts);
}

/*
 * Directory reader process watch routine.
 * Checks for changes in file_list (modifying it accordingly)
 * at regular intervals and notifies the parent process of these.
 * Returns on failure only.
 */
static int read_proc_watch(const char *path, pid_t parent_pid,
	int pipe_fd, struct watch_rec *file_list,
	size_t list_size, size_t nfiles, Boolean has_mpts)
{
	struct msg_data msg;
	DIR *dir;
	struct dirent *ent;
	struct stat st;
	size_t i;
	ssize_t out;
	
	nice(WATCH_PROC_NICE);
	
	/* Loop until signaled or reparented, watching for directory changes,
	 * sleeping refresh_int secs between iterations */
	while(getppid() == parent_pid) {
		unsigned int files_total = 0;
		unsigned int files_skipped = 0;
		unsigned int size_total = 0;
	
		dir = opendir(path);
		if(!dir) return RP_ENOACC;
		
		while((ent = readdir(dir))){
			Boolean is_mpoint = False;
			Boolean is_mounted = False;

			if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;

			for(i = 0; i < nfiles; i++) {
				if(!strcmp(file_list[i].name, ent->d_name)) {
					file_list[i].touched = True;
					break;
				}
			}

			if(lstat(ent->d_name, &st) == -1) {
				msg.stat_errno = errno;
				memset(&st, 0, sizeof(struct stat));
			} else if(S_ISLNK(st.st_mode)) {
				msg.is_symlink = True;
				if(stat(ent->d_name, &st) == -1) {
					msg.stat_errno = errno;
					memset(&st, 0, sizeof(struct stat));
				}
			} else {
				msg.is_symlink = False;
			}

			if( !filter(ent->d_name, st.st_mode) ) {
				files_skipped++;
				continue;
			}

			files_total++;
			size_total += st.st_size;
			
			/* new file */
			if(i == nfiles) {
				if(list_size < (nfiles + 1)) {
					size_t new_size = list_size + FILE_LIST_GROW_BY;
					file_list = realloc(file_list,
						sizeof(struct watch_rec) * new_size);
					if(!file_list) return RP_ENOMEM;
					list_size = new_size;
				}
				file_list[nfiles].name = strdup(ent->d_name);
				file_list[nfiles].mtime = st.st_mtime;
				file_list[nfiles].touched = True;
				
				/* is it a mount point ? */
				if(S_ISDIR(st.st_mode)) {
					char cur_fqn[strlen(path) + strlen(ent->d_name)+2];
					sprintf(cur_fqn, "%s/%s", path, ent->d_name);
					
					if(path_mounted(cur_fqn)) {
						is_mpoint = True;
						is_mounted = True;
					} else if(has_mpts) {
						is_mpoint = (is_in_fstab(cur_fqn) ? True : False);
					}
				}
				file_list[nfiles].is_mpoint = is_mpoint;

				nfiles++;
				
				dbg_trace("update: \'%s\' was created\n", ent->d_name);
				msg.reason = MSG_ADD;		
				msg.files_total = files_total;
				msg.files_skipped = files_skipped;
				msg.size_total = size_total;
			
			} else if(file_list[i].mtime != st.st_mtime) {
				/* file was modified */
				file_list[i].mtime = st.st_mtime;
				dbg_trace("update: \'%s\' was changed\n", ent->d_name);
				msg.reason = MSG_UPDATE;
			
			} else if(S_ISDIR(st.st_mode) && file_list[i].device != st.st_dev) {
				/* mount point change */
				dbg_trace("update: \'%s\' device changed\n", ent->d_name);
				file_list[i].device = st.st_dev;
				msg.reason = MSG_UPDATE;
				
			} else continue; /* nothing changed */
			
			/* send add/update message */
			if(S_ISREG(st.st_mode)) {
				msg.db_index = db_match(ent->d_name, &app_inst.type_db);
			} else if(S_ISDIR(st.st_mode)) {
				if(file_list[i].is_mpoint) {
					char fqn[strlen(path) + strlen(ent->d_name) + 2];
					sprintf(fqn, "%s/%s", path, ent->d_name);
					
					is_mpoint = True;
					is_mounted = (path_mounted(fqn) ? True : False);
				}
				msg.db_index = DB_UNKNOWN;
			} else {
				msg.db_index = DB_UNKNOWN;
			}
			dbg_trace("%s: mounted %s\n",
				ent->d_name, is_mounted ? "True" : "False");

			msg.size = st.st_size;
			msg.name_len = strlen(ent->d_name);
			msg.mode = st.st_mode;
			msg.ctime = st.st_ctime;
			msg.mtime = st.st_mtime;
			msg.gid = st.st_gid;
			msg.uid = st.st_uid;
			msg.is_mpoint = is_mpoint;
			msg.is_mounted = is_mounted;
	
			out = write(pipe_fd, &msg, sizeof(struct msg_data));
			out += write(pipe_fd, ent->d_name, msg.name_len);
			if(out < (sizeof(struct msg_data) + msg.name_len))
				return RP_IOFAIL;
			
		}
		closedir(dir);

		/* check for deleted files */
		for(i = 0; i < nfiles; i++) {
			size_t delta;
		
			if(file_list[i].touched) {
				file_list[i].touched = False;
				continue;
			}
			dbg_trace("update: \'%s\' was removed\n", file_list[i].name);
			
			/* send removal message */
			msg.reason = MSG_REMOVE;
			msg.name_len = strlen(file_list[i].name);

			out = write(pipe_fd, &msg, sizeof(struct msg_data));
			out += write(pipe_fd, file_list[i].name, msg.name_len);
			if(out < (sizeof(struct msg_data) + msg.name_len))
				return RP_IOFAIL;

			/* remove it from the watch list */
			delta = (nfiles - 1) - i;

			if(delta) {
				memmove(&file_list[i], &file_list[i + 1],
					sizeof(struct watch_rec) * delta);
			}
			nfiles--;
		}
		memset(&msg, 0, sizeof(struct msg_data));
		msg.reason = MSG_EOD;
		msg.files_total = files_total;
		msg.files_skipped = files_skipped;
		msg.size_total = size_total;
		
		out = write(pipe_fd, &msg, sizeof(struct msg_data));
		if(out < sizeof(struct msg_data)) return RP_IOFAIL;
						
		sleep(app_res.refresh_int);
	}

	return RP_SUCCES; /* reached if parent process exits */
}

static void read_proc_sigalrm(int sig)
{
	sleep(1);
	/* we use SIGALRM to force refresh */
}

static void read_proc_sigterm(int sig)
{
	_exit(0);
}
