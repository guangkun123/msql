
/*  A Bison parser, made from msql_yacc.y
    by GNU Bison version 1.28  */

#define YYBISON 1  /* Identify Bison output.  */

#define	END_OF_INPUT	257
#define	GE	258
#define	LE	259
#define	NE	260
#define	EQ	261
#define	GT	262
#define	LT	263
#define	CREATE	264
#define	DROP	265
#define	INSERT	266
#define	DELETE	267
#define	SELECT	268
#define	UPDATE	269
#define	ALL	270
#define	DISTINCT	271
#define	WHERE	272
#define	ORDER	273
#define	FROM	274
#define	INTO	275
#define	TABLE	276
#define	BY	277
#define	ASC	278
#define	DESC	279
#define	LIKE	280
#define	AND	281
#define	OR	282
#define	VALUES	283
#define	SET	284
#define	NOT	285
#define	NULLSYM	286
#define	PRIMARY	287
#define	KEY	288
#define	IDENT	289
#define	TEXT	290
#define	NUM	291
#define	REAL_NUM	292
#define	INT	293
#define	BOOL	294
#define	CHAR	295
#define	REAL	296

#line 21 "msql_yacc.y"

#include <stdio.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>

#include "msql_priv.h"
#include "msql.h"

int	yylineno;
extern	int selectWildcard,
	selectDistinct,
	yytoklen;
ident_t	*msqlCreateIdent();

#define myFree(x) Free(x,__FILE__,__LINE__)

#ifndef YYSTYPE
#define YYSTYPE int
#endif
#include <stdio.h>

#ifndef __cplusplus
#ifndef __STDC__
#define const
#endif
#endif



#define	YYFINAL		126
#define	YYFLAG		-32768
#define	YYNTBASE	48

#define YYTRANSLATE(x) ((unsigned)(x) <= 296 ? yytranslate[x] : 79)

static const char yytranslate[] = {     0,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,    43,
    44,    46,     2,    45,     2,    47,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     1,     3,     4,     5,     6,
     7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
    17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
    27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
    37,    38,    39,    40,    41,    42
};

#if YYDEBUG != 0
static const short yyprhs[] = {     0,
     0,     1,     4,     6,     8,    10,    12,    14,    16,    17,
    25,    27,    31,    36,    38,    40,    45,    46,    49,    50,
    53,    61,    62,    64,    66,    70,    72,    74,    76,    78,
    82,    83,    86,    92,    96,    98,   100,   102,   104,   106,
   108,   110,   112,   114,   117,   118,   122,   127,   130,   132,
   134,   135,   139,   150,   154,   156,   160,   162,   168,   174,
   178,   183,   185,   187,   189,   191,   193,   195,   197
};

static const short yyrhs[] = {    -1,
    49,     3,     0,    50,     0,    57,     0,    69,     0,    70,
     0,    73,     0,    75,     0,     0,    10,    22,    35,    43,
    51,    52,    44,     0,    53,     0,    52,    45,    53,     0,
    78,    54,    55,    56,     0,    39,     0,    42,     0,    41,
    43,    37,    44,     0,     0,    31,    32,     0,     0,    33,
    34,     0,    14,    58,    59,    20,    61,    62,    66,     0,
     0,    16,     0,    17,     0,    59,    45,    60,     0,    60,
     0,    46,     0,    78,     0,    35,     0,    61,    45,    35,
     0,     0,    18,    63,     0,    63,    64,    78,    65,    77,
     0,    78,    65,    77,     0,    27,     0,    28,     0,     7,
     0,     6,     0,     9,     0,     5,     0,     8,     0,     4,
     0,    26,     0,    31,    26,     0,     0,    19,    23,    67,
     0,    67,    45,    78,    68,     0,    78,    68,     0,    24,
     0,    25,     0,     0,    11,    22,    35,     0,    12,    21,
    35,    43,    71,    44,    29,    43,    72,    44,     0,    71,
    45,    78,     0,    78,     0,    72,    45,    76,     0,    76,
     0,    15,    35,    30,    74,    62,     0,    74,    45,    78,
     7,    76,     0,    78,     7,    76,     0,    13,    20,    35,
    62,     0,    36,     0,    37,     0,    38,     0,    32,     0,
    76,     0,    78,     0,    35,     0,    35,    47,    35,     0
};

#endif

#if YYDEBUG != 0
static const short yyrline[] = { 0,
   110,   111,   118,   119,   120,   121,   122,   123,   132,   137,
   141,   142,   147,   164,   165,   166,   174,   178,   185,   189,
   203,   209,   211,   213,   217,   218,   219,   229,   235,   239,
   246,   247,   251,   253,   258,   260,   264,   266,   268,   270,
   272,   274,   276,   278,   283,   284,   287,   289,   293,   294,
   295,   304,   316,   324,   328,   336,   340,   352,   359,   362,
   372,   384,   389,   394,   399,   405,   409,   416,   426
};
#endif


#if YYDEBUG != 0 || defined (YYERROR_VERBOSE)

static const char * const yytname[] = {   "$","error","$undefined.","END_OF_INPUT",
"GE","LE","NE","EQ","GT","LT","CREATE","DROP","INSERT","DELETE","SELECT","UPDATE",
"ALL","DISTINCT","WHERE","ORDER","FROM","INTO","TABLE","BY","ASC","DESC","LIKE",
"AND","OR","VALUES","SET","NOT","NULLSYM","PRIMARY","KEY","IDENT","TEXT","NUM",
"REAL_NUM","INT","BOOL","CHAR","REAL","'('","')'","','","'*'","'.'","query",
"verb_clause","create","@1","field_list","field_list_item","type","opt_nullspec",
"opt_keyspec","select","dist_qual","item_list","field","table_list","where_clause",
"cond_list","cond_cont","cond_op","order_clause","order_list","order_dir","drop",
"insert","fields","values","update","update_list","delete","literal","cond_literal",
"qual_ident", NULL
};
#endif

static const short yyr1[] = {     0,
    48,    48,    49,    49,    49,    49,    49,    49,    51,    50,
    52,    52,    53,    54,    54,    54,    55,    55,    56,    56,
    57,    58,    58,    58,    59,    59,    59,    60,    61,    61,
    62,    62,    63,    63,    64,    64,    65,    65,    65,    65,
    65,    65,    65,    65,    66,    66,    67,    67,    68,    68,
    68,    69,    70,    71,    71,    72,    72,    73,    74,    74,
    75,    76,    76,    76,    76,    77,    77,    78,    78
};

static const short yyr2[] = {     0,
     0,     2,     1,     1,     1,     1,     1,     1,     0,     7,
     1,     3,     4,     1,     1,     4,     0,     2,     0,     2,
     7,     0,     1,     1,     3,     1,     1,     1,     1,     3,
     0,     2,     5,     3,     1,     1,     1,     1,     1,     1,
     1,     1,     1,     2,     0,     3,     4,     2,     1,     1,
     0,     3,    10,     3,     1,     3,     1,     5,     5,     3,
     4,     1,     1,     1,     1,     1,     1,     1,     3
};

static const short yydefact[] = {     1,
     0,     0,     0,     0,    22,     0,     0,     3,     4,     5,
     6,     7,     8,     0,     0,     0,     0,    23,    24,     0,
     0,     2,     0,    52,     0,    31,    68,    27,     0,    26,
    28,     0,     9,     0,     0,    61,     0,     0,     0,    31,
     0,     0,     0,    55,    32,     0,    69,    29,    31,    25,
     0,    58,     0,     0,    11,     0,     0,     0,    35,    36,
     0,    42,    40,    38,    37,    41,    39,    43,     0,     0,
     0,    45,     0,    65,    62,    63,    64,    60,    10,     0,
    14,     0,    15,    17,     0,    54,     0,    44,    66,    34,
    67,    30,     0,    21,     0,    12,     0,     0,    19,     0,
     0,     0,    59,     0,    18,     0,    13,     0,    57,    33,
    46,    51,    16,    20,    53,     0,     0,    49,    50,    48,
    56,    51,    47,     0,     0,     0
};

static const short yydefgoto[] = {   124,
     7,     8,    42,    54,    55,    84,    99,   107,     9,    20,
    29,    30,    49,    36,    45,    61,    70,    94,   111,   120,
    10,    11,    43,   108,    12,    40,    13,    89,    90,    31
};

static const short yypact[] = {    46,
     3,    11,     1,     7,    -8,    -5,    44,-32768,-32768,-32768,
-32768,-32768,-32768,    15,    31,    33,    38,-32768,-32768,   -28,
    22,-32768,    32,-32768,    34,    56,    29,-32768,   -14,-32768,
-32768,    43,-32768,    43,    43,-32768,    45,    48,    43,   -17,
    72,    43,    -9,-32768,    35,     8,-32768,-32768,   -13,-32768,
    43,-32768,    17,     0,-32768,   -18,    52,    43,-32768,-32768,
    43,-32768,-32768,-32768,-32768,-32768,-32768,-32768,    58,     5,
    51,    63,    80,-32768,-32768,-32768,-32768,-32768,-32768,    43,
-32768,    49,-32768,    57,    50,-32768,     8,-32768,-32768,-32768,
-32768,-32768,    66,-32768,    17,-32768,    53,    59,    61,    17,
     5,    43,-32768,    54,-32768,    62,-32768,    20,-32768,-32768,
    55,    47,-32768,-32768,-32768,    17,    43,-32768,-32768,-32768,
-32768,    47,-32768,    95,    97,-32768
};

static const short yypgoto[] = {-32768,
-32768,-32768,-32768,-32768,    19,-32768,-32768,-32768,-32768,-32768,
-32768,    64,-32768,   -29,-32768,-32768,    14,-32768,-32768,   -20,
-32768,-32768,-32768,-32768,-32768,-32768,-32768,   -49,     4,   -32
};


#define	YYLAST		105


static const short yytable[] = {    41,
    35,    44,    46,    78,    35,    38,    27,    18,    19,    56,
    52,    62,    63,    64,    65,    66,    67,    28,    73,    72,
    81,    16,    82,    83,    14,    86,    17,    51,    87,    21,
    39,    71,    15,    68,    57,    58,    74,    91,    69,    27,
    75,    76,    77,    79,    80,   103,    22,    56,    74,    23,
   109,    32,    75,    76,    77,     1,     2,     3,     4,     5,
     6,    59,    60,   115,   116,    24,   121,    25,    91,   112,
   118,   119,    26,    35,    33,    37,    34,    27,    53,    47,
    85,    93,    48,    88,   122,    92,    95,    98,   102,   104,
   105,    97,   100,   106,   125,   114,   126,   113,    96,   117,
   101,   123,    50,     0,   110
};

static const short yycheck[] = {    32,
    18,    34,    35,    53,    18,    20,    35,    16,    17,    42,
    40,     4,     5,     6,     7,     8,     9,    46,    51,    49,
    39,    21,    41,    42,    22,    58,    20,    45,    61,    35,
    45,    45,    22,    26,    44,    45,    32,    70,    31,    35,
    36,    37,    38,    44,    45,    95,     3,    80,    32,    35,
   100,    30,    36,    37,    38,    10,    11,    12,    13,    14,
    15,    27,    28,    44,    45,    35,   116,    35,   101,   102,
    24,    25,    35,    18,    43,    47,    43,    35,     7,    35,
    29,    19,    35,    26,   117,    35,     7,    31,    23,    37,
    32,    43,    43,    33,     0,    34,     0,    44,    80,    45,
    87,   122,    39,    -1,   101
};
/* -*-C-*-  Note some compilers choke on comments on `#line' lines.  */
#line 3 "/usr/lib/bison.simple"
/* This file comes from bison-1.28.  */

/* Skeleton output parser for bison,
   Copyright (C) 1984, 1989, 1990 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* This is the parser code that is written into each bison parser
  when the %semantic_parser declaration is not specified in the grammar.
  It was written by Richard Stallman by simplifying the hairy parser
  used when %semantic_parser is specified.  */

#ifndef YYSTACK_USE_ALLOCA
#ifdef alloca
#define YYSTACK_USE_ALLOCA
#else /* alloca not defined */
#ifdef __GNUC__
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#else /* not GNU C.  */
#if (!defined (__STDC__) && defined (sparc)) || defined (__sparc__) || defined (__sparc) || defined (__sgi) || (defined (__sun) && defined (__i386))
#define YYSTACK_USE_ALLOCA
#include <alloca.h>
#else /* not sparc */
/* We think this test detects Watcom and Microsoft C.  */
/* This used to test MSDOS, but that is a bad idea
   since that symbol is in the user namespace.  */
#if (defined (_MSDOS) || defined (_MSDOS_)) && !defined (__TURBOC__)
#if 0 /* No need for malloc.h, which pollutes the namespace;
	 instead, just don't use alloca.  */
#include <malloc.h>
#endif
#else /* not MSDOS, or __TURBOC__ */
#if defined(_AIX)
/* I don't know what this was needed for, but it pollutes the namespace.
   So I turned it off.   rms, 2 May 1997.  */
/* #include <malloc.h>  */
 #pragma alloca
#define YYSTACK_USE_ALLOCA
#else /* not MSDOS, or __TURBOC__, or _AIX */
#if 0
#ifdef __hpux /* haible@ilog.fr says this works for HPUX 9.05 and up,
		 and on HPUX 10.  Eventually we can turn this on.  */
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#endif /* __hpux */
#endif
#endif /* not _AIX */
#endif /* not MSDOS, or __TURBOC__ */
#endif /* not sparc */
#endif /* not GNU C */
#endif /* alloca not defined */
#endif /* YYSTACK_USE_ALLOCA not defined */

#ifdef YYSTACK_USE_ALLOCA
#define YYSTACK_ALLOC alloca
#else
#define YYSTACK_ALLOC malloc
#endif

/* Note: there must be only one dollar sign in this file.
   It is replaced by the list of actions, each action
   as one case of the switch.  */

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		-2
#define YYEOF		0
#define YYACCEPT	goto yyacceptlab
#define YYABORT 	goto yyabortlab
#define YYERROR		goto yyerrlab1
/* Like YYERROR except do call yyerror.
   This remains here temporarily to ease the
   transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */
#define YYFAIL		goto yyerrlab
#define YYRECOVERING()  (!!yyerrstatus)
#define YYBACKUP(token, value) \
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    { yychar = (token), yylval = (value);			\
      yychar1 = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { yyerror ("syntax error: cannot back up"); YYERROR; }	\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

#ifndef YYPURE
#define YYLEX		yylex()
#endif

#ifdef YYPURE
#ifdef YYLSP_NEEDED
#ifdef YYLEX_PARAM
#define YYLEX		yylex(&yylval, &yylloc, YYLEX_PARAM)
#else
#define YYLEX		yylex(&yylval, &yylloc)
#endif
#else /* not YYLSP_NEEDED */
#ifdef YYLEX_PARAM
#define YYLEX		yylex(&yylval, YYLEX_PARAM)
#else
#define YYLEX		yylex(&yylval)
#endif
#endif /* not YYLSP_NEEDED */
#endif

/* If nonreentrant, generate the variables here */

#ifndef YYPURE

int	yychar;			/*  the lookahead symbol		*/
YYSTYPE	yylval;			/*  the semantic value of the		*/
				/*  lookahead symbol			*/

#ifdef YYLSP_NEEDED
YYLTYPE yylloc;			/*  location data for the lookahead	*/
				/*  symbol				*/
#endif

int yynerrs;			/*  number of parse errors so far       */
#endif  /* not YYPURE */

#if YYDEBUG != 0
int yydebug;			/*  nonzero means print parse trace	*/
/* Since this is uninitialized, it does not stop multiple parsers
   from coexisting.  */
#endif

/*  YYINITDEPTH indicates the initial size of the parser's stacks	*/

#ifndef	YYINITDEPTH
#define YYINITDEPTH 200
#endif

/*  YYMAXDEPTH is the maximum size the stacks can grow to
    (effective only if the built-in stack extension method is used).  */

#if YYMAXDEPTH == 0
#undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
#define YYMAXDEPTH 10000
#endif

/* Define __yy_memcpy.  Note that the size argument
   should be passed with type unsigned int, because that is what the non-GCC
   definitions require.  With GCC, __builtin_memcpy takes an arg
   of type size_t, but it can handle unsigned int.  */

#if __GNUC__ > 1		/* GNU C and GNU C++ define this.  */
#define __yy_memcpy(TO,FROM,COUNT)	__builtin_memcpy(TO,FROM,COUNT)
#else				/* not GNU C or C++ */
#ifndef __cplusplus

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__yy_memcpy (to, from, count)
     char *to;
     char *from;
     unsigned int count;
{
  register char *f = from;
  register char *t = to;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#else /* __cplusplus */

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__yy_memcpy (char *to, char *from, unsigned int count)
{
  register char *t = to;
  register char *f = from;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#endif
#endif

#line 217 "/usr/lib/bison.simple"

/* The user can define YYPARSE_PARAM as the name of an argument to be passed
   into yyparse.  The argument should have type void *.
   It should actually point to an object.
   Grammar actions can access the variable by casting it
   to the proper pointer type.  */

#ifdef YYPARSE_PARAM
#ifdef __cplusplus
#define YYPARSE_PARAM_ARG void *YYPARSE_PARAM
#define YYPARSE_PARAM_DECL
#else /* not __cplusplus */
#define YYPARSE_PARAM_ARG YYPARSE_PARAM
#define YYPARSE_PARAM_DECL void *YYPARSE_PARAM;
#endif /* not __cplusplus */
#else /* not YYPARSE_PARAM */
#define YYPARSE_PARAM_ARG
#define YYPARSE_PARAM_DECL
#endif /* not YYPARSE_PARAM */

/* Prevent warning if -Wstrict-prototypes.  */
#ifdef __GNUC__
#ifdef YYPARSE_PARAM
int yyparse (void *);
#else
int yyparse (void);
#endif
#endif

int
yyparse(YYPARSE_PARAM_ARG)
     YYPARSE_PARAM_DECL
{
  register int yystate;
  register int yyn;
  register short *yyssp;
  register YYSTYPE *yyvsp;
  int yyerrstatus;	/*  number of tokens to shift before error messages enabled */
  int yychar1 = 0;		/*  lookahead token as an internal (translated) token number */

  short	yyssa[YYINITDEPTH];	/*  the state stack			*/
  YYSTYPE yyvsa[YYINITDEPTH];	/*  the semantic value stack		*/

  short *yyss = yyssa;		/*  refer to the stacks thru separate pointers */
  YYSTYPE *yyvs = yyvsa;	/*  to allow yyoverflow to reallocate them elsewhere */

#ifdef YYLSP_NEEDED
  YYLTYPE yylsa[YYINITDEPTH];	/*  the location stack			*/
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;

#define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)
#else
#define YYPOPSTACK   (yyvsp--, yyssp--)
#endif

  int yystacksize = YYINITDEPTH;
  int yyfree_stacks = 0;

#ifdef YYPURE
  int yychar;
  YYSTYPE yylval;
  int yynerrs;
#ifdef YYLSP_NEEDED
  YYLTYPE yylloc;
#endif
#endif

  YYSTYPE yyval;		/*  the variable used to return		*/
				/*  semantic values from the action	*/
				/*  routines				*/

  int yylen;

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Starting parse\n");
#endif

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss - 1;
  yyvsp = yyvs;
#ifdef YYLSP_NEEDED
  yylsp = yyls;
#endif

/* Push a new state, which is found in  yystate  .  */
/* In all cases, when you get here, the value and location stacks
   have just been pushed. so pushing a state here evens the stacks.  */
yynewstate:

  *++yyssp = yystate;

  if (yyssp >= yyss + yystacksize - 1)
    {
      /* Give user a chance to reallocate the stack */
      /* Use copies of these so that the &'s don't force the real ones into memory. */
      YYSTYPE *yyvs1 = yyvs;
      short *yyss1 = yyss;
#ifdef YYLSP_NEEDED
      YYLTYPE *yyls1 = yyls;
#endif

      /* Get the current used size of the three stacks, in elements.  */
      int size = yyssp - yyss + 1;

#ifdef yyoverflow
      /* Each stack pointer address is followed by the size of
	 the data in use in that stack, in bytes.  */
#ifdef YYLSP_NEEDED
      /* This used to be a conditional around just the two extra args,
	 but that might be undefined if yyoverflow is a macro.  */
      yyoverflow("parser stack overflow",
		 &yyss1, size * sizeof (*yyssp),
		 &yyvs1, size * sizeof (*yyvsp),
		 &yyls1, size * sizeof (*yylsp),
		 &yystacksize);
#else
      yyoverflow("parser stack overflow",
		 &yyss1, size * sizeof (*yyssp),
		 &yyvs1, size * sizeof (*yyvsp),
		 &yystacksize);
#endif

      yyss = yyss1; yyvs = yyvs1;
#ifdef YYLSP_NEEDED
      yyls = yyls1;
#endif
#else /* no yyoverflow */
      /* Extend the stack our own way.  */
      if (yystacksize >= YYMAXDEPTH)
	{
	  yyerror("parser stack overflow");
	  if (yyfree_stacks)
	    {
	      free (yyss);
	      free (yyvs);
#ifdef YYLSP_NEEDED
	      free (yyls);
#endif
	    }
	  return 2;
	}
      yystacksize *= 2;
      if (yystacksize > YYMAXDEPTH)
	yystacksize = YYMAXDEPTH;
#ifndef YYSTACK_USE_ALLOCA
      yyfree_stacks = 1;
#endif
      yyss = (short *) YYSTACK_ALLOC (yystacksize * sizeof (*yyssp));
      __yy_memcpy ((char *)yyss, (char *)yyss1,
		   size * (unsigned int) sizeof (*yyssp));
      yyvs = (YYSTYPE *) YYSTACK_ALLOC (yystacksize * sizeof (*yyvsp));
      __yy_memcpy ((char *)yyvs, (char *)yyvs1,
		   size * (unsigned int) sizeof (*yyvsp));
#ifdef YYLSP_NEEDED
      yyls = (YYLTYPE *) YYSTACK_ALLOC (yystacksize * sizeof (*yylsp));
      __yy_memcpy ((char *)yyls, (char *)yyls1,
		   size * (unsigned int) sizeof (*yylsp));
#endif
#endif /* no yyoverflow */

      yyssp = yyss + size - 1;
      yyvsp = yyvs + size - 1;
#ifdef YYLSP_NEEDED
      yylsp = yyls + size - 1;
#endif

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Stack size increased to %d\n", yystacksize);
#endif

      if (yyssp >= yyss + yystacksize - 1)
	YYABORT;
    }

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Entering state %d\n", yystate);
#endif

  goto yybackup;
 yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* yychar is either YYEMPTY or YYEOF
     or a valid token in external form.  */

  if (yychar == YYEMPTY)
    {
#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Reading a token: ");
#endif
      yychar = YYLEX;
    }

  /* Convert token to internal form (in yychar1) for indexing tables with */

  if (yychar <= 0)		/* This means end of input. */
    {
      yychar1 = 0;
      yychar = YYEOF;		/* Don't call YYLEX any more */

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Now at end of input.\n");
#endif
    }
  else
    {
      yychar1 = YYTRANSLATE(yychar);

#if YYDEBUG != 0
      if (yydebug)
	{
	  fprintf (stderr, "Next token is %d (%s", yychar, yytname[yychar1]);
	  /* Give the individual parser a way to print the precise meaning
	     of a token, for further debugging info.  */
#ifdef YYPRINT
	  YYPRINT (stderr, yychar, yylval);
#endif
	  fprintf (stderr, ")\n");
	}
#endif
    }

  yyn += yychar1;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != yychar1)
    goto yydefault;

  yyn = yytable[yyn];

  /* yyn is what to do for this token type in this state.
     Negative => reduce, -yyn is rule number.
     Positive => shift, yyn is new state.
       New state is final state => don't bother to shift,
       just return success.
     0, or most negative number => error.  */

  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrlab;

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Shifting token %d (%s), ", yychar, yytname[yychar1]);
#endif

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
#ifdef YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  /* count tokens shifted since error; after three, turn off error status.  */
  if (yyerrstatus) yyerrstatus--;

  yystate = yyn;
  goto yynewstate;

/* Do the default action for the current state.  */
yydefault:

  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;

/* Do a reduction.  yyn is the number of a rule to reduce with.  */
yyreduce:
  yylen = yyr2[yyn];
  if (yylen > 0)
    yyval = yyvsp[1-yylen]; /* implement default value of the action */

#if YYDEBUG != 0
  if (yydebug)
    {
      int i;

      fprintf (stderr, "Reducing via rule %d (line %d), ",
	       yyn, yyrline[yyn]);

      /* Print the symbols being reduced, and their result.  */
      for (i = yyprhs[yyn]; yyrhs[i] > 0; i++)
	fprintf (stderr, "%s ", yytname[yyrhs[i]]);
      fprintf (stderr, " -> %s\n", yytname[yyr1[yyn]]);
    }
#endif


  switch (yyn) {

case 2:
#line 112 "msql_yacc.y"
{	
			msqlProcessQuery();
			msqlClean();
		;
    break;}
case 9:
#line 133 "msql_yacc.y"
{
			command = CREATE;
			msqlAddTable(yyvsp[-1]);
		;
    break;}
case 13:
#line 148 "msql_yacc.y"
{ 
			if(msqlAddField(yyvsp[-3],yyvsp[-2],arrayLen,notnullflag,keyflag)<0)
			{
				msqlClean();
				return;
			}
			if (arrayLen)
			{
				(void)myFree(arrayLen);
			}
			arrayLen = 0;
		;
    break;}
case 16:
#line 167 "msql_yacc.y"
{ 
			arrayLen = yyvsp[-1]; 
			yyval=yyvsp[-3]; 
		;
    break;}
case 17:
#line 175 "msql_yacc.y"
{
			notnullflag = 0;
		;
    break;}
case 18:
#line 179 "msql_yacc.y"
{
			notnullflag = 1;
		;
    break;}
case 19:
#line 186 "msql_yacc.y"
{
			keyflag = 0;
		;
    break;}
case 20:
#line 190 "msql_yacc.y"
{
			keyflag = 1;
		;
    break;}
case 21:
#line 204 "msql_yacc.y"
{
			command = SELECT;
		;
    break;}
case 22:
#line 210 "msql_yacc.y"
{ selectDistinct = 0; ;
    break;}
case 23:
#line 212 "msql_yacc.y"
{ selectDistinct = 0; ;
    break;}
case 24:
#line 214 "msql_yacc.y"
{ selectDistinct = 1; ;
    break;}
case 27:
#line 220 "msql_yacc.y"
{
			ident_t	*tmp;

			tmp = msqlCreateIdent(NULL,"*");
			msqlAddField(tmp,0,0,0,0);
			selectWildcard = 1;
		;
    break;}
case 28:
#line 230 "msql_yacc.y"
{
			msqlAddField(yyvsp[0],0,0,0,0);
		;
    break;}
case 29:
#line 236 "msql_yacc.y"
{
			msqlAddTable(yyvsp[0]);
		;
    break;}
case 30:
#line 240 "msql_yacc.y"
{
			msqlAddTable(yyvsp[0]);
		;
    break;}
case 33:
#line 252 "msql_yacc.y"
{ msqlAddCond(yyvsp[-2],yyvsp[-1],yyvsp[0],yyvsp[-3]); ;
    break;}
case 34:
#line 254 "msql_yacc.y"
{ msqlAddCond(yyvsp[-2],yyvsp[-1],yyvsp[0],NO_BOOL); ;
    break;}
case 35:
#line 259 "msql_yacc.y"
{ yyval = (char *)AND_BOOL; ;
    break;}
case 36:
#line 261 "msql_yacc.y"
{ yyval = (char *)OR_BOOL; ;
    break;}
case 37:
#line 265 "msql_yacc.y"
{ yyval = (char *)EQ_OP; ;
    break;}
case 38:
#line 267 "msql_yacc.y"
{ yyval = (char *)NE_OP; ;
    break;}
case 39:
#line 269 "msql_yacc.y"
{ yyval = (char *)LT_OP; ;
    break;}
case 40:
#line 271 "msql_yacc.y"
{ yyval = (char *)LE_OP; ;
    break;}
case 41:
#line 273 "msql_yacc.y"
{ yyval = (char *)GT_OP; ;
    break;}
case 42:
#line 275 "msql_yacc.y"
{ yyval = (char *)GE_OP; ;
    break;}
case 43:
#line 277 "msql_yacc.y"
{ yyval = (char *)LIKE_OP; ;
    break;}
case 44:
#line 279 "msql_yacc.y"
{ yyval = (char *)NOT_LIKE_OP; ;
    break;}
case 47:
#line 288 "msql_yacc.y"
{ msqlAddOrder(yyvsp[-1],(int) yyvsp[0]); ;
    break;}
case 48:
#line 290 "msql_yacc.y"
{ msqlAddOrder(yyvsp[-1],(int) yyvsp[0]); ;
    break;}
case 51:
#line 296 "msql_yacc.y"
{ yyval = (char *) ASC; ;
    break;}
case 52:
#line 305 "msql_yacc.y"
{
			command = DROP;
			msqlAddTable(yyvsp[0]);
		;
    break;}
case 53:
#line 317 "msql_yacc.y"
{
			command = INSERT;
			msqlAddTable(yyvsp[-7]);
		;
    break;}
case 54:
#line 325 "msql_yacc.y"
{ 
			msqlAddField(yyvsp[0],0,0,0,0);
		;
    break;}
case 55:
#line 329 "msql_yacc.y"
{ 
			msqlAddField(yyvsp[0],0,0,0,0); 
		;
    break;}
case 56:
#line 337 "msql_yacc.y"
{ 
			msqlAddFieldValue(yyvsp[0]);
		 ;
    break;}
case 57:
#line 341 "msql_yacc.y"
{ 
			msqlAddFieldValue(yyvsp[0]); 
		;
    break;}
case 58:
#line 353 "msql_yacc.y"
{
			command = UPDATE;
			msqlAddTable(yyvsp[-3]);
		;
    break;}
case 59:
#line 360 "msql_yacc.y"
{ msqlAddField(yyvsp[-2],0,0,0,0);
		  msqlAddFieldValue(yyvsp[0]); ;
    break;}
case 60:
#line 363 "msql_yacc.y"
{ msqlAddField(yyvsp[-2],0,0,0,0);
		  msqlAddFieldValue(yyvsp[0]); ;
    break;}
case 61:
#line 373 "msql_yacc.y"
{
			command = DELETE;
			msqlAddTable(yyvsp[-1]);
		;
    break;}
case 62:
#line 385 "msql_yacc.y"
{
			yyval = (char *)msqlCreateValue(yyvsp[0],CHAR_TYPE,yytoklen);
			(void)myFree(yyvsp[0]);
		;
    break;}
case 63:
#line 390 "msql_yacc.y"
{
			yyval = (char *)msqlCreateValue(yyvsp[0],INT_TYPE,0);
			(void)myFree(yyvsp[0]);
		;
    break;}
case 64:
#line 395 "msql_yacc.y"
{
			yyval = (char *)msqlCreateValue(yyvsp[0],REAL_TYPE,0);
			(void)myFree(yyvsp[0]);
		;
    break;}
case 65:
#line 400 "msql_yacc.y"
{
			yyval = (char *)msqlCreateValue("null",NULL_TYPE,0);
		;
    break;}
case 66:
#line 406 "msql_yacc.y"
{
			yyval = yyvsp[0];
		;
    break;}
case 67:
#line 410 "msql_yacc.y"
{
			yyval = (char *)msqlCreateValue(yyvsp[0],IDENT_TYPE);
		;
    break;}
case 68:
#line 417 "msql_yacc.y"
{ 
			yyval = (char *)msqlCreateIdent(NULL,yyvsp[0]); 
			(void)myFree(yyvsp[0]);
			if (yyval == NULL)
			{
				msqlClean();
				return;
			}
		;
    break;}
case 69:
#line 427 "msql_yacc.y"
{ 
			yyval = (char *)msqlCreateIdent(yyvsp[-2],yyvsp[0]); 
			(void)myFree(yyvsp[-2]);
			(void)myFree(yyvsp[0]);
			if (yyval == NULL)
			{
				msqlClean();
				return;
			}
		;
    break;}
}
   /* the action file gets copied in in place of this dollarsign */
#line 543 "/usr/lib/bison.simple"

  yyvsp -= yylen;
  yyssp -= yylen;
#ifdef YYLSP_NEEDED
  yylsp -= yylen;
#endif

#if YYDEBUG != 0
  if (yydebug)
    {
      short *ssp1 = yyss - 1;
      fprintf (stderr, "state stack now");
      while (ssp1 != yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

  *++yyvsp = yyval;

#ifdef YYLSP_NEEDED
  yylsp++;
  if (yylen == 0)
    {
      yylsp->first_line = yylloc.first_line;
      yylsp->first_column = yylloc.first_column;
      yylsp->last_line = (yylsp-1)->last_line;
      yylsp->last_column = (yylsp-1)->last_column;
      yylsp->text = 0;
    }
  else
    {
      yylsp->last_line = (yylsp+yylen-1)->last_line;
      yylsp->last_column = (yylsp+yylen-1)->last_column;
    }
#endif

  /* Now "shift" the result of the reduction.
     Determine what state that goes to,
     based on the state we popped back to
     and the rule number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTBASE] + *yyssp;
  if (yystate >= 0 && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTBASE];

  goto yynewstate;

yyerrlab:   /* here on detecting error */

  if (! yyerrstatus)
    /* If not already recovering from an error, report this error.  */
    {
      ++yynerrs;

#ifdef YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (yyn > YYFLAG && yyn < YYLAST)
	{
	  int size = 0;
	  char *msg;
	  int x, count;

	  count = 0;
	  /* Start X at -yyn if nec to avoid negative indexes in yycheck.  */
	  for (x = (yyn < 0 ? -yyn : 0);
	       x < (sizeof(yytname) / sizeof(char *)); x++)
	    if (yycheck[x + yyn] == x)
	      size += strlen(yytname[x]) + 15, count++;
	  msg = (char *) malloc(size + 15);
	  if (msg != 0)
	    {
	      strcpy(msg, "parse error");

	      if (count < 5)
		{
		  count = 0;
		  for (x = (yyn < 0 ? -yyn : 0);
		       x < (sizeof(yytname) / sizeof(char *)); x++)
		    if (yycheck[x + yyn] == x)
		      {
			strcat(msg, count == 0 ? ", expecting `" : " or `");
			strcat(msg, yytname[x]);
			strcat(msg, "'");
			count++;
		      }
		}
	      yyerror(msg);
	      free(msg);
	    }
	  else
	    yyerror ("parse error; also virtual memory exceeded");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror("parse error");
    }

  goto yyerrlab1;
yyerrlab1:   /* here on error raised explicitly by an action */

  if (yyerrstatus == 3)
    {
      /* if just tried and failed to reuse lookahead token after an error, discard it.  */

      /* return failure if at end of input */
      if (yychar == YYEOF)
	YYABORT;

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Discarding token %d (%s).\n", yychar, yytname[yychar1]);
#endif

      yychar = YYEMPTY;
    }

  /* Else will try to reuse lookahead token
     after shifting the error token.  */

  yyerrstatus = 3;		/* Each real token shifted decrements this */

  goto yyerrhandle;

yyerrdefault:  /* current state does not do anything special for the error token. */

#if 0
  /* This is wrong; only states that explicitly want error tokens
     should shift them.  */
  yyn = yydefact[yystate];  /* If its default is to accept any token, ok.  Otherwise pop it.*/
  if (yyn) goto yydefault;
#endif

yyerrpop:   /* pop the current state because it cannot handle the error token */

  if (yyssp == yyss) YYABORT;
  yyvsp--;
  yystate = *--yyssp;
#ifdef YYLSP_NEEDED
  yylsp--;
#endif

#if YYDEBUG != 0
  if (yydebug)
    {
      short *ssp1 = yyss - 1;
      fprintf (stderr, "Error: state stack now");
      while (ssp1 != yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

yyerrhandle:

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yyerrdefault;

  yyn += YYTERROR;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != YYTERROR)
    goto yyerrdefault;

  yyn = yytable[yyn];
  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrpop;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrpop;

  if (yyn == YYFINAL)
    YYACCEPT;

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Shifting error token, ");
#endif

  *++yyvsp = yylval;
#ifdef YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  yystate = yyn;
  goto yynewstate;

 yyacceptlab:
  /* YYACCEPT comes here.  */
  if (yyfree_stacks)
    {
      free (yyss);
      free (yyvs);
#ifdef YYLSP_NEEDED
      free (yyls);
#endif
    }
  return 0;

 yyabortlab:
  /* YYABORT comes here.  */
  if (yyfree_stacks)
    {
      free (yyss);
      free (yyvs);
#ifdef YYLSP_NEEDED
      free (yyls);
#endif
    }
  return 1;
}
#line 437 "msql_yacc.y"
