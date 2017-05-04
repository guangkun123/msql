/*
**	acl.c	- 
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


#define	ALLOW	1
#define REJECT	2

typedef	struct acc_s {
	char	name[50];
	int	access;
	struct	acc_s *next;
} acc_t;


typedef struct acl_s {
	char	db[NAME_LEN];
	acc_t	*host,
		*read,
		*write;
	tlist_t	*access,
		*option;
	struct	acl_s *next;
} acl_t;



static acl_t	*aclHead = NULL;
static int	accessPerms;

extern  char    *packet;

#define	ERR(msg)	if (verbose) printf msg 

#define	DATABASE	1
#define	READ		2
#define	WRITE		3
#define	HOST		4
#define	ACCESS		5
#define OPTION		6


static int checkToken(tok)
	char	*tok;
{
	if (strcmp(tok,"database") == 0)
		return(DATABASE);
	if (strcmp(tok,"read") == 0)
		return(READ);
	if (strcmp(tok,"write") == 0)
		return(WRITE);
	if (strcmp(tok,"host") == 0)
		return(HOST);
	if (strcmp(tok,"access") == 0)
		return(ACCESS);
	if (strcmp(tok,"option") == 0)
		return(OPTION);
	return(-1);
}



int msqlLoadAcl(verbose)
	int	verbose;
{
	acl_t	*new,
		*aclTail;
	tlist_t	*tNew,
		*tTail;
	acc_t	*accNew,
		*accTail;
	char	buf[1024],
		path[255],
		*tok;
	FILE	*fp;
	int	newEntry,
		lineNum;


	/*
	** Open the acl file
	*/
	(void)sprintf(path,"%s/msql.acl", msqlHomeDir);
	fp = fopen(path,"r");
	if (!fp)
	{
		if (verbose)
		{
			perror("Warning : Couldn't open ACL file");
			fprintf(stderr,"Without an ACL file global access is Read/Write\n\n");
		}
		sprintf(packet,"-1:Warning - Couldn't open ACL file\n");
		return(-1);
	}


	/*
	** Process the file
	*/
	fgets(buf,sizeof(buf),fp);
	newEntry = 1;
	lineNum = 1;
	while(!feof(fp))
	{
		tok = (char *)strtok(buf," \t\n\r=,");
		if (!tok)
		{
			/* Blank line ends a db entry */
			newEntry = 1;
			fgets(buf,sizeof(buf),fp);
			lineNum++;
			continue;
		}
			

		if (*tok == '#')
		{
			/* Comments are skipped */
			fgets(buf,sizeof(buf),fp);
			lineNum++;
			continue;
		}



		switch(checkToken(tok))
		{
		    case DATABASE:
			if (!newEntry)
			{
				ERR(("Bad entry header location at line %d\n",
					lineNum));
				sprintf(packet,
				   "-1:Bad entry header location at line %d\n"
					,lineNum);
				fclose(fp);
				return(-1);
			}
			newEntry = 0;
			tok = (char *)strtok(NULL," \t\n\r");
			if (!tok)
			{
				ERR(("Missing database name at line %d\n",
					lineNum));
				printf(packet,
					"-1:Missing database name at line %d\n",
					lineNum);
				fclose(fp);
				return(-1);
			}
			new = (acl_t *)malloc(sizeof(acl_t));
			(void)bzero(new,sizeof(acl_t));
			(void)strcpy(new->db,tok);
			if (aclHead)
			{
				aclTail->next = new;
				aclTail = new;
			}
			else
			{
				aclHead = aclTail = new;
			}
			break;


		    case READ:
			if (newEntry)
			{
				ERR(("Bad entry header location at line %d\n",
					lineNum));
				sprintf(packet, 
				    "-1:Bad entry header location at line %d\n"
					,lineNum);
				fclose(fp);
				return(-1);
			}
			if (new->read)
			{
				accTail = new->read;
				while(accTail->next)
					accTail = accTail->next;
			}
			else
			{
				accTail = NULL;
			}
			tok = (char *)strtok(NULL," \t,\n\r");
			while(tok)
			{
				accNew = (acc_t *)malloc(sizeof(acc_t));
				(void)bzero(accNew,sizeof(acc_t));
				if (*tok == '-')
				{
					strcpy(accNew->name,tok+1);
					accNew->access = REJECT;
				}
				else
				{
					strcpy(accNew->name,tok);
					accNew->access = ALLOW;
				}
				if (accTail)
				{
					accTail->next = accNew;
				}
				else
				{
					new->read = accNew;
				}
				accTail = accNew;
				tok = (char *)strtok(NULL," \t,\n\r");
			}
			break;


		    case WRITE:
			if (newEntry)
			{
				ERR(("Bad entry header location at line %d\n",
					lineNum));
				sprintf(packet,
				    "-1:Bad entry header location at line %d\n"
					,lineNum);
				fclose(fp);
				return(-1);
			}
			if (new->write)
			{
				accTail = new->write;
				while(accTail->next)
					accTail = accTail->next;
			}
			else
			{
				accTail = NULL;
			}
			tok = (char *)strtok(NULL," \t,\n\r");
			while(tok)
			{
				accNew = (acc_t *)malloc(sizeof(acc_t));
				(void)bzero(accNew,sizeof(acc_t));
				if (*tok == '-')
				{
					strcpy(accNew->name,tok+1);
					accNew->access = REJECT;
				}
				else
				{
					strcpy(accNew->name,tok);
					accNew->access = ALLOW;
				}
				if (accTail)
				{
					accTail->next = accNew;
				}
				else
				{
					new->write = accNew;
				}
				accTail = accNew;
				tok = (char *)strtok(NULL," \t,\n\r");
			}
			break;


		    case HOST:
			if (newEntry)
			{
				ERR(("Bad entry header location at line %d\n",
					lineNum));
				sprintf(packet,
				    "-1:Bad entry header location at line %d\n"
					,lineNum);
				fclose(fp);
				return(-1);
			}
			if (new->host)
			{
				accTail = new->host;
				while(accTail->next)
					accTail = accTail->next;
			}
			else
			{
				accTail = NULL;
			}
			tok = (char *)strtok(NULL," \t,\n\r");
			while(tok)
			{
				accNew = (acc_t *)malloc(sizeof(acc_t));
				(void)bzero(accNew,sizeof(acc_t));
				if (*tok == '-')
				{
					strcpy(accNew->name,tok+1);
					accNew->access = REJECT;
				}
				else
				{
					strcpy(accNew->name,tok);
					accNew->access = ALLOW;
				}
				if (accTail)
				{
					accTail->next = accNew;
				}
				else
				{
					new->host = accNew;
				}
				accTail = accNew;
				tok = (char *)strtok(NULL," \t,\n\r");
			}
			break;


		    case ACCESS:
			if (newEntry)
			{
				ERR(("Bad entry header location at line %d\n",
					lineNum));
				sprintf(packet,
				    "-1:Bad entry header location at line %d\n"
					,lineNum);
				fclose(fp);
				return(-1);
			}
			if (new->access)
			{
				tTail = new->access;
				while(tTail->next)
					tTail = tTail->next;
			}
			else
			{
				tTail = NULL;
			}
			tok = (char *)strtok(NULL," \t,\n\r");
			while(tok)
			{
				tNew = (tlist_t *)malloc(sizeof(tlist_t));
				(void)bzero(tNew,sizeof(tlist_t));
				strcpy(tNew->name,tok);
				if (tTail)
				{
					tTail->next = tNew;
				}
				else
				{
					new->access = tNew;
				}
				tTail = tNew;
				tok = (char *)strtok(NULL," \t,\n\r");
			}
			break;


		    case OPTION:
			if (newEntry)
			{
				ERR(("Bad entry header location at line %d\n",
					lineNum));
				sprintf(packet,
				    "-1:Bad entry header location at line %d\n"
					,lineNum);
				fclose(fp);
				return(-1);
			}
			if (new->option)
			{
				tTail = new->option;
				while(tTail->next)
					tTail = tTail->next;
			}
			else
			{
				tTail = NULL;
			}
			tok = (char *)strtok(NULL," \t,\n\r");
			while(tok)
			{
				tNew = (tlist_t *)malloc(sizeof(tlist_t));
				(void)bzero(tNew,sizeof(tlist_t));
				strcpy(tNew->name,tok);
				if (tTail)
				{
					tTail->next = tNew;
				}
				else
				{
					new->option = tNew;
				}
				tTail = tNew;
				tok = (char *)strtok(NULL," \t,\n\r");
			}
			break;


		    default:
			ERR(("Unknown ACL command \"%s\" at line %d\n", 
				tok,lineNum));
			sprintf(packet,
				"-1:Unknown ACL command \"%s\" at line %d\n", 
				tok,lineNum);
			fclose(fp);
			return(-1);
			break;

		}

		fgets(buf,sizeof(buf),fp);
		lineNum++;
	}
	fclose(fp);
	return(0);
}


dumpAcc(acc)
	acc_t	*acc;
{
	while(acc)
	{
		printf("\t\t%s %s\n",(acc->access==ALLOW)?"Yes":"No ",
			acc->name);
		acc = acc->next;
	}
}


static dumpAcl()
{
	acl_t	*curAcl;
	tlist_t	*curT;
	acc_t	*curAcc;

	curAcl = aclHead;
	while(curAcl)
	{
		printf("\nACL's for Database %s\n",curAcl->db);
		printf("\tRead access :-\n");
		dumpAcc(curAcl->read);
		printf("\tWrite access :-\n");
		dumpAcc(curAcl->write);
		printf("\tHost access :-\n");
		dumpAcc(curAcl->host);

		curAcl = curAcl->next;
	}
}



static int matchToken(pattern,tok)
	char	*pattern,
		*tok;
{
	register char 	*cp;
	char	*buf1,
		*buf2,
		*cp2;
	int	length1,
		length2;

	/*
	** Put everything to lower case
	*/
	buf1 = (char *)strdup(pattern);
	buf2 = (char *)strdup(tok);

	cp = buf1;
	while(*cp)
	{
		*cp = tolower(*cp);
		cp++;
	}

	cp = buf2;
	while(*cp)
	{
		*cp = tolower(*cp);
		cp++;
	}
	
	/*
	** Is this a wildcard?
	*/
	cp = pattern;
	if (*cp == '*')
	{
		if (*(cp+1) == 0)	/* match anything */
		{
			(void)free(buf1);
			(void)free(buf2);
			return(1);
		}
		length1 = strlen(cp)-1;
		length2 = strlen(tok);
		if (length1 > length2)
		{
			(void)free(buf1);
			(void)free(buf2);
			return(0);
		}
		cp2 = buf2 + length2 - length1;
		if (strcmp(cp+1,cp2) == 0)
		{
			(void)free(buf1);
			(void)free(buf2);
			return(1);
		}
		else
		{
			(void)free(buf1);
			(void)free(buf2);
			return(0);
		}
	}

	/*
	** OK, does the actual text match
	*/
	if (strcmp(buf1,buf2) == 0)
	{
		(void)free(buf1);
		(void)free(buf2);
		return(1);
	}
	else
	{
		(void)free(buf1);
		(void)free(buf2);
		return(0);
	}
}



static int matchTextList(list,tok)
	tlist_t	*list;
	char	*tok;
{
	tlist_t	*cur;

	cur = list;
	while(cur)
	{
		if (matchToken(cur->name, tok))
		{
			return(1);
		}
		cur = cur->next;
	}
	return(0);
}



static int matchAccessList(list,tok)
	acc_t	*list;
	char	*tok;
{
	acc_t	*cur;

	cur = list;
	while(cur)
	{
		if (matchToken(cur->name, tok))
		{
			if (cur->access == ALLOW)
			{
				return(1);
			}
			else
			{
				return(0);
			}
		}
		cur = cur->next;
	}
	return(0);
}



int msqlCheckAccess(db,info)
	char	*db;
	cinfo_t	*info;
{
	char 	*host,
		*user;
	struct  sockaddr *local, 
		*remote;
	acl_t	*curAcl;
	acc_t	*curAcc;
	int	res,
		perms;


	host = info->host;
	user = info->user;
	local = (struct sockaddr *)&(info->local);
	remote = (struct sockaddr *)&(info->remote);
	
	/*
	** Find an ACL entry that matches this DB
	*/
	curAcl = aclHead;
	while(curAcl)
	{
		if(matchToken(curAcl->db,db))
		{
			break;
		}
		curAcl = curAcl->next;
	}

	if (!curAcl)
	{
		return(RW_ACCESS);	/* default if no specific ACL */
	}

	/*
	** Now check the connection details
	*/
	if (!host)
	{
		/* No host == local connection */
		
		if (!matchTextList(curAcl->access,"local"))
		{
			return(NO_ACCESS);
		}
	}
	else
	{
		if (!matchTextList(curAcl->access,"remote"))
		{
			return(NO_ACCESS);
		}
		if (!matchAccessList(curAcl->host,host))
		{
			return(NO_ACCESS);
		}
	}

	/*
	** Now check the access perms
	*/
	perms = 0;
	if (matchAccessList(curAcl->read, user))
	{
		perms |= READ_ACCESS;
	}
	if (matchAccessList(curAcl->write, user))
	{
		perms |= WRITE_ACCESS;
	}
	if (perms == 0)
	{
		return(NO_ACCESS);
	}


	/*
	** Now perform any options for this connection
	*/

	
	return(perms);
}



static freeAcc(head)
	acc_t	*head;
{
	register acc_t	*curAcc,
			*prevAcc;

	curAcc = head;
	while(curAcc)
	{
		prevAcc = curAcc;
		curAcc = curAcc->next;
		(void)free(prevAcc);
	}
}

static freeTlist(head)
	tlist_t	*head;
{
	register tlist_t	*curText,
			*prevText;

	curText = head;
	while(curText)
	{
		prevText = curText;
		curText = curText->next;
		(void)free(prevText);
	}
}


static freeAcls()
{
	register acl_t	*curAcl,
			*prevAcl;

	curAcl = aclHead;
	while(curAcl)
	{
		freeAcc(curAcl->host);
		freeAcc(curAcl->read);
		freeAcc(curAcl->write);
		freeTlist(curAcl->access);
		freeTlist(curAcl->option);
		prevAcl = curAcl;
		curAcl = curAcl->next;
		(void)free(prevAcl);
	}
	aclHead = NULL;
}

reloadAcls(sock)
	int	sock;
{
	freeAcls();
	msqlLoadAcl(0);
}


msqlSetPerms(perms)
	int	perms;
{
	accessPerms = perms;
}

int msqlCheckPerms(access)
	int	access;
{
	return( (accessPerms & access) == access);
}



int msqlCheckLocal(info)
	cinfo_t	*info;
{
	char 	*host,
		*user;
	struct  sockaddr *local, 
		*remote;
	acl_t	*curAcl;
	acc_t	*curAcc;
	int	res,
		perms;


	host = info->host;
	user = info->user;
	local = (struct sockaddr *)&(info->local);
	remote = (struct sockaddr *)&(info->remote);
	if ( (!info->host || strcmp(host,"localhost") == 0) &&
	     strcmp(info->user,ROOT) == 0)
	{
		return(1);
	}
	else
	{
		return(0);
	}
}
