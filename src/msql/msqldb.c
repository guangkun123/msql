/*	msqldb.c	- 
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
** The software may be modified for your own purposes, but modified versions
** may not be distributed.
**
** This software is provided "as is" without any expressed or implied warranty.
**
** ID = "$Id:"
**
*/

/*
** Notes for people hacking on this code.
**
**	o A cache holding the details of the N most recently used tables
**	  is maintained.  Details include the table structure, FD's for
**	  the files on disk, pointers to mmap()ed if mmap() in use, a
**	  malloced area the size of a table row, and variaous other
**	  bits that are of use.  See cache_t in msql_priv.h for more
**	  info.  The size of the cache is also set there (must watch
**	  the number of entries as we can easily run out of FD's)
**
**	o The format of a row in a table is 
**		<ActiveByte>[<NullByte><Value>]*
**	  The active byte will be 1 if the row is in use or 0 if it's a
**	  hole.  The null byte will be 1 if the value is NULL, 0
**	  otherwise.
**
**	o Given the above, a row starts at offset the combined length
**	  of all the fields + 1*number of fields + 1.
*/



#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>


#ifdef HAVE_MMAP
#  include <sys/mman.h>
#endif

#ifdef HAVE_DIRENT_H
#  include <dirent.h>
#endif

#ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
#endif

#include <common/debug.h>
#include <common/site.h>
#include <common/portability.h>
#include <regexp/regexp.h>

#include "y.tab.h"

#define	_MSQL_SERVER_SOURCE

#include "msql_priv.h"
#include "msql.h"
#include "errmsg.h"

#define NO_POS		0xFFFFFFFF
#define	REG		register





static	cache_t	tableCache[CACHE_SIZE];
static	char	*qSortRowBuf;

extern	int	outSock;
extern	char	*packet;

int	selectWildcard = 0,
	selectDistinct = 0;

char	errMsg[200];
char 	readKeyBuf[BUF_SIZE];	/* Global to get it out of the stack frame */
char	*msqlHomeDir;

char	*readRow();
cache_t *loadTableDef();


#define	MEM_ALLOC	1
#define MEM_DEALLOC	2
#define MEM_KILL_AGE	500

typedef struct _memBlock {
	char	allocFile[40],
		deallocFile[40];
	int	allocLine,
		deallocLine,
		type,
		status,
		age;
	caddr_t	addr;
	off_t	size;
	struct _memBlock *next;
} memBlock;

static	memBlock *memHead = NULL;



pushBlock(file,line,type,addr,size)
	char	*file;
	int	line,
		type;
	caddr_t	addr;
	off_t	size;
{
	memBlock *new;

	new = (memBlock *)malloc(sizeof(memBlock));
	if (!new)
	{
		perror("malloc() :");
		return;
	}
	(void)bzero(new,sizeof(memBlock));
	(void)strcpy(new->allocFile,file);
	new->allocLine = line;
	new->type = type;
	new->addr = addr;
	new->size = size;
	new->status = MEM_ALLOC;
	if (memHead)
	{
		new->next = memHead;
	}
	else
	{
		new->next = NULL;
	}
	memHead = new;
}


dropBlock(file,line,addr,type)
	char	*file;
	int	line;
	caddr_t	addr;
	int	type;
{
	memBlock *cur,
		 *prev = NULL;
	int	found = 0;

	cur = memHead;
	while(cur)
	{
		if(cur->status == MEM_DEALLOC)
		{
			cur->age++;
		}
		if (cur->age > MEM_KILL_AGE)
		{
			if (prev)
			{
				prev->next = cur->next;
			}
			else
			{
				memHead = cur->next;
			}
			if (type == MMAP_BLK)
			{
#ifdef HAVE_MMAP
				munmap(cur->addr,cur->size);
				cur->addr = NULL;
#endif
			}
			else
			{
				if (cur->addr)
				{
					free(cur->addr);
					cur->addr = NULL;
				}
				
			}
			free(cur);
			cur = prev->next;
			continue;
		}
		if (cur->addr == addr && cur->type == type)
		{
			if (cur->status == MEM_ALLOC)
			{
				(void)strcpy(cur->deallocFile,file);
				cur->deallocLine = line;
				cur->status = MEM_DEALLOC;
			}
			else
			{
				msqlDebug(0xffff,"Error: Muliple deallocation\n");
				msqlDebug(0xffff,"\t%u bytes at %X\n",
					cur->size, cur->addr);
				msqlDebug(0xffff,"\tAllocated at %s:%d\n",
					cur->allocFile, cur->allocLine);
				msqlDebug(0xffff,"\tDeallocated at %s:%d\n",
					cur->deallocFile, cur->deallocLine);
				abort();
			}
			found = 1;
		}
		prev = cur;
		cur = cur->next;
	}
	if (!found)
	{
		msqlDebug(0xffff,"Error : drop of unknown memory block (%X)\n",
			addr);
	}
}



checkBlocks(type)
	int	type;
{
	memBlock *cur;
	int	total,
		count;

	cur = memHead;
	total = count = 0;
	while(cur)
	{
		if (cur->status == MEM_DEALLOC)
		{
			cur = cur->next;
			continue;
		}
		total++;
		if (cur->type == type)
		{
			count++;
			msqlDebug(0xffff,"%s leak :-\n", blockTags[type]);
			msqlDebug(0xffff,"\t%u bytes at %X\n",
				cur->size, cur->addr);
			msqlDebug(0xffff,"\tBlock created at %s:%d\n\n",
				cur->allocFile, cur->allocLine);
		}
		cur = cur->next;
	}
	msqlDebug(0xffff,"Found %d leaked blocks of which %d where %s blocks\n",
		total,count,blockTags[type]);
}




#ifdef HAVE_MMAP

caddr_t MMap(addr, len, prot, flags, fd, off,file,line)
	caddr_t addr;
	size_t len;
	int prot, flags, fd;
	off_t off;
	char	*file;
	int	line;
{
	caddr_t dest;

	dest = mmap(addr,len,prot,flags,fd,off);
	msqlDebug(MOD_MMAP,"mmap'ing %u bytes at %X (%s:%d)\n",(unsigned)len,
		dest,file,line);
	if (dest == (caddr_t)-1)
	{
		perror("mmap");
	}
	if (debugSet(MOD_MMAP))
		pushBlock(file,line,MMAP_BLK,dest,len);
	return(dest);
}

int MUnmap(addr,len,file,line)
	caddr_t addr;
	size_t len;
	char	*file;
	int	line;
{
	int	res = 0;

	msqlDebug(MOD_MMAP,"munmap'ing %u bytes from %X (%s:%d)\n",
		(unsigned)len,addr, file, line);
	if (debugSet(MOD_MMAP))
		dropBlock(file,line,addr,MMAP_BLK);
	else
		res = munmap(addr,len);
	if (res < 0)
	{
		perror("munmap");
	}
	return(res);
}

#define mmap(a,l,p,fl,fd,o)	MMap(a,(size_t)l,p,fl,fd,o,__FILE__,__LINE__)
#define munmap(a,l) 		MUnmap(a,(size_t)l,__FILE__,__LINE__)


#endif


char *FastMalloc(size,file,line)
	int	size;
	char	*file;
	int	line;
{
	char	*cp;

	cp = (char *)malloc(size);
	msqlDebug(MOD_MALLOC,"Allocating %d bytes at %X (%s:%d)\n",size,cp,
		file,line);
	if (size > 1000000)
	{
		msqlDebug(MOD_MALLOC,"Huge malloc trapped!\n");
		abort();
	}
	if (debugSet(MOD_MALLOC))
		pushBlock(file,line,MALLOC_BLK,cp,size);
	return(cp);
}


char *Malloc(size,file,line)
	int	size;
	char	*file;
	int	line;
{
	char	*cp;
	REG	char *cp1;

	cp = (char *)malloc(size);
	if (cp)
	{
		bzero(cp,size);
	}
	msqlDebug(MOD_MALLOC,"Allocating %d bytes at %X (%s:%d)\n",size,cp,
		file,line);
	if (size > 1000000)
	{
		msqlDebug(MOD_MALLOC,"Huge malloc trapped!\n");
		abort();
	}
	if (debugSet(MOD_MALLOC))
		pushBlock(file,line,MALLOC_BLK,cp,size);
	return(cp);
}

void Free(addr, file,line)
	char	*addr,
		*file;
	int	line;
{
	msqlDebug(MOD_MALLOC,"Freeing address %X (%s:%d)\n",addr,
		file,line);
	if (debugSet(MOD_MALLOC))
		dropBlock(file,line,addr,MALLOC_BLK);
	else
		(void) free(addr);
}


#define malloc(s)		Malloc(s,__FILE__,__LINE__)
#define fastMalloc(s)		FastMalloc(s,__FILE__,__LINE__)
#define free(a)			Free(a,__FILE__,__LINE__)

#define	safeFree(x)	{if(x) { (void)free(x); x = NULL; } }




/****************************************************************************
** 	_openTable
**
**	Purpose	: Open the datafile for a given table
**	Args	: Database and Table names
**	Returns	: file descriptor for the data file
**	Notes	: 
*/

int openTable(table,DB)
	char	*table;
	char	*DB;
{
	char	path[255];

	(void)sprintf(path,"%s/msqldb/%s/%s.dat",msqlHomeDir,DB,table);
	return(open(path,O_RDWR));
}



#ifndef NEW_DB

int openKey(table,DB)
	char	*table;
	char	*DB;
{
	char	path[255];

	(void)sprintf(path,"%s/msqldb/%s/%s.key",msqlHomeDir,DB,table);
	return(open(path,O_RDWR));
}

#else

DB *openKey(table,DB)
	char	*table;
	char	*DB;
{
	char	path[255];

	(void)sprintf(path,"%s/msqldb/%s/%s.key",msqlHomeDir,DB,table);
	return(dbopen(path,O_RDWR|O_CREAT,0700,DB_BTREE,NULL));
}

#endif




/****************************************************************************
** 	_openStack
**
**	Purpose	: Open the stack file for a given table
**	Args	: Database and Table names
**	Returns	: fiel descriptor of the stack file
**	Notes	: 
*/

int openStack(table,DB)
	char	*table;
	char	*DB;
{
	char	path[255];

	(void)sprintf(path,"%s/msqldb/%s/%s.stk",msqlHomeDir,DB,table);
	return(open(path,O_RDWR | O_CREAT, 0600));
}





void initBackend()
{
	(void) bzero(tableCache, sizeof(tableCache));
}





/****************************************************************************
** 	_popBlankPos
**
**	Purpose	: Pop the localtion of a hole from the table's file stack
**	Args	: Database and table names
**	Returns	: Offset off hole, NO_POS if the stack's empty
**	Notes	: 
*/


static u_int popBlankPos(cacheEntry,db,table)
	cache_t	*cacheEntry;
	char	*db,
		*table;
{
	int	fd;
	u_int	pos;
	off_t	offset;

	msqlTrace(TRACE_IN,"popBlankPos()");
	fd = cacheEntry->stackFD;
	if (fd < 0)
	{
		msqlDebug(MOD_ACCESS,"popBlankPos() : No stack file for %s\n",
			(cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);
		msqlTrace(TRACE_OUT,"popBlankPos()");
		return(NO_POS);
	}
	offset = lseek(fd, (off_t) 0 - sizeof(int), SEEK_END);
	if (offset < 0)
	{
		msqlDebug(MOD_ACCESS,"popBlankPos() : No hole in %s\n",
			(cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);
		msqlTrace(TRACE_OUT,"popBlankPos()");
		return(NO_POS);
	}
	read(fd,&pos,sizeof(u_int));
	ftruncate(fd,offset);
	msqlDebug(MOD_ACCESS,"popBlankPos() : Using hole at %u in %s\n",
		pos,(cacheEntry->result)?cacheEntry->resInfo:cacheEntry->table);
	msqlTrace(TRACE_OUT,"popBlankPos()");
	return(pos);
}


/****************************************************************************
** 	_pushBlankPos
**
**	Purpose	: Store the location of a data hole 
**	Args	: database and table names
**		  offset for the hole.
**	Returns	: -1 on error
**	Notes	: Using a stack-file for storage of the hole info
**		  enables a consistent and reliable access time for
**		  inserts into a table regardless of its size.
*/

static int pushBlankPos(cacheEntry,db,table,pos)
	cache_t	*cacheEntry;
	char	*db,
		*table;
	u_int	pos;
{
	int	fd;

	msqlTrace(TRACE_IN,"pushBlankPos()");
	fd = cacheEntry->stackFD;
	if (fd < 0)
	{
		msqlDebug(MOD_ACCESS,"pushBlankPos() : No stack file for %s\n",
		(cacheEntry->result)?cacheEntry->resInfo:cacheEntry->table);
		msqlTrace(TRACE_OUT,"pushBlankPos()");
		return(-1);
	}
	lseek(fd, (off_t) 0, SEEK_END);
	write(fd,&pos,sizeof(u_int));
	msqlDebug(MOD_ACCESS,"pushBlankPos() : Setting hole at %u in %s\n",
		pos,(cacheEntry->result)?cacheEntry->resInfo:cacheEntry->table);
	msqlTrace(TRACE_OUT,"pushBlankPos()");
	return(0);
}



static void setupKey(cacheEntry,key)
	cache_t	*cacheEntry;
	pkey_t 	*key;
{
	val_t	value;

	msqlTrace(TRACE_IN,"setupKey()");
	bzero(cacheEntry->keyBuf,cacheEntry->keyLen+1);
	value = *(key->value);
	switch(key->value->type)
	{
		case INT_TYPE:
#ifndef _CRAY
			bcopy(&value.val.intVal,
				cacheEntry->keyBuf+1,cacheEntry->keyLen);
#else
			packInt32(value.val.intVal, cacheEntry->keyBuf+1);
#endif
			break;
		
		case CHAR_TYPE:
			bcopy(value.val.charVal, cacheEntry->keyBuf+1,
			    (cacheEntry->keyLen > strlen(value.val.charVal))
			    ? strlen(value.val.charVal) : cacheEntry->keyLen);
			break;

		case REAL_TYPE:
			bcopy(&value.val.realVal,
				cacheEntry->keyBuf+1,cacheEntry->keyLen);
			break;
	}
	msqlTrace(TRACE_OUT,"setupKey()");
}




/****************************************************************************
** 	_msqlListDBs
**
**	Purpose	: Send a list of available databases to the client
**	Args	: Client socket
**	Returns	: -1 on error
**	Notes	: The only things in the top level data directory are
**		  database directories so we just send a file listing.
*/

int msqlListDBs(sock)
	int	sock;
{
	char	path[255];
	DIR	*dirp;
#ifdef HAVE_DIRENT
	struct	dirent *cur;
#else
	struct	direct *cur;
#endif

	msqlTrace(TRACE_IN,"msqlListDBs()");
	(void)sprintf(path,"%s/msqldb",msqlHomeDir);
	dirp = opendir(path);
	if (!dirp)
	{
		sprintf(errMsg,BAD_DIR_ERROR,path);
		msqlDebug(MOD_ERR,"Can't open directory \"%s\"\n",path);
		msqlTrace(TRACE_OUT,"msqlListDBs()");
		return(-1);
	}
	
	/*
	** Grab the names dodging any . files
	*/

	cur = readdir(dirp);
	while(cur)
	{
		if (*cur->d_name == '.')
		{
			cur = readdir(dirp);
			continue;
		}
		sprintf(packet,"%d:%s\n",strlen(cur->d_name),cur->d_name);
		writePkt(sock);
		cur = readdir(dirp);
	}
	sprintf(packet,"-100:\n");
	writePkt(sock);
	closedir(dirp);
	msqlTrace(TRACE_OUT,"msqlListDBs()");
	return(0);
}






/****************************************************************************
** 	_msqlListTables
**
**	Purpose	: Send a list of available tables for a given DB
**	Args	: Client socket and Database name
**	Returns	: -1 on error
**	Notes	: Looks for table definitions files (*.def)
*/

int msqlListTables(sock,DB)
	int	sock;
	char	*DB;
{
	char	path[255],
		*cp;
	DIR	*dirp;
#ifdef HAVE_DIRENT
	struct	dirent *cur;
#else
	struct	direct *cur;
#endif

	msqlTrace(TRACE_IN,"msqlListTables()");
	(void)sprintf(path,"%s/msqldb/%s",msqlHomeDir,DB);
	dirp = opendir(path);
	if (!dirp)
	{
		sprintf(errMsg,BAD_DIR_ERROR,path);
		msqlDebug(MOD_ERR,"Can't open directory \"%s\"\n",path);
		msqlTrace(TRACE_OUT,"msqlListTables()");
		return(-1);
	}
	
	/*
	** Skip over '.' and '..'
	*/
	cur = readdir(dirp);
	while(cur)
	{
		if (*cur->d_name != '.')
			break;
		cur = readdir(dirp);
	}

	/*
	** Grab the names
	*/

	while(cur)
	{
		cp = (char *)rindex(cur->d_name,'.');
		if (!cp)
		{
			cur = readdir(dirp);
			continue;
		}
	
		if (strcmp(cp,".def") == 0)
		{
			*cp = 0;
			sprintf(packet,"%d:%s\n",
				strlen(cur->d_name),cur->d_name);
			writePkt(sock);
		}
		cur = readdir(dirp);
	}
	sprintf(packet,"-100:\n");
	writePkt(sock);
	closedir(dirp);
	msqlTrace(TRACE_OUT,"msqlListTables()");
	return(0);
}






/****************************************************************************
** 	_msqlListFields
**
**	Purpose	: Send a list of table fields to the client
**	Args	: Client socket.  Table and database names
**	Returns	: 
**	Notes	: 
*/

void msqlListFields(sock,table,DB)
	int	sock;
	char	*table,
		*DB;
{
 	field_t	*curField;
	char	buf[50];
	cache_t	*cacheEntry;

	msqlTrace(TRACE_IN,"msqlListFields()");
	msqlDebug(MOD_GENERAL,"Table to list = %s\n",table);
	if((cacheEntry = loadTableDef(table,NULL,DB)))
	{
      		curField = cacheEntry->def;
		while(curField)
		{
			sprintf(buf,"%d",curField->length);
			sprintf(packet,"%d:%s%d:%s1:%d%d:%s1:%s1:%s", 
				strlen(table), table,
				strlen(curField->name), curField->name, 
				curField->type,strlen(buf), buf, 
				curField->flags & NOT_NULL_FLAG?"Y":"N", 
				curField->flags & PRI_KEY_FLAG ?"Y":"N");
			writePkt(sock);
			curField = curField->next;
		}
		sprintf(packet,"-100:\n");
		writePkt(sock);
	}
	else
	{
		sprintf(errMsg,BAD_TABLE_ERROR,table);
		sprintf(packet,"-1:%s\n",errMsg);
		writePkt(sock);
		msqlTrace(TRACE_OUT,"msqlListFields()");
		return;
	}
	msqlTrace(TRACE_OUT,"msqlListFields()");
	return;
}






/****************************************************************************
** 	_msqlBackendInit
**
**	Purpose	: Any db backend startup code - called per query
**	Args	: None
**	Returns	: Nothing
**	Notes	: 
*/

void msqlBackendInit()
{
	(void)bzero(tableCache,sizeof(tableCache));
}





/****************************************************************************
** 	_msqlBackendClean
**
**	Purpose	: Any db cleanup code - called after query processed
**	Args	: None
**	Returns	: Nothing
**	Notes	: 
*/

void msqlBackendClean()
{
	selectWildcard = 0;
	selectDistinct = 0;
}







/****************************************************************************
** 	_freeTableDef
**
**	Purpose	: Free memory used by a table def
**	Args	: pointer to table def
**	Returns	: Nothing
**	Notes	: 
*/

static void freeTableDef(tableDef)
	field_t	*tableDef;
{
	field_t	*curField,
		*prevField;

	msqlTrace(TRACE_IN,"freeTableDef()");
	curField = tableDef;
	while(curField)
	{
		prevField = curField;
		curField = curField->next;

		safeFree(prevField);
	}
	msqlTrace(TRACE_OUT,"freeTableDef()");
}


static void freeCacheEntry(entry)
	cache_t	*entry;
{
	char	path[255];

	msqlTrace(TRACE_IN,"freeCacheEntry()");
	if (entry->result)
	{
        	(void)sprintf(path,"%s/msqldb/%s/%s.def",msqlHomeDir,
			entry->DB,entry->table);
	}

	freeTableDef(entry->def);
	entry->def = NULL;
	*(entry->DB) = 0;
	*(entry->table) = 0;
	entry->age = 0;
	safeFree(entry->rowBuf);
	safeFree(entry->keyBuf);
#ifdef HAVE_MMAP
	if (entry->dataMap != (caddr_t) NULL)
	{
		munmap(entry->dataMap,entry->size);
		entry->dataMap = NULL;
		entry->size = 0;
	}
	if (entry->keyMap != (caddr_t) NULL)
	{
		munmap(entry->keyMap,entry->keySize);
		entry->keyMap = NULL;
		entry->keySize = 0;
	}
#endif
	close(entry->stackFD);
	close(entry->dataFD);
#ifdef NEW_DB
	if (entry->dbp)
	{
		entry->dbp->close(entry->dbp);
		entry->dbp = NULL;
	}
#else
	if (entry->keyFD >= 0)
	{
		close(entry->keyFD);
	}
#endif
	if (entry->result)
	{
		unlink(path);
	}
	msqlTrace(TRACE_OUT,"freeCacheEntry()");
}



void dropCache()
{
	int	index = 0;

	while(index < CACHE_SIZE)
	{
		if (tableCache[index].def)
		{
			freeCacheEntry(tableCache + index);
		}
		index++;
	}
}

/****************************************************************************
** 	_readTableDef
**
**	Purpose	: Load a table definition from file
**	Args	: Database and Table names
**	Returns	: pointer to table definition
**	Notes	: 
*/

field_t *readTableDef(table,alias,DB,keyLen)
	char	*table,
		*alias,
		*DB;
	int	*keyLen;
{
	field_t	*headField,
		*tmpField,
		*prevField,
		*curField;
	char	path[255];
	int	numFields,
		numBytes,
		fieldCount,
		fd;
	static	char buf[MAX_FIELDS * sizeof(field_t)];

	msqlTrace(TRACE_IN,"readTableDef()");
	(void)sprintf(path,"%s/msqldb/%s/%s.def",msqlHomeDir,DB,table);
	fd = open(path,O_RDONLY,0);
	if (fd < 0)
	{
		sprintf(errMsg,BAD_TABLE_ERROR,table);
		msqlDebug(MOD_ERR,"Unknown table \"%s\"\n",table);
		msqlTrace(TRACE_OUT,"readTableDef()");
		return(NULL);
	}
	numBytes = read(fd,buf,sizeof(buf));
	if (numBytes < 1)
	{
		sprintf(errMsg,TABLE_READ_ERROR,table);
		msqlDebug(MOD_ERR,"Error reading table \"%s\" definition\n",table);
		msqlTrace(TRACE_OUT,"readTableDef()");
		return(NULL);
	}
		
	numFields = numBytes / sizeof(field_t);
	fieldCount = 0;
	*keyLen = 0;
	headField = NULL;
	while(fieldCount < numFields)
	{
		tmpField = (field_t *)(buf + (fieldCount * sizeof(field_t)));
		curField = (field_t *)fastMalloc(sizeof(field_t));
		if (!headField)
		{
			headField = prevField = curField;
		}
		else
		{
			prevField->next = curField;
			prevField = curField;
		}
		(void)bcopy(tmpField, curField, sizeof(field_t));
		if (alias)
		{
			strcpy(curField->table,alias);
		}
		if (tmpField->flags & PRI_KEY_FLAG)
		{
			*keyLen = tmpField->length;
		}
		fieldCount++;
	}
	close(fd);
	msqlTrace(TRACE_OUT,"readTableDef()");
	return(headField);
}






static cache_t *createTmpTable(table1,table2,fields)
	cache_t	*table1,
		*table2;
	field_t	*fields;
{
	REG	cache_t *new;
	field_t	*curField,
		*newField,
		*tmpField;
	char	path[255],
		*tmpfile,
		*tmpStr,
		*cp;
	int	fd,
		foundField;


	/*
	** Create a name for this tmp table
	*/
	msqlTrace(TRACE_IN,"createTmpTable()");
	tmpStr = tmpfile = (char *)msql_tmpnam(NULL);
	cp = (char *)rindex(tmpfile,'/');
	if (cp)
	{
		tmpfile = cp+1;
	}
	(void)sprintf(path,"%s/msqldb/.tmp/%s.dat",msqlHomeDir,tmpfile);


	/*
	** start building the table cache entry
	*/
	new = (cache_t *)malloc(sizeof(cache_t));
	if (!new)
	{
		sprintf(errMsg,TMP_MEM_ERROR);
		msqlDebug(MOD_ERR,"Out of memory for temporary table (%s)\n"
		,path);
		msqlTrace(TRACE_OUT,"createTmpTable()");
		free((char *)tmpStr);
		return(NULL);
	}
	(void)strcpy(new->table,tmpfile);
	fd = open(path,O_RDWR|O_CREAT|O_TRUNC, 0700);
	if (fd < 0)
	{
		sprintf(errMsg,TMP_CREATE_ERROR);
		msqlDebug(MOD_ERR,"Couldn't create temporary table (%s)\n",
			path);
		(void)free((char *)new);
		msqlTrace(TRACE_OUT,"createTmpTable()");
		free((char *)tmpStr);
		return(NULL);
	}
	new->dataFD = fd;
	new->keyFD = new->stackFD = -1;
	new->result = 1;


	/*
	** Add the field definitions.  Ensure that any key fields are
	** not flagged as such as we can't access the key data of the
	** original table when accessing this.
	*/

	curField = table1->def;
	newField = NULL;
	while(curField)
	{
		/*
		** If we've been given a list of fields, only add this
		** field to the tmp table if it's in the list.
		*/
		if (fields)
		{
			foundField = 0;
			tmpField = fields;
			while(tmpField)
			{
				if(strcmp(tmpField->name,curField->name)==0 &&
				   strcmp(tmpField->table,curField->table)==0)
				{
					foundField = 1;
					break;
				}
				tmpField = tmpField->next;
			}
			if (!foundField)
			{
				curField = curField->next;
				continue;
			}
		}

		/*
		** O.k.  Add this field
		*/
		if (newField)
		{
			newField->next = (field_t *)fastMalloc(sizeof(field_t));
			newField = newField->next;
		}
		else
		{
			new->def=newField=(field_t *)fastMalloc(sizeof(field_t));
		}
		(void)bcopy(curField,newField,sizeof(field_t));
		if( *(newField->table) == 0)
		{
			(void)strcpy(newField->table,table1->table);
		}
		/* newField->flags=0; */
		new->rowLen += curField->length + 1;
		curField = curField->next;
	}
	if (table2)
	{
		curField = table2->def;
		while(curField)
		{
			/*
			** If we've been given a list of fields, only add this
			** field to the tmp table if it's in the list.
			*/
			if (fields)
			{
				foundField = 0;
				tmpField = fields;
				while(tmpField)
				{
					if(strcmp(tmpField->name,
						curField->name)==0 &&
				   	strcmp(tmpField->table,
						curField->table)==0)
					{
						foundField = 1;
						break;
					}
					tmpField = tmpField->next;
				}
				if (!foundField)
				{
					curField = curField->next;
					continue;
				}
			}

			/*
			** Add it.
			*/
			if (newField)
			{
				newField->next = (field_t *)fastMalloc(
					sizeof(field_t));
				newField = newField->next;
			}
			else
			{
				new->def=newField=(field_t *)fastMalloc(
					sizeof(field_t));
			}
			(void)bcopy(curField,newField,sizeof(field_t));
			if( *(newField->table) == 0)
			{
				(void)strcpy(newField->table,table2->table);
			}
			new->rowLen += curField->length + 1;
			curField = curField->next;
		}
	}

	if (newField)	
	{
		newField->next = NULL;
	}
	new->rowBuf = (u_char *)malloc(new->rowLen+1);
	new->keyLen = 0;
	msqlTrace(TRACE_OUT,"createTmpTable()");
	free((char *)tmpStr);
	return(new);
}




static void freeTmpTable(entry)
	cache_t	*entry;
{
	char	path[255];

        msqlTrace(TRACE_IN,"freeTmpTable()");
	(void)sprintf(path,"%s/msqldb/.tmp/%s.dat",msqlHomeDir,entry->table);
	freeTableDef(entry->def);
	entry->def = NULL;
	*(entry->DB) = 0;
	*(entry->table) = 0;
	entry->age = 0;
	safeFree(entry->rowBuf);
	safeFree(entry->keyBuf);
#ifdef HAVE_MMAP
	if (entry->dataMap != (caddr_t) NULL)
	{
		munmap(entry->dataMap,entry->size);
		entry->dataMap = NULL;
		entry->size = 0;
	}
	if (entry->keyMap != (caddr_t) NULL)
	{
		munmap(entry->keyMap,entry->keySize);
		entry->keyMap = NULL;
		entry->keySize = 0;
	}
#endif
	if (entry->stackFD >= 0)
		close(entry->stackFD);
	close(entry->dataFD);
#ifdef NEW_DB
	if (entry->dbp)
	{
		entry->dbp->close(entry->dbp);
		entry->dbp = NULL;
	}
#else
	if (entry->keyFD >= 0)
		close(entry->keyFD);
#endif
	(void)free((char *)entry);
	unlink(path);
        msqlTrace(TRACE_OUT,"freeTmpTable()");
}



/****************************************************************************
** 	_findRowLen
**
**	Purpose	: Determine the on-disk size of a table's rows
**	Args	: None
**	Returns	: Row Length
**	Notes	: Uses global table definition pointer.
*/

static int findRowLen(cacheEntry)
	cache_t	*cacheEntry;
{
	int	rowLen;
	field_t	*fieldDef;

	rowLen = 0;
	fieldDef = cacheEntry->def;
	while(fieldDef)
	{
		rowLen += fieldDef->length +1;  /* +1 for NULL indicator */
		fieldDef = fieldDef->next;
	}
	return(rowLen);
}




/****************************************************************************
** 	_loadTableDef
**
**	Purpose	: Locate a table definition
**	Args	: Database and Table names
**	Returns	: -1 on error
**	Notes	: Table description cache searched first.  If it's not
**		  there, the LRU entry is freed and the table def is
**		  loaded into the cache.  The tableDef, stackFD,
**		  cacheEntry and dataFD globals are set.
*/


cache_t *loadTableDef(table,cname,DB)
	char	*table,
		*cname,
		*DB;
{
	int	maxAge,
		cacheIndex,
		keyLen;
	field_t	*def,
		*curField;
	REG 	cache_t	*entry;
	REG 	int	count;
	char	path[255],
		*tableName;
	struct	stat statBuf;


	/*
	** Look for the entry in the cache.  Keep track of the oldest
	** entry during the pass so that we can replace it if needed
	*/
	msqlTrace(TRACE_IN,"loadTableDef()");
	msqlDebug(MOD_CACHE,"Table cache search for %s:%s\n",table,DB);
	count = cacheIndex = 0;
	maxAge = -1;
	if (cname)
	{
		if (!*cname)
		{
			cname = NULL;
		}
	}
	while(count < CACHE_SIZE)
	{
		entry = tableCache + count;
		msqlDebug(MOD_CACHE,"Cache entry %d = %s:%s, age = %d\n", count,
			entry->table?entry->table:"NULL",
			entry->DB?entry->DB:"NULL", 
			entry->age);
		if (strcmp(entry->DB,DB)==0 && strcmp(entry->table,table)==0 &&
		    strcmp((cname)?cname:"",entry->cname)==0)
		{
			msqlDebug(MOD_CACHE,"Found cache entry at %d\n", count);
			entry->age = 1;
			msqlTrace(TRACE_OUT,"loadTableDef()");
			return(entry);
		}
		if (entry->age > 0)
			entry->age++;

		/*
		** Empty entries have an age of 0.  If we're marking
		** an empty cache position just keep the mark
		*/
		if ((entry->age == 0) && (maxAge != 0))
		{
			maxAge = entry->age;
			cacheIndex = count;
		}
		else
		{
			if ((entry->age > maxAge) && (maxAge != 0))
			{
				maxAge = entry->age;
				cacheIndex = count;
			}
		}
		count++;
	}

	/*
	** It wasn't in the cache.  Free up the oldest cache entry 
	*/

	entry = tableCache + cacheIndex;
	if(entry->def)
	{
		msqlDebug(MOD_CACHE,"Removing cache entry %d (%s:%s)\n", 
			cacheIndex, entry->DB, entry->table);
#ifdef HAVE_MMAP
		if (entry->dataMap != (caddr_t) NULL)
		{
			munmap(entry->dataMap,entry->size);
			entry->dataMap = NULL;
			entry->size = 0;
		}
		if (entry->keyMap != (caddr_t) NULL)
		{
			munmap(entry->keyMap,entry->keySize);
			entry->keyMap = NULL;
			entry->keySize = 0;
		}
#endif

		(void)close(entry->stackFD);
		(void)close(entry->dataFD);
#ifdef NEW_DB
		if (entry->dbp)
		{
			entry->dbp->close(entry->dbp);
			entry->dbp = NULL;
		}
#else
		if (entry->keyFD >= 0)
		{
			(void)close(entry->keyFD);
		}
#endif
		freeTableDef(entry->def);
		safeFree(entry->rowBuf);
		safeFree(entry->keyBuf);
		entry->def = NULL;
	}

	/*
	** Now load the new entry
	*/
	if (cname)
	{
		tableName = cname;
		def = readTableDef(cname,table,DB,&keyLen);
	}
	else
	{
		tableName = table;
		def = readTableDef(table,NULL,DB,&keyLen);
	}
	if (!def)
	{
		sprintf(errMsg,TABLE_READ_ERROR,table);
		msqlDebug(MOD_ERR,"Couldn't read table definition for %s\n",table);
		msqlTrace(TRACE_OUT,"loadTableDef()");
		return(NULL);
	}
	entry->def = def;
	entry->age = 1;
	entry->result = 0;
	entry->keyLen = keyLen;
	entry->keyFD = -1;
	strcpy(entry->DB,DB);
	strcpy(entry->table,table);
	if (cname)
	{
		strcpy(entry->cname,cname);
	}
	else
	{
		*(entry->cname) = 0;
	}
	
	msqlDebug(MOD_CACHE,"Loading cache entry %d (%s:%s)\n", cacheIndex, 
		entry->DB, entry->table);
	if((entry->dataFD = openTable(tableName,DB)) < 0)
	{
		sprintf(errMsg,DATA_OPEN_ERROR,tableName);
		msqlTrace(TRACE_OUT,"loadTableDef()");
		return(NULL);
	}
	if((entry->stackFD = openStack(tableName,DB)) < 0)
	{
		sprintf(errMsg,STACK_OPEN_ERROR,tableName);
		msqlTrace(TRACE_OUT,"loadTableDef()");
		return(NULL);
	}
	curField = entry->def;
	while(curField)
	{
		if (curField->flags & PRI_KEY_FLAG)
		{
#ifdef NEW_DB
			entry->dbp = openKey(tableName,DB);
			if (!entry->dbp)
			{
				sprintf(errMsg,KEY_OPEN_ERROR,tableName);
				msqlTrace(TRACE_OUT,"loadTableDef()");
				return(NULL);
			}
#else
			entry->keyFD = openKey(tableName,DB);
			if (entry->keyFD < 0)
			{
				sprintf(errMsg,KEY_OPEN_ERROR,tableName);
				msqlTrace(TRACE_OUT,"loadTableDef()");
				return(NULL);
			}
#endif
			break;
		}
		curField = curField->next;
	}

#ifdef HAVE_MMAP
	/*
	** Setup for Mapping the data file
	*/
	entry->dataMap = NULL;
	entry->keyMap = NULL;
	entry->remapData = 1;
	if (entry->keyFD == -1)
		entry->remapKey = 0;
	else
		entry->remapKey = 1;
	initTable(entry,FULL_REMAP);
#endif

	/*
	** Set the globals and bail.  We need rowLen + 2 (one for the
	** active byte and also one for regexp over-run protection) and
	** keyLen + 1 (one for the active byte) buffers for performance.
	*/
	entry->rowLen = findRowLen(entry);
	entry->rowBuf = (u_char *)malloc(entry->rowLen + 2);
	entry->keyBuf = (u_char *)malloc(entry->keyLen + 1);
	fstat(entry->dataFD,&statBuf);
	entry->numRows = statBuf.st_size / (entry->rowLen + 1);
	msqlTrace(TRACE_OUT,"loadTableDef()");
	return(entry);
}







/****************************************************************************
** 	_initTable
**
**	Purpose	: Reset table pointers used during query processing
**	Args	: None
**	Returns	: Nothing
**	Notes	: This just puts the file into a known state, particular
**		  the current seek pointers.
*/

int initTable(cacheEntry,mapFlag)
	cache_t	*cacheEntry;
	int	mapFlag;
{
	struct	stat sbuf;
	char	active;

	msqlTrace(TRACE_IN,"initTable()");
#ifdef HAVE_MMAP
	if (mapFlag && FULL_REMAP)
	{
		if (cacheEntry->remapData)
		{
			fstat(cacheEntry->dataFD, &sbuf);
			cacheEntry->size = sbuf.st_size;
			if (cacheEntry->size)
			{
				cacheEntry->dataMap = (caddr_t)mmap(NULL, 
					(size_t)cacheEntry->size, 
					(PROT_READ | PROT_WRITE), 
					MAP_SHARED, cacheEntry->dataFD, 
					(off_t)0);
				if (cacheEntry->dataMap == (caddr_t)-1)
					return(-1);
			}
			cacheEntry->remapData = 0;
		}
	}

#  ifndef NEW_DB
	if (mapFlag && FULL_REMAP || mapFlag && KEY_REMAP)
	{
		if (cacheEntry->remapKey)
		{
			fstat(cacheEntry->keyFD, &sbuf);
			cacheEntry->keySize = sbuf.st_size;
			if (cacheEntry->keySize)
			{
				cacheEntry->keyMap = (caddr_t) mmap(NULL, 
					(size_t)cacheEntry->keySize,
					PROT_READ | PROT_WRITE, MAP_SHARED, 
					cacheEntry->keyFD, (off_t)0);
				if (cacheEntry->keyMap == (caddr_t)-1)
					return(-1);
			}
			cacheEntry->remapKey = 0;
		}
	}
#  endif
#else
	readRow(cacheEntry,&active,NO_POS);
#endif
	msqlTrace(TRACE_OUT,"initTable()");
	return(0);
}

	




/****************************************************************************
** 	_writeRow
**
**	Purpose	: Store a table row in the database
**	Args	: datafile FD, row data, length of data, target offset
**	Returns	: -1 on error
**	Notes	: If the rowNum is NO_POS then append the row
*/

#ifdef HAVE_MMAP

int writeRow(cacheEntry,row,rowNum)
	cache_t	*cacheEntry;
	char	*row;
	u_int	rowNum;
{
	u_char	active = 1;
	REG	off_t	seekPos;
	char	*buf;


	if (rowNum == NO_POS)
	{
		msqlDebug(MOD_ACCESS,"writeRow() : append to %s\n",
			(cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);

		cacheEntry->numRows++;
	}
	else
	{
		msqlDebug(MOD_ACCESS,"writeRow() : write at row %u of %s\n",
			rowNum, (cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);
	}
	if (rowNum == NO_POS)  /* append and flag remap */
	{
		cacheEntry->remapData = 1;
		if (cacheEntry->dataMap != (caddr_t) NULL &&
		    cacheEntry->dataMap != (caddr_t) -1 )
		{
               		munmap(cacheEntry->dataMap, cacheEntry->size);
			cacheEntry->dataMap = NULL;
			cacheEntry->size = 0;
		}
		if (lseek(cacheEntry->dataFD,(off_t)0, SEEK_END) < 0)
		{
			sprintf(errMsg,"seek error on append");
			return(-1);
		}
		if (row)
		{
			bcopy(cacheEntry->rowBuf+1,row,cacheEntry->rowLen);
		}	
		*cacheEntry->rowBuf = active;
		if (write(cacheEntry->dataFD,cacheEntry->rowBuf,
			cacheEntry->rowLen + 1) < 0)
		{
			sprintf(errMsg,WRITE_ERROR);
			return(-1);
		}
	}
	else
	{
		seekPos = rowNum * (cacheEntry->rowLen + 1);
		buf = ((char *)cacheEntry->dataMap) + seekPos;
		*buf = active;
		if (row)
		{
			bcopy(row, buf+1, cacheEntry->rowLen);
		}
		else
		{
			bcopy(cacheEntry->rowBuf+1, buf+1, cacheEntry->rowLen);
		}
	}
	return(0);
}

#else

int writeRow(cacheEntry,row,rowNum)
	cache_t	*cacheEntry;
	char	*row;
	u_int	rowNum;
{
	int	fd;
	u_char	active=1;

	msqlTrace(TRACE_IN,"writeRow()");
	if (rowNum == NO_POS)
	{
		msqlDebug(MOD_ACCESS,"writeRow() : append to %s\n",
			(cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);

		cacheEntry->numRows++;
	}
	else
	{
		msqlDebug(MOD_ACCESS,"writeRow() : write at row %u of %s\n",
			rowNum, (cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);
	}
	*cacheEntry->rowBuf = active;
	fd = cacheEntry->dataFD;
	if (rowNum == NO_POS)  /* append */
	{
		lseek(fd,(off_t)0, SEEK_END);
	}
	else
	{
		lseek(fd, (off_t)rowNum * (cacheEntry->rowLen + 1), SEEK_SET);
	}
	if (row)
	{
		bcopy(row,cacheEntry->rowBuf+1, cacheEntry->rowLen);
	}
	if (write(fd,cacheEntry->rowBuf, cacheEntry->rowLen + 1) < 0)
	{
		sprintf(errMsg,WRITE_ERROR);
		msqlTrace(TRACE_OUT,"writeRow()");
		return(-1);
	}
	msqlTrace(TRACE_OUT,"writeRow()");
	return(0);
}

#endif


#ifdef NEW_DB

int writeKey(cacheEntry, key, rowNum)
	cache_t	*cacheEntry;
	pkey_t	*key;
	u_int	rowNum;
{
	int	res;
	DBT	keyInfo,
		dataInfo;

	if (rowNum == NO_POS)
	{
		rowNum = cacheEntry->numRows;
	}

	setupKey(cacheEntry,key);
	keyInfo.data = cacheEntry->keyBuf+1;
	keyInfo.size = cacheEntry->keyLen;

	msqlDebug(MOD_KEY,"Writing key for %s at row %d",(char *)keyInfo.data, 
		rowNum);

	dataInfo.data = &rowNum;
	dataInfo.size = sizeof(u_int);
	res = cacheEntry->dbp->put(cacheEntry->dbp,&keyInfo,&dataInfo,
		R_NOOVERWRITE);
	msqlDebug(MOD_KEY," .. result = %d\n",res);
	if (res == -1)
		return(-1);
	else
		return(0);
}


#else
#ifdef HAVE_MMAP


int writeKey(cacheEntry ,key,rowNum)
	cache_t	*cacheEntry;
	pkey_t	*key;
	u_int	rowNum;
{
	char	active = 1;
	REG 	off_t	seekPos;
	char	*buf;


	setupKey(cacheEntry,key);
	*cacheEntry->keyBuf = active;
	if (rowNum == NO_POS)  /* append and flag remap */
	{
		cacheEntry->remapKey = 1;
		if (cacheEntry->keyMap)
		{
			munmap(cacheEntry->keyMap, cacheEntry->keySize);
			cacheEntry->keyMap = NULL;
			cacheEntry->keySize = 0;
		}
		lseek(cacheEntry->keyFD,(off_t)0, SEEK_END);
		if (write(cacheEntry->keyFD,cacheEntry->keyBuf,
			cacheEntry->keyLen + 1) < 0)
			return(-1);
	}
	else
	{
		seekPos = rowNum * (cacheEntry->keyLen + 1);
		buf = ((char *)cacheEntry->keyMap) + seekPos;
		(void) bcopy(cacheEntry->keyBuf, buf,cacheEntry->keyLen + 1);
	}
	return(0);
}
#else

int writeKey(cacheEntry,key,rowNum)
	cache_t	*cacheEntry;
	pkey_t	*key;
	u_int	rowNum;
{
	int	fd;
	char	active=1;

	msqlTrace(TRACE_IN,"writeKey()");
	setupKey(cacheEntry,key);
	*cacheEntry->keyBuf = active;
	fd = cacheEntry->keyFD;
	if (rowNum == NO_POS)  /* append */
	{
		lseek(fd,(off_t)0, SEEK_END);
	}
	else
	{
		lseek(fd, (off_t)rowNum * (cacheEntry->keyLen + 1), SEEK_SET);
	}
	if (write(fd,cacheEntry->keyBuf, cacheEntry->keyLen + 1) < 0)
	{
		sprintf(errMsg,KEY_WRITE_ERROR);
		msqlTrace(TRACE_OUT,"writeKey()");
		return(-1);
	}
	msqlTrace(TRACE_OUT,"writeKey()");
	return(0);
}



#endif
#endif


/****************************************************************************
** 	_readRow
**
**	Purpose	: Grab a row from a datafile
**	Args	: datafile FD, length the row, pointer to active flag buf
**	Returns	: pointer to static row buffer
**	Notes	: The original version of this routine bcopy()'ed the
**		  row into a space provided by the caller.  Profiling
**		  showed that bcopy() took as much execution time as
** 		  read() (although there were 8 reads and 10,000+
**		  bcopy()'s on the sample run).  Returning a pointer and 
**		  then just copying the bits we need for memory allignment is
**		  much faster (ie. 1/7 the amount of time in bcopy for
**		  no loss elsewhere).
*/

#ifdef HAVE_MMAP
char *readRow(cacheEntry,active,rowNum)
	cache_t	*cacheEntry;
	char	*active;
	u_int	rowNum;
{
	REG 	off_t	seekPos;
	char	*buf;


	seekPos = rowNum * (cacheEntry->rowLen + 1);
	if ((seekPos >= cacheEntry->size) || (!cacheEntry->dataMap))
	{
		msqlDebug(MOD_ACCESS,"readRow() : %u of %s - No Such Row \n",
			rowNum, (cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);
		*active = 0;
		return(NULL);
	}
	buf = ((char *)cacheEntry->dataMap) + seekPos;
	*active = *buf;
	msqlDebug(MOD_ACCESS,"readRow() : %u of %s - %s\n",
		rowNum, (cacheEntry->result)?cacheEntry->resInfo:
		cacheEntry->table,(*active)?"Active":"Inactive");
	return(((char *)buf) + 1);
}
#else

char *readRow(cacheEntry,active,rowNum)
	cache_t	*cacheEntry;
        char    *active;
        u_int 	rowNum;
{
        int     numBytes,
		numRows,
                maxRead,
		rowLen,
		fd;
        REG 	int    offset;
 

	msqlTrace(TRACE_IN,"readRow()");
	rowLen = cacheEntry->rowLen;
	fd = cacheEntry->dataFD;

	/*
	** A row num of NO_POS forces initialisation
	*/ 
        if (rowNum == NO_POS)
        {
		msqlDebug(MOD_ACCESS,"readRow() : Read buffer initialised\n");
		cacheEntry->firstRow = cacheEntry->lastRow = -1;
		*active=0;
		msqlTrace(TRACE_OUT,"readRow()");
		return(NULL);
	}

	/*
	** If the row isn't in the buf, load it
	*/
	if ((int)rowNum < (int)cacheEntry->firstRow || 
	    (int)rowNum > (int)cacheEntry->lastRow)
	{
		/*
		** Grab as many rows as we can fit in the buffer.
		** If the desired row is one more than the lastRow,
		** we can just read as the file pointer will be in
		** the right spot
		*/
                maxRead = (BUF_SIZE / (rowLen+1)) * (rowLen+1);
		lseek(fd, (off_t)rowNum * (rowLen+1), SEEK_SET);
                numBytes = read(fd,cacheEntry->readBuf,maxRead);
                if (numBytes < 1)
		{
			cacheEntry->firstRow = cacheEntry->lastRow = -1;
			*active=0;
			msqlDebug(MOD_ACCESS,
				"readRow() : %u of %s - No Such Row \n",
				rowNum,(cacheEntry->result)?cacheEntry->resInfo:
				cacheEntry->table);
			msqlTrace(TRACE_OUT,"readRow()");
                        return(NULL);
		}
                numRows = numBytes / (rowLen + 1);
		cacheEntry->firstRow = rowNum;
		cacheEntry->lastRow = rowNum + numRows - 1;
        }
        offset = (rowNum - cacheEntry->firstRow) * (rowLen + 1);
        *active = *(char *)(cacheEntry->readBuf + offset);

	msqlDebug(MOD_ACCESS,"readRow() : %u of %s - %s\n",
		rowNum, (cacheEntry->result)?cacheEntry->resInfo:
		cacheEntry->table,(*active)?"Active":"Inactive");
	msqlTrace(TRACE_OUT,"readRow()");
        return((char *)(cacheEntry->readBuf + offset + 1));
}

#endif



#ifdef NEW_DB

u_int readKey(cacheEntry,key)
	cache_t	*cacheEntry;
	pkey_t	*key;
{
	u_int	rowNum;
	int	res;
	DBT	keyInfo,
		dataInfo;

	setupKey(cacheEntry,key);
	keyInfo.data = cacheEntry->keyBuf+1;
	keyInfo.size = cacheEntry->keyLen;
	msqlDebug(MOD_KEY,"Finding key for %s\n",(char *)keyInfo.data);
	res = cacheEntry->dbp->get(cacheEntry->dbp,&keyInfo,&dataInfo,0);
	if (res == 0)
	{
		bcopy(dataInfo.data,&rowNum,sizeof(rowNum));
	}
	else
	{
		rowNum = NO_POS;
	}
	msqlDebug(MOD_KEY,"Key location = %d\n", rowNum);
	return(rowNum);
}

#else
#ifdef HAVE_MMAP

u_int readKey(cacheEntry,key)
	cache_t	*cacheEntry;
	pkey_t	*key;
{
	REG 	off_t	seekPos;
	u_int	rowNum,
		maxRow;
	char	*buf;


	setupKey(cacheEntry,key);
	rowNum = 0;
	seekPos = 0;
	maxRow = cacheEntry->keySize / (cacheEntry->keyLen + 1);
	while(rowNum < maxRow)
	{
		if ((seekPos > cacheEntry->keySize) || !cacheEntry->keyMap)
		{
			return(NO_POS);
		}
		buf = ((char *)cacheEntry->keyMap) + seekPos;
		if (*buf)
		{
			/* Inline bcmp() */
			REG char *s, *t, *e;

			s = ((char*)cacheEntry->keyBuf) + 1;
			e = s + cacheEntry->keyLen;
			t = buf + 1;
			while (s < e && *s == *t)
			{
				s++;
				t++;
			}
			if (s >= e)
			{
				return(rowNum);
			}
		}
		rowNum++;
		seekPos += (cacheEntry->keyLen + 1);
	}
	return(NO_POS);
}

#else

u_int readKey(cacheEntry,key)
	cache_t	*cacheEntry;
	pkey_t	*key;
{
        int     numBytes,
                maxRead,
		fd;
        REG 	int    rowLen,
			numRows,
			curRow;
	char		*cp;
 

	msqlTrace(TRACE_IN,"readKey()");
	setupKey(cacheEntry,key);
	rowLen = cacheEntry->keyLen;
	fd = cacheEntry->keyFD;
	lseek(fd,(off_t)0,SEEK_SET);
	maxRead = (BUF_SIZE / (rowLen+1)) * (rowLen+1);
	numBytes = read(fd,readKeyBuf,maxRead);
	numRows = numBytes / (rowLen + 1);
	curRow = 0;
	cp = readKeyBuf;
	while(numBytes)
	{
		if (curRow >= numRows)
		{
                	numBytes = read(fd,readKeyBuf,maxRead);
                	numRows += numBytes / (rowLen + 1);
			cp = readKeyBuf;
			continue;
		}
		if (*cp)
		{
			if(bcmp(cacheEntry->keyBuf + 1, cp+1, rowLen) == 0)
			{
				msqlTrace(TRACE_OUT,"readKey()");
				return(curRow);
			}
		}
		curRow++;
		cp += rowLen + 1;
        }
	msqlTrace(TRACE_OUT,"readKey()");
	return(NO_POS);
}

#endif
#endif



/****************************************************************************
** 	_deleteRow
**
**	Purpose	: Invalidate a row in the table
**	Args	: datafile FD, rowlength, desired row location
**	Returns	: -1 on error
**	Notes	: This just sets the row header byte to 0 indicating
**		  that it's no longer in use 
*/

int deleteRow(cacheEntry,rowNum)
	cache_t	*cacheEntry;
	u_int	rowNum;
{
	char	*activePtr;
	int	rowLen;

#ifndef HAVE_MMAP
	int	fd;
	char	activeBuf;
#endif

	msqlTrace(TRACE_IN,"deleteRow()");
	msqlDebug(MOD_ACCESS,"deleteRow() : row %u of %s\n",
		rowNum, (cacheEntry->result)?cacheEntry->resInfo:
		cacheEntry->table);
	rowLen = cacheEntry->rowLen;

#ifdef HAVE_MMAP
	activePtr = ((char *)cacheEntry->dataMap) + (rowNum * (rowLen + 1));
	*activePtr = 0;
#else
	fd = cacheEntry->dataFD;
	if (lseek(fd,(off_t)rowNum * (rowLen+1), SEEK_SET) < 0)
	{
		sprintf(errMsg,SEEK_ERROR);
		msqlTrace(TRACE_OUT,"deleteRow()");
		return(-1);
	}
	activeBuf = 0;
	if (write(fd,&activeBuf,1) < 0)
	{
		sprintf(errMsg,WRITE_ERROR);
		msqlTrace(TRACE_OUT,"deleteRow()");
		return(-1);
	}
	/*
	** Force a reload of the data buffer else we won't see the new
	** active value.
	*/
	(void) readRow(cacheEntry,&activeBuf, NO_POS);
#endif
	msqlTrace(TRACE_OUT,"deleteRow()");
	return(0);
}




#ifdef NEW_DB


int deleteKey(cacheEntry, key)
	cache_t	*cacheEntry;
	pkey_t	*key;
{
	int	res;
	DBT	keyInfo;

	setupKey(cacheEntry,key);
	keyInfo.data = cacheEntry->keyBuf+1;
	keyInfo.size = cacheEntry->keyLen;
	msqlDebug(MOD_KEY,"Delete key for %s ",(char *)keyInfo.data);
	res = cacheEntry->dbp->del(cacheEntry->dbp,&keyInfo,0);
	msqlDebug(MOD_KEY," .. result = %d\n",res);
	if (res == -1)
		return(-1);
	else
		return(0);
}

#else

int deleteKey(cacheEntry,rowNum)
	cache_t	*cacheEntry;
	u_int	rowNum;
{
	char	*activePtr;
	int	rowLen;

#ifndef HAVE_MMAP
	char	activeBuf = 0;
	int	fd;
#endif

	msqlTrace(TRACE_IN,"deleteKey()");
	rowLen = cacheEntry->keyLen;

#ifdef HAVE_MMAP
	if (cacheEntry->keyMap)
	{
		activePtr = ((char *)cacheEntry->keyMap) + 
			(rowNum * (rowLen + 1));
		*activePtr = 0;
	}
#else

	fd = cacheEntry->keyFD;
	if (fd >= 0)
	{
		if (lseek(fd,(off_t)rowNum * (rowLen+1), SEEK_SET) < 0)
		{
			sprintf(errMsg,KEY_SEEK_ERROR);
			msqlTrace(TRACE_OUT,"deleteKey()");
			return(-1);
		}
		activeBuf = 0;
		if (write(fd,&activeBuf,1) < 0)
		{
			sprintf(errMsg,KEY_WRITE_ERROR);
			msqlTrace(TRACE_OUT,"deleteKey()");
			return(-1);
		}
	}
#endif
	msqlTrace(TRACE_OUT,"deleteKey()");
	return(0);
}

#endif








/****************************************************************************
** 	_checkNullFields
**
**	Purpose	: Ensure that fields flagged "not null" have a value
**	Args	: table row
**	Returns	: -1 on error
**	Notes	:
*/

static int checkNullFields(cacheEntry,row)
	cache_t	*cacheEntry;
	char	*row;
{
	REG 	field_t	*curField;
	REG 	int	offset;

	msqlTrace(TRACE_IN,"checkNullFields()");
	offset = 0;
	curField = cacheEntry->def;
	while(curField)
	{
		if (!*(row + offset) && (curField->flags & NOT_NULL_FLAG))
		{
			sprintf(errMsg,BAD_NULL_ERROR, curField->name);
			msqlDebug(MOD_ERR,"Field \"%s\" cannot be null\n",
				curField->name);
			msqlTrace(TRACE_OUT,"checkNullFields()");
			return(-1);
		}
		offset += curField->length + 1;
		curField = curField->next;
	}
	msqlTrace(TRACE_OUT,"checkNullFields()");
	return(0);
}



static void qualifyFields(table,fields)
	char	*table;
	field_t	*fields;
{
	field_t	*curField;

	msqlTrace(TRACE_IN,"qualifyField()");
	curField = fields;
	while(curField)
	{
		if(*(curField->table) == 0)
		{
			(void)strcpy(curField->table,table);
		}
		curField=curField->next;
	}
	msqlTrace(TRACE_OUT,"qualifyField()");
}


static void qualifyConds(table,conds)
	char	*table;
	cond_t	*conds;
{
	cond_t	*curCond;

	msqlTrace(TRACE_IN,"qualifyConds()");
	curCond = conds;
	while(curCond)
	{
		if(*(curCond->table) == 0)
		{
			(void)strcpy(curCond->table,table);
		}
		curCond=curCond->next;
	}
	msqlTrace(TRACE_OUT,"qualifyConds()");
}



static void qualifyOrder(table,order)
	char	*table;
	order_t	*order;
{
	order_t	*curOrder;

	msqlTrace(TRACE_IN,"qualifyOrder()");
	curOrder = order;
	while(curOrder)
	{
		if(*(curOrder->table) == 0)
		{
			(void)strcpy(curOrder->table,table);
		}
		curOrder=curOrder->next;
	}
	msqlTrace(TRACE_OUT,"qualifyOrder()");
}




/****************************************************************************
** 	_findKeyValue
**
**	Purpose	: 
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

static void findKeyValue(cacheEntry, row, keyPtr)
	cache_t	*cacheEntry;
	u_char	*row;
	pkey_t	**keyPtr;
{
	REG 	field_t	*curField;
	int	curOffset;
	static	pkey_t	key;
	u_char	*cp;
	int	ip,
		*offset;
	double	*fp;
	char	buf[8];




	msqlTrace(TRACE_IN,"findKeyValue()");
	if (keyPtr)
	{
		*keyPtr = NULL;
	}
	curField = cacheEntry->def;
	curOffset = 0;
	key.value = NULL;
	while(curField)
	{
		if (curField->flags & PRI_KEY_FLAG)
		{
			if (keyPtr)
			{
				key.table = curField->table;
				key.name = curField->name;
				key.type = curField->type;
				key.length = curField->length;
				key.op = 0;
				*keyPtr = &key;

				if (key.value)
				{
					freeValue(key.value);
				}

				switch(curField->type)
                        	{
                                case INT_TYPE:
#ifndef _CRAY
                                        bcopy(row + curOffset + 1,&ip,4);
                                        key.value =(val_t *)
                                                fillValue(&ip,INT_TYPE);
#else
                                        key.value = (val_t*)fillValue(
                                                row + *offset + 1, INT_TYPE);
#endif
                                        break;

                                case CHAR_TYPE:
                                        cp = (u_char *)row + curOffset + 1;
                                        key.value = (val_t *)
                                                fillValue(cp, curField->type,
                                               curField->length);
                                        break;

                                case REAL_TYPE:
                                        bcopy(row + curOffset + 1,buf,8);
                                        fp = (double *)buf;
                                        key.value =(val_t *)
                                                fillValue(fp,REAL_TYPE);
                                       break;
				}
			}
			break;
		}
		curOffset += curField->length+1; /* +1 for null indicator */
		curField = curField->next;
	}
}


/****************************************************************************
** 	_setupFields
**
**	Purpose	: Determine the byte offset into a row of the desired fields
**	Args	: Empty field list (field location) array,
**		  List of desired fields 
**	Returns	: -1 on error
**	Notes	: The field list array holds the byte offsets for the
**		  fields.  ie. array element 0 will hold the byte offset
**		  of the first desired field etc.
*/

static int setupFields(cacheEntry,flist, fields, keyPtr)
	cache_t	*cacheEntry;
	int	*flist;
	field_t	*fields;
	pkey_t	**keyPtr;
{
	REG 	field_t	*curField,
			*fieldDef;
	int	numFields,
		*curFL,
		curOffset;
	static	pkey_t	key;



	msqlTrace(TRACE_IN,"setupFields()");
	numFields = 0;
	curField = fields;
	curFL = flist;
	if (keyPtr)
	{
		*keyPtr = NULL;
	}
	while(curField)
	{
		numFields++;
		if (numFields < MAX_FIELDS)
			*curFL++ = -1;
		curField=curField->next;
	}
	if (numFields > MAX_FIELDS)
	{
		sprintf(errMsg,FIELD_COUNT_ERROR);
		msqlDebug(MOD_ERR,"Too many fileds in query\n");
		msqlTrace(TRACE_OUT,"setupFields()");
		return(-1);
	}
	*curFL = -1;

	curField = fields;
	curFL = flist;
	while(curField)
	{
	    fieldDef = cacheEntry->def;
	    curOffset = 0;
	    while(fieldDef)
	    {
		if(strcmp(curField->name,fieldDef->name) == 0 &&
		   strcmp(curField->table,fieldDef->table) == 0)
		{
			curField->type = fieldDef->type;
			curField->length = fieldDef->length;
			curField->flags = fieldDef->flags;
			if (curField->flags & PRI_KEY_FLAG)
			{
				if (keyPtr)
				{
					key.table = curField->table;
					key.name = curField->name;
					key.type = curField->type;
					key.length = curField->length;
					key.value = curField->value;
					key.op = 0;
					*keyPtr = &key;
				}
			}
			*curFL = curOffset;
			if (!curField->value)
				break;
			if (!curField->value->nullVal)
			{
				switch(curField->type)
				{
					case INT_TYPE:
						if(curField->value->type
							!= INT_TYPE)
						{
							sprintf(errMsg,
								TYPE_ERROR,
								curField->name);
							msqlDebug(MOD_ERR,TYPE_ERROR,
								curField->name);
							return(-1);
						}
						break;
		
					case CHAR_TYPE:
						if(curField->value->type
							!= CHAR_TYPE)
						{
							sprintf(errMsg,
								TYPE_ERROR,
								curField->name);
							msqlDebug(MOD_ERR,TYPE_ERROR,
								curField->name);
							return(-1);
						}
						if (strlen(
						curField->value->val.charVal)>
					 	curField->length)
						{
							sprintf(errMsg,
								SIZE_ERROR,
								curField->name);
							msqlDebug(MOD_ERR,
								SIZE_ERROR,
								curField->name);
							return(-1);
						}
						break;

					case REAL_TYPE:
						if(curField->value->type
							== INT_TYPE)
						{
						  curField->value->val.realVal
						  = curField->value->val.intVal;
                        			  curField->value->type = 
							REAL_TYPE;
						}
						if(curField->value->type
							!= REAL_TYPE)
						{
							sprintf(errMsg,
								TYPE_ERROR,
								curField->name);
							msqlDebug(MOD_ERR,TYPE_ERROR,
								curField->name);
							return(-1);
						}
						break;
				}
			}
			break;
		}
		curOffset += fieldDef->length+1; /* +1 for null indicator */
		fieldDef = fieldDef->next;
	    }
	    if(!fieldDef)  /* Bad entry */
	    {
		if (curField->table)
		{
		    sprintf(errMsg,BAD_FIELD_ERROR,
				curField->table,curField->name);
		    msqlDebug(MOD_ERR,"Unknown field \"%s.%s\"\n",
				curField->table,curField->name);
		    msqlTrace(TRACE_OUT,"setupFields()");
		    return(-1);
		}
		else
		{
		    sprintf(errMsg,BAD_FIELD_2_ERROR,curField->name);
		    msqlDebug(MOD_ERR,"Unknown field \"%s\"\n",curField->name);
		    msqlTrace(TRACE_OUT,"setupFields()");
		    return(-1);
		}
	    }
	    curFL++;
	    curField = curField->next;
	}
	msqlTrace(TRACE_OUT,"setupFields()");
	return(0);
}





/****************************************************************************
** 	_setupConds
**
**	Purpose	: Determine the byte offset into a row for conditional
**		  data.
**	Args	: Condition list (field location) array,
**		  List of fileds used in conditionals
**	Returns	: -1 on error
**	Notes	: As per setupFields.
*/

static int setupConds(cacheEntry,clist, conds, keyPtr)
	cache_t	*cacheEntry;
	int	*clist;
	cond_t	*conds;
	pkey_t	**keyPtr;
{
	REG 	cond_t	*curCond;
	REG 	field_t	*fieldDef;
	int	numConds,
		*curFL,
		curOffset;
	char	*name,
		*length,
		*type;
	static	pkey_t	key;


	msqlTrace(TRACE_IN,"setupConds()");
	numConds = 0;
	curCond = conds;
	curFL = clist;
	*keyPtr = NULL;
	while(curCond)
	{
		numConds++;
		if (numConds < MAX_FIELDS)
			*curFL++ = -1;
		curCond=curCond->next;
	}
	if (numConds > MAX_FIELDS)
	{
		sprintf(errMsg,COND_COUNT_ERROR);
		msqlDebug(MOD_ERR,"Too many fields in condition\n");
		msqlTrace(TRACE_OUT,"setupConds()");
		return(-1);
	}
	*curFL = -1;
	
	curCond = conds;
	curFL = clist;
	while(curCond)
	{
		fieldDef = cacheEntry->def;
		curOffset = 0;
		while(fieldDef)
		{
			if(strcmp(curCond->name,fieldDef->name) == 0 &&
			   strcmp(curCond->table,fieldDef->table) == 0)
			{
				curCond->type = fieldDef->type;
				curCond->length = fieldDef->length;
				*curFL = curOffset;
				if (fieldDef->flags & PRI_KEY_FLAG)
				{
					key.name = fieldDef->name;
					key.type = fieldDef->type;
					key.length = fieldDef->length;
					key.value = curCond->value;
					key.op = curCond->op;
					*keyPtr = &key;
				}
				break;
			}
			curOffset += fieldDef->length+1; /* +1 for null ind */
			fieldDef = fieldDef->next;
		}
		if (!fieldDef)
		{
			sprintf(errMsg,BAD_FIELD_2_ERROR, curCond->name);
			msqlDebug(MOD_ERR,"Unknown field in where clause \"%s\"\n",
				curCond->name);
			msqlTrace(TRACE_OUT,"setupConds()");
			return(-1);
		}
		curFL++;
		curCond = curCond->next;
	}
	msqlTrace(TRACE_OUT,"setupConds()");
	return(0);
}



/****************************************************************************
** 	_setupOrder
**
**	Purpose	: Determine the byte offset into a row for order
**		  data.
**	Args	: Order list (field location) array,
**		  List of fileds used in order
**	Returns	: -1 on error
**	Notes	: As per setupFields.
*/

static int setupOrder(cacheEntry,olist, order)
	cache_t	*cacheEntry;
	int	*olist;
	order_t	*order;
{
	REG 	order_t	*curOrder;
	REG 	field_t	*fieldDef;
	int	numOrder,
		*curFL,
		curOffset;
	char	*name,
		*length,
		*type;

	msqlTrace(TRACE_IN,"setupOrder()");
	numOrder = 0;
	curOrder = order;
	curFL = olist;
	while(curOrder)
	{
		numOrder++;
		if (numOrder < MAX_FIELDS)
			*curFL++ = -1;
		curOrder=curOrder->next;
	}
	if (numOrder > MAX_FIELDS)
	{
		sprintf(errMsg,ORDER_COUNT_ERROR);
		msqlDebug(MOD_ERR,"Too many fields in order specification\n");
		msqlTrace(TRACE_OUT,"setupOrder()");
		return(-1);
	}
	*curFL = -1;
	
	curOrder = order;
	curFL = olist;
	while(curOrder)
	{
		fieldDef = cacheEntry->def;
		curOffset = 0;
		while(fieldDef)
		{
			if(strcmp(curOrder->name,fieldDef->name) == 0 &&
			   strcmp(curOrder->table,fieldDef->table) == 0)
			{
				curOrder->type = fieldDef->type;
				curOrder->length = fieldDef->length;
				*curFL = curOffset;
				break;
			}
			curOffset += fieldDef->length+1; /* +1 for null ind */
			fieldDef = fieldDef->next;
		}
		if (!fieldDef)
		{
			sprintf(errMsg,BAD_FIELD_2_ERROR, curOrder->name);
			msqlDebug(MOD_ERR,"Unknown field in order clause \"%s\"\n",
				curOrder->name);
			msqlTrace(TRACE_OUT,"setupOrder()");
			return(-1);
		}
		curFL++;
		curOrder = curOrder->next;
	}
	msqlTrace(TRACE_OUT,"setupOrder()");
	return(0);
}







/****************************************************************************
** 	_expandWildCards
**
**	Purpose	: Handle "*" in a select clause
**	Args	: 
**	Returns	: 
**	Notes	: This just drops the entire table into the field list
**		  when it finds a "*"
*/

field_t *expandFieldWildCards(cacheEntry,fields)
	cache_t	*cacheEntry;
	field_t	*fields;
{
	char	path[255],
		line[100],
		*name;
	REG 	field_t	*curField,
			*fieldDef;
	field_t	*prevField,
		*newField,
		*tmpField,
		*head;


	/*
	** Scan the field list
	*/

	msqlTrace(TRACE_IN,"expandWildcard()");
	head = curField = fields;
	prevField = NULL;
	while(curField)
	{
		if (strcmp(curField->name,"*") == 0)
		{
			/*
			** Setup a new entry for each field
			*/
			fieldDef = cacheEntry->def;
			while(fieldDef)
			{
				newField = (field_t *)malloc(sizeof(field_t));
				strcpy(newField->name,fieldDef->name);
				strcpy(newField->table,fieldDef->table);
				if (!prevField)
				{
					head = newField;
				}
				else
					prevField->next = newField;
				newField->next = curField->next;
				prevField = newField;
				fieldDef = fieldDef->next;
			}

			/*
			** Blow away the wildcard entry
			*/
			if (curField->type == CHAR_TYPE)
				safeFree(curField->value->val.charVal);
			tmpField = curField;
			curField = curField->next;
			safeFree(tmpField);
		}
		else
		{
			prevField = curField;
			curField = curField->next;
		}
	}
	msqlTrace(TRACE_OUT,"expandWildcard()");
	return(head);
}



expandTableFields(table)
	char    *table;
{
	cache_t *cacheEntry;
	extern char *curDB;
	char	tableName[NAME_LEN];

	msqlTrace(TRACE_IN,"expandTableFields()");
	strcpy(tableName,table);
	if((cacheEntry = loadTableDef(tableName,NULL,curDB)))
	{
		fieldHead = expandFieldWildCards(cacheEntry,fieldHead);
	}
	msqlTrace(TRACE_OUT,"expandTableFields()");
}





/****************************************************************************
** 	_fillRow
**
**	Purpose	: Create a new row-buf using the info given
**	Args	: 
**	Returns	: 
**	Notes	: 
*/


static void fillRow(row,fields,flist)
	char	*row;
	field_t	*fields;
	int	flist[];
{
	int	*offset,
		length;
	field_t	*curField;
	char	*cp;
	int	*ip;
	double	*fp;

	msqlTrace(TRACE_IN,"fillRow()");
	curField = fields;
	offset = flist;
	while(curField)
	{
		if (!curField->value->nullVal)
		{
			cp = row + *offset;
			*cp = '\001';
			cp++;
			switch(curField->type)
			{
				case INT_TYPE:
#ifndef _CRAY
					bcopy4(&(curField->value->val.intVal),
						cp);
#else
					packInt32(curField->value->val.intVal,
						cp);
#endif
					break;
		
				case CHAR_TYPE:
					length=strlen(curField->value->val.charVal);
					if (length > curField->length)
						length = curField->length;
					bcopy(curField->value->val.charVal,cp,
						length);
					break;

				case REAL_TYPE:
					bcopy8(&(curField->value->val.realVal),
						cp);
					break;
			}
		}
		offset++;
		curField = curField->next;
	}
	msqlTrace(TRACE_OUT,"fillRow()");
}






/****************************************************************************
** 	_updateValues
**
**	Purpose	: Modify a row-buf to reflect the contents of the field list
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

static void updateValues(row,fields,flist)
	char	*row;
	field_t	*fields;
	int	flist[];
{
	int	*offset;
	field_t	*curField;
	char	*cp;

	msqlTrace(TRACE_IN,"updateValues()");
	curField = fields;
	offset = flist;
	while(curField)
	{
		cp = row + *offset;
		if (!curField->value->nullVal)
                {
                        *cp = '\001';
                        cp++;
			switch(curField->type)
			{
				case INT_TYPE:
#ifndef _CRAY
					bcopy4(&(curField->value->val.intVal),
						cp);
#else
				 	packInt32(curField->value->val.intVal, 
						cp);
#endif
					break;
		
				case CHAR_TYPE:
					strncpy(cp,curField->value->val.charVal,
						curField->length);
					break;

				case REAL_TYPE:
					bcopy8(&(curField->value->val.realVal),
						cp);
					break;
			}
		}
		else
		{
			*cp = '\000';
		}
		offset++;
		curField = curField->next;
	}
	msqlTrace(TRACE_OUT,"updateValues()");
}






/****************************************************************************
** 	_translateValues
**
**	Purpose	: Create real field values from the query text
**	Args	: 
**	Returns	: 
**	Notes	: All field values are passed as text in the query
**		  string.  This just converts them to their native format
*/

#ifdef NOT_DEF

static void translateValues(fields)
	field_t	*fields;
{
	field_t	*curField;

	msqlTrace(TRACE_IN,"translateValues()");
	curField = fields;
	while(curField)
	{
		if (curField->length)
                {
			curField->value->nullVal = 0;
			switch(curField->type)
			{
				case INT_TYPE:
#ifndef _CRAY
					curField->value->val.intVal =
						atoi(curField->textRep);
#else
					packInt32(curField->value->val.intVal,
						cp);
#endif
					safeFree(curField->textRep);
					break;

				 case CHAR_TYPE:
					curField->value->val.charVal = 
						curField->textRep;
					break;

				case REAL_TYPE:
					sscanf(curField->textRep,"%lf",
					 	&(curField->value->val.realVal));
					safeFree(curField->textRep);
					break;
			}
		}
		else 
		{
			curField->value->nullVal = 0;
		}
		curField = curField->next;
	}
	msqlTrace(TRACE_OUT,"translateValues()");
}

#endif






/****************************************************************************
** 	_extractValues
**
**	Purpose	: Rip the required data from a row-buf
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

static void extractValues(row,fields,flist)
	u_char	*row;
	field_t	*fields;
	int	flist[];
{
	field_t	*curField;
	u_char	*cp;
	int	ip,
		*offset;
	double	*fp;
	char	buf[8];

	msqlTrace(TRACE_IN,"extractValues()");
	curField = fields;
	offset = flist;
	while(curField)
	{
		if (curField->value)
		{
			freeValue(curField->value);
			curField->value = NULL;
		}
		if ( * (row + *offset)) 
		{
			curField->value=NULL;
			switch(curField->type)
			{
				case INT_TYPE:
#ifndef _CRAY
					bcopy4(row + *offset + 1,&ip);
					curField->value =(val_t *)
						fillValue(&ip,INT_TYPE);
#else
					curField->value = (val_t*)fillValue(
						row + *offset + 1, INT_TYPE);
#endif
					break;

				case CHAR_TYPE:
					cp = (u_char *)row + *offset + 1;
					curField->value = (val_t *)
						fillValue(cp, CHAR_TYPE,
						curField->length);
					break;

				case REAL_TYPE:
					bcopy8(row + *offset + 1,buf);
					fp = (double *)buf;
					curField->value =(val_t *)
						fillValue(fp,REAL_TYPE);
					break;
			}
		} 
		else 
		{
			curField->value = (val_t *)nullValue();
		}
		curField = curField->next;
		offset++;
	}
	msqlTrace(TRACE_OUT,"extractValues()");
}



static char	regErrFlag;

void regerror()
{
	regErrFlag++;
}




static int regexpTest(str,re,maxLen)
	char	*str,
		*re;
	int	maxLen;
{
	char	regbuf[1024],
		strBuf[2048],
		*strPtr,
		*tmpBuf,
		hold;
	REG 	char *cp1, *cp2;
	regexp	*reg;
	int	res;

	/*
	** Map an SQL regexp into a UNIX regexp
	*/
	cp1 = re;
	cp2 = regbuf;
	(void)bzero(regbuf,sizeof(regbuf));
	*cp2++ = '^';
	while(*cp1 && maxLen)
	{
		switch(*cp1)
		{
			case '\\':
				if (*(cp1+1) == '%' || *(cp1+1) == '_')
				{
					cp1++;
					*cp2 = *cp1;
				}
				cp1++;
				cp2++;
				break;

			case '_':
				*cp2++ = '.';
				cp1++;
				break;

			case '%':
				*cp2++ = '.';
				*cp2++ = '*';
				cp1++;
				break;

			case '.':
			case '*':
			case '+':
				*cp2++ = '\\';
				*cp2++ = *cp1++;
				break;

			default:
				*cp2++ = *cp1++;
				break;
		}
	}
	*cp2 = '$';

	/*
	** Do the regexp thang.  We do an ugly hack here : The data of
	** a field may be exactly the same length as the field itself.
	** Seeing as the regexp routines work on null rerminated strings
	** if the field is totally full we get field over-run.  So,
	** store the value of the last byte, null it out, run the regexp
	** and then reset it (hey, I said it was ugly).
	*/
	regErrFlag = 0;
	if (maxLen < sizeof(strBuf))
	{
		strncpy(strBuf,str,maxLen);
		*(strBuf + maxLen) = 0;
		tmpBuf = NULL;
		strPtr = strBuf;
	}
	else
	{
		tmpBuf = (char *)malloc(maxLen+2);
		strncpy(tmpBuf,str,maxLen);
		*(tmpBuf + maxLen) = 0;
		strPtr = tmpBuf;
	}
	reg = regcomp(regbuf);
	res = regexec(reg,strPtr);
	safeFree(reg);
	if (tmpBuf)
		free(tmpBuf);
	if (regErrFlag)
	{
		strcpy(errMsg, BAD_LIKE_ERROR);
		msqlDebug(MOD_ERR, "Evaluation of LIKE clause failed\n");
		return(-1);
	}
	return(res);
}



/****************************************************************************
** 	_byteMatch
**
**	Purpose	: comparison suite for single bytes.
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

static int byteMatch(v1,v2,op)
	char	v1, v2; 
	int	op;
{
	int	result;

	switch(op)
	{
		case EQ_OP:
			result = (v1 == v2);
			break;
			
		case NE_OP:
			result = (v1 != v2);
			break;
			
		case LT_OP:
			result = (v1 < v2);
			break;
			
		case LE_OP:
			result = (v1 <= v2);
			break;
			
		case GT_OP:
			result = (v1 > v2);
			break;
			
		case GE_OP:
			result = (v1 >= v2);
			break;
	}
	return(result);
}



/****************************************************************************
** 	_intMatch
**
**	Purpose	: comparison suite for integer fields.
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

static int intMatch(v1,v2,op)
	int	v1, v2, op;
{
	int	result;

	switch(op)
	{
		case EQ_OP:
			result = (v1 == v2);
			break;
			
		case NE_OP:
			result = (v1 != v2);
			break;
			
		case LT_OP:
			result = (v1 < v2);
			break;
			
		case LE_OP:
			result = (v1 <= v2);
			break;
			
		case GT_OP:
			result = (v1 > v2);
			break;
			
		case GE_OP:
			result = (v1 >= v2);
			break;
	}
	return(result);
}






/****************************************************************************
** 	_charMatch
**
**	Purpose	: Comparison suite for text fields
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

static int charMatch(v1,v2,op,maxLen)
	char	*v1,
		*v2;
	int	op,
		maxLen;
{
	int	result,
		cmp;
	REG	char	*c1,*c2;
	REG	int	offset;

	if (op != LIKE_OP)
	{
		c1 = v1;
		c2 = v2;
		offset=0;
		cmp = 0;
		while(offset < maxLen)
		{
			if ((cmp = *c1 - *c2) != 0)
				break;
			if ( *c1==0 || *c2==0)
				break;
			c1++;
			c2++;
			offset++;
		}
	}
	switch(op)
	{
		case EQ_OP:
			result = (cmp == 0);
			break;
			
		case NE_OP:
			result = (cmp != 0);
			break;
			
		case LT_OP:
			result = (cmp < 0);
			break;
			
		case LE_OP:
			result = (cmp <= 0);
			break;
			
		case GT_OP:
			result = (cmp > 0);
			break;
			
		case GE_OP:
			result = (cmp >= 0);
			break;

		case LIKE_OP:
			result = regexpTest(v1,v2,maxLen);
			break;

		case NOT_LIKE_OP:
			result = !(regexpTest(v1,v2,maxLen));
			break;
	}
	return(result);
}






/****************************************************************************
** 	_realMatch
**
**	Purpose	: Comparison suite for real fields
**	Args	: 
**	Returns	: 
**	Notes	: 
*/

static int realMatch(v1,v2,op)
	double	v1, 
		v2;
	int 	op;
{
	int	result;

	switch(op)
	{
		case EQ_OP:
			result = (v1 == v2);
			break;
			
		case NE_OP:
			result = (v1 != v2);
			break;
			
		case LT_OP:
			result = (v1 < v2);
			break;
			
		case LE_OP:
			result = (v1 <= v2);
			break;
			
		case GT_OP:
			result = (v1 > v2);
			break;
			
		case GE_OP:
			result = (v1 >= v2);
			break;
	}
	return(result);
}






/****************************************************************************
** 	_matchRow
**
**	Purpose	: Determine if the given row matches the required data
**	Args	: 
**	Returns	: 
**	Notes	: Used by "where" clauses
*/

static int matchRow(cacheEntry,row,conds,clist)
	cache_t	*cacheEntry;
	u_char	*row;
	cond_t	*conds;
	int	*clist;
{
	REG 	cond_t	*curCond;
	REG 	char	*cp;
	REG 	int	result,
			tmp,
			ival;
	int	*offset,
		init=1,
		iv;
	double	fv;
	char	buf[8];
	u_char	*tmpUChar;
	val_t	*value,
		tmpVal;
	field_t	*curField,
		tmpField;
	int	tmpFlist[2],
		foundField;


	msqlTrace(TRACE_IN,"matchRow()");
	result=0;
	if (!conds)
	{
		msqlTrace(TRACE_OUT,"matchRow()");
		return(1);
	}
	curCond = conds;
	offset = clist;
	while(curCond)
	{
		/*
		** If we are comparing 2 fields (e.g. in a join) then
		** grab the value of the second field so that we can do
		** the comparison.  Watch for type mismatches!
		*/
		foundField = 0;
		switch(curCond->value->type)
		{
		    case IDENT_TYPE:
			value = curCond->value;
			curField = cacheEntry->def;
			if (!*(value->val.identVal->seg1))
			{
				if (!cacheEntry->result)
				{
					strcpy(value->val.identVal->seg1,
						cacheEntry->table);
				}
				else
				{
					strcpy(errMsg,UNQUAL_ERROR);
					msqlDebug(MOD_ERR,
					   "Unqualified field in comparison\n");
					msqlTrace(TRACE_OUT,"matchRow()");
					return(-1);
				}
			}
			while(curField)
			{
				if (strcmp(curField->table,
					value->val.identVal->seg1) == 0 &&
				    strcmp(curField->name,
					value->val.identVal->seg2) == 0)
				{
					(void)bcopy(curField,&tmpField,
						sizeof(field_t));
					tmpField.value=NULL;
					tmpField.next = NULL;
					setupFields(cacheEntry,tmpFlist,
						&tmpField,NULL);
					extractValues(row,&tmpField,tmpFlist);
					(void)bcopy(tmpField.value,&tmpVal,
						sizeof(val_t));
					if (tmpVal.type == CHAR_TYPE)
					{
					    tmpVal.val.charVal= (u_char*)
						fastMalloc
						(curField->length + 1);
					    bcopy(tmpField.value->val.charVal,
						tmpVal.val.charVal,
						curField->length);
					    *(tmpVal.val.charVal + 
						curField->length) = 0;
					}
					freeValue(tmpField.value);
					tmpField.value = NULL;
					value = &tmpVal;
					foundField = 1;
					break;
				}
				curField=curField->next;
			}
			if (!foundField)
			{
				sprintf(errMsg,BAD_FIELD_ERROR,
					value->val.identVal->seg1,
					value->val.identVal->seg2);
				msqlDebug(MOD_ERR,"Unknown field '%s.%s'\n",
					value->val.identVal->seg1,
					value->val.identVal->seg2);
				msqlTrace(TRACE_OUT,"matchRow()");
				(void)free((char *)value->val.charVal);
				return(-1);
			}
			break;

		    case INT_TYPE:
		    case REAL_TYPE:
		    case CHAR_TYPE:
		    default:
			value = curCond->value;
			break;
		}


		/*
		** Ensure that the comparison is with the correct type.
		** We can't do this in setupConds() as we have to wait
		** for the evaluation of field to field comparisons.  We
		** also fudge it for real/int comparisons.
		*/

		if (value->nullVal)
		{
			value->type = curCond->type;
		}
		if (curCond->type == REAL_TYPE  && value->type == INT_TYPE)
		{
			value->val.realVal = value->val.intVal;
			value->type = REAL_TYPE;
		}
		if (curCond->type != value->type)
		{
			sprintf(errMsg,BAD_TYPE_ERROR, curCond->name);
			msqlDebug(MOD_ERR,"Bad type for comparison of '%s'",
				curCond->name);
			return(-1);
		}


		/*
		** O.K. do the actual comparison
		*/
		switch(curCond->type)
		{
			case INT_TYPE:
				if (value->nullVal)
				{
					tmp = byteMatch(*(row + *offset),
						0,curCond->op);
					break;
				}
#ifdef _CRAY
				ival = unpackInt32(row + *offset + 1);
#else

				bcopy4((row + *offset +1),&iv);
#endif
				if (curCond->op == LIKE_OP)
				{
					strcpy(errMsg, INT_LIKE_ERROR);
					msqlDebug(MOD_ERR,
					   "Can't perform LIKE on int value\n");
					msqlTrace(TRACE_OUT,"matchRow()");
					return(-1);
				}
#ifndef _CRAY
				tmp = intMatch(iv,value->val.intVal,
					curCond->op);
#else
				tmp = intMatch(ival, value->val.intVal, 
					curCond->op);
#endif
				break;

			case CHAR_TYPE:
				if (value->nullVal)
				{
					tmp = byteMatch(*(row + *offset),
						0,curCond->op);
					break;
				}
				cp = (char *)row + *offset +1;
				tmp = charMatch(cp,value->val.charVal,
					curCond->op, curCond->length);
				if (value == &tmpVal)
				{
					free((char *)tmpVal.val.charVal);
				}
				if (tmp < 0)
				{
					msqlTrace(TRACE_OUT,"matchRow()");
					return(-1);
				}
				break;

			case REAL_TYPE:
				if (value->nullVal)
				{
					tmp = byteMatch(*(row + *offset),
						0,curCond->op);
					break;
				}
				bcopy8((row + *offset +1),&fv);
				if (curCond->op == LIKE_OP)
				{
					strcpy(errMsg, REAL_LIKE_ERROR);
					msqlDebug(MOD_ERR,
					  "Can't perform LIKE on real value\n");
					msqlTrace(TRACE_OUT,"matchRow()");
					return(-1);
				}
				tmp = realMatch(fv,value->val.realVal,
					curCond->op);
				break;
		}
	
		if (init)
		{
			result = tmp;
			init = 0;
		}
		else
		{
			switch(curCond->bool)
			{
				case NO_BOOL:
					result = tmp;
					break;
	
				case AND_BOOL:
					result &= tmp;
					break;
	
				case OR_BOOL:
					result |= tmp;
					break;
			}
		}
		curCond = curCond->next;
		offset++;
	}
	msqlTrace(TRACE_OUT,"matchRow()");
	return(result);
}


static int compareRows(r1,r2,order,olist)
	char	*r1,
		*r2;
	order_t	*order;
	int	*olist;
{
	REG 	order_t *curOrder;
	char	buf[sizeof(double)],
		*cp1,
		*cp2;
	int	res,
		*offset,
		ip1,
		ip2,
		ival1,
		ival2;
	double	fp1,
		fp2;


	/*
	** Allow for cases when rows are not defined
	*/
	msqlTrace(TRACE_IN,"compareRows()");
	if (r1 && !r2)
	{
		msqlTrace(TRACE_OUT,"compareRows()");
		return(-1);
	}
	if (!r1 && r2)
	{
		msqlTrace(TRACE_OUT,"compareRows()");
		return(1);
	}
	if (!r1 && !r2)
	{
		msqlTrace(TRACE_OUT,"compareRows()");
		return(0);
	}

	/*
	** OK, we have both rows.
	*/
	curOrder = order;
	offset = olist;
	while(curOrder)
	{
		switch(curOrder->type)
		{
			case INT_TYPE:
#ifndef _CRAY
				bcopy4((r1 + *offset +1),buf);
				ip1 = (int) * (int*)buf;
				bcopy4((r2 + *offset +1),buf);
				ip2 = (int) * (int*)buf;

				if (ip1 == ip2)
					res = 0;
				if (ip1 > ip2)
					res = 1;
				if (ip1 < ip2)
					res = -1;
#else
				ival1 = unpackInt32(r1 + *offset + 1);
				ival2 = unpackInt32(r2 + *offset + 1);

				if (ival1 == ival2)
					res = 0;
				if (ival1 > ival2)
					res = 1;
				if (ival1 < ival2)
					res = -1;
#endif
				break;

			case CHAR_TYPE:
				cp1 = (char *)r1 + *offset +1;
				cp2 = (char *)r2 + *offset +1;
				res = strncmp(cp1,cp2,curOrder->length);
				break;

			case REAL_TYPE:
				bcopy8((r1+*offset+1),buf);
				fp1 = (double) * (double *)(buf);
				bcopy8((r2+*offset+1),buf);
				fp2 = (double) * (double *)(buf);
				if (fp1 == fp2)
					res = 0;
				if (fp1 > fp2)
					res = 1;
				if (fp1 < fp2)
					res = -1;
				break;
		}
		if (curOrder->dir == DESC)
		{
			res = 0 - res;
		}
		if (res != 0)
		{
			msqlTrace(TRACE_OUT,"compareRows()");
			return(res);
		}
		curOrder = curOrder->next;
		offset++;
	}
	msqlTrace(TRACE_OUT,"compareRows()");
	return(0);
}




int msqlInit(DB)
	char	*DB;
{
	char	path[255];
	struct	stat buf;
	extern	char *curDB;

	msqlTrace(TRACE_IN,"msqlInit()");
	(void)sprintf(path,"%s/msqldb/%s",msqlHomeDir,DB);
	if (stat(path,&buf) < 0)
	{
		sprintf(errMsg,BAD_DB_ERROR,DB);
		msqlDebug(MOD_ERR,"Unknown database \"%s\"\n",DB);
		msqlTrace(TRACE_OUT,"msqlInit()");
		return(-1);
	}
	msqlTrace(TRACE_OUT,"msqlInit()");
	return(0);
}



int msqlCreate(table,fields,DB)
	char	*table;
	field_t	*fields;
	char	*DB;
{
	char	defPath[255],
		datPath[255],
		keyPath[255],
		line[80];
	field_t	*curField;
	int	fd,
		rem,
		fieldCount,
		mode,
		foundKey;
	struct	stat sbuf;

	msqlTrace(TRACE_IN,"msqlCreate()");

	/*
	** Write the catalog entry
	*/
	(void)sprintf(defPath,"%s/msqldb/%s/%s.def",msqlHomeDir,DB,table);
	if (stat(defPath,&sbuf) ==0)
	{
		sprintf(errMsg,TABLE_EXISTS_ERROR,table);
		msqlDebug(MOD_ERR,"Table \"%s\" exists\n",table);
		msqlTrace(TRACE_OUT,"msqlCreate()");
		return(-1);
	}

	mode = O_WRONLY | O_CREAT;
#ifdef 	O_BINARY
	mode |= O_BINARY;
#endif
	fd = open(defPath, mode, 0600);
	if (fd < 0)
	{
		sprintf(errMsg,TABLE_FAIL_ERROR,table);
		msqlDebug(MOD_ERR,"Can't create table \"%s\"\n",table);
		msqlTrace(TRACE_OUT,"msqlCreate()");
		return(-1);
	}
	
	/*
	** Ensure that there aren't too many fields
	*/
	curField = fields;
	fieldCount = foundKey = 0;
	while(curField)
	{
		(void)strcpy(curField->table,table);
		fieldCount++;
		if (curField->flags & PRI_KEY_FLAG)
		{
			foundKey = 1;
		}
		curField = curField->next;
	}
	if (fieldCount > MAX_FIELDS)
	{
		sprintf(errMsg,TABLE_WIDTH_ERROR,MAX_FIELDS);
		msqlDebug(MOD_ERR,"Too many fields in table (%d Max)\n",MAX_FIELDS);
		close(fd);
		unlink(defPath);
		msqlTrace(TRACE_OUT,"msqlCreate()");
		return(-1);
	}

	/*
	** Dump the field definition to the table def file
	*/
	curField = fields;
	while(curField)
	{
		if(write(fd,curField,sizeof(field_t)) <0)
		{
			(void)close(fd);
			unlink(defPath);
			sprintf(errMsg,CATALOG_WRITE_ERROR);
			msqlDebug(MOD_ERR,"Error writing catalog\n");
			msqlTrace(TRACE_OUT,"msqlCreate()");
			return(-1);
		}
		curField = curField->next;
	}
	(void)close(fd);

	/*
	** If there was a key field, create the key file
	*/
	if (foundKey)
	{
		(void)sprintf(keyPath,"%s/msqldb/%s/%s.key",msqlHomeDir,DB,
			table);
		mode = O_CREAT|O_RDWR;
#ifdef O_BINARY
		mode |= O_BINARY;
#endif
		fd = open(keyPath, mode, 0600);
		if (fd < 0)
		{
			sprintf(errMsg,KEY_CREATE_ERROR);
			msqlDebug(MOD_ERR,"Creation of key table failed!\n");
			unlink(defPath);
			msqlTrace(TRACE_OUT,"msqlCreate()");
			return(-1);
		}
		close(fd);
	}


	/*
	** Create an empty table
	*/
	
	(void)sprintf(datPath,"%s/msqldb/%s/%s.dat",msqlHomeDir,DB,table);
	(void)unlink(datPath);
	mode = O_CREAT|O_WRONLY;
#ifdef O_BINARY
	mode |= O_BINARY;
#endif
	fd = open(datPath, mode, 0600);
	if (fd < 0)
	{
		unlink(datPath);
		unlink(defPath);
		unlink(keyPath);
		sprintf(errMsg,DATA_FILE_ERROR,table);
		msqlDebug(MOD_ERR,"Error creating table file for \"%s\"\n",table);
		msqlTrace(TRACE_OUT,"msqlCreate()");
		return(-1);
	}
	close(fd);
	sprintf(packet,"1:\n");
	writePkt(outSock);
	msqlTrace(TRACE_OUT,"msqlCreate()");
	return(0);
}


int msqlDrop(table,DB)
	char	*table,
		*DB;
{
	char	path[255],
		*name;
	int	fd;
	REG 	cache_t *entry;
	REG 	int	count;
	int	mode;

	msqlTrace(TRACE_IN,"msqlDrop()");

	/* 
	** Invalidate the cache entry so that we don't use it again 
	*/

	count = 0;
	while(count < CACHE_SIZE)
	{
		entry = tableCache + count;
		if (*(entry->cname))
			name = entry->cname;
		else
			name = entry->table;
		if((strcmp(entry->DB,DB)==0)&&(strcmp(name,table)==0))
		{
			msqlDebug(MOD_CACHE,"Clearing cache entry %d (%s:%s)\n",
				count,DB,table);
			freeTableDef(entry->def);
			entry->def = NULL;
			*(entry->DB) = 0;
			*(entry->table) = 0;
			entry->age = 0;
			safeFree(entry->rowBuf);
			safeFree(entry->keyBuf);
#ifdef HAVE_MMAP
			if (entry->dataMap != (caddr_t) NULL)
			{
				munmap(entry->dataMap,entry->size);
				entry->dataMap = NULL;
				entry->size = 0;
			}
			if (entry->keyMap != (caddr_t) NULL)
			{
				munmap(entry->keyMap,entry->keySize);
				entry->keyMap = NULL;
				entry->keySize = 0;
			}
#endif
			close(entry->stackFD);
			close(entry->dataFD);
#ifdef NEW_DB
			if (entry->dbp)
			{
				entry->dbp->close(entry->dbp);
				entry->dbp = NULL;
			}
#else
			if (entry->keyFD >= 0)
				close(entry->keyFD);
#endif
			break;
		}
		count++;
	}

	/*
	** Now blow away the table data ,stack file, and key files
	*/
	(void)sprintf(path,"%s/msqldb/%s/%s.def",msqlHomeDir,DB,table);
	mode = O_RDWR;
#ifdef O_BINARY
	mode |= O_BINARY;
#endif

	fd = open(path, mode, 0);
	if (fd < 0)
	{
		sprintf(errMsg,BAD_TABLE_ERROR,table);
		msqlDebug(MOD_ERR,"Unknown table \"%s\"\n",table);
		msqlTrace(TRACE_OUT,"msqlDrop()");
		return(-1);
	}
	(void)close(fd);
	unlink(path);
	(void)sprintf(path,"%s/msqldb/%s/%s.dat",msqlHomeDir,DB,table);
	unlink(path);
	(void)sprintf(path,"%s/msqldb/%s/%s.stk",msqlHomeDir,DB,table);
	unlink(path);
	(void)sprintf(path,"%s/msqldb/%s/%s.key",msqlHomeDir,DB,table);
	unlink(path);

	sprintf(packet,"1:\n");
	writePkt(outSock);
	msqlTrace(TRACE_OUT,"msqlDrop()");
	return(0);
}


int msqlDelete(table,conds,DB)
	char	*table;
	cond_t	*conds;
	char	*DB;
{
	int	clist[MAX_FIELDS],
		flist[MAX_FIELDS],
		rowLen,
		keyLen,
		useKey,
		hasKey,
		res;
	u_int	rowNum;
	char	*row,
		active;
	field_t	*curField;
	pkey_t	*key = NULL;
	cache_t	*cacheEntry;
	cond_t	*curCond;


	msqlTrace(TRACE_IN,"msqlDelete()");
	if((cacheEntry = loadTableDef(table,NULL,DB)) == NULL)
	{
		msqlTrace(TRACE_OUT,"msqlDelete()");
		return(-1);
	}

	/*
	** Find the offsets of the given condition
	*/

	qualifyConds(table,conds);
	(void)bzero(clist,MAX_FIELDS * sizeof(int));
	if (setupConds(cacheEntry,clist,conds,&key) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlDelete()");
		return(-1);
	}

	if (initTable(cacheEntry,FULL_REMAP) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlDelete()");
		return(-1);
	}


	/*
	** If the data is keyed just grab the row
	*/

	useKey = 0;
	if (key)
	{
		if (key->op == EQ_OP)
		{
			useKey = 1;
			curCond = conds;
			while(curCond)
			{
				if (curCond->bool == OR_BOOL)
				{
					useKey = 0;
					break;
				}
				curCond = curCond->next;
			}
		}
	}

	rowLen = cacheEntry->rowLen;
	keyLen = cacheEntry->keyLen;
	if (useKey)
	{
		rowNum = readKey(cacheEntry,key);	
		msqlDebug(MOD_KEY,"Primary Key gave row %u\n",rowNum);
		if (rowNum != NO_POS)
		{
			row = readRow(cacheEntry,&active,rowNum);
			if (!row)
			{
				return(-1);
			}
			if (active)
			{
				res = matchRow(cacheEntry,row,conds,clist);
				if (res < 0)
				{
					return(-1);
				}
				if (res == 1)
				{
					if(deleteRow(cacheEntry,rowNum) < 0)
					{
					       msqlTrace(TRACE_OUT,"msqlDelete()");
						return(-1);
					}
					if (cacheEntry->keyFD > 0)
					{
#ifdef NEW_DB
					    if(deleteKey(cacheEntry,key) < 0)
#else
					    if(deleteKey(cacheEntry,rowNum) < 0)
#endif
					    {
					       msqlTrace(TRACE_OUT,"msqlDelete()");
						return(-1);
					    }
					}
					pushBlankPos(cacheEntry,DB,table,
						rowNum);
				}
			}
		}
	}
	else
	{
		rowNum = 0;
		while((row = readRow(cacheEntry,&active,rowNum)))
		{
			if (!active)
			{
				rowNum++;
				continue;
			}
			res = matchRow(cacheEntry,row,conds,clist);
			if (res < 0)
			{
				return(res);
			}
			if (res == 1)
			{
				res = deleteRow(cacheEntry,rowNum);
				if(res < 0)
				{
					msqlTrace(TRACE_OUT,"msqlDelete()");
					return(res);
				}
				findKeyValue(cacheEntry,row,&key);
				if (cacheEntry->keyFD > 0)
				{
#ifdef NEW_DB
					res = deleteKey(cacheEntry,key);
#else
					res = deleteKey(cacheEntry,rowNum);
#endif
					if (key)
					{
						freeValue(key->value);
						key->value=NULL;
					}
					if(res < 0)
					{
						msqlTrace(TRACE_OUT,
							"msqlDelete()");
						return(res);
					}
				}
				pushBlankPos(cacheEntry,DB,table,rowNum);
			}
			rowNum++;
		}
	}
	sprintf(packet,"1:\n");
	writePkt(outSock);
	msqlTrace(TRACE_OUT,"msqlDelete()");
	return(0);
}




int msqlInsert(table,fields,DB)
	char	*table;
	field_t	*fields;
	char	*DB;
{
	int	flist[MAX_FIELDS],
		rowLen,
		useKey;
	u_int	rowNum;
	u_char	*row;
	REG 	field_t	*curField,
		*curField2;
	pkey_t	*key;
	cache_t	*cacheEntry;


	msqlTrace(TRACE_IN,"msqlInsert()");
	if((cacheEntry = loadTableDef(table,NULL,DB)) == NULL)
	{
		msqlTrace(TRACE_OUT,"msqlInsert()");
		return(-1);
	}

	/*
	** Find the offsets of the given fields
	*/
	qualifyFields(table,fields);
	(void)bzero(flist,MAX_FIELDS * sizeof(int));
	if (setupFields(cacheEntry,flist,fields,&key) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlInsert()");
		return(-1);
	}

	/*
	** Ensure that no field is listed more than once and that each
	** field was given a value.
	*/
	curField = fields;
	while(curField)
	{
		if (!curField->value)
		{
			sprintf(errMsg, NO_VALUE_ERROR, curField->name);
			msqlDebug(MOD_ERR,
				"No value specified for field '%s'",
				curField->name);
			msqlTrace(TRACE_OUT,"msqlInsert()");
			return(-1);
		}
		curField2 = curField;
		while(curField2)
		{
			if (curField2 == curField)
			{
				curField2 = curField2->next;
				continue;
			}
			if (strcmp(curField->name,curField2->name) == 0 &&
			    strcmp(curField->table,curField2->table) == 0)
			{
				sprintf(errMsg,NON_UNIQ_ERROR, curField->name);
				msqlDebug(MOD_ERR,"Field '%s' not unique",
					curField->name);
				msqlTrace(TRACE_OUT,"msqlInsert()");
				return(-1);
			}
			curField2 = curField2->next;
		}
		curField = curField->next;
	}

	/*
	** Create a blank row
	*/
	rowLen = cacheEntry->rowLen;
	row = cacheEntry->rowBuf + 1;

	/*
	** Find a place to put this row
	*/

	rowNum = popBlankPos(cacheEntry,DB,table);

	if (initTable(cacheEntry,KEY_REMAP) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlInsert()");
		return(-1);
	}

	/*
	** Check for a unique primary key if we have one.
	*/

	if (key)
	{
		if (readKey(cacheEntry,key) != NO_POS)
		{
			sprintf(errMsg,KEY_UNIQ_ERROR, key->name);
			msqlDebug(MOD_ERR,"Non unique key value in field '%s'\n",
				key->name);
			msqlTrace(TRACE_OUT,"msqlInsert()");
			return(-1);
		}
	}

	/*
	** Fill in the given fields and dump it to the table file
	*/

	(void)bzero(row,rowLen);
	fillRow(row,fields,flist);
	if (checkNullFields(cacheEntry,row) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlInsert()");
		return(-1);
	}
	if (key)
	{
		writeKey(cacheEntry,key,rowNum);
	}
	if(writeRow(cacheEntry,NULL,rowNum) < 0)
	{
		sprintf(errMsg,"Error on data write");
		msqlDebug(MOD_ERR,"Error on data write\n");
		msqlTrace(TRACE_OUT,"msqlInsert()");
		return(-1);
	}
	
	sprintf(packet,"1:\n");
	writePkt(outSock);
	msqlTrace(TRACE_OUT,"msqlInsert()");
	return(0);
}



int msqlUpdate(table,fields,conds,DB)
	char	*table;
	field_t	*fields;
	cond_t	*conds;
	char	*DB;
{
	int	flist[MAX_FIELDS],
		clist[MAX_FIELDS],
		rowLen,
		useKey,
		res;
	u_int	rowNum,
		keyRow;
	char	*row,
		active;
	field_t	*curField;
	pkey_t	*keyCond,
		*keyField;
	cond_t	*curCond;
	cache_t	*cacheEntry;
	


	msqlTrace(TRACE_IN,"msqlUpdate()");
	if((cacheEntry = loadTableDef(table,NULL,DB)) == NULL)
	{
		msqlTrace(TRACE_OUT,"msqlUpdate()");
		return(-1);
	}

	/*
	** Find the offsets of the given fields and condition
	*/
	qualifyFields(table,fields);
	qualifyConds(table,conds);
	(void)bzero(flist,MAX_FIELDS * sizeof(int));
	if (setupFields(cacheEntry,flist,fields,&keyField) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlUpdate()");
		return(-1);
	}
	(void)bzero(clist,MAX_FIELDS * sizeof(int));
	if (setupConds(cacheEntry,clist,conds,&keyCond) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlUpdate()");
		return(-1);
	}

	rowLen = cacheEntry->rowLen;

	if (initTable(cacheEntry,FULL_REMAP) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlUpdate()");
		return(-1);
	}

	/*
	** If the data is keyed just grab the row
	*/

	useKey = 0;
	if (keyCond)
	{
		if (keyCond->op == EQ_OP)
		{
			useKey = 1;
			curCond = conds;
			while(curCond)
			{
				if (curCond->bool == OR_BOOL)
				{
					useKey = 0;
					break;
				}
				curCond = curCond->next;
			}
		}
	}

	if (useKey)
	{
		rowNum = readKey(cacheEntry,keyCond);	
		msqlDebug(MOD_KEY,"Primary Key gave row %u\n",rowNum);
		if (rowNum != NO_POS)
		{
			row = readRow(cacheEntry,&active,rowNum);
			if (active)
			{
				res = matchRow(cacheEntry,row,conds,clist);
				if (res < 0)
				{
					return(-1);
				}
				if (res == 1)
				{
					curField = fields;
					while(curField)
					{
						if(curField->flags&PRI_KEY_FLAG)
						{
							keyField->value =
								curField->value;
							break;
						}
						curField=curField->next;
					}
					if (keyField)
					{
					    keyRow=readKey(cacheEntry,keyField);
					    if(keyRow != NO_POS &&
						keyRow != rowNum)
					    {
						sprintf(errMsg,KEY_UNIQ_ERROR, 
							keyField->name);
						msqlDebug(MOD_ERR,
							KEY_UNIQ_ERROR,
							keyField->name);
						msqlTrace(TRACE_OUT,
							"msqlUpdate()");
						return(-1);
					    }
					}
#ifdef HAVE_MMAP
					bcopy(row, cacheEntry->rowBuf,
						cacheEntry->rowLen);
					row = (char *)cacheEntry->rowBuf;
#endif
					updateValues(row,fields,flist);
					if (checkNullFields(cacheEntry,row) < 0)
					{
					       msqlTrace(TRACE_OUT,
							"msqlUpdate()");
						return(-1);
					}
					if (keyField)
					{
						writeKey(cacheEntry, keyField, 
							rowNum);
					}

					if(writeRow(cacheEntry,row,rowNum) < 0)
					{
						sprintf(errMsg,WRITE_ERROR);
						msqlDebug(MOD_ERR,
						       "Error on data write\n");
					       msqlTrace(TRACE_OUT,
							"msqlUpdate()");
						return(-1);
					}
				}
			}
		}
	}
	else
	{
		rowNum = 0;
		while((row = readRow(cacheEntry,&active,rowNum)))
		{
			if (!active)
			{
				rowNum++;
				continue;
			}
			res = matchRow(cacheEntry,row,conds,clist);
			if (res < 0)
			{
				return(-1);
			}
			if (res == 1)
			{
#ifdef HAVE_MMAP
					bcopy(row, cacheEntry->rowBuf,
						cacheEntry->rowLen);
					row = (char *)cacheEntry->rowBuf;
#endif
				updateValues(row,fields,flist);
				if (checkNullFields(cacheEntry,row) < 0)
				{
					msqlTrace(TRACE_OUT,"msqlUpdate()");
					return(-1);
				}
				if(writeRow(cacheEntry,row,rowNum) < 0)
				{
					sprintf(errMsg,WRITE_ERROR);
					msqlDebug(MOD_ERR,"Error on data write\n");
					msqlTrace(TRACE_OUT,"msqlUpdate()");
					return(-1);
				}
				if (keyField)
				{
					curField = fields;
					while(curField)
					{
						if(curField->flags&PRI_KEY_FLAG)
						{
						    if( keyField->value !=
							curField->value)
						    {
							freeValue(
							    keyField->value);
						    }
						    keyField->value =
							curField->value;
						    writeKey(cacheEntry,
							keyField,
							rowNum);
						}
						curField=curField->next;
					}
				}
			}
			rowNum++;
		}
	}
	sprintf(packet,"1:\n");
	writePkt(outSock);
	msqlTrace(TRACE_OUT,"msqlUpdate()");
	return(0);
}


static void formatPacket(packet,fields)
	char	*packet;
	field_t	*fields;
{
	char	outBuf[100],
		bufLen[10];
	u_char	*outData;
	field_t	*curField;

	msqlTrace(TRACE_IN,"formatPacket()");
	curField = fields;
	while(curField)
	{
		if (!curField->value->nullVal)
		{
			switch(curField->type)
			{
				case INT_TYPE:
					sprintf(outBuf,"%d",
					    curField->value->val.intVal);
					outData = (u_char *)outBuf;
					break;
				case CHAR_TYPE:
					outData = curField->value->val.charVal;
					break;
				case REAL_TYPE:
					/* Analogy Start */
					sprintf(outBuf,"%.16g",
					    curField->value->val.realVal);
					/* Analogy End */
					outData = (u_char *)outBuf;
					break;
			}
			sprintf(bufLen,"%d:",strlen(outData));
			strcat(packet,bufLen);
			strcat(packet,outData);
		}
		else
		{
			strcat(packet,"-2:");
		}
		curField = curField->next;
	}
	strcat(packet,"\n");
	msqlTrace(TRACE_OUT,"formatPacket()");
}



static void mergeRows(row,table1,t1Row,table2,t2Row)
	char	*row,
		*t1Row,
		*t2Row;
	cache_t	*table1,
		*table2;
{
	(void)bcopy(t1Row,row,table1->rowLen);
	(void)bcopy(t2Row,row+table1->rowLen,table2->rowLen);
}


static field_t *createTmpFieldCopy(field)
	field_t *field;
{
	static	field_t new;

	bcopy(field,&new,sizeof(new));
	new.next = NULL;
	new.value = NULL;
	return(&new);
}

static field_t *getFieldByName(name,def,offset)
	char	*name;
	field_t	*def;
	int	*offset;
{
	field_t	*cur;

	cur = def;
	offset = 0;
	while(cur)
	{
		if (strcmp(cur->name,name) == 0)
			break;
		offset += cur->length;
		cur = cur->next;
	}
	if (!cur)
	{	
		return(NULL);
	}
	return(createTmpFieldCopy(cur));
}


static int checkForPartialMatch(conds)
	cond_t	*conds;
{
	cond_t	*curCond;
	int	res;
	
	res = 1;
	curCond = conds;
	while(curCond)
	{
		if (curCond->value->type == IDENT)
		{
			res = 0;
		}
		if (curCond->bool == OR_BOOL)
		{
			return(0);
		}
		curCond=curCond->next;
	}
	return(res);
}



static cache_t *joinTables(table1,table2,conds,DB)
	cache_t	*table1,
		*table2;
	cond_t	*conds;
	char	*DB;
{
	cache_t	*tmpTable,
		*curTable,
		*outer,
		*inner;
	int	addCond,
		haveOr = 0,
		addPartial,
		oRowNum,
		iRowNum,
		clist[MAX_FIELDS],
		outerClist[MAX_FIELDS],
		res;
	cond_t	*newCondHead, 	*newCondTail,
		*t1CondHead, 	*t1CondTail,
		*t2CondHead, 	*t2CondTail,
		*outerConds,
		*newCond,
		*curCond,
		*keyCond;
	field_t	*curField,
		*tmpField,
		*keyField;
	char	*oRow,
		*iRow,
		active;
	u_char	*row;
	pkey_t	*key,
		*outerKey;


        msqlTrace(TRACE_IN,"joinTables()");

	/*
	** Create a condition list for all conditions that relate
	** to the newly created result table.  All we have to do is
	** look for a result table entry from the same original
	** table as the condition field as all table fields are
	** merged into the result.
	**
	** Also create condition lists for each source table so we
	** can do partial match optimisation on the outer loop.
	*/
	t1CondHead = t2CondHead = newCondHead = NULL;
	curCond = conds;
	while(curCond)
	{
	    addCond = 0;
	    addPartial = 0;
	    curTable = table1;
	    curField = curTable->def;
	    if (curCond->bool == OR_BOOL)
	    {
		haveOr = 1;
	    }

	    while(curField)
	    {
		if(strcmp(curField->table,curCond->table) == 0)
		{
		    if (curCond->value->type == IDENT_TYPE)
		    {
			/*
			** If it's an ident compare, only add the cond
			** if both idents are in the result table
			*/
			tmpField = table1->def;
			tmpTable = table1;
			while(tmpField)
			{
			    if(strcmp(tmpField->table,
				curCond->value->val.identVal->seg1)==0 &&
			       strcmp(tmpField->name,
				curCond->value->val.identVal->seg2)==0)
			    {
				addCond = 1;
				break;
			    }
			    tmpField = tmpField->next;
			    if (!tmpField)
			    {
				if (tmpTable == table1)
				{
				    	tmpTable = table2;
					tmpField = table2->def;
				}
			    }
			}
		    }
		    else
		    {
			addCond = 1;
			addPartial = 1;
		    }
		    break;
		}
	        curField = curField->next;
		if (!curField)
		{
			if (curTable == table1)
			{
				curField = table2->def;
				curTable = table2;
			}
		}
	    }
	    if (addCond)
	    {
		newCond = (cond_t *)fastMalloc(sizeof(cond_t));
		(void)bcopy(curCond,newCond,sizeof(cond_t));
		if (!newCondHead)
		{
		    newCondHead = newCondTail = newCond;
		}
		else
		{
		    newCondTail->next = newCond;
		    newCondTail = newCond;
		}
		newCond->next = NULL;
	     }
	     if (addPartial)
	     {
		if (strcmp(curCond->table, table1->table) == 0)
		{
			newCond = (cond_t *)fastMalloc(sizeof(cond_t));
			(void)bcopy(curCond,newCond,sizeof(cond_t));
			if(!t1CondHead)
			{
				t1CondHead = t1CondTail = newCond;
			}
			else
			{
				t1CondTail->next = newCond;
				t1CondTail = newCond;
			}
			newCond->next = NULL;
		}
		if (strcmp(curCond->table, table2->table) == 0)
		{
			newCond = (cond_t *)fastMalloc(sizeof(cond_t));
			(void)bcopy(curCond,newCond,sizeof(cond_t));
			if(!t2CondHead)
			{
				t2CondHead = t2CondTail = newCond;
			}
			else
			{
				t2CondTail->next = newCond;
				t2CondTail = newCond;
			}
			newCond->next = NULL;
		}
	    }
	    curCond = curCond->next;
	}

	/*
	** See if we can do partial match optimisation on either table
	*/
	outer = table1;
	inner = table2;
	outerConds = NULL;
	if (checkForPartialMatch(t1CondHead))
	{
		outerConds = t1CondHead;
		if(setupConds(table1,outerClist,outerConds,&outerKey) < 0)
		{
			return(NULL);
		}
	}
	else if(checkForPartialMatch(t2CondHead))
	{
		outer = table2;
		inner = table1;
		outerConds = t2CondHead;
		if(setupConds(table2,outerClist,outerConds,&outerKey) < 0)
		{
			return(NULL);
		}
	}

	/*
	** Create a table definition for the join result.  We can't do
	** this earlier as we must know which is the inner and outer table
	*/
	tmpTable = createTmpTable(outer,inner,NULL);
	if (!tmpTable)
	{
        	msqlTrace(TRACE_OUT,"joinTables()");
		return(NULL);
	}
	(void)sprintf(tmpTable->resInfo,"'%s (%s+%s)'",tmpTable->table,
		table1->table, table2->table);




	/*
	** See if we can use a key for the inner table
	**
	** This gets too ugly with the current condition handling.  This
	** can wait for the new expression based stuff in 1.1
	if (table2->keyFD > 0)
		curField = inner->def;
	else
		curField = NULL;
	keyField = NULL;
	while(curField && !keyField)
	{
    		if(curField->flags & PRI_KEY_FLAG)
    		{
			curCond = newCondHead;
			while(curCond)
			{
				if(strcmp(curField->table,curCond->table) == 0 
				   &&strcmp(curField->name,curCond->name) == 0)
				{
					keyField = curField;
					keyCond = curCond;
				}

				if (curCond->type == IDENT_TYPE)
				{
					if(!strcmp(curField->table,
						curCond->val.identVal->seg1)
				   	  &&!strcmp(curField->name,
						curCond->val.identVal->seg2))
					{
						keyVal = 
					}
				}
				curCond = curCond->next;
			}
		}
		curField = curField->next;
	}
	*
	*/

	/*
	** Do an N Squared join of the tables
	*/

	row = tmpTable->rowBuf;
	if (setupConds(tmpTable,clist,newCondHead,&key) < 0)
	{
        	msqlTrace(TRACE_OUT,"joinTables()");
		return(NULL);
	}
	oRowNum = 0;
	while((oRow = readRow(outer,&active,oRowNum++) ))
	{
		/*
		** Dodge holes 
		*/
		if (!active)
			continue;

		/* 
		** Partial match optimisation 
		*/
		if(outerConds)
		{
			if (matchRow(outer,oRow,outerConds,outerClist)!=1)
			{
				continue;
			}
		}

		/*
		** Go ahead and join this row with the inner table
		*/

		/*
		**
		if (keyField)
		{
			if (keyCond->type == IDENT_TYPE)
			{
				extractValues(oRow,
			}
		}
		**
		*/
		iRowNum = 0;
		while((iRow = readRow(inner,&active,iRowNum++) ))
		{
			if (!active)
				continue;
			*row = 1;
			mergeRows(row+1,outer,oRow,inner,iRow);
			if (!haveOr)
			{
				res=matchRow(tmpTable,row+1,newCondHead,
					clist);
			}
			else
			{
				res = 1;
			}
			if (res < 0)
			{
        			msqlTrace(TRACE_OUT,"joinTables()");
				return(NULL);
			}
			if (res == 1)
			{
				if(writeRow(tmpTable,NULL,NO_POS) < 0)
				{
        				msqlTrace(TRACE_OUT,
						"joinTables()");
					freeTmpTable(tmpTable);
					return(NULL);
				}
			}
		}
	}

	/*
	** Free up the space allocated to the new condition list.
	** We don't need to free the value structs as we just copied
	** the pointers to them.  They'll be freed during msqlClen();
	*/
	curCond = newCondHead;
	while(curCond)
	{
                newCond = curCond;
                curCond = curCond->next;
                (void)free((char *)newCond);
	}
	curCond = t1CondHead;
	while(curCond)
	{
                newCond = curCond;
                curCond = curCond->next;
                (void)free((char *)newCond);
	}
	curCond = t2CondHead;
	while(curCond)
	{
                newCond = curCond;
                curCond = curCond->next;
                (void)free((char *)newCond);
	}
	msqlTrace(TRACE_OUT,"joinTables()");
	return(tmpTable);
}



char *dupRow(entry,row)
	cache_t	*entry;
	char 	*row;
{
	char	*new;

	new = (char *)fastMalloc(entry->rowLen);
	(void)bcopy(row,new,entry->rowLen);
	return(new);
}



#ifndef HAVE_MMAP

	/* The Old Sorting Routine for those without a working mmap() */

cache_t *createSortedTable(entry,order)
	cache_t	*entry;
	order_t	*order;
{
	cache_t	*new;
	field_t	*curField;
	char	*row,
		*cur = NULL,
		*last,
		active;
	u_int	rowNum,
		numRows,
		curRowNum;
	int	flist[MAX_FIELDS],
		olist[MAX_FIELDS];
	
        msqlTrace(TRACE_IN,"createSortedTable()");
	new = createTmpTable(entry,NULL,NULL);
	if (!new)
	{
        	msqlTrace(TRACE_OUT,"createSortedTable()");
		return(NULL);
	}
	(void)sprintf(new->resInfo,"'%s (ordered %s)'",new->table,entry->table);
	if(initTable(entry,FULL_REMAP) < 0)
	{
		freeTmpTable(new);
        	msqlTrace(TRACE_OUT,"createSortedTable()");
		return(NULL);
	}
	if (setupOrder(entry,olist,order) < 0)
	{
		freeTmpTable(new);
        	msqlTrace(TRACE_OUT,"createSortedTable()");
		return(NULL);
	}
	if (setupFields(entry,flist,entry->def,NULL) < 0)
	{
		freeTmpTable(new);
        	msqlTrace(TRACE_OUT,"createSortedTable()");
		return(NULL);
	}


	numRows = 1;
	last = NULL;
	while(numRows > 0)
	{
		rowNum = 0;
		numRows = 0;
		if (cur)
		{
			(void)free(cur);
			cur = NULL;
		}
		while((row = readRow(entry,&active,rowNum)))
		{
			if (!active)
			{
				rowNum++;
				continue;
			}
			numRows ++;
			if (!cur)
			{
				if (cur)
					(void)free(cur);
				cur = dupRow(entry,row);
				curRowNum = rowNum;
				rowNum++;
				continue;
			}
			if (compareRows(cur,row,order,olist) >= 0)
			{
				if (compareRows(last,row,order,olist) <= 0 ||
				    !last)
				{
					if (cur)
						(void)free(cur);
					cur = dupRow(entry,row);
					curRowNum = rowNum;
				}
			}
			rowNum++;
		}
		if (cur)
		{
			extractValues(cur,new->def,flist);
			(void)bzero((char*)(new->rowBuf+1),new->rowLen);
			fillRow(new->rowBuf + 1, new->def, flist);
			writeRow(new,NULL,NO_POS);
			deleteRow(entry,curRowNum);
			curField = new->def;
			while(curField)
			{
				freeValue(curField->value);
				curField->value = NULL;
				curField = curField->next;
			}
			if (last)
			{
				(void)free(last);
			}
			last = dupRow(entry,cur);
		}
	}
	if (last)
		(void)free(last);
       	msqlTrace(TRACE_OUT,"createSortedTable()");
	return(new);
}

#else

	/* 
	** The new sorting routine for those with mmap()
	**
	** This is an algorithm I came up with just to do things a bit
	** faster (i.e. less shuffles and a rapid set reduction).
	** Call it bambi sort for now :-)
	*/

bSwap(entry, low, high)
	cache_t	*entry;
	u_int	low,
		high;
{
	char	*tmp,
		lowActive,
		highActive;

	tmp = readRow(entry,&lowActive,low);
	bcopy(tmp,qSortRowBuf,entry->rowLen);
	tmp = readRow(entry,&highActive,high);
	writeRow(entry, tmp, low);
	writeRow(entry, qSortRowBuf, high);
}



static bSort(entry, order, olist, low, high)
	cache_t	*entry;
	order_t	*order;
	int	*olist;
	u_int	low,
		high;
{
	u_int 	newHigh = -1,
		newLow = -1,
		index;
	REG char *curRow,
		*lowRow,
		*highRow;
	char	active;


	/* can we bail out without doing anything */
	if (low >= high)
	{
		return;
	}

	/* OK, go for it */
	lowRow = readRow(entry,&active,low);
	while (!active && low < high)
	{
		low++;
		lowRow = readRow(entry,&active,low);
	}
	if (!active)
	{
		return;
	}
		
	highRow = readRow(entry,&active,high);
	while (!active && high > low)
	{
		high--;
		highRow = readRow(entry,&active,high);
	}
	if (!active)
	{
		return;
	}
	
	for (index=low; index <= high; index++)
	{
		curRow = readRow(entry,&active,index);
		if(!active)
		{
			continue;
		}
		if (compareRows(curRow, lowRow, order, olist) < 0)
		{
			newLow = index;
			lowRow = curRow;
		}
		if (compareRows(curRow, highRow, order, olist) > 0)
		{
			newHigh = index;
			highRow = curRow;
		}
	}
	if (newLow != -1)
	{
		bSwap(entry,low,newLow);
	}
	if (newHigh != -1)
	{
		if (newHigh == low)
			bSwap(entry,high,newLow);
		else
			bSwap(entry,high,newHigh);
	}
	if (high != 0)
	{
		bSort(entry, order, olist, low+1, high-1);
	}
}



static int createSortedTable(entry,order)
	cache_t	*entry;
	order_t	*order;
{
	int	olist[MAX_FIELDS];
	u_int	numRows;

	msqlTrace(TRACE_IN,"createSortedTable()");

	if(initTable(entry,FULL_REMAP) < 0)
	{
		msqlTrace(TRACE_OUT,"createSortedTable()");
		return(-1);
	}
	if (setupOrder(entry,olist,order) < 0)
	{
		msqlTrace(TRACE_OUT,"createSortedTable()");
		return(-1);
	}


	numRows = (entry->size / (entry->rowLen+1));
	if (numRows > 0)
	{
		qSortRowBuf = (char *)malloc(entry->rowLen);
		bSort(entry, order, olist, 0, numRows > 0 ? numRows-1 : 0);
		free(qSortRowBuf);
	}

	msqlTrace(TRACE_OUT,"createSortedTable()");
	return(0);
}

#endif


int createDistinctTable(entry)
	cache_t	*entry;
{
	char	*row,
		*cur = NULL,
		active;
	u_int	rowNum,
		curRowNum;
	int	flist[MAX_FIELDS];
	

	if(initTable(entry,FULL_REMAP) < 0)
	{
		return(-1);
	}
	if (setupFields(entry,flist,entry->def,NULL) < 0)
	{
		return(-1);
	}


	curRowNum = 0;
	while((row = readRow(entry,&active,curRowNum)))
	{
		if (!active)
		{
			curRowNum++;
			continue;
		}
#ifdef HAVE_MMAP
		cur = row;
#else
		if (cur)
			(void)free(cur);
		cur = dupRow(entry,row);
#endif
		rowNum = curRowNum;
		rowNum = 0;
		while((row = readRow(entry,&active,rowNum)))
		{
			if (!active)
			{
				rowNum++;
				continue;
			}
			if (rowNum == curRowNum)
			{
				rowNum++;
				continue;
			}

			if (bcmp(cur,row,entry->rowLen) == 0)
			{
				deleteRow(entry,rowNum);
			}
			rowNum++;
		}
		curRowNum++;
	}
#ifndef HAVE_MMAP
	if (cur)
		(void)free(cur);
#endif
	return(0);
}




static int doSelect(cacheEntry, tables, fields,conds,dest,tmpTable)
	cache_t	*cacheEntry;
	tname_t	*tables;
	field_t	*fields;
	cond_t	*conds;
	int	dest;
	cache_t	*tmpTable;
{
	int	flist[MAX_FIELDS],
		clist[MAX_FIELDS],
		tmpFlist[MAX_FIELDS],
		rowLen,
		rowNum,
		numFields,
		numMatch,
		res;
	char	*row,
		active,
		useKey,
		outBuf[100];
	cond_t	*curCond;
	pkey_t	*key;
	REG 	field_t *curField;


	msqlTrace(TRACE_IN,"doSelect()");

	fieldHead = fields;
	
	numFields = 0;
	curField = fieldHead;
	while(curField)
	{
		numFields++;
		curField = curField->next;
	}

	/*
	** Find the offsets of the given fields and condition
	*/
	(void)bzero(flist,MAX_FIELDS * sizeof(int));
	if (setupFields(cacheEntry,flist,fields,&key) < 0)
	{
		msqlTrace(TRACE_OUT,"doSelect()");
		return(-1);
	}
	(void)bzero(clist,MAX_FIELDS * sizeof(int));
	if (setupConds(cacheEntry,clist,conds,&key) < 0)
	{
		msqlTrace(TRACE_OUT,"doSelect()");
		return(-1);
	}

	if (tmpTable)
	{
		(void)bzero(tmpFlist,MAX_FIELDS * sizeof(int));
		if (setupFields(tmpTable,tmpFlist,fields,NULL) < 0)
		{
			msqlTrace(TRACE_OUT,"doSelect()");
			return(-1);
		}
	}

	rowLen = cacheEntry->rowLen;

	if (initTable(cacheEntry,FULL_REMAP) < 0)
	{
		msqlTrace(TRACE_OUT,"doSelect()");
		return(-1);
	}

	/*
	** Tell the client how many fields there are in a row
	*/

	if (dest == DEST_CLIENT)
	{
		sprintf(packet,"1:%d:\n",numFields);
		if (writePkt(outSock) < 0)
		{
			msqlTrace(TRACE_OUT,"doSelect() write error");
			return(0);
		}
	}


	/*
	** If the data is keyed and it's a simple condition just grab the row
	*/

	useKey = 0;
	if (key)
	{
		if (key->op == EQ_OP)
		{
			useKey = 1;
			curCond = conds;
			while(curCond)
			{
				if (curCond->bool == OR_BOOL)
				{
					useKey = 0;
					break;
				}
				curCond = curCond->next;
			}
		}
		if (cacheEntry->keyFD < 0 || !cacheEntry->keyBuf)
		{
			useKey = 0;
		}
	}

	if (useKey)
	{
		rowNum = readKey(cacheEntry,key);	
		msqlDebug(MOD_KEY,"Primary Key gave row %u\n",rowNum);
		if (rowNum != NO_POS)
		{
			row = readRow(cacheEntry,&active,rowNum);
			if (active)
			{
				res = matchRow(cacheEntry,row,conds,clist);
				if (res < 0)
				{
					return(-1);
				}
				if (res == 1)
				{
					extractValues(row,fields,flist);
					if (dest == DEST_CLIENT)
					{
						bzero(packet,PKT_LEN);
						formatPacket(packet,fields);
						if (writePkt(outSock) < 0)
						{
							msqlTrace(TRACE_OUT,
								"doSelect");
							return(0);
						}
					}
					else
					{
						bzero((char*)(tmpTable->rowBuf
							+1), tmpTable->rowLen);
						fillRow(tmpTable->rowBuf + 1, 
							fields, tmpFlist);
						writeRow(tmpTable,NULL,NO_POS);
					}
				}
			}
		}
	}
	else
	{
		rowNum = 0;
		numMatch = 0;
		while((row = readRow(cacheEntry,&active,rowNum++) ))
		{
			if (!active)
				continue;
			res = matchRow(cacheEntry,row,conds,clist);
			if (res < 0)
			{
				return(-1);
			}
			if (res == 1)
			{
				if ( (msqlSelectLimit) && 
				     (++numMatch > msqlSelectLimit) &&
				     (dest == DEST_CLIENT)
				   )
				{
  					break;
				}
				extractValues(row,fields,flist);
				if (dest == DEST_CLIENT)
				{
					bzero(packet,PKT_LEN);
					formatPacket(packet,fields);
					if (writePkt(outSock) < 0)
					{
						msqlTrace(TRACE_OUT,"doSelect");
						return(0);
					}
				}
				else
				{
					bzero((char*)(tmpTable->rowBuf+1),
							tmpTable->rowLen);
					fillRow(tmpTable->rowBuf + 1, fields,
						tmpFlist);
					writeRow(tmpTable,NULL,NO_POS);
				}
			}
		}
	}
	if (dest == DEST_CLIENT)
	{
		sprintf(packet,"-100:\n");
		if (writePkt(outSock) < 0)
		{
			msqlTrace(TRACE_OUT,"doSelect");
			return(0);
		}

		/*
		** Send the field info down the line to the client
		*/
		curField = fields;
		while(curField)
		{
			sprintf(outBuf,"%d",curField->length);
			sprintf(packet,"%d:%s%d:%s1:%d%d:%s1:%s1:%s", 
				strlen(curField->table), curField->table,
				strlen(curField->name), curField->name, 
				curField->type,strlen(outBuf), outBuf, 
				curField->flags & NOT_NULL_FLAG ? "Y":"N", 
				curField->flags & PRI_KEY_FLAG ? "Y":"N");
			if (writePkt(outSock) < 0)
			{
				msqlTrace(TRACE_OUT,"doSelect");
				return(0);
			}
			curField = curField->next;
		}
		sprintf(packet,"-100:\n");
		writePkt(outSock);
	}
	msqlTrace(TRACE_OUT,"doSelect()");
	return(0);
}




extern	field_t	*fieldHead;

int msqlSelect(tables,fields,conds,order,DB)
	tname_t	*tables;
	field_t	*fields;
	cond_t	*conds;
	order_t	*order;
	char	*DB;
{
	cache_t	*cacheEntry,
		*table1,
		*table2,
		*tmpTable,
		*prevTable;
	REG	tname_t	*curTable;
	REG	field_t	*curField;
	REG	cond_t	*curCond;
	int	join,
		foundTable;

	msqlTrace(TRACE_IN,"msqlSelect()");

	/*
	** Check out the tables and fields specified in the query.  If
	** multiple tables are specified all field specs must be
	** qualified and they must reference a selected table.
	*/

	curTable = tables;
	tmpTable = NULL;
	if (curTable->next)
	{
		join = 1;
	}
	else
	{
		cond_t	*curCond;

		/*
		** If there's no joins ensure that each conditionand field
		** is fully qualified with the correct table
		*/
		qualifyFields(tables->name,fields);
		qualifyConds(tables->name,conds);
		qualifyOrder(tables->name,order);
		join = 0;
	}

	/*
	** Ensure that any field or condition refers to fields of 
	** selected tables
	*/
	curField = fields;
	while(curField)
	{
		curTable = tables;
		while(curTable)
		{
			if (strcmp(curField->table,curTable->name) == 0)
			{
				break;
			}
			curTable = curTable->next;
		}
		if (!curTable)
		{
			sprintf(errMsg,UNSELECT_ERROR,curField->table);
			return(-1);
		}
		curField = curField->next;
	}
	curCond = conds;
	while(curCond)
	{
		curTable = tables;
		while(curTable)
		{
			if (strcmp(curCond->table,curTable->name) == 0)
			{
				break;
			}
			curTable = curTable->next;
		}
		if (!curTable)
		{
			sprintf(errMsg,UNSELECT_ERROR,curCond->table);
			return(-1);
		}
		curCond = curCond->next;
	}



	curField = fields;
	while (curField)
	{
		if (*(curField->table) == 0)
		{
			if (join)
			{
				sprintf(errMsg, UNQUAL_JOIN_ERROR, 
					curField->name);
				return(-1);
			}
			curField = curField->next;
			continue;
		}
		curTable = tables;
		foundTable = 0;
		while(curTable)
		{
			if (strcmp(curTable->name,curField->table) == 0)
			{
				foundTable = 1;
				break;
			}
			curTable = curTable->next;
		}
		if (!foundTable)
		{
			sprintf(errMsg,UNSELECT_ERROR, curField->table);
			return(-1);
		}
		curField = curField->next;
	}


	/*
	** If there's multiple tables, join the suckers.
	*/

	if (join)
	{
		curTable = tables;
		while(curTable)
		{
			if (curTable == tables)
			{
				table1 = loadTableDef(curTable->name,
					curTable->cname,DB);
				if (!table1)
				{
					msqlTrace(TRACE_OUT,"msqlSelect()");
					return(-1);
				}
				if (initTable(table1,FULL_REMAP) < 0)
				{
					msqlTrace(TRACE_OUT,"msqlSelect()");
					return(-1);
				}
				curTable = curTable->next;
				table2 = loadTableDef(curTable->name,
					curTable->cname,DB);
				if (!table2)
				{
					msqlTrace(TRACE_OUT,"msqlSelect()");
					return(-1);
				}
				if (initTable(table2,FULL_REMAP) < 0)
				{
					msqlTrace(TRACE_OUT,"msqlSelect()");
					return(-1);
				}
				if (!table1 || !table2)
				{
					msqlTrace(TRACE_OUT,"msqlSelect()");
					return(-1);
				}
				tmpTable = joinTables(table1,table2,conds,DB);
				if (!tmpTable)
				{
					msqlTrace(TRACE_OUT,"msqlSelect()");
					return(-1);
				}
					
			}
			else
			{
				table1 = tmpTable;
				table2 = loadTableDef(curTable->name,
					curTable->cname,DB);
				if (!table2)
				{
					msqlTrace(TRACE_OUT,"msqlSelect()");
					return(-1);
				}
				if (initTable(table1,FULL_REMAP) < 0)
				{
					msqlTrace(TRACE_OUT,"msqlDelete()");
					return(-1);
				}
				if (initTable(table2,FULL_REMAP) < 0)
				{
					msqlTrace(TRACE_OUT,"msqlDelete()");
					return(-1);
				}
				tmpTable = joinTables(table1,table2,conds,DB);
				if (table1->result)
				{
					freeTmpTable(table1);
				}
				if (!tmpTable)
				{
					return(-1);
				}
			}
			curTable = curTable->next;
		}
	}

	/*
	** Perform the actual select.  If there's an order clause or
	** a pending DISTINCT, send the results to a table for further 
	** processing.
	**
	** Look for the wildcard field spec.  Must do this before we
	** "setup" because it edits the field list.  selectWildcard
	** is a global set from inside the yacc parser.  Wild card
	** expansion is only called if this is set otherwise it will
	** consume 50% of the execution time of selects!
	*/

	if (!tmpTable)
	{
		if((cacheEntry = loadTableDef(tables->name,tables->cname,
			DB)) == NULL)
		{
			msqlTrace(TRACE_OUT,"msqlSelect()");
			return(-1);
		}
	}
	else
	{
		cacheEntry = tmpTable;
		/* conds = NULL; */
	}
	if (selectWildcard)
	{
		fields = expandFieldWildCards(cacheEntry,fields);
	}

	if (!order && !selectDistinct)
	{
		if (doSelect(cacheEntry,tables,fields,conds,
			DEST_CLIENT,NULL) < 0)
		{
			msqlTrace(TRACE_OUT,"msqlSelect()");
			return(-1);
		}
		if (cacheEntry->result)
		{
			freeTmpTable(cacheEntry);
		}
		msqlTrace(TRACE_OUT,"msqlSelect()");
		return(0);
	}

	/*
	** From here on we just want a table with the required fields
	** (i.e. not all the fields of a join)
	*/
	tmpTable = createTmpTable(cacheEntry,NULL,fields);
	if (!tmpTable)
	{
		return(-1);
	}
	(void)sprintf(tmpTable->resInfo,"'%s (stripped %s)'",
		tmpTable->table,cacheEntry->table);
	if (doSelect(cacheEntry,tables,fields,conds,
		DEST_TABLE,tmpTable) < 0)
	{
		msqlTrace(TRACE_OUT,"msqlSelect()");
		return(-1);
	}
	if (cacheEntry->result)
	{
		freeTmpTable(cacheEntry);
	}
	cacheEntry = tmpTable;

	/*
	** Blow away multiples if required
	*/
	if (selectDistinct)
	{
		if (createDistinctTable(cacheEntry) < 0)
		{
			msqlTrace(TRACE_OUT,"msqlSelect()");
			return(-1);
		}
	}
	
	/*
	** Sort the result if required
	*/
	if (order)
	{
		cache_t	*sortTable;


#ifndef HAVE_MMAP
		/* 
		** If you don't have mmap() you'll be using the old
		** sorting code
		*/
		sortTable = createSortedTable(cacheEntry,order);
		if (cacheEntry->result)
		{
			freeTmpTable(cacheEntry);
		}
		if (!sortTable)
		{
			msqlTrace(TRACE_OUT,"msqlSelect()");
			return(-1);
		}
		cacheEntry = sortTable;

#else
		/*
		** If mmap() is in use you'll be using the new sorting
		** code that does the sort in place
		*/
		if (createSortedTable(cacheEntry,order) < 0)
		{
			if (cacheEntry->result)
				freeTmpTable(cacheEntry);
			msqlTrace(TRACE_OUT,"msqlSelect()");
			return(-1);
		}
#endif

	}


	/*
	** Send the result to the client if we haven't yet.
	*/
	if (doSelect(cacheEntry,tables,fields,NULL,DEST_CLIENT,NULL)<0)
	{
		if (cacheEntry->result)
			freeTmpTable(cacheEntry);
		msqlTrace(TRACE_OUT,"msqlSelect()");
		return(-1);
	}

	/*
	** Free the result table
	*/
	if (cacheEntry->result)
	{
		freeTmpTable(cacheEntry);
	}


	msqlTrace(TRACE_OUT,"msqlSelect()");
	return(0);
}






void msqlCreateDB(sock,db)
	int	sock;
	char	*db;
{
	char	path[255];
	DIR	*dirp;

	/*
	** See if the directory exists
	*/

	(void)sprintf(path,"%s/msqldb/%s", msqlHomeDir, db);
	dirp = opendir(path);
	if (dirp)
	{
		closedir(dirp);
		sprintf(packet,"-1:Error creating database : %s exists!\n",db);
                writePkt(sock);
		return;
	}

	/*
	** Create the directory
	*/
	if (mkdir(path,0700) < 0)
	{
		sprintf(packet,"-1:Error creating database\n");
		writePkt(sock);
		return;
	}
	sprintf(packet,"-100:\n");
	writePkt(sock);
}


void msqlDropDB(sock,db)
	int	sock;
	char	*db;
{
	char	path[255],
		filePath[255],
		buf[10];
	DIR	*dirp;
#ifdef HAVE_DIRENT
	struct	dirent *cur;
#else
	struct	direct *cur;
#endif
	int	index;
	cache_t	*entry;

	/*
	** Invalidate any cache entries that are for this DB
	*/
	index = 0;
	while(index < CACHE_SIZE)
	{
		entry = tableCache + index++;
		if (strcmp(entry->DB,db) == 0)
		{
			freeTableDef(entry->def);
			entry->def = NULL;
			*(entry->DB) = 0;
			*(entry->table) = 0;
			entry->age = 0;
			safeFree(entry->rowBuf);
			safeFree(entry->keyBuf);
			close(entry->stackFD);
			close(entry->dataFD);
#ifdef NEW_DB
			if (entry->dbp)
			{
				entry->dbp->close(entry->dbp);
				entry->dbp = NULL;
			}
#else
			if (entry->keyFD >= 0)
				close(entry->keyFD);
#endif

#ifdef HAVE_MMAP
			if (entry->dataMap != (caddr_t) NULL)
			{
				munmap(entry->dataMap,entry->size);
				entry->dataMap = NULL;
				entry->size = 0;
			}
			if (entry->keyMap != (caddr_t) NULL)
			{
				munmap(entry->keyMap,entry->keySize);
				entry->keyMap = NULL;
				entry->keySize = 0;
			}
#endif
		}
	}

	/*
	** See if the directory exists
	*/

	(void)sprintf(path,"%s/msqldb/%s", msqlHomeDir, db);
	dirp = opendir(path);
	if (!dirp)
	{
		sprintf(packet,"-1:Error dropping database : %s doesn't exist\n"
			, db);
		writePkt(sock);
		return;
	}

	/*
	** Blow away any files but dodge '.' and '..'
	*/

	cur = readdir(dirp);
	cur = readdir(dirp);
	cur = readdir(dirp);
	while(cur)
	{
		sprintf(filePath,"%s/%s",path,cur->d_name);
		unlink(filePath);
		cur = readdir(dirp);
	}
		
	if (rmdir(path) < 0)
	{
		sprintf(packet,"-1:Error dropping database\n");
		writePkt(sock);
		closedir(dirp);
		return;
	}
	closedir(dirp);

	sprintf(packet,"-100:\n");
	writePkt(sock);
}
