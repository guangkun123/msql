/*
**	portability.h	- 
**
**
** Copyright (c) 1993-95  David J. Hughes
** Copyright (c) 1995  Hughes Technologies Pty Ltd
**
** Permission to use, copy, and distribute for non-commercial purposes,
** is hereby granted without fee, providing that the above copyright
** notice appear in all copies and that both the copyright notice and this
** permission notice appear in supporting documentation.
**
** This software is provided "as is" without any expressed or implied warranty.
**
*/


#ifndef PORTABILITY_H
#define PORTABILITY_H 

#include <common/config.h>

#ifndef HAVE_BCOPY
#  undef 	bzero
#  undef 	bcopy
#  undef 	bcmp
#  define	bzero(a,l)	memset((void *)a,0,(size_t)l)
#  define	bcopy(s,d,l)	memcpy(d,s,(size_t)l)
#  define	bcmp		memcmp
#endif

#ifndef HAVE_RINDEX
#  undef	index
#  undef	rindex
#  define	index		strchr
#  define	rindex		strrchr
#endif

#ifndef HAVE_RANDOM
#  undef	random
#  undef	srandom
#  define	random		srand
#  define	srandom		srand
#endif

#ifdef HAVE_SELECT_H
	/*
	** AIX has a struct fd_set and can be distinguished by
	** its needing <select.h>
	*/
	typedef struct fd_set fd_set
#endif

#ifndef HAVE_U_INT
	typedef	unsigned int u_int;
#endif

#ifndef HAVE_FTRUNCATE
	/*
	** SCO ODT doesn't have ftruncate() !!! Have to use old Xenix stuff
	*/
#       undef	ftruncate
#  	define 	ftruncate	chsize
#endif


#endif /* PORTABILTIY_H */
