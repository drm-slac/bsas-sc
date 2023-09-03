TOP=../..

include $(TOP)/configure/CONFIG

PROD = writer

writer_LIBS += pvxs Com
writer_LIBS += common nttable

writer_SRCS += writerMain.cpp writer.cpp

# HDF5 dependency (a bit hacky)
HDF5_L = $(shell pkg-config --libs-only-l hdf5)
HDF5 = $(HDF5_L:-l%=%)
writer_SYS_LIBS += $(HDF5)
USR_INCLUDES += $(shell pkg-config --cflags-only-I hdf5)
USR_LDFLAGS += $(shell pkg-config --libs-only-L hdf5)

include $(TOP)/configure/RULES
