TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = tlbc2
PROD_NAME = $(PROD_IOC)
# tlbc2.dbd will be created and installed
DBD += tlbc2.dbd

# tlbc2.dbd will be made up from these files:
tlbc2_DBD += base.dbd

# Include dbd files from all support applications:
tlbc2_DBD += TLBC2.dbd

# Add all the support libraries needed by this IOC
tlbc2_LIBS += TLBC2

# tlbc2_registerRecordDeviceDriver.cpp derives from tlbc2.dbd
tlbc2_SRCS += tlbc2_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
tlbc2_SRCS_DEFAULT += tlbc2Main.cpp

include $(ADCORE)/ADApp/commonDriverMakefile

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

