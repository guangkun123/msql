/*
**	msqladmin.c	- 
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
** ID = "$Id:"
**
*/


#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>

#ifdef HAVE_DIRENT
#  include <dirent.h>
#else
#  include <sys/dir.h>
#endif

#include "version.h"
#include "msql.h"


#include <common/site.h>
#include <common/portability.h>

int	qFlag = 0;
char	*msqlHomeDir;


usage()
{
	printf("\n\nusage : msqladmin [-h host] [-q] <Command>\n\n");
	printf("where command =");
	printf("\t drop DatabaseName\n");
	printf("\t\t create DatabaseName\n");
	printf("\t\t shutdown\n");
	printf("\t\t reload\n");
	printf("\t\t version\n");
	printf("\n -q\tQuiet mode.  No verification of commands.\n\n");
}


createDB(sock,db)
	int	sock;
	char	*db;
{
	if(msqlCreateDB(sock,db) < 0)
	{
		fprintf(stderr,"\nmSQL Command failed!\nServer error = %s\n\n",
				msqlErrMsg);
		msqlClose(sock);
		exit(1);
	}
	else
	{
		printf("Database \"%s\" created.\n",db);
	}
}


dropDB(sock,db)
	int	sock;
	char	*db;
{
	char	buf[10];


	if (!qFlag)
	{
		printf("\n\nDropping the database is potentially a very bad ");
		printf("thing to do.\nAny data stored in the database will be");
		printf(" destroyed.\n\nDo you really want to drop the ");
		printf("\"%s\" ",db);
		printf("database?  [Y/N] ");
		bzero(buf,10);
		fgets(buf,10,stdin);
		if ( (*buf != 'y') && (*buf != 'Y'))
		{
			printf("\n\nOK, aborting database drop!\n\n");
			msqlClose(sock);
			exit(0);
		}
	}
	if(msqlDropDB(sock,db) < 0)
	{
		fprintf(stderr,"\nmSQL Command failed!\nServer error = %s\n\n",
			msqlErrMsg);
		msqlClose(sock);
		exit(1);
	}
	else
	{
		fprintf(stderr,"Database \"%s\" dropped\n",db);
	}
}



main(argc,argv)
	int	argc;
	char	*argv[];
{
	int	sock,
		c,
		argsLeft,
		errFlag = 0;
	char	*host = NULL;
	extern	int optind;
	extern	char *optarg;


        msqlHomeDir = (char *)getenv("MSQL_HOME");
        if (!msqlHomeDir)
        {
                msqlHomeDir = INST_DIR;
        }

	while((c=getopt(argc,argv,"h:q"))!= -1)
        {
                switch(c)
                {
                        case 'h':
                                if (host)
                                        errFlag++;
                                else
                                        host = optarg;
                                break;
			case 'q':
				if (qFlag)
					errFlag++;
				else
					qFlag++;
				break;
			case '?':
				errFlag++;
				break;
		}
	}

	argsLeft = argc - optind;

	if (errFlag || argsLeft == 0)
	{
		usage();
		exit(1);
	}


        if ((sock = msqlConnect(host)) < 0)
        {
                fprintf(stderr,"ERROR : %s\n",msqlErrMsg);
                exit(1);
        }

	if (strcmp(argv[optind],"create") == 0)
	{
		if (argsLeft != 2)
		{
			usage();
			msqlClose(sock);
			exit(1);
		}
		createDB(sock,argv[optind+1]);
		msqlClose(sock);
		exit(0);
	}
	if (strcmp(argv[optind],"drop") == 0)
	{
		if (argsLeft != 2)
		{
			usage();
			msqlClose(sock);
			exit(1);
		}
		dropDB(sock,argv[optind+1]);
		msqlClose(sock);
		exit(0);
	}
	if (strcmp(argv[optind],"shutdown") == 0)
	{
		if (argsLeft != 1)
		{
			usage();
			msqlClose(sock);
			exit(1);
		}
		if(msqlShutdown(sock) < 0)
		{
			printf("\nmSQL Command failed!\nServer error = %s\n\n",
				msqlErrMsg);
			msqlClose(sock);
			exit(1);
		}
		exit(0);
	}
	if (strcmp(argv[optind],"reload") == 0)
	{
		if (argsLeft != 1)
		{
			usage();
			msqlClose(sock);
			exit(1);
		}
		if(msqlReloadAcls(sock) < 0)
		{
			printf("\nmSQL Command failed!\nServer error = %s\n\n",
				msqlErrMsg);
			msqlClose(sock);
			exit(1);
		}
		msqlClose(sock);
		exit(0);
	}
	if (strcmp(argv[optind],"version") == 0)
	{
		if (argsLeft != 1)
		{
			usage();
			msqlClose(sock);
			exit(1);
		}
		printf("\nVersion Details :-\n\n");
		printf("\tmsqladmin version \t%s\n",SERVER_VERSION);
		printf("\tmSQL connection \t%s\n",msqlGetHostInfo());
		printf("\tmSQL server version \t%s\n", msqlGetServerInfo());
		printf("\tmSQL protocol version \t%d\n", msqlGetProtoInfo());
		printf("\tmSQL TCP socket \t%d\n", MSQL_PORT);
		printf("\tmSQL UNIX socket \t%s\n", MSQL_UNIX_ADDR);
		printf("\tmSQL root user \t\t%s\n", ROOT);
		msqlClose(sock);
		exit(0);
	}
	usage();
	msqlClose(sock);
	exit(1);
}
