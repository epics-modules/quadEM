TOP=../..
include $(TOP)/configure/CONFIG

LIBRARY_IOC += quadEM


DBD += drvT4U_EM.dbd
DBD += drvT4UDirect_EM.dbd

quadEM_SRCS += drvT4U_EM.cpp
quadEM_SRCS += drvT4UDirect_EM.cpp

PROD_NAME = quadEMTestApp
PROD_IOC += $(PROD_NAME)

PROD_LIBS += quadEM

include $(ADCORE)/ADApp/commonLibraryMakefile
include $(ADCORE)/ADApp/commonDriverMakefile
$(PROD_NAME)_DBD += drvT4U_EM.dbd
$(PROD_NAME)_DBD += drvT4UDirect_EM.dbd

PROD_NAME = quadEMTestApp

include $(TOP)/configure/RULES
