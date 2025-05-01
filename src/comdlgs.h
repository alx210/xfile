/*
 * Copyright (C) 2012-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef COMDLGS_H
#define COMDLGS_H

/* 
 * message_box types
 */
enum mb_type {
	MB_QUESTION,	/* Yes/No */
	MB_CQUESTION,	/* Yes/No/Cancel */
	MB_CONFIRM,		/* Ok/Cancel */
	MB_NOTIFY,
	MB_WARN,
	MB_ERROR,
	MB_NOTIFY_NB,	/* non blocking */
	MB_WARN_NB,
	MB_ERROR_NB
};

enum mb_result {
	MBR_CONFIRM,
	MBR_DECLINE,
	MBR_CANCEL, /* Cancel in MB_CQUESTION */
	_MBR_NVALUES
};

/*
 * Displays a modal message dialog. MB_QUESTION and MB_CONFIRM return True
 * when YES or OK was clicked.
 */
enum mb_result message_box(Widget parent, enum mb_type type,
	const char *msg_title, const char *msg_str);

/*
 * Same as message_box but takes printf format string and arguments
 */
enum mb_result va_message_box(Widget parent, enum mb_type type,
	const char *msg_title, const char *msg_fmt, ...);

/*
 * Displays a blocking directory selection dialog.
 * Returns a valid path name or NULL if selection was cancelled.
 * If a valid path name is returned it must be freed by the caller.
 */
char* dir_select_dlg(Widget parent, const char *title,
	const char *init_path, const char *context);

/*
 * Displays a blocking input dialog.
 * Returns a valid string or NULL if cancelled. If a non NULL pointer
 * is returned it must be freed by the caller.
 */
char* input_string_dlg(Widget parent, const char *title,
	const char *msg_str, const char *init_str, const char *context, int flags);

/* Displays attribute editor dialog for files */
void attrib_dlg(Widget wp, char *const *files, unsigned int nfiles);

/* input_string_dlg flags */
#define ISF_PRESELECT 0x01	/* preselect the text (title for file names) */
#define ISF_FILENAME 0x02	/* text is a file name */
#define ISF_NOSLASH 0x04	/* disallow the / character */
#define ISF_ALLOWEMPTY 0x08	/* returns zero length string if left empty */

#endif /* COMDLGS_H */
