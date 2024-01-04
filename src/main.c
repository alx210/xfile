/*
 * Copyright (C) 2023-2024 alx@fastestcode.org
 * This software is distributed under the terms of the MIT/X license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <Xm/Xm.h>
#include <Xm/MainW.h>
#include <Xm/RowColumn.h>
#include <Xm/TextF.h>
#include <Xm/Frame.h>
#include <Xm/ScrollBar.h>
#include <Xm/ScrolledW.h>
#include <Xm/Container.h>
#include <Xm/Protocols.h>
#include <Xm/ToggleB.h>
#include "comdlgs.h"
#include "guiutil.h"
#include "graphics.h"
#include "main.h"
#include "menu.h"
#include "const.h"
#include "filemgr.h"
#include "listw.h"
#include "pathw.h"
#include "cbproc.h"
#include "path.h"
#include "exec.h"
#include "mount.h"
#include "fsproc.h"
#include "usrtool.h"
#include "fsutil.h"
#include "debug.h"
#include "memdb.h" /* must be the last header */

/* Icon bitmaps */
#include "xbm/cabinet.xbm"
#include "xbm/cabinet_m.xbm"

/* Forward declarations */
static void sigchld_handler(int);
static void sigusr_handler(int);
static void create_main_window(void);
static void create_main_menus(void);
static Boolean load_db(void);
static int db_dir_filter(const struct dirent*);
static void window_close_cb(Widget, XtPointer, XtPointer);
static int get_best_icon_size(void);
extern void XmRenderTableGetDefaultFontExtents(XmRenderTable,
	int *height, int *ascent, int *descent);
extern void populate_tools_menu(void);
static void xt_sigchld_handler(XtPointer p, XtSignalId *id);
static void xfile_open(int, char**);

/* Config directory permissions */
#define CONF_DIR_PERMS  (S_IRWXU|S_IRGRP)

/* Application global data initialized in main() */
struct app_resources app_res;
struct app_inst_data app_inst = {0};
struct file_type_db type_db = {0};
static XtSignalId sigchld_sigid;

int main(int argc, char **argv)
{
	Arg args[10];
	Cardinal n;
	Pixmap icon_pix = None;
	Pixmap icon_mask = None;
	char *open_spec = NULL;
	char *path;
	int res;
	
	XtResource xrdb_resources[] = {
	{
		"fullPathInTitle", "FullPathInTitle",
		XmRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, show_full_path),
		XmRImmediate,(XtPointer)False
	},
	{
		"appNameInTitle", "AppNameInTitle",
		XmRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, show_app_title),
		XmRImmediate,(XtPointer)True
	},
	{
		"userDatabaseOnly", "UserDatabaseOnly",
		XmRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, user_db_only),
		XmRImmediate,(XtPointer)False
	},
	{
		"confirmRemoval", "ConfirmRemoval",
		XmRString, sizeof(String),
		XtOffsetOf(struct app_resources, confirm_rm),
		XmRImmediate,(XtPointer)CS_CONFIRM_ALWAYS
	},
	{
		"refreshInterval", "RefreshInterval",
		XmRInt, sizeof(int),
		XtOffsetOf(struct app_resources, refresh_int),
		XmRImmediate,(XtPointer)DEF_REFRESH_INT
	},
	{
		"showAll", "ShowAll",
		XmRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, show_all),
		XmRImmediate,(XtPointer)False
	},	
	{
		"toggleView", "ToggleView",
		XmRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, toggle_view),
		XmRImmediate,(XtPointer)False
	},	
	{
		"reverseOrder", "ReverseOrder",
		XmRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, reverse_order),
		XmRImmediate,(XtPointer)False
	},
	{
		"filter", "Filter",
		XmRString, sizeof(String),
		XtOffsetOf(struct app_resources, filter),
		XmRImmediate,(XtPointer)NULL
	},
	{
		"filterDirectories", "FilterDirectories",
		XmRString, sizeof(Boolean),
		XtOffsetOf(struct app_resources, filter_dirs),
		XmRImmediate,(XtPointer)False
	},
	{
		"defaultPath", "DefaultPath",
		XmRString, sizeof(String),
		XtOffsetOf(struct app_resources, def_path),
		XmRImmediate,(XtPointer)NULL
	},
	{
		"sortBy", "SortBy",
		XtRString, sizeof(String),
		XtOffsetOf(struct app_resources, sort_by),
		XtRImmediate,(XtPointer)NULL
	},
	{
		"terminal", "Terminal",
		XtRString, sizeof(String),
		XtOffsetOf(struct app_resources, terminal),
		XtRImmediate,(XtPointer)"xterm"
	},
	{
		"mediaDirectory", "MediaDirectory",
		XtRString, sizeof(String),
		XtOffsetOf(struct app_resources, media_dir),
		XtRImmediate,(XtPointer)NULL
	},
	{
		"mediaMountCommand", "MediaMountCommand",
		XtRString, sizeof(String),
		XtOffsetOf(struct app_resources, media_mount_cmd),
		XtRImmediate,(XtPointer)NULL
	},
	{
		"mediaUnmountCommand", "MediaUnmountCommand",
		XtRString, sizeof(String),
		XtOffsetOf(struct app_resources, media_umount_cmd),
		XtRImmediate,(XtPointer)NULL
	},
	{
		"mountCommand", "MountCommand",
		XtRString, sizeof(String),
		XtOffsetOf(struct app_resources, mount_cmd),
		XtRImmediate,(XtPointer)"mount"
	},
	{
		"unmountCommand", "UnmountCommand",
		XtRString, sizeof(String),
		XtOffsetOf(struct app_resources, umount_cmd),
		XtRImmediate,(XtPointer)"umount"
	},
	{
		"userMounts", "UserMounts",
		XtRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, user_mounts),
		XtRImmediate,(XtPointer)True
	},
	{
		"pathField", "PathField",
		XmRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, path_field),
		XmRImmediate,(XtPointer)True
	},	
	{
		"statusField", "StatusField",
		XmRBoolean, sizeof(Boolean),
		XtOffsetOf(struct app_resources, status_field),
		XmRImmediate,(XtPointer)True
	},
	{
		"iconSize", "IconSize",
		XtRString, sizeof(String),
		XtOffsetOf(struct app_resources, icon_size),
		XtRImmediate,(XtPointer)CS_ICON_AUTO
	}
	};

	XrmOptionDescRec xrdb_options[] = {
		{"-a", "showAll", XrmoptionNoArg, "True"},
		{"+a", "showAll", XrmoptionNoArg, "False"},
		{"-l", "toggleView", XrmoptionNoArg, "True"},
		{"-r", "reverseOrder", XrmoptionNoArg, "True"},
		{"-f", "filter", XrmoptionSepArg, NULL},
		{"-s", "sortBy", XrmoptionStickyArg, NULL}
	};

	memset(&app_inst, 0, sizeof(struct app_inst_data));
	
	/* Store real binary name for forks */
	path = realpath(argv[0], NULL);
	if(path)
		app_inst.bin_name = path;
	else
		app_inst.bin_name = argv[0];

	XtSetLanguageProc(NULL, NULL, NULL);

	XtToolkitInitialize();
	app_inst.context = XtCreateApplicationContext();
	
	XtAppSetFallbackResources(app_inst.context, (String*)xfile_ad);
	
	app_inst.display = XtOpenDisplay(app_inst.context, NULL, APP_NAME,
		APP_CLASS, xrdb_options, XtNumber(xrdb_options), &argc, argv);

	n = 0;
	XtSetArg(args[n], XmNmappedWhenManaged, False); n++;
	XtSetArg(args[n], XmNdeleteResponse, XmUNMAP); n++;

	app_inst.wshell = XtAppCreateShell(APP_NAME, APP_CLASS,
		applicationShellWidgetClass, app_inst.display, args, n);

	add_delete_window_handler(app_inst.wshell,
		window_close_cb, (XtPointer)NULL);
	
	XtGetApplicationResources(app_inst.wshell,
		(XtPointer)&app_res, xrdb_resources,
		XtNumber(xrdb_resources), NULL, 0);

	/* Check if we were invoked using alternate name and diverge */
	path = get_path_tail(argv[0]);
	if(!strcmp(path, XFILE_OPEN))
		xfile_open(argc, argv); /* this never returns */

	/* non XRDB arguments */
	if(argc > 1) {
		open_spec = realpath(argv[1], NULL);
		if(argc > 2) stderr_msg(
			"Ignoring %d redundant argument(s)\n", argc - 2);
	}
	
	if(!open_spec) {
		open_spec = realpath(app_res.def_path ?
			app_res.def_path : ".", NULL);
	}
	
	/* Media directory may contain envvars... */
	if(app_res.media_dir) {
		int rv;
		char *exp_cmd;

		rv = expand_env_vars(app_res.media_dir, NULL, &exp_cmd);
		if(!rv) {
			app_res.media_dir = exp_cmd;
		} else {
			stderr_msg("Error parsing mediaDirectory value (%s): %s\n",
				app_res.media_dir, strerror(rv));
		}
			
	}
	
	create_main_window();

	create_wm_icon(cabinet, &icon_pix, &icon_mask);
	
	n = 0;	
	XtSetArg(args[n], XmNiconPixmap, icon_pix); n++;
	XtSetArg(args[n], XmNiconMask, icon_mask); n++;
	XtSetValues(app_inst.wshell, args, n);
	
	n = 0;
	XtSetArg(args[n], XmNcolormap, &app_inst.colormap); n++;
	XtSetArg(args[n], XmNvisual, &app_inst.visual); n++;
	XtSetArg(args[n], XmNscreen, &app_inst.screen); n++;
	XtSetArg(args[n], XmNtitle, &app_inst.shell_title); n++;
	XtGetValues(app_inst.wshell, args, n);
	
	if(app_inst.shell_title) {
		app_inst.shell_title = strdup(app_inst.shell_title);
	} else app_inst.shell_title = APP_NAME;

	XtRealizeWidget(app_inst.wshell);
	
	if(app_inst.visual == CopyFromParent) {
		app_inst.visual = DefaultVisual(app_inst.display,
			XScreenNumberOfScreen(app_inst.screen));
	}
	
	/* Set some initial instance options from app resources */
	
	/* The filter string must be malloc'd (see set_filter_cb() in cbproc.c ) */
	if(app_res.filter) app_inst.filter = strdup(app_res.filter);
	
	/* NOTE: The matching stuff below should really be done with XtRepTypes,
	 *       but there is not enough of it to be worth the trouble yet */
	if(!strcasecmp(app_res.confirm_rm, CS_CONFIRM_ALWAYS)) {
		app_inst.confirm_rm = CONFIRM_ALWAYS;
	} else if(!strcasecmp(app_res.confirm_rm, CS_CONFIRM_MULTI)) {
		app_inst.confirm_rm = CONFIRM_MULTI;
	} else {
		stderr_msg("Illegal confirmRemoval option specified, using default.\n");
		app_inst.confirm_rm = CONFIRM_ALWAYS;
	}
	
	if(!strcasecmp(app_res.icon_size, CS_ICON_AUTO)) {
		app_inst.icon_size_id = get_best_icon_size();
	} else if(!strcasecmp(app_res.icon_size, CS_ICON_TINY)) {
		app_inst.icon_size_id = IS_TINY;
	} else if(!strcasecmp(app_res.icon_size, CS_ICON_SMALL)) {
		app_inst.icon_size_id = IS_SMALL;
	} else if(!strcasecmp(app_res.icon_size, CS_ICON_MEDIUM)) {
		app_inst.icon_size_id = IS_MEDIUM;
	} else if(!strcasecmp(app_res.icon_size, CS_ICON_LARGE)) {
		app_inst.icon_size_id = IS_LARGE;
	} else {
		stderr_msg("Illegal iconSize value specified, using default.\n");
		app_inst.icon_size_id = get_best_icon_size();
	}
	
	db_init(&app_inst.type_db);
	load_db(); 
	
	sigchld_sigid = XtAppAddSignal(app_inst.context,
		xt_sigchld_handler, NULL);
		
	rsignal(SIGCHLD, sigchld_handler, SA_NOCLDSTOP);
	rsignal(SIGUSR1, sigusr_handler, 0);
	rsignal(SIGUSR2, sigusr_handler, 0);
	
	res = initialize();
	if(res) return res;

	set_status_text(NULL);
	map_shell_unpos(app_inst.wshell);
	
	set_location(open_spec, True);

	XtAppMainLoop(app_inst.context);
	return 0;
}

/*
 * Creates main window widgets. Initializes widget handles in app_inst.
 */
static void create_main_window(void)
{
	Widget wscrolled;
	Widget wframe;
	Widget whscrl, wvscrl;
	Pixel frm_bg_pixel;
	Arg args[10];
	short fl_sort_dir;
	short fl_sort_order;
	short fl_view_mode;
	char *name = NULL;
	Cardinal n;
	XtCallbackRec activate_cbr[] = {
		{ activate_cb, NULL }, { NULL, NULL }
	};
	XtCallbackRec sel_change_cbr[] = {
		{ sel_change_cb, NULL }, { NULL, NULL }
	};
	XtCallbackRec path_change_cbr[] = {
		{ path_change_cb, NULL }, { NULL, NULL }
	};
	XtCallbackRec dir_up_cbr[] = {
		{ dir_up_cb, NULL }, { NULL, NULL }
	};
	XtCallbackRec delete_cbr[] = {
		{ delete_cb, NULL }, { NULL, NULL }
	};

	
	app_inst.wmain = XmCreateMainWindow(app_inst.wshell, "main", NULL, 0);
	
	/* path field  */
	app_inst.wpath_frm = XmVaCreateFrame(app_inst.wmain, "pathBar",
		XmNshadowType, XmSHADOW_OUT,
		XmNshadowThickness, 1, NULL);
	app_inst.wpath = VaCreatePathField(app_inst.wpath_frm, "pathField",
		XmNvalueChangedCallback, path_change_cbr, NULL);
	
	/* 3D frame and text field for status display */
	app_inst.wstatus_frm = XmVaCreateFrame(app_inst.wmain, "statusBar",
		XmNshadowType, XmSHADOW_OUT,
		XmNshadowThickness, 1, NULL);
	
	XtVaGetValues(app_inst.wstatus_frm, XmNbackground, &frm_bg_pixel, NULL);
	
	app_inst.wstatus = XmVaCreateTextField(app_inst.wstatus_frm, "statusField",
		XmNeditable, False, XmNcursorPositionVisible, False,
		XmNshadowThickness, 0, XmNmarginHeight, 1, XmNmarginWidth, 1,
		XmNtraversalOn, False, XmNvalue, "Ready",
		XmNbackground, frm_bg_pixel, NULL);
	XtUninstallTranslations(app_inst.wstatus);
	
	/* 3D frame and scrolled window for the file list */
	wframe = XmVaCreateFrame(app_inst.wmain, "frame",
		XmNshadowType, XmSHADOW_OUT,
		XmNshadowThickness, 1, NULL);
	
	wscrolled = XmVaCreateScrolledWindow(wframe, "scrolledWindow",
		XmNscrollingPolicy, XmAPPLICATION_DEFINED, XmNspacing, 0,
		XmNshadowThickness, 1, NULL);

	wvscrl = XmCreateScrollBar(wscrolled, "verticalScrollBar", NULL, 0);
	whscrl = XmVaCreateScrollBar(wscrolled, "horizontalSctollBar",
		XmNorientation, XmHORIZONTAL, NULL);
	
	/* file list widget */
	n = 0;
	XtSetArg(args[n], XfNactivateCallback, activate_cbr); n++;
	XtSetArg(args[n], XfNselectionChangeCallback, sel_change_cbr); n++;
	XtSetArg(args[n], XfNdirectoryUpCallback, dir_up_cbr); n++;
	XtSetArg(args[n], XfNdeleteCallback, delete_cbr); n++;
	XtSetArg(args[n], XfNhorizontalScrollBar, whscrl); n++;
	XtSetArg(args[n], XfNverticalScrollBar, wvscrl); n++;
	if(app_res.sort_by) {
		short v;

		switch(app_res.sort_by[0]) {
			case 's': v = XfSIZE; break;
			case 't': v = XfTIME; break;
			case 'f': v = XfTYPE; break;
			case 'x': v = XfSUFFIX; break;
			case 'n': v = XfNAME; break;
			default :
			stderr_msg("Illegal sort option specified, using default.\n");
			v = XfNAME;
			break;
		}
		XtSetArg(args[n], XfNsortOrder, v);
		n++;
	}
	app_inst.wlist = CreateFileList(wscrolled, "fileList", args, n);

	XmScrolledWindowSetAreas(wscrolled, whscrl, wvscrl, app_inst.wlist);
	
	/* list popup wants app_inst.wlist, so create menus at last */
	create_main_menus();
	
	XtVaSetValues(app_inst.wmain,
		XmNmenuBar, app_inst.wmenu,
		XmNcommandWindow, app_inst.wpath_frm,
		XmNmessageWindow, app_inst.wstatus_frm,
		XmNworkWindow, wframe, 
		XmNinitialFocus, wframe, NULL);

	/* process toggle switches and sync the UI to resources */
	n = 0;
	XtSetArg(args[n], XfNsortOrder, &fl_sort_order); n++;
	XtSetArg(args[n], XfNsortDirection, &fl_sort_dir); n++;
	XtSetArg(args[n], XfNviewMode, &fl_view_mode); n++;
	XtGetValues(app_inst.wlist, args, n);
	
	n = 0;
	if(app_res.reverse_order) {
		fl_sort_dir = (fl_sort_dir == XfDESCEND) ? XfASCEND : XfDESCEND;
		XtSetArg(args[n], XfNsortDirection, fl_sort_dir);
		n++;
	}
	if(app_res.toggle_view) {
		fl_view_mode = (fl_view_mode == XfCOMPACT) ? XfDETAILED : XfCOMPACT;
		XtSetArg(args[n], XfNviewMode, fl_view_mode);
		n++;
	}
	if(n) XtSetValues(app_inst.wlist, args, n);

	/* sync menu toggle/radio button states */
	switch(fl_sort_order) {
		case XfNAME: name = "*sortByMenu.name"; break;
		case XfSIZE: name = "*sortByMenu.size"; break;
		case XfTIME: name = "*sortByMenu.time"; break;
		case XfTYPE: name = "*sortByMenu.type"; break;
		case XfSUFFIX: name = "*sortByMenu.suffix"; break;
	}
	XmToggleButtonSetState(get_menu_item(app_inst.wmenu, name), True, False);
	
	switch(fl_sort_dir) {
		case XfASCEND: name = "*sortOrderMenu.ascending"; break;
		case XfDESCEND: name = "*sortOrderMenu.descending"; break;
	}
	XmToggleButtonSetState(get_menu_item(app_inst.wmenu, name), True, False);
	
	XmToggleButtonSetState(get_menu_item(app_inst.wmenu, "*viewMenu.detailed"),
		(fl_view_mode == XfDETAILED) ? True : False, False);

	XmToggleButtonSetState(get_menu_item(app_inst.wmenu, "*viewMenu.showAll"),
		app_res.show_all, False);

	XmToggleButtonSetState(
		get_menu_item(app_inst.wmenu, "*viewMenu.showPathField"),
		app_res.path_field, False);

	XmToggleButtonSetState(
		get_menu_item(app_inst.wmenu, "*viewMenu.showStatusField"),
		app_res.status_field, False);

	set_ui_sensitivity(0);

	if(app_res.path_field) {
		XtManageChild(app_inst.wpath);
		XtManageChild(app_inst.wpath_frm);
	}
	if(app_res.status_field) {
		XtManageChild(app_inst.wstatus);
		XtManageChild(app_inst.wstatus_frm);
	}

	XtManageChild(wvscrl);
	XtManageChild(whscrl);
	XtManageChild(wscrolled);
	XtManageChild(wframe);
	XtManageChild(app_inst.wlist);
	XtManageChild(app_inst.wmenu);
	XtManageChild(app_inst.wmain);
}

/*
 * Creates the main menu bar, pulldowns and context/popup menus.
 * Initializes app_inst fields accordingly. Other main window elements
 * must have been created already.
 */
static void create_main_menus(void)
{
	struct user_tool_rec *user_tools;
	int nuser_tools;
	
	struct menu_item main_items[] = {
		{IT_CASCADE, "fileMenu", "&File", NULL, NULL},
		/* context dependent action items will be added here at runtime */
		{IT_SEPARATOR, "actionsSeparator", NULL},
		{IT_PUSH, "makeDirectory", "&Make Directory...", make_dir_cb, NULL},
		{IT_PUSH, "makeFile", "M&ake File...", make_file_cb, NULL},
		{IT_SEPARATOR },
		{IT_PUSH, "copyTo", "&Copy...", copy_to_cb, NULL},
		{IT_PUSH, "moveTo", "&Move...", move_to_cb, NULL},
		{IT_PUSH, "linkTo", "&Link...", link_to_cb, NULL},
		{IT_PUSH, "rename", "&Rename", rename_cb, NULL},
		{IT_PUSH, "delete", "&Delete", delete_cb, NULL},
		{IT_PUSH, "attributes", "A&ttributes", attributes_cb, NULL},
		{IT_SEPARATOR },
		{IT_PUSH, "exit", "&Exit", window_close_cb, NULL},
		{IT_END },
				
		{IT_CASCADE, "editMenu", "&Edit", NULL, NULL},
		{IT_PUSH, "selectAll","&Select All", select_all_cb, NULL},
		{IT_PUSH, "deselect", "&Deselect", deselect_cb, NULL},
		{IT_PUSH, "invertSelection", "&Invert Selection", invert_selection_cb, NULL},
		{IT_PUSH, "selectPattern","Select &Pattern...", select_pattern_cb, NULL},
		{IT_END },
		
		{IT_CASCADE, "viewMenu", "&View", NULL, NULL},
		{IT_PUSH, "goUp", "Go &Up", dir_up_cb, NULL},
		{IT_PUSH, "reread", "&Reread", reread_cb, NULL},
		{IT_SEPARATOR },		
		{IT_TOGGLE, "detailed", "&Detailed", toggle_detailed_cb, NULL},
		{IT_TOGGLE, "showAll", "Show &All", toggle_show_all_cb, NULL},
		{IT_CASCADE_RADIO, "sortByMenu", "&Sort By", NULL, NULL},
		{IT_RADIO, "name", "&Name", sort_by_name_cb, NULL},
		{IT_RADIO, "suffix", "Suffi&x", sort_by_suffix_cb, NULL},
		{IT_RADIO, "size", "&Size", sort_by_size_cb, NULL},
		{IT_RADIO, "time", "&Time", sort_by_time_cb, NULL},
		{IT_RADIO, "type", "T&ype", sort_by_type_cb, NULL},
		{IT_END},

		{IT_CASCADE_RADIO, "sortOrderMenu", "S&ort Order", NULL, NULL},
		{IT_RADIO, "ascending", "&Ascending", sort_ascending_cb, NULL},
		{IT_RADIO, "descending", "&Descending", sort_descending_cb, NULL},
		{IT_END },
		
		{IT_PUSH, "filter", "&Filter...", set_filter_cb, NULL},
		{IT_SEPARATOR },
		{IT_TOGGLE, "showPathField", "Pa&th Field", show_path_field_cb, NULL},
		{IT_TOGGLE, "showStatusField", "Stat&us Field", show_status_field_cb, NULL},
		{IT_END },

		{IT_CASCADE, "toolsMenu", "&Tools", NULL, NULL},
		{IT_PUSH, "terminal", "&Terminal", exec_terminal_cb, NULL},
		/*{IT_PUSH, "search", "&Search", NULL},*/
		{IT_END },
		
		{IT_CASCADE, "windowMenu", "&Window", NULL, NULL},
		{IT_PUSH, "new","&New...", new_window_cb, NULL},
		{IT_PUSH, "duplicate","&Duplicate", dup_window_cb, NULL},
		{IT_END },
		
		{IT_CASCADE_HELP, "helpMenu", "&Help", NULL, NULL},
		/*{IT_PUSH, "manual", "&User Manual", NULL},*/
		{IT_PUSH, "fileTypes", "&File Types", dbinfo_cb, NULL},
		{IT_PUSH, "about", "&About " APP_TITLE, about_cb, NULL},
	};
	
	/* File list context menu */
	struct menu_item ctx_items[] = {
		/* context dependent action items will be added here at runtime */
		{IT_SEPARATOR, "actionsSeparator", NULL, NULL},
		{IT_PUSH, "copyTo", "&Copy...", copy_to_cb, NULL},
		{IT_PUSH, "moveTo", "&Move...", move_to_cb, NULL},
		{IT_PUSH, "linkTo", "&Link...", link_to_cb, NULL},
		{IT_PUSH, "rename", "&Rename", rename_cb, NULL},
		{IT_PUSH, "delete", "&Delete", delete_cb, NULL},
		{IT_SEPARATOR, NULL },
		{IT_PUSH, "attributes", "&Attributes", attributes_cb, NULL},
	};
	
	app_inst.wmenu = XmCreateMenuBar(app_inst.wmain, "mainMenu", NULL, 0);
	build_menu_bar(app_inst.wmenu, main_items, XtNumber(main_items));
	
	app_inst.wmfile = get_menu_item(app_inst.wmenu, "*fileMenu");
	dbg_assert(app_inst.wmfile);
	app_inst.wmctx = create_popup(app_inst.wlist,
		"contextMenu", ctx_items, XtNumber(ctx_items));
	
	/* populate the Tools pulldown */
	nuser_tools = get_user_tools(&user_tools);
	
	if(nuser_tools) {
		struct menu_item *tool_menu;
		Widget wtools;
		int i;
		
		wtools = get_menu_item(app_inst.wmenu, "*toolsMenu");
		dbg_assert(wtools);

		tool_menu = calloc(nuser_tools + 1, sizeof(struct menu_item));
		tool_menu[0].type = IT_SEPARATOR;
		
		for(i = 0; i < nuser_tools; i++) {
			tool_menu[i + 1].type = IT_PUSH;
			tool_menu[i + 1].name = user_tools[i].name;
			tool_menu[i + 1].callback = user_tool_cbproc;
			tool_menu[i + 1].cb_data = &user_tools[i];
		}
		
		add_menu_items(wtools, tool_menu, nuser_tools + 1);
		
		free(tool_menu);		
	}
}

/* WM_DELETE_WINDOW handler */
static void window_close_cb(Widget w, XtPointer client, XtPointer call)
{
	if(app_inst.wshell) stop_read_proc();
	
	if(app_inst.num_sub_shells) {
		dbg_trace("%d sub-shells still active\n", app_inst.num_sub_shells);
		XtDestroyWidget(app_inst.wshell);
		app_inst.wshell = NULL;
	} else {
		dbg_trace("exit flag set in %s\n", __FUNCTION__);
		XtAppSetExitFlag(app_inst.context);
	}
}

static void sigchld_handler(int sig)
{
	pid_t pid;
	int status = 0;

	pid = waitpid(-1, &status, WNOHANG);

	if(pid > 0 && !read_proc_sigchld(pid, status)) {
		if(mount_proc_sigchld(pid, status) ||
			fs_proc_sigchld(pid, status) ) {
				XtNoticeSignal(sigchld_sigid);
		}
	}
}

static void sigusr_handler(int sig)
{
	XtNoticeSignal(sigchld_sigid);
}

/* Deferred SIGCHLD handler for pids that aren't our reader proc */
static void xt_sigchld_handler(XtPointer p, XtSignalId *id)
{
	/* A copy/move/mount process finished, so... */
	force_update();
}

static int db_dir_filter(const struct dirent *e)
{
	size_t name_len = strlen(e->d_name);
	const size_t suffix_len = sizeof(DB_SUFFIX) - 1;

	if(name_len <= suffix_len ||
		strcasecmp(&e->d_name[name_len - suffix_len], DB_SUFFIX)) return 0;
	return 1;
}

/*
 * Loads any file database files it finds in installation prefix
 * and user's home. Should be called only once at startup.
 * Returns True on success.
 */
static Boolean load_db(void)
{
	char *db_path;
	struct dirent **names;
	int i, nfiles;
	int errv;
	char *home = getenv("HOME");
	Boolean success = False;
	char **db_names = NULL;
	int ndb_names = 0;
	
	db_init(&app_inst.type_db);
	
	/* default database */
	if(!app_res.user_db_only) {
		db_path = build_path(NULL, PREFIX, SHARE_SUBDIR, DB_SUBDIR, NULL);
		if(!db_path) return False;
		nfiles = scandir(db_path, &names, db_dir_filter, alphasort);
		dbg_trace("%d files in system db\n");
		if(nfiles > 0) {
			char *fqn;

			db_names = malloc((nfiles + 1) * sizeof(char*));
			
			for(i = 0; i < nfiles; i++) {
				fqn = build_path(NULL, db_path, names[i]->d_name, NULL);
				if(!fqn) break;
				
				db_names[ndb_names] = fqn;
				ndb_names++;
				
				dbg_trace("%s\n", fqn);
				errv = db_parse_file(fqn, &app_inst.type_db, SYS_DB_PR);
				if(errv) {
					if(app_inst.wmain) {
						va_message_box(app_inst.wmain, MB_ERROR, APP_TITLE,
							"Cannot parse file type database \'%s\'.\n%s.",
							fqn, app_inst.type_db.status_msg);
					} else {
						stderr_msg(
							"Cannot parse file type database \'%s\'.\n%s.",
							fqn, app_inst.type_db.status_msg);
					}
				} else {
					success = True;
				}
			}
			db_names[ndb_names] = NULL;
			
			#ifndef MEMDB
			for(i = 0; i < nfiles; i++) free(names[i]);
			free(names);
			#endif
		}		
		free(db_path);
	}
	/* user database */
	if(home) {
		db_path = build_path(NULL, home, HOME_SUBDIR, DB_SUBDIR, NULL);
		if(!db_path) return False;
		
		if(access(db_path, X_OK) && errno == ENOENT) {
			if((errv = create_path(db_path, CONF_DIR_PERMS))) {
				if(app_inst.wmain) {
					va_message_box(app_inst.wmain, MB_ERROR, APP_TITLE,
						"Cannot access or create RC directory \'%s\'.\n%s.",
						db_path, strerror(errv));
				} else {
					stderr_msg(
						"Cannot access or create RC directory \'%s\'.\n%s.",
						db_path, strerror(errv));
				}
			}
		}
		nfiles = scandir(db_path, &names, db_dir_filter, alphasort);
		dbg_trace("%d files in user db\n");
		
		if(nfiles > 0) {
			char *fqn;
			
			db_names = realloc(db_names,
				sizeof(char*) * (nfiles + ndb_names + 1));
			
			for(i = 0; i < nfiles; i++) {
				fqn = build_path(NULL, db_path, names[i]->d_name, NULL);
				if(!fqn) break;
				
				db_names[ndb_names] = fqn;
				ndb_names++;
				
				dbg_trace("%s\n", fqn);
				errv = db_parse_file(fqn, &app_inst.type_db, USR_DB_PR);
				if(errv) {
					if(app_inst.wmain) {
						va_message_box(app_inst.wmain, MB_ERROR, APP_TITLE,
							"Cannot parse file type database \'%s\'.\n%s",
							fqn, app_inst.type_db.status_msg);
					} else {
						stderr_msg(
							"Cannot parse file type database \'%s\'.\n%s",
							fqn, app_inst.type_db.status_msg);
					}
				} else {
					success = True;
				}
			}
			db_names[ndb_names] = NULL;
			
			#ifndef MEMDB
			for(i = 0; i < nfiles; i++) free(names[i]);
			free(names);
			#endif
		}		
		free(db_path);
	}
		
	app_inst.db_names = db_names;
	
	return success;
}

/*
 * Matches icon size to font height used in the list widget and returns its id
 */
static int get_best_icon_size(void)
{
	int height, ascent, descent, id = IS_SMALL;
	XmRenderTable rt = NULL;
	
	XtVaGetValues(app_inst.wlist, XmNrenderTable, &rt, NULL);
	if(!rt) return IS_SMALL;
	XmRenderTableGetDefaultFontExtents(rt, &height, &ascent, &descent);

	if(height <= 8)
		id = IS_TINY;
	else if(height >= 12)
		id = IS_SMALL;
	else if(height >= 16)
		id = IS_MEDIUM;
	else if(height >= 24)
		id = IS_LARGE;

	return id;
}

/*
 * Invoked from main if xfile is run as 'xfile-open'.
 * This function never returns.
 */
static void xfile_open(int argc, char **argv)
{
	int errors = 0;
	struct env_var_rec vars[4] = { NULL };
	int i;
	if(!load_db()) exit(EXIT_FAILURE);
	
	for(i = 1; i < argc; i++) {
		struct file_type_rec *ft;
		struct stat st;
		int db_type;
		char *name = NULL;
		char *path = NULL;
		char *action = NULL;
		char *exp_cmd;
		char *esc_str;
		int rv;
		
		if(stat(argv[i], &st) == -1) {
			stderr_msg("Error accessing \'%s\': %s.\n",
				argv[i], strerror(errno));
			errors++;
			continue;
		} else if(S_ISDIR(st.st_mode)) {
			char *arg = argv[i];
			rv = spawn_command_args(APP_NAME, &arg, 1);
			if(rv) {
				stderr_msg("Failed execute \'%s %s\': %s.",
					app_inst.bin_name, arg, strerror(rv));
				errors++;
			}
			continue;
		}
		
		db_type = db_match(argv[i], &app_inst.type_db);
		if(DB_DEFINED(db_type)) {
			ft = db_get_record(&app_inst.type_db, db_type);
			vars[2].name = ENV_FMIME;
			vars[2].value = ft->mime;
			if(ft->nactions) action = ft->actions[0].command;
		} else if(DB_ISTEXT(db_type)) {
			action = ENV_DEF_TEXT_CMD;
		}
		
		if(!action) {
			stderr_msg("Unknown file type "
				"or no actions defined for \'%s'.\n", argv[i]);
			errors++;
			continue;
		}
		
		if(escape_string(argv[i], &name))
			name = strdup(argv[i]);
		
		path = get_working_dir();
		if(!escape_string(path, &esc_str)) {
			free(path);
			path = esc_str;
		}

		vars[0].name = ENV_FNAME;
		vars[0].value = name;
		vars[1].name = ENV_FPATH;
		vars[1].value = path;

		rv = expand_env_vars(action, vars, &exp_cmd);
		if(!rv) {
			rv = spawn_command(exp_cmd);
			if(rv) {
				stderr_msg("Error executing \'%s\': %s\n",
					exp_cmd, strerror(rv));
				errors++;
			}
		} else {
			stderr_msg("Error parsing command string \'%s\': %s.\n",
				action, strerror(rv));
			errors++;
		}

		free(exp_cmd);
		free(path);
		free(name);
	}
	if(errors && argc > 2)
		stderr_msg("Exiting with error status due to previous errors.\n");
	
	exit(errors ? EXIT_FAILURE : EXIT_SUCCESS);
}

/* Sets the status bar text from printf(3) arguments */
void set_status_text(const char *fmt, ...)
{
	char *buffer;
	va_list ap;
	size_t len;

	if(!fmt) {
		XmTextFieldSetString(app_inst.wstatus, "");
		return;
	}

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

	buffer = malloc(len);
	if(!buffer) {
		stderr_msg(strerror(errno));
		return;
	}
	va_start(ap, fmt);
	vsnprintf(buffer, len, fmt, ap);
	va_end(ap);
	
	XmTextFieldSetString(app_inst.wstatus, buffer);
	
	free(buffer);
}

/* Fabricates and sets shell and icon title depending on resources set */
void update_shell_title(const char *path)
{
	char *cd = NULL;
	char *home = getenv("HOME");
	char *p;

	if(!path) {
		XtVaSetValues(app_inst.wshell, XmNtitle,
			app_inst.shell_title, XmNiconName, app_inst.shell_title, NULL);
		return;	
	}

	if(home && !strncmp(path, home, strlen(home)) ) {
		cd = malloc(strlen(path));
		p = (char*) &path[strlen(home)];
		sprintf(cd, "~%s", p);
	} else {
		cd = strdup(path);
	}

	if(!app_res.show_full_path && (strlen(cd) > 1)) {
		p = &cd[strlen(cd)];
		while(*p == '/' && p != cd) {
			*p = '\0';
			p--;
		}
		p = strrchr(cd, '/');
		if(p) memmove(cd, p + 1, strlen(p));

	}
	
	if(app_res.show_app_title) {
		char *buffer;
		size_t len = strlen(cd) + strlen(app_inst.shell_title) + 4;
		buffer = malloc(len);

		snprintf(buffer, len, "%s - %s", cd, app_inst.shell_title);

		XtVaSetValues(app_inst.wshell, XmNtitle,
			buffer, XmNiconName, buffer, NULL);
		free(buffer);
	} else {
		XtVaSetValues(app_inst.wshell, XmNtitle,
			cd, XmNiconName, cd, NULL);
	}

	free(cd);	
}

/*
 * Enables disables GUI elements according to flags specified
 */
void set_ui_sensitivity(short flags)
{
	unsigned int i;
	
	char *sel_ops[] = {
		"*attributes",
		"*copyTo",
		"*moveTo",
		"*linkTo",
		"*delete"
	};
	
	char *dir_ops[] = {
		"*makeDirectory",
		"*makeFile",
		"*goUp",
		"*reread",
		"*selectAll",
		"*invertSelection",
		"*deselect",
		"*filter",
		"*selectPattern"
	};
	
	char *single_ops[] = {
		"*rename",
		"*linkTo"
	};
	
	for(i = 0; i < XtNumber(sel_ops); i++) {
		enable_menu_item(app_inst.wmfile, sel_ops[i], (flags & UIF_SEL));
		enable_menu_item(app_inst.wmctx, sel_ops[i], (flags & UIF_SEL));
	}
	
	for(i = 0; i < XtNumber(dir_ops); i++) {
		enable_menu_item(app_inst.wmain, dir_ops[i], (flags & UIF_DIR));
	}

	for(i = 0; i < XtNumber(single_ops); i++) {
		enable_menu_item(app_inst.wmfile, single_ops[i], (flags & UIF_SINGLE));
		enable_menu_item(app_inst.wmctx, single_ops[i], (flags & UIF_SINGLE));
	}

}

/* 
 * Displays an error message and terminates.
 * if "title" is null "Fatal Error" will be used.
 * if "message" is NULL then strerror() text for the errno_value is used.
 */
void fatal_error(int rv, const char *title, const char *msg)
{
	if(msg == NULL) msg = strerror(rv);
	
	if(app_inst.wshell){
		char *buffer;
		
		if(!title) title = "Fatal error";
		buffer = malloc(strlen(title) + strlen(msg) + 4);
		sprintf(buffer, "%s:\n%s", title, msg);
		message_box(app_inst.wshell, MB_ERROR, APP_TITLE, buffer);
		free(buffer);
	}else{
		fprintf(stderr, "[%s] %s: %s\n", APP_TITLE,
			title ? title : "Fatal error", msg);
	}
	exit(EXIT_FAILURE);
}

/* Prints an APP_NAME: prefixed message to stderr */
void stderr_msg(const char *fmt, ...)
{
	va_list ap;

	fputs(APP_NAME ": ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fflush(stderr);
}

/*
 * Forks off a new xfile instance
 * Inherits UI state if inherit_ui is True, uses app-defaults otherwise
 */
void fork_xfile(const char *path, Boolean inherit_ui)
{
	char *argv[20];
	int argc = 0;
	int errval = 0;
	int n = 0;
	char xrm_sz[] = "-xrm";
	
	if(inherit_ui) {
		Arg warg[10];
		int view_mode;
		int sort_order;
		int sort_dir;
		char *order_sz = "name";

		XtSetArg(warg[n], XfNviewMode, &view_mode); n++;
		XtSetArg(warg[n], XfNsortOrder, &sort_order); n++;
		XtSetArg(warg[n], XfNsortDirection, &sort_dir); n++;
		XtGetValues(app_inst.wlist, warg, n);
		
		/* view mode */
		argv[argc] = xrm_sz; argc++;
		argv[argc] = (view_mode == XfCOMPACT) ?
			"*main*fileList.viewMode: compact" :
			"*main*fileList.viewMode: detailed";
		argc++;
		
		/* sorting direction */
		argv[argc] = xrm_sz; argc++;
		argv[argc] = (sort_dir == XfASCEND) ?
			"*main*fileList.sortDirection: ascend" :
			"*main*fileList.sortDirection: descend";
		argc++;
		
		/* sorting order */
		switch(sort_order) {
			case XfNAME:
			order_sz = "main*fileList.sortOrder: name";
			break;
			case XfTIME:
			order_sz = "main*fileList.sortOrder: time";
			break;
			case XfSUFFIX:
			order_sz = "main*fileList.sortOrder: suffix";
			break;
			case XfTYPE:
			order_sz = "main*fileList.sortOrder: type";
			break;
			case XfSIZE:
			order_sz = "main*fileList.sortOrder: size";
			break;
		}
		argv[argc] = xrm_sz; argc++;
		argv[argc] = order_sz; argc++;
		
		/* path and status fields */
		argv[argc] = xrm_sz; argc++;
		if(XmToggleButtonGetState(
			get_menu_item(app_inst.wmenu, "*viewMenu.showPathField"))) {
			argv[argc] = APP_CLASS ".pathField: True";
		} else {
			argv[argc] = APP_CLASS ".pathField: False";
		}
		argc++;
		
		argv[argc] = xrm_sz; argc++;
		if(XmToggleButtonGetState(
			get_menu_item(app_inst.wmenu, "*viewMenu.showStatusField"))) {
			argv[argc] = APP_CLASS ".statusField: True";
		} else {
			argv[argc] = APP_CLASS ".statusField: False";
		}
		argc++;
		
		/* show dot direcotries ? */
		argv[argc] = xrm_sz; argc++;
		if(XmToggleButtonGetState(
			get_menu_item(app_inst.wmenu, "*viewMenu.showAll"))) {
			argv[argc] = APP_CLASS ".showAll: True"; argc++;
		} else {
			argv[argc] = APP_CLASS ".showAll: False"; argc++;
		}
		
		/* filter */
		if(app_inst.filter) {
			argv[argc] = "-f";
			argc++;
			argv[argc] = app_inst.filter;
			argc++;
		}
	}
	
	argv[argc] = (char*)path;
	argc++;
	
	errval = spawn_command_args(app_inst.bin_name, argv, argc);
	if(errval) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Failed to create new %s instance.\n%s",
			app_inst.bin_name, strerror(errval), NULL);
	}
}

/* Reliable signal handling (using POSIX sigaction) */
sigfunc_t rsignal(int sig, sigfunc_t handler, int flags)
{
	struct sigaction set, ret;
	
	set.sa_handler = handler;
	sigemptyset(&set.sa_mask);
	set.sa_flags = flags;
	if(sig == SIGALRM) {
		#ifdef SA_INTERRUPT
		set.sa_flags |= SA_INTERRUPT;
		#endif
	} else {
		set.sa_flags |= SA_RESTART;
	}
	if(sigaction(sig, &set, &ret) < 0) return SIG_ERR;
	
	return ret.sa_handler;
}
