/*
**	net.c	- 
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
** ID = "$Id:"
**
*/


#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>

#include <common/portability.h>
#include "msql_priv.h"


static 	u_char	packetBuf[PKT_LEN + 4];
static	int	readTimeout;
u_char	*packet = NULL;


intToBuf(cp,val)
        u_char  *cp;
        int     val;
{
        *cp++ = (unsigned int)(val & 0x000000ff);
        *cp++ = (unsigned int)(val & 0x0000ff00) >> 8;
        *cp++ = (unsigned int)(val & 0x00ff0000) >> 16;
        *cp++ = (unsigned int)(val & 0xff000000) >> 24;
}


bufToInt(cp)
        u_char  *cp;
{
        int val;
 
        val = 0;
        val = *cp++;
        val += ((int) *cp++) << 8 ;
        val += ((int) *cp++) << 16;
        val += ((int) *cp++) << 24;
        return(val);
}


initNet()
{
	packet = (u_char *)packetBuf + 4;
}



int writePkt(fd)
	int	fd;
{
	u_char	*cp;
	int	len,
		offset,
		remain,
		numBytes;

	len = strlen(packet);
	intToBuf(packetBuf,len);
	offset = 0;
	remain = len+4;
	while(remain > 0)
	{
		numBytes = write(fd,packetBuf + offset, remain);
		if (numBytes == -1)
		{
			return(-1);
		}
		offset += numBytes;
		remain -= numBytes;
	}
	return(0);
}


RETSIGTYPE alarmHandler(sig)
	int	sig;
{
	signal(sig,alarmHandler);
	readTimeout = 1;
}


int readPkt(fd)
	int	fd;
{
	u_char	*c,
		buf[4];
	int	len,
		remain,
		offset,
		numBytes;
	static	int init = 1;

#ifdef MSQL_SERVER
	if (init)
	{
		signal(SIGALRM,alarmHandler);
		init = 0;
	}
	alarm(10);
#endif
	readTimeout = 0;
	remain = 4;
	offset = 0;
	while(remain > 0)
	{
		numBytes = read(fd,buf + offset,remain);
		if(numBytes <= 0)
		{
			alarm(0);
         		return(-1);
		}
		remain -= numBytes;
		offset += numBytes;
		
	}
#ifdef MSQL_SERVER
	if (readTimeout)
	{
		alarm(0);
		return(-1);
	}
#endif
	len = bufToInt(buf);
	if (len > PKT_LEN)
	{
		fprintf(stderr,"Packet too large (%d)\n", len);
		alarm(0);
		return(-1);
	}
	remain = len;
	offset = 0;
	while(remain > 0)
	{
		numBytes = read(fd,packet+offset,remain);
		if (readTimeout)
		{
			alarm(0);
			return(-1);
		}
		if (numBytes <= 0)
		{
			alarm(0);
         		return(-1);
		}
		remain -= numBytes;
		offset += numBytes;
	}
	*(packet+offset) = 0;
#ifdef MSQL_SERVER
	alarm(0);
#endif
        return(len);
}



/***********************************************************************
 * 
 * This section of code contains machine-specific code for handling integers.
 * Msql supports 32 bit 2's complement integers.  To hide the details of
 * converting integers for specific machines, the routines packInt32() and
 * unpackInt32() were written.  If you have a machine that has native ints
 * other than 32 bit 2's complement, you must either write your own versions
 * of packInt32() and unpackInt32(), or modify the supplied ones.  For any
 * machine using 2's complement ints, simple changes to the
 * BYTES_PER_INT, HIGH_BITS, HIGH_BITS_MASK, and SIGN_BIT_MASK macros should
 * make your code work.  If you have something else, you're on your own ...
 *
 ************************************************************************/

#if _CRAY
#define BYTES_PER_INT	8
#endif

#ifndef BYTES_PER_INT
#define BYTES_PER_INT	4		/* default.  most boxes fit here */
#endif

#if BYTES_PER_INT == 8
#define BIG_INTS	1
#define HIGH_BITS	32			/* bits-per-int minus 32 */
#define HIGH_BITS_MASK	0xffffffff00000000	/* mask of the high bits */
#define SIGN_BIT_MASK	0x0000000080000000      /* mask of your sign bit */
#endif

#if BYTES_PER_INT == 4
#define BIG_INTS      	0
#endif

/*
 * Pack a native integer into a character buffer.  The buffer is assumed
 * to be at least 4 bytes long.
 */

int
packInt32(num, buf)
int	num;
char	*buf;
{
#if BIG_INTS
	num <<= HIGH_BITS;
#endif

	bcopy4((char *)&num, buf);
	return 0;
}

/*
 * Extract a native integer from a character buffer.  The buffer is assumed
 * to have been formatted using the packInt32() routine.
 */

int
unpackInt32(buf)
char	*buf;
{
	int	num;

	bcopy4(buf, (char *)&num);

#if BIG_INTS
	num >>= HIGH_BITS;

	if (num & SIGN_BIT_MASK) {
		num |= HIGH_BITS_MASK;
	}
#endif

	return num;
}
