TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC_vxWorks += quadEM

DBD += quadEMSupport.dbd

INC += drvQuadEM.h

# The following are compiled and added to the Support library
quadEM_SRCS += drvQuadEM.c

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

