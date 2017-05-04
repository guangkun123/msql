#!/bin/sh

. $TOP/makegen/makegen.cf

src=$1
dest=$2
mode=$3
owner=$4
group=$5

echo	"install ::"
echo	"	cp $src $dest"
echo	"	$ranlib $dest"
if test "$mode." != "."
then
	echo	"	$chmod $mode $dest"
fi
if test "$owner." = "root."
then
	echo	"	$chown $owner $dest"
fi
if test "$group." != "."
then
	echo	"	$chgrp $group $dest"
fi
