/*
 * Copyright (C) 2022-2024 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef FILEMGR_H
#define FILEMGR_H

/* One time/startup file manager intialization routine */
int initialize(void);

/* Reads directory specified; if absolute is False path is appended
 * to the current location. Returns 0 on success, errno otherwise. */
int set_location(const char *path, Boolean absolute);

/* Rereads current directory */
int reread(void);

/* Sets status line to current directory stats */
void set_default_status_text(void);

/* Sets status line to current selection stats */
void set_sel_status_text(void);

/* Updates file manager context menus */
void update_context_menus(const struct ctx_menu_item *items,
	int nitems, int idefault);

/*
 * Async routine, called by the GUI process SIGCHLD handler.
 * Checks whether pid is a reader/watcher proc and sets up a
 * deferred GUI routine that reports an error if necessary.
 * Returns False if pid is not a reader/watcher process.
 */
Boolean read_proc_sigchld(pid_t pid, int status);

/* Force the watcher process to check for changes */
void force_update(void);

/* Kills the directory reader/watcher process if one exists */
void stop_read_proc(void);

/* user flags for file list items */
#define FLI_MNTPOINT	0x01
#define FLI_MOUNTED		0x02

#endif /* FILEMGR_H */
