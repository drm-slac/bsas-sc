#!../../bin/linux-x86_64/simulator

< envPaths

dbLoadDatabase("../../dbd/simulator.dbd",0,0)
simulator_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/sim_table.db","P=SIM:,R=STAT,SCAN=1 second,COUNT=1000,NUM_SAMPLES=1000,CONFIG=16,TIME_STEP_SEC=0.001,NUM_ROWS=1000")

iocInit()

