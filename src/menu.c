/*
 * Copyright (C) 2022 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */
 
/*
 * Menu construction routines
 */

#include <stdlib.h>
#include <stdio.h>
#include <Xm/Xm.h>
#include <Xm/RowColumn.h>
#include <Xm/CascadeB.h>
#include <Xm/SeparatoG.h>
#include <Xm/PushBG.h>
#include <Xm/ToggleBG.h>
#include "main.h"
#include "menu.h"
#include "debug.h"
#include "memdb.h" /* must be the last header */

/* For making up some widget names */
#define WIDGET_NAME_MAX 64

static XmString munge_label(const char*, const char*, KeySym*);

/*
 * Creates a popup menu
 */
Widget create_popup(Widget wparent, const char *name,
	const struct menu_item *items, unsigned int nitems)
{
	Widget wpopup;
	unsigned int i, n;
	XmString label_str = NULL;
	Arg args[8];
	KeySym mnemonic;
	char tmp_name[WIDGET_NAME_MAX];
	WidgetClass wc[] = {
		xmPushButtonGadgetClass,
		xmToggleButtonGadgetClass,
		xmToggleButtonGadgetClass,
		xmSeparatorGadgetClass
	};
	
	n = 0;
	XtSetArg(args[n], XmNpopupEnabled, XmPOPUP_KEYBOARD); n++;
	wpopup = XmCreatePopupMenu(wparent, (String)name, args, n);
	
	for(i = 0, n = 0; i < nitems; i++, n = 0){
		XtCallbackRec item_cb[] = {
			{ (XtCallbackProc)items[i].callback, (XtPointer)items[i].cb_data},
			{ (XtCallbackProc)NULL, (XtPointer)NULL}
		};
		
		/* create and set label string if any */
		if(items[i].label){
			label_str = munge_label(items[i].label, NULL, &mnemonic);
			XtSetArg(args[n], XmNlabelString, label_str); n++;
			if(mnemonic){
				XtSetArg(args[n], XmNmnemonic, mnemonic);
				n++;
			}
		}
		
		switch(items[i].type){
			case IT_PUSH:
			if(items[i].callback){
				XtSetArg(args[n], XmNactivateCallback, item_cb); n++;
			}
			break;
			
			case IT_TOGGLE:
			if(items[i].callback){
				XtSetArg(args[n], XmNvalueChangedCallback, item_cb); n++;
			}
			break;
			
			case IT_RADIO:
			XtSetArg(args[n], XmNindicatorType, XmONE_OF_MANY); n++;
			if(items[i].callback){
				XtSetArg(args[n], XmNvalueChangedCallback, item_cb); n++;
			}
			break;
			
			default: break;
		}
		
		if(!items[i].name) snprintf(tmp_name, WIDGET_NAME_MAX, "widget%d", i);
		
		XtCreateManagedWidget(items[i].name ? items[i].name : tmp_name,
			wc[items[i].type], wpopup, args, n);
		
		/* temporary label XmString */
		if(items[i].label) XmStringFree(label_str);
	}

	return wpopup;
}

/*
 * Populates the specified menu bar with pulldown menus.
 * First item in the 'items' array must be an IT_CASCADE. All items
 * that follow a cascade item until IT_END will belong to the pulldown
 * associated with that button.
 */
void build_menu_bar(Widget wmenu_bar,
	const struct menu_item *items, unsigned int nitems)
{
	unsigned int i, n, level;
	Widget wlevel[MENU_MAX_LEVELS + 1] = {None};
	XmString label_str = NULL;
	Arg args[10];
	KeySym mnemonic;
	char tmp_name[WIDGET_NAME_MAX];
	WidgetClass wc[] = {
		xmPushButtonGadgetClass,
		xmToggleButtonGadgetClass,
		xmToggleButtonGadgetClass,
		xmSeparatorGadgetClass
	};
	
	wlevel[0] = wmenu_bar;
		
	for(i = 0, level = 0; i < nitems; i++){
		XtCallbackRec item_cb[] = {
			{(XtCallbackProc)items[i].callback, (XtPointer)items[i].cb_data},
			{(XtCallbackProc)NULL, (XtPointer)NULL}
		};
		
		dbg_assert((level + 1) < MENU_MAX_LEVELS);
		
		if(items[i].type == IT_END){
			level--;
			dbg_assert(level >= 0);
			continue;
		}
		
		if(items[i].type >= IT_CASCADE ){
			Widget wpulldown;
			Widget wcascade;

			wpulldown = XmCreatePulldownMenu(
				wlevel[level], items[i].name, NULL, 0);
			if(items[i].type == IT_CASCADE_RADIO){
				XtVaSetValues(wpulldown, XmNradioAlwaysOne, True,
					XmNradioBehavior, True, NULL);
			}
			
			label_str = munge_label(items[i].label, NULL, &mnemonic);
			
			n = 0;
			XtSetArg(args[n], XmNlabelString, label_str); n++;
			XtSetArg(args[n], XmNsubMenuId, wpulldown); n++;
			if(mnemonic){
				XtSetArg(args[n], XmNmnemonic, mnemonic); n++;
			}
			snprintf(tmp_name, WIDGET_NAME_MAX, "%sCascade", items[i].name);
			wcascade = XmCreateCascadeButton(
				wlevel[level], tmp_name, args, n);
			XmStringFree(label_str);
			
			if(items[i].type==IT_CASCADE_HELP)
				XtVaSetValues(wmenu_bar, XmNmenuHelpWidget, wcascade, NULL);
			
			XtManageChild(wcascade);
			
			wlevel[++level] = wpulldown;
			continue;	
		}

		/* only cascade items at level 0 */
		dbg_assert(level > 0);
		
		n = 0;
		
		/* create and set label string if any */
		if(items[i].label){
			label_str = munge_label(items[i].label, NULL, &mnemonic);
			XtSetArg(args[n], XmNlabelString, label_str); n++;
			if(mnemonic){
				XtSetArg(args[n], XmNmnemonic, mnemonic);	n++;
			}
		}
	
		switch(items[i].type){
			case IT_PUSH:
			if(items[i].callback){
				XtSetArg(args[n], XmNactivateCallback, item_cb); n++;
			}
			break;
			case IT_TOGGLE:
			if(items[i].callback){
				XtSetArg(args[n], XmNvalueChangedCallback, item_cb); n++;
			}
			break;
			case IT_RADIO:
			XtSetArg(args[n], XmNindicatorType,XmONE_OF_MANY); n++;
			if(items[i].callback){
				XtSetArg(args[n], XmNvalueChangedCallback, item_cb); n++;
			}
			break;

			default:
			break;
		}
		
		if(!items[i].name) snprintf(tmp_name, WIDGET_NAME_MAX, "widget%d", i);

		XtCreateManagedWidget(
			items[i].name ? items[i].name : tmp_name,
			wc[items[i].type], wlevel[level], args, n);
		
		/* temporary label XmString */
		if(items[i].label) XmStringFree(label_str);
	}
}

/* 
 * Parses text string for a mnemonic and returns XmString with /tag/
 * rendition tag, if specified, and the mnemonic if found. 
 * The caller is responsible for freeing the returned XmString.
 */
static XmString munge_label(const char *text, const char *tag, KeySym *mnemonic)
{
	XmString label;
	char *token;
	char *tmp;

	tmp = strdup(text);
	token = strchr(tmp, MENU_MC_PREFIX);
	if(token){
		*mnemonic = (KeySym)token[1];
		memmove(token, token + 1, strlen(token + 1));
		token[strlen(token) - 1] = 0;
	}else if(mnemonic) {
		*mnemonic = 0;
	}
	label = XmStringGenerate(tmp, NULL, XmCHARSET_TEXT, (XmStringTag)tag);
	free(tmp);
	return label;
}

/* Returns menu item Widget with the corresponding name */
Widget get_menu_item(Widget wmenu, const char *name)
{
	Widget w = XtNameToWidget(wmenu, name);
	dbg_assert(w);
	if(!w) stderr_msg("No such menu item: %s\n", name);
	return w;
}

/* Sets menu item sensitivity */
void enable_menu_item(Widget wmenu, const char *name, Boolean enable)
{
	Widget w = XtNameToWidget(wmenu, name);
	dbg_assert(w); /* must exist if the meditating guru is asking for it */
	if(!w) {
		stderr_msg("No such menu item: %s\n", name);
		return;
	}
	XtSetSensitive(w, enable);
}

/*
 * Modifies context sensitive menu adding/updating/replacing the
 * *actions* part according to items. On the initial call, ctx must
 * have been initialized to contain valid popup shell handle, and other
 * fields set to NULL/zero; this function allocates extra data within
 * that is reused across consecutive calls.
 */
void modify_context_menu(struct ctx_menu_data *ctx,
	const struct ctx_menu_item *items, unsigned int nitems,
	unsigned int idefault)
{
	unsigned int n, i;
	Arg args[10];
	const char sz_action[] = "contextAction";
	
	/* create more button gadgets if need be */
	if(ctx->nitems < nitems) {
		char tmp_name[WIDGET_NAME_MAX];
		Widget *new_list;
		
		new_list = realloc(ctx->witems, sizeof(Widget) * nitems);
		if(!new_list) return;
		ctx->witems = new_list;

		for(i = ctx->nitems; i < nitems; i++) {
			snprintf(tmp_name, WIDGET_NAME_MAX, "%s%d\n", sz_action, i + 1);

			XtSetArg(args[0], XmNpositionIndex, i);
			ctx->witems[i] = XmCreatePushButtonGadget(
				ctx->wmenu, tmp_name, args, 1);
		}
		ctx->nitems = nitems;
	}

	/* set/update labels and callback data */
	for(i = 0; i < nitems; i++) {
		XmString label;
		KeySym mnemonic;
		XtCallbackRec item_cb[] = {
			{ (XtCallbackProc)items[i].callback, (XtPointer)items[i].cb_data},
			{ (XtCallbackProc)NULL, (XtPointer)NULL}
		};
		
		label = munge_label(items[i].label,
			(i == idefault) ? DEFAULT_ACTION_RTAG : NULL, &mnemonic);

		n = 0;
		XtSetArg(args[n], XmNactivateCallback, item_cb); n++;
		XtSetArg(args[n], XmNlabelString, label); n++;
		if(mnemonic) {
			XtSetArg(args[n], XmNmnemonic, mnemonic); n++;
		}
		XtSetValues(ctx->witems[i], args, n);
		XtManageChild(ctx->witems[i]);
		
		XmStringFree(label);
	}
	
	/* unmanage unused (left over from the loop above) items */
	for( ; i < ctx->nitems; i++)
		XtUnmanageChild(ctx->witems[i]);
	
	/* hide the separator when no actions defined */
	if(!nitems) {
		Widget w = get_menu_item(ctx->wmenu, "*actionsSeparator");
		if(w) XtUnmanageChild(w);
	} else {
		Widget w = get_menu_item(ctx->wmenu, "*actionsSeparator");
		if(w) XtManageChild(w);
	}
}

/*
 * Adds items to an existing pulldown menu
 */
void add_menu_items(Widget wmenu,
	const struct menu_item *items, unsigned int nitems)
{
	Arg args[10];
	Cardinal n;
	char tmp_name[WIDGET_NAME_MAX];
	unsigned int i;
	WidgetClass wc[] = {
		xmPushButtonGadgetClass,
		xmToggleButtonGadgetClass,
		xmToggleButtonGadgetClass,
		xmSeparatorGadgetClass
	};
		
	for(i = 0; i < nitems; i++) {
		XmString label = NULL;
		KeySym mnemonic;
		XtCallbackRec item_cb[] = {
			{ (XtCallbackProc)items[i].callback, (XtPointer)items[i].cb_data},
			{ (XtCallbackProc)NULL, (XtPointer)NULL}
		};
		
		n = 0;
		
		if(items[i].label) {
			label = munge_label(items[i].label, NULL, &mnemonic);
			XtSetArg(args[n], XmNlabelString, label); n++;
			if(mnemonic) {
				XtSetArg(args[n], XmNmnemonic, mnemonic); n++;
			}
		}

		switch(items[i].type){
			case IT_PUSH:
			if(items[i].callback){
				XtSetArg(args[n], XmNactivateCallback, item_cb); n++;
			}
			break;
			case IT_TOGGLE:
			if(items[i].callback){
				XtSetArg(args[n], XmNvalueChangedCallback, item_cb); n++;
			}
			break;
			case IT_RADIO:
			XtSetArg(args[n], XmNindicatorType, XmONE_OF_MANY); n++;
			if(items[i].callback){
				XtSetArg(args[n], XmNvalueChangedCallback, item_cb); n++;
			}
			break;

			default:
			break;
		}

		if(!items[i].name) snprintf(tmp_name, WIDGET_NAME_MAX, "widget%d", i);

		XtCreateManagedWidget( items[i].name ? items[i].name : tmp_name,
			wc[items[i].type], wmenu, args, n);
		
		if(items[i].label) XmStringFree(label);
	}
}
