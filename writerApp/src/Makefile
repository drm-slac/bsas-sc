TOP=../..

include $(TOP)/configure/CONFIG

# =====================================================
# Path to "NON EPICS" External PACKAGES: USER INCLUDES
# =====================================================
USR_INCLUDES += -I$(HDF5_INCLUDE)
USR_INCLUDES += -I$(ZLIB_INCLUDE)

#======================================================
# PATH TO "NON EPICS" EXTERNAL PACKAGES: USER LIBRARIES
#======================================================
hdf5_DIR  = $(HDF5_LIB)
z_DIR  = $(ZLIB_LIB)

# ======================================================
# LINK "NON EPICS" EXTERNAL PACKAGE LIBRARIES STATICALLY
# =======================================================
USR_LIBS_Linux += hdf5 z
USR_LIBS_Linux += z

# ======================================================
# Host Application
# ======================================================
PROD_HOST = writer
PROD = writer

writer_LIBS += pvxs Com
writer_LIBS += common nttable

writer_SRCS += writerMain.cpp writer.cpp

# HDF5 dependency (a bit hacky)
#HDF5_L = $(shell pkg-config --libs-only-l hdf5)
#HDF5 = $(HDF5_L:-l%=%)
#writer_SYS_LIBS += $(HDF5)
#USR_INCLUDES += $(shell pkg-config --cflags-only-I hdf5)
#USR_LDFLAGS += $(shell pkg-config --libs-only-L hdf5)

include $(TOP)/configure/RULES
