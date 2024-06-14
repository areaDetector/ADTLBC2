TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += TLBC2

# xxxRecord.h will be created from xxxRecord.dbd
#DBDINC += xxxRecord
# install TLBC2.dbd into <top>/dbd
DBD += TLBC2.dbd

# specify all source files to be compiled and added to the library
#TLBC2_SRCS += xxx

TLBC2_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

