/*
**	msql_priv.h	- Private (internal) definitions
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
** ID = "msql_priv.h,v 1.3 1994/08/19 08:03:14 bambi Exp"
*/

#include "version.h"

#ifdef NEW_DB
#  include <limits.h>
#  include <db.h>
#endif


/***************************************************************************
*	Configuration parameters 
*/



#define MAX_FIELDS	75		/* Max fields per query */
#define MAX_CON		24		/* Max connections */
#define BUF_SIZE	(256*1024)	/* Read buf size if no mmap() */
#define NAME_LEN	19		/* Field/table name length */
#define	PKT_LEN		(32*1024)	/* Max size of client/server packet */
#define	CACHE_SIZE	8		/* Size of table cache */






/***************************************************************************
** Internal structures
*/

/*
** Identifier format.  Multi-part to enable stuff like "emp.name"
*/
typedef struct ident_s {
	char	seg1[NAME_LEN + 2],
		seg2[NAME_LEN + 2];
} ident_t;


/* 
** Field value storage union 
*/
typedef struct val_s {
	union val_u {			
		int	intVal;
		u_char	*charVal;
		double	realVal;
		ident_t	*identVal;
	} val;
	int	type,
		nullVal,
		dataLen;
} val_t;


typedef struct tname_s {
	char	name[NAME_LEN + 2],
		cname[NAME_LEN + 2];
	struct	tname_s *next;
} tname_t;

/* 
** Internal field list element
*/
typedef struct field_ps {		
	char	table[NAME_LEN + 1],
		name[NAME_LEN + 1];
	val_t 	*value;
	int	type,
		length,
		null,
		flags;
		
	struct	field_ps *next;
} field_t;


/* 
** Where clause list element 
*/
typedef struct cond_s {			
	char	table[NAME_LEN + 2],
		name[NAME_LEN + 2];
	val_t 	*value;
	int	op,
		bool,
		type,
		length;
	struct	cond_s *next;
} cond_t;



typedef struct tlist_s {
	char	name[NAME_LEN + 2];
	struct	tlist_s *next;
} tlist_t;


/* 
** Order clause list element 
*/
typedef struct order_s {		
	char	table[NAME_LEN + 2],
		name[NAME_LEN + 2];
	int	dir,
		type,
		length;
	struct	order_s *next;
} order_t;




/* 
** Table cache entry struct 
*/
typedef struct cache_s {		
	char	DB[NAME_LEN + 2],
		cname[NAME_LEN + 2],
		table[NAME_LEN + 2],
		resInfo[4 * (NAME_LEN+1)]; /* used for debugging info */
	u_char	*rowBuf,
		*keyBuf;
	int	age,
		stackFD,
		dataFD,
		keyFD,
		rowLen,
		keyLen,
		result;
	u_int	numRows;
	field_t	*def;

#ifdef	NEW_DB
	DB	*dbp;
#endif

#ifdef	HAVE_MMAP
	int	remapData,
		remapKey,
		remapStack;
	caddr_t	dataMap,
		keyMap,
		stackMap;
	off_t	size,
		keySize,
		stackSize;
#else
	char	readBuf[BUF_SIZE];
        u_int   firstRow, 
		lastRow;

#endif
	
} cache_t;



typedef struct pkey_s {
	char	*table,
		*name;
	val_t	*value;
	int	type,
		length,
		op;
} pkey_t;





typedef struct cinfo_s {
	char    *db,
		*host,
		*user;
	int     access;
	struct  sockaddr_in local, remote;
} cinfo_t;




/***************************************************************************
*	External variables  (var's defined in msql_proc.c)
*/

#ifndef MSQL_ADT


extern	int	command,
		notnullflag,
		keyflag,
		msqlSelectLimit;
extern	char	*arrayLen;
extern	cond_t	*condHead;
extern	field_t	*fieldHead;
extern	order_t	*orderHead;
extern	tlist_t	*tableHead;

#endif





/***************************************************************************
*	YACC stack type
*/
#ifdef YYSTYPE
#  undef YYSTYPE
#endif
typedef char	* C_PTR;
#define YYSTYPE C_PTR




/***************************************************************************
*	Macros and other constants
*/


#define	EQ_OP		1
#define	LT_OP		2
#define	GT_OP		3
#define	NE_OP		4
#define	LE_OP		5
#define GE_OP		6
#define LIKE_OP		7
#define NOT_LIKE_OP	8

#define	NO_BOOL		0
#define	AND_BOOL	1
#define	OR_BOOL		2


#define	MEM_ALIGN	4

#define QUIT		1
#define	INIT_DB		2
#define QUERY		3
#define DB_LIST		4
#define TABLE_LIST	5
#define FIELD_LIST	6
#define	CREATE_DB	7
#define DROP_DB		8
#define RELOAD_ACL	9
#define SHUTDOWN	10

#define NO_REMAP	0
#define DATA_REMAP	1
#define KEY_REMAP	2
#define STACK_REMAP	4
#define FULL_REMAP	0xffff

#define DEST_CLIENT	1
#define DEST_TABLE	2

#define	NO_ACCESS	0
#define	READ_ACCESS	1
#define	WRITE_ACCESS	2
#define RW_ACCESS	READ_ACCESS | WRITE_ACCESS

static char *comTable[] = { 
	"???", "Quit", "Init DB", "Query", "DB List", "Table List", 
	"Field List", "Create DB", "Drop DB", "Reload ACL",
	"Shutdown", "???" };


#define MALLOC_BLK      1
#define MMAP_BLK        2
 
static char *blockTags[] = {
        "ERROR : Unknown block tag",
        "malloc()",
        "mmap()",
        "ERROR : Unknown block tag"
};
 



int writePkt();
int readPkt();


#ifndef _MSQL_SERVER_SOURCE

extern	char	*msqlHomeDir;

#endif



/*
** Inline definitions
*/

/* inlined, unaligned, 4-byte copy */
#define bcopy4(s,d) \
      ((((unsigned char *)d)[3] = ((unsigned char *)s)[3]), \
       (((unsigned char *)d)[2] = ((unsigned char *)s)[2]), \
       (((unsigned char *)d)[1] = ((unsigned char *)s)[1]), \
       (((unsigned char *)d)[0] = ((unsigned char *)s)[0]))
/* inlined, unaligned, 8-byte copy */
#define bcopy8(s,d) \
      ((((unsigned char *)d)[7] = ((unsigned char *)s)[7]), \
       (((unsigned char *)d)[6] = ((unsigned char *)s)[6]), \
       (((unsigned char *)d)[5] = ((unsigned char *)s)[5]), \
       (((unsigned char *)d)[4] = ((unsigned char *)s)[4]), \
       (((unsigned char *)d)[3] = ((unsigned char *)s)[3]), \
       (((unsigned char *)d)[2] = ((unsigned char *)s)[2]), \
       (((unsigned char *)d)[1] = ((unsigned char *)s)[1]), \
       (((unsigned char *)d)[0] = ((unsigned char *)s)[0]))

