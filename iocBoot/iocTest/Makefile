TOP = ../..
include $(TOP)/configure/CONFIG
#ARCH = vxWorks-ppc32
#ARCH = linux-x86
ARCH = linux-x86_64
#ARCH = win32-x86
#ARCH=windows-x64-static
# vxWorks architecture needs a cdCommands file
buildInstall: cdCommands envPaths dllPath.bat

include $(TOP)/configure/RULES.ioc
