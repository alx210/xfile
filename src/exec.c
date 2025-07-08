/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <X11/Intrinsic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include "main.h"
#include "const.h"
#include "exec.h"
#include "debug.h"

static inline int is_space(char c);
static char* get_var_value(const struct env_var_rec*, const char*);

/*
 * Expands environment variables found in 'in' and returns the expanded string
 * in 'out', which is allocated from the heap and must be freed by the caller.
 *
 * The vars array may contain variable definitions for words starting
 * with the % character, and must be terminated with a NULLed out record.
 * Any % prefixed words not found in vars will be looked up in X resources
 * APP_CLASS.ENV_RES_NAME. Any $ prefixed words will be expanded using getenv().
 *
 * Returns zero on success, errno otherwise.
 */
int expand_env_vars(const char *in, struct env_var_rec *vars, char **out)
{
	char *src;
	char *buf;
	char **parts;
	size_t nparts = 0;
	char *s, *p;
	int res = 0;
	
	src = strdup(in);
	if(!src) return ENOMEM;

	s = p = src;
	
	/* count parts and allocate temporary storage */
	while(*p) {
		if((*p == '$' && p[1] != '$') ||
			(*p == '%' && p[1] != '%')) nparts++;
		p++;
	}
	
	if(!nparts) {
		*out = src;
		return 0;
	}
	
	parts = calloc((nparts + 1) * 2, sizeof(char*));
	if(!parts) {
		free(src);
		return ENOMEM;
	}
	
	/* reset for parsing */
	nparts = 0;
	s = p = src;
	
	while(*p) {
		if(*p == '$' || *p == '%') {
			char vc = *p;
			
			/* double special char stands for literal */
			if(p[1] == vc) {
				memmove(p, p + 1, strlen(p));
				p++;
				continue;
			}
			
			/* starting point of the next part */
			parts[nparts++] = s;
			
			/* explicit scope ${...} */
			if(p[1] == '{') {
				p[0] = '\0';
				p += 2;
				s = p;
				
				while(*p && *p != '}') p++;

				if(*p == '\0') {
					res = EINVAL;
					break;
				}
				
				if(!(p - s)) break;
				
				buf = malloc((p - s) + 1);
				if(!buf) {
					res = errno;
					break;
				}
				memcpy(buf, s, p - s);
				buf[p - s] = '\0';
				
				s--;
				memmove(s, p + 1, strlen(p));
				
				p = s;
			} else { 
				/* implicit scope $...
				 * (eventually terminated by a space, dot, quote, or slash) */
				p[0] = '\0';
				p++;
				s = p;
				
				while(*p && *p != ' ' && *p != '\"' && *p != '\'' &&
					*p != '\t' && *p != '.' && *p != '\\' && *p != '/') p++;
				
				if(!(p - s)) {
					res = EINVAL;
					break;
				}
				
				buf = malloc((p - s) + 1);
				if(!buf) {
					res = errno;
					break;
				}
				memcpy(buf, s, p - s);
				buf[p - s] = '\0';

				memmove(s , p, strlen(p) + 1);
				
				p = s;
			}
			
			if(vc == '$') {
				parts[nparts++] = getenv(buf);
			} else { /* was % */
				parts[nparts++] = get_var_value(vars, buf);
			}
			free(buf);
		}
		p++;
	}
	
	if(!res) {
		size_t i, len = 1;
		
		/* add trailing part (if string didn't end with a variable) */
		if(p != s) parts[nparts++] = s;
		p = src;

		for(i = 0; i < nparts; i++) {
			if(parts[i] && *parts[i]) len += strlen(parts[i]);
		}
		
		/* compose parts into a single string */
		buf = malloc(len);
		if(buf) {
			buf[0] = '\0';

			for(i = 0; i < nparts; i++) {
				if(parts[i] && *parts[i]) strcat(buf, parts[i]);
			}
			*out = buf;
		} else {
			res = ENOMEM;
		}
	}
	
	free(parts);
	free(src);
	return res;
}

static char* get_var_value(const struct env_var_rec *v, const char *name)
{
	XrmDatabase rdb = XtDatabase(app_inst.display);
	char *sz_name;
	char *type;
	XrmValue value = { 0, NULL };
	size_t len;
	
	/* try context dependent variables first... */
	while(v && v->name) {
		if(!strcmp(v->name, name)) return v->value;
		v++;
	}
	
	/* ...then user variables in X resources */
	len = strlen(APP_CLASS) + strlen(ENV_RES_NAME) + strlen(name) + 3;
	sz_name = malloc(len);
	snprintf(sz_name, len, "%s.%s.%s", APP_CLASS, ENV_RES_NAME, name);
	if(!XrmGetResource(rdb, sz_name, sz_name, &type, &value)) {
		stderr_msg("WARNING! Undefined user variable: %s\n", name);
	}
	free(sz_name);

	return value.addr;
}

static inline int is_space(char c)
{
	char spc[] = " \t\n"; /* anything we consider a space character */
	return (strchr(spc, c)) ? 1 : 0;
}

/*
 * Checks the string specified for special characters and escapes them
 * with the \ character. Returns with zero and a new string allocated
 * from the heap in result, ENOENT if no special characters found,
 * ENOMEM on error.
 */
int escape_string(const char *string, char **result)
{
	char esc_chrs[] = "\"\' \n\t\\";
	char *sp = (char*) string;
	char *dp;
	size_t count = 0;
	char *res;	

	while(*sp != '\0') {
		if(strchr(esc_chrs, *sp)) count++;
		sp++;
	}
	if(!count) return ENOENT;
	
	res = malloc(strlen(string) + count + 1);
	if(!res) return ENOMEM;
	
	sp = (char*)string;
	dp = res;
	
	while(*sp != '\0') {
		if(strchr(esc_chrs, *sp)) {
			*dp = '\\';
			dp++;
		}
		*dp = *sp;
		sp++;
		dp++;
	}
	*dp = '\0';
	*result = res;
	
	dbg_trace("%s: %s --> %s\n", __FUNCTION__, string, res);

	return 0;
}

/*
 * Does the opposite of escape_string, modifying the string specified
 */
void unescape_string(char *str)
{
	char *p = str;
	
	while(*p) {
		if(*p == '\\')
			memmove(p, p + 1, strlen(p));

		p++;
	}
}

/*
 * Splits cmd_spec into separate arguments, cmd_spec will be modified in
 * process and pointers into it placed in argv, terminated with NULL.
 * Quotation marks and escape character \ are threated same way as by sh(1).
 * Returns zero on success, errno otherwise. Memory for the argv array is
 * allocated from heap and must be freed.
 */
int split_arguments(char *cmd_spec, char ***argv_ret, size_t *argc_ret)
{
	char *p, *t;
	char pc = 0;
	int done = 0;
	char **argv = NULL;
	size_t argv_size = 0;
	unsigned int argc = 0;
	
	p = cmd_spec;
	t = NULL;

	while(!done){
		if(!t){
			/* save the starting point */
			while(*p && is_space(*p)) p++;
			if(*p == '\0') break;
			t = p;
		}
		
		if(*p == '\\' && pc == '\\') {
			/* remove escaped \ and reset state */
			memmove(p - 1, p, strlen(p) + 1);
			pc = 0;
		} 
		
		if(*p == '\"' || *p == '\''){
			if(pc == '\\'){
				/* literal " or ' */
				pc = *p;
				memmove(p - 1, p, strlen(p) + 1);
			} else {
				/* quotation marks; remove them, ignoring blanks within */
				memmove(p, p + 1, strlen(p));

				do_quotes:				
				while(*p != '\"' && *p != '\''){
					if(*p == '\0'){
						if(argv) free(argv);
						stderr_msg("ERROR! "
							"Missing closing quotation mark in: %s\n",
							cmd_spec);
						return EINVAL;
					}
					pc = *p;
					p++;

					if(*p == '\\' && pc == '\\') {
						memmove(p - 1, p, strlen(p) + 1);
						pc = 0;
					} 
				}
				/* escaped " or '  within quotes */
				if(pc == '\\') {
					memmove(p - 1, p, strlen(p) + 1);
					pc = *p;
					p++;
					goto do_quotes;
				}
				/* closing */
				memmove(p, p + 1, strlen(p));
			}
		}
		/* finish token if unescaped blank or zero */
		if((is_space(*p) && pc != '\\') || *p == '\0'){
			if(*p == '\0') done = 1;
			if(argv_size < argc + 1){
				char **new_ptr;
				new_ptr = realloc(argv, (argv_size += 32) * sizeof(char*));
				if(!new_ptr){
					if(argv) free(argv);
					return ENOMEM;
				}
				argv = new_ptr;
			}
			*p = '\0';
			argv[argc] = t;
			
			dbg_trace("argv[%d]: %s\n",argc,argv[argc]);
			
			t = NULL;
			argc++;
		} else if(pc == '\\' && is_space(*p)) {
			/* escaped blank; remove the \ */
			memmove(p - 1, p, strlen(p) + 1);
		}
		
		pc = *p;
		p++;
	}
	
	if(!argc) return EINVAL;
	
	argv[argc] = NULL;
	*argv_ret = argv;
	if(argc_ret) *argc_ret = argc;

	return 0;
}

/*
 * Splits cmd_spec into separate arguments (see split_arguments)
 * and vfork-execvs it. Returns zero on success, errno otherwise.
 */
int spawn_cs_command(const char *cmd_spec)
{
	pid_t pid;
	char *str;
	char **argv;
	volatile int errval = 0;
	
	
	str = strdup(cmd_spec);
	if(!str) return errno;
	
	if( (errval = split_arguments(str, &argv, NULL)) )
		return errval;
	
	pid = vfork();
	if(pid == 0){
		if( (setsid() == -1) || (execvp(argv[0], argv) == -1) )
			errval = errno;
		_exit(0);
	}else if(pid == -1){
		errval = errno;
	}
	
	free(str);
	free(argv);

	return errval;
}

/*
 * Runs an executable file in a separate process with arguments specified.
 * Returns zero on success, errno otherwise.
 */
int spawn_command(const char *cmd, char * const *args, size_t nargs)
{
	pid_t pid;
	volatile int errval = 0;
	char **argv;
	size_t i = 0;
	
	argv = malloc( sizeof(char*) * (nargs + 2) );
	if(!argv) return errno;
	
	for(i = 0; i < nargs; i++) {
		argv[i + 1] = args[i];
	}	
	argv[0] = (char*)cmd;
	argv[nargs + 1] = NULL;
	
	pid = vfork();
	if(pid == 0) {
		setsid();
		
		if(execvp(cmd, argv) == (-1))
			errval = errno;
		_exit(0);
	} else if(pid == -1) {
		errval = errno;
	}

	free(argv);
	return errval;
}

/*
 * Runs an executable file (must be FQN) in a separate process.
 * Returns zero on success, errno otherwise.
 */
int spawn_exe(const char *cmd)
{
	pid_t pid;
	volatile int errval = 0;
	
	pid = vfork();
	if(pid == 0){
		setsid();
		
		if(execl(cmd, cmd, NULL) == (-1))
			errval = errno;
		_exit(0);
	}else if(pid == -1){
		errval = errno;
	}
	return errval;
}
