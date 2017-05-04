/*
**	relshow.c	- Display the database structure
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>

#include <common/portability.h>
#include "msql_priv.h"
#include "msql.h"


char	PROGNAME[] = "Relshow";

void usage()
{
	printf("\nUsage : relshow [-h host] [dbName [relName]]\n\n");
	printf("         Where   dbName is the name of a database\n");
	printf("                 relname is the name of a relation\n\n");
	printf("If no database is given, list the known databases\n");
	printf("If no relation is given, list relations in the database\n");
	printf("If database and relation given, list fields and field types\n");
	printf("   in the given relation\n\n\007");
}




main(argc,argv)
	int	argc;
	char	*argv[];
{
	char	dbShow = 0,
		relShow = 0,
		fieldShow = 0;
	char	typeName[10];
	int	sock,
		argsLeft,
		errFlag = 0,
		c;
	m_row	cur;
	m_result *res;
	m_field	*curField;
        char    *host = NULL;
        extern  int optind;
        extern  char *optarg;


        while((c=getopt(argc,argv,"h:"))!= -1)
        {
                switch(c)
                {
                        case 'h':
                                if (host)
                                        errFlag++;
                                else
                                        host = optarg;
                                break;
                        case '?':
                                errFlag++;
                                break;
                }
        }

	argsLeft = argc - optind;

	/*
	** Work out what we here to do
	*/

	switch(argsLeft)
	{
		case 0:	dbShow++;
			break;
		case 1: relShow++;
			break;
		case 2:	fieldShow++;
			break;
		default:usage();
			exit(1);
	}


	/*
	**  Fire up mSQL
	*/

	if ((sock = msqlConnect(host)) < 0)
	{
		printf("\nError connecting to database : %s\n\n", msqlErrMsg);
		exit(1);
	}

	if (!dbShow)
	{
		if(msqlSelectDB(sock,argv[optind]) < 0)
		{
			printf("\n%s\n\n",msqlErrMsg);
			msqlClose(sock);
			exit(1);
		}
	}


	/*
	** List the available databases if required
	*/

	if (dbShow)
	{
		res = msqlListDBs(sock);
		if (!res)
		{
			printf("\nERROR : Couldn't get database list!\n");
			exit(1);
		}
		printf("\n\n  +-----------------+\n");
		printf("  |    Databases    |\n");
		printf("  +-----------------+\n");
		while((cur = msqlFetchRow(res)))
		{
			printf("  | %-15.15s |\n", cur[0]);
		}
		printf("  +-----------------+\n\n");
		msqlFreeResult(res);
		msqlClose(sock);
		exit(0);

	}


	/*
	** List the available relations if required
	*/

	if (relShow)
	{

		res = msqlListTables(sock);
		if (!res)
		{
			printf("\n");
			printf("ERROR : Unable to list tables in database %s\n",
				argv[optind]);
			exit(1);
		}
		printf("\n\nDatabase = %s\n\n",argv[optind]);
		printf("  +---------------------+\n");
		printf("  |       Table         |\n");
		printf("  +---------------------+\n");
		while((cur = msqlFetchRow(res)))
		{
			printf("  | %-19.19s |\n", cur[0]);
		}
		printf("  +---------------------+\n\n");
		msqlFreeResult(res);
		msqlClose(sock);
		exit(0);
	}


	/*
	** List the attributes and types if required
	*/

	if (fieldShow)
	{
		/*
		** Get the list of attributes
		*/

		res = msqlListFields(sock,argv[optind+1]);
		if (!res)
		{
			printf("ERROR : Couldn't find %s in %s!\n\n",
				argv[optind+1], argv[optind]);
			exit(1);
		}

		/*
		** Display the information
		*/

		printf("\nDatabase = %s\n",argv[optind]);
		printf("\nTable    = %s\n\n",argv[optind + 1]);
		printf(" +-----------------+----------+--------+----------+-----+\n");
		printf(" |     Field       |   Type   | Length | Not Null | Key |\n");
		printf(" +-----------------+----------+--------+----------+-----+\n");
		while((curField = msqlFetchField(res)))
		{
			
			printf(" | %-15.15s | ",curField->name);
			switch(curField->type)
			{
				case INT_TYPE:
					strcpy(typeName,"int");
					break;

				case CHAR_TYPE:
					strcpy(typeName,"char");
					break;

				case REAL_TYPE:
					strcpy(typeName,"real");
					break;

				default:
					strcpy(typeName,"Unknown");
					break;
			}
			printf("%-8.8s |",typeName);
			printf(" %-6d |",curField->length);
			printf(" %-8.8s |", IS_NOT_NULL(curField->flags)?
				"Y":"N");
			printf(" %-3.3s |\n", IS_PRI_KEY(curField->flags)?
				"Y":"N");
		}
		printf(" +-----------------+----------+--------+----------+-----+");
		printf("\n\n");
		msqlFreeResult(res);
		msqlClose(sock);
	}
}
