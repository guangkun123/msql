/*
** A small test program to see if we can do shared read/write mapped
** regions.
**
**						bambi
*/

#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>

#define	PATH	"/tmp/MmAp-TeSt"

main()
{
	int	fd,
		res;
	static   char	text[] = "Test Data";
	caddr_t	cp;

	fd = open(PATH,O_CREAT|O_RDWR|O_TRUNC);
	if (fd < 0)
	{
		fprintf(stderr,"mmap test : couldn't create tmp file!\n\n");
		exit(1);
	}
	write(fd,text,strlen(text));
	cp = mmap(NULL,strlen(text), PROT_READ|PROT_WRITE, MAP_SHARED, fd,0);
	if (cp == (caddr_t) -1)
	{
		res = 1;
	}
	else
	{
		res=0;
		munmap(cp,strlen(text));
	}
	close(fd);
	unlink(PATH);
	exit(res);
}
