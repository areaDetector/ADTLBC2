# Camera driver configuration
#
# The following parameters must be defined before loading this configuration:
#
# $(PREFIX)
# Prefix for all records.
#
# $(PORT)
# The port name for the detector.
#
# $(MAX_IMAGE_WIDTH)
# The maximum image width.
#
# $(MAX_IMAGE_HEIGHT)
# The maximum image height.

dbLoadDatabase "../../dbd/tlbc2.dbd"
tlbc2_registerRecordDeviceDriver(pdbbase)

epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db;$(ADTLBC2)/db")

TLBC2Config("$(PORT)", "$(MAX_IMAGE_WIDTH)", "$(MAX_IMAGE_HEIGHT)", 0, 0)
dbLoadRecords("BC210CU.template", "P=$(PREFIX), R=cam1:, PORT=$(PORT), ADDR=0, TIMEOUT=1")
