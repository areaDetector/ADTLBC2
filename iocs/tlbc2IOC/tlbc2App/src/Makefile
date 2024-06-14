TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = tlbc2
# tlbc2.dbd will be created and installed
DBD += tlbc2.dbd

# tlbc2.dbd will be made up from these files:
tlbc2_DBD += base.dbd

# Include dbd files from all support applications:
#tlbc2_DBD += xxx.dbd

# Add all the support libraries needed by this IOC
#tlbc2_LIBS += xxx

# tlbc2_registerRecordDeviceDriver.cpp derives from tlbc2.dbd
tlbc2_SRCS += tlbc2_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
tlbc2_SRCS_DEFAULT += tlbc2Main.cpp
tlbc2_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#tlbc2_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
tlbc2_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

