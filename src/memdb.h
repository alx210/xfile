/*
 * This file is a part of the frmwrk library.
 * Memory allocation tracker/debugger.
 * This must be the last header included in a module.
 */

#ifndef FRMWRK_MEMDB_H
#define FRMWRK_MEMDB_H

#if defined(DEBUG) && defined(MEMDB)

#ifdef __cplusplus
extern "C"{
#endif

void* memdb_malloc(const char*,int,unsigned int);
void* memdb_calloc(const char*,int,unsigned int, unsigned int);
void* memdb_realloc(const char*,int,void*,unsigned int);
void memdb_free(const char*,int,void*);
char *memdb_strdup(const char*, int, const char*);
char *memdb_realpath(const char*, int, const char*, char*);

/* Prints memory allocation statistics. Module name may be NULL,
 * in which case global statistics are printed. If verbose is 0
 * only grand total is printed.  */
void memdb_stat(const char *module, int verbose);

#ifdef __cplusplus
}
#endif

#define malloc(n) memdb_malloc(__FILE__,__LINE__,n)
#define calloc(n,c) memdb_calloc(__FILE__,__LINE__,n,c)
#define realloc(p,n) memdb_realloc(__FILE__,__LINE__,p,n)
#define free(p) memdb_free(__FILE__,__LINE__,p)
#define strdup(p) memdb_strdup(__FILE__,__LINE__,p)
#define realpath(s,p) memdb_realpath(__FILE__,__LINE__,s,p)

#define memdb_lstat(v) memdb_stat(__FILE__,v)
#define memdb_gstat(v) memdb_stat(NULL,v)

#else
#define memdb_lstat(v) ((void)0)
#define memdb_gstat(v) ((void)0)
#endif  /* DEBUG && MEMDB */

#endif /* FRMWRK_MEMDB_H */
