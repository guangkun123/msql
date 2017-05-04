/*
**	debug.h	- 	definitions for the debugger
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
** ID = "debug.h,v 1.3 1994/08/19 08:02:56 bambi Exp"
*/


#define	MOD_CACHE	1
#define MOD_QUERY	2
#define	MOD_KEY		4
#define	MOD_ERR		8
#define MOD_GENERAL	16
#define MOD_TRACE	32
#define MOD_MALLOC	64
#define MOD_MMAP	128
#define MOD_ACCESS	256

#define TRACE_IN	1
#define TRACE_OUT	2

void	_msqlDebug();
void	_msqlTraced();
void	initDebug();

extern	int debugLevel;

#define	msqlDebug	if(debugLevel) _msqlDebug
#define	msqlTrace	if(debugLevel) _msqlTrace
