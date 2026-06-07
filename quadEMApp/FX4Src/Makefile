TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

LIBRARY_IOC += fx4QuadEM

DBD += drvFX4.dbd

LIB_SRCS += drvFX4.cpp

include $(ADCORE)/ADApp/commonLibraryMakefile
LIB_LIBS += quadEM

USR_INCLUDES += -I$(IXWEBSOCKET)/include
USR_INCLUDES += -I$(NLOHMANN_JSON)/include

USR_CPPFLAGS_WIN32 += /std:c++17 -DNOMINMAX
USR_LDFLAGS += -L$(IXWEBSOCKET_LIB)
LIB_SYS_LIBS += ixwebsocket

ifeq ($(OS_CLASS),WIN32)
    USR_CPPFLAGS += -D_CRT_SECURE_NO_WARNINGS
    LIB_SYS_LIBS += ws2_32
    LIB_SYS_LIBS += crypt32
    LIB_SYS_LIBS += bcrypt
else
    LIB_SYS_LIBS += pthread
endif

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

