/*
**      errmsg.h  - Error messages
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

/*
** German translations by Nils Faerber <gl055@appl2.hrz.uni-siegen.de>
*/
 


/*************************************************************************
**
**	ENGLISH MESSAGES
**
*/

#ifdef ENGLISH

   /* CLIENT LIBRARY MESSAGES */

#  define SOCKET_ERROR        	"Can't create UNIX socket"
#  define CONNECTION_ERROR    	"Can't connect to local MSQL server"
#  define IPSOCK_ERROR        	"Can't create IP socket"
#  define UNKNOWN_HOST        	"Unknown MSQL Server Host (%s)"
#  define CONN_HOST_ERROR     	"Can't connect to MSQL server on %s"
#  define SERVER_GONE_ERROR   	"MSQL server has gone away"
#  define UNKNOWN_ERROR       	"Unknown MSQL error"
#  define PACKET_ERROR         	"Bad packet received from server"
#  define USERNAME_ERROR    	"Can't find your username. Who are you?"
#  define VERSION_ERROR		\
		"Protocol mismatch. Server Version = %d Client Version = %d"

   /* SERVER MESSAGE */

#  define CON_COUNT_ERROR	"Too many connections"
#  define BAD_HOST_ERROR	"Can't get hostname for your address"
#  define HANDSHAKE_ERROR	"Bad handshake"
#  define ACCESS_DENIED_ERROR	"Access to database denied"
#  define NO_DB_ERROR		"No Database Selected"
#  define NO_TABLE_ERROR	"No Table Selected"
#  define PERM_DENIED_ERROR	"Permission denied"
#  define UNKNOWN_COM_ERROR	"Unknown command"
#  define BAD_DIR_ERROR		"Can't open directory \"%s\""
#  define BAD_TABLE_ERROR	"Unknown table \"%s\""
#  define TABLE_READ_ERROR	"Error reading table \"%s\" definition"
#  define TMP_MEM_ERROR		"Out of memory for temporary table"
#  define TMP_CREATE_ERROR	"Couldn't create temporary table"
#  define DATA_OPEN_ERROR	"Couldn't open data file for %s"
#  define KEY_OPEN_ERROR	"Couldn't open key file for %s"
#  define STACK_OPEN_ERROR	"Couldn't open stack file for %s"
#  define WRITE_ERROR		"Data write failed"
#  define KEY_WRITE_ERROR	"Write of key failed"
#  define SEEK_ERROR		"Seek into data table failed!"
#  define KEY_SEEK_ERROR	"Seek into key table failed!"
#  define BAD_NULL_ERROR	"Field \"%s\" cannot be null"
#  define FIELD_COUNT_ERROR	"Too many fileds in query"
#  define TYPE_ERROR		"Literal value for \'%s\' is wrong type"
#  define BAD_FIELD_ERROR	"Unknown field \"%s.%s\""
#  define BAD_FIELD_2_ERROR	"Unknown field \"%s\""
#  define COND_COUNT_ERROR	"Too many fields in condition"
#  define ORDER_COUNT_ERROR	"Too many fields in order specification"
#  define BAD_LIKE_ERROR	"Evaluation of LIKE clause failed"
#  define UNQUAL_ERROR		"Unqualified field in comparison"
#  define BAD_TYPE_ERROR	"Bad type for comparison of '%s'"
#  define INT_LIKE_ERROR	"Can't perform LIKE on int value"
#  define REAL_LIKE_ERROR	"Can't perform LIKE on real value"
#  define BAD_DB_ERROR		"Unknown database \"%s\""
#  define TABLE_EXISTS_ERROR	"Table \"%s\" exists"
#  define TABLE_FAIL_ERROR	"Can't create table \"%s\""
#  define TABLE_WIDTH_ERROR	"Too many fields in table (%d Max)"
#  define CATALOG_WRITE_ERROR	"Error writing catalog"
#  define KEY_CREATE_ERROR	"Creation of key table failed"
#  define DATA_FILE_ERROR	"Error creating table file for \"%s\""
#  define BAD_TABLE_ERROR	"Unknown table \"%s\""
#  define NO_VALUE_ERROR	"No value specified for field '%s'"
#  define NON_UNIQ_ERROR	"Field '%s' not unique"
#  define KEY_UNIQ_ERROR	"Non unique key value in field '%s'"
#  define UNSELECT_ERROR	"Reference to un-selected table \"%s\""
#  define UNQUAL_JOIN_ERROR	"Unqualified field \"%s\" in join"
#  define SIZE_ERROR		"Value for field \'%s\' is too large"

#endif


/*************************************************************************
**
**	GERMAN MESSAGES
**
*/


#ifdef GERMAN

   /* CLIENT LIBRARY MESSAGES */

#  define SOCKET_ERROR 		"Kann UNIX-Socket nicht anlegen"
#  define CONNECTION_ERROR	"Keine Verbindung zu lokalem MSQL Server"
#  define IPSOCK_ERROR   	"Kann IP-Socket nicht anlegen"
#  define UNKNOWN_HOST   	"Unbekannter MSQL Server Host (%s)"
#  define CONN_HOST_ERROR 	"Keine Verbindung zu MSQL Server auf %s"
#  define SERVER_GONE_ERROR	"MSQL Server nicht vorhanden"
#  define UNKNOWN_ERROR    	"Unbekannter MSQL Fehler"
#  define PACKET_ERROR     	"Fehlerhaftes Paket von Server empfangen "
#  define USERNAME_ERROR   	"Kann Usernamen nicht herausfinden."
#  define VERSION_ERROR    	\
		"Protokolle ungleich. Server Version = % d Client Version = %d"


   /* SERVER MESSAGE */

#  define CON_COUNT_ERROR	"Too many connections"
#  define BAD_HOST_ERROR	"Can't get hostname for your address"
#  define HANDSHAKE_ERROR	"Bad handshake"
#  define ACCESS_DENIED_ERROR	"Access to database denied"
#  define NO_DB_ERROR		"No Database Selected"
#  define NO_TABLE_ERROR	"No Table Selected"
#  define PERM_DENIED_ERROR	"Permission denied"
#  define UNKNOWN_COM_ERROR	"Unknown command"
#  define BAD_DIR_ERROR		"Can't open directory \"%s\""
#  define BAD_TABLE_ERROR	"Unknown table \"%s\""
#  define TABLE_READ_ERROR	"Error reading table \"%s\" definition"
#  define TMP_MEM_ERROR		"Out of memory for temporary table"
#  define TMP_CREATE_ERROR	"Couldn't create temporary table"
#  define DATA_OPEN_ERROR	"Couldn't open data file for %s"
#  define KEY_OPEN_ERROR	"Couldn't open key file for %s"
#  define STACK_OPEN_ERROR	"Couldn't open stack file for %s"
#  define WRITE_ERROR		"Data write failed"
#  define KEY_WRITE_ERROR	"Write of key failed"
#  define SEEK_ERROR		"Seek into data table failed!"
#  define KEY_SEEK_ERROR	"Seek into key table failed!"
#  define BAD_NULL_ERROR	"Field \"%s\" cannot be null"
#  define FIELD_COUNT_ERROR	"Too many fileds in query"
#  define TYPE_ERROR		"Literal value for \'%s\' is wrong type"
#  define BAD_FIELD_ERROR	"Unknown field \"%s.%s\""
#  define BAD_FIELD_2_ERROR	"Unknown field \"%s\""
#  define COND_COUNT_ERROR	"Too many fields in condition"
#  define ORDER_COUNT_ERROR	"Too many fields in order specification"
#  define BAD_LIKE_ERROR	"Evaluation of LIKE clause failed"
#  define UNQUAL_ERROR		"Unqualified field in comparison"
#  define BAD_TYPE_ERROR	"Bad type for comparison of '%s'"
#  define INT_LIKE_ERROR	"Can't perform LIKE on int value"
#  define REAL_LIKE_ERROR	"Can't perform LIKE on real value"
#  define BAD_DB_ERROR		"Unknown database \"%s\""
#  define TABLE_EXISTS_ERROR	"Table \"%s\" exists"
#  define TABLE_FAIL_ERROR	"Can't create table \"%s\""
#  define TABLE_WIDTH_ERROR	"Too many fields in table (%d Max)"
#  define CATALOG_WRITE_ERROR	"Error writing catalog"
#  define KEY_CREATE_ERROR	"Creation of key table failed"
#  define DATA_FILE_ERROR	"Error creating table file for \"%s\""
#  define BAD_TABLE_ERROR	"Unknown table \"%s\""
#  define NO_VALUE_ERROR	"No value specified for field '%s'"
#  define NON_UNIQ_ERROR	"Field '%s' not unique"
#  define KEY_UNIQ_ERROR	"Non unique key value in field '%s'"
#  define UNSELECT_ERROR	"Reference to un-selected table \"%s\""
#  define UNQUAL_JOIN_ERROR	"Unqualified field \"%s\" in join"
#  define SIZE_ERROR		"Value for field \'%s\' is too large"

#endif

