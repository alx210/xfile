/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * File type database parser
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include "typedb.h"
#include "debug.h"

#define TYPES_GROW_BY	64
#define FIELD_GROW_BY	6
#define MAX_PARSE_ERROR	255
#define PROBE_NCHRS	256 /* must be power of 2 */

/* Parser state */
enum rec_id {
	RID_NONE,
	RID_TYPE,
	RID_ICON,
	RID_MIME,
	RID_NAME_PAT,
	RID_CONTENT_PAT,
	RID_EXEC
};

struct parser_state {
	char *buffer;
	char *buf_ptr;
	size_t buf_size;
	unsigned int iline;
	char parse_error[MAX_PARSE_ERROR+1];
	enum rec_id cur_rec;
	struct file_type_rec *recs;
	int recs_size;
	int nrecs;
	int db_priority;
};

/* Keywords */
static const char KW_ICON[]="icon";
static const char KW_MIME[]="mime";
static const char KW_NAME_PAT[]="match_name";
static const char KW_CONT_PAT[]="match_content";
static const char KW_EXEC[]="action";

/* Local prototypes */
static char* get_line(struct parser_state*);
static void chop_blanks(char*);
static char* skip_blanks(char*);
static char* get_tail(char*);
static int parse_action(char *str, struct action_rec*);
static int parse_content_pat(char*,struct content_pattern_rec*);
static void set_parse_error(struct parser_state*, const char*);
static int parse_buffer(struct parser_state*);
static int parse_add_field(struct file_type_rec*,enum rec_id,char*);
static int add_type_rec(struct parser_state*,struct file_type_rec*);
static void free_rec(struct file_type_rec*, unsigned long);
static int find_type_rec(const struct parser_state*,const char*);
static int match_name_pat(const char*, struct file_type_rec*);
static int match_content_pat(const char*, struct file_type_rec*);
static int probe_contents(const char *fname);

/* 
 * Gets next line from parser buffer skipping comments and empty lines. 
 */
static char* get_line(struct parser_state *ps)
{
	char *p, *cur;
	
	do {
		cur = ps->buf_ptr;
		
		if(*ps->buf_ptr == '\0') return NULL;

		p = strchr(ps->buf_ptr, '\n');

		if(p){
			ps->buf_ptr = p + 1;
			*p = '\0';
		}else{
			while(*ps->buf_ptr != '\0') ps->buf_ptr++;
		}

		cur = skip_blanks(cur);
		ps->iline++;
	}while(*cur == '\0' || *cur == '#');
	
	return cur;
}

/* Zeroify right hand side blanks */
static void chop_blanks(char *p)
{
	char *pb = p;
	
	while(p[1] != '\0') p++;
	while(isspace(*p)){
		*p = '\0';
		if(p == pb) break;
		p--;
	}
}

/* Skip left hand side blanks */
static char* skip_blanks(char *p)
{
	while(isspace(*p)) p++;
	return p;
}

/* Get a pointer to the last non-whitespace character */
static char* get_tail(char *sz)
{
	char *p = sz;
	while(p[1] != '\0') p++;
	while(isspace(*p) && p != sz) p--;
	return p;
}

/*
 * Parses textual config data and constructs file type records.
 * Returns either DB_SUCCESS or DB_SYNTAX and sets the parse error string.
 */
static int parse_buffer(struct parser_state *ps)
{
	struct file_type_rec tmp;
	char *line, *p;
	int status = DB_SUCCESS;
	
	memset(&tmp, 0, sizeof(struct file_type_rec));
	ps->iline = 0;
	
	while((line = get_line(ps)) && status == DB_SUCCESS){

		if(line[0] == '}'){
			if(ps->cur_rec == RID_TYPE){
				status = add_type_rec(ps,&tmp);
				if(status) break;
				memset(&tmp,0,sizeof(struct file_type_rec));
				ps->cur_rec = RID_NONE;
				continue;
			}else if(ps->cur_rec > RID_TYPE){
				ps->cur_rec = RID_TYPE;
				continue;
			}else{
				set_parse_error(ps,"} out of scope");
				status = DB_SYNTAX;
				break;			
			}
		}else if(line[0] == '{'){
			set_parse_error(ps,"{ out of scope");
			status = DB_SYNTAX;
			break;				
		}

		if(ps->cur_rec == RID_NONE){
			p = get_tail(line);
			if(*p == '{'){
				*p = '\0';
				chop_blanks(line);
			}else if(*get_line(ps) != '{'){
				set_parse_error(ps,"{ expected");
				status = DB_SYNTAX;
				break;
			}
			tmp.name = strdup(line);
			ps->cur_rec = RID_TYPE;

		}else if(ps->cur_rec == RID_TYPE){
			enum rec_id rec;

			if(!strncmp(KW_ICON,line,strlen(KW_ICON))){
				rec = RID_ICON;
			}else if(!strncmp(KW_MIME, line, strlen(KW_MIME))){
				rec = RID_MIME;
			}else if(!strncmp(KW_NAME_PAT, line, strlen(KW_NAME_PAT))){
				rec = RID_NAME_PAT;
			}else if(!strncmp(KW_CONT_PAT, line, strlen(KW_CONT_PAT))){
				rec = RID_CONTENT_PAT;
			}else if(!strncmp(KW_EXEC, line, strlen(KW_EXEC))){
				rec = RID_EXEC;
			}else{
				set_parse_error(ps,"Record field type expected");
				status = DB_SYNTAX;
				break;
			}				
			
			p = line;
			
			if(rec == RID_ICON || rec == RID_MIME){
				/* inline only */
				while(*p && !isspace(*p)) p++;
				p = skip_blanks(p);
				if(*p == '\0'){
					set_parse_error(ps, "Value expected");
					status = DB_SYNTAX;
					break;
				}
				status = parse_add_field(&tmp, rec, p);
				if(status == DB_SYNTAX) set_parse_error(ps, "Syntax error");	
			}else{
				/* inline/multi line values */
				while(*p && !isspace(*p) && *p != '{') p++;
				p = skip_blanks(p);
				if(*p == '{'){
					ps->cur_rec = rec;
					continue;
				}else if(*p == '\0'){
					if(*get_line(ps) == '{'){
						ps->cur_rec = rec;
						continue;
					}else{
						set_parse_error(ps,"{ or record value expected");
						status = DB_SYNTAX;
						break;
					}
				}else{
					status = parse_add_field(&tmp, rec, p);
					if(status == DB_SYNTAX) set_parse_error(ps, "Syntax error");	
				}
			}
		}else{
			status = parse_add_field(&tmp,ps->cur_rec, line);
			if(status == DB_SYNTAX) set_parse_error(ps, "Syntax error");	
		}
	}

	if(!status && ps->cur_rec != RID_NONE){
		set_parse_error(ps, "Unexpected end of file");
		status = DB_SYNTAX;
	}
	
	if(status != DB_SUCCESS) free_rec(&tmp, 1);
	
	return status;
}

/*
 * Parses action (<&title>: <command>) string.
 * Allocates memory for <name> and <command> fields from the heap.
 */
static int parse_action(char *str, struct action_rec *rec)
{
	char *p = str;
	char *command = NULL;
	
	while(*p){
		if(*p == '\\' && (p[1] == '\\' || p[1] == ':')){
			memmove(p, p + 1, strlen(p + 1) + 1);
		} else if(*p == ':'){
			command = skip_blanks(p + 1);
			*p = '\0';
		}
		p++;
	}
	if(!command) return DB_SYNTAX;
	
	rec->command = strdup(command);
	rec->title = strdup(str);
	
	if(!rec->command || !rec->title){
		if(rec->command) free(rec->command);
		if(rec->title) free(rec->title);
		return DB_MALLOC;
	}
	return DB_SUCCESS;
}

/*
 * Parses content pattern (<offset> <data>) string.
 * Allocates storage for <data> from the heap.
 */
static int parse_content_pat(char *str, struct content_pattern_rec *rec)
{
	unsigned char *pdata;
	char *p = str;
	size_t len, i;
	
	while(!isspace(*p) && *p) p++;
	if(*p == '\0') return DB_SYNTAX;

	*p = '\0';	p++;
	p = skip_blanks(p);

	pdata = calloc(strlen(p) + 1, sizeof(char));
	if(!pdata) return DB_MALLOC;

	rec->offset = strtol(str, NULL, 10);

	for(i = 0, len = 0; p[i]; i++){
		if(p[i] == '\\'){
			char bs[3];
			
			if(p[i + 1] == '\\'){
				pdata[len++] = '\\';
				continue;
			}
			
			bs[0] = p[++i];
			bs[1] = p[++i];
			bs[2] = '\0';
			
			if( !isalnum(bs[0]) || !isalnum(bs[1]) ){
				free(pdata);
				return DB_SYNTAX;
			}
			
			pdata[len++] = (unsigned char) strtol(bs, NULL, 16);
		} else {
			pdata[len++] = p[i];
		}
	}
	
	rec->data = pdata;
	rec->data_len = len;

	return DB_SUCCESS;
}

static void set_parse_error(struct parser_state *ps, const char *text)
{
	snprintf(ps->parse_error,
		MAX_PARSE_ERROR,"Line %d: %s",ps->iline,text);
}

/*
 * This routine allocates storage space from the heap used for temporary file
 * type record construction. This memory is reused when a completed record is
 * added to the record list.
 */
static int parse_add_field(struct file_type_rec *p, enum rec_id rid, char *data)
{
	int rv;
	
	switch(rid){
		case RID_ICON:
		if(p->icon_name) free(p->icon_name);
		p->icon_name = strdup(data);
		if(!p->icon_name) return DB_MALLOC;
		break;

		case RID_MIME:
		if(p->mime) free(p->mime);
		p->mime = strdup(data);
		if(!p->mime) return DB_MALLOC;
		break;
		
		case RID_EXEC:
		if((p->nactions) + 1 >= p->actions_size){
			struct action_rec *np;
			np = realloc(p->actions,
				sizeof(struct action_rec)*
				(p->actions_size + FIELD_GROW_BY));
			if(!np) return DB_MALLOC;
			p->actions_size += FIELD_GROW_BY;
			p->actions = np;
		}
		memset(&p->actions[p->nactions], 0, sizeof(struct action_rec));
		rv = parse_action(data, &p->actions[p->nactions]);
		if(rv) return rv;
		p->nactions++;
		break;
		
		case RID_CONTENT_PAT:
		if((p->ncontent_pat + 1) >= p->content_pat_size){
			struct content_pattern_rec *np;
			np = realloc(p->content_pat,
				sizeof(struct content_pattern_rec)*
				(p->content_pat_size + FIELD_GROW_BY));
			if(!np) return DB_MALLOC;
			p->content_pat = np;
			p->content_pat_size += FIELD_GROW_BY;
		}
		memset(&p->content_pat[p->ncontent_pat], 0,
			sizeof(struct content_pattern_rec));
		rv = parse_content_pat(data, &p->content_pat[p->ncontent_pat]);
		if(rv) return rv;
		p->ncontent_pat++;
		break;
		
		case RID_NAME_PAT:

		if((p->nname_pat + 1) >= p->name_pat_size){
			char **np;
			np = realloc(p->name_pat,
				sizeof(char*) * (p->name_pat_size + FIELD_GROW_BY));
			if(!np) return DB_MALLOC;
			p->name_pat = np;
			p->name_pat_size += FIELD_GROW_BY;
		}
		p->name_pat[p->nname_pat] = strdup(data);
		if(!p->name_pat[p->nname_pat]) return DB_MALLOC;
		p->nname_pat++;
		break;
		
		default: dbg_trap("invalid record id %d\n",rid);
	}
	return DB_SUCCESS;
}

/*
 * Searches for named record in ps->recs and returns its index
 * if found, -1 otherwise.
 */
static int find_type_rec(
	const struct parser_state *ps, const char *name)
{
	int i;
	
	for(i=0; i < ps->nrecs; i++){
		if(!strcmp(ps->recs[i].name,name)) return i;
	}
	return -1;
}

/*
 * Adds a new type record using data from 'tmp'.
 * If a record with such name exists it is replaced.
 */
static int add_type_rec(struct parser_state *ps, struct file_type_rec *tmp)
{
	int irec;
	
	if(!tmp->nname_pat && !tmp->ncontent_pat){
		set_parse_error(ps,"Record contains no patterns");
		return DB_SYNTAX;
	}
	
	tmp->priority = ps->db_priority;
	
	irec = find_type_rec(ps,tmp->name);
	
	if(irec == (-1)){
		if(ps->nrecs + 1 >= ps->recs_size){
			struct file_type_rec *nr;

			nr=realloc(ps->recs, sizeof(struct file_type_rec)*
				(ps->recs_size + TYPES_GROW_BY));
			if(!nr) return DB_MALLOC;
			ps->recs = nr;
			ps->recs_size += TYPES_GROW_BY;
		}
		irec = ps->nrecs;
		ps->nrecs++;
	}else{
		free_rec(&ps->recs[irec], 1);
	}
	ps->recs[irec] = *tmp;
	
	return DB_SUCCESS;
}

/*
 * Free memory held by a file_type_rec structure.
 */
static void free_rec(struct file_type_rec *rec, unsigned long nrec)
{
	unsigned long i;

	for(i = 0; i < nrec; i++){
		int j;
	
		if(rec[i].name) free(rec[i].name);
		if(rec[i].icon_name) free(rec[i].icon_name);
		if(rec[i].mime) free(rec[i].mime);
		
		if(rec[i].name_pat){
			for(j=0; j < rec[i].nname_pat; j++){
				free(rec[i].name_pat[j]);
			}
			free(rec[i].name_pat);
		}

		if(rec[i].content_pat){		
			for(j=0; j< rec[i].ncontent_pat; j++){
				if(rec[i].content_pat[j].data)
					free(rec[i].content_pat[j].data);
			}
			free(rec[i].content_pat);
		}
		
		if(rec[i].actions){
			for(j=0; j< rec[i].nactions; j++){
				if(rec[i].actions[j].title)
					free(rec[i].actions[j].title);
				if(rec[i].actions[j].command)
					free(rec[i].actions[j].command);
			}
			free(rec[i].actions);
		}
	}
}

/*
 * Parses a file type data base file. If 'db' is used for the first time,
 * it must be zeroed out. Subsequent calls with same 'db' will merge data.
 * Returns DB_SUCCESS if whole database was parsed successfully, or DB_SYNTAX
 * on parsing errors. In later case 'db' will contain data that was parsed
 * prior to the error.
 */
int db_parse(char *buffer, size_t buf_size,
	struct file_type_db *db, int db_priority)
{
	struct parser_state ps;
	int err;
	
	memset(&ps, 0, sizeof(struct parser_state));
	ps.buffer = (char*)buffer;
	
	ps.buf_size = buf_size;
	ps.buf_ptr = ps.buffer;
	
	ps.recs = db->recs;
	ps.nrecs = db->count;
	ps.recs_size = db->size;
	
	err = parse_buffer(&ps);

	db->recs = ps.recs;
	db->count = ps.nrecs;
	db->size = ps.recs_size;
	if(db->status_msg) free(db->status_msg);
	db->status_msg = (ps.parse_error[0]) ? strdup(ps.parse_error) : NULL;

	return err;
}

int db_parse_file(const char *filename,
	struct file_type_db *db, int db_priority)
{
	FILE *file;
	struct stat st;
	int err;
	char *buffer;

	if(stat(filename, &st) < 0) {
		db->status_msg = strdup(strerror(errno));
		return DB_FILEIO;
	}
	if(st.st_size == 0) return 0;
	
	if((buffer = malloc(st.st_size + 1)) == NULL) {
		err = DB_MALLOC;
		db->status_msg = strdup(strerror(errno));
		return err;
	}
	
	file = fopen(filename,"r");
	if(!file){
		db->status_msg = strdup(strerror(errno));
		err = DB_FILEIO;
		free(buffer);
		return err;
	}

	if(fread(buffer, 1, st.st_size, file) < st.st_size){
		db->status_msg = strdup(strerror(errno));
		err = DB_FILEIO;
		free(buffer);
		fclose(file);
		return err;
	}
	fclose(file);
	buffer[st.st_size]='\0';

	err = db_parse(buffer, st.st_size, db, db_priority);
	free(buffer);
	
	return err;
}

/*
 * Frees internal fields of 'db'
 */
void db_free(struct file_type_db *db)
{
	if(db->recs){
		free_rec(db->recs, db->count);
		free(db->recs);
	}
	if(db->status_msg) free(db->status_msg);
	memset(db, 0, sizeof(struct file_type_db));
}

/*
 * Matches db rec's name patterns against file name specified.
 * Returns non zero if match is found.
 */
static int match_name_pat(const char *file_name, struct file_type_rec *rec)
{
	unsigned int i;
	char *name;
	
	if(!rec->nname_pat) return 0;
	
	name = strrchr(file_name, '/');
	if(!name) name = (char*)file_name;
	
	for(i = 0; i < rec->nname_pat; i++) {
		if(!fnmatch(rec->name_pat[i], name, 0))
			return strlen(rec->name_pat[i]);
	}
	return 0;
}

/*
 * Matches db rec's content patterns against specified file's contents.
 * Returns non zero if match is found.
 */
static int match_content_pat(const char *file_name, struct file_type_rec *rec)
{
	FILE *file;
	size_t max_len = 0;
	void *buffer;
	unsigned int i;
	int match = 0;
	
	if(!rec->ncontent_pat ||
		!(file = fopen(file_name, "r"))) return 0;
	
	for(i = 0; i < rec->ncontent_pat; i++) {
		if(rec->content_pat[i].data_len > max_len)
			max_len = rec->content_pat[i].data_len;
	}
	
	buffer = malloc(max_len);
	if(!buffer) {
		fclose(file);
		return 0;
	}
	
	for(i = 0; i < rec->ncontent_pat; i++) {
		size_t rb;
		
		if(fseek(file, rec->content_pat[i].offset, SEEK_SET)) continue;
		
		rb = fread(buffer, 1, rec->content_pat[i].data_len, file);
		if(rb < rec->content_pat[i].data_len) continue;
		
		if(!memcmp(rec->content_pat[i].data, buffer,
			rec->content_pat[i].data_len)) match++;
	}
	
	free(buffer);	
	fclose(file);

	return match;
}

/* 
 * Matches the specified path and mode against the database and returns
 * a record index, or DB_(UNKNOWN|TEXT|BINARY) if no match was found.
 */
int db_match(const char *name, struct file_type_db *db)
{
	int i, n, p;
	int c = DB_UNKNOWN;
	int nlast = 0;
	int plast = 0;
	
	for(i = 0, n = 0, p = 0; i < db->count; i++, n = 0) {
		n += match_name_pat(name, &db->recs[i]);
		n += match_content_pat(name, &db->recs[i]);
		p = db->recs[i].priority;

		if(n > nlast || (n && p >= plast)) {
			plast = p;
			nlast = n;
			c = i;
		}
	}

	if(c == DB_UNKNOWN) c = probe_contents(name);

	return c;
}

struct file_type_rec* db_get_record(struct file_type_db *db, int index)
{
	if(index < 0 || index > db->count) return NULL;
	return &db->recs[index];
}

/*
 * For files that are not matched by type DB we'd like at least to know
 * whether these are text or binary. This here patented ass-u-me algorithm
 * gets it right most of the time.
 */
static int probe_contents(const char *fname)
{
	FILE *fin;
	size_t n, nc;
	static char buf[PROBE_NCHRS];
	int res = DB_TEXT;
	
	fin = fopen(fname, "r");
	if(!fin) return DB_UNKNOWN;
	
	nc = fread(buf, 1, PROBE_NCHRS, fin);
	fclose(fin);
	if(nc == 0) {
		return DB_TEXT;
	} else if(nc < 4) {
		for(n = 0; n < nc; n++) {
			if(!isprint(buf[n]) && !isspace(buf[n]))
				return DB_BINARY;
		}
		return DB_TEXT;				
	}
	
	if(nc % 4) nc -= (nc % 4);
	
	mblen(NULL, 0);
		
	for(n = 0; n < nc; ) {
		int cl = mblen(buf + n, nc - n);
		if(cl < 1) {
			res = DB_BINARY;
			break;
		} else {
			n += cl;
		}
	}
	return res;
}
