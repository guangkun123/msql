/*
**	debug.c	- Shared debug output routines
**
**
** Copyright (c) 1993  David J. Hughes
**
** Permission to use, copy, and distribute for non-commercial purposes,
** is hereby granted without fee, providing that the above copyright
** notice appear in all copies and that both the copyright notice and this
** permission notice appear in supporting documentation.
**
** This software is provided "as is" without any expressed or implied warranty.
**
*/

#ifndef lint
static char RCSid[] = 
	"debug.c,v 1.3 1994/08/19 08:02:54 bambi Exp";
#endif 


#include <stdio.h>
#include <stdlib.h>
#include <varargs.h>
#include "debug.h"

#include <common/portability.h>

int 		debugLevel=0;
extern char	PROGNAME[];
int		titleFlag = 0;


void initDebug()
{
	char	*env,
		*tmp,
		*tok;

	env = getenv("MINERVA_DEBUG");
	if(env)
	{
		tmp = (char *)strdup(env);
	}
	else
		return;
	printf("\n-------------------------------------------------------\n");
	printf("MINERVA_DEBUG found.  %s started with the following :-\n\n",
		PROGNAME);
	tok = (char *)strtok(tmp,":");
	while(tok)
	{
		if (strcmp(tok,"cache") == 0)
		{
			debugLevel |= MOD_CACHE;
			printf("Debug level : cache\n");
		}
		if (strcmp(tok,"query") == 0)
		{
			debugLevel |= MOD_QUERY;
			printf("Debug level : query\n");
		}
		if (strcmp(tok,"general") == 0)
		{
			debugLevel |= MOD_GENERAL;
			printf("Debug level : general\n");
		}
		if (strcmp(tok,"error") == 0)
		{
			debugLevel |= MOD_ERR;
			printf("Debug level : error\n");
		}
		if (strcmp(tok,"key") == 0)
		{
			debugLevel |= MOD_KEY;
			printf("Debug level : key\n");
		}
		if (strcmp(tok,"malloc") == 0)
		{
			debugLevel |= MOD_MALLOC;
			printf("Debug level : malloc\n");
		}
		if (strcmp(tok,"trace") == 0)
		{
			debugLevel |= MOD_TRACE;
			printf("Debug level : trace\n");
		}
		if (strcmp(tok,"mmap") == 0)
		{
			debugLevel |= MOD_MMAP;
			printf("Debug level : mmap\n");
		}
		if (strcmp(tok,"access") == 0)
		{
			debugLevel |= MOD_ACCESS;
			printf("Debug level : access\n");
		}
		if (strcmp(tok,"proctitle") == 0)
		{
			titleFlag=1;
			printf("Debug level : proctitle\n");
		}
		tok = (char *)strtok(NULL,":");
	}
	(void)free(tmp);
	printf("\n-------------------------------------------------------\n\n");
}


void _msqlDebug(va_alist)
	va_dcl
{
		va_list args;
	char	msg[1024],
		*fmt;
	int	module,
		out = 0;

	va_start(args);
	module = (int) va_arg(args, int *);
	if (! (module & debugLevel))
	{
		va_end(args);
		return;
	}

	fmt = (char *)va_arg(args, char *);
	if (!fmt)
        	return;
	(void)vsprintf(msg,fmt,args);
	va_end(args);
	printf("[%s] %s",PROGNAME,msg);
	fflush(stdout);
}


int debugSet(module)
	int	module;
{
	if (! (module & debugLevel))
                return(0);
 	return(1);
}



_msqlTrace(va_alist)
	va_dcl
{
	va_list args;
	char	msg[1024],
		*fmt,
		*tag;
	int	loop,
		dir;
	static	int indent = 0;
	static 	char inTag[] = "-->",
		     outTag[] = "<--";

	va_start(args);
	if (! (debugLevel & MOD_TRACE))
	{
		va_end(args);
		return;
	}

	dir = va_arg(args, int);
	if (dir == TRACE_IN)
	{
		tag = inTag;
		indent++;
	}
	else
		tag = outTag;
	fmt = (char *)va_arg(args, char *);
	if (!fmt)
        	return;
	(void)vsprintf(msg,fmt,args);
	va_end(args);
	printf("[%s] ",PROGNAME);
	for (loop = 1; loop <indent; loop++)
		printf("  ");
	printf("%s %s\n",tag,msg);
	fflush(stdout);
	if (dir == TRACE_OUT)
		indent--;
}

