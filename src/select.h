/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/* Returns True if this instance owns the selection */
Boolean is_selection_owner(void);

/* Tries and grabs primary X selection, returns True on success */
void grab_selection(void);

/* Ungrabs primary selection */
void ungrab_selection(void);

/* Gets file list from primary selection (if any), and initiates
 * copy/move to the current working directory */
void paste_selection(Boolean move);
