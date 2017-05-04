/*
**	site.h	- local configuration parameters
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
** ID = "site.h,v 1.3 1994/08/19 08:02:57 bambi Exp"
*/



/**************************************************************************
***************************************************************************
**
**		config details
**
***************************************************************************
**************************************************************************/


/*
** TCP port for the MSQL daemon
*/

#ifdef ROOT_EXEC
#define MSQL_PORT	1112
#else
#define MSQL_PORT	4333
#endif

	
/*
** UNIX Domain socket for MSQL daemon
*/

#ifdef ROOT_EXEC
#define MSQL_UNIX_ADDR	"/dev/msql"
#else
#define MSQL_UNIX_ADDR	"/tmp/msql.sock"
#endif


/*
** Max length for a path name
*/

#ifndef MAX_PATH_LEN
#  define MAX_PATH_LEN	160
#endif

#include "common/config.h"
