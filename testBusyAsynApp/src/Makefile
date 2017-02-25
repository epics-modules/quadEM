TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

LIBRARY_IOC += testBusyAsyn
LIB_SRCS += testBusyAsyn.cpp
LIB_LIBS += busy asyn 
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)
PROD_IOC += testBusyAsyn
DBD      += testBusyAsyn.dbd
PROD_SRCS_DEFAULT += testBusyAsyn_registerRecordDeviceDriver.cpp testBusyAsynMain.cpp
PROD_SRCS_vxWorks  += testBusyAsyn_registerRecordDeviceDriver.cpp
PROD_LIBS += testBusyAsyn autosave busy asyn
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)
PROD_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
