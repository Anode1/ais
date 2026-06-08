/**
 * Copyright (C) 2001 Vasili Gavrilov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _COMMON_H
#define _COMMON_H

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
/* #include <sys/uio.h> */  /* we do not use locks here */
#include <sys/types.h>
#include <sys/file.h>


#define BOOL	int
#define TRUE	1
#define FALSE	0

extern int debug;
extern int trace;

void rtrim(char * string, char trim);
void ltrim(char * string, char trim);
char *strdup(const char *str); /* duplicate string - when not 2001 POSIX (XSI) */
unsigned int mysnprintf(char *buf, size_t size, const char *fmt, ...); /* if snprintf is not availbale */


/**********************
 * Logging stuff below
 **********************/
int LOG(char* fmt, ...);

/* variadic macros working only with C99 compilers: */
#define PRINT(mesg, ...) fprintf(stdout, mesg, __VA_ARGS__)

#ifdef DEBUG
#define DPRINT(mesg, ...) fprintf(stdout, mesg, __VA_ARGS__)
#else
#define DPRINT(mesg, ...)
#endif

#define OUT_OF_MEM() { \
			fprintf(stderr, "Out of memory: %s:%d", __FILE__, __LINE__); \
			exit(-1); \
		}

#define FATAL(X){ \
		fprintf(stderr, X); \
		exit(-1); \
	}

#endif /* __COMMON_H__ */
