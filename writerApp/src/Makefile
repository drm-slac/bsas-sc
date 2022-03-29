TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = writer
# writer.dbd will be created and installed
DBD += writer.dbd

# writer.dbd will be made up from these files:
writer_DBD += base.dbd

# Include dbd files from all support applications:
#writer_DBD += xxx.dbd

# Add all the support libraries needed by this IOC
#writer_LIBS += xxx

# writer_registerRecordDeviceDriver.cpp derives from writer.dbd
writer_SRCS += writer_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
writer_SRCS_DEFAULT += writerMain.cpp
writer_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#writer_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
writer_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

