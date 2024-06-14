#!../../bin/linux-x86_64/tlbc2

#- You may have to change tlbc2 to something else
#- everywhere it appears in this file

#< envPaths

## Register all support components
dbLoadDatabase "../../dbd/tlbc2.dbd"
tlbc2_registerRecordDeviceDriver(pdbbase) 

## Load record instances
#dbLoadRecords("../../db/tlbc2.db","user=root")

iocInit()

## Start any sequence programs
#seq snctlbc2,"user=root"
