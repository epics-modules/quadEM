TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

DBD += quadEMTestApp.dbd

# The following builds a test application
#==================================
PROD_NAME = quadEMTestApp
PROD_IOC += $(PROD_NAME)
## <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
PROD_SRCS_DEFAULT += $(PROD_NAME)_registerRecordDeviceDriver.cpp quadEMTestAppMain.cpp
PROD_SRCS_vxWorks += $(PROD_NAME)_registerRecordDeviceDriver.cpp

PROD_LIBS += caenQuadEM
PROD_LIBS += nslsQuadEM
PROD_LIBS += sensicQuadEM
PROD_LIBS += sydorQuadEM
PROD_LIBS += fx4QuadEM
PROD_LIBS += quadEM

ifdef SNCSEQ
    PROD_LIBS += seq
    PROD_LIBS += pv
endif

# Need to put this in or there are missing symbols on vxWorks because Ipac and IpUnidig are linked
# in after PROD_LIBS, which has EPICS_BASE_IOC_LIBS from commonDriverMakefile
PROD_LIBS_vxWorks += $(EPICS_BASE_IOC_LIBS)

include $(ADCORE)/ADApp/commonLibraryMakefile
include $(ADCORE)/ADApp/commonDriverMakefile
$(PROD_NAME)_DBD += drvAsynIPPort.dbd
$(PROD_NAME)_DBD += drvAHxxx.dbd
$(PROD_NAME)_DBD += drvTetrAMM.dbd
$(PROD_NAME)_DBD += drvNSLS_EM.dbd
$(PROD_NAME)_DBD += drvNSLS2_EM.dbd
#$(PROD_NAME)_DBD += drvNSLS2_IC.dbd
$(PROD_NAME)_DBD += drvPCR4.dbd
$(PROD_NAME)_DBD += drvPCR4.dbd
$(PROD_NAME)_DBD += drvPCR4.dbd
$(PROD_NAME)_DBD += drvT4U_EM.dbd
$(PROD_NAME)_DBD += drvT4UDirect_EM.dbd
$(PROD_NAME)_DBD += drvFX4.dbd

PROD_NAME = quadEMTestApp

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
