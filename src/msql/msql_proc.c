/*
**	msql_proc.c	- 
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


#ifndef lint
static char RCS_id[] = 
	"msql_proc.c,v 1.3 1994/08/19 08:03:15 bambi Exp";
#endif 


#include <stdio.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>


#include <common/debug.h>
#include <common/portability.h>

#define	MSQL_ADT
#include "msql_priv.h"
#include "msql.h"
#include "y.tab.h"

int     command,
	notnullflag,
	keyflag,
	outSock;

char 	*curDB,
	*arrayLen;

cond_t	*condHead = NULL;

field_t	*fieldHead = NULL,
	*lastField = NULL;

order_t	*orderHead = NULL;

tlist_t	*tableHead = NULL;

static	cond_t	*condTail = NULL;
static	field_t	*fieldTail = NULL;
static	order_t	*orderTail = NULL;
static	tlist_t	*tableTail = NULL;
static 	int	havePriKey = 0;


#define	malloc(L)	Malloc(L,__FILE__,__LINE__)
#define	free(A)		Free(A,__FILE__,__LINE__)
#define	safeFree(x)	{if (x) { (void) free(x); x = NULL; }}
#define REG		register


extern	char	*packet;



/****************************************************************************
** 	_msqlClean - clean out the internal structures
**
**	Purpose	: Free all space and reset structures after a query
**	Args	: None
**	Returns	: Nothing
**	Notes	: Updates all public data structures
*/

msqlClean()
{
	register field_t	*curField, *tmpField;
	register cond_t		*curCond, *tmpCond;
	register order_t 	*curOrder, *tmpOrder;
	register tlist_t	*curTable, *tmpTable;

	msqlTrace(TRACE_IN,"msqlClean()");
	command = 0;
	havePriKey = 0;

	/*
	** blow away the table list from the query
	*/
	curTable = tableHead;
	while(curTable)
	{
		tmpTable = curTable;
		curTable = curTable->next;
		(void)free(tmpTable);
	}


	/*
	** blow away the field list from the query
	*/
	curField = fieldHead;
	while(curField)
	{
		freeValue(curField->value);
		tmpField = curField;
		curField = curField->next;
		(void)free(tmpField);
	}

	/*
	** Blow away the condition list from the query
	*/
	curCond = condHead;
	while(curCond)
	{
		freeValue(curCond->value);
		curCond->op = curCond->bool = 0;
		tmpCond = curCond;
		curCond = curCond->next;
		(void)free(tmpCond);
	}
	curOrder = orderHead;
	while(curOrder)
	{
		curOrder->dir = 0;
		tmpOrder = curOrder;
		curOrder = curOrder->next;
		(void)free(tmpOrder);
	}


	/*
	** Reset the list pointers
	*/

	condHead = condTail = (cond_t *) NULL;
	fieldHead = fieldTail = lastField = (field_t *) NULL;
	orderHead = orderTail = (order_t *) NULL;
	tableHead = tableTail = (tlist_t *) NULL;

	msqlBackendClean();

	msqlTrace(TRACE_OUT,"msqlClean()");
}



ident_t *msqlCreateIdent(seg1,seg2)
	char	*seg1,
		*seg2;
{
	ident_t	*new;

	msqlTrace(TRACE_IN,"msqlCreateIdent()");
	if (seg1)
	{
		if (strlen(seg1) > NAME_LEN)
		{
			sprintf(packet,
				"-1:Identifier name '%s' too long\n",seg1);
			writePkt(outSock);
			msqlTrace(TRACE_OUT,"msqlCreateIdent()");
			return(NULL);
		}
	}
	if (seg2)
	{
		if (strlen(seg2) > NAME_LEN)
		{
			sprintf(packet,
				"-1:Identifier name '%s' too long\n",seg2);
			writePkt(outSock);
			msqlTrace(TRACE_OUT,"msqlCreateIdent()");
			return(NULL);
		}
	}
	new = (ident_t *)malloc(sizeof(ident_t));
	if (seg1)
	{
		(void)strcpy(new->seg1,seg1);
	}
	if (seg2)
	{
		(void)strcpy(new->seg2,seg2);
	}
	msqlTrace(TRACE_OUT,"msqlCreateIdent()");
	return(new);
}



static u_char expandEscape(c,remain)
	u_char	*c;
	int	remain;
{
	u_char	ret;

	switch(*c)
	{
		case 'n':
			ret = '\n';
			break;
		case 't':
			ret = '\t';
			break;
		case 'r':
			ret = '\r';
			break;
		case 'b':
			ret = '\b';
			break;
		default:
			ret = *c;
			break;
	}
	return(ret);
}



val_t *msqlCreateValue(textRep,type,tokLen)
	u_char	*textRep;
	int	type,
		tokLen;
{
	val_t	*new;
	int	length,
		remain,
		match;
	REG 	u_char	*cp,
			*cp2;
	static	u_char nullVal[] = "null";

	msqlTrace(TRACE_IN,"msqlCreateValue()");
	new = (val_t *)malloc(sizeof(val_t));
	new->type = type;
	new->dataLen = tokLen;
	switch(type)
	{
		case NULL_TYPE:
			new->nullVal = 1;
			break;
		case IDENT_TYPE:
			new->val.identVal = (ident_t *)textRep;
			break;

		case CHAR_TYPE:
			remain = length = tokLen - 2;
			new->val.charVal = (u_char *)malloc(length+1);
			cp = textRep+1;
			cp2 = new->val.charVal;
			while(remain)
			{
				if (*cp == '\\')
				{
					remain--;
					*cp2 = expandEscape(++cp,remain);
					if (*cp2)
					{
						cp2++;
						cp++;
						remain--;
					}
				}
				else
				{
					*cp2++ = *cp++;
					remain--;
				}
			}
			break;

		case INT_TYPE:
			new->val.intVal = atoi(textRep);
			break;

		case REAL_TYPE:
			sscanf((char *)textRep ,"%lf",&new->val.realVal);
			break;
	}
	msqlTrace(TRACE_OUT,"msqlCreateValue()");
	return(new);
}


val_t *fillValue(val,type,length)
	char	*val;
	int	type,
		length;
{
	val_t	*new;

	msqlTrace(TRACE_IN,"fillValue()");
	new = (val_t *)malloc(sizeof(val_t));
	new->type = type;
	new->nullVal = 0;
	switch(type)
	{
		case CHAR_TYPE:
			new->val.charVal = (u_char *)malloc(length+1);
			(void)bcopy(val,new->val.charVal,length);
			break;

		case INT_TYPE:
#ifndef _CRAY
			new->val.intVal = (int) * (int *)val;
#else
			new->val.intVal = unpackInt32(val);
#endif
			break;

		case REAL_TYPE:
			new->val.realVal = (double) * (double *)val;
			break;
	}
	msqlTrace(TRACE_OUT,"fillValue()");
	return(new);
}


val_t *nullValue()
{
	val_t	*new;

	new = (val_t *)malloc(sizeof(val_t));
	new->nullVal = 1;
	return(new);
}



freeValue(val)
	val_t	*val;
{
	msqlTrace(TRACE_IN,"freeValue()");
	if (!val)
	{
		msqlTrace(TRACE_OUT,"freeValue()");
		return;
	}
	switch(val->type)
	{
		case IDENT_TYPE:
			(void)free(val->val.identVal);
			break;
		case CHAR_TYPE:
			if (!val->nullVal)
				(void)free(val->val.charVal);
			break;
	}
	(void)free(val);
	msqlTrace(TRACE_OUT,"freeValue()");
}



/****************************************************************************
** 	_msqlAddField - add a field definition to the list
**
**	Purpose	: store field details from the query for later use
**	Args	: field name, field type, field length, value
**	Returns	: Nothing
**	Notes	: Depending on the query in process, only some of the
**		  args will be supplied.  eg. a SELECT doesn't use the
**		  type arg.  The length arg is only used during a create
**		  if the field is of type CHAR
*/

int msqlAddField(ident,type,length,notNull,priKey)
	ident_t	*ident;
	int 	type;
	char	*length;
	int	notNull,
		priKey;
{
	register field_t	*new;
	char	*name,
		*table;

	msqlTrace(TRACE_IN,"msqlAddField()");

	if (priKey)
	{
		if (havePriKey)
		{
			sprintf(packet,"-1:Can't have multiple primary keys\n");
			writePkt(outSock);
			msqlTrace(TRACE_OUT,"msqlAddField()");
			return(-1);

		}
		else
		{
			havePriKey = 1;
		}
	}


	if (*(ident->seg2))
	{
		name = ident->seg2;
		table = ident->seg1;
	}
	else
	{
		name = ident->seg1;
		table = NULL;
	}
	new = (field_t *)malloc(sizeof(field_t));
	if (table)
	{
		(void)strncpy(new->table,table,NAME_LEN - 1);
	}
	(void)strncpy(new->name,name,NAME_LEN - 1);
	if (notNull)
	{
		new->flags |= NOT_NULL_FLAG;
	}
	if (priKey)
	{
		new->flags |= PRI_KEY_FLAG;
		new->flags |= NOT_NULL_FLAG;
	}
	switch(type)
	{
		case INT:
			new->type = INT_TYPE;
			new->length = 4;
			break;

		case CHAR:
			new->type = CHAR_TYPE;
			new->length = atoi(length);
			break;

		case REAL:
			new->type = REAL_TYPE;
			new->length = sizeof(double);
			break;

		default:
			new->type = 0;
			new->length = 0;
			break;
	}
	if (!fieldHead)
	{
		fieldHead = fieldTail = new;
	}
	else
	{
		fieldTail->next = new;
		fieldTail = new;
	}
	free(ident);
	msqlTrace(TRACE_OUT,"msqlAddField()");
	return(0);
}


msqlAddFieldValue(value)
	val_t	*value;
{
	register field_t	*fieldVal;
	u_char	*buf;

	msqlTrace(TRACE_IN,"msqlAddFieldValue()");
	if (!lastField)
	{
		lastField = fieldVal = fieldHead;
	}
	else
	{	
		fieldVal = lastField->next;
		lastField = lastField->next;
	}
	if (fieldVal)
	{
		if (fieldVal->type == CHAR_TYPE)
		{
			buf = (u_char *)malloc(fieldVal->length+1);
			bcopy(value->val.charVal,buf,value->dataLen);
			free(value->val.charVal);
			value->val.charVal = buf;
		}
		fieldVal->value = value;
	}
	msqlTrace(TRACE_OUT,"msqlAddFieldValue()");
}




/****************************************************************************
** 	_msqlAddCond  -  add a conditional spec to the list
**
**	Purpose	: Store part of a "where_clause" for later use
**	Args	: field name, test op, value, bool (ie. AND | OR)
**	Returns	: Nothing
**	Notes	: the BOOL field is only provided if this is not the first
**		  element of a where_clause.
*/

msqlAddCond(ident,op,value,bool)
	ident_t	*ident;
	int	op;
	val_t	*value;
	int	bool;
{
	register cond_t	*new;
	char	*name,
		*table;

	msqlTrace(TRACE_IN,"msqlAddCond()");

	if (*(ident->seg2))
	{
		name = ident->seg2;
		table = ident->seg1;
	}
	else
	{
		name = ident->seg1;
		table = NULL;
	}

	new = (cond_t *)malloc(sizeof(cond_t));
	(void)strcpy(new->name,name);
	if (table)
	{
		(void)strcpy(new->table,table);
	}
	
	new->op = op;
	new->bool = bool;
	new->value = value;

	if (!condHead)
	{
		condHead = condTail = new;
	}
	else
	{
		condTail->next = new;
		condTail = new;
	}
	free(ident);
	msqlTrace(TRACE_OUT,"msqlAddCond()");
}



/****************************************************************************
** 	_msqlAddOrder  -  add an order definition to the list
**
**	Purpose	: Store part of an "order_clause"
**	Args	: field name, order direction (ie. ASC or DESC)
**	Returns	: Nothing
**	Notes	: 
*/

msqlAddOrder(ident,dir)
	ident_t	*ident;
	int	dir;
{
	register order_t	*new;

	msqlTrace(TRACE_IN,"msqlAddOrder()");

	new = (order_t *)malloc(sizeof(order_t));
	if (*ident->seg1)
	{
		(void)strcpy(new->table,ident->seg1);
	}
	(void)strcpy(new->name,ident->seg2);
	new->dir = dir;
	if (!orderHead)
	{
		orderHead = orderTail = new;
	}
	else
	{
		orderTail->next = new;
		orderTail = new;
	}
	free(ident);
	msqlTrace(TRACE_OUT,"msqlAddOrder()");
}




msqlAddTable(name)
	char	*name;
{
	register tlist_t	*new;

	msqlTrace(TRACE_IN,"msqlAddTable()");

	new = (tlist_t *)malloc(sizeof(tlist_t));
	(void)strcpy(new->name,name);
	(void)free(name);
	if (!tableHead)
	{
		tableHead = tableTail = new;
	}
	else
	{
		tableTail->next = new;
		tableTail = new;
	}
	msqlTrace(TRACE_OUT,"msqlAddTable()");
}







msqlSetDB(db)
	char	*db;
{
	curDB = db;
}



/****************************************************************************
** 	_msqlProcessQuery  -  send to query to the approp routine
**
**	Purpose	: Call the required routine to build the native query
**	Args	: None
**	Returns	: Nothing
**	Notes	: Global command variable used.  This is called when the
**		  end of an individual query is found in the miniSQL code
**		  and provides the hooks into the per-database routines.
*/



msqlProcessQuery()
{
	int	res;

	msqlTrace(TRACE_IN,"msqlProcessQuery()");
	if (!curDB)
	{
		sprintf(packet,"-1:No current database\n");
		writePkt(outSock);
		msqlDebug(MOD_ERR,"No current database\n");
		msqlTrace(TRACE_OUT,"msqlProcessQuery()");
		return;
	}
	switch(command)
	{
		case SELECT: 
			setProcTitle("Q=Select");
			if (!msqlCheckPerms(READ_ACCESS))
			{
				sprintf(packet,"-1:Access Denied\n");
				writePkt(outSock);
				msqlTrace(TRACE_OUT,"msqlProcessQuery()");
				return;
			}
			res = msqlSelect(tableHead,fieldHead,condHead,
				orderHead,curDB);
			break;
		case CREATE: 
			setProcTitle("Q=Create");
			if (!msqlCheckPerms(WRITE_ACCESS))
			{
				sprintf(packet,"-1:Access Denied\n");
				writePkt(outSock);
				msqlTrace(TRACE_OUT,"msqlProcessQuery()");
				return;
			}
			res = msqlCreate(tableHead->name,fieldHead,curDB);
			break;
		case UPDATE: 
			setProcTitle("Q=Update");
			if (!msqlCheckPerms(RW_ACCESS))
			{
				sprintf(packet,"-1:Access Denied\n");
				writePkt(outSock);
				msqlTrace(TRACE_OUT,"msqlProcessQuery()");
				return;
			}
			res = msqlUpdate(tableHead->name,fieldHead,condHead,
				curDB);
			break;
		case INSERT: 
			setProcTitle("Q=Insert");
			if (!msqlCheckPerms(WRITE_ACCESS))
			{
				sprintf(packet,"-1:Access Denied\n");
				writePkt(outSock);
				msqlTrace(TRACE_OUT,"msqlProcessQuery()");
				return;
			}
			res = msqlInsert(tableHead->name,fieldHead,curDB);
			break;
		case DELETE: 
			setProcTitle("Q=Delete");
			if (!msqlCheckPerms(WRITE_ACCESS))
			{
				sprintf(packet,"-1:Access Denied\n");
				writePkt(outSock);
				msqlTrace(TRACE_OUT,"msqlProcessQuery()");
				return;
			}
			res = msqlDelete(tableHead->name,condHead,curDB);
			break;
		case DROP: 
			setProcTitle("Q=Drop");
			if (!msqlCheckPerms(WRITE_ACCESS))
			{
				sprintf(packet,"-1:Access Denied\n");
				writePkt(outSock);
				msqlTrace(TRACE_OUT,"msqlProcessQuery()");
				return;
			}
			res = msqlDrop(tableHead->name,curDB);
			break;
	}
	if (res < 0)
	{
		extern	char	errMsg[];
		char	errBuf[180];

		sprintf(packet,"-1:%s\n",errMsg);
		msqlTrace(TRACE_OUT,"msqlProcessQuery()");
		writePkt(outSock);
	}
	msqlTrace(TRACE_OUT,"msqlProcessQuery()");
}


msqlQueryOverrunError(txt)
	char	*txt;
{

	msqlTrace(TRACE_IN,"msqlQueryOverrunError()");
	sprintf(packet,"-1:Syntax error.  Bad text after query. '%s'\n",txt);
	writePkt(outSock);
	msqlTrace(TRACE_OUT,"msqlQueryOverrunError()");
}




char *msqlParseQuery(inBuf,sock)
        char    *inBuf;
        int     sock;
{
	msqlTrace(TRACE_IN,"msqlParseQuery()");
        outSock = sock;
        msqlInitScanner(inBuf);
        yyparse();
	msqlTrace(TRACE_OUT,"msqlParseQuery()");
}

