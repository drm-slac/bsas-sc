#!../../bin/linux-x86_64/stacker

< envPaths

dbLoadDatabase("../../dbd/stacker.dbd",0,0)
stacker_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/stack_0016.db","P=SIM:,R=TABLE:,INPP=SIM:,INPR=SCALAR:,CONFIG=0x02,PERIOD=1.0")

iocInit()

#pvxsl

