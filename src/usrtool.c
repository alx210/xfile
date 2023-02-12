/*
 * Copyright (C) 2023 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <stdio.h>
#include <Xm/Xm.h>
#include "main.h"
#include "comdlgs.h"
#include "const.h"
#include "exec.h"
#include "fsutil.h"
#include "usrtool.h"
#include "debug.h"
#include "memdb.h" /* must be the last header */

/* Forward declarations */
static Bool xrm_enum_cb(XrmDatabase *rdb, XrmBindingList bindings,
	XrmQuarkList quarks, XrmRepresentation *type,
	XrmValue *value, XPointer closure);

#define TOOLS_GROWBY 10
#define TOOLS_RES_NAME APP_NAME ".tools"
#define TOOLS_RES_CLASS APP_CLASS ".Tools"

static struct user_tool_rec *tools = NULL;
static unsigned int ntools = 0;

static XrmQuark name_list[3] = {NULLQUARK};
static XrmQuark class_list[3] = {NULLQUARK};
static XrmQuark qtitle_name;
static XrmQuark qtitle_class;

static Bool xrm_enum_cb(XrmDatabase *rdb, XrmBindingList bindings,
	XrmQuarkList quarks, XrmRepresentation *type,
	XrmValue *value, XPointer closure)
{
	static size_t nalloc = 0;
	XrmRepresentation title_rep;
	XrmValue title_value;
	XrmQuark title_name_list[] = {
		name_list[0], name_list[1], NULLQUARK, qtitle_name, NULLQUARK
	};
	XrmQuark title_class_list[] = {
		class_list[0], class_list[1], NULLQUARK, qtitle_class, NULLQUARK
	};
	
	if( ((quarks[0] == name_list[0]) || (quarks[0] == class_list[0])) &&
		((quarks[1] == name_list[1]) || (quarks[1] == class_list[1])) ) {

		if(ntools == nalloc) {
			struct user_tool_rec *p;

			nalloc += TOOLS_GROWBY;
			p = realloc(tools, nalloc * sizeof(struct user_tool_rec));
			if(!p) return True;
			tools = p;
		}
		
		memset(&tools[ntools], 0, sizeof(struct user_tool_rec));
		tools[ntools].name = XrmQuarkToString(quarks[2]);
		tools[ntools].command = strdup(value->addr);
		tools[ntools].title = tools[ntools].name;
		
		title_name_list[2] = quarks[2];
		title_class_list[2] = quarks[2];
		
		if(XrmQGetResource(XtDatabase(app_inst.display), title_name_list,
			title_class_list, &title_rep, &title_value)) {
			tools[ntools].title = strdup(title_value.addr);
		}
		
		ntools++;
	}
	return False;
}

unsigned int get_user_tools(struct user_tool_rec **p)
{
	XrmDatabase rdb = XtDatabase(app_inst.display);

	dbg_assert(tools == NULL);

	XrmStringToQuarkList(TOOLS_RES_NAME, name_list);
	XrmStringToQuarkList(TOOLS_RES_CLASS, class_list);

	qtitle_name = XrmStringToQuark("title");
	qtitle_class = XrmStringToQuark("Title");
	
	XrmEnumerateDatabase(rdb, name_list, class_list,
		XrmEnumOneLevel, xrm_enum_cb, NULL);
	
	if(ntools) *p = tools;
	return ntools;
}

void user_tool_cbproc(Widget w, XtPointer closure, XtPointer data)
{
	int rv;
	struct user_tool_rec *tool = (struct user_tool_rec*) closure;
	struct env_var_rec vars[4] = { NULL }; /* last record must be NULLed */
	char *exp_cmd;
	char *files = NULL;
	char *path = get_working_dir();
	char *token;
	char *user_param = NULL;
	unsigned int n = 0;
	
	if(app_inst.cur_sel.count > 1) {
		unsigned int i;
		size_t cat_len = 0;
		char **esc_names;
		
		esc_names = calloc(app_inst.cur_sel.count, sizeof(char*));
		
		for(i = 0; i < app_inst.cur_sel.count; i++) {
			escape_string(app_inst.cur_sel.names[i], &esc_names[i]);
			cat_len += strlen(esc_names[i] ?
				esc_names[i] : app_inst.cur_sel.names[i]) + 3;
		}

		files = malloc(cat_len);
		files[0] = '\0';
		
		for(i = 0; i < app_inst.cur_sel.count; i++) {
			strcat(files, esc_names[i] ?
				esc_names[i] : app_inst.cur_sel.names[i]);
			strcat(files, " ");
			if(esc_names[i]) free(esc_names[i]);
		}
		files[strlen(files) - 1] = '\0';
		free(esc_names);
	} else if(app_inst.cur_sel.count == 1) {
		files = strdup(app_inst.cur_sel.names[0]);
	} else {
		files = strdup("");
	}

	if((token = strstr(tool->command, "%u"))) {
		if( (token != tool->command) ||
			( (*(token - 1) != '%' ) && (*(token - 1) != '\\' ) ) ) {
			user_param = input_string_dlg(app_inst.wshell,
				"Specify additional arguments",	tool->hist, ISF_PRESELECT);
			if(tool->hist) free(tool->hist);
			tool->hist = user_param;
			/* prevent "undefined variable" warning from expand_env_vars */
			if(!user_param) user_param = "";
		}
	}
	
	n = 0;
	vars[n].name = "n";
	vars[n].value = files;
	n++;

	vars[n].name = "p";
	vars[n].value = path;
	n++;
	
	if(user_param) {
		vars[n].name = "u";
		vars[n].value = user_param;
		n++;
	}
	
	rv = expand_env_vars(tool->command, vars, &exp_cmd);
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error parsing action command string:\n"
			"%s\n%s.", tool->command, strerror(rv));
		free(files);
		free(path);
		return;
	}
	dbg_trace("TOOL(%s): [%s]\n", tool->name, exp_cmd);
	rv = spawn_command(exp_cmd);
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error executing tool command %s:\n"
			"%s.", tool->command, strerror(rv));	
	}
	
	free(files);
	free(path);
}

