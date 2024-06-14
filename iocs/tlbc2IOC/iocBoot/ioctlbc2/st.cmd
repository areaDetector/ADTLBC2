#!../../bin/linux-x86_64/tlbc2

< envPaths

## Register all support components
dbLoadDatabase "../../dbd/tlbc2.dbd"
tlbc2_registerRecordDeviceDriver(pdbbase)

epicsEnvSet("PREFIX", "CED:A:TLBC2:")
epicsEnvSet("PORT", "TLBC2")

# The search path for database files
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db")

TLBC2Config("$(PORT)", 4096, 3000, 0)
dbLoadRecords("ADBase.template", "P=$(PREFIX), R=cam1:, PORT=$(PORT), ADDR=0, TIMEOUT=1")

NDStdArraysConfigure("Image1", 20, 0, "$(PORT)", 0, 0, 0, 0, 0, 5)
dbLoadRecords("NDStdArrays.template", "P=$(PREFIX), R=image1:, PORT=Image1, ADDR=0, TIMEOUT=1, NDARRAY_PORT=$(PORT), TYPE=Int16, FTVL=USHORT, NELEMENTS=12288000")

iocInit()
