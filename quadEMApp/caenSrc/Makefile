TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += caenQuadEM

DBD += drvAHxxx.dbd
DBD += drvTetrAMM.dbd

# The following are compiled and added to the Support library
LIB_SRCS         += drvAHxxx.cpp
LIB_SRCS         += drvTetrAMM.cpp

include $(ADCORE)/ADApp/commonLibraryMakefile
LIB_LIBS += quadEM

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
