/*
**	msqld.c	- 
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
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <varargs.h>

#include "version.h"

#include "common/portability.h"

#ifdef HAVE_SYS_UN_H
#  include <sys/un.h>
#endif

#include <signal.h>
#include <netdb.h>

#ifdef HAVE_SELECT_H
#  include <select.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif

#include "common/site.h"
#include "common/debug.h"

#include "msql_priv.h"
#include "errmsg.h"




extern  char    *yytext;
extern	int	yylineno;
extern	int	yydebug;

extern	char	errMsg[];

int	curSock,
	IPsock,
	UNIXsock,
	numCons;

char	*unixPort;

#define MAX_SOCK_NUM	100

cinfo_t	conArray[MAX_SOCK_NUM];

extern	char	*packet;
char	PROGNAME[] = "msqld",
	BLANK_ARGV[] = "                                                  ";

#define safeFree(x)  { if(x){ (void)free(x); x = NULL; } }


void sigTrap(sig)
	int	sig;
{
	int	clientSock;

	signal(sig,SIG_IGN);
	fprintf(stderr,"\nHit by a sig %d\n\n",sig);
	clientSock = 3;
	printf("\n\nForced server shutdown due to bad signal!\n\n");
	while(clientSock < MAX_SOCK_NUM)
	{
		if (conArray[clientSock].db)
		{
			printf("Forcing close on Socket %d\n",clientSock);
			shutdown(clientSock,2);
			close(clientSock);
		}
		clientSock++;
	}
	shutdown(IPsock,2);
	close(IPsock);
#ifdef HAVE_SYS_UN_H
	shutdown(UNIXsock,2);
	close(UNIXsock);
	unlink(unixPort);
#endif
	printf("\n");
	abort();
}


sendError(fd,err)
	char	*err;
{
#	ifdef DEBUG
		printf("Send error called\n");
#	endif
	if (err)
		sprintf(packet,"-1:%s\n",err);
	else
		sprintf(packet,"-1:%s\n",errMsg);
	writePkt(fd);
}

sendOK(fd)
{
#	ifdef DEBUG
		printf("Send OK called\n");
#	endif
	sprintf(packet,"1:\n");
	writePkt(fd);
}


/****************************************************************************
** 	_initServer
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

initServer()
{
	int	sock,
		tcpPort,
		addrLen,
		opt;
	struct	sockaddr_in	IPaddr;
	struct	servent		*serv_ptr;
	char	*envVar;

#ifdef HAVE_SYS_UN_H
	struct	sockaddr_un	UNIXaddr;
#endif

	/*
	** Create an IP socket
	*/
	tcpPort = MSQL_PORT;
	if ((serv_ptr = getservbyname("msql", "tcp")))
	{
		tcpPort = ntohs(serv_ptr->s_port);
	}
	if ((envVar = getenv("MSQL_TCP_PORT")))
	{
		tcpPort = atoi(envVar);
	}
	msqlDebug(MOD_GENERAL,"IP Socket is %d\n",tcpPort);
	IPsock = socket(AF_INET, SOCK_STREAM, 0);
	if (IPsock < 0)
	{
		perror("Can't start server : IP Socket ");
		exit(1);
	}
#ifdef SO_REUSEADDR
	opt = 1;
	setsockopt(IPsock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
#endif

	(void)bzero(&IPaddr, sizeof(IPaddr));
	IPaddr.sin_family = AF_INET;
	IPaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	IPaddr.sin_port = htons(tcpPort);
	if (bind(IPsock, (struct sockaddr *)&IPaddr, sizeof(IPaddr)) < 0)
	{
		perror("Can't start server : IP Bind ");
		exit(1);
	}
	listen(IPsock,5);

#ifdef HAVE_SYS_UN_H
	/*
	** Create the UNIX socket
	*/
	unixPort = MSQL_UNIX_ADDR;
	if ((envVar = getenv("MSQL_UNIX_PORT")))
	{
		unixPort = envVar;
	}
	
	msqlDebug(MOD_GENERAL,"UNIX Socket is %s\n",unixPort);
	UNIXsock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (UNIXsock < 0)
	{
		perror("Can't start server : UNIX Socket ");
		exit(1);
	}
	(void)bzero(&UNIXaddr, sizeof(UNIXaddr));
	UNIXaddr.sun_family = AF_UNIX;
	strcpy(UNIXaddr.sun_path, unixPort);
	unlink(unixPort);
	if (bind(UNIXsock, (struct sockaddr *)&UNIXaddr, sizeof(UNIXaddr)) < 0)
	{
		perror("Can't start server : UNIX Bind ");
		exit(1);
	}
	listen(UNIXsock,5);
#endif
}



RETSIGTYPE puntServer(sig)
	int	sig;
{
	int	clientSock;

	signal(sig,SIG_IGN);
	clientSock = 3;
	if (sig == -1)
	{
		printf("\n\nNormal Server shutdown!\n\n");
	}
	else
	{
		printf("\n\nServer Aborting!\n\n");
	}
	while(clientSock < MAX_SOCK_NUM)
	{
		if (conArray[clientSock].db)
		{
			printf("Forcing close on Socket %d\n",clientSock);
			shutdown(clientSock,2);
			close(clientSock);
		}
		clientSock++;
	}
	shutdown(IPsock,2);
	close(IPsock);

#ifdef HAVE_SYS_UN_H
	shutdown(UNIXsock,2);
	close(UNIXsock);
	unlink(unixPort);
#endif
	printf("\n");
	dropCache();

	if (debugSet(MOD_MALLOC))
	{
		fprintf(stderr,"\n\nmalloc() leak detection .....\n");
		checkBlocks(MALLOC_BLK);
	}
	if (debugSet(MOD_MMAP))
	{
		fprintf(stderr,"\n\nmmap() leak detection .....\n");
		checkBlocks(MMAP_BLK);
	}
	printf("\n\nmSQL Daemon Shutdown Complete.\n\n");
	if (sig >= 0)
	{
		exit(1);
	}
}



yyerror(s)
        char	*s;
{
 	sprintf(packet,"-1:%s near \"%s\"\n", s, yytext?yytext:"");
	writePkt(curSock);
	msqlClean();
}





static int curComSock;
static fd_set *clientFdSet;

static setConnectionState(sock, fds)
	int	sock;
	fd_set	*fds;
{
	curComSock = sock;
	clientFdSet = fds;
}


RETSIGTYPE puntClient(sig)
	int	sig;
{
	signal(sig, puntClient);
	if (clientFdSet)
	{
		FD_CLR(curComSock, clientFdSet);
		shutdown(curComSock,2);
		close(curComSock);
		if (conArray[curComSock].db)
		{
			safeFree(conArray[curComSock].db);
			safeFree(conArray[curComSock].host);
			safeFree(conArray[curComSock].user);
		}
		conArray[curComSock].db = NULL;
		printf("Forced close of client on socket %d due to pipe sig\n",
			curComSock);
		numCons--;
	}
}



setupSignals()
{
#ifdef SIGSEGV
	signal(SIGSEGV,sigTrap);
#endif
#ifdef SIGBUS
	signal(SIGBUS,sigTrap);
#endif
#ifdef SIGINT
	signal(SIGINT,puntServer);
#endif
#ifdef SIGQUIT
	signal(SIGQUIT,puntServer);
#endif
#ifdef SIGKILL
	signal(SIGKILL,puntServer);
#endif
#ifdef SIGPIPE
	signal(SIGPIPE,puntClient);
#endif
#ifdef SIGTERM
	signal(SIGTERM,puntServer);
#endif
#ifdef SIGHUP
	signal(SIGHUP,SIG_IGN);
#endif
}



main(argc,argv)
	int	argc;
	char	*argv[];
{
	fd_set	readFDs,
		clientFDs;
	int	sock,
		newSock,
		comSock,
		command,
		opt,
		sig,
		error;
	char	dbname[30],
		*uname,
		*cp,
		*prog,
		*arg,
		path[255];
	FILE	*pidFile;

	/*
	** We have enough space for fiddling with the argv, continue
	*/
	msqlHomeDir = (char *)getenv("MSQL_HOME");
	if (!msqlHomeDir)
	{
		msqlHomeDir = INST_DIR;
	}
	umask(0);
	numCons=0;
	chdir(msqlHomeDir);
	printf("\n\nmSQL Server %s starting ...\n\n",SERVER_VERSION);
	initDebug();
	initNet();
	initServer();
	initBackend();
	(void)sprintf(path,"%s/msqld.pid",PID_DIR);
	pidFile = fopen(path,"w");
	if (!pidFile)
	{
		perror("Couldn't open PID file");
	}
	else
	{
		fprintf(pidFile,"%d",getpid());
		fclose(pidFile);
	}
	chmod(path,0644);
	umask(0);
	setupSignals();
	msqlLoadAcl(1);
	(void)bzero(&clientFDs,sizeof(fd_set));
	(void)bzero(conArray,sizeof(conArray));
	msqlDebug(MOD_GENERAL,"miniSQL debug mode.  Waiting for connections.\n");
	while(1)
	{
		(void)bcopy(&clientFDs,&readFDs,sizeof(fd_set));
		FD_SET(IPsock,&readFDs);
#ifdef HAVE_SYS_UN_H
		FD_SET(UNIXsock,&readFDs);
#endif
		if(select(MAX_SOCK_NUM+1,&readFDs,0,0,0) < 0)
			continue;

		/*
		** Is this a new connection request
		*/

		sock = 0;
		if (FD_ISSET(IPsock,&readFDs))
		{
			sock = IPsock;
		}
#ifdef HAVE_SYS_UN_H
		if (FD_ISSET(UNIXsock,&readFDs))
		{
			sock = UNIXsock;
		}
#endif
		if (sock)
		{
			struct	sockaddr_in 	cAddr;
			struct	sockaddr	dummy;
			int	cAddrLen,
				dummyLen;

			cAddrLen = sizeof(struct sockaddr_in);
			newSock = accept(sock, (struct sockaddr *)&cAddr, 
				&cAddrLen);
			if(newSock < 0)
			{
				perror("Error in accept ");
				continue;
			}
			dummyLen = sizeof(struct sockaddr);
			if (getsockname(newSock,&dummy, &dummyLen) < 0)
			{
				perror("Error on new connection socket");
				continue;
			}
			if (conArray[newSock].db)
			{
				safeFree(conArray[newSock].db);
				safeFree(conArray[newSock].host);
				safeFree(conArray[newSock].user);
				conArray[newSock].db = NULL;
			}

			/*
			** Are we over the connection limit
			*/
			numCons++;
			if (numCons > MAX_CON)
			{
				numCons--;
				sendError(newSock,CON_COUNT_ERROR);
				shutdown(newSock,2);
				close(newSock);
				continue;
			}


			/*
			** store the connection details
			*/

			msqlDebug(MOD_GENERAL,"New connection received on %d\n",
				newSock);
			error = 0;
			if (sock == IPsock)
			{
				int	addrLen;
				struct	hostent *hp;

				addrLen = sizeof(struct sockaddr);
				getpeername(newSock, (struct sockaddr *)
					&conArray[newSock].remote, &addrLen);
				addrLen = sizeof(struct sockaddr);
				getsockname(newSock, (struct sockaddr *)
					&conArray[newSock].local, &addrLen);
				hp = (struct hostent *)gethostbyaddr(
				    (char *)&conArray[newSock].remote.sin_addr,
				    sizeof(conArray[newSock].remote.sin_addr),
				    AF_INET);
				if (!hp)
				{
					sendError(newSock, BAD_HOST_ERROR);
					error = 1;
					shutdown(newSock,2);
					close(newSock);
					numCons --;
				}
				else
				{
					conArray[newSock].host = (char *)
						strdup(hp->h_name);
					msqlDebug(MOD_GENERAL,"Host = %s\n",
						hp->h_name);
				}
			}
			else
			{
				conArray[newSock].host = NULL;
				bzero(&conArray[newSock].local,
					sizeof(struct sockaddr));
				bzero(&conArray[newSock].
					remote,sizeof(struct sockaddr));
				msqlDebug(MOD_GENERAL,"Host = UNIX domain\n");
			}

			setConnectionState(newSock,&clientFDs);

			if (!error)
			{
				opt=1;
				setsockopt(newSock,SOL_SOCKET,SO_KEEPALIVE,
					(char *) &opt, sizeof(opt));
				sprintf(packet,
					"0:%d:%s\n",
					PROTOCOL_VERSION,SERVER_VERSION);
				writePkt(newSock);
				if (readPkt(newSock) <=0)
				{
					sendError(newSock,HANDSHAKE_ERROR);
					shutdown(newSock,2);
					close(newSock);
					conArray[newSock].host = NULL;
                                	bzero(&conArray[newSock].local,
                                        	sizeof(struct sockaddr));
                                	bzero(&conArray[newSock].
                                        	remote,sizeof(struct sockaddr));
					numCons--;
				}
				else
				{
					FD_SET(newSock,&clientFDs);
					uname = (char *)strtok(packet,"\n");
					msqlDebug(MOD_GENERAL,"User = %s\n",uname);
					safeFree(conArray[newSock].user);
					conArray[newSock].user = (char *)
						strdup(uname);
					sprintf(packet,"-100:\n");
					writePkt(newSock);
				}
			}
			continue;
		}

	

		/*
		** This must be a command.
		*/	

		comSock = 3;
		while(comSock < MAX_SOCK_NUM)
		{
		    if (FD_ISSET(comSock,&readFDs))
		    {
			setConnectionState(comSock,&clientFDs);
			if (readPkt(comSock) <= 0)
			{
				msqlDebug(MOD_GENERAL,
					"Command read on sock %d failed!\n",
					comSock);
				command = QUIT;
			}
			else
			{
				command = atoi(packet);
			}
			msqlDebug(MOD_GENERAL,"Command on sock %d = %d (%s)\n",
				comSock, command, comTable[command]);
			switch(command)
			{
			    case INIT_DB:
				cp=(char *)strtok(packet+2,"\n\r");
				if (!cp)
				{
					sendError(comSock,NO_DB_ERROR);
					break;
				}
				strcpy(dbname,cp);
				msqlDebug(MOD_GENERAL,"DBName = %s\n", dbname);
				conArray[comSock].access = msqlCheckAccess(
					dbname, conArray + comSock);
				if(conArray[comSock].access == NO_ACCESS)
				{
					sendError(comSock, 
						ACCESS_DENIED_ERROR);
					break;
				}
				if (msqlInit(dbname) < 0)
				{
					sendError(comSock,NULL);
				}
				else
				{
					sendOK(comSock);
					conArray[comSock].db =
						(char *)strdup(dbname);
					msqlSetDB(dbname);
				}
				break;

			    case QUERY:
				if (!conArray[comSock].db)
				{
					sendError(comSock,NO_DB_ERROR);
					break;
				}
				curSock = comSock;
				cp=(char *)(packet+2);
				arg = (char *)strdup(cp);
				if (debugSet(MOD_QUERY))
					fprintf(stderr,"\n");
				msqlDebug(MOD_QUERY,"Query = %s",arg);
				msqlSetDB(conArray[comSock].db);
				msqlSetPerms(conArray[comSock].access);
				msqlParseQuery(arg,comSock);
				safeFree(arg);
				break;

			    case DB_LIST:
				curSock = comSock;
				msqlListDBs(comSock);
				break;

			    case TABLE_LIST:
				if (!conArray[comSock].db)
				{
					sendError(comSock, NO_DB_ERROR);
					break;
				}
				curSock = comSock;
				msqlListTables(comSock,conArray[comSock].db);
				break;

			    case FIELD_LIST:
				if (!conArray[comSock].db)
				{
					sendError(comSock,NO_DB_ERROR);
					break;
				}
				cp=(char *)strtok(packet+2,
					"\n\r");
				if (!cp)
				{
					sendError(comSock,NO_TABLE_ERROR);
					break;
				}
				arg = (char *)strdup(cp);
				curSock = comSock;
				msqlListFields(comSock,
					arg,conArray[comSock].db);
				safeFree(arg);
				break;

			    case QUIT:
				msqlDebug(MOD_GENERAL,"DB QUIT!\n");
				FD_CLR(comSock,&clientFDs);
				shutdown(comSock,2);
				close(comSock);
				if (conArray[comSock].db)
				{
				   safeFree(conArray[comSock].db);
				   safeFree(conArray[comSock].host);
				   safeFree(conArray[comSock].user);
				}
				conArray[comSock].db = NULL;
				numCons--;
				break;
		
			    case CREATE_DB:
				if (!msqlCheckLocal(conArray + comSock))
				{
					sendError(comSock,PERM_DENIED_ERROR);
					break;
				}
				cp=(char *)strtok(packet+2,
					"\n\r");
				if (!cp)
				{
					sendError(comSock,NO_DB_ERROR);
					break;
				}
				arg = (char *)strdup(cp);
				msqlCreateDB(comSock,arg);
				safeFree(arg);
				break;

			    case DROP_DB:	
				if (!msqlCheckLocal(conArray + comSock))
				{
					sendError(comSock,PERM_DENIED_ERROR);
					break;
				}
				cp=(char *)strtok(packet+2,
					"\n\r");
				if (!cp)
				{
					sendError(comSock,NO_DB_ERROR);
					break;
				}
				arg = (char *)strdup(cp);
				msqlDropDB(comSock,arg);
				safeFree(arg);
				break;

			    case RELOAD_ACL:
				if (!msqlCheckLocal(conArray + comSock))
				{
					sendError(comSock,PERM_DENIED_ERROR);
					break;
				}
				(void)sprintf(packet,"-100:\n");
				reloadAcls(comSock);
				writePkt(comSock);
				break;

			    case SHUTDOWN:
				if (!msqlCheckLocal(conArray + comSock))
				{
					sendError(comSock,PERM_DENIED_ERROR);
					break;
				}
				sprintf(packet,"-100:\n");
				writePkt(comSock);
				puntServer(-1);
				exit(0);
				break;

			    default:
				sendError(comSock, UNKNOWN_COM_ERROR);
				break;
			}
			msqlDebug(MOD_GENERAL,"Command Processed!\n");
		    }
		    comSock++;
		}
	}
}
