/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef CONST_H
#define CONST_H

#ifndef PREFIX
#define PREFIX "/usr"
#endif

/* X resource class, name and window title */
#define APP_CLASS "XFile"
#define APP_NAME "xfile"
#define APP_TITLE "XFile"

/* Configuration files/directories */
#define HOME_SUBDIR ".xfile"
#define SHARE_SUBDIR "share/xfile"
#define DB_SUBDIR "types"
#define PM_SUBDIR "icons"
#define DB_SUFFIX ".db"
#define HIS_SUFFIX ".his"

/* Alternate invocation to just open files from db */
#define XFILE_OPEN "xfile-open"

/* Default update polling interval */
#define DEF_REFRESH_INT 4

/* Default history limit */
#define DEF_HISTORY_MAX 8

/* Default time format string (RFC 2822 like) */
#define DEF_TIME_FMT "%d %b %Y %H:%M"

/* Subresource for user environment variables */
#define ENV_RES_NAME "variable"

/* Type database priorities */
#define SYS_DB_PR 0
#define USR_DB_PR 1

/* Type database built-in variables */
#define ENV_FNAME "n"
#define ENV_FPATH "p"
#define ENV_FMIME "m"

/* Default command string to exec on text files */
#define ENV_DEF_TEXT_CMD "%textEditor %n"

/* Removal confirmation resource strings and constants */
#define CS_CONFIRM_ALWAYS "always"
#define CS_CONFIRM_MULTI "multiple"
#define CONFIRM_ALWAYS 1
#define CONFIRM_MULTI 2

/* Icons size resource strings */
#define CS_ICON_AUTO	"auto"
#define CS_ICON_TINY	"tiny"
#define CS_ICON_SMALL	"small"
#define CS_ICON_MEDIUM	"medium"
#define CS_ICON_LARGE	"large"

/* Default duplicate suffix */
#define DEF_DUP_SUFFIX ".copy"

/* Fallback X resources */
extern const char *xfile_ad[]; /* in fallback.c */

#endif /* CONST_H */
