TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += sensicQuadEM

DBD += drvPCR4.dbd

sensicQuadEM_SRCS         += drvPCR4.cpp

include $(ADCORE)/ADApp/commonLibraryMakefile
LIB_LIBS += quadEM

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
