TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += quadEM

DBD += drvAPS_EM.dbd
DBD += drvAHxxx.dbd
DBD += quadEMTestApp.dbd
DBD += quadEMTestAppVx.dbd

INC += drvQuadEM.h

# The following are compiled and added to the Support library
quadEM_SRCS         += drvQuadEM.cpp
quadEM_SRCS         += drvAHxxx.cpp
quadEM_SRCS         += quadEM_SNL.st
quadEM_SRCS         += four1.c
quadEM_SRCS_vxWorks += drvAPS_EM.cpp

# The following builds a test application
#==================================
PROD_IOC += quadEMTestApp
## <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
PROD_SRCS_DEFAULT += quadEMTestApp_registerRecordDeviceDriver.cpp quadEMTestAppMain.cpp
PROD_SRCS_vxWorks += quadEMTestAppVx_registerRecordDeviceDriver.cpp

PROD_LIBS += quadEM
PROD_LIBS_vxWorks += Ipac
PROD_LIBS_vxWorks += ipUnidig
PROD_LIBS += mca
PROD_LIBS += seq pv

include $(AREA_DETECTOR)/ADApp/commonDriverMakefile

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

