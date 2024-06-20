#!../../bin/linux-x86_64/tlbc2

< envPaths

epicsEnvSet("PREFIX", "CED:A:TLBC2:")
epicsEnvSet("PORT", "TLBC2")
epicsEnvSet("MAX_IMAGE_WIDTH", "4096")
epicsEnvSet("MAX_IMAGE_HEIGHT", "3000")

< camera.cmd

epicsEnvSet("MAX_IMAGE_PIXELS", "12288000")

< plugins.cmd

iocInit()
