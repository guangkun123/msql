############################################################################
#
# Top level Makefile for mSQL.  This only sets up a target tree for the
# current platform.  It doesn't do any other part of the build.
#
#						bambi
############################################################################

#
# Note : If the current box doesn't support sym-links with "ln -s" then
#	 set the ln option required below.  If it doesn't support
#	 sym-links at all, set the option to be nothing. This'll force
#	 hard links.

SYM_OPT= -s


SHELL=/bin/sh

all:
	@ echo ""; echo "You have not read the installation procedures.";\
	echo "Please read the README file for build instructions.";\
	echo

target:
	@ scripts/make-target



dist:
	@ echo; echo -n "Full Distribution - Enter archive file name : " ;\
	read TAR_FILE ;\
	DIR=`pwd | sed "s,.*/,,"` ; \
	cd ..;\
	rm -f /tmp/dist.files ;\
	for FILE in `find ${DIR} ! -type d -print | egrep -v "/targets/|CVS"` ;\
	do \
		echo $$FILE >> /tmp/dist.files ;\
	done ;\
	tar -cvf $$TAR_FILE -I /tmp/dist.files ;\
	echo; echo "Archive of full distribution complete"
