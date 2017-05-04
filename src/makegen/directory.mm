#!/bin/sh

. $TOP/makegen/makegen.cf

DIR=$1
TARGETS=`echo $2 | sed "s/,/ /g"`

for targ in $TARGETS
do
	echo "$targ ::"
	echo "	@echo					;\\"
	echo "	echo \"--> [$DIR] directory  \"        	;\\"
	echo "	cd $DIR                            	;\\"
	echo "	make \$(MFLAGS)	$targ			;\\"
	echo "	echo \"<-- [$DIR] done       \"		"
	echo ""
done
