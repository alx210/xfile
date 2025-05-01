/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef FSTAB_H
#define FSTAB_H

/* Checks if path is a mount point in fstab */
int is_in_fstab(const char *path);

/* Checks if any fstab mount points are sub-directories in path */
int has_fstab_entries(const char *path);

/* Returns device, file system and options for given mount point in fstab */
int get_mount_info(const char *path, char **dev, char **fs, char **opt);

#endif /* FSTAB_H */
