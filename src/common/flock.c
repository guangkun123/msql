/*
**	flock.c		- Implementation of flock using SysV fcntl's
**
**	Author	: 	David J. Hughes   ( Bambi@Bond.edu.au )
**	Date	:	16-Feb-1994
**	Org	: 	Information Technology Services
**			Bond University
**
**	This code is placed in the public domain and can be used for any
**	purposes.  It is supplied "as is" without any implied warranty.
**	etc. etc. etc.
*/


#include <stdio.h>
#include <fcntl.h>

#include "config.h"
#include "portability.h"

#ifndef HAVE_FLOCK

int flock(fd,op)
	int	fd,
		op;
{
	int	res;
	flock_t	arg;

	if (LOCK_EX & op)
		arg.l_type = F_WRLCK;
	else if (LOCK_SH & op)
		arg.l_type = F_RDLCK;
	else if (LOCK_UN & op)
		arg.l_type = F_UNLCK;

	arg.l_whence = 0;
	arg.l_start = 0;
	arg.l_len = 0;
	
	if (op & LOCK_NB)
		res = fcntl(fd,F_SETLK, &arg);
	else
		res = fcntl(fd,F_SETLKW, &arg);

	if (res == -1)
		return(-1);
	else
		return(0);
}

#endif
