/*
 * Copyright (C) 2023-2026 alx@fastestcode.org
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
		
		if(!strlen(value->addr)) {
			char *name = XrmQuarkToString(quarks[2]);
			stderr_msg("Invalid command string for tool \"%s\".\n", name);
			return False;
		}
		
		if(ntools == nalloc) {
			struct user_tool_rec *p;

			nalloc += TOOLS_GROWBY;
			p = realloc(tools, nalloc * sizeof(struct user_tool_rec));
			if(!p) return True;
			tools = p;
		}
		
		memset(&tools[ntools], 0, sizeof(struct user_tool_rec));
		tools[ntools].name = strdup(XrmQuarkToString(quarks[2]));
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
	struct user_tool_rec *tool = (struct user_tool_rec*) closure;
	struct env_var_rec vars[4] = { NULL }; /* last record must be NULLed */
	char *exp_cmd;
	char *path = get_working_dir();
	char *token;
	char *user_param = NULL;
	char *exp_names = NULL;
	char **argv;
	size_t argc;
	int rv, i, n;
	struct file_list_selection *cur_sel =
			file_list_get_selection(app_inst.wlist);
	
	if((token = strstr(tool->command, "%u"))) {
		if( (token != tool->command) ||
			(( token[-1] != '%' ) && (token[-1] != '\\' ) ) ) {
			user_param = input_string_dlg(app_inst.wshell, 
				"Command Arguments",
				"Specify additional arguments",	tool->hist, NULL,
				ISF_PRESELECT | ISF_ALLOWEMPTY);
			if(!user_param) {
				free(path);
				return;
			}
			if(tool->hist) free(tool->hist);
			tool->hist = user_param;
		}
	}
	
	n = 0;
	vars[n].name = ENV_FNAME;
	vars[n].value = "%n";
	n++;
	
	vars[n].name = ENV_FPATH;
	vars[n].value = path;
	n++;
	
	if(user_param) {
		vars[n].name = ENV_UPARM;
		vars[n].value = user_param;
		n++;
	}
	
	rv = expand_env_vars(tool->command, vars, &exp_cmd);
	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error parsing command string:\n"
			"%s\n%s.", tool->command, strerror(rv));
		free(path);
		return;
	}
	
	rv = split_arguments(exp_cmd, &argv, &argc);
	if(rv) {
		free(exp_cmd);
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Error parsing command string:\n"
			"%s\n%s.", tool->command, strerror(rv));
		free(path);
		return;
	}
	
	/* if we've re-substituted %n earlier, find it so we can use the
	 * arg's string as a template for pasting file names */
	for(n = (-1), i = 0; i < argc; i++) {
		if( (token = strstr(argv[i], "%n")) ) {
			token[1] = 's';
			n = i;
			break;
		}
	}

	/* if we've got file names, we want to allocate a new argument list,
	 * and splice in file name arguments generated using the template string */
	if((n >= 0) && cur_sel->count) {
		char *tmpl = argv[n];
		char **new_argv;
		int new_argc = argc + cur_sel->count - 1;
		size_t len;

		/* allocate a block of memory to hold all the pasted and zero
		 * terminated file name strings in a contiguous chunk, which will
		 * be later mapped into the new argument list */
		for(len = 0, i = 0; i < cur_sel->count; i++)
			len += strlen(tmpl) + strlen(cur_sel->names[i]) + 1;
		
		exp_names = malloc(len);
		new_argv = malloc(sizeof(char*) * new_argc);
		if(!exp_names || !new_argv) {
			if(exp_names) free(exp_names);
			if(new_argv) free(new_argv);
			free(exp_cmd);
			free(argv);
			free(path);
			va_message_box(app_inst.wshell,
				MB_ERROR, APP_TITLE, "Memory allocation error");
			return;
		}
		
		/* copy original argv contents, leaving a gap for the
		 * list of file names in the middle */
		if(n) memcpy(new_argv, argv, sizeof(char*) * n);
		if((argc - n) > 1) {
			memcpy(new_argv + n + cur_sel->count,
				argv + n + 1, sizeof(char*) * (argc - n - 1));
		}

		/* generate new file names using the template, and map
		 * them into the argument list */
		for(len = 0, i = 0; i < cur_sel->count; i++) {
			sprintf(exp_names + len, tmpl, cur_sel->names[i]);
			new_argv[n + i] = exp_names + len;
			len += strlen(tmpl) + strlen(cur_sel->names[i]) + 1;
		}

		free(argv);
		argv = new_argv;
		argc = new_argc;
	} else if(n >= 0) {
		argv[n] = "";
	}
	
	if(argc && strlen(argv[0])) {
		if(argc > 1)
			rv = spawn_command(argv[0], argv + 1, argc - 1);
		else
			rv = spawn_command(argv[0], NULL, 0);

		if(rv) {
			va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
				"Error executing tool command %s:\n"
				"%s.", tool->command, strerror(rv));	
		}
	} else {
		/* can happen if it's just envvars and context variables */
		message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Command specified resolved to an empty string.");
	}

	if(exp_names) free(exp_names);
	free(exp_cmd);
	free(argv);
	free(path);
}
