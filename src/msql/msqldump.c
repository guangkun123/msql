/*
**      msqldump.c  - Dump a tables contents and format to an ASCII file
**
**
** Original version written by Igor Romanenko (igor@frog.kiev.ua) under
** the name of msqlsave.c and placed in the public domain.  This program
** remains in the public domain to continue the spirit of its original
** author although the modifications made to the original program are :-
**
** 		     Copyright (c) 1994  David J. Hughes
**
**
** This software is provided "as is" without any expressed or implied warranty.
**
** The author's original notes follow :-
**
**		******************************************************
**		*                                                    *
**		* MSQLSAVE.C -- saves the contents of an mSQL table  *
**		*               in .msql format.                     *
**		*                                                    *
**		* AUTHOR: Igor Romanenko (igor@frog.kiev.ua)         *
**		* DATE:   December 3, 1994                           *
**		* WARRANTY: None, expressed, impressed, implied      *
**		*           or other                                 *
**		* STATUS: Public domain                              *
**		*                                                    *
**		******************************************************
*/

#include <stdio.h>
#include <common/portability.h>
#include "msql.h"


/* Exit codes */

#define EX_USAGE 1
#define EX_MSQLERR 2
#define EX_CONSCHECK 3

char	usage[] = "\n\n\
usage: 	%s [-h host] [-vtdc] database [table]\n\n\
	Produce an ASCII dump of a database table or an entire database\n\n\
	-h	Use mSQL server on remote host\n\
	-v	Verbose mode\n\
	-t	Do not produce the table creation information\n\
	-d	Do not produce the data insertion information\n\
	-c	Complete INSERT statements (including field names)\n\
\n";

int	verbose = 0,
	tFlag = 0,
	cFlag = 0,
	dFlag = 0;
int	sock = -1;
char	insert_pat[5 * 1024];



/*
** DBerror -- prints mSQL error message and exits the program.
*/

void DBerror()
{
	fprintf(stderr, "mSQL error: %s\n", msqlErrMsg);
	if ( sock != -1 )
		msqlClose(sock);
	exit(EX_MSQLERR);
}


char *escapeText(str)
	char	*str;
{
	register char	*cp,
			*cp2,
			*tmp;
	int	numQuotes;

	cp = str;
	numQuotes = 0;
	while((cp = (char *)index(cp,'\'')))
	{
		numQuotes++;
		cp++;
	}
	cp = str;
	while((cp = (char *)index(cp,'\\')))
	{
		numQuotes++;
		cp++;
	}

	if (numQuotes)
	{
		tmp = (char *)malloc(strlen(str)+numQuotes+1);
		cp2 = tmp;
		cp = str;
		while(*cp)
		{
			if (*cp == '\'' || *cp == '\\')
				*cp2++='\\';
			*cp2++ = *cp++;
		}
		*cp2 = '\0';
		return(tmp);
	}
	else
	{
		return((char *)strdup(str));
	}
}


/*
** dbConnect -- connects to the host and selects DB.
**            Also checks whether the tablename is a valid table name.
*/

void dbConnect(host,database)
	char	*host,
		*database;
{
	if (verbose)
	{
		fprintf(stderr, "Connecting to %s...\n", 
			host ? host : "localhost");
	}
	sock = msqlConnect(host);
	if ( sock == -1 )
		DBerror();
	if ( verbose )
		fprintf(stderr, "Selecting data base %s...\n", database);
	if ( msqlSelectDB(sock, database) == -1 )
		DBerror();
}



/*
** dbDisconnect -- disconnects from the host.
*/

void dbDisconnect(host)
	char	*host;
{
	if (verbose)
	{
		fprintf(stderr, "Disconnecting from %s...\n", 
			host ? host : "localhost");
	}
	msqlClose(sock);
}



/*
** getStructure -- retrievs database structure, prints out corresponding
**                 CREATE statement and fills out insert_pat.
*/

int getTableStructure(table)
	char	*table;
{
	m_field 	*mf;
	m_result	*tableRes;
	int		init = 1,
			numFields;

	if (verbose)
	{
		fprintf(stderr, "Retrieving table structure for table %s...\n",
			table);
	}
	if (!(tableRes = msqlListFields(sock, table))) 
	{
		fprintf(stderr, "mSQL error: No such table - %s\n", table);
		exit(EX_MSQLERR);
	}

	if (!tFlag)
	{
		printf("\n#\n# Table structure for table '%s'\n#\n",table);
		printf("CREATE TABLE %s (\n", table);
	}

	if (cFlag)
		sprintf(insert_pat, "INSERT INTO %s (", table);
	else
		sprintf(insert_pat, "INSERT INTO %s VALUES (", table);

	while(mf=msqlFetchField(tableRes)) 
	{
		if (init)
		{
			init = 0;
		}
		else
		{
			if (!tFlag)
				printf(",\n");
			if (cFlag)
				strcat(insert_pat,", ");
		}
		if (cFlag)
			strcat(insert_pat,mf->name);
		if (!tFlag)
		{
			printf("  %s ", mf->name);
			switch(mf->type) 
			{
				case INT_TYPE:
					printf("INT");
					break;
				case CHAR_TYPE:
					printf("CHAR(%d)", mf->length);
					break;
				case REAL_TYPE:
					printf("REAL");
					break;
				default:
					fprintf(stderr, 
						"Unknown field type: %d\n", 
						mf->type);
					exit(EX_CONSCHECK);
			}

			if(IS_NOT_NULL(mf->flags) )
				printf(" NOT NULL");
			if(IS_PRI_KEY(mf->flags) )
				printf(" PRIMARY KEY");
		}
	}
	if (!tFlag)
		printf("\n) \\g\n\n");
	if (cFlag)
		strcat(insert_pat,") VALUES (");
	numFields = msqlNumFields(tableRes);
	msqlFreeResult(tableRes);
	return(numFields);
}




/*
** dumpTable saves database contents as a series of INSERT statements.
*/

void dumpTable(numFields,table)
	int	numFields;
	char	*table;
{
	char		query[48],
			*tmp;
	m_result 	*res;
	m_field 	*field;
	m_row 		row;
	int		i;
	int		init = 1;

	if (verbose)
		fprintf(stderr, "Sending SELECT query...\n");
	printf("\n#\n# Dumping data for table '%s'\n#\n\n",table);
	sprintf(query, "SELECT * FROM %s", table);
	if (msqlQuery(sock, query) == -1)
		DBerror();
	if (!(res=msqlStoreResult()))
		DBerror();
	if (verbose)
	{
		fprintf(stderr, "Retrieved %d rows. Processing...\n", 
			msqlNumRows(res) );
	}
	if (msqlNumFields(res) != numFields)
	{
		fprintf(stderr,"Error in field count!  Aborting.\n\n");
		exit(EX_CONSCHECK);
	}

	while (row=msqlFetchRow(res)) 
	{
		printf("%s", insert_pat);
		init = 1;
		msqlFieldSeek(res,0);
		for (i = 0; i < msqlNumFields(res); i++) 
		{
			if (!(field = msqlFetchField(res))) 
			{
				fprintf(stderr,"Not enough fields! Aborting\n");
				exit(EX_CONSCHECK);
			}
			if (!init )
				printf(",");
			else
				init=0;
			if (row[i])
			{
				if (field->type == CHAR_TYPE)
				{
					tmp = escapeText(row[i]);
					printf("\'%s\'", tmp);
					free(tmp);
				}
				else
				{
					printf("%s", row[i]);
				}
			}
			else
			{
				printf("NULL");
			}
		}
		printf(")\\g\n");
	}
}



char *getTableName()
{
	static m_result *res = NULL;
	m_row		row;

	if (!res)
	{
		res = msqlListTables(sock);
		if (!res)
			return(NULL);
	}
	row = msqlFetchRow(res);
	if (row)
	{
		return((char *)row[0]);
	}
	else
	{
		msqlFreeResult(res);
		return(NULL);
	}
}




main(argc, argv)
	int	argc;
	char	*argv[];
{
	int	c,
		numRows,
		errFlag = 0;
	char	*host = NULL,
		*database = NULL,
		*table = NULL;
	extern	char *optarg;
	extern	int optind;

	/*
	** Check out the args
	*/
	while((c=getopt(argc,argv,"h:vtcd"))!= -1)
	{
		switch(c)
		{
			case 'h':
				if (host)
					errFlag++;
				else
					host = optarg;
				break;

			case 'v':
				if (verbose)
					errFlag++;
				else
					verbose++;
				break;

			case 't':
				if (tFlag)
					errFlag++;
				else
					tFlag++;
				break;

			case 'c':
				if (cFlag)
					errFlag++;
				else
					cFlag++;
				break;

			case 'd':
				if (dFlag)
					errFlag++;
				else
					dFlag++;
				break;

			case '?':
				errFlag++;
		}
	}
	if (errFlag)
	{
		fprintf(stderr,usage, argv[0]);
		exit(EX_USAGE);
	}

	if (optind < argc)
		database = argv[optind++];
	if (optind < argc)
		table = argv[optind++];

	if (!database)
	{
		fprintf(stderr,usage, argv[0]);
		exit(EX_USAGE);
	}
	

	printf("#\n# mSQL Dump  (requires mSQL-1.0.6 or better)\n#\n");
	printf("# Host: %s    Database: %s\n",
	    host ? host : "localhost", database);
	printf("#--------------------------------------------------------\n\n");
	dbConnect(host,database);
	if (table)
	{
		numRows = getTableStructure(table);
		if (!dFlag)
			dumpTable(numRows,table);
	}
	else
	{
		while((table = getTableName()))
		{
			numRows = getTableStructure(table);
			if (!dFlag)
				dumpTable(numRows,table);
		}
	}
	dbDisconnect(host);
	printf("\n");
	exit(0);
}


