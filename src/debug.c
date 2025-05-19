/*
 * Copyright (C) 2023-2025 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * This file is a part of the frmwrk library.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>

void _dbg_trace(int trap, const char *file,
	int line, const char *fmt, ...)
{
	FILE *out;
	#ifdef DBG_TO_STDERR
	out=stderr;
	#else
	out=stdout;
	#endif /* DBG_TO_STDERR */
	va_list vl;
	va_start(vl,fmt);
	fprintf(out,"%s (%u): ",file,line);	
	vfprintf(out,fmt,vl);
	va_end(vl);
	fflush(out);
	if(trap) raise(SIGTRAP);
}
