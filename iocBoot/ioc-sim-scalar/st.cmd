#!../../bin/linux-x86_64/simulator

< envPaths

dbLoadDatabase("$(TOP)/dbd/simulator.dbd",0,0)
simulator_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("$(TOP)/db/sim_0016.db","P=SIM:,R=SCALAR:,SCAN=100 Hz")

iocInit()

