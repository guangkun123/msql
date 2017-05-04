#
# Dummy Makefile used with makegen.
#
# NOTE : The TOP variable must be set to reflect the top of the
# src tree (either relative or absolute)
#

TOP=..
TARGETS= all ESL MSQL install esl-install msql-install clean

$(TARGETS) :: Makefile.full
	@$(MAKE) -f Makefile.full $@

Makefile.full :: $(TOP)/site.mm Makefile.tmpl
	@$(TOP)/makegen/makegen $(TOP) ;\
	echo "Depending ....." ;\
	echo >> Makefile.full ;\
	echo "######### Dependencies #########" >> Makefile.full ;\
	make -f Makefile.full depend ;\
	echo "Done."


