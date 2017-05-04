#
# Dummy Makefile used with makegen.
#
# NOTE : The TOP variable must be set to reflect the top of the
# src tree (either relative or absolute)
#

TOP=.
TARGETS= all ESL MSQL install esl-install msql-install clean dbsetup


$(TARGETS) :: Makefile.full
	@$(MAKE) -f Makefile.full $@

Makefile.full :: $(TOP)/site.mm Makefile.tmpl
	@$(TOP)/makegen/makegen $(TOP)

