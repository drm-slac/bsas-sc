# Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG

DIRS += configure
DIRS += managerApp
DIRS += nttableApp
DIRS += commonApp
DIRS += highfiveApp
DIRS += mergerApp
DIRS += writerApp
DIRS += documentation
#DIRS += test

# All dirs except configure depend on configure
$(foreach dir, $(filter-out configure, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += configure))

# additional dependency rules:
commonApp_DEPEND_DIRS += nttableApp
writerApp_DEPEND_DIRS += commonApp
writerApp_DEPEND_DIRS += highfiveApp

USR_CPPFLAGS += -std-c++11

include $(TOP)/configure/RULES_TOP

UNINSTALL_DIRS += $(wildcard $(INSTALL_LOCATION)/python*)
