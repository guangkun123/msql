#!/bin/sh

. $TOP/makegen/makegen.cf

lib=$1
libsrc=`echo $2 | sed "s/,/ /g"`
libobj=`echo $3 | sed "s/,/ /g"`

for src in $libsrc
do
	base=`echo $src | sed "s/\..*//"`
	obj=`echo $src | sed "s/\.c\$/.o/"`
	libobj="$libobj $obj"
	echo	"$obj : $src"
	echo	'	$(CC) $(CC_ONLY) $(CC_FLAGS) -c '"$src"
	echo
	echo	"clean ::"
	echo	"	rm -f $obj"
	echo
	echo    "depend ::"
	echo    '	@$(TOP)/makedepend/makedepend -a -fMakefile.full $(CC_FLAGS)' $src >/dev/null 2>&1
	echo

	shift
done

echo	"all : $lib"
echo
echo	"$lib : $libobj"
echo	"	ar rc $lib $libobj"
echo	"	$ranlib $lib"
echo
echo	"clean :: "
echo	"	rm -f $lib"
echo
