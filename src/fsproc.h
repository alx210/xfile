/*
 * Copyright (C) 2019-2025 alx@fastestcode.org
 * This software is distributed under the terms of the MIT/X license.
 * See the included COPYING file for further information.
 */

#ifndef FSPROC_H 
#define FSPROC_H

/* The wd parameter specifies the working directory for the forked
 * work process, and may be NULL, in which case it's inherited. 
 * Names in srcs may contain path components, but must be always
 * relative to wd.
 */

/* Recursively copies files and directories from srcs[] to dest */
int copy_files(const char *wd, char* const *srcs,
	size_t num_srcs, const char *dest);

/* Duplicates files/directories specified in srcs */
int dup_files(const char *wd, char * const *srcs, size_t num_srcs);

/* Recursively moves files and directories in srcs to dest directory */
int move_files(const char *wd, char* const *srcs,
	size_t num_srcs, const char *dest);

/* Recursively deletes files and directories in srcs */
int delete_files(const char *wd,
	char* const *srcs, size_t num_srcs);

/* Recursively sets attributes for files and directories in srcs.
 * att_flags (see ATT constants below) specifies which attributes to set */
int set_attributes(const char *wd, char* const *srcs,
	size_t num_srcs, gid_t gid, uid_t uid, mode_t file_mode,
	mode_t file_mode_mask, mode_t dir_mode, mode_t dir_mode_mask,
	int att_flags);

#define ATT_OWNER	0x001 /* gid/uid are specified */
#define ATT_FMODE	0x002 /* file_mode is specified */
#define ATT_DMODE	0x004 /* directory mode is specified */
#define ATT_RECUR	0x008 /* recurse into directories */

/* SIGCHLD handler for background process PIDs
 * Returns True if pid was an fsproc process */
Boolean fs_proc_sigchld(pid_t pid, int status);

#endif /* FSPROC_H */
