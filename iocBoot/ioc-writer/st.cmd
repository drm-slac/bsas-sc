#!../../bin/linux-x86_64/writer

#- You may have to change writer to something else
#- everywhere it appears in this file

#< envPaths

## Register all support components
dbLoadDatabase("../../dbd/writer.dbd",0,0)
writer_registerRecordDeviceDriver(pdbbase) 

## Load record instances
dbLoadRecords("../../db/writer.db","user=bmartins")

iocInit()

## Start any sequence programs
#seq sncwriter,"user=bmartins"
