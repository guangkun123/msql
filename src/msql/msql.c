/*
**	msql.c	- 
**
**
** Copyright (c) 1993-95  David J. Hughes
** Copyright (c) 1995   Hughes Technologies Pty Ltd
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


#ifndef lint
static char RCS_id[] = 
	"msql.c,v 1.3 1994/08/19 08:03:09 bambi Exp";
#endif 


#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>

#include "common/portability.h"

#include "msql.h"
#include "version.h"


#define	MAX_LEN 15 * 1024


usage()
{
	(void)fprintf(stderr,"\n\nUsage : msql [-h host] database\n\n");
}



help()
{
	(void)printf("\n\nMiniSQL Help!\n\n");
	(void)printf("The following commands are available :- \n\n");
	(void)printf("\t\\q	Quit\n");
	(void)printf("\t\\g	Go (Send query to database)\n");
	(void)printf("\t\\e	Edit (Edit previous query)\n");
	(void)printf("\t\\p	Print (Print the query buffer)\n");
}


int max(v1,v2)
	int	v1,
		v2;
{
	if (v1 > v2)
		return(v1);
	else
		return(v2);
}


bufFill(buf,length,max,filler)
	char	*buf;
	int	length,
		max;
	char	filler;
{
	int	count;
	char	tmpBuf[2];

	tmpBuf[0] = filler;
	tmpBuf[1] = 0;
	count = max - length;
	while (count-- >= 0)
	{
		strcat(buf,tmpBuf);
	}
}



fill(length,max,filler)
	int	length,
		max;
	char	filler;
{
	int	count;

	count = max - length;
	while (count-- >= 0)
	{
		printf("%c",filler);
	}
}



handleQuery(sock, q)
	int	sock;
	char	*q;
{
	char	*nq,
		sepBuf[MAX_LEN];
	int	off,
		maxLen,
		length;
	m_result *result;
	m_row	cur;
	m_field	*curField;

	if (!q)
	{
		printf("No query specified !!\n");
		return;
	}
	if (!*q)
	{
		printf("No query specified !!\n");
		return;
	}

	if (msqlQuery(sock,q) < 0)
	{
		printf("\n\nERROR : %s\n\n",msqlErrMsg);
		return;
	}
	printf("\nQuery OK.\n\n");

	result = msqlStoreResult();
	if (!result)
	{
		printf("\n\n");	
		return;
	}

	printf("%d rows matched.\n\n",msqlNumRows(result));

	/*
	** Print a pretty header .... 
	*/
	(void)bzero(sepBuf,sizeof(sepBuf));
	strcat(sepBuf," +");
	maxLen = 1;
	while((curField = msqlFetchField(result)))
	{
		switch(curField->type)
		{
		    	case REAL_TYPE:
				length = strlen(curField->name);
				if (length < 12)
				{
					length = 12;
				}
				break;

		    	case INT_TYPE:
				length = strlen(curField->name);
				if (length < 8)
				{
					length = 8;
				}
				break;

		    	case CHAR_TYPE:
				length = max(strlen(curField->name),
						curField->length);
				break;
		}
		maxLen += length;
		if (maxLen >= MAX_LEN)
		{
			printf("\n\nRow length is too long to be displayed\n");
			msqlFreeResult(result);
			return;
		}
		bufFill(sepBuf,0,length,'-');
		strcat(sepBuf,"-+");
	}
	strcat(sepBuf,"\n");
	printf(sepBuf);
	msqlFieldSeek(result,0);

	printf(" |");
	while((curField = msqlFetchField(result)))
	{
		switch(curField->type)
		{
		    	case INT_TYPE:
				length = strlen(curField->name);
				if (length < 8)
				{
					length = 8;
				}
				break;

		    	case REAL_TYPE:
				length = strlen(curField->name);
				if (length < 12)
				{
					length = 12;
				}
				break;

		    	case CHAR_TYPE:
				length = max(strlen(curField->name),
						curField->length);
				break;
		}
		printf(" %s",curField->name);
		fill(strlen(curField->name),length,' ');
		printf("|");
	}
	printf("\n");
	msqlFieldSeek(result,0);
	printf(sepBuf);



	/*
	** Print the returned data
	*/
	while ((cur = msqlFetchRow(result)))
	{
		off = 0;
		printf(" |");
		while(off < msqlNumFields(result))
		{
			curField = msqlFetchField(result);
			switch(curField->type)
			{
			    case INT_TYPE:
				length = strlen(curField->name);
				if (length < 8)
				{
					length = 8;
				}
				break;

			    case REAL_TYPE:
				length = strlen(curField->name);
				if (length < 12)
				{
					length = 12;
				}
				break;

			    case CHAR_TYPE:
				length = max(strlen(curField->name),
						curField->length);
				break;
			}
			if (cur[off])
			{
				printf(" %s",cur[off]);
				fill(strlen(cur[off]),length,' ');
			}
			else
			{
				printf(" NULL");
				fill(4,length,' ');
			}
			printf("|");
			off++;
		}
		printf("\n");
		msqlFieldSeek(result,0);
	}
	printf(sepBuf);
	msqlFreeResult(result);
	printf("\n\n");
}



editQuery(q)
	char	*q;
{
	char	*filename,
		*editor,
		combuf[1024];
	int	fd;

	filename = (char *)msql_tmpnam(NULL);
	fd = open(filename,O_CREAT | O_WRONLY, 0777);
	editor = (char *)getenv("VISUAL");
	if (!editor)
	{
		editor = (char *)getenv("EDITOR");
	}
	if (!editor)
	{
		editor = "vi";
	}
	write(fd,q,strlen(q));
	close(fd);
	sprintf(combuf,"%s %s",editor,filename);
	system(combuf);
	fd = open(filename,O_RDONLY, 0777);
	bzero(q,MAX_LEN);
	read(fd,q,MAX_LEN);
	close(fd);
	unlink(filename);
}
	


main(argc,argv)
	int	argc;
	char	*argv[];
{
	char	qbuf[MAX_LEN],
		*cp,
		*host = NULL;
	int	newQ = 1,
		prompt = 1,
		sock,
		inString = 0,
		c,
		argsLeft,
		errFlag = 0,
		qLen = 0;
	register u_int inchar;
	extern	int optind;
	extern	char *optarg;

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


	printf("\n");
	if (argsLeft != 1)
	{
		usage();
		exit(1);
	}

	/*
	** If we don't have a hostname have a look at MSQL_HOST
	*/
	if (!host)
	{
		host = (char *)getenv("MSQL_HOST");
	}

	if ((sock = msqlConnect(host)) < 0)
	{
		printf("ERROR : %s\n",msqlErrMsg);
		exit(1);
	}

	if (msqlSelectDB(sock, argv[optind]) < 0)
	{
		printf("ERROR : %s\n",msqlErrMsg);
		exit(1);
	}

	/*
	**  Run in interactive mode like the ingres/postgres monitor
	*/

	printf("Welcome to the miniSQL monitor.  Type \\h for help.\n\n");
		
	inchar = EOF+1;
	(void)bzero(qbuf,sizeof(qbuf));
	cp = qbuf;
	printf("\nmSQL > ");
	while(!feof(stdin))
	{
		inchar = fgetc(stdin);
		qLen ++;
		if (qLen == MAX_LEN)
		{
			printf("\n\n\nError : Query text too long ( > %d bytes!)\n\n", MAX_LEN);
			printf("Check your query to ensure that there isn't an unclosed text field.\n\n");
			exit(1);
		}
		if (inchar == '\\')
		{
			if (inString)
			{
				*cp++ = inchar;
				inchar = fgetc(stdin);
				*cp++ = inchar;
				continue;
			}

			inchar = fgetc(stdin);
			if (inchar == EOF)
				continue;
			switch(inchar)
			{
				case 'h':
					help();
					newQ = 1;
					printf("\nmSQL > ");
					prompt=0;
					break;
				case 'g':
					handleQuery(sock,qbuf);
					newQ = 1;
					qLen = 0;
					inString = 0;
					printf("\nmSQL > ");
					prompt=0;
					break;
				case 'e':
					editQuery(qbuf);
					printf("Query buffer\n");
					printf("------------\n");
					printf("%s\n[continue]\n",qbuf);
					printf("    -> ");
					prompt=0;
					cp = qbuf + strlen(qbuf);
					qLen = strlen(qbuf);
					break;
				case 'q':
					msqlClose(sock);
					printf("\n\nBye!\n\n");	
					exit(0);

				case 'p':
					printf("\nQuery buffer\n");
					printf("------------\n");
					printf("%s\n[continue]\n",qbuf);
					printf("    -> ");
					prompt=0;
					break;
				default:
					printf("\n\nUnknown command.\n\n");
					newQ = 1;
					printf("\nmSQL > ");
					prompt=0;
					break;
			}
		}
		else
		{
			if (inchar == '\'')
			{
				if (inString)
					inString = 0;
				else
					inString = 1;
			}
			if (inString)
			{
				*cp++ = inchar;
				continue;
			}
			if ((newQ )&& (inchar != '\n'))
			{
				newQ = 0;
				cp = qbuf;
				(void)bzero(qbuf,sizeof(qbuf));
			}
			if (inchar == '#')
			{
				while(!feof(stdin))
				{
					inchar = fgetc(stdin);
					if (inchar == '\n')
					{
						break;
					}
				}
				continue;
			}
			if (inchar == '\n')
			{
				if (prompt)
				{
					printf("    -> ");
				}
				else
				{
					prompt++;
					continue;
				}
			}
			*cp++ = inchar;
		}
	}
	msqlClose(sock);
	printf("\nBye!\n\n");
	exit(0);
}
