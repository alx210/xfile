/*
 * Copyright (C) 2023-2025 alx@fastestcode.org
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

/* Forward declarations */
static Bool escape_string(const char *string, char **result);
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

static Bool xrm_enum_cb(XrmDatabase *rdb, XrmBindingList bindings,
	XrmQuarkList quarks, XrmRepresentation *type,
	XrmValue *value, XPointer closure)
{
	static size_t nalloc = 0;
	
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
	struct file_list_selection *cur_sel =
			file_list_get_selection(app_inst.wlist);
	
	if(cur_sel->count > 1) {
		unsigned int i;
		size_t cat_len = 0;
		char **esc_names;
		
		esc_names = calloc(cur_sel->count, sizeof(char*));
		
		for(i = 0; i < cur_sel->count; i++) {
			escape_string(cur_sel->names[i], &esc_names[i]);
			cat_len += strlen(esc_names[i] ?
				esc_names[i] : cur_sel->names[i]) + 3;
		}

		files = malloc(cat_len);
		files[0] = '\0';
		
		for(i = 0; i < cur_sel->count; i++) {
			strcat(files, esc_names[i] ?
				esc_names[i] : cur_sel->names[i]);
			strcat(files, " ");
			if(esc_names[i]) free(esc_names[i]);
		}
		files[strlen(files) - 1] = '\0';
		free(esc_names);
	} else if(cur_sel->count == 1) {
		if(!escape_string(cur_sel->names[0], &files))
			files = strdup(cur_sel->names[0]);
	} else {
		files = strdup("");
	}

	if((token = strstr(tool->command, "%u"))) {
		if( (token != tool->command) ||
			( (*(token - 1) != '%' ) && (*(token - 1) != '\\' ) ) ) {
			user_param = input_string_dlg(app_inst.wshell, 
				"Command Arguments",
				"Specify additional arguments",	tool->hist, NULL,
				ISF_PRESELECT | ISF_ALLOWEMPTY);
			if(!user_param) {
				free(path);
				free(files);
				return;
			}
			if(tool->hist) free(tool->hist);
			tool->hist = user_param;
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
	rv = spawn_cs_command(exp_cmd);
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error executing tool command %s:\n"
			"%s.", tool->command, strerror(rv));	
	}
	
	free(files);
	free(path);
}

/*
 * Checks the string specified for special characters and escapes them
 * with the \ character. Puts the string in quotes if it contains blanks.
 * Returns True and the modified string allocated from the heap in result.
 */
static Bool escape_string(const char *string, char **result)
{
	char spec_chrs[] = "\"\'\\";
	char blnk_chrs[] = " \n\t";
	char *sp = (char*) string;
	char *dp;
	size_t count = 0;
	char *res;
	int quote = 0;

	while(*sp != '\0') {
		if(strchr(spec_chrs, *sp)) count++;
		if(strchr(blnk_chrs, *sp)) quote = 1;
		sp++;
	}
	if(!count && !quote) return False;
	
	res = malloc(strlen(string) + count + 3);
	if(!res) return False;
	
	sp = (char*)string;
	dp = res;
	
	if(quote) {
		*dp = '\"';
		dp++;
	}
	
	while(*sp != '\0') {
		if(strchr(spec_chrs, *sp)) {
			*dp = '\\';
			dp++;
		}
		*dp = *sp;
		sp++;
		dp++;
	}
	
	if(quote) {
		*dp = '\"';
		dp++;
	}
	
	*dp = '\0';
	*result = res;
	dbg_trace("%s\n", res);
	return True;
}
