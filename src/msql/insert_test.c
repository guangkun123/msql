#include <stdio.h>
#include "msql.h"


#define INSERT_QUERY "insert into test (name,num) values ('item %d', %d)"


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
		fprintf(stderr,"usage : insert_test <dbname> <Num>\n\n");
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

	num = atoi(argv[2]);
	count = 0;
	while (count < num)
	{
		sprintf(qbuf,INSERT_QUERY,count,count);
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
