/*
**	libmsql.c	- 
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <signal.h>
#include <netdb.h>
#include <pwd.h>
#include <stdlib.h>
#include <varargs.h>
#include <string.h>

#include "common/portability.h"

#ifdef HAVE_SYS_UN_H
#  include <sys/un.h>
#endif


#include "common/site.h"

#include "msql_priv.h"
#include "errmsg.h"

#define _LIB_SOURCE
#include "msql.h"

#ifndef	INADDR_NONE
#define INADDR_NONE	-1
#endif


static	char	serverInfo[80],
		hostInfo[80];
static	int	curServerSock,
		numFields,
		queryTableSize,
		fieldTableSize,
		protoInfo;
static	m_data *tmpDataStore = NULL,
		*queryData = NULL,
		*fieldData = NULL;


char	msqlErrMsg[160];

RETSIGTYPE	(*oldHandler)();
static void msqlDebug();

#define	resetError()	(void)bzero(msqlErrMsg,sizeof(msqlErrMsg))
#define chopError()	{ char *cp; cp = msqlErrMsg+strlen(msqlErrMsg) -1; \
				if (*cp == '\n') *cp = 0;}
			

extern	char	*packet;

#define safeFree(x)     {if(x) { (void)free(x); x = NULL; } }



/**************************************************************************
**	_ msqlInitDebug
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

#define	MOD_QUERY	1
#define MOD_API		2
#define MOD_MALLOC	4
#define MOD_ERROR	8


static int 	debugLevel=0,
		debugInit = 0;

static void msqlInitDebug()
{
	char	*env,
		*tmp,
		*tok;

	env = getenv("MINERVA_DEBUG");
	if(env)
	{
		tmp = (char *)strdup(env);
	}
	else
		return;
	printf("\n-------------------------------------------------------\n");
	printf("MINERVA_DEBUG found. libmsql started with the following:-\n\n");
	tok = (char *)strtok(tmp,":");
	while(tok)
	{
		if (strcmp(tok,"msql_query") == 0)
		{
			debugLevel |= MOD_QUERY;
			printf("Debug level : query\n");
		}
		if (strcmp(tok,"msql_api") == 0)
		{
			debugLevel |= MOD_API;
			printf("Debug level : api\n");
		}
		if (strcmp(tok,"msql_malloc") == 0)
		{
			debugLevel |= MOD_MALLOC;
			printf("Debug level : malloc\n");
		}
		tok = (char *)strtok(NULL,":");
	}
	safeFree(tmp);
	printf("\n-------------------------------------------------------\n\n");
}






/**************************************************************************
**	_msqlDebug
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

static void msqlDebug(va_alist)
	va_dcl
{
		va_list args;
	char	msg[1024],
		*fmt;
	int	module,
		out = 0;

	va_start(args);
	module = (int) va_arg(args, int *);
	if (! (module & debugLevel))
	{
		va_end(args);
		return;
	}

	fmt = (char *)va_arg(args, char *);
	if (!fmt)
        	return;
	(void)vsprintf(msg,fmt,args);
	va_end(args);
	printf("[libmsql] %s",msg);
	fflush(stdout);
}


/**************************************************************************
**	_setServerSock 	
**
**	Purpose	: Store the server socket currently in use
**	Args	: Server socket
**	Returns	: Nothing
**	Notes	: The current socket is stored so that the signal
**		  handlers know which one to shut down.
*/

static void setServerSock(sock)
	int	sock;
{
	curServerSock = sock;
}


 

/**************************************************************************
**	_closeServer 	
**
**	Purpose	: Shut down the server connection
**	Args	: Server socket
**	Returns	: Nothing
**	Notes	: This is used by msqlClose and the signal handlers
*/

static void closeServer(sock)
	int	sock;
{
	msqlDebug(MOD_API,"Server socket (%d) closed\n", sock);
	shutdown(sock,2);
	signal(SIGPIPE,oldHandler);
	close(sock);
}





/**************************************************************************
**	_msqlClose
**
**	Purpose	: Send a QUIT to the server and close the connection
**	Args	: Server socket
**	Returns	: Nothing
**	Notes	: 
*/

void msqlClose(sock)
	int	sock;
{
	char	buf[6];

	setServerSock(sock);
	sprintf(packet,"%d:\n",QUIT);
	writePkt(sock);
	closeServer(sock);
}





/**************************************************************************
**	_pipeHandler
**
**	Purpose	: Close the server connection if we get a SIGPIPE
**	Args	: sig
**	Returns	: Nothing
**	Notes	: 
*/

RETSIGTYPE pipeHandler(sig)
	int	sig;
{
	msqlDebug(MOD_API,"Hit by pipe signal\n");
	closeServer(curServerSock);
	return;
}




static freeQueryData(cur)
	m_data	*cur;
{
	m_data	*prev;
	int	offset;

	while(cur)
	{
		offset = 0;
		while(offset < cur->width)
		{
			safeFree(cur->data[offset]);
			offset++;
		}
		safeFree(cur->data);
		prev = cur;
		cur = cur->next;
		safeFree(prev);
		msqlDebug(MOD_MALLOC, "Query data row - free @ %X\n", prev);
	}
	safeFree(cur);
}





static freeFieldList(fieldData)
	m_fdata	*fieldData;
{
	m_fdata	*cur,
		*prev;

	cur = fieldData;
	while(cur)
	{
		prev = cur;
		cur = cur->next;
		safeFree(prev->field.table);
		safeFree(prev->field.name);
		safeFree(prev);
		msqlDebug(MOD_MALLOC, "Field List Entry- free @ %X\n", prev);
	}
}



/**************************************************************************
**	_msqlFreeResult
**
**	Purpose	: Free the memory allocated to a table returned by a select
**	Args	: Pointer to head of table
**	Returns	: Nothing
**	Notes	: 
*/

void msqlFreeResult(result)
	m_result  *result;
{
	freeQueryData(result->queryData);
	freeFieldList(result->fieldData);
	safeFree(result);
	msqlDebug(MOD_MALLOC,"Result Handle - Free @ %X\n",result);
}

		

static m_fdata *tableToFieldList(data)
	m_data	*data;
{
	m_data	*curRow;
	m_fdata	*curField,
		*prevField,
		*head = NULL;

	curRow = data;
	while(curRow)
	{
		curField = (m_fdata *)malloc(sizeof(m_fdata));
		msqlDebug(MOD_MALLOC,"Field List Entry - malloc @ %X of %d\n",
			curField, sizeof(m_fdata));
		(void)bzero(curField, sizeof(m_fdata));
		if (head)
		{
			prevField->next = curField;
			prevField = curField;
		}
		else
		{
			head = prevField = curField;
		}

		curField->field.table = (char *)strdup((char *)curRow->data[0]);
		curField->field.name = (char *)strdup((char *)curRow->data[1]);
		curField->field.type = atoi((char*)curRow->data[2]);
		curField->field.length = atoi((char*)curRow->data[3]);
		curField->field.flags = 0;
		if (*curRow->data[4] == 'Y')
			curField->field.flags |= NOT_NULL_FLAG;
		if (*curRow->data[5] == 'Y')
			curField->field.flags |= PRI_KEY_FLAG;
		curRow = curRow->next;
	}
	return(head);
}


/**************************************************************************
**	_msqlConnect
**
**	Purpose	: Form a connection to a mSQL server
**	Args	: hostname of server
**	Returns	: socket for further use.  -1 on error
**	Notes	: If host == NULL, localhost is used via UNIX domain socket
*/

int msqlConnect(host)
	char	*host;
{
	char	*cp,
		*envVar,
		*unixPort;
	struct	sockaddr_in IPaddr;

#ifdef HAVE_SYS_UN_H
	struct	sockaddr_un UNIXaddr;
#endif
        struct  servent         *serv_ptr;
	struct	hostent *hp;
	u_long	IPAddr;
	int	opt,
		version,
		sock,
		tcpPort;
	struct	passwd *pw;


	resetError();
	initNet();
	if (!debugInit)
	{
		debugInit++;
		msqlInitDebug();
	}

	/*
	** Grab a socket and connect it to the server
	*/

#ifndef HAVE_SYS_UN_H
	if (!host)
	{
		host = "localhost";
	}
#endif

	if (!host)
	{
#ifdef HAVE_SYS_UN_H
		/* Shouldn't get in here with UNIX socks */
	        unixPort = MSQL_UNIX_ADDR;
        	if ((envVar = getenv("MSQL_UNIX_PORT")))
        	{
                	unixPort = envVar;
        	}
		strcpy(hostInfo,"Localhost via UNIX socket");
		msqlDebug(MOD_API,"Server name = NULL.  Using UNIX sock(%s)\n",
			unixPort);
		sock = socket(AF_UNIX,SOCK_STREAM,0);
		if (sock < 0)
		{
			sprintf(msqlErrMsg,SOCKET_ERROR);
			return(-1);
		}
		setServerSock(sock);
		opt = 1;
		setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&opt, 
			sizeof(opt));

		(void)bzero(&UNIXaddr,sizeof(UNIXaddr));
		UNIXaddr.sun_family = AF_UNIX;
		strcpy(UNIXaddr.sun_path, unixPort);
		if(connect(sock,(struct sockaddr *) &UNIXaddr, 
			sizeof(UNIXaddr))<0)
		{
			sprintf(msqlErrMsg,CONNECTION_ERROR);
			close(sock);
			return(-1);
		}
#endif
	}
	else
	{
	        tcpPort = MSQL_PORT;
        	if ((serv_ptr = getservbyname("msql", "tcp")))
        	{
                	tcpPort = ntohs(serv_ptr->s_port);
        	}
        	if ((envVar = getenv("MSQL_TCP_PORT")))
        	{
                	tcpPort = atoi(envVar);
        	}

		sprintf(hostInfo,"%s via TCP/IP",host);
		msqlDebug(MOD_API,"Server name = %s.  Using TCP sock (%d)\n",
			host, tcpPort);
		sock = socket(AF_INET,SOCK_STREAM,0);
		if (sock < 0)
		{
			sprintf(msqlErrMsg,IPSOCK_ERROR);
			return(-1);
		}
		setServerSock(sock);
		opt = 1;
		setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&opt, 
			sizeof(opt));

		(void)bzero(&IPaddr,sizeof(IPaddr));
		IPaddr.sin_family = AF_INET;

		/*
		** The server name may be a host name or IP address
		*/
	
		if ((IPAddr = inet_addr(host)) != INADDR_NONE)
		{
			bcopy(&IPAddr,&IPaddr.sin_addr,sizeof(IPAddr));
		}
		else
		{
			hp = gethostbyname(host);
			if (!hp)
			{
				sprintf(msqlErrMsg,
					UNKNOWN_HOST,
					host);
				close(sock);
				return(-1);
			}
			bcopy(hp->h_addr,&IPaddr.sin_addr, hp->h_length);
		}
		IPaddr.sin_port = htons(tcpPort);
		if(connect(sock,(struct sockaddr *) &IPaddr, 
			sizeof(IPaddr))<0)
		{
			sprintf(msqlErrMsg,CONN_HOST_ERROR, host);
			perror("Connect");
			close(sock);
			return(-1);
		}
	}

	oldHandler = signal(SIGPIPE,pipeHandler);
	msqlDebug(MOD_API,"Connection socket = %d\n",sock);

	/*
	** Check the greeting message and save the version info
	*/

	if(readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}


	/*
	** Check the result.  First check the status code from the
	** server and then the protocol version.  If the status == -1
	** or the protocol doesn't match our version, bail out!
	*/

	if (atoi(packet) == -1)  
	{
		if (cp = (char *)index(packet,':'))
		{
			strcpy(msqlErrMsg,cp+1);
			chopError();
		}
		else
		{
			strcpy(msqlErrMsg,UNKNOWN_ERROR);
		}
		closeServer(sock);
		return(-1);
	}

	
	cp = (char *)index(packet,':');
	if (!cp)
	{
		strcpy(msqlErrMsg,PACKET_ERROR);
		closeServer(sock);
		return(-1);
	}
	version = atoi(cp + 1);
	if (version != PROTOCOL_VERSION) 
	{
		sprintf(msqlErrMsg, VERSION_ERROR, version, PROTOCOL_VERSION);
		closeServer(sock);
		return(-1);
	}
	msqlDebug(MOD_API,"mSQL protocol version - API=%d, server=%d\n",
		PROTOCOL_VERSION, version);
	protoInfo = version;
	cp = (char *)index(cp+1,':');
	if (cp)
	{
		msqlDebug(MOD_API,"Server greeting = '%s'\n",cp+1);
		strcpy(serverInfo,cp+1);
	}
	else
	{
		strcpy(serverInfo,"Error in server handshake!");
	}
	if (*(serverInfo+strlen(cp+1)-1) == '\n')
	{
		*(serverInfo+strlen(cp+1)-1) = 0;
	}


	/*
	** Send the username for this process for ACL checks
	*/
	pw = getpwuid(geteuid());
	if (!pw)
	{
		strcpy(msqlErrMsg,USERNAME_ERROR);
		closeServer(sock);
		return(-1);
	}
	(void)sprintf(packet,"%s\n",pw->pw_name);
	writePkt(sock);
	(void)bzero(packet,PKT_LEN);
	if(readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}


	/*
	** Check the result
	*/

	if (atoi(packet) == -1)
	{
		char	*cp;

		cp = (char *)index(packet,':');
		if (cp)
		{
			strcpy(msqlErrMsg,cp+1);
			chopError();
		}
		else
		{
			strcpy(msqlErrMsg,UNKNOWN_ERROR);
		}
		closeServer(sock);
		return(-1);
	}
	return(sock);
}






/**************************************************************************
**	_msqlInitDB
**
**	Purpose	: Tell the server which database we want to use
**	Args	: Server sock and DB name
**	Returns	: -1 on error
**	Notes	: 
*/

int msqlSelectDB(sock,db)
	int	sock;
	char	*db;
{
	int	res;



        msqlDebug(MOD_API,"Select Database = \"%s\"\n",db);

	resetError();
	setServerSock(sock);
	
	/*
	** Issue the init DB command
	*/

	(void)sprintf(packet,"%d:%s\n",INIT_DB,db);
	writePkt(sock);
	(void)bzero(packet,PKT_LEN);
	if(readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}


	/*
	** Check the result
	*/

	if (atoi(packet) == -1)
	{
		char	*cp;

		cp = (char *)index(packet,':');
		if (cp)
		{
			strcpy(msqlErrMsg,cp+1);
			chopError();
		}
		else
		{
			strcpy(msqlErrMsg,UNKNOWN_ERROR);
		}
		return(-1);
	}

	return(0);
}





/**************************************************************************
**	_msqlStoreResult
**
**	Purpose	: Store the data returned from a query
**	Args	: None
**	Returns	: Result handle or NULL if no data
**	Notes	: 
*/

m_result *msqlStoreResult()
{
	m_result *tmp;

	if (!queryData && !fieldData)
	{
		return(NULL);
	}
	tmp = (m_result *)malloc(sizeof(m_result));
	msqlDebug(MOD_MALLOC,"Result Handle - malloc @ %X of %d\n",
		tmp, sizeof(m_result));
	if (!tmp)
	{
		return(NULL);
	}
	(void)bzero(tmp, sizeof(m_result));
	tmp->queryData = queryData;
	tmp->numRows = queryTableSize;
	tmp->fieldData = tableToFieldList(fieldData);
	tmp->numFields = fieldTableSize;
	tmp->cursor = tmp->queryData;
	tmp->fieldCursor = tmp->fieldData;
	freeQueryData(fieldData);
	queryData = NULL;
	fieldData = NULL;
	return(tmp);
}





/**************************************************************************
**	_msqlFetchField
**
**	Purpose	: Return a row of the query results
**	Args	: result handle
**	Returns	: pointer to row data
**	Notes	: 
*/

m_field	*msqlFetchField(handle)
	m_result *handle;
{
	m_field	*tmp;

	if (!handle->fieldCursor)
	{
		return(NULL);
	}
	tmp = &(handle->fieldCursor->field);
	handle->fieldCursor = handle->fieldCursor->next;
	return(tmp);
}



/**************************************************************************
**	_msqlFetchRow
**
**	Purpose	: Return a row of the query results
**	Args	: result handle
**	Returns	: pointer to row data
**	Notes	: 
*/

m_row	msqlFetchRow(handle)
	m_result *handle;
{
	m_row	tmp;

	if (!handle->cursor)
	{
		return(NULL);
	}
	tmp = handle->cursor->data;
	handle->cursor = handle->cursor->next;
	return(tmp);
}




/**************************************************************************
**	_msqlFieldSeek
**
**	Purpose	: Move the result cursor
**	Args	: result handle, offset
**	Returns	: Nothing.  Just sets the cursor
**	Notes	: The data is a single linked list so we can go backwards
*/

void msqlFieldSeek(handle, offset)
	m_result *handle;
	int	offset;
{
	m_fdata	*tmp;

	
	msqlDebug(MOD_API,"msqlFieldSeek() pos = \n",offset);
	tmp = handle->fieldData;
	while(offset)
	{
		if (!tmp)
			break;
		tmp = tmp->next;
		offset--;
	}
	handle->fieldCursor = tmp;
}

/**************************************************************************
**	_msqlDataSeek
**
**	Purpose	: Move the result cursor
**	Args	: result handle, offset
**	Returns	: Nothing.  Just sets the cursor
**	Notes	: The data is a single linked list so we can go backwards
*/

void msqlDataSeek(handle, offset)
	m_result *handle;
	int	offset;
{
	m_data	*tmp;

	
	msqlDebug(MOD_API,"msqlDataSeek() pos = \n",offset);
	tmp = handle->queryData;
	while(offset)
	{
		if (!tmp)
			break;
		tmp = tmp->next;
		offset--;
	}
	handle->cursor = tmp;
}



/**************************************************************************
**	_
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

int msqlQuery(sock,q)
	int	sock;
	char	*q;
{
	int	len,
		res;
	char	*cp;
	


        msqlDebug(MOD_QUERY,"Query = \"%s\"\n",q);
	resetError();
	setServerSock(sock);

	/*
	** Issue the query
	*/

	(void)sprintf(packet,"%d:%s\n",QUERY,q);
	writePkt(sock);
	(void)bzero(packet,PKT_LEN);
	if(readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}


	/*
	** Check the result.  It may be an indication of further data to
	** come (ie. from a select)
	*/

	if (atoi(packet) == -1)
	{
		cp = (char *)index(packet,':');
		if (cp)
		{
			strcpy(msqlErrMsg,cp+1);
			chopError();
		}
		else
		{
			strcpy(msqlErrMsg,UNKNOWN_ERROR);
		}
		return(-1);
	}

	cp = (char *)index(packet,':');
	numFields = 0;
	if (cp)
	{
		numFields = atoi(cp+1);
		if (numFields <= 0)
			return(0);
	}
	else
	{
		return(0);
	}

	/*
	** numFields > 0 therefore we have data waiting on the socket.
	** Grab it and dump it into a table structure.  If there's
	** uncollected data free it - it's no longer available.
	*/
	if (queryData)
	{
		freeQueryData(queryData);
		freeQueryData(fieldData);
		queryData = NULL;
		fieldData = NULL;
	}

	queryTableSize = readQueryData(sock);
	if (queryTableSize < 0)
	{
		return(-1);
	}
	queryData = tmpDataStore;
	tmpDataStore = NULL;
	numFields = 6;
	fieldTableSize = readQueryData(sock);
	if (fieldTableSize < 0)
	{
		return(-1);
	}
	fieldData = tmpDataStore;
	tmpDataStore = NULL;
	return(0);
}




/**************************************************************************
**	_
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

int readQueryData(sock)
	int	sock;
{
	int	off,
		len,
		numRows;
	char	*cp;
	m_data	*cur;
	
	if (readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}

	numRows = 0;
	while(atoi(packet) != -100)
	{
		if (atoi(packet) == -1)
		{
			cp = (char *)index(packet,':');
			if (cp)
			{
				strcpy(msqlErrMsg,cp+1);
				chopError();
			}	
			else
			{
				strcpy(msqlErrMsg,UNKNOWN_ERROR);
			}
			return(-1);
		}
		numRows++;
		if(!tmpDataStore)
		{
			tmpDataStore = cur = (m_data *)malloc(sizeof(m_data));
		}
		else
		{
			cur->next = (m_data *)malloc(sizeof(m_data));
			cur = cur->next;
		}
		msqlDebug(MOD_MALLOC,"Query data row - malloc @ %X of %d\n",
			cur, sizeof(m_data));
		(void)bzero(cur,sizeof(m_data));
		cur->data = (char **)malloc(numFields * sizeof(char *));
		(void)bzero(cur->data,numFields * sizeof(char *));
		cur->width = numFields;
		off = 0;
		cp = packet;
		while(off < numFields)
		{
			len = atoi(cp);
			cp = (char *)index(cp,':');
			if (len == -2)
			{
				cur->data[off] = (char *)NULL;
				cp++;
			}
			else
			{
				cur->data[off] = (char *)malloc(len+1);
				(void)bzero(cur->data[off],len+1);
				(void)bcopy(cp+1,cur->data[off],len);
				cp += len + 1;
			}
			off++;
		}

		if (readPkt(sock) <= 0)
		{
			closeServer(sock);
			strcpy(msqlErrMsg,SERVER_GONE_ERROR);
			return(-1);
		}
	}
	return(numRows);
}






/**************************************************************************
**	_
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

m_result *msqlListDBs(sock)
	int	sock;
{
	m_result *tmp;

	msqlDebug(MOD_API,"msqlListDBs(%d)\n",sock);
	tmp = (m_result *)malloc(sizeof(m_result));
	if (!tmp)
	{
		return(NULL);
	}
	(void)bzero(tmp, sizeof(m_result));
	msqlDebug(MOD_MALLOC,"Result Handle - malloc @ %X of %d\n",
		tmp, sizeof(m_result));
	sprintf(packet,"%d:\n",DB_LIST);
	writePkt(sock);
	numFields = 1;
	tmp->numRows = readQueryData(sock);
	if (tmp->numRows < 0)
	{
		(void)free(tmp);
		return(NULL);
	}
	tmp->queryData = tmpDataStore;
	tmp->cursor = tmp->queryData;
	tmp->numFields = 1;
	tmp->fieldData = (m_fdata *)malloc(sizeof(m_fdata));
	msqlDebug(MOD_MALLOC,"Field List Entry - malloc @ %X of %d\n",
		tmp->fieldData, sizeof(m_fdata));
	(void)bzero(tmp->fieldData, sizeof(m_fdata));
	tmp->fieldData->field.table = (char *)strdup("mSQL Catalog");
	tmp->fieldData->field.name = (char *)strdup("Database");
	tmp->fieldData->field.type = CHAR_TYPE;
	tmp->fieldData->field.length = NAME_LEN;
	tmp->fieldData->field.flags = 0;
	tmp->fieldCursor = tmp->fieldData;
	tmpDataStore = NULL;
	return(tmp);
}





/**************************************************************************
**	_
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

m_result *msqlListTables(sock)
	int	sock;
{
	m_result *tmp;

	msqlDebug(MOD_API,"msqlListTables(%d)\n",sock);
	tmp = (m_result *)malloc(sizeof(m_result));
	if (!tmp)
	{
		return(NULL);
	}
	msqlDebug(MOD_MALLOC,"Result Handle - malloc @ %X of %d\n",
		tmp, sizeof(m_result));
	(void)bzero(tmp, sizeof(m_result));
	sprintf(packet,"%d:\n",TABLE_LIST);
	writePkt(sock);
	numFields = 1;
	tmp->numRows = readQueryData(sock);
	if (tmp->numRows < 0)
	{
		(void)free(tmp);
		return(NULL);
	}
	tmp->queryData = tmpDataStore;
	tmp->numFields = 0;
	tmp->cursor = tmp->queryData;
	tmp->fieldCursor = NULL;
	tmpDataStore = NULL;
	tmp->numFields = 1;
	tmp->fieldData = (m_fdata *)malloc(sizeof(m_fdata));
	msqlDebug(MOD_MALLOC,"Field List Entry - malloc @ %X of %d\n",
		tmp->fieldData, sizeof(m_fdata));
	(void)bzero(tmp->fieldData, sizeof(m_fdata));
	tmp->fieldData->field.table = (char *)strdup("mSQL Catalog");
	tmp->fieldData->field.name = (char *)strdup("Table");
	tmp->fieldData->field.type = CHAR_TYPE;
	tmp->fieldData->field.length = NAME_LEN;
	tmp->fieldData->field.flags = 0;
	tmp->fieldCursor = tmp->fieldData;
	return(tmp);
}


/**************************************************************************
**	_
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

m_result *msqlListFields(sock,table)
	int	sock;
	char	*table;
{
	m_result *tmp;

	msqlDebug(MOD_API,"msqlListFields(%d,%s)\n",sock,table);
	tmp = (m_result *)malloc(sizeof(m_result));
	if (!tmp)
	{
		return(NULL);
	}
	msqlDebug(MOD_MALLOC,"Result Handle - malloc @ %X of %d\n",
		tmp, sizeof(m_result));
	(void)bzero(tmp, sizeof(m_result));
	sprintf(packet,"%d:%s\n",FIELD_LIST,table);
	writePkt(sock);
	numFields = 6;
	tmp->numFields = readQueryData(sock);
	if(tmp->numFields < 0)
	{
		(void)free(tmp);
		return(NULL);
	}
	tmp->fieldData = tableToFieldList(tmpDataStore);
	tmp->fieldCursor = tmp->fieldData;
	tmp->queryData = NULL;
	tmp->cursor = NULL;
	tmp->numRows = 0;
	freeQueryData(tmpDataStore);
	tmpDataStore = NULL;
	return(tmp);
}




int msqlCreateDB(sock,DB)
	int	sock;
	char	*DB;
{
	char	*cp;

	msqlDebug(MOD_API,"msqlCreateDB(%d,%s)\n",sock,DB);
	sprintf(packet,"%d:%s\n",CREATE_DB,DB);
	writePkt(sock);
	(void)bzero(packet,PKT_LEN);
	if(readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}


	/*
	** Check the result.  
	*/

	if (atoi(packet) == -1)
	{
		cp = (char *)index(packet,':');
		if (cp)
		{
			strcpy(msqlErrMsg,cp+1);
			chopError();
		}
		else
		{
			strcpy(msqlErrMsg,UNKNOWN_ERROR);
		}
		return(-1);
	}
	return(0);
}

int msqlDropDB(sock,DB)
	int	sock;
	char	*DB;
{
	char	*cp;

	msqlDebug(MOD_API,"msqlDropDB(%d,%s)\n",sock,DB);
	sprintf(packet,"%d:%s\n",DROP_DB,DB);
	writePkt(sock);
	(void)bzero(packet,PKT_LEN);
	if(readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}


	/*
	** Check the result.  
	*/

	if (atoi(packet) == -1)
	{
		cp = (char *)index(packet,':');
		if (cp)
		{
			strcpy(msqlErrMsg,cp+1);
			chopError();
		}
		else
		{
			strcpy(msqlErrMsg,UNKNOWN_ERROR);
		}
		return(-1);
	}
	return(0);
}


int msqlShutdown(sock)
	int	sock;
{
	char	*cp;

	msqlDebug(MOD_API,"msqlShutdown(%d)\n",sock);
	sprintf(packet,"%d:\n",SHUTDOWN);
	writePkt(sock);
	(void)bzero(packet,PKT_LEN);
	if(readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}


	/*
	** Check the result.  
	*/

	if (atoi(packet) == -1)
	{
		cp = (char *)index(packet,':');
		if (cp)
		{
			strcpy(msqlErrMsg,cp+1);
			chopError();
		}
		else
		{
			strcpy(msqlErrMsg,UNKNOWN_ERROR);
		}
		return(-1);
	}
	return(0);
}



int msqlReloadAcls(sock)
	int	sock;
{
	char	*cp;

	msqlDebug(MOD_API,"msqlReloadAcl(%d)\n",sock);
	sprintf(packet,"%d:\n",RELOAD_ACL);
	writePkt(sock);
	(void)bzero(packet,PKT_LEN);
	if(readPkt(sock) <= 0)
	{
		closeServer(sock);
		strcpy(msqlErrMsg,SERVER_GONE_ERROR);
		return(-1);
	}


	/*
	** Check the result.  
	*/

	if (atoi(packet) == -1)
	{
		cp = (char *)index(packet,':');
		if (cp)
		{
			strcpy(msqlErrMsg,cp+1);
			chopError();
		}
		else
		{
			strcpy(msqlErrMsg,UNKNOWN_ERROR);
		}
		return(-1);
	}
	return(0);
}



char *msqlGetServerInfo()
{
	return(serverInfo);
}


char *msqlGetHostInfo()
{
	return(hostInfo);
}


int msqlGetProtoInfo()
{
	return(protoInfo);
}

