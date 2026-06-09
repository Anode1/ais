/**
 * Copyright (C) 2001 Vasili Gavrilov <vgavrilov AAAATTTTT users.sourceforge.net>
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

#include "common.h"


void rtrim(char * string, char trim){
	int i;
	for(i = strlen(string)-1; (i >= 0) && (strchr (string + i, trim)!=NULL); i--)
		string[i] = '\0';
}

/* not efficient - please rewrite me! */
void ltrim(char * string, char trim){
	while((*string != '\0') && (strchr (string, trim)!=NULL))
		memmove(string, string+1, strlen(string));
}


/* Safe version of sprintf */
unsigned int mysnprintf(char *buf, size_t size, const char *fmt, ...){

	 unsigned int n;
	 va_list  ap;
	 va_start(ap, fmt);
	 vsprintf(buf, fmt, ap);
	 n = strlen(buf);
	 va_end(ap);
	 if (n >= size){
		 printf("snprintf: '%s' overflowed array", fmt);
	 }
	 return(n);
}

/* duplicate string - when not 2001 POSIX (XSI) */
char *strdup(const char *str){

	size_t len;
	char *copy;

	if(!str) /* NULL passed? return NULL as well */
		return NULL;

	len = strlen(str) + 1;
	copy = (char*)malloc(len);
	if(copy == NULL)
		OUT_OF_MEM();

	strcpy(copy, str);

	return copy;
}

/* print debug message */
int LOG(char *fmt, ...){

	char* logfile = ".log";
	int fd;
	char buf[1024];
	va_list ap;

	errno=0;
	fd = open(logfile, O_WRONLY|O_APPEND|O_CREAT, 0664);
	if(fd<0){
		fprintf(stderr, "Can't open log file %s: %s\n", logfile, strerror(errno));
		return -1;
	}

	strcpy(buf, "[DEBUG] ");
	write(fd, buf, strlen(buf));

	va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
	va_end(ap);

    write(fd, buf, strlen(buf));
    write(fd, "\n", 1);

	close(fd);

	return 0;
}

