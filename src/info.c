/*
 * Copyright (C) 2023 alx@fastestcode.org
 * This software is distributed under the terms of the MIT/X license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <Xm/Xm.h>
#include <Xm/MwmUtil.h>
#include <Xm/Text.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/Separator.h>
#include <Xm/DialogS.h>
#include <Xm/List.h>

#include "main.h"
#include "graphics.h"
#include "guiutil.h"
#include "comdlgs.h"
#include "const.h"
#include "version.h"

/* Icon bitmaps */
#include "xbm/cabinet.xpm"

void display_dbinfo_dialog(Widget wparent)
{
	Arg args[10];
	Cardinal n;
	Widget wdlg;
	Widget wform;
	Widget wtlabel;
	Widget wdblabel;
	Widget wtlist;
	Widget wdblist;
	Widget wclose;
	Widget wsep;
	XmString xms;
	unsigned int i;
	char **ptr;
	
	if(!app_inst.type_db.count) {
		message_box(wparent, MB_NOTIFY, "Type Database Info",
			"There are no file types defined.");
		return;
	}
	
	n = 0;
	XtSetArg(args[n], XmNmappedWhenManaged, True); n++;
	XtSetArg(args[n], XmNallowShellResize, True); n++;
	XtSetArg(args[n], XmNdeleteResponse, XmDESTROY); n++;
	XtSetArg(args[n], XmNminWidth, 320); n++;
	XtSetArg(args[n], XmNminHeight, 280); n++;
	wdlg = XmCreateDialogShell(wparent, "Type Database Info", args, n);

	n = 0;
	XtSetArg(args[n], XmNhorizontalSpacing, 4); n++;
	XtSetArg(args[n], XmNverticalSpacing, 4); n++;
	XtSetArg(args[n], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); n++;
	wform = XmCreateForm(wdlg, "form", args, n);
	
	n = 0;
	xms = XmStringCreateLocalized("Sourced Database Files");
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNlabelString, xms); n++;
	wdblabel = XmCreateLabel(wform, "label", args, n);
	XmStringFree(xms);
	
	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, wdblabel); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNheight, 40); n++;
	wdblist = XmCreateScrolledList(wform, "files", args, n);

	n = 0;
	xms = XmStringCreateLocalized("Defined File Types");
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, XtParent(wdblist)); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNlabelString, xms); n++;
	wtlabel = XmCreateLabel(wform, "label", args, n);
	XmStringFree(xms);

	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, wtlabel); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNheight, 80); n++;
	wtlist = XmCreateScrolledList(wform, "types", args, n);

	n = 0;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
	XtSetArg(args[n], XmNseparatorType, XmSHADOW_ETCHED_IN); n++;
	wsep = XmCreateSeparator(wform, "separator", args, n);

	n = 0;
	xms = XmStringCreateLocalized("Close");
	XtSetArg(args[n], XmNlabelString, xms); n++;
	XtSetArg(args[n], XmNshowAsDefault, True); n++;
	XtSetArg(args[n], XmNsensitive, True); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	wclose = XmCreatePushButton(wform, "closeButton", args, n);
	XmStringFree(xms);
	
	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNbottomWidget, wsep); n++;
	XtSetValues(XtParent(wtlist), args, n);

	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNbottomWidget, wclose); n++;
	XtSetValues(wsep, args, n);

	n = 0;
	XtSetArg(args[n], XmNdefaultButton, wclose); n++;
	XtSetArg(args[n], XmNcancelButton, wclose); n++;
	XtSetValues(wform, args, n);

	if((ptr = app_inst.db_names)) {
		while(*ptr) {
			xms = XmStringCreateLocalized(*ptr);
			if(xms) {
				XmListAddItem(wdblist, xms, 0);
				XmStringFree(xms);
			}
			ptr++;
		}
	} else {
		xms = XmStringCreateLocalized("(Using fallback database)");
		if(xms) {
			XmListAddItem(wdblist, xms, 0);
			XmStringFree(xms);
		}
	}
	
	for(i = 0; i < app_inst.type_db.count; i++) {
		xms = XmStringCreateLocalized(app_inst.type_db.recs[i].name);
		if(xms) {
			XmListAddItem(wtlist, xms, 0);
			XmStringFree(xms);
		}
	}

	XtManageChild(wdblabel);
	XtManageChild(wtlabel);
	XtManageChild(wdblist);
	XtManageChild(wtlist);
	XtManageChild(wsep);
	XtManageChild(wclose);	
	XtManageChild(wform);

	XtRealizeWidget(wdlg);
}

void display_about_dialog(Widget wparent)
{
	Arg args[10];
	Cardinal n;
	Widget wdlg;
	Widget wform;
	Widget wtext;
	Widget wicon;
	Widget wclose;
	Widget wsep;
	Pixmap icon_pix;
	char *about_text;
	size_t text_len;
	XmString xms;
	
	text_len = snprintf(NULL, 0, "%s\nVersion %d.%d (%s)\n\n%s",
		DESCRIPTION_CS, APP_VER, APP_REV, APP_BLD, COPYRIGHT_CS) + 1;
	about_text = malloc(text_len);
	snprintf(about_text, text_len, "%s\nVersion %d.%d (%s)\n\n%s",
		DESCRIPTION_CS, APP_VER, APP_REV, APP_BLD, COPYRIGHT_CS);
	
	n = 0;
	XtSetArg(args[n], XmNmappedWhenManaged, True); n++;
	XtSetArg(args[n], XmNallowShellResize, True); n++;
	XtSetArg(args[n], XmNdeleteResponse, XmDESTROY); n++;
	wdlg = XmCreateDialogShell(wparent, "About", args, n);

	n = 0;
	XtSetArg(args[n], XmNhorizontalSpacing, 4); n++;
	XtSetArg(args[n], XmNverticalSpacing, 4); n++;
	XtSetArg(args[n], XmNnoResize, True); n++;
	XtSetArg(args[n], XmNdialogStyle, XmDIALOG_PRIMARY_APPLICATION_MODAL); n++;
	wform = XmCreateForm(wdlg, "form", args, n);

	if(!create_ui_pixmap(wform, cabinet_xpm, &icon_pix, NULL))
		icon_pix = XmUNSPECIFIED_PIXMAP;

	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNlabelPixmap, icon_pix); n++;
	XtSetArg(args[n], XmNlabelType, XmPIXMAP); n++;
	wicon = XmCreateLabel(wform, "icon", args, n);
	
	n = 0;
	xms = XmStringCreateLocalized(about_text);
	XtSetArg(args[n], XmNlabelString, xms); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNleftWidget, wicon); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	wtext = XmCreateLabel(wform, "text", args, n);
	XmStringFree(xms);

	n = 0;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, wtext); n++;
	XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
	XtSetArg(args[n], XmNseparatorType, XmSHADOW_ETCHED_IN); n++;
	wsep = XmCreateSeparator(wform, "separator", args, n);

	n = 0;
	xms = XmStringCreateLocalized("Close");
	XtSetArg(args[n], XmNlabelString, xms); n++;
	XtSetArg(args[n], XmNshowAsDefault, True); n++;
	XtSetArg(args[n], XmNsensitive, True); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, wsep); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	wclose = XmCreatePushButton(wform, "closeButton", args, n);
	XmStringFree(xms);

	n = 0;
	XtSetArg(args[n], XmNdefaultButton, wclose); n++;
	XtSetArg(args[n], XmNcancelButton, wclose); n++;
	XtSetValues(wform, args, n);

	XtManageChild(wicon);
	XtManageChild(wtext);
	XtManageChild(wsep);
	XtManageChild(wclose);	
	XtManageChild(wform);

	XtRealizeWidget(wdlg);
	
	free(about_text);
}
