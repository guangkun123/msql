#!/bin/sh

. $TOP/makegen/makegen.cf

obj=$1
src=$2
shift; shift
deps=$*

base=`echo $src | sed "s/\..*//"`

echo
echo	"# Make rules for building $obj"
echo
echo	"$obj : $src $deps"
echo	'	$(CC) $(CC_FLAGS)'" -c $src"
echo
echo	"clean ::"
echo	"	rm -f $obj $base.lint"
echo
echo	"depend ::"
echo	'	@$(TOP)/makedepend/makedepend -a -fMakefile.full $(CC_FLAGS)' $src 2>/dev/null
echo
