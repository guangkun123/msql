/*
**	msql.h	- 
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


#if defined(__STDC__) || defined(__cplusplus)
#  define __ANSI_PROTO(x)	x
#else
#  define __ANSI_PROTO(x)	()
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef	char	** m_row;

typedef struct field_s {
	char	*name,
		*table;
	int	type,
		length,
		flags;
} m_field;



typedef	struct	m_data_s {
	int	width;
	m_row	data;
	struct	m_data_s *next;
} m_data;

typedef struct m_fdata_s {
	m_field	field;
	struct m_fdata_s *next;
} m_fdata;



typedef struct result_s {
        m_data 	*queryData,
                *cursor;
	m_fdata	*fieldData,
		*fieldCursor;
	int	numRows,
		numFields;
} m_result;


#define	msqlNumRows(res) res->numRows
#define	msqlNumFields(res) res->numFields


#define INT_TYPE	1
#define CHAR_TYPE	2
#define REAL_TYPE	3
#define IDENT_TYPE	4
#define NULL_TYPE	5

#define NOT_NULL_FLAG   1
#define PRI_KEY_FLAG    2

#define IS_PRI_KEY(n)	(n & PRI_KEY_FLAG)
#define IS_NOT_NULL(n)	(n & NOT_NULL_FLAG)


/*
** Pre-declarations for the API library functions
*/
#ifndef _MSQL_SERVER_SOURCE
	extern  char msqlErrMsg[];
	int 	msqlConnect __ANSI_PROTO((char *));
	int 	msqlSelectDB __ANSI_PROTO((int, char*));
	int 	msqlQuery __ANSI_PROTO((int, char*));
	int 	msqlCreateDB __ANSI_PROTO((int, char*));
	int 	msqlDropDB __ANSI_PROTO((int, char*));
	int 	msqlShutdown __ANSI_PROTO((int));
	int 	msqlReloadAcls __ANSI_PROTO((int));
	int 	msqlGetProtoInfo();
	char 	*msqlGetServerInfo();
	char 	*msqlGetHostInfo();
	void	msqlClose __ANSI_PROTO((int));
	void 	msqlDataSeek __ANSI_PROTO((m_result*, int));
	void 	msqlFieldSeek __ANSI_PROTO((m_result*, int));
	void 	msqlFreeResult __ANSI_PROTO((m_result*));
        m_row   msqlFetchRow __ANSI_PROTO((m_result*));
	m_field	*msqlFetchField __ANSI_PROTO((m_result *));
	m_result *msqlListDBs __ANSI_PROTO((int));
	m_result *msqlListTables __ANSI_PROTO((int));
	m_result *msqlListFields __ANSI_PROTO((int, char*));
	m_result *msqlStoreResult();
#endif

#ifdef __cplusplus
	}
#endif
