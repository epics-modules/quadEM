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
DBD += drvNSLS2_EM.dbd
#DBD += drvNSLS2_IC.dbd
DBD += drvPCR4.dbd
DBD += drvSoftQuadEM.dbd
DBD += quadEMTestApp.dbd

INC += drvQuadEM.h

# The following are compiled and added to the Support library
quadEM_SRCS         += drvQuadEM.cpp
quadEM_SRCS         += drvAHxxx.cpp
quadEM_SRCS         += drvTetrAMM.cpp
quadEM_SRCS         += drvNSLS_EM.cpp
quadEM_SRCS         += drvNSLS2_EM.cpp
#quadEM_SRCS         += drvNSLS2_IC.cpp
quadEM_SRCS         += drvPCR4.cpp
quadEM_SRCS         += drvSoftQuadEM.cpp

# Can only build drvNSLS2_EM for Windows and vxWorks in simulation mode
drvNSLS2_EM_CXXFLAGS_WIN32   += -DSIMULATION_MODE -DPOLLING_MODE
drvNSLS2_EM_CXXFLAGS_vxWorks += -DSIMULATION_MODE -DPOLLING_MODE
# If not building for the embedded system on Linux then uncomment the following line to also use simulation mode
drvNSLS2_EM_CXXFLAGS_Linux  += -DSIMULATION_MODE -DPOLLING_MODE

# Can only build drvNSLS2_IC for Windows and vxWorks in simulation mode
drvNSLS2_IC_CXXFLAGS_WIN32   += -DSIMULATION_MODE
drvNSLS2_IC_CXXFLAGS_vxWorks += -DSIMULATION_MODE
# If not building for the embedded system on Linux then uncomment the following line to also use simulation mode
drvNSLS2_IC_CXXFLAGS_Linux  += -DSIMULATION_MODE

# The following builds a test application
#==================================
PROD_NAME = quadEMTestApp
PROD_IOC += $(PROD_NAME)
## <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
PROD_SRCS_DEFAULT += $(PROD_NAME)_registerRecordDeviceDriver.cpp quadEMTestAppMain.cpp
PROD_SRCS_vxWorks += $(PROD_NAME)_registerRecordDeviceDriver.cpp

PROD_LIBS += quadEM
PROD_LIBS += sydorQuadEM

ifdef SNCSEQ
    PROD_LIBS += seq
    PROD_LIBS += pv
endif

PROD_LIBS += busy
PROD_LIBS += calc

PROD_LIBS += autosave
PROD_LIBS += devIocStats
PROD_LIBS += reccaster

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
$(PROD_NAME)_DBD += drvT4U_EM.dbd
$(PROD_NAME)_DBD += drvT4UDirect_EM.dbd

$(PROD_NAME)_DBD += NDFileNetCDF.dbd
$(PROD_NAME)_DBD += NDFileHDF5.dbd

$(PROD_NAME)_DBD += busySupport.dbd
$(PROD_NAME)_DBD += calcSupport.dbd
$(PROD_NAME)_DBD += devIocStats.dbd
$(PROD_NAME)_DBD += asSupport.dbd
$(PROD_NAME)_DBD += reccaster.dbd


# Link QSRV (pvAccess Server) if available
ifdef EPICS_QSRV_MAJOR_VERSION
    PROD_LIBS += qsrv
    PROD_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
    $(PROD_NAME)_DBD += PVAServerRegister.dbd
    $(PROD_NAME)_DBD += qsrv.dbd
endif

PROD_NAME = quadEMTestApp

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
