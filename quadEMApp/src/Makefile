TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += quadEM

DBD += drvAPS_EM.dbd
DBD += drvAH401B.dbd
DBD += quadEMTestApp.dbd

INC += drvQuadEM.h

# The following are compiled and added to the Support library
quadEM_SRCS         += drvQuadEM.cpp
quadEM_SRCS         += drvAH401B.cpp
quadEM_SRCS         += quadEM_SNL.st
quadEM_SRCS_vxWorks += drvAPS_EM.cpp

# The following builds a test application
#==================================
PROD_IOC += quadEMTestApp
## <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
PROD_SRCS += quadEMTestApp_registerRecordDeviceDriver.cpp
PROD_SRCS_DEFAULT += quadEMTestAppMain.cpp
PROD_SRCS_vxWorks += -nil-
PROD_LIBS += quadEM
PROD_LIBS += mca
PROD_LIBS += std
PROD_LIBS += busy
PROD_LIBS += calc 
PROD_LIBS += autosave
PROD_LIBS += asyn
PROD_LIBS += seq pv
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

