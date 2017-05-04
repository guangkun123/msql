/*
**	strlib.c	- "Dynamic" versions of string routines
**
**
** Copyright (c) 1993-95  David J. Hughes
** Copyright (c) 1995 Hughes Technologies Pty Ltd
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
	"strlib.c,v 1.3 1994/08/19 08:02:59 bambi Exp";
#endif 


#include <stdio.h>
#include <varargs.h>
#include <common/portability.h>


#define	STR_LEN 4096
 
char	strBuf[STR_LEN];	/* 4K buffer used by all d* routines */



#ifndef HAVE_STRDUP
char	*strdup(s)
	char	*s;
{
	char	*s1;

	if (!s)
		return(NULL);
	s1 = (char *)malloc(strlen(s) + 1);
	if (!s1)
		return(NULL);
	
	strcpy(s1,s);
	return(s1);
}
#endif






/****************************************************************************
** 	_dsprintf 
**
**	Purpose	: A sprintf that handles dynamic allocation (sort of)
**	Args	: varargs (like printf)
**	Returns	: char * to newly allocated string
**	Notes	: This is to handle creation of queries that could be
**		  just about any length.  Allocating one large block
**		  here makes more sense (and stuffs up the heap less
**		  than using large local arrays in each proc call.
*/


char *dsprintf(va_alist)
	va_dcl
{
	va_list args;
	int	len;
	char	*fmt,
		*tmp;

	va_start(args);
	fmt = va_arg(args, char *);
	(void) bzero(strBuf,STR_LEN);
	(void) vsprintf(strBuf,fmt,args);
	len = strlen(strBuf);
	tmp = (char *)strdup(strBuf);
	if (!tmp)
	{
		fprintf(stderr,"\ndsprintf() Out of memory\n\n");
		exit(1);
	}
	return(tmp);
}







/****************************************************************************
** 	_dcsprintf
**
**	Purpose	: A combination dynamic sprintf and strcat
**	Args	: Original string
**		: printf styled vararg list
**	Returns	: pointer to new string
**	Notes	: old string is freed
*/

char *dcsprintf(va_alist)
	va_dcl
{
	va_list	args;
	char	*s1,
		*tmp,
		*fmt;

	va_start(args);
	s1 = va_arg(args, char *);
	fmt = va_arg(args, char *);
	(void)bzero(strBuf,STR_LEN);
	if (s1)
	{
		(void)strcpy(strBuf,s1);
		(void)free(s1);
	}
	(void) vsprintf(strBuf + strlen(strBuf),fmt,args);
	tmp = (char *) strdup(strBuf);
	if (!tmp)
	{
		fprintf(stderr,"\ndcsprintf() Out of memory\n\n" );
		exit(1);
	}
	return(tmp);
}


char *dstrcat(s1,s2)
	char	*s1,
		*s2;
{
	char	*tmp;

	(void)bzero(strBuf,STR_LEN);
	if (s1)
		strcpy(strBuf,s1);
	strcat(strBuf,s2);
	tmp = (char *)strdup(strBuf);
	return(tmp);
}






/****************************************************************************
** 	_Mstrtok
**
**	Purpose	: An strtok(3) that doesn't match spans of tok-sep's
**	Args	: 
**	Returns	: 
**	Notes	: This is required so that a string like "foo::" will
**		  return "" as the second token using Mstrtok(NULL,":");
*/


char	*strtokBuf = NULL;

char *Mstrtok(str,sep)
	char	*str,
		*sep;
{
	static char	*cp,
			eos;
	char	*eot,
		*tok;

	if (str)
	{
		if (strtokBuf)
			(void)free(strtokBuf);
		strtokBuf = (char *)strdup(str);
		cp = strtokBuf;
		eos=0;
	}

	if (*cp == 0)
	{
		/*
		** Force an end of string condition
		*/
		return(NULL);
	}

	eot = (char *)strpbrk(cp,sep);
	if (eot)
	{
		tok = cp;
		cp = eot + 1;
		*eot = 0;
		return(tok);
	}
	else
	{
		if (eos)
			return(NULL);
		eos = 1;
		return(cp);
	}
}


char *Mmalloc(length)
	int	length;
{
	char	*buf;

	buf = (char *)malloc(length);
	if (!buf)
	{
		fprintf(stderr,"\nMmalloc : Out of memory!  Core dumped.\n\n");
		abort();
		exit(1);  /* just in case SIGIOT is caught */
	}
	(void)bzero(buf,length);
	return(buf);
}

char *Mfree(ptr)
	char	*ptr;
{
	if (!ptr)
	{
		fprintf(stderr,"\nMfree : Free of NULL pointer!  Core dumped.\n\n");
		abort();
		exit(1);  /* just in case SIGIOT is caught */
	}
	(void)free(ptr);
	return(NULL);
}
