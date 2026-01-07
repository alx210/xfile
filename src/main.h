/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * Common prototypes data structures and constants
 */

#ifndef MAIN_H
#define MAIN_H

#include <Xm/Xm.h>
#include "typedb.h"
#include "listw.h"

/* Application resources */
struct app_resources {
	Boolean show_full_path;
	Boolean show_app_title;
	Boolean user_db_only;
	unsigned int refresh_int;
	String confirm_rm;
	Boolean path_field;
	Boolean status_field;
	Boolean show_all;
	Boolean toggle_view;
	Boolean reverse_order;
	Boolean filter_dirs;
	Boolean user_mounts;
	String sort_by;
	String filter;
	String def_path;
	String icon_size;
	String mount_cmd;
	String umount_cmd;
	String media_dir;
	String media_mount_cmd;
	String media_umount_cmd;
	String dup_suffix;
	unsigned int history_max;
};

/* Global application instance data */
struct app_inst_data {
	/* static instance data */
	char *shell_title;
	pid_t parent;

	Display *display;
	Colormap colormap;
	Visual *visual;
	Screen *screen;
	XtAppContext context;
	
	/* file manager shell and widgets */
	Widget wshell;
	Widget wmain;
	Widget wmenu;
	Widget wlist;
	Widget wpath;
	Widget wstatus;
	Widget wpath_frm;
	Widget wstatus_frm;
	
	/* context menu (file dropdown and list popup) widgets */
	Widget wmfile;
	Widget wmedit;
	Widget wmctx;
	
	/* global file type database */
	struct file_type_db type_db;
	char **db_names;
	
	/* current location, selection and stats */
	char *location;

	unsigned int nfiles_read;
	unsigned int nfiles_shown;
	unsigned int nfiles_hidden;
	struct fsize size_shown;
	
	/* last destination selected in copy/move to dialog */
	char *last_dest;
	
	/* options */
	int icon_size_id;
	int confirm_rm;
	char *filter;

	/* number of currently active sub-shells */
	unsigned int num_sub_shells;
};

/* Defined in main.c */
extern struct app_inst_data app_inst;
extern struct app_resources app_res;

/* Reliable signal handling (using POSIX sigaction) */
typedef void (*sigfunc_t)(int);
sigfunc_t rsignal(int sig, sigfunc_t, int);

/* Prints an APP_NAME: prefixed message to stderr */
void stderr_msg(const char *fmt, ...);

/* Sets the status bar text using printf like arguments */
void set_status_text(const char *fmt, ...);

/* Updates shell title according to path specified */
void update_shell_title(const char *path);

/* Forks off a new xfile instance.
 * Inherits UI state if inherit_ui is True, uses app-defaults otherwise. */
void fork_xfile(const char *path, Boolean inherit_ui);

/* Sets GUI sensitivity according to UIF* flags */
void set_ui_sensitivity(short);

/* UI sensitivity flags */
#define UIF_DIR 0x0001		/* displaying a directory */
#define UIF_SEL 0x0002		/* active selection */
#define UIF_SINGLE 0x0004	/* single item selection */

#endif /* MAIN_H */
