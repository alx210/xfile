/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef EXEC_H
#define EXEC_H

struct env_var_rec {
	char *name;
	char *value;
};

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
int expand_env_vars(const char *in, struct env_var_rec *vars, char **out);

/*
 * Splits cmd_spec into separate arguments, cmd_spec will be modified in
 * process and pointers into it placed in argv_ret, terminated with NULL.
 * Quotation marks and escape character \ are threated same way as by sh(1).
 * Returns zero on success, errno otherwise. Memory for the argv array is
 * allocated from heap and must be freed.
 */
int split_arguments(char *cmd_spec, char ***argv_ret, size_t *argc_ret);

/*
 * Takes a command string and vfork-execvs it.
 * Returns zero on success, errno otherwise.
 */
int spawn_cs_command(const char *cmd_spec);

/*
 * Runs an executable file in a separate process with arguments specified.
 * Returns zero on success, errno otherwise.
 */
int spawn_command(const char *cmd, char * const *args, size_t nargs);

/*
 * Runs an executable file (must be FQN) in a separate process.
 * Returns zero on success, errno otherwise.
 */
int spawn_exe(const char *cmd);

#endif /* EXEC_H */
