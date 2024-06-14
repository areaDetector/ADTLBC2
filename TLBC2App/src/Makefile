TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += TLBC2

# install TLBC2.dbd into <top>/dbd
DBD += TLBC2.dbd

# specify all source files to be compiled and added to the library
TLBC2_SRCS += TLBC2.cpp

USR_INCLUDES += -I$(THORLABS_INC)

TLBC2_64_DIR += $(THORLABS_LIB)
TLBC2_DLL_LIBS += TLBC2_64

include $(ADCORE)/ADApp/commonLibraryMakefile

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

