/*
**	msqldb.c	- 
**
**
** Copyright (c) 1993  David J. Hughes
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
#include <common/portability.h>
#include <regexp/regexp.h>

#include "y.tab.h"

#define	_MSQL_SERVER_SOURCE

#include "msql_priv.h"
#include "msql.h"

#define NO_POS		0xFFFFFFFF
#define	REG		register

#define TYPE_ERR	"Literal value for \'%s\' does not match field type!\n"


static	cache_t	tableCache[CACHE_SIZE];

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
#endif
			}
			else
			{
				free(cur->addr);
			}
			free(cur);
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
		perror("mmap");
	}
	return(res);
}

#define mmap(a,l,p,fl,fd,o)	MMap(a,(size_t)l,p,fl,fd,o,__FILE__,__LINE__)
#define munmap(a,l) 		MUnmap(a,(size_t)l,__FILE__,__LINE__)


#endif

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
		/*  An inline bzero() */
		cp1 = cp;
		while (cp1-cp < size)
		{
			*cp1++ = 0;
		}
		
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



int openKey(table,DB)
	char	*table;
	char	*DB;
{
	char	path[255];

	(void)sprintf(path,"%s/msqldb/%s/%s.key",msqlHomeDir,DB,table);
	return(open(path,O_RDWR));
}





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
				strlen(value.val.charVal));
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
		sprintf(errMsg,"Can't open directory \"%s\"",path);
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
		sprintf(errMsg,"Can't open directory \"%s\"",path);
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
	if((cacheEntry = loadTableDef(table,DB)))
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
	}
	sprintf(packet,"-100:\n");
	writePkt(sock);
	msqlTrace(TRACE_OUT,"msqlListFields()");
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
	msqlTrace(TRACE_IN,"freeCacheEntry()");
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
	close(entry->keyFD);
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

field_t *readTableDef(table,DB,keyLen)
	char	*table,
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
		sprintf(errMsg,"Unknown table \"%s\"",table);
		msqlDebug(MOD_ERR,"Unknown table \"%s\"\n",table);
		msqlTrace(TRACE_OUT,"readTableDef()");
		return(NULL);
	}
	numBytes = read(fd,buf,sizeof(buf));
	if (numBytes < 1)
	{
		sprintf(errMsg,"Error reading table \"%s\" definition",table);
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
		curField = (field_t *)malloc(sizeof(field_t));
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
		*cp;
	int	fd,
		foundField;


	/*
	** Create a name for this tmp table
	*/
	msqlTrace(TRACE_IN,"createTmpTable()");
	tmpfile = (char *)tmpnam(NULL);
	cp = (char *)rindex(tmpfile,'/');
	if (cp)
	{
		tmpfile = cp+1;
	}
	(void)sprintf(path,"%s/msqldb/.tmp/%s.dat",msqlHomeDir,tmpfile);
	(void)sprintf(path,"/tmp/%s.dat",tmpfile);


	/*
	** start building the table cache entry
	*/
	new = (cache_t *)malloc(sizeof(cache_t));
	if (!new)
	{
		sprintf(errMsg,"Out of memory for temporary table");
		msqlDebug(MOD_ERR,"Out of memory for temporary table (%s)\n"
		,path);
		msqlTrace(TRACE_OUT,"createTmpTable()");
		return(NULL);
	}
	(void)strcpy(new->table,tmpfile);
	fd = open(path,O_RDWR|O_CREAT|O_TRUNC, 0700);
	if (fd < 0)
	{
		sprintf(errMsg,"Couldn't create temporary table");
		msqlDebug(MOD_ERR,"Couldn't create temporary table (%s)\n",
			path);
		(void)free(new);
		msqlTrace(TRACE_OUT,"createTmpTable()");
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
			newField->next = (field_t *)malloc(sizeof(field_t));
			newField = newField->next;
		}
		else
		{
			new->def=newField=(field_t *)malloc(sizeof(field_t));
		}
		(void)bcopy(curField,newField,sizeof(field_t));
		if( *(newField->table) == 0)
		{
			(void)strcpy(newField->table,table1->table);
		}
		newField->flags=0;
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
				newField->next = (field_t *)malloc(
					sizeof(field_t));
				newField = newField->next;
			}
			else
			{
				new->def=newField=(field_t *)malloc(
					sizeof(field_t));
			}
			(void)bcopy(curField,newField,sizeof(field_t));
			(void)strcpy(newField->table,table2->table);
			new->rowLen += curField->length + 1;
			curField = curField->next;
		}
	}

	if (newField)	
	{
		newField->next = NULL;
	}
	new->rowBuf = (char *)malloc(new->rowLen+1);
	new->keyLen = 0;
	msqlTrace(TRACE_OUT,"createTmpTable()");
	return(new);
}




static void freeTmpTable(entry)
	cache_t	*entry;
{
	char	path[255];

        msqlTrace(TRACE_IN,"freeTmpTable()");
	(void)sprintf(path,"%s/msqldb/.tmp/%s.dat",msqlHomeDir,entry->table);
	(void)sprintf(path,"/tmp/%s.dat",entry->table);
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
	close(entry->keyFD);
	(void)free(entry);
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


cache_t *loadTableDef(table,DB)
	char	*table,
		*DB;
{
	int	maxAge,
		cacheIndex,
		keyLen;
	field_t	*def;
	REG 	cache_t	*entry;
	REG 	int	count;
	char	path[255];


	/*
	** Look for the entry in the cache.  Keep track of the oldest
	** entry during the pass so that we can replace it if needed
	*/
	msqlTrace(TRACE_IN,"loadTableDef()");
	msqlDebug(MOD_CACHE,"Table cache search for %s:%s\n",table,DB);
	count = cacheIndex = 0;
	maxAge = -1;
	while(count < CACHE_SIZE)
	{
		entry = tableCache + count;
		msqlDebug(MOD_CACHE,"Cache entry %d = %s:%s, age = %d\n", count,
			entry->table?entry->table:"NULL",
			entry->DB?entry->DB:"NULL", 
			entry->age);
		if ((strcmp(entry->DB,DB)==0)&&(strcmp(entry->table,table)==0))
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
		(void)close(entry->keyFD);
		freeTableDef(entry->def);
		safeFree(entry->rowBuf);
		safeFree(entry->keyBuf);
		entry->def = NULL;
	}

	/*
	** Now load the new entry
	*/
	def = readTableDef(table,DB,&keyLen);
	if (!def)
	{
		sprintf(errMsg,"Couldn't read table definition for %s",table);
		msqlDebug(MOD_ERR,"Couldn't read table definition for %s\n",table);
		msqlTrace(TRACE_OUT,"loadTableDef()");
		return(NULL);
	}
	entry->def = def;
	entry->age = 1;
	entry->result = 0;
	entry->keyLen = keyLen;
	strcpy(entry->DB,DB);
	strcpy(entry->table,table);
	
	msqlDebug(MOD_CACHE,"Loading cache entry %d (%s:%s)\n", cacheIndex, 
		entry->DB, entry->table);
	if((entry->dataFD = openTable(table,DB)) < 0)
	{
		sprintf(errMsg,"Couldn't open data file for %s");
		msqlTrace(TRACE_OUT,"loadTableDef()");
		return(NULL);
	}
	if((entry->stackFD = openStack(table,DB)) < 0)
	{
		sprintf(errMsg,"Couldn't open stack file for %s");
		msqlTrace(TRACE_OUT,"loadTableDef()");
		return(NULL);
	}
	entry->keyFD = openKey(table,DB);

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
	entry->rowBuf = (char *)malloc(entry->rowLen + 2);
	entry->keyBuf = (char *)malloc(entry->keyLen + 1);
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
			if (cacheEntry->dataMap != (caddr_t) NULL &&
			    cacheEntry->dataMap != (caddr_t) -1 )
			{
                		munmap(cacheEntry->dataMap, cacheEntry->size);
			}
			fstat(cacheEntry->dataFD, &sbuf);
			cacheEntry->size = sbuf.st_size;
			if (cacheEntry->size)
			{
				cacheEntry->dataMap = (caddr_t)mmap(NULL, 
					cacheEntry->size, 
					(PROT_READ | PROT_WRITE), 
					MAP_SHARED, cacheEntry->dataFD, 0);
				if (cacheEntry->dataMap == (caddr_t)-1)
					return(-1);
			}
			cacheEntry->remapData = 0;
		}
	}
	if (mapFlag && FULL_REMAP || mapFlag && KEY_REMAP)
	{
		if (cacheEntry->remapKey)
		{
			if (cacheEntry->keyMap)
			{
               			munmap(cacheEntry->keyMap, 
					cacheEntry->keySize);
			}
			fstat(cacheEntry->keyFD, &sbuf);
			cacheEntry->keySize = sbuf.st_size;
			if (cacheEntry->keySize)
			{
				cacheEntry->keyMap = (caddr_t) mmap(NULL, 
					cacheEntry->keySize,
					PROT_READ | PROT_WRITE, MAP_SHARED, 
					cacheEntry->keyFD, 0);
				if (cacheEntry->keyMap == (caddr_t)-1)
					return(-1);
			}
			cacheEntry->remapKey = 0;
		}
	}
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
	char	active = 1;
	REG	off_t	seekPos;
	char	*buf;


	if (rowNum == NO_POS)
	{
		msqlDebug(MOD_ACCESS,"writeRow() : append to %s\n",
			(cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);
	}
	else
	{
		msqlDebug(MOD_ACCESS,"writeRow() : write at row %u of %s\n",
			rowNum, (cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);
	}
	*cacheEntry->rowBuf = active;
	if (rowNum == NO_POS)  /* append and flag remap */
	{
		cacheEntry->remapData = 1;
		lseek(cacheEntry->dataFD,(off_t)0, SEEK_END);
		if (row)
		{
			bcopy(cacheEntry->rowBuf+1,row,cacheEntry->rowLen);
		}	
		if (write(cacheEntry->dataFD,cacheEntry->rowBuf,
			cacheEntry->rowLen + 1) < 0)
		{
			sprintf(errMsg,"Data write failed!");
			return(-1);
		}
	}
	else
	{
		seekPos = rowNum * (cacheEntry->rowLen + 1);
		buf = ((char *)cacheEntry->dataMap) + seekPos;
		if (row)
		{
			*buf = 1;
			bcopy(row, buf+1, cacheEntry->rowLen);
		}
		else
		{
			bcopy(cacheEntry->rowBuf, buf, cacheEntry->rowLen+1);
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
	char	active=1;

	msqlTrace(TRACE_IN,"writeRow()");
	if (rowNum == NO_POS)
	{
		msqlDebug(MOD_ACCESS,"writeRow() : append to %s\n",
			(cacheEntry->result)?cacheEntry->resInfo:
			cacheEntry->table);
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
		sprintf(errMsg,"Data write failed!");
		msqlTrace(TRACE_OUT,"writeRow()");
		return(-1);
	}
	msqlTrace(TRACE_OUT,"writeRow()");
	return(0);
}

#endif



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
		sprintf(errMsg,"Write of key failed!");
		msqlTrace(TRACE_OUT,"writeKey()");
		return(-1);
	}
	msqlTrace(TRACE_OUT,"writeKey()");
	return(0);
}



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
	maxRow = cacheEntry->keySize / (cacheEntry->keyLen + 1);
	while(rowNum < maxRow)
	{
		seekPos = rowNum * (cacheEntry->keyLen + 1);
		if ((seekPos > cacheEntry->keySize) || !cacheEntry->keyMap)
		{
			return(NO_POS);
		}
		buf = ((char *)cacheEntry->keyMap) + seekPos;
		if (*buf)
		{
			if(bcmp(cacheEntry->keyBuf+1,buf+1,cacheEntry->keyLen)==0)
			{
				return(rowNum);
			}
		}
		rowNum++;
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



/****************************************************************************
** 	_deleteRow
**
**	Purpose	: Invalidate a row in the table
**	Args	: datafile FD, rowlength, desired row location
**	Returns	: -1 on error
**	Notes	: This only sets the row header byte to 0 indicating
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
		sprintf(errMsg,"Seek into data table failed!");
		msqlTrace(TRACE_OUT,"deleteRow()");
		return(-1);
	}
	activeBuf = 0;
	if (write(fd,&activeBuf,1) < 0)
	{
		sprintf(errMsg,"Data write failed!");
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
			sprintf(errMsg,"Seek into key table failed!");
			msqlTrace(TRACE_OUT,"deleteKey()");
			return(-1);
		}
		activeBuf = 0;
		if (write(fd,&activeBuf,1) < 0)
		{
			sprintf(errMsg,"Write of key failed");
			msqlTrace(TRACE_OUT,"deleteKey()");
			return(-1);
		}
	}
#endif
	msqlTrace(TRACE_OUT,"deleteKey()");
	return(0);
}






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
			sprintf(errMsg,"Field \"%s\" cannot be null",
				curField->name);
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
		sprintf(errMsg,"Too many fileds in query");
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
								TYPE_ERR,
								curField->name);
							msqlDebug(MOD_ERR,TYPE_ERR,
								curField->name);
							return(-1);
						}
						break;
		
					case CHAR_TYPE:
						if(curField->value->type
							!= CHAR_TYPE)
						{
							sprintf(errMsg,
								TYPE_ERR,
								curField->name);
							msqlDebug(MOD_ERR,TYPE_ERR,
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
								TYPE_ERR,
								curField->name);
							msqlDebug(MOD_ERR,TYPE_ERR,
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
		    sprintf(errMsg,"Unknown field \"%s.%s\"",
				curField->table,curField->name);
		    msqlDebug(MOD_ERR,"Unknown field \"%s.%s\"\n",
				curField->table,curField->name);
		    msqlTrace(TRACE_OUT,"setupFields()");
		    return(-1);
		}
		else
		{
		    sprintf(errMsg,"Unknown field \"%s\"",curField->name);
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
		sprintf(errMsg,"Too many fields in condition");
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
			sprintf(errMsg,"Unknown field in where clause \"%s\"",
				curCond->name);
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
		sprintf(errMsg,"Too many fields in order specification");
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
			sprintf(errMsg,"Unknown field in order clause \"%s\"",
				curOrder->name);
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
					bcopy(&(curField->value->val.intVal),cp,
						sizeof(int));
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
					bcopy(&(curField->value->val.realVal),cp,
						sizeof(double));
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
		if (!curField->value->nullVal)
                {
                        cp = row + *offset;
                        *cp = '\001';
                        cp++;
			switch(curField->type)
			{
				case INT_TYPE:
#ifndef _CRAY
					bcopy(&(curField->value->val.intVal),cp,
						sizeof(int));
#else
				 	packInt32(curField->value->val.intVal, 
						cp);
#endif
					break;
		
				case CHAR_TYPE:
					(void)bzero(cp, curField->length);
					strncpy(cp, curField->value->val.charVal,
						curField->length);
					break;

				case REAL_TYPE:
					bcopy(&(curField->value->val.realVal),cp,
						sizeof(double));
					break;
			}
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
	char	*row;
	field_t	*fields;
	int	flist[];
{
	field_t	*curField;
	char	*cp;
	int	ip,
		*offset;
	double	*fp;
	char	buf[8];

	msqlTrace(TRACE_IN,"extractValues()");
	curField = fields;
	offset = flist;
	while(curField)
	{
		if ( * (row + *offset)) 
		{
			freeValue(curField->value);
			curField->value=NULL;
			switch(curField->type)
			{
				case INT_TYPE:
#ifndef _CRAY
					bcopy(row + *offset + 1,&ip,4);
					curField->value =(val_t *)
						fillValue(&ip,INT_TYPE);
#else
					curField->value = (val_t*)fillValue(
						row + *offset + 1, INT_TYPE);
#endif
					break;

				case CHAR_TYPE:
					cp = (char *)row + *offset + 1;
					curField->value = (val_t *)
						fillValue(cp, CHAR_TYPE,
						curField->length);
					break;

				case REAL_TYPE:
					bcopy(row + *offset + 1,buf,8);
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
				if (*(cp1+1))
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
	hold = *(str + maxLen);
	*(str + maxLen) = 0;
	reg = regcomp(regbuf);
	res = regexec(reg,str);
	*(str + maxLen) = hold;
	safeFree(reg);
	if (regErrFlag)
	{
		strcpy(errMsg, "Evaluation of LIKE clause failed");
		msqlDebug(MOD_ERR, "Evaluation of LIKE clause failed\n");
		return(-1);
	}
	return(res);
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
	char	*row;
	cond_t	*conds;
	int	*clist;
{
	REG 	cond_t	*curCond;
	REG 	char	*cp;
	REG 	int	*ip,
			result,
			tmp,
			ival;
	int	*offset,
		init=1;
	double	*fp;
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
					strcpy(errMsg,
					   "Unqualified field in comparison");
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
					    tmpVal.val.charVal= (u_char*)malloc
						(curField->length + 1);
					    bcopy(tmpField.value->val.charVal,
						tmpVal.val.charVal,
						curField->length);
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
				sprintf(errMsg,"Unknown field '%s.%s'",
					value->val.identVal->seg1,
					value->val.identVal->seg2);
				msqlDebug(MOD_ERR,"Unknown field '%s.%s'\n",
					value->val.identVal->seg1,
					value->val.identVal->seg2);
				msqlTrace(TRACE_OUT,"matchRow()");
				(void)free(value->val.charVal);
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
			sprintf(errMsg,"Bad type for comparison of '%s'",
				curCond->name);
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
#ifndef _CRAY
					tmp = intMatch(*(row + *offset),0,
						curCond->op);
#else
					tmp = intMatch(ival, 0, curCond->op);
#endif
					break;
				}
#ifdef _CRAY
				ival = unpackInt32(row + *offset + 1);
#else

				(void)bcopy((row + *offset +1),buf,sizeof(int));
				ip = (int*)buf;
#endif
				if (curCond->op == LIKE_OP)
				{
					strcpy(errMsg,
					    "Can't perform LIKE on int value");
					msqlDebug(MOD_ERR,
					   "Can't perform LIKE on int value\n");
					msqlTrace(TRACE_OUT,"matchRow()");
					return(-1);
				}
#ifndef _CRAY
				tmp = intMatch(*ip,value->val.intVal,
					curCond->op);
#else
				tmp = intMatch(ival, value->val.intVal, 
					curCond->op);
#endif
				break;

			case CHAR_TYPE:
				if (value->nullVal)
				{
					tmp = intMatch(*(row + *offset),0,
						curCond->op);
					break;
				}
				cp = (char *)row + *offset +1;
				tmp = charMatch(cp,value->val.charVal,
					curCond->op, curCond->length);
				if (value == &tmpVal)
				{
					free(tmpVal.val.charVal);
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
					tmp = intMatch(*(row + *offset),0,
						curCond->op);
					break;
				}
				(void)bcopy((row + *offset +1),buf,
					sizeof(double));
				fp = (double *)(buf);
				if (curCond->op == LIKE_OP)
				{
					strcpy(errMsg,
					    "Can't perform LIKE on real value");
					msqlDebug(MOD_ERR,
					  "Can't perform LIKE on real value\n");
					msqlTrace(TRACE_OUT,"matchRow()");
					return(-1);
				}
				tmp = realMatch(*fp,value->val.realVal,
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
				(void)bcopy((r1 + *offset +1),buf,sizeof(int));
				ip1 = (int) * (int*)buf;
				(void)bcopy((r2 + *offset +1),buf,sizeof(int));
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
				res = strcmp(cp1,cp2);
				break;

			case REAL_TYPE:
				(void)bcopy((r1+*offset+1),buf,sizeof(double));
				fp1 = (double) * (double *)(buf);
				(void)bcopy((r2+*offset+1),buf,sizeof(double));
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
		sprintf(errMsg,"Unknown database \"%s\"",DB);
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
		line[80];
	field_t	*curField;
	int	fd,
		rem,
		fieldCount,
		foundKey;

	msqlTrace(TRACE_IN,"msqlCreate()");

	/*
	** Write the catalog entry
	*/
	(void)sprintf(defPath,"%s/msqldb/%s/%s.def",msqlHomeDir,DB,table);
	fd = open(defPath,O_RDONLY,0);
	if (fd >= 0)
	{
		(void)close(fd);
		sprintf(errMsg,"Table \"%s\" exists",table);
		msqlDebug(MOD_ERR,"Table \"%s\" exists\n",table);
		msqlTrace(TRACE_OUT,"msqlCreate()");
		return(-1);
	}
	fd = open(defPath,O_WRONLY | O_CREAT, 0600);
	if (fd < 0)
	{
		sprintf(errMsg,"Can't create table \"%s\"",table);
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
		sprintf(errMsg,"Too many fields in table (%d Max)",MAX_FIELDS);
		msqlDebug(MOD_ERR,"Too many fields in table (%d Max)\n",MAX_FIELDS);
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
			sprintf(errMsg,"Error writing catalog");
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
		(void)sprintf(defPath,"%s/msqldb/%s/%s.key",msqlHomeDir,DB,table);
		fd = open(defPath, O_CREAT|O_RDWR, 0600);
		if (fd < 0)
		{
			sprintf(errMsg,"Creation of key table failed!");
			msqlDebug(MOD_ERR,"Creation of key table failed!\n");
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
	fd = open(datPath,O_CREAT | O_WRONLY, 0600);
	if (fd < 0)
	{
		unlink(datPath);
		unlink(defPath);
		sprintf(errMsg,"Error creating table file for \"%s\"",table);
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
	char	path[255];
	FILE	*fp;
	REG 	cache_t *entry;
	REG 	int	count;

	msqlTrace(TRACE_IN,"msqlDrop()");

	/* 
	** Invalidate the cache entry so that we don't use it again 
	*/

	count = 0;
	while(count < CACHE_SIZE)
	{
		entry = tableCache + count;
		if((strcmp(entry->DB,DB)==0)&&(strcmp(entry->table,table)==0))
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
			close(entry->keyFD);
			break;
		}
		count++;
	}

	/*
	** Now blow away the table data ,stack file, and key files
	*/
	(void)sprintf(path,"%s/msqldb/%s/%s.def",msqlHomeDir,DB,table);
	fp = fopen(path,"r");
	if (!fp)
	{
		sprintf(errMsg,"Unknown table \"%s\"",table);
		msqlDebug(MOD_ERR,"Unknown table \"%s\"\n",table);
		msqlTrace(TRACE_OUT,"msqlDrop()");
		return(-1);
	}
	(void)fclose(fp);
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
		rowLen,
		keyLen,
		useKey,
		hasKey,
		res;
	u_int	rowNum;
	char	*row,
		active;
	field_t	*curField;
	pkey_t	*key;
	cache_t	*cacheEntry;


	msqlTrace(TRACE_IN,"msqlDelete()");
	if((cacheEntry = loadTableDef(table,DB)) == NULL)
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
					if(deleteKey(cacheEntry,rowNum) < 0)
					{
					       msqlTrace(TRACE_OUT,"msqlDelete()");
						return(-1);
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
				res = deleteKey(cacheEntry,rowNum);
				if(res < 0)
				{
					msqlTrace(TRACE_OUT,"msqlDelete()");
					return(res);
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
	char	*row;
	REG 	field_t	*curField,
		*curField2;
	pkey_t	*key;
	cache_t	*cacheEntry;


	msqlTrace(TRACE_IN,"msqlInsert()");
	if((cacheEntry = loadTableDef(table,DB)) == NULL)
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
			sprintf(errMsg,
				"No value specified for field '%s'",
				curField->name);
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
				sprintf(errMsg,"Field '%s' not unique",
					curField->name);
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
			sprintf(errMsg,"Non unique key value in field '%s'",
				key->name);
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
	if(writeRow(cacheEntry,NULL,rowNum) < 0)
	{
		sprintf(errMsg,"Error on data write");
		msqlDebug(MOD_ERR,"Error on data write\n");
		msqlTrace(TRACE_OUT,"msqlInsert()");
		return(-1);
	}
	if (key)
	{
		writeKey(cacheEntry,key,rowNum);
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
	u_int	rowNum;
	char	*row,
		active;
	field_t	*curField;
	pkey_t	*keyCond,
		*keyField;
	cache_t	*cacheEntry;
	


	msqlTrace(TRACE_IN,"msqlUpdate()");
	if((cacheEntry = loadTableDef(table,DB)) == NULL)
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
					updateValues(row,fields,flist);
					if (checkNullFields(cacheEntry,row) < 0)
					{
					       msqlTrace(TRACE_OUT,"msqlUpdate()");
						return(-1);
					}
					if(writeRow(cacheEntry,row,rowNum) < 0)
					{
						sprintf(errMsg,
							"Error on data write");
						msqlDebug(MOD_ERR,
						       "Error on data write\n");
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
							freeValue(
							    keyField->value);
							keyField->value =
								curField->value;
							writeKey(cacheEntry,
								keyField,
								rowNum);

							break;
						}
						curField=curField->next;
					    }
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
				updateValues(row,fields,flist);
				if (checkNullFields(cacheEntry,row) < 0)
				{
					msqlTrace(TRACE_OUT,"msqlUpdate()");
					return(-1);
				}
				if(writeRow(cacheEntry,row,rowNum) < 0)
				{
					sprintf(errMsg,"Error on data write");
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
							freeValue(
							    keyField->value);
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
		bufLen[4];
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
					sprintf(outBuf,"%f",
					    curField->value->val.realVal);
					outData = (u_char *)outBuf;
					break;
			}
		}
		else
		{
			*outBuf = '\0';
			outData = (u_char *)outBuf;
		}
		sprintf(bufLen,"%d:",strlen(outData));
		strcat(packet,bufLen);
		strcat(packet,outData);
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
	
	res = 0;
	curCond = conds;
	while(curCond)
	{
		if (curCond->value->type != IDENT)
		{
			res = 1;
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
		addPartial,
		oRowNum,
		iRowNum,
		clist[MAX_FIELDS],
		outerClist[MAX_FIELDS],
		res;
	cond_t	*newCondHead,
		*newCondTail,
		*t1CondHead,
		*t1CondTail,
		*t2CondHead,
		*t2CondTail,
		*outerConds,
		*newCond,
		*curCond,
		*keyCond;
	field_t	*curField,
		*tmpField,
		*keyField;
	char	*oRow,
		*iRow,
		*row,
		active;
	pkey_t	key,
		outerKey;


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
	    while(curField)
	    {
		if(strcmp(curField->table,curCond->table) == 0)
		{
		    if (curCond->value->type == IDENT_TYPE)
		    {
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
		newCond = (cond_t *)malloc(sizeof(cond_t));
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
			newCond = (cond_t *)malloc(sizeof(cond_t));
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
			newCond = (cond_t *)malloc(sizeof(cond_t));
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
	** this earlier as we mus know which is the inner and outer table
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
	** THIS CODE IS NOT IN USE YET
	if (table2->keyFD > 0)
		curField = table2->def;
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
					break;
				}

				if (curCond->type == IDENT_TYPE)
				{
					if(!strcmp(curField->table,
						curCond->val.identVal->seg1)
				   	  &&!strcmp(curField->name,
						curCond->val.identVal->seg2))
					{
					}
				}
				curCond = curCond->next;
			}
		}
		curField = curField->next;
	}
	**
	**
	*/


	/*
	** If there's a key we can use work out how we can use it
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
		iRowNum = 0;
		while((iRow = readRow(inner,&active,iRowNum++) ))
		{
			if (!active)
				continue;
			*row = 1;
			mergeRows(row+1,outer,oRow,inner,iRow);
			res = matchRow(tmpTable,row+1,newCondHead,clist);
			if (res < 0)
			{
        			msqlTrace(TRACE_OUT,"joinTables()");
				return(NULL);
			}
			if (res == 1)
			{
				if(writeRow(tmpTable,NULL,NO_POS) < 0)
				{
        				msqlTrace(TRACE_OUT,"joinTables()");
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
                (void)free(newCond);
	}
	curCond = t1CondHead;
	while(curCond)
	{
                newCond = curCond;
                curCond = curCond->next;
                (void)free(newCond);
	}
	curCond = t2CondHead;
	while(curCond)
	{
                newCond = curCond;
                curCond = curCond->next;
                (void)free(newCond);
	}
	msqlTrace(TRACE_OUT,"joinTables()");
	return(tmpTable);
}



char *dupRow(entry,row)
	cache_t	*entry;
	char 	*row;
{
	char	*new;

	new = (char *)malloc(entry->rowLen);
	(void)bcopy(row,new,entry->rowLen);
	return(new);
}



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
        	msqlTrace(TRACE_OUT,"createSortedTable()");
		return(NULL);
	}
	if (setupOrder(entry,olist,order) < 0)
	{
        	msqlTrace(TRACE_OUT,"createSortedTable()");
		return(NULL);
	}
	if (setupFields(entry,flist,entry->def,NULL) < 0)
	{
        	msqlTrace(TRACE_OUT,"createSortedTable()");
		return(NULL);
	}


	numRows = 1;
	last = NULL;
	while(numRows > 0)
	{
		rowNum = 0;
		numRows = 0;
		cur = NULL;
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
#ifdef HAVE_MMAP
				cur = row;
#else
				if (cur)
					(void)free(cur);
				cur = dupRow(entry,row);
#endif
				curRowNum = rowNum;
				rowNum++;
				continue;
			}
			if (compareRows(cur,row,order,olist) >= 0)
			{
				if (compareRows(last,row,order,olist) <= 0 ||
				    !last)
				{
#ifdef HAVE_MMAP
					cur = row;
#else
					if (cur)
						(void)free(cur);
					cur = dupRow(entry,row);
#endif
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
#ifdef HAVE_MMAP
			last = cur;
#else
			if (last)
			{
				(void)free(last);
			}
			last = dupRow(entry,cur);
#endif
		}
	}
       	msqlTrace(TRACE_OUT,"createSortedTable()");
	return(new);
}



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
}




static int doSelect(cacheEntry, tables, fields,conds,dest,tmpTable)
	cache_t	*cacheEntry;
	tlist_t	*tables;
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
		res;
	char	*row,
		active,
		useKey,
		outBuf[100];
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
		writePkt(outSock);
	}


	/*
	** If the data is keyed and it's a simple condition just grab the row
	*/

	useKey = 0;
	if (key)
	{
		if (key->op == EQ_OP)
		{
			if (conds->next == NULL)
			{
				useKey = 1;
			}
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
						writePkt(outSock);
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
				extractValues(row,fields,flist);
				if (dest == DEST_CLIENT)
				{
					bzero(packet,PKT_LEN);
					formatPacket(packet,fields);
					writePkt(outSock);
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
		writePkt(outSock);

		/*
		** Send the field info down the line to the client
		*/
		curField = fields;
		while(curField)
		{
			sprintf(outBuf,"%d",curField->length);
			sprintf(packet,"%d:%s%d:%s1:%d%d:%s1:%s1:%s", 
				strlen(tables->name), tables->name,
				strlen(curField->name), curField->name, 
				curField->type,strlen(outBuf), outBuf, 
				curField->flags & NOT_NULL_FLAG ? "Y":"N", 
				curField->flags & PRI_KEY_FLAG ? "Y":"N");
			writePkt(outSock);
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
	tlist_t	*tables;
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
	REG	tlist_t	*curTable;
	REG	field_t	*curField;
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
		curField = fields;
		while(curField)
		{
			if (strcmp(curField->table,tables->name)!=0)
			{
				sprintf(errMsg,
					"Reference to un-selected table \"%s\"",
					curField->table);
				return(-1);
			}
			curField = curField->next;
		}
		join = 0;
	}

	curField = fields;
	while (curField)
	{
		if (*(curField->table) == 0)
		{
			if (join)
			{
				sprintf(errMsg,
					"Unqualified field \"%s\" in join",
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
			sprintf(errMsg,"Reference to un-selected table \"%s\"",
				curField->table);
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
				table1 = loadTableDef(curTable->name,DB);
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
				table2 = loadTableDef(curTable->name,DB);
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
				table2 = loadTableDef(curTable->name,DB);
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
		if((cacheEntry = loadTableDef(tables->name,DB)) == NULL)
		{
			msqlTrace(TRACE_OUT,"msqlSelect()");
			return(-1);
		}
	}
	else
	{
		cacheEntry = tmpTable;
		conds = NULL;
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
		if (createDistinctTable(cacheEntry,NULL,fields) < 0)
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
	}


	/*
	** Send the result to the client if we haven't yet.
	*/
	if (doSelect(cacheEntry,tables,fields,NULL,DEST_CLIENT,NULL)<0)
	{
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
			close(entry->keyFD);
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
	sprintf(packet,"-100:\n");
	writePkt(sock);
}

