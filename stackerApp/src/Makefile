TOP=../..

include $(TOP)/configure/CONFIG

PROD_IOC = stacker
DBD += stacker.dbd

USR_CPPFLAGS += -DUSE_TYPED_RSET
#USR_CPPFLAGS += -DUSE_TYPED_DSET # Not supported by EPICS 7.0.3

stacker_DBD += base.dbd stackerSup.dbd pvxsIoc.dbd

stacker_SRCS += stacker_registerRecordDeviceDriver.cpp
stacker_SRCS += stackerMain.cpp stackerSup.cpp

stacker_LIBS += pvxsIoc pvxs
stacker_LIBS += common nttable
stacker_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
