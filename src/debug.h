/*
 * Copyright (C) 2022-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */
 
/*
 * This file is a part of the frmwrk library.
 * Debugging helpers
 */

#ifndef DEBUG_H
#define DEBUG_H 

#ifdef DEBUG

#include <stdio.h>

void _dbg_trace(int, const char*, int, const char*, ...);

#define dbg_trace(fmt,...) _dbg_trace(0,__FILE__,__LINE__,fmt,##__VA_ARGS__)
#define dbg_trap(fmt,...) _dbg_trace(1,__FILE__,__LINE__,fmt,##__VA_ARGS__)
#define dbg_assert(exp)((exp)?(void)0:\
	_dbg_trace(1,__FILE__,__LINE__,"Assertion failed. Exp: %s\n",#exp));
#define dbg_printf(f, ...) fprintf(stderr, f,##__VA_ARGS__)

#else /* NON DEBUG */

#define dbg_trace(...) ((void)0)
#define dbg_trap(...) ((void)0)
#define dbg_printf(...) ((void)0)
#define dbg_assert(exp) ((void)0)

#endif /* DEBUG */

#endif /* DEBUG_H */
