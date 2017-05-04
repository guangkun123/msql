#
# Dummy Makefile used with makegen.
#
# NOTE : The TOP variable must be set to reflect the top of the
# src tree (either relative or absolute)
#

TOP=..
TARGETS= all install clean lextest

$(TARGETS) :: Makefile.full

$(TARGETS) ::
	@$(MAKE) -f Makefile.full $@

Makefile.full :: $(TOP)/site.mm Makefile.tmpl
	@$(TOP)/makegen/makegen $(TOP) ;\
	echo "Depending ....." ;\
	echo >> Makefile.full ;\
	$(MAKE) -f Makefile.full depend ;\
	echo "Done."


msqld ::
	$(MAKE) -f Makefile.full msqld
