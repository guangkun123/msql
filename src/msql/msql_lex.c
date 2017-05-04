/*
**	msql_lex.c	- 
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

/*
** This is a hand crafted scanner that looks and smells like a lex
** generated scanner.  I've kept the same interface so that unmodified
** yacc parsers can run with this.
**
** This scanner uses a state machine to translate the input data into
** tokens.  Failed matches cause a fallback to the start of the token
** scan and a possible transition to a known alternate state.  The
** state structure is defined in doc/scanner.doc
**
** NOTE : Because the scanner must revert back to the start of the
**	token on a failure it can only work from an input buffer. It
**	cannot work from a file or anything else.
*/


#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include <common/portability.h>

#include "msql_priv.h"
#include "y.tab.h"

#define	REG		register
#define NUM_HASH	16

#ifndef DEBUG
#  define malloc(s) 	Malloc(s,__FILE__,__LINE__)
#  define free(a) 	Free(a,__FILE__,__LINE__)
#endif


/*
** Macros for handling the scanner's internal pointers
*/
#define yyGet()		(*tokPtr++); yytoklen++
#define yyUnget()	tokPtr--; yytoklen--
#define yySkip()	(*tokPtr++); tokStart++
#define yyRevert()	{tokPtr=tokStart; yytoklen=0;}
#define yyReturn(t)	{tokStart=tokPtr; return(t);}


/*
** Macros for matching character classes.  These are in addition to
** those provided in <ctypes.h>
*/
#ifdef	iswhite
# undef iswhite
#endif
#define iswhite(c)	(c == ' ' || c == '\t' || c == '\n')

#ifdef	iscompop
# undef iscompop
#endif
#define iscompop(c)	(c == '<' || c == '>' || c == '=')


/*
** Debugging macros.
*/

/* #define DEBUG_STATE	/* Define this to watch the state transitions */

#ifdef DEBUG
#  define token(x)	(int) "x"
#else
#  define token(x)	x
#endif /* DEBUG */

#ifdef DEBUG_STATE
#  define CASE(x)	case x: if (x) printf("%c -> state %d\n",c,x); \
				else printf("Scanner starting at state 0\n");
#else
#  define CASE(x)	case x:
#endif

u_char	*yytext 	= NULL;
u_int	yytoklen	= 0;
int	yylineno 	= 1;
static	u_char 		*tokPtr,
			*tokStart;
static	int		state;


#ifdef DEBUG
	YYSTYPE		yylval;
#else
	extern	YYSTYPE		yylval;
#endif



typedef struct symtab_s {
	char	*name;
	int	tok;
} symtab_t;


static symtab_t symtab[16][16] = {
	{ /* 0 */
		{ "select",	token(SELECT)},
		{ "values",	token(VALUES)},
		{ 0,		0}
	},
	{ /* 1 */
		{ "or",		token(OR)},
		{ "not",	token(NOT)},
		{ 0,		0}
	},
	{ /* 2 */
		{ "distinct",	token(DISTINCT)},
		{ 0,		0}
	},
	{ /* 3 */
		{ "and",	token(AND)},
		{ "delete",	token(DELETE)},
		{ "update",	token(UPDATE)},
		{ 0,		0}
	},
	{ /* 4 */
		{ "from",	token(FROM)},
		{ "create",	token(CREATE)},
		{ "primary",	token(PRIMARY)},
		{ "smallint",	token(INT)},
		{ "real",	token(REAL)},
		{ 0,		0}
	},
	{ /* 5 */
		{ "drop",	token(DROP)},
		{ "insert",	token(INSERT)},
		{ "like",	token(LIKE)},
		{ 0,		0}
	},
	{ /* 6 */
		{ 0,		0}
	},
	{ /* 7 */
		{ "asc",	token(ASC)},
		{ 0,		0}
	},
	{ /* 8 */
		{ "table",	token(TABLE)},
		{ 0,		0}
	},
	{ /* 9 */
		{ "<=",		token(LE)},
		{ "all",	token(ALL)},
		{ "key",	token(KEY)},
		{ 0,		0}
	},
	{ /* 10 */
		{ "<>",         token(NE)},
		{ "into",	token(INTO)},
		{ 0,		0}
	},
	{ /* 11 */
		{ "where",	token(WHERE)},
		{ ">=",		token(GE)},
		{ "by",		token(BY)},
		{ "null",	token(NULLSYM)},
		{ "int",	token(INT)},
		{ 0,		0}
	},
	{ /* 12 */
		{ "<",		token(LT)},
		{ "order",	token(ORDER)},
		{ "set",	token(SET)},
		{ 0,		0}
	},
	{ /* 13 */
		{ "=",          token(EQ)},
		{ 0,		0}
	},
	{ /* 14 */
		{ ">",          token(GT)},
		{ "integer",	token(INT)},
		{ "char",	token(CHAR)},
		{ 0,		0}
	},
	{ /* 15 */
		{ "desc",	token(DESC)},
		{ 0,		0}
	}
};





msqlInitScanner(buf)
	u_char	*buf;
{
	tokStart = buf;
	state = 0;
	yylineno = 1;
}


int findKeyword(tok,len)
	char	*tok;
	int	len;
{
	REG	char	*cp1,
			*cp2,
			tmp;
	REG	symtab_t *stab;
	int	found;
        REG 	int     hash=0,
			index=0;


        cp1 = tok;
        while(*cp1 && index++ < len)
        {
                hash += *cp1++;
        }
        hash = hash & (NUM_HASH - 1);

	stab = symtab[hash];
	while(stab->name)
	{
		cp1 = stab->name;
		cp2 = tok;
		found = 1;
		while(cp2 - tok < len)
		{
			if (!(*cp1))
			{
				found = 0;
				break;
			}
/*
			if (tolower(*cp2++) != *cp1++)
*/
			tmp = *cp2++;
			if (tmp >64 && tmp<91)
				tmp+=32;
			if (tmp != *cp1++)
			{
				found = 0;
				break;
			}
		}
		if (*cp1)
		{
			found = 0;
		}
		if (found)
		{
			yytext = (u_char *)stab->name;
			yylval = (YYSTYPE)stab->tok;
			return(stab->tok);
		}
		stab++;
	}
	return(0);
}

u_char *tokenDup(tok,len)
	u_char	*tok;
	int	len;
{
	u_char	*new;

	new = (u_char *)malloc(len+1);
	(void)bcopy(tok,new,len);
	*(new + len) = 0;
	return(new);
}


u_char *readTextLiteral(tok)
	u_char	*tok;
{
	REG 	u_char c;
	int	bail;

	bail = 0;
	while(!bail)
	{
		c = yyGet();
		switch(c)
		{
			case 0:
				return(NULL);

			case '\\':
				c = yyGet();
				if (!c)
					return(NULL);
				break;
	
			case '\'':
				bail=1;
				break;
		}
	}
	return(tokenDup(tok,yytoklen));
}


int yylex()
{
	REG	u_char	c;
	int	tokval;
	static	u_char dummyBuf[2];


	/*
	** Handle the end of input.  We return an EOI token when we hit
	** the end and then return a 0 on the next call to yylex.  This
	** allows the parser to do the right thing with trailing garbage
	** in the expression.
	*/
	if (state == 1000)
	{
		return(0);
	}
	state = 0;

	/*
	** Dive into the state machine
	*/
	while(1)
	{
		switch(state)
		{
			/* State 0 : Start of token */
			CASE(0)
				tokPtr = tokStart;
				yytext = NULL;
				yytoklen = 0;
				c = yyGet();
				while (iswhite(c))
				{
					if (c == '\n')
						yylineno++;
					c = yySkip();
				}
				if (c == '\'')
				{
					state = 12;
					break;
				}
				if (isalpha(c))
				{
					state = 1;
					break;
				}
				if (isdigit(c))
				{
					state = 5;
					break;
				}
				if (c == '-' || c == '+')
				{
					state = 9;
					break;
				}
				if (iscompop(c))
				{
					state = 10;
					break;
				}
				if (c == '#')
				{
					state = 14;
					break;
				}
				if (c == 0)
				{
					state = 1000;
					break;
				}
				state = 999;
				break;

			/* State 1 : Incomplete keyword or ident */
			CASE(1)
				c = yyGet();
				if (isalpha(c))
				{
					state = 1;
					break;
				}
				if (isdigit(c) || c == '_')
				{
					state = 3;
					break;
				}
				state = 2;
				break;


			/* State 2 : Complete keyword or ident */
			CASE(2)
				yyUnget();
				tokval = findKeyword(tokStart,yytoklen);
				if (tokval)
				{
					yyReturn(tokval);
				}
				else
				{
					yytext = tokenDup(tokStart,yytoklen);
					yylval = (YYSTYPE) yytext;
					yyReturn(token(IDENT));
				}
				break;


			/* State 3 : Incomplete ident */
			CASE(3)
				c = yyGet();
				if (isalnum(c) || c == '_')
				{
					state = 3;
					break;
				}
				state = 4;
				break;


			/* State 4: Complete ident */
			CASE(4)
				yyUnget();
				yytext = tokenDup(tokStart,yytoklen);
				yylval = (YYSTYPE) yytext;
				yyReturn(token(IDENT));


			/* State 5: Incomplete real or int number */
			CASE(5)
				c = yyGet();
				if (isdigit(c))
				{
					state = 5;
					break;
				}
				if (c == '.')
				{
					state = 7;
					break;
				}
				state = 6;
				break;


			/* State 6: Complete integer number */
			CASE(6)
				yyUnget();
				yytext = tokenDup(tokStart,yytoklen);
				yylval = (YYSTYPE) yytext;
				yyReturn(token(NUM));
				break;


			/* State 7: Incomplete real number */
			CASE(7)
				c = yyGet();
				if (isdigit(c))
				{
					state = 7;
					break;
				}
				state = 8;
				break;


			/* State 8: Complete real number */
			CASE(8)
				yyUnget();
				yytext = tokenDup(tokStart,yytoklen);
				yylval = (YYSTYPE) yytext;
				yyReturn(token(REAL_NUM));


			/* State 9: Incomplete signed number */
			CASE(9)
				c = yyGet();
				if (isdigit(c))
				{
					state = 5;
					break;
				}
				state = 999;
				break;


			/* State 10: Incomplete comparison operator */
			CASE(10)
				c = yyGet();
				if (iscompop(c))
				{
					state = 10;
					break;
				}
				state = 11;
				break;


			/* State 11: Complete comparison operator */
			CASE(11)
				yyUnget();
				tokval = findKeyword(tokStart,yytoklen);
				if (tokval)
				{
					yyReturn(tokval);
				}
				state = 999;
				break;

	
			/* State 12: Incomplete text string */
			CASE(12)
				yytext = readTextLiteral(tokStart);
				yylval = (YYSTYPE) yytext;
				if (yytext)
				{
					state = 13;
					break;
				}
				state = 999;
				break;



			/* State 13: Complete text string */
			CASE(13)
				yyReturn(token(TEXT));
				break;


			/* State 14: Comment */
			CASE(14)
				c = yySkip();
				if (c == '\n')
				{
					state = 0;
				}
				else
				{
					state = 14;
				}
				break;
				

			/* State 999 : Unknown token.  Revert to single char */
			CASE(999)
				yyRevert();
				c = yyGet();
				*dummyBuf = c;
				*(dummyBuf+1) = 0;
				yytext = dummyBuf;
				yylval = (YYSTYPE) yytext;
				yyReturn(token(yytext[0]));


			/* State 1000 : End Of Input */
			CASE(1000)
				yyReturn(token(END_OF_INPUT));

		}
	}
}


#ifdef DEBUG

main()
{
	char	*p,
		tmpBuf[4 * 1024];

	(void)bzero(tmpBuf,sizeof(tmpBuf));
	read(fileno(stdin),tmpBuf,sizeof(tmpBuf));
	msqlInitScanner(tmpBuf);
	while(p = (char *) yylex())
	{
		printf("%-15.15s of length %u is \"%s\"\n", p, yytoklen,
			yytext?yytext:(u_char *)"(null)");
	}
}

#endif
