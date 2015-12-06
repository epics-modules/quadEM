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
DBD += drvTetrAMM.dbd
DBD += drvNSLS_EM.dbd
DBD += quadEMTestApp.dbd
DBD += quadEMTestAppVx.dbd

INC += drvQuadEM.h

# The following are compiled and added to the Support library
quadEM_SRCS         += drvQuadEM.cpp
quadEM_SRCS         += drvAHxxx.cpp
quadEM_SRCS         += drvTetrAMM.cpp
quadEM_SRCS         += drvNSLS_EM.cpp
quadEM_SRCS         += quadEM_SNL.st
quadEM_SRCS         += four1.c
quadEM_SRCS_vxWorks += drvAPS_EM.cpp

quadEM_LIBS_vxWorks += Ipac
quadEM_LIBS_vxWorks += ipUnidig
quadEM_LIBS         += mca
quadEM_LIBS         += seq pv


# The following builds a test application
#==================================
PROD_NAME = quadEMTestApp
PROD_IOC += $(PROD_NAME)
## <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
PROD_SRCS_DEFAULT += $(PROD_NAME)_registerRecordDeviceDriver.cpp quadEMTestAppMain.cpp
PROD_SRCS_vxWorks += $(PROD_NAME)Vx_registerRecordDeviceDriver.cpp

PROD_LIBS += quadEM
PROD_LIBS_vxWorks += Ipac
PROD_LIBS_vxWorks += ipUnidig
PROD_LIBS += mca
PROD_LIBS += seq pv
# Need to put this in or there are missing symbols on vxWorks because Ipac and IpUnidig are linked
# in after PROD_LIBS, which has EPICS_BASE_IOC_LIBS from commonDriverMakefile
PROD_LIBS_vxWorks += $(EPICS_BASE_IOC_LIBS)

include $(ADCORE)/ADApp/commonLibraryMakefile
include $(ADCORE)/ADApp/commonDriverMakefile

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

