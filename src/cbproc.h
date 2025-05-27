/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef CBPROC_H
#define CBPROC_H

/* File list view and popup menu callbacks */
void activate_cb(Widget, XtPointer, XtPointer);
void sel_change_cb(Widget, XtPointer, XtPointer);
void context_action_cb(Widget, XtPointer, XtPointer);

/* Main menu callbacks */
void make_dir_cb(Widget, XtPointer, XtPointer);
void make_file_cb(Widget, XtPointer, XtPointer);
void copy_to_cb(Widget, XtPointer, XtPointer);
void move_to_cb(Widget, XtPointer, XtPointer);
void link_to_cb(Widget, XtPointer, XtPointer);
void duplicate_cb(Widget, XtPointer, XtPointer);
void rename_cb(Widget, XtPointer, XtPointer);
void delete_cb(Widget, XtPointer, XtPointer);
void attributes_cb(Widget, XtPointer, XtPointer);
void select_all_cb(Widget, XtPointer, XtPointer);
void deselect_cb(Widget, XtPointer, XtPointer);
void invert_selection_cb(Widget, XtPointer, XtPointer);
void select_pattern_cb(Widget, XtPointer, XtPointer);
void copy_here_cb(Widget, XtPointer, XtPointer);
void move_here_cb(Widget, XtPointer, XtPointer);
void toggle_detailed_cb(Widget, XtPointer, XtPointer);
void toggle_show_all_cb(Widget, XtPointer, XtPointer);
void sort_by_name_cb(Widget, XtPointer, XtPointer);
void sort_by_size_cb(Widget, XtPointer, XtPointer);
void sort_by_time_cb(Widget, XtPointer, XtPointer);
void sort_by_type_cb(Widget, XtPointer, XtPointer);
void sort_by_suffix_cb(Widget, XtPointer, XtPointer);
void sort_ascending_cb(Widget, XtPointer, XtPointer);
void sort_descending_cb(Widget, XtPointer, XtPointer);
void set_filter_cb(Widget, XtPointer, XtPointer);
void exec_terminal_cb(Widget, XtPointer, XtPointer);
void dir_up_cb(Widget, XtPointer, XtPointer);
void reread_cb(Widget, XtPointer, XtPointer);
void path_change_cb(Widget, XtPointer, XtPointer);
void show_path_field_cb(Widget, XtPointer, XtPointer);
void show_status_field_cb(Widget, XtPointer, XtPointer);
void new_window_cb(Widget, XtPointer, XtPointer);
void dup_window_cb(Widget, XtPointer, XtPointer);
void about_cb(Widget, XtPointer, XtPointer);
void dbinfo_cb(Widget, XtPointer, XtPointer);
void sub_shell_destroy_cb(Widget, XtPointer, XtPointer);

#endif /* CBPROC_H */
