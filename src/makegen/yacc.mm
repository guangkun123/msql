#!/bin/sh

. $TOP/makegen/makegen.cf

src=$1
shift
comment=$*
outfile=`echo $src | sed "s/\.y\$/.c/"`
obj=`echo $src | sed "s/\.y\$/.o/"`

echo	"$outfile : $src"
echo	"	@echo \"$comment\""
echo	'	$(YACC) $(YACC_FLAGS)'" $src"
echo	"	mv y.tab.c $outfile"
echo
echo	"$obj : $outfile"
echo    '	$(CC) $(CC_FLAGS)'" -c $outfile"
echo
echo	"clean ::"
echo	"	rm -f $outfile y.tab.*"
echo
echo	"depend ::"
echo	"	@touch y.tab.h"
echo
