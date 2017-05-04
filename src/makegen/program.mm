#!/bin/sh

. $TOP/makegen/makegen.cf

prog=$1
progobjs=`echo $2 | sed "s/,/ /g"`
proglibs=`echo $3 | sed "s/,/ /g"`
shift

echo
echo	"# Make rules for building $prog"
echo
echo	"all : $prog"
echo
echo	"$prog : $progobjs"
echo	'	$(LINK) $(CC_FLAGS)'" $progobjs -o $prog "'$(LD_LIBS)'" $proglibs"
echo
echo	"clean :: "
echo	"	rm -f $prog"
echo
#echo	"depend ::"
#echo	"	@for I in $progobjs;\\"
#echo	"	do echo \"$prog	:\$\${I}\">>Makefile.full;\\"
#echo	"	done"
#echo
