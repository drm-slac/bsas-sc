#!../../bin/linux-x86_64/simulator

< envPaths

dbLoadDatabase("../../dbd/simulator.dbd",0,0)
simulator_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/sim_table.db","P=SIM:,R=TABLE,SCAN=1 second,COUNT=2,COLUMNS=0x00,TIME_STEP_SEC=0.001,NUM_ROWS=5")

iocInit()

