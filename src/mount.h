/*
 * Copyright (C) 2022 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef MOUNT_H
#define MOUNT_H

int exec_mount(const char *mpt_path);

int exec_umount(const char *mpt_path);

/* SIGCHLD handler for mount commands
 * Returns True if pid was a mount command */
Boolean mount_proc_sigchld(pid_t, int);

#endif /* MOUNT_H */
