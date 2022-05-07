#!../../bin/linux-x86_64/simulator

< envPaths

dbLoadDatabase("../../dbd/simulator.dbd",0,0)
simulator_registerRecordDeviceDriver(pdbbase)

#   NUM_SAMPLES: 1000 (number of "compressed" samples)
#         COUNT: 1000 (number of signals, each signal has 'num_samp', 'min', 'max', 'mean', 'std' and 'rms' columns)
# TIME_STEP_SEC: 0.001 (time difference between rows, in seconds)
#                NUM_SAMPLES / TIME_STEP_SEC = simulated acquisition rate (1 MHz)
#      NUM_ROWS: 1000 (number of rows in each PV update = 1 second worth of data)
#dbLoadRecords("../../db/sim_table.db","P=SIM:,R=STAT,SCAN=1 second,COUNT=1000,NUM_SAMPLES=1000,CONFIG=16,TIME_STEP_SEC=0.001,NUM_ROWS=1000")

# 1 signal with 1000 compressed samples in each row, 100ms between rows, 1 sec per table update
dbLoadRecords("../../db/sim_table.db","P=SIM:,R=STAT,SCAN=1 second,COUNT=1,NUM_SAMPLES=1000,CONFIG=16,TIME_STEP_SEC=0.1,NUM_ROWS=10")

iocInit()

