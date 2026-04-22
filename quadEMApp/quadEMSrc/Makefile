TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += quadEM

DBD += drvSoftQuadEM.dbd

INC += drvQuadEM.h

# The following are compiled and added to the Support library
quadEM_SRCS         += drvQuadEM.cpp
quadEM_SRCS         += drvSoftQuadEM.cpp

include $(ADCORE)/ADApp/commonLibraryMakefile

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
