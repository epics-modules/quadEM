TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC_vxWorks += quadEM

DBD += devQuadEM.dbd

# The following are compiled and added to the Support library
quadEM_SRCS += quadEM.cc
quadEM_SRCS += quadEMScan.cc
quadEM_SRCS += quadEMSweep.cc
quadEM_SRCS += quadEMPID.cc
quadEM_SRCS += devQuadEM.cc

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

