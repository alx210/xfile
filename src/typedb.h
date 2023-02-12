/*
 * Copyright (C) 2022 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * File type database data structures, constants and prototypes
 */

#ifndef TYPEDB_H
#define TYPEDB_H

struct action_rec
{
	char *title;
	char *command;
};

struct content_pattern_rec
{
	unsigned long offset;
	unsigned char *data;
	unsigned int data_len;
};

struct file_type_rec
{
	char *name;
	char *icon_name;
	char *mime;	

	char **name_pat;
	unsigned int nname_pat;
	
	struct content_pattern_rec *content_pat;
	unsigned int ncontent_pat;
	
	struct action_rec *actions;
	unsigned int nactions;
	
	unsigned int priority;

	/* buffer sizes are in sizeof(struct) units */
	unsigned int name_pat_size;
	unsigned int content_pat_size;
	unsigned int actions_size;
};

struct file_type_db
{
	struct file_type_rec *recs;
	unsigned int size;
	unsigned int count;
	char *status_msg;
};


#define DB_UNKNOWN	(-1)
#define DB_TEXT		(-2)
#define DB_BINARY	(-3)

#define DB_DEFINED(i) ((i) >= 0)
#define DB_ISTEXT(i) ((i) == DB_TEXT)
#define DB_ISBIN(i) ((i) == DB_BINARY)

#define DB_SUCCESS	0
#define DB_SYNTAX	(-1)
#define DB_MALLOC	(-3)
#define DB_FILEIO	(-4)

/* See db_parse comment */
#define db_init(db) memset(db, 0, sizeof(struct file_type_db));

/*
 * Parses a file type data base file. If 'db' is used for the first time,
 * it must be zeroed out. Subsequent calls with same 'db' will merge data.
 *
 * Returns DB_SUCCESS if whole database was parsed successfully, or DB_SYNTAX
 * on parsing errors. In later case 'db' will contain data that was parsed
 * prior to the error.
 */
int db_parse(char *buffer, size_t buf_size,
	struct file_type_db *db, int db_priority);

/* Same as parse_db, excepts it reads data from the file specified */
int db_parse_file(const char *filename,
	struct file_type_db *db, int db_priority);

/* Frees db's fields allocated by parse_db */
void db_free(struct file_type_db *db);

/* 
 * Matches the specified path and mode against the database and returns
 * the closest record index found, or DB_UNKNOWN if no match was found.
 */
int db_match(const char *file_name, struct file_type_db *db);

/*
 * Safe type record retrieval routine.
 * Returns NULL if index is invalid.
 */
struct file_type_rec* db_get_record(struct file_type_db *db, int index);

#endif /* TYPEDB_H */
