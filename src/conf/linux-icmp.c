/*
**	Test program to determine whether we're using the
**	dodgy ICMP structs that Linux has.  (POSIX must have
**	missed that past of the programming interface, hey! :-)
*/

#include <common/portability.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifdef HAVE_NETINET_IN_SYSTM_H
#  include <netinet/in_systm.h>
#endif
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

main()
{
	struct	icmphdr	ic;

	ic.checksum = 0;
}
