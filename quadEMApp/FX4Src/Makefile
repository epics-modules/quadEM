TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += fx4QuadEM

DBD += drvFX4.dbd

# The following are compiled and added to the Support library
LIB_SRCS  += drvFX4.cpp

include $(ADCORE)/ADApp/commonLibraryMakefile
LIB_LIBS += quadEM

PROD += test_fx4_1
test_fx4_1_SRCS += test_fx4_1.cpp

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
