/*
 * Copyright (C) 2023 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * Implements attributes editor dialog and utility functions
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <Xm/Xm.h>
#include <Xm/MwmUtil.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/TextF.h>
#include <Xm/PushB.h>
#include <Xm/ToggleBG.h>
#include <Xm/ToggleB.h>
#include <Xm/LabelG.h>
#include <Xm/SeparatoG.h>
#include <Xm/RowColumn.h>
#include "guiutil.h"
#include "main.h"
#include "comdlgs.h"
#include "const.h"
#include "attrib.h"
#include "stack.h"
#include "path.h"
#include "fsproc.h"
#include "fsutil.h"
#include "debug.h"
#include "memdb.h" /* must be the last header */

enum attrib_gadgets {
	GID_NAME,
	GID_TYPE,
	GID_SIZE,
	GID_CTIME,
	GID_MTIME,
	GID_ATIME,
	NUM_ATT_GAD
};

static const char *attrib_sz[] = {
	"Name:", "Type:", "Size:",
	"Created:", "Modified:", "Accessed:"
};

static const char *mode_sz[] = {
	"Read", "Write", "Exec",
	"SUID", "SGID", "SVTX"
};

static const char *usr_sz[] = {
	"User", "Group", "Others"
};

#define NUM_MODE_GAD 12
#define NUM_MASK_GAD 6

/* indexed by mode toggle gadget callback */
static const mode_t mode_bits[] = {
	S_IRUSR, S_IWUSR, S_IXUSR,
	S_IRGRP, S_IWGRP, S_IXGRP,
	S_IROTH, S_IWOTH, S_IXOTH,
	S_ISUID, S_ISGID, S_ISVTX
};

static const mode_t mask_bits[] = {
	S_IRUSR|S_IRGRP|S_IROTH,
	S_IWUSR|S_IWGRP|S_IWOTH,
	S_IXUSR|S_IXGRP|S_IXOTH
};

/* dialog instance data */
struct attrib_dlg_data {
	Widget wdlg;
	Widget wattrib[NUM_ATT_GAD];
	Widget wmode[NUM_MODE_GAD];
	Widget wmode_mask[NUM_MASK_GAD];
	Widget wowner;
	Widget wrecurse;
	Widget wmask_frm;
	
	Boolean have_dirs;
	Boolean mode_changed;
	Boolean owner_changed;
	mode_t mode;
	mode_t dmode_mask;
	mode_t fmode_mask;
	Boolean set_mode;
	Boolean recurse;
	
	char **files;
	unsigned int nfiles;

	/* 'totals' worker proc data */
	XtWorkProcId calc_size_wpid;
	XtIntervalId totals_update_iid;
	Boolean wp_running;
	struct stack *dir_stk;
	DIR *dir;
	char *cur_path;
	unsigned int ifile;
	size_t size_total;
	unsigned int files_total;
	unsigned int dirs_total;
};

#define TOTALS_UPDATE_INT 250
#define TIME_BUFSIZ	64

/* Forward declarations */
static void create_attrib_dlg(Widget, struct attrib_dlg_data*);
static void free_dlg_data(struct attrib_dlg_data*);
static void attrib_dlg_unmap_cb(Widget, XtPointer, XtPointer);
static void mode_toggle_cb(Widget, XtPointer, XtPointer);
static void fmask_toggle_cb(Widget, XtPointer, XtPointer);
static void dmask_toggle_cb(Widget, XtPointer, XtPointer);
static void recurse_toggle_cb(Widget, XtPointer, XtPointer);
static void owner_edit_cb(Widget, XtPointer, XtPointer);
static void mode_widget_toggle_cb(Widget, XtPointer, XtPointer);
static void apply_cb(Widget, XtPointer, XtPointer);
static void cancel_cb(Widget, XtPointer, XtPointer);
static void totals_update_cb(XtPointer, XtIntervalId*);
static Boolean calc_size_wp(XtPointer);
static char* get_owner_string(uid_t, gid_t);
static Boolean parse_owner_string(Widget, const char*, uid_t*, gid_t*);

void attrib_dlg(Widget wp, char *const *files, unsigned int nfiles)
{
	struct attrib_dlg_data *dlg_data;
	unsigned int i;
	
	dlg_data = calloc(1, sizeof(struct attrib_dlg_data));
	if(dlg_data) dlg_data->files = calloc(nfiles, sizeof(char*));

	if(!dlg_data || !dlg_data->files) {
		if(dlg_data) free(dlg_data);
		va_message_box(wp, MB_ERROR_NB, APP_TITLE,
			"The requested action couldn't be completed "
			"due to an error.\n%s.", strerror(ENOMEM));
		return;
	}

	dlg_data->have_dirs = False;
	
	for(i = 0; i < nfiles; i++) {
		struct stat st;
		if(!stat(files[i], &st) && S_ISDIR(st.st_mode)) {
			dlg_data->have_dirs = True;
		}
		dlg_data->files[i] = strdup(files[i]);
		if(!dlg_data->files[i]) {
			dlg_data->nfiles = i;
			free_dlg_data(dlg_data);
			va_message_box(wp, MB_ERROR_NB, APP_TITLE,
				"The requested action couldn't be completed "
				"due to an error.\n%s.", strerror(ENOMEM));
			return;
		}
	}

	dlg_data->wp_running = False;
	dlg_data->nfiles = nfiles;
	
	create_attrib_dlg(wp, dlg_data);

	/* set initial toggle button states */
	if(dlg_data->nfiles == 1) {
		Arg arg;
		XmString xms;
		struct tm tm_file;
		char *file_name = dlg_data->files[0];
		char *link_name = NULL;
		char *owner;
		char *psz;
		char sz_time[TIME_BUFSIZ];
		char sz_size[SIZE_CS_MAX];
		Boolean is_symlink = False;
		struct stat st, lst;
		unsigned int i, j, m;
	
		if(!lstat(file_name, &lst) && ( (lst.st_mode & S_IFMT) == S_IFLNK) ) {
			char *buf;
			if(!get_link_target(file_name, &buf)) {
				link_name = malloc(strlen(file_name) + strlen(buf) + 4);
				sprintf(link_name, "%s (%s)", file_name, buf);
				free(buf);
			}
			is_symlink = True;
		}

		if(stat(file_name, &st) == -1) {
			XtDestroyWidget(dlg_data->wdlg);
			free_dlg_data(dlg_data);
			if(is_symlink) {
				va_message_box(wp, MB_ERROR_NB, APP_TITLE,
					"Cannot stat the target of symbolic link: "
					"%s.\n%s.", link_name, strerror(errno));
			} else {
				va_message_box(wp, MB_ERROR_NB, APP_TITLE,
					"Cannot stat: %s.\n%s.", file_name, strerror(errno));
			}
			return;
		}
		
		dlg_data->mode = (st.st_mode & 0xfff);
		dlg_data->set_mode = True;

		/* attributes */
		dlg_data->size_total = st.st_size;
		
		psz = gronk_ctrl_chars(link_name ? link_name : file_name);
		if(link_name) free(link_name);
		xms = XmStringCreateLocalized(psz);
		free(psz);
		XtSetArg(arg, XmNlabelString, xms);
		XtSetValues(dlg_data->wattrib[GID_NAME], &arg, 1);
		XmStringFree(xms);

		xms = XmStringCreateLocalized(get_size_string(st.st_size, sz_size));
		XtSetArg(arg, XmNlabelString, xms);
		XtSetValues(dlg_data->wattrib[GID_SIZE], &arg, 1);
		XmStringFree(xms);
		
		xms = XmStringCreateLocalized(
			get_unix_type_string( (is_symlink ? lst.st_mode : st.st_mode) ));
		XtSetArg(arg, XmNlabelString, xms);
		XtSetValues(dlg_data->wattrib[GID_TYPE], &arg, 1);
		XmStringFree(xms);
		
		localtime_r(&st.st_ctime, &tm_file);
		strftime(sz_time, TIME_BUFSIZ, DEF_TIME_FMT, &tm_file);
		xms = XmStringCreateLocalized(sz_time);
		XtSetArg(arg, XmNlabelString, xms);
		XtSetValues(dlg_data->wattrib[GID_CTIME], &arg, 1);
		XmStringFree(xms);

		localtime_r(&st.st_mtime, &tm_file);
		strftime(sz_time, TIME_BUFSIZ, DEF_TIME_FMT, &tm_file);
		xms = XmStringCreateLocalized(sz_time);
		XtSetArg(arg, XmNlabelString, xms);
		XtSetValues(dlg_data->wattrib[GID_MTIME], &arg, 1);
		XmStringFree(xms);

		localtime_r(&st.st_atime, &tm_file);
		strftime(sz_time, TIME_BUFSIZ, DEF_TIME_FMT, &tm_file);
		xms = XmStringCreateLocalized(sz_time);
		XtSetArg(arg, XmNlabelString, xms);
		XtSetValues(dlg_data->wattrib[GID_ATIME], &arg, 1);
		XmStringFree(xms);
		
		/* mode toggle gadgets */
		for(i = 0; i < 3; i++) {
			m = ((st.st_mode & 0x01ff) >> (( 2 - i) * 3));
			for(j = 0;  j < 3; j++) {
				XmToggleButtonGadgetSetState(dlg_data->wmode[i * 3 + j],
					(m & 0x04) ? True : False, False);
				m = ( m << 1) & 0x07;
			}
		}
		
		m = ((st.st_mode & 0x0e00) >> 9);
		for(i = 0; i < 3; i++) {
			XmToggleButtonGadgetSetState(dlg_data->wmode[i + 9],
				(m & 0x04) ? True : False, False);
			m = ( m << 1) & 0x07;		
		}

		/* owner (user:group) string */
		owner = get_owner_string(st.st_uid, st.st_gid);
		XmTextFieldSetString(dlg_data->wowner, owner);
		free(owner);
	} else {
		unsigned int i, j, m;
		dlg_data->mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

		for(i = 0; i < 3; i++) {
			m = ((dlg_data->mode & 0x01ff) >> (( 2 - i) * 3));
			for(j = 0;  j < 3; j++) {
				XmToggleButtonGadgetSetState(dlg_data->wmode[i * 3 + j],
					(m & 0x04) ? True : False, False);
				m = ( m << 1) & 0x07;
			}
		}
	}
	
	/* initial state for mode mask toggles (RWX for files RW for dirs)
	 * invokes the toggle callback to modify d/fmode_mask accordingly */
	if(dlg_data->nfiles > 1 || dlg_data->have_dirs) {
		int i, set[] = { 0, 1, 3, 4 };
		
		for(i = 0; i < (sizeof(set) / sizeof(int)); i++)
			XmToggleButtonGadgetSetState(
				dlg_data->wmode_mask[set[i]], True, True);
	}
	
	/* gray out dir mode mask if no directories in the list */
	if(!dlg_data->have_dirs && dlg_data->nfiles > 1) {
		for(i = 3; i < NUM_MASK_GAD; i++) {
			XtSetSensitive(dlg_data->wmode_mask[i],	False);
		}
	}

	XtAddCallback(dlg_data->wowner, XmNvalueChangedCallback,
		owner_edit_cb, (XtPointer) dlg_data);

	XtManageChild(dlg_data->wdlg);

	if(dlg_data->nfiles > 1 || dlg_data->have_dirs) {
		dlg_data->calc_size_wpid = XtAppAddWorkProc(
			app_inst.context, calc_size_wp, (XtPointer)dlg_data);
		dlg_data->totals_update_iid = XtAppAddTimeOut(
			app_inst.context, TOTALS_UPDATE_INT,
			totals_update_cb, (XtPointer)dlg_data);
	}
}

static void free_dlg_data(struct attrib_dlg_data *data)
{
	if(data->nfiles) {
		unsigned int i;
		
		for(i = 0; i < data->nfiles; i++) {
			free(data->files[i]);
		}
		free(data->files);
	}
	
	if(data->wp_running) {
		/* we were still counting files [see calc_size_wp()] */
		char *ptr;
		
		XtRemoveWorkProc(data->calc_size_wpid);
		XtRemoveTimeOut(data->totals_update_iid);
		
		if(data->cur_path) free(data->cur_path);
		if(data->dir) closedir(data->dir);
		while( (ptr = stk_pop(data->dir_stk, NULL)) ) free(ptr);
		stk_free(data->dir_stk);
	}
	
	free(data);
	memdb_lstat(0);
}

/*
 * Creates the attributes dialog and widgets
 */
static void create_attrib_dlg(Widget wparent, struct attrib_dlg_data *dlg_data)
{
	Arg args[10];
	Cardinal n, i, j;
	Widget wdlg;
	Widget wmode_frm;
	Widget wlabel_rc;
	Widget wdata_rc;
	Widget wmode_rc;
	Widget wowner_rc;
	Widget wmask_rc = None;
	Widget wtmp;
	Widget wapply;
	Widget wcancel;
	Widget wlabel[NUM_ATT_GAD];
	Widget wtop;
	XmString xms;
	XtCallbackRec cbr[2] = { NULL };

	n = 0;
	xms = XmStringCreateLocalized(
		(dlg_data->nfiles > 1) ? "Multi Selection Attributes" : "Attributes");
	XtSetArg(args[n], XmNdialogTitle, xms); n++;
	XtSetArg(args[n], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); n++;
	XtSetArg(args[n], XmNmarginWidth, 8); n++;
	XtSetArg(args[n], XmNmarginHeight, 8); n++;
	XtSetArg(args[n], XmNverticalSpacing, 2); n++;
	XtSetArg(args[n], XmNautoUnmanage, False); n++;
	XtSetArg(args[n], XmNdeleteResponse, XmUNMAP); n++;
	XtSetArg(args[n], XmNunmapCallback, cbr); n++;
	XtSetArg(args[n], XmNresizePolicy, XmRESIZE_GROW); n++;
	cbr[0].callback = attrib_dlg_unmap_cb;
	cbr[0].closure = (XtPointer) dlg_data;
	wdlg = XmCreateFormDialog(wparent, "attributesDialog", args, n);
	XmStringFree(xms);
	XtSetArg(args[0], XmNmwmFunctions, MWM_FUNC_MOVE | MWM_FUNC_CLOSE);
	XtSetValues(XtParent(wdlg), args, 1);
	
	/* general properties */
	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
	XtSetArg(args[n], XmNnumColumns, 1); n++;
	XtSetArg(args[n], XmNmarginHeight, 0); n++;
	XtSetArg(args[n], XmNmarginWidth, 0); n++;
	wlabel_rc = XmCreateRowColumn(wdlg, "labels", args, n);

	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNleftWidget, wlabel_rc); n++;
	XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
	XtSetArg(args[n], XmNnumColumns, 1); n++;
	XtSetArg(args[n], XmNmarginHeight, 0); n++;
	XtSetArg(args[n], XmNmarginWidth, 0); n++;
	wdata_rc = XmCreateRowColumn(wdlg, "data", args, n);
	
	for(i = 0; i < NUM_ATT_GAD; i++) {
		char name[64];
		n = 0;
		xms = XmStringCreateLocalized((char*)attrib_sz[i]);
		XtSetArg(args[n], XmNlabelString, xms); n++;
		wlabel[i] = XmCreateLabelGadget(wlabel_rc, "label", args, n);
		XmStringFree(xms);

		n = 0;
		snprintf(name, 64, "label%s", attrib_sz[i]);
		dlg_data->wattrib[i] = XmCreateLabelGadget(wdata_rc, name, args, n);
	}
	if(dlg_data->nfiles > 1) {
		XtManageChild(dlg_data->wattrib[GID_SIZE]);
		XtManageChild(wlabel[GID_SIZE]);
	} else {
		XtManageChildren(dlg_data->wattrib, NUM_ATT_GAD);
		XtManageChildren(wlabel, NUM_ATT_GAD);
	}
	
	xms = XmStringCreateLocalized("(computing)");
	XtSetArg(args[0], XmNlabelString, xms);
	XtSetValues(dlg_data->wattrib[GID_SIZE], args, 1);
	XmStringFree(xms);

	/* owner */
	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, wlabel_rc); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
	XtSetArg(args[n], XmNpacking, XmPACK_TIGHT); n++;
	XtSetArg(args[n], XmNmarginHeight, 0); n++;
	XtSetArg(args[n], XmNmarginWidth, 0); n++;
	wowner_rc = XmCreateRowColumn(wdlg, "ownerRowColumn", args, n);
	
	n = 0;
	xms = XmStringCreateLocalized("Owner (user:group)");
	XtSetArg(args[n], XmNlabelString, xms); n++;
	wtmp = XmCreateLabelGadget(wowner_rc, "label", args, n);
	XmStringFree(xms);
	
	n = 0;
	XtSetArg(args[n], XmNcolumns, 16); n++;
	dlg_data->wowner = XmCreateTextField(wowner_rc, "owner", args, n);
	XtManageChild(wtmp);
	XtManageChild(dlg_data->wowner);


	/* mode */
	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, dlg_data->wowner); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNshadowType, XmSHADOW_ETCHED_IN); n++;
	wmode_frm = XmCreateFrame(wdlg, "modeFrame", args, n);

	n = 0;
	XtSetArg(args[n], XmNframeChildType, XmFRAME_TITLE_CHILD); n++;
	if(dlg_data->nfiles > 1) {
		XtCallbackRec attrib_toggle_cbr[] = {
			{ mode_widget_toggle_cb, (XtPointer)dlg_data },
			{ NULL, NULL }
		};
		xms = XmStringCreateLocalized("Change Mode");
		XtSetArg(args[n], XmNlabelString, xms); n++;
		XtSetArg(args[n], XmNvalueChangedCallback, attrib_toggle_cbr); n++;
		wtmp = XmCreateToggleButtonGadget(wmode_frm, "label", args, n);
	} else {
		xms = XmStringCreateLocalized("Mode");
		XtSetArg(args[n], XmNlabelString, xms); n++;
		wtmp = XmCreateLabelGadget(wmode_frm, "label", args, n);		
	}
	XmStringFree(xms);
	XtManageChild(wtmp);

	n = 0;
	XtSetArg(args[n], XmNframeChildType, XmFRAME_WORKAREA_CHILD); n++;
	XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
	XtSetArg(args[n], XmNpacking, XmPACK_COLUMN); n++;
	XtSetArg(args[n], XmNmarginHeight, 0); n++;
	XtSetArg(args[n], XmNmarginWidth, 0); n++;
	XtSetArg(args[n], XmNnumColumns, 4); n++;
	wmode_rc = XmCreateRowColumn(wmode_frm, "modeRowColumn", args, n);
	
	/* 3x3 UGOxRWX toggle button matrix */
	for(j = 0; j < 3; j++ ) {
		n = 0;
		xms = XmStringCreateLocalized((char*)usr_sz[j]);
		XtSetArg(args[n], XmNlabelString, xms); n++;
		wtmp = XmCreateLabelGadget(wmode_rc, "label", args, n);
		XmStringFree(xms);
		XtManageChild(wtmp);

		for(i = 0; i < 3; i++) {
			char gad_name[sizeof(usr_sz[j]) + sizeof(mode_sz[i]) + 1];
			XtCallbackRec toggle_cbr[] = {
				{ mode_toggle_cb, (XtPointer)dlg_data }, { NULL, NULL }
			};

			sprintf(gad_name, "%s%s", usr_sz[j], mode_sz[i]);
			gad_name[0] = tolower(gad_name[0]);

			n = 0;
			xms = XmStringCreateLocalized((char*)mode_sz[i]);
			XtSetArg(args[n], XmNuserData, &mode_bits[i + (j * 3)]); n++;
			XtSetArg(args[n], XmNvalueChangedCallback, toggle_cbr); n++;
			XtSetArg(args[n], XmNlabelString, xms); n++;
			XtSetArg(args[n], XmNspacing, 2); n++;
			XtSetArg(args[n], XmNsensitive,
				((dlg_data->nfiles > 1) ? False : True)); n++;
			dlg_data->wmode[i + (j * 3)] = 
				XmCreateToggleButtonGadget(wmode_rc, gad_name, args, n);
			XmStringFree(xms);
		}
	}
	
	/* SUID, SGID, SVTX */
	xms = XmStringCreateLocalized("Special");
	XtSetArg(args[0], XmNlabelString, xms);
	wtmp = XmCreateLabelGadget(wmode_rc, "label", args, 1);
	XtManageChild(wtmp);
	
	for(i = 0; i < 3; i++) {
		char gad_name[sizeof(mode_sz[3 + i]) + 8];
		XtCallbackRec toggle_cbr[] = {
			{ mode_toggle_cb, (XtPointer)dlg_data }, { NULL, NULL }
		};
		
		sprintf(gad_name, "special%s", mode_sz[3 + i]);
		
		n = 0;
		xms = XmStringCreateLocalized((char*)mode_sz[3 + i]);
		XtSetArg(args[n], XmNuserData, &mode_bits[9 + i]); n++;
		XtSetArg(args[n], XmNvalueChangedCallback, toggle_cbr); n++;
		XtSetArg(args[n], XmNlabelString, xms); n++;
		XtSetArg(args[n], XmNspacing, 2); n++;
		XtSetArg(args[n], XmNsensitive,
			((dlg_data->nfiles > 1) ? False : True)); n++;
		dlg_data->wmode[9 + i] = 
			XmCreateToggleButtonGadget(wmode_rc, gad_name, args, n);
		XmStringFree(xms);		
	}
	
	XtManageChildren(dlg_data->wmode, NUM_MODE_GAD);
	
	wtop = wmode_frm;

	if((dlg_data->nfiles > 1) || dlg_data->have_dirs) {
		/* extra options for directories/multisel */
		XtCallbackRec toggle_cbr[] = {
			{ recurse_toggle_cb, (XtPointer)dlg_data  },
			{ NULL, NULL }
		};
		
		if(dlg_data->have_dirs) {
			n = 0;
			XtSetArg(args[n], XmNtopWidget, wtop); n++;
			XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
			XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
			xms = XmStringCreateLocalized("Set Mode and/or Owner Recursively");
			XtSetArg(args[n], XmNlabelString, xms); n++;
			XtSetArg(args[n], XmNset, XmUNSET);
			XtSetArg(args[n], XmNvalueChangedCallback, toggle_cbr); n++;
			dlg_data->wrecurse = XmCreateToggleButton(wdlg, "recurse", args, n);
			XmStringFree(xms);
			
			wtop = dlg_data->wrecurse;
		}

		n = 0;
		XtSetArg(args[n], XmNtopWidget, wtop); n++;
		XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
		XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNshadowType, XmSHADOW_ETCHED_IN); n++;
		XtSetArg(args[n], XmNsensitive, False); n++;
		dlg_data->wmask_frm = XmCreateFrame(wdlg, "modeMaskFrame", args, n);
		
		n = 0;
		if(dlg_data->nfiles > 1)
			xms = XmStringCreateLocalized("Multi-Selection Mode Mask");
		else
			xms = XmStringCreateLocalized("Recursion Mode Mask");
		XtSetArg(args[n], XmNlabelString, xms); n++;
		XtSetArg(args[n], XmNframeChildType, XmFRAME_TITLE_CHILD); n++;
		wtmp = XmCreateLabelGadget(dlg_data->wmask_frm, "label", args, n);
		XmStringFree(xms);
		XtManageChild(wtmp);

		n = 0;
		XtSetArg(args[n], XmNframeChildType, XmFRAME_WORKAREA_CHILD); n++;
		XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
		XtSetArg(args[n], XmNpacking, XmPACK_COLUMN); n++;
		XtSetArg(args[n], XmNmarginHeight, 0); n++;
		XtSetArg(args[n], XmNmarginWidth, 0); n++;
		XtSetArg(args[n], XmNnumColumns, 2); n++;
		wmask_rc = XmCreateRowColumn(dlg_data->wmask_frm,
			"modeMaskRowColumn", args, n);

		/* File/Directory mode mask */
		xms = XmStringCreateLocalized("File");
		XtSetArg(args[0], XmNlabelString, xms);
		wtmp = XmCreateLabelGadget(wmask_rc, "label", args, 1);
		XmStringFree(xms);
		XtManageChild(wtmp);

		toggle_cbr[0].callback = fmask_toggle_cb;

		for(i = 0; i < 3; i++) {
			char gad_name[sizeof(mode_sz[i]) + 16];
			sprintf(gad_name, "fileModeMask%s", mode_sz[i]);

			n = 0;
			xms = XmStringCreateLocalized((char*)mode_sz[i]);
			XtSetArg(args[n], XmNuserData, &mask_bits[i]); n++;
			XtSetArg(args[n], XmNvalueChangedCallback, toggle_cbr); n++;
			XtSetArg(args[n], XmNlabelString, xms); n++;
			XtSetArg(args[n], XmNspacing, 2); n++;
			dlg_data->wmode_mask[i] = 
				XmCreateToggleButtonGadget(wmask_rc, gad_name, args, n);
			XmStringFree(xms);		
		}

		xms = XmStringCreateLocalized("Directory");
		XtSetArg(args[0], XmNlabelString, xms);
		wtmp = XmCreateLabelGadget(wmask_rc, "label", args, 1);
		XmStringFree(xms);
		XtManageChild(wtmp);

		toggle_cbr[0].callback = dmask_toggle_cb;

		for(i = 0; i < 3; i++) {
			char gad_name[sizeof(mode_sz[i]) + 20];
			sprintf(gad_name, "directoryModeMask%s", mode_sz[i]);

			n = 0;
			xms = XmStringCreateLocalized((char*)mode_sz[i]);
			XtSetArg(args[n], XmNuserData, &mode_bits[i]); n++;
			XtSetArg(args[n], XmNvalueChangedCallback, toggle_cbr); n++;
			XtSetArg(args[n], XmNlabelString, xms); n++;
			XtSetArg(args[n], XmNspacing, 2); n++;
			dlg_data->wmode_mask[3 + i] = 
				XmCreateToggleButtonGadget(wmask_rc, gad_name, args, n);
			XmStringFree(xms);		
		}
		XtManageChildren(dlg_data->wmode_mask, NUM_MASK_GAD);
		
		wtop = dlg_data->wmask_frm;
	} /* have dirs or multisel */

	/* action buttons */
	n = 0;
	xms = XmStringCreateLocalized("Apply");
	XtSetArg(args[n], XmNmarginHeight, 4); n++;
	XtSetArg(args[n], XmNmarginWidth, 4); n++;
	XtSetArg(args[n], XmNlabelString, xms); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, wtop); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNactivateCallback, cbr); n++;
	cbr[0].callback = apply_cb;
	cbr[0].closure = (XtPointer) dlg_data;
	wapply = XmCreatePushButton(wdlg, "apply", args, n);
	XmStringFree(xms);
	XtManageChild(wapply);

	n = 0;
	xms = XmStringCreateLocalized("Dismiss");
	XtSetArg(args[n], XmNmarginHeight, 4); n++;
	XtSetArg(args[n], XmNmarginWidth, 4); n++;
	XtSetArg(args[n], XmNlabelString, xms); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, wtop); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNactivateCallback, cbr); n++;
	cbr[0].callback = cancel_cb;
	cbr[0].closure = (XtPointer) dlg_data;
	wcancel = XmCreatePushButton(wdlg, "dismiss", args, n);
	XmStringFree(xms);
	XtManageChild(wcancel);

	n = 0;
	XtSetArg(args[n], XmNdefaultButton, wapply); n++;
	XtSetArg(args[n], XmNcancelButton, wcancel); n++;
	XtSetValues(wdlg, args, n);

	if(dlg_data->have_dirs || (dlg_data->nfiles > 1)) {
		if(dlg_data->have_dirs) XtManageChild(dlg_data->wrecurse);
		XtManageChild(wmask_rc);
		XtManageChild(dlg_data->wmask_frm);
	}
	XtManageChild(wdata_rc);
	XtManageChild(wowner_rc);
	XtManageChild(wlabel_rc);
	XtManageChild(wmode_rc);
	XtManageChild(wmode_frm);

	dlg_data->wdlg = wdlg;
}

static void apply_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct attrib_dlg_data* dlg_data = (struct attrib_dlg_data*) pclient;
	int att_flags = 0;
	gid_t gid = 0;
	uid_t uid = 0;
	mode_t fmode_mask = 0xfff;
	mode_t dmode_mask = 0xfff;
	
	#ifdef DEBUG
	char mode_sz[MODE_CS_MAX];
	#endif

	/* always mix in file mode mask, since we only provide RWX toggles */
	dlg_data->dmode_mask |= 0xe000;
	dlg_data->fmode_mask |= 0xe000;
	
	if(dlg_data->set_mode && dlg_data->mode_changed)
		att_flags |= ATT_FMODE | ATT_DMODE;

	if(dlg_data->owner_changed) {
		Boolean valid;
		char *sz_owner;
		
		sz_owner = XmTextFieldGetString(dlg_data->wowner);
		valid = parse_owner_string(dlg_data->wdlg, sz_owner, &uid, &gid);
		XtFree(sz_owner);
		if(!valid) return;
		
		att_flags |= ATT_OWNER;
	}

	if(dlg_data->have_dirs &&
		XmToggleButtonGetState(dlg_data->wrecurse)) {
			att_flags |= ATT_RECUR;
		dmode_mask = dlg_data->dmode_mask;
		fmode_mask = dlg_data->fmode_mask;
	}
	
	if(dlg_data->nfiles > 1) fmode_mask = dlg_data->fmode_mask;
	
	dbg_trace("mode : %s\n", get_mode_string(dlg_data->mode, mode_sz));
	dbg_trace("fmask: %s\n", get_mode_string(fmode_mask, mode_sz));
	dbg_trace("dmask: %s\n", get_mode_string(dmode_mask, mode_sz));

	/* forks off a process */
	set_attributes(dlg_data->files, dlg_data->nfiles, gid, uid,
		dlg_data->mode, dlg_data->mode,	fmode_mask, dmode_mask, att_flags);
	
	XtUnmanageChild(dlg_data->wdlg);
}

static void cancel_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct attrib_dlg_data* dlg_data = (struct attrib_dlg_data*) pclient;
	XtUnmanageChild(dlg_data->wdlg);
}

static void mode_toggle_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	Arg arg;
	mode_t *pmode;
	struct attrib_dlg_data* dlg_data = (struct attrib_dlg_data*) pclient;
	XmToggleButtonCallbackStruct *cbs = 
		(XmToggleButtonCallbackStruct*) pcall;

	dlg_data->mode_changed = True;

	/* pointer to mode_bits */
	XtSetArg(arg, XmNuserData, &pmode);
	XtGetValues(w, &arg, 1);
	
	if(cbs->set)
		dlg_data->mode |= (*pmode);
	else
		dlg_data->mode &= ~(*pmode);
}

static void dmask_toggle_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	Arg arg;
	mode_t *pmode;
	struct attrib_dlg_data* dlg_data = (struct attrib_dlg_data*) pclient;
	XmToggleButtonCallbackStruct *cbs = 
		(XmToggleButtonCallbackStruct*) pcall;

	/* pointer to mode_bits */
	XtSetArg(arg, XmNuserData, &pmode);
	XtGetValues(w, &arg, 1);
	
	if(cbs->set)
		dlg_data->dmode_mask |= (*pmode);
	else
		dlg_data->dmode_mask &= ~(*pmode);
}

static void fmask_toggle_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	Arg arg;
	mode_t *pmode;
	struct attrib_dlg_data* dlg_data = (struct attrib_dlg_data*) pclient;
	XmToggleButtonCallbackStruct *cbs = 
		(XmToggleButtonCallbackStruct*) pcall;

	/* pointer to mode_bits */
	XtSetArg(arg, XmNuserData, &pmode);
	XtGetValues(w, &arg, 1);
	
	if(cbs->set)
		dlg_data->fmode_mask |= (*pmode);
	else
		dlg_data->fmode_mask &= ~(*pmode);
}

static void recurse_toggle_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct attrib_dlg_data* dlg_data = (struct attrib_dlg_data*) pclient;
	XmToggleButtonCallbackStruct *cbs = 
		(XmToggleButtonCallbackStruct*) pcall;

	dlg_data->mode_changed = True;
	dlg_data->recurse = cbs->set;
	
	if(dlg_data->nfiles == 1)
		XtSetSensitive(dlg_data->wmask_frm, cbs->set);
	else if(dlg_data->set_mode && cbs->set)
		XtSetSensitive(dlg_data->wmask_frm,	True);
}

static void owner_edit_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct attrib_dlg_data* dlg_data = (struct attrib_dlg_data*) pclient;
	dlg_data->owner_changed = True;
}

static void attrib_dlg_unmap_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	struct attrib_dlg_data* dlg_data = (struct attrib_dlg_data*) pclient;

	XtDestroyWidget(dlg_data->wdlg);
	free_dlg_data(dlg_data);
}

static void mode_widget_toggle_cb(Widget w, XtPointer pclient, XtPointer pcall)
{
	unsigned int i;
	struct attrib_dlg_data* dlg_data = 
		(struct attrib_dlg_data*) pclient;
	XmToggleButtonCallbackStruct *cbs = 
		(XmToggleButtonCallbackStruct*) pcall;
	
	dlg_data->set_mode = cbs->set;
	dlg_data->mode_changed = cbs->set;
	
	for(i = 0; i < NUM_MODE_GAD; i++)
		XtSetSensitive(dlg_data->wmode[i], cbs->set);
	
	XtSetSensitive(dlg_data->wmask_frm, cbs->set);
}

/*
 * Updates size and totals labels periodically, while calc_size_wp is active
 */
static void totals_update_cb(XtPointer cbd, XtIntervalId *iid)
{
	struct attrib_dlg_data* dlg_data = 
		(struct attrib_dlg_data*) cbd;
	char size_sz[SIZE_CS_MAX + 3];
	char *items_sz = NULL;
	XmString label;
	Arg args[5];
	
	if(dlg_data->wp_running) {
		/* hopefully this signifies clearly enough that we're still counting */	
		size_sz[0] = '~';
		get_size_string(dlg_data->size_total, &size_sz[1]);
	} else {
		get_size_string(dlg_data->size_total, size_sz);
	}

	if(dlg_data->nfiles > 1 || dlg_data->have_dirs) {
		size_t items_len = 0;

		do_items_sz: /* count on first, fill out on second pass */
		if(dlg_data->have_dirs) {
			items_len = snprintf(items_sz, items_len,
				"%u file%s in %u director%s",
				dlg_data->files_total,
				( (dlg_data->files_total == 1) ? "" : "s" ),
				dlg_data->dirs_total,
				( (dlg_data->dirs_total == 1) ? "y" : "ies"));
		} else {
			items_len = snprintf(items_sz, items_len,
				"%u %s", dlg_data->files_total,
				( (dlg_data->files_total == 1) ? "file" : "files" ));
		}
		if(!items_sz) {
			items_sz = malloc(++items_len);
			goto do_items_sz;
		}
	}

	if(dlg_data->have_dirs || dlg_data->nfiles > 1) {
		/* size and item counts combined in the 'size' field */
		char content_sz[strlen(size_sz) + strlen(items_sz) + 4];

		sprintf(content_sz, "%s (%s)", size_sz, items_sz);

		label = XmStringCreateLocalized(content_sz);
		XtSetArg(args[0], XmNlabelString, label);
		XtSetValues(dlg_data->wattrib[GID_SIZE], args, 1);
		XmStringFree(label);
	}
	
	if(items_sz) free(items_sz);
	
	/* Reactivate the update timeout if we're still counting */
	if(dlg_data->wp_running) {
		dlg_data->totals_update_iid = XtAppAddTimeOut(
			app_inst.context, TOTALS_UPDATE_INT,
			totals_update_cb, (XtPointer)dlg_data);
	}
}

/*
 * Xt work procedure. Calculates total size and number of files,
 * including sub-directories, in the file list passed to attrib_dlg,
 * processing one file at a time, removing itself when finished.
 */
static Boolean calc_size_wp(XtPointer client_data)
{
	struct attrib_dlg_data *dlg_data =
		(struct attrib_dlg_data*) client_data;
	struct stat st;
	char *path;

	if(!dlg_data->wp_running) {
		dlg_data->ifile = 0;
		dlg_data->dir_stk = stk_alloc();
		dlg_data->dir = NULL;
		dlg_data->size_total = 0;
		dlg_data->cur_path = NULL;
		dlg_data->wp_running = True;
	}

	if(dlg_data->dir) {
		struct dirent *ent;
		
		ent = readdir(dlg_data->dir);
		if(!ent) {
			closedir(dlg_data->dir);
			dlg_data->dir = NULL;
			if(dlg_data->cur_path) {
				free(dlg_data->cur_path);
				dlg_data->cur_path = NULL;
			}
		} else {
			char *fqn;
			
			if(!strcmp(ent->d_name, ".") ||
				!strcmp(ent->d_name, "..")) return False;
			
			dbg_assert(dlg_data->cur_path);
			fqn = build_path(NULL, dlg_data->cur_path, ent->d_name, NULL);

			if(!stat(fqn, &st)) {
				dlg_data->size_total += st.st_size;
			
				if(S_ISDIR(st.st_mode)) {
					stk_push(dlg_data->dir_stk, fqn, strlen(fqn) + 1);
					dlg_data->dirs_total++;
				} else {
					dlg_data->files_total++;
				}
			}
			
			free(fqn);
		}
		return False;
	
	} else if( (path = stk_pop(dlg_data->dir_stk, NULL)) ) {
		dlg_data->dir = opendir(path);
		if(dlg_data->dir) dlg_data->cur_path = path;
		return False;

	} else if(dlg_data->ifile < dlg_data->nfiles){
		if(!stat(dlg_data->files[dlg_data->ifile], &st)) {
			dlg_data->size_total += st.st_size;

			if(S_ISDIR(st.st_mode)) {
				dlg_data->dir = opendir(dlg_data->files[dlg_data->ifile]);
				if(dlg_data->dir) {
					dlg_data->cur_path =
						strdup(dlg_data->files[dlg_data->ifile]);
				}
				dlg_data->dirs_total++;
			} else {
				dlg_data->files_total++;
			}
		}
		dlg_data->ifile++;
		return False;
	}

	dbg_assert(dlg_data->dir == NULL);
	dbg_assert(!dlg_data->dir_stk->num_recs);
	dbg_assert(!dlg_data->cur_path);
	
	/* we're done if arrived here */
	stk_free(dlg_data->dir_stk);
	dlg_data->wp_running = False;

	return True;
}

/*
 * Returns malloc'd user:group string for given uid, gid, or NULL on error.
 */
static char* get_owner_string(uid_t uid, gid_t gid)
{
	struct passwd *pw;
	struct group *gr;
	char *buf = NULL;

	gr = getgrgid(gid);
	pw = getpwuid(uid);
	
	if(gr && pw) {
		size_t len = strlen(gr->gr_name) + strlen(pw->pw_name);
		buf = malloc(len + 3);
		if(buf) sprintf(buf, "%s:%s", pw->pw_name, gr->gr_name);
	} else {
		buf = malloc(32);
		if(buf) snprintf(buf, 32, "%d:%d", uid, gid);
	}
	return buf;
}

/*
 * Parses user:group string and places ids in r_uid/gid if exist.
 * Returns zero on success, errno otherwise.
 */
static Boolean parse_owner_string(
	Widget wdlg, const char *str, uid_t *r_uid, gid_t *r_gid)
{
	unsigned int uid;
	unsigned int gid;
	size_t str_len = strlen(str);
	struct passwd *pw;
	struct group *gr;
	
	if(strchr(str, ':')) {
		if(sscanf(str, "%u:%u", &uid, &gid) == 2) {
			pw = getpwuid((uid_t)uid);
			gr = getgrgid((gid_t)gid);
		} else {
			char sz_uid[str_len];
			char sz_gid[str_len];

			if(sscanf(str, "%[^:]:%[^:]", sz_uid, sz_gid) < 2) {
				message_box(wdlg, MB_ERROR, APP_TITLE,
					"The owner must be specified as USER:GROUP,\n"
					"both either as names or as numeric IDs.");
				return False;
			}
			pw = getpwnam(sz_uid);
			gr = getgrnam(sz_gid);
		}
	} else {
		if(sscanf(str, "%u", &uid) == 1) {
			pw = getpwuid((uid_t)uid);
			gr = getgrgid((gid_t)uid);
		} else {
			char sz_id[str_len];

			if(sscanf(str, "%[^:]", sz_id) < 1) {
				message_box(wdlg, MB_ERROR, APP_TITLE,
					"Specify a valid user/group name.");
				return False;
			}
			pw = getpwnam(sz_id);
			gr = getgrnam(sz_id);
		}
	}
	
	if(!pw || !gr) {
		char *err_str = NULL;
		
		if(!pw && !gr) {
			err_str = "Invalid owner specified. No such user nor group!";
		} else if(!pw) {
			err_str = "Invalid owner specified. No such user!";
		} else if(!gr) {
			err_str = "Invalid owner specified. No such group!";
		}
		message_box(wdlg, MB_ERROR, APP_TITLE, err_str);
		return False;
	}
	
	*r_uid = pw->pw_uid;
	*r_gid = gr->gr_gid;

	return True;
}
