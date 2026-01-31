/*
 * Copyright (C) 2022-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <Xm/Xm.h>
#include "listw.h"
#include "pathw.h"
#include "main.h"
#include "const.h"
#include "comdlgs.h"
#include "menu.h"
#include "filemgr.h"
#include "typedb.h"
#include "exec.h"
#include "path.h"
#include "fsproc.h"
#include "fsutil.h"
#include "mount.h"
#include "info.h"
#include "select.h"
#include "debug.h"


#define EXEC_STATUS_TIMEOUT 3000

static void open_dir_proc(Widget, XtPointer, XtPointer);
static void new_window_proc(Widget, XtPointer, XtPointer);
static void pass_to_proc(Widget, XtPointer, XtPointer);
static void run_action_proc(Widget, XtPointer, XtPointer);
static void run_exfile_proc(Widget, XtPointer, XtPointer);
static void mount_proc(Widget, XtPointer, XtPointer);
static void umount_proc(Widget, XtPointer, XtPointer);
static void action_status_timer_cb(XtPointer, XtIntervalId*);
static void set_action_status_text(const char*, const char*);
int run_action(const char*, const char*, const struct file_type_rec*);

/*
 * Context menu callback.
 * /Open/ handler for directories.
 */
static void open_dir_proc(Widget w, XtPointer pclient, XtPointer pcall)
{
	set_location((char*)pclient, False);
}

/*
 * Context menu callback.
 * Creates new xfile instance showing the directory selected.
 */
static void new_window_proc(Widget w, XtPointer pclient, XtPointer pcall)
{
	const char *name = (char*)pclient;
	size_t len = strlen(app_inst.location) + strlen(name) + 2;
	char *buffer = malloc(len);

	dbg_assert(app_inst.location);
	sprintf(buffer, "%s/%s", app_inst.location, name);
	fork_xfile(buffer, True);
	free(buffer);
}

/*
 * Context menu callback.
 * Displays input dialog prompting the user for a command to run
 * with the currently selected file name as a parameter.
 */
static void pass_to_proc(Widget w, XtPointer pclient, XtPointer pcall)
{
	char *input;
	char *path;
	char *fqn;
	size_t argc;
	char **argv;
	int rv;
	static char *last_input = NULL;
	struct file_list_selection *cur_sel = 
		file_list_get_selection(app_inst.wlist);
	
	if(!cur_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
	
	input = input_string_dlg(app_inst.wshell, "Pass To",
		"Specify a command to pass the file to",
		last_input, "command", ISF_PRESELECT);
	if(!input) return;
	
	if(last_input) free(last_input);
	last_input = strdup(input);
	
	path = realpath(app_inst.location, NULL);
	if(!path) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error accessing working directory.\n%s", strerror(errno));
		return;
	}
	fqn = malloc(strlen(path) + strlen(cur_sel->item.name) + 2);
	sprintf(fqn, "%s/%s", path, cur_sel->item.name);
	free(path);

	rv = split_arguments(input, &argv, &argc);
	if(!rv) {
		argv = realloc(argv, (argc + 1) * sizeof(char*));
		argv[argc] = fqn;
		
		set_action_status_text("Executing", argv[0]);

		rv = spawn_command(*argv, argv + 1, argc);
		if(rv) {
			va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
				"Error executing command \"%s\"\n%s",
				argv[0], strerror(rv));
		}
		free(argv);
	} else {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error parsing command string \"%s\"\n%s",
			last_input, strerror(rv));		
	}

	free(input);
	free(fqn);
}

/*
 * Context menu callback.
 * Runs action pointed to in callback data.
 */
static void run_action_proc(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cur_sel;
	char *action = (char*)pclient;
	struct file_type_rec *ft = NULL;

	cur_sel = file_list_get_selection(app_inst.wlist);
	if(!cur_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
		
	if(DB_DEFINED(cur_sel->item.db_type)) {
		ft = db_get_record(&app_inst.type_db, cur_sel->item.db_type);
	}
	
	run_action(cur_sel->item.name, action, ft);
}

/* Mount/unmount context menu callbacks */
static void mount_proc(Widget w, XtPointer pclient, XtPointer pcall)
{
	int rv;
	char *real_path;
	
	real_path = realpath((char*)pclient, NULL);
	if(real_path) {
		set_action_status_text("Mounting", real_path);
		rv = exec_mount(real_path);
		free(real_path);
	} else {
		rv = errno;
	}
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	}
}

static void umount_proc(Widget w, XtPointer pclient, XtPointer pcall)
{
	int rv;
	char *real_path;
	
	real_path = realpath((char*)pclient, NULL);
	if(real_path) {
		set_action_status_text("Unmounting", real_path);
		rv = exec_umount(real_path);
		free(real_path);
	} else {
		rv = errno;
	}
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	}
}


/*
 * Context Menu Callback. Runs an executable file.
 * Nobody runs commands from file manager, but one may want to start a GUI
 * application, hence no terminal is spawned, neither is the user asked
 * for parameters.
 */
static void run_exfile_proc(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cur_sel;
	char *path;
	size_t fqn_len;
	char *fqn;
	int rv = 0;
	
	cur_sel = file_list_get_selection(app_inst.wlist);
	if(!cur_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
	
	path = realpath(app_inst.location, NULL);
	if(!path) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error accessing working directory.\n%s", strerror(errno));
		return;
	}

	fqn_len = strlen(path) + strlen(cur_sel->item.name) + 2;
	fqn = malloc(fqn_len);
	snprintf(fqn, fqn_len, "%s/%s", path, cur_sel->item.name);

	set_action_status_text("Executing", fqn);
	rv = spawn_exe(fqn);

	free(path);
	free(fqn);
	
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error executing \'%s\'\n%s.",
			cur_sel->item.name, strerror(rv));
	}
}

/* set_action_status_text timer callback */
static void action_status_timer_cb(XtPointer closure, XtIntervalId *iid)
{
	show_selection_stats();
}

/* Temporarily sets status-bar text message */
static void set_action_status_text(const char *action, const char *cmd)
{
	set_status_text("%s: %s", action, cmd);

	XtAppAddTimeOut(app_inst.context, EXEC_STATUS_TIMEOUT,
		action_status_timer_cb, NULL);
}


/*
 * FileList activate (double-click/return) callbacks
 */
void activate_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cbd = (struct file_list_selection*)pcall;
	
	if(S_ISDIR(cbd->item.mode)) {
		set_location(cbd->item.name, False);
	} else if(S_ISREG(cbd->item.mode)) {
		struct file_type_rec *rec;

		rec = db_get_record(&app_inst.type_db, cbd->item.db_type);
		if(rec && rec->nactions) {
			run_action_proc(w, rec->actions[0].command, NULL);
		} else {
			if(DB_ISTEXT(cbd->item.db_type))
				run_action_proc(w, ENV_DEF_TEXT_CMD, NULL);
			else
				pass_to_proc(w, NULL, NULL);
		}
	} else {
		pass_to_proc(w, NULL, NULL);
	}
}

/*
 * FileList selection change callback
 */
void sel_change_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cbd = (struct file_list_selection*)pcall;
	struct ctx_menu_item *cmi = NULL;
	short ui_flags = UIF_DIR;
	
	if(cbd->count) {
		if(cbd->initial)
			grab_selection();
	} else {
		ungrab_selection();
	}

	show_selection_stats();
	
	if(cbd->count == 1) {
		struct file_type_rec *ft = NULL;
		char *file_name = cbd->item.name;
		Boolean is_exec = (access(file_name, X_OK) ? False : True);
		
		ui_flags |= (UIF_SEL | UIF_SINGLE);
		
		ft = db_get_record(&app_inst.type_db, cbd->item.db_type);

		if(ft && ft->nactions) {
			unsigned int i;
			
			cmi = calloc(ft->nactions + 2, sizeof(struct ctx_menu_item));
			
			for(i = 0; i < ft->nactions; i++) {
				cmi[i].label = ft->actions[i].title;
				cmi[i].callback = run_action_proc;
				cmi[i].cb_data = ft->actions[i].command;
			}

			if(is_exec) {
				cmi[i].label = "E&xecute";
				cmi[i].callback = run_exfile_proc;
				cmi[i].cb_data = NULL;
				i++;
			}
			cmi[i].label = "&Pass To...";
			cmi[i].callback = pass_to_proc;
			cmi[i].cb_data = NULL;
			i++;						
			update_context_menus(cmi, i, 0);
			free(cmi);
		} else {
			/* not a regular file, no matching type, or no actions defined */
			unsigned int ncmi = 0;
			
			struct ctx_menu_item def_text_act[] = {
				{
				.label = "&Open",
				.callback = run_action_proc,
				.cb_data = ENV_DEF_TEXT_CMD
				},
				{
				.label = "&Pass To...",
				.callback = pass_to_proc,
				.cb_data = NULL
				},
				{
				.label = "E&xecute",
				.callback = run_exfile_proc,
				.cb_data = NULL
				}
			};

			struct ctx_menu_item def_reg_act[] = {
				{
				.label = "&Pass To...",
				.callback = pass_to_proc,
				.cb_data = NULL
				},
				{
				.label = "E&xecute",
				.callback = run_exfile_proc,
				.cb_data = NULL
				}
			};
			
			struct ctx_menu_item def_dir_act[] = {
				{ 
					.label = "&Open",
					.callback = open_dir_proc,
					.cb_data = (XtPointer) file_name
				},
				{
					.label = "&New Window",
					.callback = new_window_proc,
					.cb_data = (XtPointer) file_name
				},
				{
					.label = "&Mount",
					.callback = mount_proc,
					.cb_data = (XtPointer) file_name
				}
			};

			if(S_ISDIR(cbd->item.mode)) {
				ncmi = 2;

				if((cbd->item.user_flags & FLI_MNTPOINT)
					&& app_res.user_mounts)	ncmi++;

				if(cbd->item.user_flags & FLI_MOUNTED) {
					def_dir_act[2].label = "&Unmount";
					def_dir_act[2].callback = umount_proc;
				}
				cmi = def_dir_act;

			} else if(S_ISREG(cbd->item.mode)) {
				if(DB_ISTEXT(cbd->item.db_type)) {
					cmi = def_text_act;
					ncmi = (is_exec ? 3 : 2);
				} else {
					cmi = def_reg_act;
					ncmi = (is_exec ? 2 : 1);
				}
			} /* nothing otherwise */

			update_context_menus(cmi, ncmi, 0);
		}
	} else if(cbd->count > 1) {
		ui_flags |= UIF_SEL;
		update_context_menus(NULL, 0, 0);
	} else {
		update_context_menus(NULL, 0, 0);
	}
	
	set_ui_sensitivity(ui_flags);
}

/*
 * Main menu callbacks
 */
void dir_up_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	dbg_assert(app_inst.location);
	
	if(strcmp(app_inst.location, "/")) {
		char *tmp = strdup(app_inst.location);
		
		tmp = trim_path(tmp, 1);	
				
		set_location(tmp, True);
		free(tmp);
	}
}

void make_dir_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	char *input;
	mode_t dir_mode = (S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	int rv = 0;
	
	input = input_string_dlg(app_inst.wshell, "Make Directory",
		"Specify new directory name or path to create", NULL, NULL, 0);
	if(!input) return;
	
	if( (rv = create_hier(input, dir_mode)) != 0) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Cannot create directory.\n%s.",
			strerror( (rv == ENOTDIR) ? EEXIST : rv), NULL);
	} else {
		force_update();
	}
}

void make_file_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	char *input;
	const mode_t file_mode = (S_IRUSR|S_IWUSR|S_IRGRP);
	
	input = input_string_dlg(app_inst.wshell, "Make File",
		"Specify new file name", NULL, NULL, ISF_PRESELECT);
	if(!input) return;
	
	if(mknod(input, file_mode, 0) == -1) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Cannot create file.\n%s.", strerror(errno), NULL);
	} else {
		force_update();
	}
}


void copy_to_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cur_sel;
	char *dest;
	int rv;
	
	cur_sel = file_list_get_selection(app_inst.wlist);
	if(!cur_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
	
	if(!app_inst.last_dest || access(app_inst.last_dest, R_OK|X_OK)) {
		if(app_inst.last_dest) free(app_inst.last_dest);
		app_inst.last_dest = get_working_dir();
	}

	dest = dir_select_dlg(app_inst.wshell,
		"Select Destination", app_inst.last_dest, "copyto");
	
	if(!dest) return;
	
	/* List contents may have changed meanwhile */
	if(!cur_sel->count) {
		free(dest);
		message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n"
			"Initial selection is no longer valid.");
		return;
	}
	
	dest = strip_path(dest);
	
	rv = copy_files(NULL, cur_sel->names, cur_sel->count, dest);
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	}

	if(app_inst.last_dest) free(app_inst.last_dest);	
	app_inst.last_dest = realpath(dest, NULL);
	free(dest);
}

void move_to_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cur_sel;
	char *dest;
	int rv;

	cur_sel = file_list_get_selection(app_inst.wlist);
	if(!cur_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
	
	if(!app_inst.last_dest || access(app_inst.last_dest, R_OK|X_OK)) {
		if(app_inst.last_dest) free(app_inst.last_dest);
		app_inst.last_dest = get_working_dir();
	}

	dest = dir_select_dlg(app_inst.wshell,
		"Select Destination", app_inst.last_dest, "moveto");
	
	if(!dest) return;

	if(!cur_sel->count) {
		free(dest);
		return;
	}
	
	dest = strip_path(dest);	
	
	rv = move_files(NULL, cur_sel->names, cur_sel->count, dest);
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	}

	if(app_inst.last_dest) free(app_inst.last_dest);
	app_inst.last_dest = realpath(dest, NULL);
	free(dest);
}

void rename_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *list_sel;
	char *cursel;
	char *target;
	
	list_sel = file_list_get_selection(app_inst.wlist);
	if(!list_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
	
	cursel = strdup(list_sel->item.name);
	
	target = input_string_dlg(app_inst.wshell,
		"Rename", "Specify new file name",
		cursel, NULL, ISF_NOSLASH | ISF_PRESELECT | ISF_FILENAME);
	if(!target) {
		free(cursel);
		return;
	}
	
	if(rename(cursel, target) == -1) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	} else {
		force_update();
	}
	free(target);
	free(cursel);
}

void delete_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cur_sel;
	struct stat st;
	unsigned int i;
	int rv;
	Boolean have_subdirs = False;

	cur_sel = file_list_get_selection(app_inst.wlist);
	if(!cur_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
	
	for(i = 0; i < cur_sel->count; i++) {
		if(!lstat(cur_sel->names[i], &st) &&
			S_ISDIR(st.st_mode)) have_subdirs = True;
	}
	
	
	if( (app_inst.confirm_rm == CONFIRM_ALWAYS) ||
		(app_inst.confirm_rm == CONFIRM_MULTI &&
			( (cur_sel->count > 3) || have_subdirs) ) ) {
		rv = va_message_box(app_inst.wshell, MB_QUESTION, APP_TITLE,
			"Deleting %d %s%s.\nAre you sure you want to proceed?",
			cur_sel->count, ((cur_sel->count > 1) ? "items" : "item"),
			(have_subdirs ? ", recursing into sub-directories" : ""), NULL);
		if(rv != MBR_CONFIRM || !cur_sel->count) return;
	}
	rv = delete_files(NULL, cur_sel->names, cur_sel->count);
	
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	}
}

void link_to_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *list_sel;
	char *link;
	char *target;
	char *cursel;
	
	list_sel = file_list_get_selection(app_inst.wlist);
	if(!list_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
	
	cursel = strdup(list_sel->item.name);
	
	link = input_string_dlg(app_inst.wshell, "Link To",
		"Specify a name for the symbolic link.\n"
		"It may contain either absolute or relative path.", NULL, NULL, 0);
	if(!link) {
		free(cursel);
		return;
	}		
	
	if(strchr(link, '/')) {
		target = realpath(cursel, NULL);
		if(!target) {
			va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
				"Error accessing \'%s\'.\n%s", cursel, strerror(errno));
			return;
		}
	} else {
		target = cursel;
	}
	
	if(!target || (symlink(target, link) == -1) ) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	} else {
		force_update();
	}

	if(target != cursel) free(target);
	free(cursel);
	free(link);
}

void duplicate_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	int rv;
	struct file_list_selection *cur_sel =
		file_list_get_selection(app_inst.wlist);

	if(!cur_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}
	
	rv = dup_files(NULL, cur_sel->names, cur_sel->count);
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	}
}

void attributes_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cur_sel =
		file_list_get_selection(app_inst.wlist);

	if(!cur_sel->count) {
		stderr_msg("%s invalid selection\n", __FUNCTION__);
		return;
	}

	attrib_dlg(app_inst.wshell, cur_sel->names, cur_sel->count);
}

void select_all_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	file_list_select_all(app_inst.wlist);
}

void reselect_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct file_list_selection *cur_sel =
		file_list_get_selection(app_inst.wlist);
	
	if(cur_sel->count && !is_selection_owner()) {
		short ui_flags = UIF_DIR | UIF_SEL |
			((cur_sel->count == 1) ? UIF_SINGLE : 0);
		
		grab_selection();
		set_ui_sensitivity(ui_flags);
	}
}

void deselect_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	file_list_deselect(app_inst.wlist);
}

void invert_selection_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	file_list_invert_selection(app_inst.wlist);
}

void select_pattern_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	static char *last_input = NULL;
	char *input;
	
	input = input_string_dlg(app_inst.wshell, "Select Pattern",
		"Specify a glob pattern",
		last_input, "select", ISF_PRESELECT);
	if(!input) {
		file_list_deselect(app_inst.wlist);
		return;
	}
	
	if(last_input) free(last_input);
	last_input = input;
	
	if(file_list_select_pattern(app_inst.wlist, input, True))
		set_status_text("No files matched %s", input);
}

void copy_here_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	paste_selection(False);
}

void move_here_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	paste_selection(True);
}

void toggle_detailed_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;
	Arg args[1];
	
	XtSetArg(args[0], XfNviewMode, cbs->set ? XfDETAILED : XfCOMPACT);
	XtSetValues(app_inst.wlist, args, 1);
}

void toggle_show_all_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	app_res.show_all = cbs->set;
	reread();
}

void sort_by_name_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set) {
		Arg args[1];	
		XtSetArg(args[0], XfNsortOrder, XfNAME);
		XtSetValues(app_inst.wlist, args, 1);
	}
}

void sort_by_size_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set) {
		Arg args[1];	
		XtSetArg(args[0], XfNsortOrder, XfSIZE);
		XtSetValues(app_inst.wlist, args, 1);
	}
}

void sort_by_time_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set) {
		Arg args[1];	
		XtSetArg(args[0], XfNsortOrder, XfTIME);
		XtSetValues(app_inst.wlist, args, 1);
	}
}

void sort_by_type_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set) {
		Arg args[1];	
		XtSetArg(args[0], XfNsortOrder, XfTYPE);
		XtSetValues(app_inst.wlist, args, 1);
	}
}

void sort_by_suffix_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set) {
		Arg args[1];	
		XtSetArg(args[0], XfNsortOrder, XfSUFFIX);
		XtSetValues(app_inst.wlist, args, 1);
	}
}

void sort_ascending_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set) {
		Arg args[1];	
		XtSetArg(args[0], XfNsortDirection, XfASCEND);
		XtSetValues(app_inst.wlist, args, 1);
	}
}

void sort_descending_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set) {
		Arg args[1];	
		XtSetArg(args[0], XfNsortDirection, XfDESCEND);
		XtSetValues(app_inst.wlist, args, 1);
	}
}

void show_path_field_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set)
		XtManageChild(app_inst.wpath_frm);
	else
		XtUnmanageChild(app_inst.wpath_frm);
}

void show_status_field_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	XmToggleButtonCallbackStruct *cbs =
		(XmToggleButtonCallbackStruct*) pcall;

	if(cbs->set)
		XtManageChild(app_inst.wstatus_frm);
	else
		XtUnmanageChild(app_inst.wstatus_frm);

}

void set_filter_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	char *input;

	input = input_string_dlg(app_inst.wshell, "Filter",
		"Specify a glob pattern to filter out matching names.\n"
		"Prefix the pattern with the ! character to negate it.\n"
		"Leave the field empty or choose Cancel to dismiss.",
		app_inst.filter, "filter", ISF_PRESELECT);

	if(!input) {
		if(app_inst.filter) {
			free(app_inst.filter);
			app_inst.filter = NULL;
			reread();
		}
		return;
	}
	
	if(app_inst.filter) free(app_inst.filter);
	app_inst.filter = input;
	reread();
}

void reread_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	reread();
}

void new_window_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	char *home, *input, *path;
	
	home = getenv("HOME");
	if(!home) home = "/";

	get_input: /* on failed access() check below */	
	input = input_string_dlg(app_inst.wshell, "New Window",
		"Specify path to browse", "~", "browse", ISF_PRESELECT);
	
	if(!input) return;
	
	path = input;
	
	while(isspace(*path)) path++;

	if(*path == '\0') {
		path = home;
	} else if(*path == '~') {
		size_t len = strlen(home) + strlen(path) + 1;
		char *p;

		p = malloc(len);
		sprintf(p, "%s/%s", home, path + 1);
		free(input);
		path = input = p;
	}

	if(!access(path, X_OK)) {
		fork_xfile(path, False);
	} else {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error accessing \'%s\'\n%s.", path, strerror(errno));
		free(input);
		goto get_input;
	}
	
	free(input);
}

void dup_window_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	fork_xfile(app_inst.location, True);
}

void about_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	display_about_dialog(app_inst.wshell);
}

void dbinfo_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	display_dbinfo_dialog(app_inst.wshell);
}


/*
 * PathField change callback
 */
void path_change_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct path_field_cb_data *cbd = (struct path_field_cb_data*) pcall;
	struct stat st;
	
	if(stat(cbd->value, &st) == -1) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error accessing \'%s\'\n%s.",cbd->value, strerror(errno));	
		cbd->accept = False;
	} else if(!S_ISDIR(st.st_mode)) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Specified path \'%s\' is not a directory.",cbd->value);
		cbd->accept = False;
	} else if(!set_location(cbd->value, True)) {
		cbd->accept = True;
	} else {
		cbd->accept = False;
	}
}

/*
 * All sub-shells should have this callback attached
 */
void sub_shell_destroy_cb(Widget w, XtPointer client, XtPointer call)
{
	dbg_assert(app_inst.num_sub_shells > 0);
	app_inst.num_sub_shells--;
	
	dbg_trace("app_inst.num_sub_shells %d\n", app_inst.num_sub_shells);
	
	if(!app_inst.num_sub_shells && (app_inst.wshell == NULL)) {
		dbg_trace("exit flag set in %s\n", __FUNCTION__);
		XtAppSetExitFlag(app_inst.context);
	}
}

/*
 * Expands and executes an action string.
 * This routine may either be invoked from a GUI callback, or with no GUI
 * at all by xfile_open(). Any error messages are reported using a message
 * box if app_inst.wmain is valid, or to stderr otherwise.
 */
int run_action(const char *file_name, const char *action_str,
	const struct file_type_rec *db_rec)
{
	struct env_var_rec vars[4] = { NULL };
	char *path;
	char *exp_act;
	char **argv;
	size_t argc, len, i;
	int rv = 0;

	path = get_working_dir();
	if(!path) {
		char *err_sz = "Cannot access the working directory.";
		if(app_inst.wmain)
			message_box(app_inst.wshell, MB_ERROR, APP_TITLE, err_sz);
		else
			stderr_msg("%s\n", err_sz);
		return ENOENT;
	}

	/* Expand the command string, but not any context components yet.
	 * These get substituted back for later expansion, since we don't
	 * want to have file and path names inserted prior to split_arguments(),
	 * as these may contain spaces (and contemporarily, other special chars) */
	vars[0].name = ENV_FNAME;
	vars[0].value = "%{" ENV_FNAME "}";
	vars[1].name = ENV_FPATH;
	vars[1].value = "%{" ENV_FPATH "}";
	vars[2].name = ENV_FMIME;
	vars[2].value = "%{" ENV_FMIME "}";
	
	rv = expand_env_vars(action_str, vars, &exp_act);
	if(rv) {
		if(app_inst.wmain)
			va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
				"Error in variable substitution:\n%s\n%s.",
				action_str, strerror(rv));
		else
			stderr_msg("Error in variable substitution: %s. %s.\n",
				action_str, strerror(rv));
		
		free(path);
		return rv;
	}

	rv = split_arguments(exp_act, &argv, &argc);
	if(rv || (argc < 2)) {
		if(rv) {
			if(app_inst.wmain)
				va_message_box(app_inst.wshell,	MB_ERROR, APP_TITLE,
					"Error parsing action command string:\n%s\n%s.",
					action_str, strerror(rv));
			else
				stderr_msg("Error parsing action command string: %s.%s.\n",
					action_str, strerror(rv));
		} else {
			if(app_inst.wmain)
				va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE, 
					"Action \"%s\" contains no command arguments.", action_str);
			else
				stderr_msg("Action \"%s\" contains "
					"no command arguments.\n", action_str);
		}
		free(path);
		free(exp_act);
		return rv;		
	}

	/* now that we have an argv array, expand any context variables in it */
	vars[0].name = ENV_FNAME;
	vars[0].value = (char*)file_name;
	vars[1].name = ENV_FPATH;
	vars[1].value = (char*)path;
	vars[2].name = ENV_FMIME;
	vars[2].value = (db_rec && db_rec->mime) ? db_rec->mime : "";
	
	for(i = 0; i < argc; i++) {
		/* pointers in argv currently index exp_act 
		 * so they don't need to be freed...*/
		rv = expand_env_vars(argv[i], vars, argv + i);
		if(rv) {
			/* ...but the ones expand_env_vars has returned do */
			while(i) { i--;	free(argv[i]); }
			
			if(app_inst.wmain)
				va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
					"Error in variable substitution:\n%s\n%s.",
					action_str, strerror(rv));
			else
				stderr_msg("Error in variable substitution: %s. %s.\n",
					action_str, strerror(rv));

			free(path);
			free(argv);
			free(exp_act);
			return rv;		
		}
	}
	free(exp_act);
	
	/* set the status text to indicate what's being run */
	if(app_inst.wmain) {
		for(i = 0, len = 0; i < argc; i++)
			len += strlen(argv[i]) + 1;

		if( (exp_act = malloc(len + 1)) ) {
			*exp_act = '\0';

			for(i = 0; i < argc; i++) {
				strcat(exp_act, argv[i]);
				strcat(exp_act, " ");
			}
			set_action_status_text("Executing", exp_act);
			free(exp_act);
		}
	}

	/* run the composed command */
	rv = spawn_command(argv[0], argv + 1, argc - 1);
	if(rv) {
		if(app_inst.wmain)
			va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
				"Failed to execute \"%s\".\n%s.", argv[0], strerror(rv));
		else
			stderr_msg("Failed to execute \"%s\". %s.\n",
				argv[0], strerror(rv));
	}
	
	for(i = 0; i < argc; i++) free(argv[i]);

	free(argv);
	free(path);
	
	return rv;
}
