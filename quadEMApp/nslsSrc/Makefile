TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += nslsQuadEM

DBD += drvNSLS_EM.dbd
DBD += drvNSLS2_EM.dbd
#DBD += drvNSLS2_IC.dbd
nslsQuadEM_SRCS         += drvNSLS_EM.cpp
nslsQuadEM_SRCS         += drvNSLS2_EM.cpp
#nslsQuadEM_SRCS         += drvNSLS2_IC.cpp

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

include $(ADCORE)/ADApp/commonLibraryMakefile
LIB_LIBS += quadEM

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
