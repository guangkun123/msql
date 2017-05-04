/*
**	Test program to determine whether this box has struct dirent or direct
*/

#include <sys/types.h>

#if defined(HAVE_SYS_DIR_H)
#  include <sys/dir.h>
#endif

#if defined(HAVE_DIRENT_H)
#  include <dirent.h>
#endif


main()
{
#ifdef HAVE_DIRENT
	struct	dirent *dirp;
	DIR	*TestForNeXT;
#endif

#ifdef HAVE_DIRECT
	struct	direct *dirp;
#endif

	int	foo;

	foo = 0;
	exit(0);
}
