#include <stdio.h>
#include "msql.h"


#define SELECT_QUERY "select name from test where num = %d"


main(argc,argv)
	int	argc;
	char	*argv[];
{
	int	count,
		sock,
		num;
	char	qbuf[160];
	
	if (argc != 3)
	{
		fprintf(stderr,"usage : select_test <dbname> <num>\n\n");
		exit(1);
	}

	if ((sock = msqlConnect(NULL)) < 0)
	{
		fprintf(stderr,"Couldn't connect to engine!\n%s\n\n",
			msqlErrMsg);
		perror("");
		exit(1);
	}

	if (msqlSelectDB(sock,argv[1]) < 0)
	{
		fprintf(stderr,"Couldn't select database %s!\n%s\n",argv[1],
			msqlErrMsg);
	}

	count = 0;
	num = atoi(argv[2]);
	while (count < num)
	{
		sprintf(qbuf,SELECT_QUERY,count);
		if(msqlQuery(sock,qbuf) < 0)
		{
			fprintf(stderr,"Query failed (%s)\n",msqlErrMsg);
			exit(1);
		}
		count++;
	}
	msqlClose(sock);
	exit(0);
}
