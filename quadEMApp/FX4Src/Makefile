TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

LIBRARY_IOC += fx4QuadEM

USR_INCLUDES += -I../ixwebsocket/include
USR_INCLUDES += -I../msgpack-c/include

USR_CPPFLAGS_WIN32 += /std:c++17 -DNOMINMAX

DBD += drvFX4.dbd

LIB_SRCS += drvFX4.cpp

SRC_DIRS += ../ixwebsocket
LIB_SRCS += IXBench.cpp
LIB_SRCS += IXCancellationRequest.cpp
LIB_SRCS += IXConnectionState.cpp
LIB_SRCS += IXDNSLookup.cpp
LIB_SRCS += IXExponentialBackoff.cpp
#LIB_SRCS += IXGetFreePort.cpp
LIB_SRCS += IXGzipCodec.cpp
LIB_SRCS += IXHttpClient.cpp
LIB_SRCS += IXHttp.cpp
LIB_SRCS += IXHttpServer.cpp
LIB_SRCS += IXNetSystem.cpp
LIB_SRCS += IXSelectInterrupt.cpp
LIB_SRCS += IXSelectInterruptEvent.cpp
LIB_SRCS += IXSelectInterruptFactory.cpp
LIB_SRCS += IXSelectInterruptPipe.cpp
LIB_SRCS += IXSetThreadName.cpp
LIB_SRCS += IXSocketAppleSSL.cpp
LIB_SRCS += IXSocketConnect.cpp
LIB_SRCS += IXSocket.cpp
LIB_SRCS += IXSocketFactory.cpp
LIB_SRCS += IXSocketMbedTLS.cpp
LIB_SRCS += IXSocketOpenSSL.cpp
LIB_SRCS += IXSocketServer.cpp
LIB_SRCS += IXSocketTLSOptions.cpp
LIB_SRCS += IXStrCaseCompare.cpp
LIB_SRCS += IXUdpSocket.cpp
LIB_SRCS += IXUrlParser.cpp
LIB_SRCS += IXUserAgent.cpp
LIB_SRCS += IXUuid.cpp
LIB_SRCS += IXWebSocketCloseConstants.cpp
LIB_SRCS += IXWebSocket.cpp
LIB_SRCS += IXWebSocketHandshake.cpp
LIB_SRCS += IXWebSocketHttpHeaders.cpp
LIB_SRCS += IXWebSocketPerMessageDeflateCodec.cpp
LIB_SRCS += IXWebSocketPerMessageDeflate.cpp
LIB_SRCS += IXWebSocketPerMessageDeflateOptions.cpp
LIB_SRCS += IXWebSocketProxyServer.cpp
LIB_SRCS += IXWebSocketServer.cpp
LIB_SRCS += IXWebSocketTransport.cpp

include $(ADCORE)/ADApp/commonLibraryMakefile
LIB_LIBS += quadEM

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

