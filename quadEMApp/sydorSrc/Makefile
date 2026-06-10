TOP=../..
include $(TOP)/configure/CONFIG

LIBRARY_IOC += sydorQuadEM
USR_INCLUDES += -I../../src

DBD += drvT4U_EM.dbd
DBD += drvT4UDirect_EM.dbd

LIB_SRCS += drvT4U_EM.cpp
LIB_SRCS += drvT4UDirect_EM.cpp
LIB_SYS_LIBS_WIN32 += ws2_32

include $(ADCORE)/ADApp/commonLibraryMakefile
LIB_LIBS += quadEM

include $(TOP)/configure/RULES
